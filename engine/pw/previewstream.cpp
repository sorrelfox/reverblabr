// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "pw/previewstream.h"

#include "pw/connection.h"
#include "pw/logging.h"

#include <QMetaObject>
#include <pipewire/keys.h>
#include <pipewire/pipewire.h>
#include <pipewire/stream.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/audio/raw.h>
#include <spa/param/format.h>
#include <spa/param/param.h>
#include <spa/pod/builder.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>

using namespace Qt::StringLiterals;

namespace rb::pw
{

namespace
{

/// The channel positions a file of \a channels is played as: the one mono
/// channel, a stereo pair, or auxiliary channels that the output's own
/// conversion places.
void setPositions(std::uint32_t channels, std::array<std::uint32_t, SPA_AUDIO_MAX_CHANNELS> &positions)
{
    if (channels == 1) {
        positions[0] = SPA_AUDIO_CHANNEL_MONO;
        return;
    }
    if (channels == 2) {
        positions[0] = SPA_AUDIO_CHANNEL_FL;
        positions[1] = SPA_AUDIO_CHANNEL_FR;
        return;
    }
    for (std::uint32_t channel = 0; channel < channels && channel < SPA_AUDIO_MAX_CHANNELS; ++channel) {
        positions[channel] = SPA_AUDIO_CHANNEL_AUX0 + channel;
    }
}

/// `pw_stream_dequeue_buffer()` and `pw_stream_queue_buffer()` take a
/// buffer from the stream's own queue and give it back: no allocation, no
/// lock, no system call, and every PipeWire player calls them from its
/// process callback. The compiler cannot know that, so the claim is made
/// here, as the session node makes it for its own one call.
[[nodiscard]] pw_buffer *dequeue(pw_stream *stream) noexcept RB_NONBLOCKING
{
    using Dequeue = pw_buffer *(*)(pw_stream *)RB_NONBLOCKING;
    const auto call = reinterpret_cast<Dequeue>(&pw_stream_dequeue_buffer);
    return call(stream);
}

void enqueue(pw_stream *stream, pw_buffer *buffer) noexcept RB_NONBLOCKING
{
    using Queue = int (*)(pw_stream *, pw_buffer *) RB_NONBLOCKING;
    const auto call = reinterpret_cast<Queue>(&pw_stream_queue_buffer);
    (void)call(stream, buffer);
}

} // namespace

class PreviewStream::Private
{
public:
    explicit Private(PreviewStream *owner, Connection &connection)
        : q(owner)
        , connection(connection)
    {
    }

    PreviewStream *q;
    Connection &connection;
    pw_stream *stream = nullptr;
    spa_hook listener{};
    /// Read by the audio thread only; set before the stream is connected
    /// and cleared after it is destroyed.
    rt::AudioTap *source = nullptr;
    std::uint32_t channels = 0;
    std::atomic<std::uint64_t> starved{0};

    /// One cycle, on PipeWire's data thread: as much of the tap as the
    /// buffer wants, and silence for whatever the tap has not got.
    static void onProcess(void *data) noexcept RB_NONBLOCKING
    {
        auto *self = static_cast<Private *>(data);
        pw_buffer *buffer = dequeue(self->stream);
        if (buffer == nullptr) {
            return;
        }
        spa_data &out = buffer->buffer->datas[0];
        if (out.data == nullptr || self->channels == 0) {
            enqueue(self->stream, buffer);
            return;
        }
        const std::uint32_t stride = static_cast<std::uint32_t>(sizeof(float)) * self->channels;
        std::uint64_t frames = out.maxsize / stride;
        if (buffer->requested > 0) {
            frames = std::min<std::uint64_t>(frames, buffer->requested);
        }
        auto *samples = static_cast<float *>(out.data);
        const std::size_t got =
            self->source->read(std::span<float>(samples, static_cast<std::size_t>(frames) * self->channels), frames);
        if (got < frames) {
            std::fill_n(samples + got * self->channels, (frames - got) * self->channels, 0.0f);
            self->starved.fetch_add(frames - got, std::memory_order_relaxed);
        }
        out.chunk->offset = 0;
        out.chunk->stride = static_cast<std::int32_t>(stride);
        out.chunk->size = static_cast<std::uint32_t>(frames * stride);
        enqueue(self->stream, buffer);
    }

    static void onStateChanged(void *data, pw_stream_state, pw_stream_state state, const char *error) noexcept
    {
        auto *self = static_cast<Private *>(data);
        if (state == PW_STREAM_STATE_ERROR || state == PW_STREAM_STATE_UNCONNECTED) {
            const QString reason = error != nullptr ? QString::fromUtf8(error) : u"unconnected"_s;
            // Said once the loop has come round, never from inside
            // PipeWire's own callback, which somebody may answer by
            // stopping the stream.
            QMetaObject::invokeMethod(
                self->q, [owner = self->q, reason] { Q_EMIT owner->failed(reason); }, Qt::QueuedConnection);
        }
    }

    void destroy()
    {
        if (stream != nullptr) {
            // **Not listened to while it is taken down**: the disconnect
            // below says "unconnected" like a server that went, and a stop
            // of ours is not a failure — a seek, which starts a new stream
            // straight after, would be killed by it.
            spa_hook_remove(&listener);
            pw_stream_disconnect(stream);
            pw_stream_destroy(stream);
            stream = nullptr;
        }
        source = nullptr;
    }
};

PreviewStream::PreviewStream(Connection &connection, QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>(this, connection))
{
}

PreviewStream::~PreviewStream()
{
    d->destroy();
}

bool PreviewStream::start(rt::AudioTap &source, std::uint32_t sampleRate, const QString &title)
{
    stop();
    if (!d->connection.isConnected() || d->connection.core() == nullptr || sampleRate == 0) {
        return false;
    }
    d->source = &source;
    d->channels = source.channelCount();
    d->starved.store(0, std::memory_order_relaxed);

    pw_properties *props = pw_properties_new(PW_KEY_MEDIA_TYPE,
                                             "Audio",
                                             PW_KEY_MEDIA_CATEGORY,
                                             "Playback",
                                             // **A player's role**, so the session manager treats
                                             // it as one: linked to the default output, shown in
                                             // the audio applet, its volume the user's to change.
                                             PW_KEY_MEDIA_ROLE,
                                             "Music",
                                             PW_KEY_NODE_NAME,
                                             kNodeName,
                                             PW_KEY_APP_NAME,
                                             "Reverblabr",
                                             PW_KEY_APP_ICON_NAME,
                                             "io.github.reverblabr.Reverblabr",
                                             nullptr);
    pw_properties_set(props, PW_KEY_MEDIA_NAME, title.toUtf8().constData());
    pw_properties_set(props, PW_KEY_NODE_DESCRIPTION, title.toUtf8().constData());

    static const pw_stream_events events = [] {
        pw_stream_events result{};
        result.version = PW_VERSION_STREAM_EVENTS;
        result.process = &Private::onProcess;
        result.state_changed = &Private::onStateChanged;
        return result;
    }();

    d->stream = pw_stream_new(d->connection.core(), kNodeName, props);
    if (d->stream == nullptr) {
        qCWarning(RB_PW) << "the server would not make the preview stream";
        d->source = nullptr;
        return false;
    }
    pw_stream_add_listener(d->stream, &d->listener, &events, d.get());

    spa_audio_info_raw info{};
    info.format = SPA_AUDIO_FORMAT_F32;
    info.rate = sampleRate;
    info.channels = std::min<std::uint32_t>(d->channels, SPA_AUDIO_MAX_CHANNELS);
    std::array<std::uint32_t, SPA_AUDIO_MAX_CHANNELS> positions{};
    setPositions(info.channels, positions);
    std::copy_n(positions.begin(), info.channels, info.position);

    std::array<std::uint8_t, 1024> storage{};
    // Not SPA_POD_BUILDER_INIT, which is a C compound literal.
    spa_pod_builder builder{};
    spa_pod_builder_init(&builder, storage.data(), static_cast<std::uint32_t>(storage.size()));
    const spa_pod *params[1] = {spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info)};

    const auto flags = static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS
                                                    | PW_STREAM_FLAG_RT_PROCESS);
    if (const int error = pw_stream_connect(d->stream, PW_DIRECTION_OUTPUT, PW_ID_ANY, flags, params, 1); error < 0) {
        qCWarning(RB_PW) << "the preview stream could not be connected:" << strerror(-error);
        d->destroy();
        return false;
    }
    return true;
}

void PreviewStream::setActive(bool active)
{
    if (d->stream != nullptr) {
        pw_stream_set_active(d->stream, active);
    }
}

void PreviewStream::stop()
{
    d->destroy();
}

bool PreviewStream::isRunning() const noexcept
{
    return d->stream != nullptr;
}

std::uint32_t PreviewStream::nodeId() const noexcept
{
    if (d->stream == nullptr) {
        return 0;
    }
    // SPA_ID_INVALID until the server has said which node the stream is.
    const std::uint32_t id = pw_stream_get_node_id(d->stream);
    return id == SPA_ID_INVALID ? 0 : id;
}

std::uint64_t PreviewStream::starvedFrames() const noexcept
{
    return d->starved.load(std::memory_order_relaxed);
}

} // namespace rb::pw
