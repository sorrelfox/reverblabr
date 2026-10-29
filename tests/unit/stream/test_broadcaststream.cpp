// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The thread that streams, against a server made here: it connects with
// what the settings say, sends what the tap holds, gives the server the
// title, tries again when the server goes, and gives up when trying again
// cannot help. A real Icecast is tests/broadcast.

#include "fakeserver.h"
#include "rt/audiotap.h"
#include "stream/broadcaststream.h"
#include "stream/streamencoder.h"
#include "streamdecode.h"

#include <gtest/gtest.h>
#include <sys/stat.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <numbers>
#include <optional>
#include <thread>
#include <vector>

using namespace rb;
using namespace rb::stream;
using namespace rb::test;
using namespace std::chrono_literals;

namespace
{

using State = BroadcastStatus::State;

/// What a session does to a Broadcast block's tap: a cycle of a tone every
/// few milliseconds, at 48 kHz, for as long as it lives.
class Feeder
{
public:
    explicit Feeder(std::shared_ptr<rt::AudioTap> tap, std::uint32_t rate = 48000)
        : m_tap(std::move(tap))
        , m_rate(rate)
        , m_thread([this] { run(); })
    {
    }

    /// The rate from the next cycle on, as PipeWire moving the graph does.
    void setRate(std::uint32_t rate)
    {
        m_rate = rate;
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
        std::vector<float> left(kFrames);
        std::uint64_t at = 0;
        while (!m_stopping) {
            for (std::uint32_t frame = 0; frame < kFrames; ++frame, ++at) {
                left[frame] = static_cast<float>(0.3 * std::sin(2.0 * std::numbers::pi * 440.0 * at / 48000.0));
            }
            const float *channels[] = {left.data(), left.data()};
            m_tap->announceRate(m_rate.load());
            (void)m_tap->write(channels, kFrames);
            std::this_thread::sleep_for(10ms);
        }
    }

    std::shared_ptr<rt::AudioTap> m_tap;
    std::atomic<std::uint32_t> m_rate;
    std::atomic<bool> m_stopping{false};
    std::thread m_thread;
};

/// Every status a stream said, for a test to wait on.
class Statuses
{
public:
    BroadcastStream::StatusSink sink()
    {
        return [this](const BroadcastStatus &status) {
            const std::lock_guard lock(m_mutex);
            m_said.push_back(status);
        };
    }

    [[nodiscard]] BroadcastStatus last() const
    {
        const std::lock_guard lock(m_mutex);
        return m_said.empty() ? BroadcastStatus{} : m_said.back();
    }

    [[nodiscard]] bool ever(const std::function<bool(const BroadcastStatus &)> &match) const
    {
        const std::lock_guard lock(m_mutex);
        return std::ranges::any_of(m_said, match);
    }

private:
    mutable std::mutex m_mutex;
    std::vector<BroadcastStatus> m_said;
};

[[nodiscard]] bool waitFor(const std::function<bool()> &condition, std::chrono::milliseconds timeout = 10s)
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

[[nodiscard]] BroadcastOptions quick()
{
    BroadcastOptions options;
    options.pollInterval = 5ms;
    options.connectTimeout = 2s;
    options.sendTimeout = 2s;
    options.statusInterval = 100ms;
    options.backoff.first = 100ms;
    options.backoff.longest = 400ms;
    options.backoff.lasting = 200ms;
    return options;
}

[[nodiscard]] BroadcastSettings settingsFor(std::uint16_t port, StreamFormat format = StreamFormat::Mp3)
{
    BroadcastSettings settings;
    settings.server.host = "127.0.0.1";
    settings.server.port = port;
    settings.server.mount = "/live";
    settings.station.name = "Test Radio";
    settings.format = format;
    settings.bitrateKbps = 128;
    settings.sessionName = "Show";
    return settings;
}

[[nodiscard]] std::shared_ptr<rt::AudioTap> makeTap()
{
    return std::make_shared<rt::AudioTap>(2, 480000);
}

[[nodiscard]] bool formatAvailable(StreamFormat format)
{
    return !streamFormatUnavailable(format).has_value();
}

} // namespace

TEST(BroadcastStreamTest, ItStreamsWhatTheTapHoldsToTheServer)
{
    if (!formatAvailable(StreamFormat::Mp3)) {
        GTEST_SKIP() << "this FFmpeg has no MP3 encoder";
    }
    FakeStreamServer server;
    Statuses statuses;
    auto tap = makeTap();
    const Feeder feeder(tap);
    BroadcastStream stream(tap, statuses.sink(), quick());
    stream.configure(settingsFor(server.port()));
    stream.setPassword("hackme");

    ASSERT_TRUE(waitFor([&] { return server.received().size() > 32000; })) << "a second's worth never arrived";
    const std::vector<std::string> requests = server.requests();
    ASSERT_EQ(requests.size(), 1U);
    EXPECT_TRUE(requests[0].starts_with("PUT /live HTTP/1.1\r\n"));
    EXPECT_NE(requests[0].find("Ice-Name: Test Radio\r\n"), std::string::npos);

    ASSERT_TRUE(waitFor([&] { return statuses.last().bytesSent > 32000; }));
    const BroadcastStatus status = statuses.last();
    EXPECT_EQ(status.state, State::Connected);
    EXPECT_GT(status.connectedSinceMs, 0);
    EXPECT_EQ(status.reconnects, 0U);
    EXPECT_EQ(status.sampleRate, 48000U);
    // Measured, not the setting read back: what went out over the last
    // seconds, a little over 128 kbit/s once the frames' own headers are
    // counted.
    ASSERT_TRUE(waitFor([&] { return statuses.last().kbps > 100.0; }));
    EXPECT_LT(statuses.last().kbps, 200.0);

    const std::string received = server.received();
    const Decoded decoded = decode(std::vector<char>(received.begin(), received.end()));
    ASSERT_TRUE(decoded.opened);
    EXPECT_EQ(decoded.codec, "mp3");
    EXPECT_GT(decoded.peak, 0.2f);
}

TEST(BroadcastStreamTest, WithoutAPasswordItWaitsAndSaysSoAndNeverConnects)
{
    FakeStreamServer server;
    Statuses statuses;
    auto tap = makeTap();
    BroadcastStream stream(tap, statuses.sink(), quick());
    stream.configure(settingsFor(server.port()));
    ASSERT_TRUE(waitFor([&] { return statuses.last().state == State::WaitingForPassword; }));
    stream.setPassword(std::nullopt);
    ASSERT_TRUE(waitFor([&] { return statuses.last().state == State::Failed; }));
    EXPECT_EQ(statuses.last().error, StreamError::NoPassword);
    std::this_thread::sleep_for(300ms);
    EXPECT_EQ(server.sources(), 0U);
}

TEST(BroadcastStreamTest, AWrongPasswordIsNotTriedAgainUntilThereIsAnother)
{
    FakeStreamServer server;
    server.setAnswer("HTTP/1.0 401 Authentication Required");
    Statuses statuses;
    auto tap = makeTap();
    const Feeder feeder(tap);
    BroadcastStream stream(tap, statuses.sink(), quick());
    stream.configure(settingsFor(server.port()));
    stream.setPassword("wrong");
    ASSERT_TRUE(waitFor([&] { return statuses.last().state == State::Failed; }));
    EXPECT_EQ(statuses.last().error, StreamError::AuthRejected);
    // Several of the backoff's waits go by, and nothing is asked again.
    std::this_thread::sleep_for(800ms);
    EXPECT_EQ(server.sources(), 1U);

    server.setAnswer("HTTP/1.1 200 OK");
    stream.setPassword("right");
    ASSERT_TRUE(waitFor([&] { return statuses.last().state == State::Connected; }));
    EXPECT_EQ(server.sources(), 2U);
}

TEST(BroadcastStreamTest, AServerThatHangsUpIsConnectedToAgainAndTheCountsGoOn)
{
    if (!formatAvailable(StreamFormat::Mp3)) {
        GTEST_SKIP() << "this FFmpeg has no MP3 encoder";
    }
    FakeStreamServer server;
    Statuses statuses;
    auto tap = makeTap();
    const Feeder feeder(tap);
    BroadcastStream stream(tap, statuses.sink(), quick());
    stream.configure(settingsFor(server.port()));
    stream.setPassword("hackme");
    ASSERT_TRUE(waitFor([&] { return statuses.last().bytesSent > 8000; }));
    const std::uint64_t before = statuses.last().bytesSent;

    server.hangUp();
    ASSERT_TRUE(waitFor(
        [&] { return statuses.ever([](const BroadcastStatus &status) { return status.state == State::Retrying; }); }));
    ASSERT_TRUE(waitFor([&] { return statuses.last().state == State::Connected && statuses.last().reconnects == 1; }));
    EXPECT_EQ(server.sources(), 2U);
    EXPECT_TRUE(statuses.ever([](const BroadcastStatus &status) {
        return status.state == State::Retrying && status.error == StreamError::Closed;
    }));
    // **Bytes sent count across connections**: the stream's, not the
    // connection's.
    ASSERT_TRUE(waitFor([&] { return statuses.last().bytesSent > before + 8000; }));
}

TEST(BroadcastStreamTest, NobodyThereIsTriedAgainAfterAWait)
{
    std::uint16_t port = 0;
    {
        const FakeStreamServer gone;
        port = gone.port();
    }
    Statuses statuses;
    auto tap = makeTap();
    BroadcastStream stream(tap, statuses.sink(), quick());
    stream.configure(settingsFor(port));
    stream.setPassword("hackme");
    ASSERT_TRUE(waitFor([&] {
        return statuses.ever([](const BroadcastStatus &status) {
            return status.state == State::Retrying && status.error == StreamError::ConnectFailed
                && status.retryInMs > 0;
        });
    }));
}

TEST(BroadcastStreamTest, ANewTitleGoesToTheServerAsAMetadataRequestForMp3)
{
    if (!formatAvailable(StreamFormat::Mp3)) {
        GTEST_SKIP() << "this FFmpeg has no MP3 encoder";
    }
    FakeStreamServer server;
    Statuses statuses;
    auto tap = makeTap();
    const Feeder feeder(tap);
    BroadcastStream stream(tap, statuses.sink(), quick());
    stream.configure(settingsFor(server.port()));
    stream.setPassword("hackme");
    ASSERT_TRUE(waitFor([&] { return statuses.last().state == State::Connected; }));
    EXPECT_TRUE(server.titleRequests().empty()) << "there was no title to give";

    stream.setNowPlaying(Track{.artist = "Ana", .title = "Morning"});
    ASSERT_TRUE(waitFor([&] { return statuses.last().title == "Ana - Morning"; }));
    ASSERT_EQ(server.titleRequests().size(), 1U);
    EXPECT_NE(server.titleRequests()[0].find("song=Ana%20-%20Morning"), std::string::npos);

    // **A title-only change does not connect again**, and the same title
    // again is not sent again.
    BroadcastSettings settings = settingsFor(server.port());
    settings.titleTemplate = "%title";
    stream.configure(settings);
    ASSERT_TRUE(waitFor([&] { return statuses.last().title == "Morning"; }));
    stream.setNowPlaying(Track{.artist = "Someone else", .title = "Morning"});
    std::this_thread::sleep_for(200ms);
    EXPECT_EQ(server.titleRequests().size(), 2U);
    EXPECT_EQ(server.sources(), 1U);
}

TEST(BroadcastStreamTest, AnOggStreamsTitleIsInTheStreamItself)
{
    if (!formatAvailable(StreamFormat::Vorbis)) {
        GTEST_SKIP() << "this FFmpeg has no Vorbis encoder";
    }
    FakeStreamServer server;
    Statuses statuses;
    auto tap = makeTap();
    const Feeder feeder(tap);
    BroadcastStream stream(tap, statuses.sink(), quick());
    stream.configure(settingsFor(server.port(), StreamFormat::Vorbis));
    stream.setPassword("hackme");
    stream.setNowPlaying(Track{.title = "First"});
    ASSERT_TRUE(waitFor([&] { return statuses.last().state == State::Connected; }));
    EXPECT_EQ(statuses.last().title, "First");
    stream.setNowPlaying(Track{.title = "Second"});
    ASSERT_TRUE(waitFor([&] { return statuses.last().title == "Second"; }));
    ASSERT_TRUE(waitFor([&] { return server.received().find("Second") != std::string::npos; }));
    EXPECT_NE(server.received().find("First"), std::string::npos);
    EXPECT_TRUE(server.titleRequests().empty());
    EXPECT_EQ(server.sources(), 1U);
}

TEST(BroadcastStreamTest, TheNowPlayingFileIsTheTitleUntilSomethingHandsOneIn)
{
    if (!formatAvailable(StreamFormat::Mp3)) {
        GTEST_SKIP() << "this FFmpeg has no MP3 encoder";
    }
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / "rb-broadcast-nowplaying";
    std::filesystem::create_directories(folder);
    const std::filesystem::path file = folder / "song.txt";
    std::ofstream(file) << "From the file\n";

    FakeStreamServer server;
    Statuses statuses;
    auto tap = makeTap();
    const Feeder feeder(tap);
    BroadcastStream stream(tap, statuses.sink(), quick());
    BroadcastSettings settings = settingsFor(server.port());
    settings.nowPlayingFile = file;
    stream.configure(settings);
    stream.setPassword("hackme");
    ASSERT_TRUE(waitFor([&] { return statuses.last().title == "From the file"; }));
    stream.setNowPlaying(Track{.title = "Handed in"});
    ASSERT_TRUE(waitFor([&] { return statuses.last().title == "Handed in"; }));
    std::filesystem::remove_all(folder);
}

TEST(BroadcastStreamTest, ShoutcastLogsInWithThePasswordOnThePortAfterTheListeners)
{
    if (!formatAvailable(StreamFormat::Mp3)) {
        GTEST_SKIP() << "this FFmpeg has no MP3 encoder";
    }
    FakeStreamServer server;
    server.setShoutcast("hackme");
    Statuses statuses;
    auto tap = makeTap();
    const Feeder feeder(tap);
    BroadcastStream stream(tap, statuses.sink(), quick());
    BroadcastSettings settings = settingsFor(static_cast<std::uint16_t>(server.port() - 1));
    settings.server.kind = ServerKind::Shoutcast;
    stream.configure(settings);
    stream.setPassword("hackme");
    ASSERT_TRUE(waitFor([&] { return server.received().size() > 16000; }));
    ASSERT_EQ(server.requests().size(), 1U);
    EXPECT_NE(server.requests()[0].find("icy-name:Test Radio\r\n"), std::string::npos);
    EXPECT_EQ(statuses.last().state, State::Connected);

    stream.setPassword("wrong");
    ASSERT_TRUE(waitFor([&] { return statuses.last().state == State::Failed; }));
    EXPECT_EQ(statuses.last().error, StreamError::AuthRejected);
}

TEST(BroadcastStreamTest, NoHostIsNotConfiguredAndStoppingSaysIdle)
{
    Statuses statuses;
    auto tap = makeTap();
    {
        BroadcastStream stream(tap, statuses.sink(), quick());
        BroadcastSettings settings;
        settings.server.host = "";
        stream.configure(settings);
        stream.setPassword("hackme");
        ASSERT_TRUE(waitFor([&] { return statuses.last().state == State::Failed; }));
        EXPECT_EQ(statuses.last().error, StreamError::NotConfigured);
    }
    EXPECT_EQ(statuses.last().state, State::Idle);
}

TEST(BroadcastStreamTest, ASessionAt44KhzIsStreamedAt44AndAMoveTo48ConnectsAgain)
{
    if (!formatAvailable(StreamFormat::Mp3)) {
        GTEST_SKIP() << "this FFmpeg has no MP3 encoder";
    }
    FakeStreamServer server;
    Statuses statuses;
    auto tap = makeTap();
    Feeder feeder(tap, 44100);
    BroadcastStream stream(tap, statuses.sink(), quick());
    stream.configure(settingsFor(server.port()));
    stream.setPassword("hackme");
    ASSERT_TRUE(waitFor([&] { return statuses.last().state == State::Connected; }));
    EXPECT_EQ(statuses.last().sampleRate, 44100U) << "the stream was made before the session said its rate";
    EXPECT_NE(server.requests()[0].find("ice-samplerate=44100"), std::string::npos);

    // MP3 at 44.1 kHz cannot become 48 kHz in the middle: a new connection.
    feeder.setRate(48000);
    ASSERT_TRUE(
        waitFor([&] { return statuses.last().state == State::Connected && statuses.last().sampleRate == 48000; }));
    EXPECT_EQ(server.sources(), 2U);
}

TEST(BroadcastStreamTest, ABitRateTheFormatDoesNotTakeIsBroughtIntoItsRangeBeforeTheServerIsAsked)
{
    if (!formatAvailable(StreamFormat::Vorbis)) {
        GTEST_SKIP() << "this FFmpeg has no Vorbis encoder";
    }
    FakeStreamServer server;
    Statuses statuses;
    auto tap = makeTap();
    const Feeder feeder(tap);
    BroadcastStream stream(tap, statuses.sink(), quick());
    BroadcastSettings settings = settingsFor(server.port(), StreamFormat::Vorbis);
    settings.bitrateKbps = 32; // Vorbis takes 48 at least for stereo
    stream.configure(settings);
    stream.setPassword("hackme");
    ASSERT_TRUE(waitFor([&] { return statuses.last().state == State::Connected; }));
    EXPECT_NE(server.requests()[0].find("ice-bitrate=48"), std::string::npos) << server.requests()[0];
}

TEST(BroadcastStreamTest, ASlowTitleRequestDoesNotHoldTheAudioBack)
{
    if (!formatAvailable(StreamFormat::Mp3)) {
        GTEST_SKIP() << "this FFmpeg has no MP3 encoder";
    }
    FakeStreamServer server;
    server.setTitleDelay(3s);
    Statuses statuses;
    auto tap = makeTap();
    const Feeder feeder(tap);
    BroadcastStream stream(tap, statuses.sink(), quick());
    stream.configure(settingsFor(server.port()));
    stream.setPassword("hackme");
    ASSERT_TRUE(waitFor([&] { return statuses.last().state == State::Connected; }));
    stream.setNowPlaying(Track{.artist = "Ana", .title = "Morning", .album = {}});
    ASSERT_TRUE(waitFor([&] { return server.titleRequests().size() == 1; }));
    // While the server sits on the title, the audio goes on arriving.
    const std::size_t before = server.received().size();
    std::this_thread::sleep_for(1500ms);
    EXPECT_GT(server.received().size(), before + 16000) << "the audio waited for the title";
    EXPECT_EQ(statuses.last().title, "") << "a title the server has not taken yet";
    ASSERT_TRUE(waitFor([&] { return statuses.last().title == "Ana - Morning"; }));
    EXPECT_EQ(server.sources(), 1U);
}

TEST(BroadcastStreamTest, AShoutcastServerThatHangsUpWithoutAWordIsTriedAgain)
{
    FakeStreamServer server;
    server.setShoutcast("hackme");
    server.setShoutcastHangsUp(true);
    Statuses statuses;
    auto tap = makeTap();
    const Feeder feeder(tap);
    BroadcastStream stream(tap, statuses.sink(), quick());
    BroadcastSettings settings = settingsFor(static_cast<std::uint16_t>(server.port() - 1));
    settings.server.kind = ServerKind::Shoutcast;
    stream.configure(settings);
    stream.setPassword("hackme");
    ASSERT_TRUE(waitFor([&] { return server.sources() >= 2; })) << "it gave up after a hang-up";
    EXPECT_FALSE(
        statuses.ever([](const BroadcastStatus &status) { return status.error == StreamError::AuthRejected; }));
}

TEST(BroadcastStreamTest, AStoreThatCouldNotBeAskedIsSaidAndNotTakenForNoPassword)
{
    FakeStreamServer server;
    Statuses statuses;
    auto tap = makeTap();
    const Feeder feeder(tap);
    BroadcastStream stream(tap, statuses.sink(), quick());
    stream.configure(settingsFor(server.port()));
    stream.setPasswordUnavailable();
    ASSERT_TRUE(waitFor([&] { return statuses.last().state == State::Failed; }));
    EXPECT_EQ(statuses.last().error, StreamError::PasswordUnavailable);
    stream.setPassword("hackme");
    ASSERT_TRUE(waitFor([&] { return statuses.last().state == State::Connected; }));
}

// **Stopping never waits for the network**: a server that never answers
// the connection, nor a now-playing "file" that is a pipe nobody writes to.
TEST(BroadcastStreamTest, StoppingDoesNotWaitForAServerThatNeverAnswers)
{
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / "rb-broadcast-fifo";
    std::filesystem::create_directories(folder);
    const std::filesystem::path fifo = folder / "song";
    std::filesystem::remove(fifo);
    ASSERT_EQ(::mkfifo(fifo.c_str(), 0600), 0);

    Statuses statuses;
    auto tap = makeTap();
    std::optional<BroadcastStream> stream(std::in_place, tap, statuses.sink(), quick());
    BroadcastSettings settings = settingsFor(9);
    // An address that swallows connections: nothing answers, nothing refuses.
    settings.server.host = "10.255.255.1";
    settings.nowPlayingFile = fifo;
    stream->configure(settings);
    stream->setPassword("hackme");
    ASSERT_TRUE(waitFor([&] { return statuses.last().state == State::Connecting; }));
    std::this_thread::sleep_for(300ms);
    const auto before = std::chrono::steady_clock::now();
    stream.reset();
    EXPECT_LT(std::chrono::steady_clock::now() - before, 500ms);
    std::filesystem::remove_all(folder);
}

TEST(BroadcastStreamTest, SeveralStreamsToldToStopTogetherStopTogether)
{
    Statuses statuses;
    std::vector<std::unique_ptr<BroadcastStream>> streams;
    for (int index = 0; index < 4; ++index) {
        streams.push_back(std::make_unique<BroadcastStream>(makeTap(), statuses.sink(), quick()));
        BroadcastSettings settings = settingsFor(9);
        settings.server.host = "10.255.255.1";
        streams.back()->configure(settings);
        streams.back()->setPassword("hackme");
    }
    std::this_thread::sleep_for(300ms);
    const auto before = std::chrono::steady_clock::now();
    for (const auto &stream : streams) {
        stream->requestStop();
    }
    streams.clear();
    EXPECT_LT(std::chrono::steady_clock::now() - before, 500ms);
}
