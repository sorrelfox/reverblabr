// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "stream/broadcaststream.h"

#include "record/resampler.h"
#include "rt/audiotap.h"
#include "stream/connection.h"
#include "stream/streamencoder.h"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <expected>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace rb::stream
{

namespace
{

using Clock = std::chrono::steady_clock;
using State = BroadcastStatus::State;

/// The most frames taken from the tap at a time.
constexpr std::size_t kChunkFrames = 4096;

/// How long the title request may take. It is made on the stream's own
/// thread, while the tap fills; the tap holds ten seconds.
constexpr std::chrono::milliseconds kTitleTimeout{5000};

/// How long a title the server would not take waits before it is offered
/// again.
constexpr std::chrono::milliseconds kTitleRetry{10000};

/// How far back the rate the bytes go out at is measured.
constexpr std::chrono::milliseconds kRateWindow{5000};

/// **Gives the server a title on a thread of its own.** The request is a
/// connection of its own and may take seconds; made on the stream's
/// thread it would hold the audio back, and Icecast drops a source that
/// sends nothing for ten seconds. One request at a time, and only the
/// newest title waiting: a title superseded before it was sent is not
/// worth sending.
class TitleSender
{
public:
    struct Result {
        std::string title;
        std::uint64_t connection = 0;
        bool sent = false;
    };

    explicit TitleSender(const std::atomic<bool> &stopping)
        : m_stopping(stopping)
        , m_thread([this] { run(); })
    {
    }

    ~TitleSender()
    {
        {
            const std::lock_guard lock(m_mutex);
            m_quit = true;
        }
        m_wake.notify_all();
        m_thread.join();
    }

    TitleSender(const TitleSender &) = delete;
    TitleSender &operator=(const TitleSender &) = delete;

    /// Sends \a title to \a server, for the connection numbered
    /// \a connection, in place of any title still waiting.
    void post(ServerSettings server, std::string title, std::uint64_t connection)
    {
        {
            const std::lock_guard lock(m_mutex);
            m_waiting = Waiting{std::move(server), std::move(title), connection};
        }
        m_wake.notify_all();
    }

    /// What became of the titles sent since the last look.
    [[nodiscard]] std::vector<Result> take()
    {
        const std::lock_guard lock(m_mutex);
        return std::exchange(m_results, {});
    }

private:
    struct Waiting {
        ServerSettings server;
        std::string title;
        std::uint64_t connection = 0;
    };

    void run()
    {
        for (;;) {
            Waiting next;
            {
                std::unique_lock lock(m_mutex);
                m_wake.wait(lock, [this] { return m_quit || m_waiting.has_value(); });
                if (m_quit) {
                    return;
                }
                next = std::move(*m_waiting);
                m_waiting.reset();
            }
            const bool sent = send(next.server, next.title);
            const std::lock_guard lock(m_mutex);
            m_results.push_back(Result{std::move(next.title), next.connection, sent});
        }
    }

    [[nodiscard]] bool send(const ServerSettings &server, const std::string &title) const
    {
        std::expected<Connection, StreamError> admin =
            Connection::open(server.host, server.port, kTitleTimeout, m_stopping);
        if (!admin) {
            return false;
        }
        if (!admin->send(metadataRequest(server, title), kTitleTimeout, m_stopping)) {
            return false;
        }
        const std::expected<std::string, StreamError> answer = admin->receive(
            [](const std::string &received, bool closed) { return parseHttpResponse(received, closed).has_value(); },
            kTitleTimeout,
            m_stopping);
        if (!answer) {
            return false;
        }
        const std::optional<Response> response = parseHttpResponse(*answer, true);
        return response && !errorForStatus(response->status);
    }

    const std::atomic<bool> &m_stopping;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::optional<Waiting> m_waiting;
    std::vector<Result> m_results;
    bool m_quit = false;
    std::thread m_thread;
};

/// Whether the server has to be told about a change in its first request,
/// which means connecting again.
[[nodiscard]] bool needsReconnect(const BroadcastSettings &was, const BroadcastSettings &now)
{
    return was.server != now.server || was.station != now.station || was.format != now.format
        || was.bitrateKbps != now.bitrateKbps || was.mono != now.mono;
}

} // namespace

std::string_view broadcastStateName(BroadcastStatus::State state) noexcept
{
    switch (state) {
    case State::Idle:
        return "idle";
    case State::WaitingForPassword:
        return "waitingForPassword";
    case State::Connecting:
        return "connecting";
    case State::Connected:
        return "connected";
    case State::Retrying:
        return "retrying";
    case State::Failed:
        return "failed";
    }
    return "idle";
}

/// What the callers and the thread share, under the mutex.
struct BroadcastStream::Shared {
    std::shared_ptr<rt::AudioTap> tap;
    StatusSink sink;
    BroadcastOptions options;

    std::mutex mutex;
    std::condition_variable wake;
    std::optional<BroadcastSettings> settings;
    /// Bumped by each change the thread has to look at.
    std::uint64_t settingsVersion = 0;
    bool passwordKnown = false;
    bool passwordUnavailable = false;
    std::optional<std::string> password;
    std::uint64_t passwordVersion = 0;
    std::optional<Track> handedTrack;
    std::uint64_t trackVersion = 0;
    BroadcastStatus said;

    std::atomic<bool> stopping{false};
};

BroadcastStream::BroadcastStream(std::shared_ptr<rt::AudioTap> tap, StatusSink sink, BroadcastOptions options)
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

BroadcastStream::~BroadcastStream()
{
    requestStop();
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void BroadcastStream::requestStop()
{
    m_shared->stopping.store(true, std::memory_order_relaxed);
    m_shared->wake.notify_all();
}

void BroadcastStream::configure(BroadcastSettings settings)
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

void BroadcastStream::setPassword(std::optional<std::string> password)
{
    {
        const std::lock_guard lock(m_shared->mutex);
        m_shared->passwordKnown = true;
        m_shared->passwordUnavailable = false;
        m_shared->password = std::move(password);
        ++m_shared->passwordVersion;
    }
    m_shared->wake.notify_all();
}

void BroadcastStream::setPasswordUnavailable()
{
    {
        const std::lock_guard lock(m_shared->mutex);
        m_shared->passwordKnown = true;
        m_shared->passwordUnavailable = true;
        m_shared->password.reset();
        ++m_shared->passwordVersion;
    }
    m_shared->wake.notify_all();
}

void BroadcastStream::setNowPlaying(Track track)
{
    {
        const std::lock_guard lock(m_shared->mutex);
        m_shared->handedTrack = std::move(track);
        ++m_shared->trackVersion;
    }
    m_shared->wake.notify_all();
}

BroadcastStatus BroadcastStream::status() const
{
    const std::lock_guard lock(m_shared->mutex);
    return m_shared->said;
}

void BroadcastStream::run()
{
    Shared &shared = *m_shared;
    rt::AudioTap &tap = *shared.tap;
    const BroadcastOptions &options = shared.options;

    // What the thread has taken from the callers.
    std::optional<BroadcastSettings> settings;
    std::uint64_t settingsSeen = 0;
    bool passwordKnown = false;
    bool passwordUnavailable = false;
    std::optional<std::string> password;
    std::uint64_t passwordSeen = 0;
    std::optional<Track> handedTrack;
    std::uint64_t trackSeen = 0;

    // The connection and what feeds it. The server's settings with the
    // password in them, for the title requests, while connected; and which
    // connection this is, so a title answered for the one before is not
    // taken for this one's.
    Connection connection;
    ServerSettings live;
    std::uint64_t connectionNumber = 0;
    std::unique_ptr<StreamEncoder> encoder;
    std::unique_ptr<record::Resampler> resampler;
    std::uint32_t sessionRate = 0;
    std::optional<rt::TapMarker> marker;
    std::vector<float> buffer(kChunkFrames * tap.channelCount());
    std::vector<float> mixed;

    Backoff backoff(options.backoff);
    BroadcastStatus status;
    Clock::time_point connectedAt{};
    Clock::time_point retryAt{};
    bool everConnected = false;
    bool connectNow = false;
    /// Since when a connection has been waiting to hear the session's rate.
    std::optional<Clock::time_point> waitingForRate;

    // The title.
    TitleSender titles(shared.stopping);
    NowPlayingFile nowPlayingFile;
    Track fileTrack;
    std::string titleSent;
    std::string titleAsked;
    Clock::time_point titleRetryAt{};
    Clock::time_point fileLookedAt{};

    // What has gone out, for the rate.
    std::deque<std::pair<Clock::time_point, std::uint64_t>> sentAt;
    Clock::time_point saidAt{};

    const auto say = [&](bool force) {
        const Clock::time_point now = Clock::now();
        status.retryInMs = status.state == State::Retrying
            ? std::max<std::int64_t>(0, std::chrono::duration_cast<std::chrono::milliseconds>(retryAt - now).count())
            : 0;
        while (!sentAt.empty() && now - sentAt.front().first > kRateWindow) {
            sentAt.pop_front();
        }
        if (status.state == State::Connected && sentAt.size() >= 2) {
            const double seconds = std::chrono::duration<double>(sentAt.back().first - sentAt.front().first).count();
            status.kbps = seconds > 0.5
                ? static_cast<double>(sentAt.back().second - sentAt.front().second) * 8.0 / 1000.0 / seconds
                : status.kbps;
        } else if (status.state != State::Connected) {
            status.kbps = 0.0;
        }
        bool changed = false;
        {
            const std::lock_guard lock(shared.mutex);
            const BroadcastStatus &was = shared.said;
            changed = was.state != status.state || was.error != status.error || was.reconnects != status.reconnects
                || was.title != status.title || was.connectedSinceMs != status.connectedSinceMs;
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

    const auto disconnect = [&] {
        connection.close();
        live = {};
        encoder.reset();
        resampler.reset();
        status.connectedSinceMs = 0;
    };

    const auto fail = [&](StreamError error) {
        if (status.state == State::Connected) {
            backoff.connectionLasted(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - connectedAt));
        }
        disconnect();
        status.error = error;
        if (isWorthRetrying(error)) {
            status.state = State::Retrying;
            retryAt = Clock::now() + backoff.next();
        } else {
            status.state = State::Failed;
        }
        say(true);
    };

    const auto sendBytes = [&](std::vector<char> bytes) {
        if (bytes.empty()) {
            return true;
        }
        if (const std::expected<void, StreamError> sent = connection.send(bytes, options.sendTimeout, shared.stopping);
            !sent) {
            if (!shared.stopping.load(std::memory_order_relaxed)) {
                fail(sent.error());
            }
            return false;
        }
        status.bytesSent += bytes.size();
        sentAt.emplace_back(Clock::now(), status.bytesSent);
        return true;
    };

    // What the title requests came to.
    const auto collectTitles = [&] {
        for (const TitleSender::Result &result : titles.take()) {
            if (result.connection != connectionNumber || status.state != State::Connected) {
                continue;
            }
            if (result.title == titleAsked) {
                titleAsked.clear();
            }
            if (result.sent) {
                titleSent = result.title;
                status.title = result.title;
                say(true);
            } else {
                titleRetryAt = Clock::now() + kTitleRetry;
            }
        }
    };

    // Gives the server the title, if there is a new one, by whichever way
    // the format takes it.
    const auto updateTitle = [&] {
        if (status.state != State::Connected) {
            return;
        }
        const std::string title = renderedTitle();
        if (title.empty() || title == titleSent || title == titleAsked || Clock::now() < titleRetryAt) {
            return;
        }
        if (isOgg(settings->format)) {
            Track track;
            track.title = title;
            if (const std::expected<void, StreamError> restarted = encoder->restart(track); !restarted) {
                fail(restarted.error());
                return;
            }
            if (!sendBytes(encoder->take())) {
                return;
            }
            titleSent = title;
            status.title = title;
            say(true);
            return;
        }
        titleAsked = title;
        titles.post(live, title, connectionNumber);
    };

    const auto connect = [&] {
        connectNow = false;
        if (!settings || settings->server.host.empty() || settings->server.port == 0) {
            status.state = State::Failed;
            status.error = StreamError::NotConfigured;
            say(true);
            return;
        }
        if (!passwordKnown) {
            status.state = State::WaitingForPassword;
            status.error.reset();
            say(true);
            return;
        }
        if (passwordUnavailable || !password) {
            status.state = State::Failed;
            status.error = passwordUnavailable ? StreamError::PasswordUnavailable : StreamError::NoPassword;
            say(true);
            return;
        }
        // **The session's rate first**, so a 44.1 kHz session goes out at
        // 44.1 kHz rather than converted to 48. The block says it with its
        // first cycle; a session that has not run a cycle within a moment
        // is streamed at 48 kHz, and connected again if it then says
        // otherwise.
        if (sessionRate == 0) {
            const Clock::time_point now = Clock::now();
            if (!waitingForRate) {
                waitingForRate = now;
            }
            if (now - *waitingForRate < options.rateWait) {
                connectNow = true;
                status.state = State::Connecting;
                status.error.reset();
                say(false);
                return;
            }
        }
        waitingForRate.reset();
        status.state = State::Connecting;
        say(true);

        const std::uint32_t rate = streamRateFor(settings->format, sessionRate != 0 ? sessionRate : 48000);
        const std::uint32_t channels = settings->mono ? 1 : 2;

        // **The encoder before the server**: settings it will not take are
        // found without taking the server's mount point first, and are not
        // tried again until they change.
        StreamEncoder::Spec spec;
        spec.format = settings->format;
        spec.sampleRate = rate;
        spec.channels = channels;
        spec.bitrateKbps = clampedBitrate(settings->format, channels, settings->bitrateKbps);
        const std::string title = renderedTitle();
        spec.track.title = title;
        std::expected<std::unique_ptr<StreamEncoder>, StreamError> made = StreamEncoder::create(spec);
        if (!made) {
            fail(made.error() == StreamError::EncoderFailed ? StreamError::UnsupportedSettings : made.error());
            return;
        }

        ServerSettings server = settings->server;
        server.password = *password;
        AudioInfo audio;
        audio.format = settings->format;
        audio.bitrateKbps = spec.bitrateKbps;
        audio.sampleRate = rate;
        audio.channels = channels;

        // A SHOUTcast source talks to the port after the listeners' one.
        const auto port = static_cast<std::uint16_t>(
            server.kind == ServerKind::Shoutcast ? std::min<std::uint32_t>(server.port + 1U, 65535U) : server.port);
        const auto handshake = [&](SourceMethod method) -> std::expected<Connection, StreamError> {
            std::expected<Connection, StreamError> opened =
                Connection::open(server.host, port, options.connectTimeout, shared.stopping);
            if (!opened) {
                return opened;
            }
            const bool icecast = server.kind == ServerKind::Icecast;
            const std::string hello =
                icecast ? icecastRequest(server, settings->station, audio, method) : shoutcastLogin(server);
            if (const std::expected<void, StreamError> sent =
                    opened->send(hello, options.connectTimeout, shared.stopping);
                !sent) {
                return std::unexpected(sent.error());
            }
            const auto complete = [icecast](const std::string &received, bool closed) {
                return icecast ? parseHttpResponse(received, closed).has_value()
                               : parseShoutcastResponse(received, closed).has_value();
            };
            const std::expected<std::string, StreamError> answer =
                opened->receive(complete, options.connectTimeout, shared.stopping);
            if (!answer) {
                return std::unexpected(answer.error());
            }
            const std::optional<Response> response =
                icecast ? parseHttpResponse(*answer, true) : parseShoutcastResponse(*answer, true);
            if (const std::optional<StreamError> refused = errorForStatus(response ? response->status : 0)) {
                // Before Icecast 2.4 there was no PUT, and the answer to one
                // is "method not allowed"; SOURCE is what those take.
                if (icecast && method == SourceMethod::Put && response
                    && (response->status == 405 || response->status == 501)) {
                    return std::unexpected(StreamError::Rejected);
                }
                return std::unexpected(*refused);
            }
            if (!icecast) {
                const std::string headers = shoutcastHeaders(settings->station, audio);
                if (const std::expected<void, StreamError> sent =
                        opened->send(headers, options.connectTimeout, shared.stopping);
                    !sent) {
                    return std::unexpected(sent.error());
                }
            }
            return opened;
        };
        std::expected<Connection, StreamError> opened = handshake(SourceMethod::Put);
        if (!opened && opened.error() == StreamError::Rejected && server.kind == ServerKind::Icecast) {
            opened = handshake(SourceMethod::Source);
        }
        if (shared.stopping.load(std::memory_order_relaxed)) {
            return;
        }
        if (!opened) {
            fail(opened.error());
            return;
        }
        connection = std::move(*opened);
        live = server;
        encoder = std::move(*made);
        resampler.reset();
        ++connectionNumber;

        status.state = State::Connected;
        status.error.reset();
        status.sampleRate = rate;
        status.connectedSinceMs = options.wallClockMs();
        if (everConnected) {
            ++status.reconnects;
        }
        everConnected = true;
        connectedAt = Clock::now();
        sentAt.clear();
        titleSent.clear();
        titleAsked.clear();
        titleRetryAt = {};
        if (isOgg(settings->format) && !title.empty()) {
            // In the stream's first comment header already.
            titleSent = title;
            status.title = title;
        }
        say(true);
        if (!sendBytes(encoder->take())) {
            return;
        }
        updateTitle();
    };

    // Takes what the tap holds: into the stream while connected, and let go
    // of otherwise. Rate markers are followed either way.
    const auto drain = [&] {
        for (;;) {
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
                    // **A rate the stream cannot follow by converting** —
                    // a session that moved from 44.1 to 48 kHz, whose
                    // stream was at its own rate — is a new connection at
                    // the new one.
                    if (status.state == State::Connected && encoder
                        && streamRateFor(settings->format, sessionRate) != encoder->spec().sampleRate) {
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
            if (status.state != State::Connected) {
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
            const std::uint32_t channels = encoder->spec().channels;
            mixed.resize(got * channels);
            for (std::size_t frame = 0; frame < got; ++frame) {
                const float *in = &buffer[frame * tapChannels];
                if (channels == 1) {
                    mixed[frame] = tapChannels > 1 ? 0.5f * (in[0] + in[1]) : in[0];
                } else {
                    mixed[frame * 2] = in[0];
                    mixed[frame * 2 + 1] = tapChannels > 1 ? in[1] : in[0];
                }
            }
            std::span<const float> samples = mixed;
            const std::uint32_t from = sessionRate != 0 ? sessionRate : encoder->spec().sampleRate;
            if (from != encoder->spec().sampleRate) {
                if (!resampler) {
                    resampler = record::Resampler::create(from, encoder->spec().sampleRate, channels);
                }
                if (!resampler) {
                    fail(StreamError::EncoderFailed);
                    return;
                }
                samples = resampler->process(samples);
            }
            if (const std::expected<void, StreamError> encoded = encoder->encode(samples); !encoded) {
                fail(encoded.error());
                return;
            }
            if (!sendBytes(encoder->take())) {
                return;
            }
        }
    };

    while (!shared.stopping.load(std::memory_order_relaxed)) {
        {
            std::unique_lock lock(shared.mutex);
            shared.wake.wait_for(lock, options.pollInterval, [&] {
                return shared.stopping.load(std::memory_order_relaxed) || shared.settingsVersion != settingsSeen
                    || shared.passwordVersion != passwordSeen || shared.trackVersion != trackSeen;
            });
            if (shared.stopping.load(std::memory_order_relaxed)) {
                break;
            }
            if (shared.settingsVersion != settingsSeen) {
                settingsSeen = shared.settingsVersion;
                const bool first = !settings;
                if (first || needsReconnect(*settings, *shared.settings) || status.state == State::Failed) {
                    connectNow = true;
                }
                if (settings && settings->nowPlayingFile != shared.settings->nowPlayingFile) {
                    fileTrack = {};
                }
                settings = shared.settings;
            }
            if (shared.passwordVersion != passwordSeen) {
                passwordSeen = shared.passwordVersion;
                // A new password connects again, with it; the same one
                // again is a try at once for a stream that was waiting or
                // had given up, and nothing for one on the air.
                const bool changed =
                    !passwordKnown || password != shared.password || passwordUnavailable != shared.passwordUnavailable;
                if (settings && (changed || status.state != State::Connected)) {
                    connectNow = true;
                }
                passwordKnown = shared.passwordKnown;
                passwordUnavailable = shared.passwordUnavailable;
                password = shared.password;
            }
            if (shared.trackVersion != trackSeen) {
                trackSeen = shared.trackVersion;
                handedTrack = shared.handedTrack;
            }
        }

        // The rate is learned from what has arrived before anything else
        // is done with it.
        drain();
        if (connectNow) {
            if (status.state == State::Connected || connection.isOpen()) {
                disconnect();
            }
            if (!waitingForRate) {
                backoff.reset();
            }
            connect();
        } else if (status.state == State::Retrying && Clock::now() >= retryAt) {
            connect();
        }

        if (status.state == State::Connected && connection.peerClosed()) {
            fail(StreamError::Closed);
        }
        drain();

        // The file is looked at only while nothing has handed a track in.
        if (settings && !settings->nowPlayingFile.empty() && !handedTrack
            && Clock::now() - fileLookedAt >= std::chrono::seconds{1}) {
            fileLookedAt = Clock::now();
            if (std::optional<std::string> title = nowPlayingFile.poll(settings->nowPlayingFile)) {
                fileTrack = Track{};
                fileTrack.title = std::move(*title);
            }
        }
        collectTitles();
        updateTitle();
        say(false);
    }
    disconnect();
    status.state = State::Idle;
    status.error.reset();
    status.kbps = 0.0;
    say(true);
}

} // namespace rb::stream
