// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The thread that puts a Live Stream block on the air, against FFmpeg's own
// RTMP server run here: the picture and the sound arrive as H.264 and AAC
// and keep time with each other, the picture says what is playing, a server
// that goes is tried again, and nothing it waits on holds a stop. A real
// server is tests/livestream.

#include "rt/audiotap.h"
#include "rtmplistener.h"
#include "stream/livestream.h"
#include "stream/rtmpoutput.h"
#include "stream/textpainter.h"

#include <gtest/gtest.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <memory>
#include <mutex>
#include <numbers>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using namespace rb;
using namespace rb::stream;
using namespace rb::test;
using namespace std::chrono_literals;

namespace
{

using State = BroadcastStatus::State;

/// A tone into a tap, a cycle every ten milliseconds, as a session would.
class Feeder
{
public:
    explicit Feeder(std::shared_ptr<rt::AudioTap> tap)
        : m_tap(std::move(tap))
        , m_thread([this] { run(); })
    {
    }

    ~Feeder()
    {
        m_stopping = true;
        m_thread.join();
    }

    Feeder(const Feeder &) = delete;
    Feeder &operator=(const Feeder &) = delete;

private:
    void run()
    {
        constexpr std::uint32_t kFrames = 480;
        std::vector<float> samples(kFrames);
        std::uint64_t at = 0;
        auto next = std::chrono::steady_clock::now();
        while (!m_stopping) {
            for (std::uint32_t frame = 0; frame < kFrames; ++frame, ++at) {
                samples[frame] = static_cast<float>(0.3 * std::sin(2.0 * std::numbers::pi * 440.0 * at / 48000.0));
            }
            const float *channels[] = {samples.data(), samples.data()};
            m_tap->announceRate(48000);
            (void)m_tap->write(channels, kFrames);
            next += 10ms;
            std::this_thread::sleep_until(next);
        }
    }

    std::shared_ptr<rt::AudioTap> m_tap;
    std::atomic<bool> m_stopping{false};
    std::thread m_thread;
};

class Statuses
{
public:
    LiveStream::StatusSink sink()
    {
        return [this](const LiveStreamStatus &status) {
            const std::lock_guard lock(m_mutex);
            m_said.push_back(status);
        };
    }

    [[nodiscard]] LiveStreamStatus last() const
    {
        const std::lock_guard lock(m_mutex);
        return m_said.empty() ? LiveStreamStatus{} : m_said.back();
    }

    [[nodiscard]] bool ever(const std::function<bool(const LiveStreamStatus &)> &match) const
    {
        const std::lock_guard lock(m_mutex);
        return std::ranges::any_of(m_said, match);
    }

private:
    mutable std::mutex m_mutex;
    std::vector<LiveStreamStatus> m_said;
};

[[nodiscard]] bool waitFor(const std::function<bool()> &condition, std::chrono::milliseconds timeout = 15s)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (condition()) {
            return true;
        }
        std::this_thread::sleep_for(10ms);
    }
    return condition();
}

[[nodiscard]] LiveStreamOptions quick()
{
    LiveStreamOptions options;
    options.pollInterval = 5ms;
    options.connectTimeout = 5s;
    options.sendTimeout = 3s;
    options.statusInterval = 100ms;
    options.backoff.first = 200ms;
    options.backoff.longest = 400ms;
    options.backoff.random = [] { return 0.0; };
    return options;
}

/// Small, so the test spends its time streaming rather than encoding.
[[nodiscard]] LiveStreamSettings settingsFor(std::uint16_t port)
{
    LiveStreamSettings settings;
    settings.url = "rtmp://127.0.0.1:" + std::to_string(port) + "/live";
    settings.quality = LiveQuality{320, 180, 15, 300, 96};
    settings.scene.title = "Test";
    settings.scene.visualisation = Visualisation::Waveform;
    settings.sessionName = "Session";
    return settings;
}

[[nodiscard]] std::shared_ptr<rt::AudioTap> tenSeconds()
{
    return std::make_shared<rt::AudioTap>(2, 48000 * 10);
}

/// **A TLS server that only reads the hello**: what name the client asked
/// for (SNI), read out of the ClientHello by hand, and then it hangs up, as
/// a service with many names does to a client that names none.
class HelloListener
{
public:
    explicit HelloListener(std::uint16_t port)
    {
        m_fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(port);
        m_listening =
            ::bind(m_fd, reinterpret_cast<sockaddr *>(&address), sizeof address) == 0 && ::listen(m_fd, 4) == 0;
        m_thread = std::thread([this] { run(); });
    }

    ~HelloListener()
    {
        m_stopping = true;
        m_thread.join();
        ::close(m_fd);
    }

    HelloListener(const HelloListener &) = delete;
    HelloListener &operator=(const HelloListener &) = delete;

    [[nodiscard]] bool listening() const
    {
        return m_listening;
    }

    /// The name of the first hello that came, empty for one with none;
    /// nothing until one came.
    [[nodiscard]] std::optional<std::string> serverName() const
    {
        const std::lock_guard lock(m_mutex);
        return m_name;
    }

private:
    void run()
    {
        while (!m_stopping) {
            pollfd entry{m_fd, POLLIN, 0};
            if (::poll(&entry, 1, 20) <= 0) {
                continue;
            }
            const int client = ::accept4(m_fd, nullptr, nullptr, SOCK_CLOEXEC);
            if (client < 0) {
                continue;
            }
            std::vector<unsigned char> hello;
            const auto deadline = std::chrono::steady_clock::now() + 3s;
            while (std::chrono::steady_clock::now() < deadline && !m_stopping) {
                pollfd readable{client, POLLIN, 0};
                if (::poll(&readable, 1, 20) <= 0) {
                    continue;
                }
                unsigned char chunk[4096];
                const ssize_t got = ::recv(client, chunk, sizeof chunk, 0);
                if (got <= 0) {
                    break;
                }
                hello.insert(hello.end(), chunk, chunk + got);
                if (hello.size() >= 5 && hello.size() >= 5U + ((hello[3] << 8) | hello[4])) {
                    break;
                }
            }
            ::close(client);
            if (std::optional<std::string> name = parse(hello)) {
                const std::lock_guard lock(m_mutex);
                if (!m_name) {
                    m_name = std::move(name);
                }
            }
        }
    }

    /// The server_name in a ClientHello, empty when it has none; nothing
    /// when it is not one.
    static std::optional<std::string> parse(const std::vector<unsigned char> &bytes)
    {
        std::size_t at = 0;
        const auto need = [&](std::size_t count) { return at + count <= bytes.size(); };
        const auto u8 = [&] { return bytes[at++]; };
        const auto u16 = [&] {
            const std::size_t value = (bytes[at] << 8) | bytes[at + 1];
            at += 2;
            return value;
        };
        // The record and the handshake headers: a handshake, a ClientHello.
        if (!need(9) || bytes[0] != 0x16 || bytes[5] != 0x01) {
            return std::nullopt;
        }
        at = 9;
        if (!need(2 + 32 + 1)) {
            return std::nullopt;
        }
        at += 2 + 32;
        at += u8();
        if (!need(2)) {
            return std::nullopt;
        }
        at += u16();
        if (!need(1)) {
            return std::nullopt;
        }
        at += u8();
        if (!need(2)) {
            return std::string{};
        }
        const std::size_t end = at + 2 + ((bytes[at] << 8) | bytes[at + 1]);
        at += 2;
        while (at + 4 <= std::min(end, bytes.size())) {
            const std::size_t type = u16();
            const std::size_t length = u16();
            if (type == 0 && need(length) && length >= 5) {
                at += 3; // the list's length, and the name's type
                const std::size_t nameLength = u16();
                if (!need(nameLength)) {
                    return std::nullopt;
                }
                return std::string{bytes.begin() + static_cast<std::ptrdiff_t>(at),
                                   bytes.begin() + static_cast<std::ptrdiff_t>(at + nameLength)};
            }
            at += length;
        }
        return std::string{};
    }

    int m_fd = -1;
    bool m_listening = false;
    std::atomic<bool> m_stopping{false};
    mutable std::mutex m_mutex;
    std::optional<std::string> m_name;
    std::thread m_thread;
};

/// **A connection that is slow to be made**: takes the client, waits, and
/// only then passes everything on to \a to and back, as a far server
/// whose handshake takes its time.
class SlowProxy
{
public:
    SlowProxy(std::uint16_t port, std::uint16_t to, std::chrono::milliseconds wait)
        : m_to(to)
        , m_wait(wait)
    {
        m_fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(port);
        (void)::bind(m_fd, reinterpret_cast<sockaddr *>(&address), sizeof address);
        (void)::listen(m_fd, 4);
        m_thread = std::thread([this] { run(); });
    }

    ~SlowProxy()
    {
        m_stopping = true;
        m_thread.join();
        ::close(m_fd);
    }

    SlowProxy(const SlowProxy &) = delete;
    SlowProxy &operator=(const SlowProxy &) = delete;

private:
    void run()
    {
        int client = -1;
        while (!m_stopping && client < 0) {
            pollfd entry{m_fd, POLLIN, 0};
            if (::poll(&entry, 1, 20) > 0) {
                client = ::accept4(m_fd, nullptr, nullptr, SOCK_CLOEXEC);
            }
        }
        if (client < 0) {
            return;
        }
        const auto until = std::chrono::steady_clock::now() + m_wait;
        while (!m_stopping && std::chrono::steady_clock::now() < until) {
            std::this_thread::sleep_for(10ms);
        }
        const int server = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(m_to);
        if (::connect(server, reinterpret_cast<sockaddr *>(&address), sizeof address) == 0) {
            while (!m_stopping) {
                pollfd both[2] = {{client, POLLIN, 0}, {server, POLLIN, 0}};
                if (::poll(both, 2, 20) <= 0) {
                    continue;
                }
                bool closed = false;
                for (int side = 0; side < 2; ++side) {
                    if ((both[side].revents & (POLLIN | POLLHUP | POLLERR)) == 0) {
                        continue;
                    }
                    char chunk[16384];
                    const ssize_t got = ::recv(both[side].fd, chunk, sizeof chunk, 0);
                    if (got <= 0
                        || ::send(both[1 - side].fd, chunk, static_cast<std::size_t>(got), MSG_NOSIGNAL) != got) {
                        closed = true;
                    }
                }
                if (closed) {
                    break;
                }
            }
        }
        ::close(server);
        ::close(client);
    }

    std::uint16_t m_to;
    std::chrono::milliseconds m_wait;
    int m_fd = -1;
    std::atomic<bool> m_stopping{false};
    std::thread m_thread;
};

/// How many pixels of the frame's top half differ from \a colour: where
/// the words are.
[[nodiscard]] int wordsIn(const Canvas &frame, Rgb colour)
{
    int count = 0;
    for (int y = frame.height() / 5; y < frame.height() / 2; ++y) {
        for (int x = 0; x < frame.width(); ++x) {
            count += frame.at(x, y) == colour ? 0 : 1;
        }
    }
    return count;
}

} // namespace

TEST(RtmpUrlTest, ReadsWhatServicesGive)
{
    const auto plain = parseRtmpUrl("rtmp://a.example.com/live2");
    ASSERT_TRUE(plain.has_value());
    EXPECT_FALSE(plain->secure);
    EXPECT_EQ(plain->host, "a.example.com");
    EXPECT_EQ(plain->port, 1935);
    EXPECT_EQ(plain->app, "live2");
    EXPECT_EQ(rtmpUrl(*plain), "rtmp://a.example.com/live2");

    const auto secure = parseRtmpUrl("  RTMPS://live.example.com:443/rtmp/  ");
    ASSERT_TRUE(secure.has_value());
    EXPECT_TRUE(secure->secure);
    EXPECT_EQ(secure->port, 443);
    EXPECT_EQ(secure->app, "rtmp");
    EXPECT_EQ(rtmpUrl(*secure), "rtmps://live.example.com/rtmp");

    const auto numbered = parseRtmpUrl("rtmp://[::1]:1936/app/instance");
    ASSERT_TRUE(numbered.has_value());
    EXPECT_EQ(numbered->host, "::1");
    EXPECT_EQ(numbered->port, 1936);
    EXPECT_EQ(numbered->app, "app/instance");
    EXPECT_EQ(rtmpUrl(*numbered), "rtmp://[::1]:1936/app/instance");
}

TEST(RtmpUrlTest, RefusesWhatIsNotAnRtmpAddress)
{
    for (const char *url : {"",
                            "http://example.com/live",
                            "rtmp://",
                            "rtmp://:1935/live",
                            "rtmp://host:0/live",
                            "rtmp://host:70000/live",
                            "rtmp://host:12x/live",
                            "rtmp://user:pass@host/live",
                            "rtmp://ho st/live",
                            "rtmp://host/li\nve",
                            "rtmp://[::1/live"}) {
        EXPECT_EQ(parseRtmpUrl(url).error_or(StreamError::Closed), StreamError::NotConfigured) << url;
    }
}

TEST(LiveQualityTest, ThePresetsAreTheSizesAndRatesOffered)
{
    EXPECT_EQ(liveQuality(LiveQualityPreset::Good), (LiveQuality{1024, 576, 15, 800, 96}));
    EXPECT_EQ(liveQuality(LiveQualityPreset::Higher), (LiveQuality{1280, 720, 30, 2500, 128}));
    EXPECT_EQ(liveQuality(LiveQualityPreset::Best), (LiveQuality{1920, 1080, 30, 4500, 320}));
}

class LiveStreamTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!probeVideoEncoders().any()) {
            GTEST_SKIP() << "this system's FFmpeg has no H.264 encoder that works";
        }
        port = freeRtmpPort();
        ASSERT_NE(port, 0);
    }

    std::uint16_t port = 0;
};

// **Picture and sound arrive, and keep time with each other**: the frames
// come at the rate asked, and the last of the picture and the last of the
// sound are no further apart than a frame or two.
TEST_F(LiveStreamTest, StreamsPictureAndSoundThatKeepTime)
{
    auto listener = std::make_unique<RtmpListener>(port, "live", "secret-key");
    Statuses statuses;
    auto tap = tenSeconds();
    Feeder feeder(tap);
    auto live = std::make_unique<LiveStream>(tap, statuses.sink(), quick());
    live->configure(settingsFor(port));
    live->setPassword("secret-key");
    ASSERT_TRUE(waitFor([&] { return statuses.last().stream.state == State::Connected; }))
        << "state " << broadcastStateName(statuses.last().stream.state);
    ASSERT_TRUE(waitFor([&] { return listener->received().videoTimes.size() >= 45; }));

    const LiveStreamStatus status = statuses.last();
    EXPECT_FALSE(status.encoder.empty());
    EXPECT_EQ(status.width, 320);
    EXPECT_EQ(status.height, 180);
    EXPECT_EQ(status.fps, 15);
    EXPECT_GT(status.framesSent, 0U);
    EXPECT_EQ(status.stream.sampleRate, 48000U);
    EXPECT_TRUE(waitFor([&] { return statuses.last().stream.bytesSent > 0 && statuses.last().stream.kbps > 0.0; }));

    live.reset();
    ASSERT_TRUE(waitFor([&] { return listener->ended(); }));
    const ReceivedStream received = listener->received();
    EXPECT_TRUE(received.opened);
    EXPECT_EQ(received.videoCodec, AV_CODEC_ID_H264);
    EXPECT_EQ(received.audioCodec, AV_CODEC_ID_AAC);
    EXPECT_EQ(received.width, 320);
    EXPECT_EQ(received.height, 180);
    EXPECT_EQ(received.sampleRate, 48000);
    EXPECT_GE(received.keyFrames, 1U);
    ASSERT_GE(received.videoTimes.size(), 45U);
    ASSERT_FALSE(received.audioTimes.empty());
    // A frame every fifteenth of a second, in order.
    for (std::size_t index = 1; index < received.videoTimes.size(); ++index) {
        EXPECT_NEAR(received.videoTimes[index] - received.videoTimes[index - 1], 1.0 / 15.0, 0.002) << index;
    }
    EXPECT_NEAR(received.videoTimes.back(), received.audioTimes.back(), 0.2);
}

TEST_F(LiveStreamTest, ThePictureSaysWhatIsPlaying)
{
    RtmpListener listener(port, "live", "key");
    Statuses statuses;
    auto tap = tenSeconds();
    Feeder feeder(tap);
    LiveStream live(tap, statuses.sink(), quick());
    live.configure(settingsFor(port));
    live.setPassword("key");
    ASSERT_TRUE(waitFor([&] { return statuses.last().stream.state == State::Connected; }));
    live.setNowPlaying(Track{"Artist", "Song", "Album"});
    EXPECT_TRUE(waitFor([&] { return statuses.last().stream.title == "Artist - Song"; }));
}

// **A server that goes is tried again**, and the count says so.
TEST_F(LiveStreamTest, AServerThatGoesIsTriedAgain)
{
    auto listener = std::make_unique<RtmpListener>(port, "live", "key");
    Statuses statuses;
    auto tap = tenSeconds();
    Feeder feeder(tap);
    LiveStream live(tap, statuses.sink(), quick());
    live.configure(settingsFor(port));
    live.setPassword("key");
    ASSERT_TRUE(waitFor([&] { return statuses.last().stream.state == State::Connected; }));
    ASSERT_TRUE(waitFor([&] { return !listener->received().videoTimes.empty(); }));
    listener.reset();
    ASSERT_TRUE(waitFor([&] {
        return statuses.ever([](const LiveStreamStatus &status) { return status.stream.state == State::Retrying; });
    }));
    listener = std::make_unique<RtmpListener>(port, "live", "key");
    ASSERT_TRUE(waitFor(
        [&] { return statuses.last().stream.state == State::Connected && statuses.last().stream.reconnects == 1; }));
    EXPECT_TRUE(waitFor([&] { return !listener->received().videoTimes.empty(); }));
}

TEST_F(LiveStreamTest, WithoutAKeyItWaitsAndThenSaysSo)
{
    Statuses statuses;
    auto tap = tenSeconds();
    LiveStream live(tap, statuses.sink(), quick());
    live.configure(settingsFor(port));
    ASSERT_TRUE(waitFor([&] { return statuses.last().stream.state == State::WaitingForPassword; }));
    live.setPassword(std::nullopt);
    ASSERT_TRUE(waitFor([&] { return statuses.last().stream.state == State::Failed; }));
    EXPECT_EQ(statuses.last().stream.error, StreamError::NoPassword);
    live.setPasswordUnavailable();
    ASSERT_TRUE(waitFor([&] { return statuses.last().stream.error == StreamError::PasswordUnavailable; }));
}

TEST_F(LiveStreamTest, AnAddressThatIsNotOneIsNotTried)
{
    Statuses statuses;
    auto tap = tenSeconds();
    LiveStream live(tap, statuses.sink(), quick());
    LiveStreamSettings settings = settingsFor(port);
    settings.url = "http://example.com/live";
    live.configure(settings);
    live.setPassword("key");
    ASSERT_TRUE(waitFor([&] { return statuses.last().stream.state == State::Failed; }));
    EXPECT_EQ(statuses.last().stream.error, StreamError::NotConfigured);
}

// **No encoder is found before the server is asked**, and stops it until
// something changes.
TEST_F(LiveStreamTest, NoEncoderStopsItBeforeTheServerIsAsked)
{
    RtmpListener listener(port, "live", "key");
    Statuses statuses;
    auto tap = tenSeconds();
    Feeder feeder(tap);
    LiveStreamOptions options = quick();
    options.probe = VideoEncoderProbe{};
    LiveStream live(tap, statuses.sink(), options);
    live.configure(settingsFor(port));
    live.setPassword("key");
    ASSERT_TRUE(waitFor([&] { return statuses.last().stream.state == State::Failed; }));
    EXPECT_EQ(statuses.last().stream.error, StreamError::EncoderMissing);
    std::this_thread::sleep_for(300ms);
    EXPECT_FALSE(listener.received().opened);
}

// **The encoder asked for is the one used**, when it is here.
TEST_F(LiveStreamTest, TheEncoderAskedForIsTaken)
{
    const VideoEncoderProbe &probe = probeVideoEncoders();
    for (const VideoEncoderKind kind : {VideoEncoderKind::X264, VideoEncoderKind::OpenH264}) {
        if (!probe.has(kind)) {
            continue;
        }
        RtmpListener listener(port, "live", "key");
        Statuses statuses;
        auto tap = tenSeconds();
        Feeder feeder(tap);
        LiveStream live(tap, statuses.sink(), quick());
        LiveStreamSettings settings = settingsFor(port);
        settings.encoder = kind;
        live.configure(settings);
        live.setPassword("key");
        ASSERT_TRUE(waitFor([&] { return statuses.last().stream.state == State::Connected; }));
        EXPECT_EQ(statuses.last().encoder, videoEncoderId(kind));
        port = freeRtmpPort();
    }
}

// **A stop never waits for the network**: a server that takes the
// connection and then says nothing holds the handshake for as long as it
// likes, and the stream still stops at once.
TEST_F(LiveStreamTest, StoppingNeverWaitsForAServerThatSaysNothing)
{
    const int silent = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    ASSERT_EQ(::bind(silent, reinterpret_cast<sockaddr *>(&address), sizeof address), 0);
    ASSERT_EQ(::listen(silent, 4), 0);

    Statuses statuses;
    auto tap = tenSeconds();
    Feeder feeder(tap);
    LiveStreamOptions options = quick();
    options.connectTimeout = 60s;
    auto live = std::make_unique<LiveStream>(tap, statuses.sink(), options);
    live->configure(settingsFor(port));
    live->setPassword("key");
    ASSERT_TRUE(waitFor([&] { return statuses.last().stream.state == State::Connecting; }));
    std::this_thread::sleep_for(500ms);
    const auto before = std::chrono::steady_clock::now();
    live.reset();
    EXPECT_LT(std::chrono::steady_clock::now() - before, 1s);
    ::close(silent);
}

// **RTMPS names the server in its TLS hello** (SNI): a service with many
// names behind one address refuses a hello without one.
TEST_F(LiveStreamTest, RtmpsNamesTheServerInItsHello)
{
    HelloListener server(port);
    ASSERT_TRUE(server.listening());
    Statuses statuses;
    auto tap = tenSeconds();
    Feeder feeder(tap);
    LiveStream live(tap, statuses.sink(), quick());
    LiveStreamSettings settings = settingsFor(port);
    settings.url = "rtmps://localhost:" + std::to_string(port) + "/live";
    live.configure(settings);
    live.setPassword("key");
    ASSERT_TRUE(waitFor([&] { return server.serverName().has_value(); })) << "no TLS hello came";
    EXPECT_EQ(*server.serverName(), "localhost");
    ASSERT_TRUE(waitFor([&] {
        return statuses.ever([](const LiveStreamStatus &status) { return status.stream.state == State::Retrying; });
    }));
}

// **Connecting by name never holds a stop**: a TLS server that takes the
// connection and never answers its hello is left behind at once.
TEST_F(LiveStreamTest, StoppingNeverWaitsForAnRtmpsServerThatSaysNothing)
{
    const int silent = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    ASSERT_EQ(::bind(silent, reinterpret_cast<sockaddr *>(&address), sizeof address), 0);
    ASSERT_EQ(::listen(silent, 4), 0);
    Statuses statuses;
    auto tap = tenSeconds();
    Feeder feeder(tap);
    LiveStreamOptions options = quick();
    options.connectTimeout = 60s;
    auto live = std::make_unique<LiveStream>(tap, statuses.sink(), options);
    LiveStreamSettings settings = settingsFor(port);
    settings.url = "rtmps://localhost:" + std::to_string(port) + "/live";
    live->configure(settings);
    live->setPassword("key");
    ASSERT_TRUE(waitFor([&] { return statuses.last().stream.state == State::Connecting; }));
    std::this_thread::sleep_for(500ms);
    const auto before = std::chrono::steady_clock::now();
    live.reset();
    EXPECT_LT(std::chrono::steady_clock::now() - before, 1s);
    ::close(silent);
}

// **A new quality keeps the words**: the picture is drawn again at the new
// size, with the same fonts.
TEST_F(LiveStreamTest, ANewQualityKeepsTheWords)
{
    if (!TextPainter::create()) {
        GTEST_SKIP() << "this system has no fonts";
    }
    auto listener = std::make_unique<RtmpListener>(port, "live", "key");
    Statuses statuses;
    auto tap = tenSeconds();
    Feeder feeder(tap);
    std::mutex mutex;
    int width = 0;
    int words = 0;
    LiveStreamOptions options = quick();
    options.onFrame = [&](const Canvas &frame) {
        const std::lock_guard lock(mutex);
        width = frame.width();
        words = wordsIn(frame, Rgb{0x16, 0x18, 0x1d});
    };
    LiveStream live(tap, statuses.sink(), options);
    LiveStreamSettings settings = settingsFor(port);
    settings.scene.visualisation = Visualisation::None;
    live.configure(settings);
    live.setPassword("key");
    const auto seen = [&](int wanted) {
        const std::lock_guard lock(mutex);
        return width == wanted && words > 100;
    };
    ASSERT_TRUE(waitFor([&] { return seen(320); }));
    settings.quality = LiveQuality{480, 270, 15, 400, 96};
    live.configure(settings);
    // A new quality is a new connection; the server takes one a time.
    ASSERT_TRUE(waitFor([&] { return listener->ended(); }));
    listener = std::make_unique<RtmpListener>(port, "live", "key");
    EXPECT_TRUE(waitFor([&] { return seen(480); })) << "the words went with the old size";
}

// **What arrived while connecting is let go**: after a slow handshake the
// sound goes out as it is made, not as a burst of what waited.
TEST_F(LiveStreamTest, ASlowConnectionSendsNoBurstOfStaleSound)
{
    const std::uint16_t serverPort = freeRtmpPort();
    ASSERT_NE(serverPort, port);
    RtmpListener listener(serverPort, "live", "key");
    SlowProxy proxy(port, serverPort, 1500ms);
    Statuses statuses;
    auto tap = tenSeconds();
    Feeder feeder(tap);
    LiveStream live(tap, statuses.sink(), quick());
    live.configure(settingsFor(port));
    live.setPassword("key");
    ASSERT_TRUE(waitFor([&] { return statuses.last().stream.state == State::Connected; }));
    const auto connected = std::chrono::steady_clock::now();
    ASSERT_TRUE(waitFor([&] { return listener.received().audioTimes.size() >= 50; }));
    // **No further ahead than the time since it connected**: the sound
    // that waited for the slow handshake, sent too, would put it a second
    // and a half ahead.
    const ReceivedStream received = listener.received();
    ASSERT_FALSE(received.audioTimes.empty());
    const double sent = *std::ranges::max_element(received.audioTimes);
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - connected).count();
    EXPECT_LT(sent, elapsed + 0.5) << "a burst of what waited while connecting";
}
