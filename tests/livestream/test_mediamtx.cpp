// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The live stream against a real RTMP server, MediaMTX, in a container: the
// picture and the sound go on the air over RTMP and over RTMPS and play for
// a viewer, a wrong key is refused, a restarted server is connected to
// again, and a long stream keeps the picture with the sound and both with
// the clock.
//
// **Not in the default run**: it needs podman, the image (pulled the first
// time, which needs the network), and a minute. `ctest --preset container`
// runs it. It exits 77, which ctest counts as skipped, when podman is not
// there or cannot run a container, as in CI's own container.
// RB_LIVESTREAM_SOAK_SECONDS sets how long the long stream runs: 1800 is
// half an hour.

#include "rt/audiotap.h"
#include "rtmplistener.h"
#include "stream/connection.h"
#include "stream/livestream.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <gtest/gtest.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <mutex>
#include <numbers>
#include <thread>

using namespace rb;
using namespace rb::stream;
using namespace rb::test;
using namespace std::chrono_literals;
using namespace Qt::StringLiterals;

namespace
{

using State = BroadcastStatus::State;

/// MediaMTX, pinned: the image the test was written against.
constexpr auto kImage = "docker.io/bluenviron/mediamtx:1.21.3";
constexpr auto kKey = "test-stream-key";

struct Ran {
    bool ok = false;
    QString output;
};

Ran run(const QString &program, const QStringList &arguments, std::chrono::milliseconds timeout = 60s)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(program, arguments);
    if (!process.waitForStarted(5000)) {
        return {false, program + u" would not start"_s};
    }
    if (!process.waitForFinished(static_cast<int>(timeout.count()))) {
        process.kill();
        process.waitForFinished(5000);
        return {false, program + u" took too long"_s};
    }
    return {process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0,
            QString::fromLocal8Bit(process.readAll())};
}

Ran podman(const QStringList &arguments, std::chrono::milliseconds timeout = 60s)
{
    return run(u"podman"_s, arguments, timeout);
}

/// The server, for the whole run.
class MediaMtx
{
public:
    static MediaMtx &instance()
    {
        static MediaMtx server;
        return server;
    }

    [[nodiscard]] bool start(QString &why)
    {
        rtmpPort = freeRtmpPort();
        rtmpsPort = freeRtmpPort();
        apiPort = freeRtmpPort();
        if (rtmpPort == 0 || rtmpsPort == 0 || apiPort == 0 || rtmpPort == rtmpsPort || apiPort == rtmpPort
            || apiPort == rtmpsPort) {
            why = u"no free ports"_s;
            return false;
        }
        // A certificate of its own for RTMPS, for "localhost", which the
        // stream is told to trust.
        const QString tls = m_folder.filePath(u"tls"_s);
        QDir().mkpath(tls);
        const Ran made = run(u"openssl"_s,
                             {u"req"_s,
                              u"-x509"_s,
                              u"-newkey"_s,
                              u"ec"_s,
                              u"-pkeyopt"_s,
                              u"ec_paramgen_curve:prime256v1"_s,
                              u"-nodes"_s,
                              u"-days"_s,
                              u"2"_s,
                              u"-subj"_s,
                              u"/CN=localhost"_s,
                              u"-addext"_s,
                              u"subjectAltName=DNS:localhost"_s,
                              u"-keyout"_s,
                              tls + u"/server.key"_s,
                              u"-out"_s,
                              tls + u"/server.crt"_s});
        if (!made.ok) {
            why = u"openssl could not make a certificate: "_s + made.output.trimmed();
            return false;
        }
        caFile = QString(tls + u"/server.crt"_s).toStdString();
        for (const QString &file : {u"/server.key"_s, u"/server.crt"_s}) {
            QFile::setPermissions(tls + file, QFileDevice::ReadOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther);
        }

        QFile in(QStringLiteral(RB_MEDIAMTX_CONFIG));
        if (!in.open(QIODevice::ReadOnly)) {
            why = u"cannot read the configuration"_s;
            return false;
        }
        QString config = QString::fromUtf8(in.readAll());
        config.replace(u"@KEY@"_s, QString::fromLatin1(kKey));
        config.replace(u"@RTMP_PORT@"_s, QString::number(rtmpPort));
        config.replace(u"@RTMPS_PORT@"_s, QString::number(rtmpsPort));
        config.replace(u"@API_PORT@"_s, QString::number(apiPort));
        m_configPath = m_folder.filePath(u"mediamtx.yml"_s);
        QFile out(m_configPath);
        if (!out.open(QIODevice::WriteOnly) || out.write(config.toUtf8()) < 0) {
            why = u"cannot write the configuration"_s;
            return false;
        }
        out.close();
        QFile::setPermissions(m_configPath, QFileDevice::ReadOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther);
        QFile::setPermissions(m_folder.path(),
                              QFileDevice::ReadOwner | QFileDevice::ExeOwner | QFileDevice::WriteOwner
                                  | QFileDevice::ReadOther | QFileDevice::ExeOther);
        QFile::setPermissions(tls,
                              QFileDevice::ReadOwner | QFileDevice::ExeOwner | QFileDevice::WriteOwner
                                  | QFileDevice::ReadOther | QFileDevice::ExeOther);
        m_name = u"reverblabr-test-mediamtx-%1"_s.arg(::getpid());
        return launch(why);
    }

    [[nodiscard]] bool restart()
    {
        QString why;
        (void)podman({u"rm"_s, u"-f"_s, u"-t"_s, u"0"_s, m_name});
        return launch(why);
    }

    void stop()
    {
        if (!m_name.isEmpty()) {
            (void)podman({u"rm"_s, u"-f"_s, u"-t"_s, u"0"_s, m_name});
        }
    }

    /// What the server says of the path \a path: ready, its tracks, the
    /// bytes it has had.
    [[nodiscard]] QJsonObject path(const std::string &path) const
    {
        const std::string answer = get("/v3/paths/get/" + path);
        const std::size_t body = answer.find("\r\n\r\n");
        if (!answer.starts_with("HTTP/1.1 200") || body == std::string::npos) {
            return {};
        }
        return QJsonDocument::fromJson(QByteArray::fromStdString(answer.substr(body + 4))).object();
    }

    std::uint16_t rtmpPort = 0;
    std::uint16_t rtmpsPort = 0;
    std::uint16_t apiPort = 0;
    std::string caFile;

private:
    MediaMtx() = default;
    ~MediaMtx()
    {
        stop();
    }

    [[nodiscard]] bool launch(QString &why)
    {
        const Ran ran = podman({u"run"_s,
                                u"-d"_s,
                                u"--rm"_s,
                                u"--network=host"_s,
                                u"--name"_s,
                                m_name,
                                u"-v"_s,
                                m_configPath + u":/mediamtx.yml:ro,Z"_s,
                                u"-v"_s,
                                m_folder.filePath(u"tls"_s) + u":/tls:ro,Z"_s,
                                QString::fromLatin1(kImage)});
        if (!ran.ok) {
            why = u"podman could not run the server: "_s + ran.output.trimmed();
            return false;
        }
        const auto deadline = std::chrono::steady_clock::now() + 30s;
        while (std::chrono::steady_clock::now() < deadline) {
            if (get("/v3/paths/list").starts_with("HTTP/1.1 200")) {
                return true;
            }
            std::this_thread::sleep_for(100ms);
        }
        why = u"the server did not answer: "_s + podman({u"logs"_s, m_name}).output.trimmed();
        return false;
    }

    [[nodiscard]] std::string get(const std::string &path) const
    {
        const std::atomic<bool> never{false};
        std::expected<Connection, StreamError> connection = Connection::open("127.0.0.1", apiPort, 2s, never);
        if (!connection) {
            return {};
        }
        const std::string request = "GET " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
        if (!connection->send(request, 2s, never)) {
            return {};
        }
        const std::expected<std::string, StreamError> answer =
            connection->receive([](const std::string &, bool closed) { return closed; }, 5s, never, 1 << 20);
        return answer.value_or(std::string{});
    }

    QTemporaryDir m_folder;
    QString m_configPath;
    QString m_name;
};

/// A tone into a tap, a cycle every ten milliseconds against the clock, as
/// a session would.
class Feeder
{
public:
    explicit Feeder(std::shared_ptr<rt::AudioTap> tap)
        : m_tap(std::move(tap))
        , m_thread([this] { feed(); })
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
    void feed()
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

class LastStatus
{
public:
    LiveStream::StatusSink sink()
    {
        return [this](const LiveStreamStatus &status) {
            const std::lock_guard lock(m_mutex);
            m_last = status;
            m_everRetried = m_everRetried || status.stream.state == State::Retrying;
        };
    }

    [[nodiscard]] LiveStreamStatus get() const
    {
        const std::lock_guard lock(m_mutex);
        return m_last;
    }

    [[nodiscard]] bool everRetried() const
    {
        const std::lock_guard lock(m_mutex);
        return m_everRetried;
    }

private:
    mutable std::mutex m_mutex;
    LiveStreamStatus m_last;
    bool m_everRetried = false;
};

[[nodiscard]] bool waitFor(const std::function<bool()> &condition, std::chrono::milliseconds timeout = 20s)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (condition()) {
            return true;
        }
        std::this_thread::sleep_for(20ms);
    }
    return condition();
}

[[nodiscard]] LiveStreamOptions options()
{
    LiveStreamOptions options;
    options.connectTimeout = 5s;
    options.backoff.first = 300ms;
    options.backoff.longest = 1s;
    options.statusInterval = 100ms;
    options.caFile = MediaMtx::instance().caFile;
    return options;
}

[[nodiscard]] LiveStreamSettings settings(const std::string &url)
{
    LiveStreamSettings settings;
    settings.url = url;
    settings.quality = LiveQuality{640, 360, 30, 800, 128};
    settings.scene.title = "Reverblabr";
    settings.scene.subtitle = "A test stream";
    settings.scene.visualisation = Visualisation::Spectrum;
    settings.sessionName = "Test";
    return settings;
}

[[nodiscard]] std::string rtmpUrl()
{
    return "rtmp://127.0.0.1:" + std::to_string(MediaMtx::instance().rtmpPort) + "/live";
}

[[nodiscard]] std::string playUrl()
{
    return rtmpUrl() + "/" + kKey;
}

/// Plays the stream for a while, as a viewer would, and says what it saw.
void expectPlayable(const std::string &label)
{
    // On the server once its first frames have arrived, which is a moment
    // after the stream has been let in.
    ASSERT_TRUE(waitFor([] {
        return MediaMtx::instance().path(std::string{"live/"} + kKey).value(u"ready"_s).toBool();
    })) << label;
    RtmpListener viewer(playUrl(), false);
    ASSERT_TRUE(waitFor([&] { return viewer.received().videoTimes.size() >= 60; })) << label;
    const ReceivedStream seen = viewer.received();
    EXPECT_EQ(seen.videoCodec, AV_CODEC_ID_H264) << label;
    EXPECT_EQ(seen.audioCodec, AV_CODEC_ID_AAC) << label;
    EXPECT_EQ(seen.width, 640) << label;
    EXPECT_EQ(seen.height, 360) << label;
    EXPECT_EQ(seen.sampleRate, 48000) << label;
}

class MediaMtxTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!probeVideoEncoders().any()) {
            GTEST_SKIP() << "this system's FFmpeg has no H.264 encoder that works";
        }
    }
};

TEST_F(MediaMtxTest, GoesOnTheAirOverRtmpAndPlays)
{
    LastStatus status;
    auto tap = std::make_shared<rt::AudioTap>(2, 480000);
    Feeder feeder(tap);
    LiveStream live(tap, status.sink(), options());
    live.configure(settings(rtmpUrl()));
    live.setPassword(kKey);
    ASSERT_TRUE(waitFor([&] { return status.get().stream.state == State::Connected; }))
        << broadcastStateName(status.get().stream.state);
    ASSERT_TRUE(
        waitFor([] { return MediaMtx::instance().path(std::string{"live/"} + kKey).value(u"ready"_s).toBool(); }));
    const QJsonObject path = MediaMtx::instance().path(std::string{"live/"} + kKey);
    EXPECT_TRUE(QJsonDocument(path).toJson().contains("H264")) << QJsonDocument(path).toJson().toStdString();
    EXPECT_TRUE(QJsonDocument(path).toJson().contains("MPEG-4 Audio"));
    expectPlayable("rtmp");
}

// **RTMPS checks the server's certificate**: against the authority it was
// given it goes on the air; with the system's, which never signed this
// one, it does not.
TEST_F(MediaMtxTest, GoesOnTheAirOverRtmpsAndChecksTheCertificate)
{
    LastStatus status;
    auto tap = std::make_shared<rt::AudioTap>(2, 480000);
    Feeder feeder(tap);
    const std::string secure = "rtmps://localhost:" + std::to_string(MediaMtx::instance().rtmpsPort) + "/live";
    {
        LiveStream live(tap, status.sink(), options());
        live.configure(settings(secure));
        live.setPassword(kKey);
        ASSERT_TRUE(waitFor([&] { return status.get().stream.state == State::Connected; }))
            << broadcastStateName(status.get().stream.state);
        expectPlayable("rtmps");
    }
    LastStatus refused;
    LiveStreamOptions untrusting = options();
    untrusting.caFile.clear();
    LiveStream live(tap, refused.sink(), untrusting);
    live.configure(settings(secure));
    live.setPassword(kKey);
    ASSERT_TRUE(waitFor([&] { return refused.everRetried(); }));
    EXPECT_NE(refused.get().stream.state, State::Connected);
}

TEST_F(MediaMtxTest, AWrongKeyIsRefused)
{
    LastStatus status;
    auto tap = std::make_shared<rt::AudioTap>(2, 480000);
    Feeder feeder(tap);
    LiveStream live(tap, status.sink(), options());
    live.configure(settings(rtmpUrl()));
    live.setPassword("not-the-key");
    ASSERT_TRUE(waitFor([&] { return status.everRetried(); }));
    EXPECT_NE(status.get().stream.state, State::Connected);
    // The right one goes on the air at once.
    live.setPassword(kKey);
    EXPECT_TRUE(waitFor([&] { return status.get().stream.state == State::Connected; }));
}

TEST_F(MediaMtxTest, ARestartedServerIsConnectedToAgain)
{
    LastStatus status;
    auto tap = std::make_shared<rt::AudioTap>(2, 480000);
    Feeder feeder(tap);
    LiveStream live(tap, status.sink(), options());
    live.configure(settings(rtmpUrl()));
    live.setPassword(kKey);
    ASSERT_TRUE(waitFor([&] { return status.get().stream.state == State::Connected; }));
    ASSERT_TRUE(MediaMtx::instance().restart());
    ASSERT_TRUE(waitFor(
        [&] { return status.get().stream.reconnects >= 1 && status.get().stream.state == State::Connected; }, 40s));
    expectPlayable("after a restart");
}

// **Picture with sound, and both with the clock**, for as long as the run
// is asked to last: a viewer who joins at the end sees the two together,
// and the stream has sent as many seconds as have passed, with no frame
// left out.
TEST_F(MediaMtxTest, ALongStreamKeepsTimeWithoutDrops)
{
    const char *asked = std::getenv("RB_LIVESTREAM_SOAK_SECONDS");
    const int seconds = asked != nullptr ? std::max(10, std::atoi(asked)) : 20;
    LastStatus status;
    auto tap = std::make_shared<rt::AudioTap>(2, 480000);
    Feeder feeder(tap);
    LiveStream live(tap, status.sink(), options());
    live.configure(settings(rtmpUrl()));
    live.setPassword(kKey);
    ASSERT_TRUE(waitFor([&] { return status.get().stream.state == State::Connected; }));
    const auto began = std::chrono::steady_clock::now();
    const std::uint64_t framesAtStart = status.get().framesSent;
    std::this_thread::sleep_for(std::chrono::seconds{seconds});
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
    const LiveStreamStatus after = status.get();
    EXPECT_EQ(after.stream.state, State::Connected);
    EXPECT_EQ(after.stream.reconnects, 0U);
    EXPECT_EQ(after.framesDropped, 0U);
    // 30 frames a second of wall clock, within half a second.
    EXPECT_NEAR(static_cast<double>(after.framesSent - framesAtStart) / 30.0, elapsed, 0.5);

    RtmpListener viewer(playUrl(), false);
    ASSERT_TRUE(waitFor(
        [&] { return viewer.received().videoTimes.size() >= 60 && viewer.received().audioTimes.size() >= 90; }));
    const ReceivedStream seen = viewer.received();
    EXPECT_NEAR(seen.videoTimes.back(), seen.audioTimes.back(), 0.25);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    if (QStandardPaths::findExecutable(u"podman"_s).isEmpty()) {
        std::printf("SKIP: podman is not installed\n");
        return 77;
    }
    if (QStandardPaths::findExecutable(u"openssl"_s).isEmpty()) {
        std::printf("SKIP: openssl is not installed, for the server's certificate\n");
        return 77;
    }
    if (const Ran info = podman({u"info"_s}); !info.ok) {
        std::printf("SKIP: podman cannot run containers here: %s\n", qPrintable(info.output.trimmed().left(300)));
        return 77;
    }
    if (!podman({u"image"_s, u"exists"_s, QString::fromLatin1(kImage)}).ok) {
        std::printf("Pulling %s; this needs the network once.\n", kImage);
        if (const Ran pulled = podman({u"pull"_s, QString::fromLatin1(kImage)}, 600s); !pulled.ok) {
            std::printf("SKIP: the MediaMTX image could not be pulled: %s\n",
                        qPrintable(pulled.output.trimmed().right(500)));
            return 77;
        }
    }
    QString why;
    if (!MediaMtx::instance().start(why)) {
        std::printf("SKIP: %s\n", qPrintable(why));
        MediaMtx::instance().stop();
        return 77;
    }
    const int result = RUN_ALL_TESTS();
    MediaMtx::instance().stop();
    return result;
}
