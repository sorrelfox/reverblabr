// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/recordingplayer.h"

#include "pw/connection.h"
#include "pw/previewstream.h"
#include "record/audiofilereader.h"
#include "rt/audiotap.h"

#include <QFileInfo>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <optional>
#include <thread>
#include <vector>

using namespace Qt::StringLiterals;

namespace rb::core
{

namespace
{

/// How far ahead of the stream the decoding thread keeps the ring: enough
/// to ride out a disk that stalls, little enough that a seek throws away
/// nothing anybody would miss.
constexpr double kAheadSeconds = 2.0;

/// How much is read from the file at a time.
constexpr std::size_t kChunkFrames = 4096;

} // namespace

class RecordingPlayer::Private
{
public:
    Private(RecordingPlayer *owner, pw::Connection &connection)
        : q(owner)
        , connection(connection)
        , stream(connection)
    {
        tick.setInterval(kTickMs);
    }

    ~Private()
    {
        stopDecoding();
        stream.stop();
    }

    RecordingPlayer *q;
    pw::Connection &connection;
    pw::PreviewStream stream;
    QTimer tick;

    QString path;
    QLatin1StringView state = previewState::stopped;
    std::optional<record::AudioFileReader> reader;
    std::unique_ptr<rt::AudioTap> tap;
    std::jthread decoder;
    std::atomic<bool> endOfFile{false};
    std::uint64_t startFrame = 0;
    /// Where a stopped or paused preview stands, in frames.
    std::uint64_t heldFrame = 0;

    [[nodiscard]] std::uint32_t rate() const
    {
        return reader ? reader->sampleRate() : 0;
    }

    [[nodiscard]] double duration() const
    {
        return reader && reader->sampleRate() > 0 ? static_cast<double>(reader->frames()) / reader->sampleRate() : 0.0;
    }

    [[nodiscard]] std::uint64_t position() const
    {
        if (tap && stream.isRunning()) {
            return startFrame + tap->framesRead();
        }
        return heldFrame;
    }

    void stopDecoding()
    {
        if (decoder.joinable()) {
            decoder.request_stop();
            decoder.join();
        }
    }

    /// Starts the stream again from \a frame, playing if \a active.
    bool startAt(std::uint64_t frame, bool active)
    {
        stopDecoding();
        stream.stop();
        if (!reader) {
            return false;
        }
        startFrame = reader->seek(frame);
        heldFrame = startFrame;
        const std::uint32_t channels = std::min<std::uint32_t>(reader->channels(), rt::AudioTap::kMaxChannels);
        tap = std::make_unique<rt::AudioTap>(
            channels, static_cast<std::size_t>(kAheadSeconds * reader->sampleRate()) + kChunkFrames);
        endOfFile.store(false, std::memory_order_relaxed);

        // **The decoding thread keeps the ring full** and does nothing else.
        // The reader is its alone until it is joined.
        decoder = std::jthread([this](const std::stop_token &token) {
            const std::uint32_t fileChannels = reader->channels();
            const std::uint32_t tapChannels = tap->channelCount();
            std::vector<float> interleaved(kChunkFrames * fileChannels);
            std::vector<std::vector<float>> planar(tapChannels, std::vector<float>(kChunkFrames));
            std::vector<const float *> pointers(tapChannels);
            for (std::uint32_t channel = 0; channel < tapChannels; ++channel) {
                pointers[channel] = planar[channel].data();
            }
            while (!token.stop_requested()) {
                if (tap->capacityFrames() - tap->readableFrames() < kChunkFrames) {
                    std::this_thread::sleep_for(std::chrono::milliseconds{10});
                    continue;
                }
                const std::size_t got = reader->read(interleaved, kChunkFrames);
                if (got == 0) {
                    endOfFile.store(true, std::memory_order_release);
                    return;
                }
                for (std::size_t frame = 0; frame < got; ++frame) {
                    for (std::uint32_t channel = 0; channel < tapChannels; ++channel) {
                        planar[channel][frame] = interleaved[frame * fileChannels + channel];
                    }
                }
                (void)tap->write(pointers, static_cast<std::uint32_t>(got));
            }
        });

        if (!stream.start(*tap, reader->sampleRate(), QFileInfo(path).fileName())) {
            stopDecoding();
            return false;
        }
        stream.setActive(active);
        return true;
    }

    void setState(QLatin1StringView next)
    {
        state = next;
        if (state == previewState::playing) {
            tick.start();
        } else {
            tick.stop();
        }
        Q_EMIT q->changed(q->status());
    }

    void finish(std::uint64_t at)
    {
        stopDecoding();
        stream.stop();
        tap.reset();
        heldFrame = at;
        setState(previewState::stopped);
    }

    void fail(const QString &reason)
    {
        const QString failedPath = path;
        finish(0);
        Q_EMIT q->failed(failedPath, reason);
    }
};

RecordingPlayer::RecordingPlayer(pw::Connection &connection, QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>(this, connection))
{
    connect(&d->tick, &QTimer::timeout, this, [this] {
        // **The end is when the stream has played the last frame**, not
        // when the file has been read: the ring is still a few seconds deep
        // then.
        if (d->endOfFile.load(std::memory_order_acquire) && d->tap && d->tap->readableFrames() == 0) {
            d->finish(d->reader ? d->reader->frames() : 0);
            return;
        }
        Q_EMIT changed(status());
    });
    connect(&d->stream, &pw::PreviewStream::failed, this, [this](const QString &reason) {
        if (d->state != previewState::stopped) {
            d->fail(reason);
        }
    });
}

RecordingPlayer::~RecordingPlayer() = default;

void RecordingPlayer::play(const QString &path)
{
    if (path == d->path && d->reader) {
        if (d->state == previewState::playing) {
            Q_EMIT changed(status());
            return;
        }
        if (d->state == previewState::paused && d->stream.isRunning()) {
            d->stream.setActive(true);
            d->setState(previewState::playing);
            return;
        }
    }
    d->stopDecoding();
    d->stream.stop();
    const bool same = path == d->path && d->reader;
    // A stopped file starts again from where it was left, unless that was
    // its end.
    std::uint64_t from = same ? d->heldFrame : 0;
    if (!same) {
        d->path = path;
        d->reader = record::AudioFileReader::open(std::filesystem::path{path.toStdString()});
        d->heldFrame = 0;
        if (!d->reader) {
            d->fail(u"unreadable"_s);
            return;
        }
    }
    if (from >= d->reader->frames()) {
        from = 0;
    }
    if (!d->connection.isConnected()) {
        d->fail(u"noAudioSystem"_s);
        return;
    }
    if (!d->startAt(from, true)) {
        d->fail(u"noAudioSystem"_s);
        return;
    }
    d->setState(previewState::playing);
}

void RecordingPlayer::pause()
{
    if (d->state != previewState::playing) {
        return;
    }
    d->stream.setActive(false);
    d->heldFrame = d->position();
    d->setState(previewState::paused);
}

void RecordingPlayer::seek(double seconds)
{
    if (!d->reader || d->rate() == 0) {
        return;
    }
    const auto frame = static_cast<std::uint64_t>(std::llround(std::max(0.0, seconds) * d->rate()));
    const bool playing = d->state == previewState::playing;
    if (!d->connection.isConnected() || !d->startAt(frame, playing)) {
        d->fail(u"noAudioSystem"_s);
        return;
    }
    d->setState(playing ? previewState::playing : previewState::paused);
}

void RecordingPlayer::stop()
{
    if (d->state == previewState::stopped && !d->stream.isRunning()) {
        return;
    }
    d->finish(0);
}

QJsonObject RecordingPlayer::status() const
{
    const double rate = d->rate();
    return QJsonObject{
        {u"path"_s, d->path},
        {u"state"_s, QString(d->state)},
        {u"seconds"_s, rate > 0 ? static_cast<double>(d->position()) / rate : 0.0},
        {u"duration"_s, d->duration()},
    };
}

std::uint32_t RecordingPlayer::streamNodeId() const
{
    return d->stream.nodeId();
}

} // namespace rb::core
