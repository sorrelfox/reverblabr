// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "stream/livestream.h"

#include "record/resampler.h"
#include "rt/audiotap.h"
#include "stream/rtmpoutput.h"
#include "stream/streamencoder.h"
#include "stream/textpainter.h"
#include "stream/visuals.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <utility>
#include <vector>

namespace rb::stream
{

namespace
{

using Clock = std::chrono::steady_clock;
using State = BroadcastStatus::State;

/// The most frames taken from the tap at a time: a frame of video at 30
/// frames a second is 1600 of them at 48 kHz, so the picture is drawn
/// from sound no older than this.
constexpr std::size_t kChunkFrames = 1024;

/// How far back the rate the bytes go out at is measured.
constexpr std::chrono::milliseconds kRateWindow{5000};

/// **How much sound may be waiting before frames are left out**: half a
/// second. More than that and the encoder is not keeping up; the sound
/// must not wait for the picture.
constexpr double kBehindSeconds = 0.5;

/// Whether a change has to be told to the server, or to the encoders,
/// which means connecting again.
[[nodiscard]] bool needsReconnect(const LiveStreamSettings &was, const LiveStreamSettings &now)
{
    return was.url != now.url || was.quality != now.quality || was.encoder != now.encoder;
}

} // namespace

LiveQuality liveQuality(LiveQualityPreset preset) noexcept
{
    switch (preset) {
    case LiveQualityPreset::Good:
        return LiveQuality{1024, 576, 15, 800, 96};
    case LiveQualityPreset::Higher:
        return LiveQuality{1280, 720, 30, 2500, 128};
    case LiveQualityPreset::Best:
        return LiveQuality{1920, 1080, 30, 4500, 320};
    }
    return LiveQuality{};
}

/// What the callers and the thread share, under the mutex.
struct LiveStream::Shared {
    std::shared_ptr<rt::AudioTap> tap;
    StatusSink sink;
    LiveStreamOptions options;

    std::mutex mutex;
    std::condition_variable wake;
    std::optional<LiveStreamSettings> settings;
    std::uint64_t settingsVersion = 0;
    bool keyKnown = false;
    bool keyUnavailable = false;
    std::optional<std::string> key;
    std::uint64_t keyVersion = 0;
    std::optional<Track> handedTrack;
    std::uint64_t trackVersion = 0;
    LiveStreamStatus said;

    std::atomic<bool> stopping{false};
};

LiveStream::LiveStream(std::shared_ptr<rt::AudioTap> tap, StatusSink sink, LiveStreamOptions options)
    : m_shared(std::make_unique<Shared>())
{
    m_shared->tap = std::move(tap);
    m_shared->sink = std::move(sink);
    m_shared->options = std::move(options);
    if (!m_shared->options.wallClockMs) {
        m_shared->options.wallClockMs = [] {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                .count();
        };
    }
    m_thread = std::thread([this] { run(); });
}

LiveStream::~LiveStream()
{
    requestStop();
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void LiveStream::requestStop()
{
    m_shared->stopping.store(true, std::memory_order_relaxed);
    m_shared->wake.notify_all();
}

void LiveStream::configure(LiveStreamSettings settings)
{
    {
        const std::lock_guard lock(m_shared->mutex);
        if (m_shared->settings && m_shared->settings->nowPlayingFile != settings.nowPlayingFile) {
            m_shared->handedTrack.reset();
            ++m_shared->trackVersion;
        }
        m_shared->settings = std::move(settings);
        ++m_shared->settingsVersion;
    }
    m_shared->wake.notify_all();
}

void LiveStream::setPassword(std::optional<std::string> key)
{
    {
        const std::lock_guard lock(m_shared->mutex);
        m_shared->keyKnown = true;
        m_shared->keyUnavailable = false;
        m_shared->key = std::move(key);
        ++m_shared->keyVersion;
    }
    m_shared->wake.notify_all();
}

void LiveStream::setPasswordUnavailable()
{
    {
        const std::lock_guard lock(m_shared->mutex);
        m_shared->keyKnown = true;
        m_shared->keyUnavailable = true;
        m_shared->key.reset();
        ++m_shared->keyVersion;
    }
    m_shared->wake.notify_all();
}

void LiveStream::setNowPlaying(Track track)
{
    {
        const std::lock_guard lock(m_shared->mutex);
        m_shared->handedTrack = std::move(track);
        ++m_shared->trackVersion;
    }
    m_shared->wake.notify_all();
}

LiveStreamStatus LiveStream::status() const
{
    const std::lock_guard lock(m_shared->mutex);
    return m_shared->said;
}

void LiveStream::run()
{
    Shared &shared = *m_shared;
    rt::AudioTap &tap = *shared.tap;
    const LiveStreamOptions &options = shared.options;
    const VideoEncoderProbe &probe = options.probe ? *options.probe : probeVideoEncoders();

    // What the thread has taken from the callers.
    std::optional<LiveStreamSettings> settings;
    std::uint64_t settingsSeen = 0;
    bool keyKnown = false;
    bool keyUnavailable = false;
    std::optional<std::string> key;
    std::uint64_t keySeen = 0;
    std::optional<Track> handedTrack;
    std::uint64_t trackSeen = 0;

    // The connection and what feeds it.
    std::unique_ptr<RtmpOutput> output;
    std::unique_ptr<VideoEncoder> video;
    std::unique_ptr<AacEncoder> audio;
    std::unique_ptr<record::Resampler> resampler;
    std::unique_ptr<Compositor> compositor;
    std::unique_ptr<TextPainter> painter = TextPainter::create();
    std::optional<AudioAnalyser> analyser;
    std::uint32_t sessionRate = 0;
    std::optional<rt::TapMarker> marker;
    std::vector<float> buffer(kChunkFrames * tap.channelCount());
    std::vector<float> stereo;
    /// The sound sent on this connection, in the encoder's samples, and
    /// the frames of video: the second follows the first.
    std::int64_t audioSamples = 0;
    std::int64_t videoFrames = 0;
    /// What earlier connections sent, for the status's running count.
    std::uint64_t bytesBefore = 0;

    Backoff backoff(options.backoff);
    LiveStreamStatus status;
    BroadcastStatus &stream = status.stream;
    Clock::time_point connectedAt{};
    Clock::time_point retryAt{};
    bool everConnected = false;
    bool connectNow = false;
    std::optional<Clock::time_point> waitingForRate;

    NowPlayingFile nowPlayingFile;
    Track fileTrack;
    Clock::time_point fileLookedAt{};

    std::deque<std::pair<Clock::time_point, std::uint64_t>> sentAt;
    Clock::time_point saidAt{};

    const auto bytesSent = [&] { return bytesBefore + (output ? output->bytesSent() : 0); };

    const auto say = [&](bool force) {
        const Clock::time_point now = Clock::now();
        stream.retryInMs = stream.state == State::Retrying
            ? std::max<std::int64_t>(0, std::chrono::duration_cast<std::chrono::milliseconds>(retryAt - now).count())
            : 0;
        stream.bytesSent = bytesSent();
        if (stream.state == State::Connected) {
            sentAt.emplace_back(now, stream.bytesSent);
        }
        while (!sentAt.empty() && now - sentAt.front().first > kRateWindow) {
            sentAt.pop_front();
        }
        if (stream.state == State::Connected && sentAt.size() >= 2) {
            const double seconds = std::chrono::duration<double>(sentAt.back().first - sentAt.front().first).count();
            stream.kbps = seconds > 0.5
                ? static_cast<double>(sentAt.back().second - sentAt.front().second) * 8.0 / 1000.0 / seconds
                : stream.kbps;
        } else if (stream.state != State::Connected) {
            stream.kbps = 0.0;
        }
        bool changed = false;
        {
            const std::lock_guard lock(shared.mutex);
            const LiveStreamStatus &was = shared.said;
            changed = was.stream.state != stream.state || was.stream.error != stream.error
                || was.stream.reconnects != stream.reconnects || was.stream.title != stream.title
                || was.stream.connectedSinceMs != stream.connectedSinceMs || was.encoder != status.encoder;
            shared.said = status;
        }
        if (changed || force || now - saidAt >= options.statusInterval) {
            saidAt = now;
            if (shared.sink) {
                shared.sink(status);
            }
        }
    };

    const auto renderedTitle = [&] {
        const Track &track = handedTrack ? *handedTrack : fileTrack;
        return settings ? renderTitle(settings->titleTemplate, track, settings->sessionName) : std::string{};
    };

    // The picture as the settings and what is playing say it now.
    const auto updateScene = [&] {
        if (!compositor || !settings) {
            return;
        }
        Scene scene = settings->scene;
        scene.track = renderedTitle();
        compositor->setScene(scene);
        if (stream.title != scene.track) {
            stream.title = scene.track;
            say(true);
        }
    };

    const auto disconnect = [&] {
        bytesBefore = bytesSent();
        output.reset();
        video.reset();
        audio.reset();
        resampler.reset();
        stream.connectedSinceMs = 0;
        status.encoder.clear();
    };

    const auto fail = [&](StreamError error) {
        if (stream.state == State::Connected) {
            backoff.connectionLasted(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - connectedAt));
        }
        disconnect();
        stream.error = error;
        if (isWorthRetrying(error)) {
            stream.state = State::Retrying;
            retryAt = Clock::now() + backoff.next();
        } else {
            stream.state = State::Failed;
        }
        say(true);
    };

    const auto connect = [&] {
        connectNow = false;
        std::expected<RtmpTarget, StreamError> target =
            settings ? parseRtmpUrl(settings->url) : std::unexpected(StreamError::NotConfigured);
        if (!target) {
            stream.state = State::Failed;
            stream.error = StreamError::NotConfigured;
            say(true);
            return;
        }
        if (!keyKnown) {
            stream.state = State::WaitingForPassword;
            stream.error.reset();
            say(true);
            return;
        }
        if (keyUnavailable || !key || key->empty()) {
            stream.state = State::Failed;
            stream.error = keyUnavailable ? StreamError::PasswordUnavailable : StreamError::NoPassword;
            say(true);
            return;
        }
        // The session's rate first, as a radio stream learns it.
        if (sessionRate == 0) {
            const Clock::time_point now = Clock::now();
            if (!waitingForRate) {
                waitingForRate = now;
            }
            if (now - *waitingForRate < options.rateWait) {
                connectNow = true;
                stream.state = State::Connecting;
                stream.error.reset();
                say(false);
                return;
            }
        }
        waitingForRate.reset();
        stream.state = State::Connecting;
        say(true);

        // **The encoders before the server**: a computer with no H.264
        // encoder, or settings none will take, are found without asking
        // the server for anything, and not tried again until they change.
        const LiveQuality &quality = settings->quality;
        const std::vector<VideoEncoderKind> order = videoEncoderOrder(settings->encoder, probe);
        if (order.empty()) {
            fail(StreamError::EncoderMissing);
            return;
        }
        VideoEncoder::Spec spec;
        spec.width = quality.width;
        spec.height = quality.height;
        spec.fps = quality.fps;
        spec.bitrateKbps = quality.videoKbps;
        spec.vaapiDevice = probe.vaapiDevice;
        std::expected<std::unique_ptr<VideoEncoder>, StreamError> madeVideo = VideoEncoder::createFirst(order, spec);
        if (!madeVideo) {
            fail(madeVideo.error());
            return;
        }
        const std::uint32_t rate = streamRateFor(StreamFormat::Aac, sessionRate != 0 ? sessionRate : 48000);
        std::expected<std::unique_ptr<AacEncoder>, StreamError> madeAudio =
            AacEncoder::create(rate, 2, quality.audioKbps);
        if (!madeAudio) {
            fail(madeAudio.error() == StreamError::EncoderFailed ? StreamError::UnsupportedSettings
                                                                 : madeAudio.error());
            return;
        }
        const VideoEncoder::Spec &made = (*madeVideo)->spec();
        if (!compositor || compositor->width() != made.width || compositor->height() != made.height) {
            // **The same painter for every size**: it is this thread's, and
            // outlives each compositor it lends its fonts to.
            compositor = std::make_unique<Compositor>(made.width, made.height, painter.get());
        }
        // Measured at the rate the sound goes out at, which a new session
        // rate can change.
        if (!analyser || analyser->sampleRate() != rate) {
            analyser.emplace(rate);
        }
        updateScene();

        RtmpOutput::Options outputOptions;
        outputOptions.connectTimeout = options.connectTimeout;
        outputOptions.sendTimeout = options.sendTimeout;
        outputOptions.caFile = options.caFile;
        std::expected<std::unique_ptr<RtmpOutput>, StreamError> opened = RtmpOutput::open(
            *target, *key, (*madeVideo)->context(), (*madeAudio)->context(), outputOptions, shared.stopping);
        if (shared.stopping.load(std::memory_order_relaxed)) {
            return;
        }
        if (!opened) {
            fail(opened.error());
            return;
        }
        // **What arrived while connecting is let go**, up to the next change
        // of rate: it is stale, and sent now it would go out as a burst
        // ahead of the clock the server and the viewers keep.
        if (!marker) {
            rt::TapMarker next;
            if (tap.popMarker(next)) {
                marker = next;
            }
        }
        std::size_t stale = tap.readableFrames();
        if (marker) {
            stale = std::min<std::size_t>(
                stale,
                marker->atFrame > tap.framesRead() ? static_cast<std::size_t>(marker->atFrame - tap.framesRead()) : 0);
        }
        while (stale > 0) {
            const std::size_t let = tap.discard(stale);
            if (let == 0) {
                break;
            }
            stale -= let;
        }
        output = std::move(*opened);
        video = std::move(*madeVideo);
        audio = std::move(*madeAudio);
        resampler.reset();
        audioSamples = 0;
        videoFrames = 0;

        stream.state = State::Connected;
        stream.error.reset();
        stream.sampleRate = rate;
        stream.connectedSinceMs = options.wallClockMs();
        if (everConnected) {
            ++stream.reconnects;
        }
        everConnected = true;
        status.encoder = std::string{videoEncoderId(video->spec().kind)};
        status.width = video->spec().width;
        status.height = video->spec().height;
        status.fps = video->spec().fps;
        connectedAt = Clock::now();
        sentAt.clear();
        say(true);
    };

    // Why the last write failed: the network's word, rather than the
    // encoder's that it could not hand its packet on.
    std::optional<StreamError> writeError;
    const auto sendVideo = [&](AVPacket *packet) {
        const std::expected<void, StreamError> written = output->writeVideo(packet);
        if (!written) {
            writeError = written.error();
        }
        return written.has_value();
    };
    const auto sendAudio = [&](AVPacket *packet) {
        const std::expected<void, StreamError> written = output->writeAudio(packet);
        if (!written) {
            writeError = written.error();
        }
        return written.has_value();
    };
    const auto failSending = [&](StreamError otherwise) {
        if (!shared.stopping.load(std::memory_order_relaxed)) {
            fail(std::exchange(writeError, std::nullopt).value_or(otherwise));
        }
    };

    // **The frames the sound has reached**: frame n shows the moment n / fps
    // seconds in, drawn once that much sound has gone out.
    const auto sendFrames = [&]() -> bool {
        const auto rate = static_cast<std::int64_t>(audio->sampleRate());
        const auto fps = static_cast<std::int64_t>(video->spec().fps);
        const bool behind = static_cast<double>(tap.readableFrames())
            > kBehindSeconds * static_cast<double>(sessionRate != 0 ? sessionRate : audio->sampleRate());
        while (videoFrames * rate <= audioSamples * fps) {
            if (behind && videoFrames > 0) {
                ++status.framesDropped;
                ++videoFrames;
                continue;
            }
            const Canvas &frame = compositor->render(analyser->visuals());
            if (options.onFrame) {
                options.onFrame(frame);
            }
            const std::expected<void, StreamError> encoded = video->encode(frame, videoFrames, sendVideo);
            ++videoFrames;
            if (!encoded) {
                return false;
            }
            ++status.framesSent;
        }
        return true;
    };

    // Takes what the tap holds: into the stream while connected, and let go
    // of otherwise. Rate markers are followed either way.
    const auto drain = [&] {
        for (;;) {
            if (shared.stopping.load(std::memory_order_relaxed)) {
                return;
            }
            if (!marker) {
                rt::TapMarker next;
                if (tap.popMarker(next)) {
                    marker = next;
                }
            }
            if (marker && marker->atFrame <= tap.framesRead()) {
                if (marker->kind == rt::TapMarker::Kind::Rate && marker->value != 0) {
                    sessionRate = marker->value;
                    resampler.reset();
                    if (stream.state == State::Connected && audio
                        && streamRateFor(StreamFormat::Aac, sessionRate) != audio->sampleRate()) {
                        marker.reset();
                        connectNow = true;
                        return;
                    }
                }
                marker.reset();
                continue;
            }
            std::size_t wanted = kChunkFrames;
            if (marker) {
                wanted = std::min<std::size_t>(wanted, static_cast<std::size_t>(marker->atFrame - tap.framesRead()));
            }
            if (stream.state != State::Connected) {
                if (tap.discard(wanted) == 0) {
                    return;
                }
                continue;
            }
            const std::size_t got = tap.read(buffer, wanted);
            if (got == 0) {
                return;
            }
            const std::uint32_t tapChannels = tap.channelCount();
            stereo.resize(got * 2);
            for (std::size_t frame = 0; frame < got; ++frame) {
                const float *in = &buffer[frame * tapChannels];
                stereo[frame * 2] = in[0];
                stereo[frame * 2 + 1] = tapChannels > 1 ? in[1] : in[0];
            }
            std::span<const float> samples = stereo;
            const std::uint32_t from = sessionRate != 0 ? sessionRate : audio->sampleRate();
            if (from != audio->sampleRate()) {
                if (!resampler) {
                    resampler = record::Resampler::create(from, audio->sampleRate(), 2);
                }
                if (!resampler) {
                    fail(StreamError::EncoderFailed);
                    return;
                }
                samples = resampler->process(samples);
            }
            analyser->feed(samples, 2);
            if (const std::expected<void, StreamError> encoded = audio->encode(samples, sendAudio); !encoded) {
                failSending(encoded.error());
                return;
            }
            audioSamples += static_cast<std::int64_t>(samples.size() / 2);
            if (!sendFrames()) {
                failSending(StreamError::EncoderFailed);
                return;
            }
        }
    };

    while (!shared.stopping.load(std::memory_order_relaxed)) {
        bool sceneChanged = false;
        {
            std::unique_lock lock(shared.mutex);
            shared.wake.wait_for(lock, options.pollInterval, [&] {
                return shared.stopping.load(std::memory_order_relaxed) || shared.settingsVersion != settingsSeen
                    || shared.keyVersion != keySeen || shared.trackVersion != trackSeen;
            });
            if (shared.stopping.load(std::memory_order_relaxed)) {
                break;
            }
            if (shared.settingsVersion != settingsSeen) {
                settingsSeen = shared.settingsVersion;
                if (!settings || needsReconnect(*settings, *shared.settings) || stream.state == State::Failed) {
                    connectNow = true;
                }
                if (settings && settings->nowPlayingFile != shared.settings->nowPlayingFile) {
                    fileTrack = {};
                }
                settings = shared.settings;
                sceneChanged = true;
            }
            if (shared.keyVersion != keySeen) {
                keySeen = shared.keyVersion;
                // A new key connects again with it; the same one again is a
                // try at once for a stream that was waiting or had given up.
                const bool changed = !keyKnown || key != shared.key || keyUnavailable != shared.keyUnavailable;
                if (settings && (changed || stream.state != State::Connected)) {
                    connectNow = true;
                }
                keyKnown = shared.keyKnown;
                keyUnavailable = shared.keyUnavailable;
                key = shared.key;
            }
            if (shared.trackVersion != trackSeen) {
                trackSeen = shared.trackVersion;
                handedTrack = shared.handedTrack;
                sceneChanged = true;
            }
        }

        // **Whatever goes wrong in here fails the stream, not the engine**: a
        // picture too large to draw, say, would otherwise end the daemon
        // and every session in it. Not tried again until something changes.
        try {
            drain();
            if (connectNow) {
                if (output) {
                    disconnect();
                }
                if (!waitingForRate) {
                    backoff.reset();
                }
                connect();
            } else if (stream.state == State::Retrying && Clock::now() >= retryAt) {
                connect();
            }
            drain();

            if (settings && !settings->nowPlayingFile.empty() && !handedTrack
                && Clock::now() - fileLookedAt >= std::chrono::seconds{1}) {
                fileLookedAt = Clock::now();
                if (std::optional<std::string> title = nowPlayingFile.poll(settings->nowPlayingFile)) {
                    fileTrack = Track{};
                    fileTrack.title = std::move(*title);
                    sceneChanged = true;
                }
            }
            if (sceneChanged) {
                updateScene();
            }
            say(false);
        } catch (const std::exception &) {
            fail(StreamError::UnsupportedSettings);
        }
    }
    disconnect();
    stream.state = State::Idle;
    stream.error.reset();
    stream.kbps = 0.0;
    say(true);
}

} // namespace rb::stream
