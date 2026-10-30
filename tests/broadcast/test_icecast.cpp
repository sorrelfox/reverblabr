// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The stream against a real Icecast, Fedora's, in a container: each
// format goes on the air and plays for a listener, the title a listener
// sees follows what is playing, a restarted server is connected to again
// with the counts right, a wrong password is refused, a stream over TLS
// reaches the server's own TLS port with its title and a certificate from
// an authority not trusted is refused there, and SHOUTcast's protocol works
// against the server's SHOUTcast port.
//
// **Not in the default run**: it needs podman, the image (built from
// Containerfile here the first time, which needs the network), and a
// minute. `ctest --preset container` runs it. It exits 77, which ctest
// counts as skipped, when podman is not there or cannot run a container,
// as in CI's own container.

#include "rt/audiotap.h"
#include "stream/broadcaststream.h"
#include "stream/connection.h"
#include "stream/streamencoder.h"
#include "streamdecode.h"
#include "tlsserver.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <numbers>
#include <random>
#include <thread>

using namespace rb;
using namespace rb::stream;
using namespace rb::test;
using namespace std::chrono_literals;
using namespace Qt::StringLiterals;

namespace
{

using State = BroadcastStatus::State;

constexpr auto kImage = "localhost/reverblabr-test-icecast:44";
constexpr auto kPassword = "test-source-password";

/// Runs podman, and gives back whether it worked and what it said.
struct Ran {
    bool ok = false;
    QString output;
};

Ran podman(const QStringList &arguments, std::chrono::milliseconds timeout = 60s)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(u"podman"_s, arguments);
    if (!process.waitForStarted(5000)) {
        return {false, u"podman would not start"_s};
    }
    if (!process.waitForFinished(static_cast<int>(timeout.count()))) {
        process.kill();
        process.waitForFinished(5000);
        return {false, u"podman took too long"_s};
    }
    return {process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0,
            QString::fromLocal8Bit(process.readAll())};
}

/// A port nothing listens on, with the one after it free too: SHOUTcast
/// takes two.
[[nodiscard]] std::uint16_t freePortPair()
{
    // One sequence for the run, so each call gives another pair.
    static std::mt19937 random(static_cast<unsigned>(::getpid()));
    std::uniform_int_distribution<int> pick(20000, 60000);
    const auto free = [](int port) {
        const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(static_cast<std::uint16_t>(port));
        const bool ok = ::bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof address) == 0;
        ::close(fd);
        return ok;
    };
    for (int attempt = 0; attempt < 200; ++attempt) {
        const int port = pick(random);
        if (free(port) && free(port + 1)) {
            return static_cast<std::uint16_t>(port);
        }
    }
    return 0;
}

/// The Icecast container, for the whole run.
class Icecast
{
public:
    static Icecast &instance()
    {
        static Icecast server;
        return server;
    }

    [[nodiscard]] bool start(QString &why)
    {
        port = freePortPair();
        shoutcastPort = freePortPair();
        tlsPort = freePortPair();
        const auto overlap = [](std::uint16_t one, std::uint16_t other) {
            return one == other || one == other + 1 || other == one + 1;
        };
        if (port == 0 || shoutcastPort == 0 || tlsPort == 0 || overlap(port, shoutcastPort) || overlap(port, tlsPort)
            || overlap(shoutcastPort, tlsPort)) {
            why = u"no free ports"_s;
            return false;
        }
        // **Icecast's own TLS**, with a certificate for this computer from
        // the test's authority, the key in the same file as Icecast wants.
        rb::test::TestAuthority::Server local;
        local.names = {"localhost"};
        local.addresses = {{127, 0, 0, 1}};
        const rb::test::Certificate certificate = authority.issue(local);
        m_certificatePath = m_folder.filePath(u"icecast.pem"_s);
        QFile pem(m_certificatePath);
        if (!pem.open(QIODevice::WriteOnly)
            || pem.write(QByteArray::fromStdString(certificate.certificatePem + certificate.keyPem)) < 0) {
            why = u"cannot write the certificate"_s;
            return false;
        }
        pem.close();
        QFile::setPermissions(m_certificatePath,
                              QFileDevice::ReadOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther);
        QFile in(QStringLiteral(RB_ICECAST_CONFIG));
        if (!in.open(QIODevice::ReadOnly)) {
            why = u"cannot read the configuration"_s;
            return false;
        }
        QString config = QString::fromUtf8(in.readAll());
        config.replace(u"@PASSWORD@"_s, QString::fromLatin1(kPassword));
        config.replace(u"@PORT@"_s, QString::number(port));
        config.replace(u"@SHOUTCAST_PORT@"_s, QString::number(shoutcastPort));
        config.replace(u"@TLS_PORT@"_s, QString::number(tlsPort));
        m_configPath = m_folder.filePath(u"icecast.xml"_s);
        QFile out(m_configPath);
        if (!out.open(QIODevice::WriteOnly) || out.write(config.toUtf8()) < 0) {
            why = u"cannot write the configuration"_s;
            return false;
        }
        out.close();
        // Readable by the container's icecast user.
        QFile::setPermissions(m_configPath, QFileDevice::ReadOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther);
        QFile::setPermissions(m_folder.path(),
                              QFileDevice::ReadOwner | QFileDevice::ExeOwner | QFileDevice::WriteOwner
                                  | QFileDevice::ReadOther | QFileDevice::ExeOther);
        m_name = u"reverblabr-test-icecast-%1"_s.arg(::getpid());
        return run(why);
    }

    [[nodiscard]] bool restart()
    {
        QString why;
        (void)podman({u"rm"_s, u"-f"_s, u"-t"_s, u"0"_s, m_name});
        return run(why);
    }

    void stop()
    {
        if (!m_name.isEmpty()) {
            (void)podman({u"rm"_s, u"-f"_s, u"-t"_s, u"0"_s, m_name});
        }
    }

    /// The server's status, as status-json.xsl gives it.
    [[nodiscard]] QJsonObject stats() const
    {
        const std::string answer = get("/status-json.xsl");
        const std::size_t body = answer.find("\r\n\r\n");
        if (body == std::string::npos) {
            return {};
        }
        return QJsonDocument::fromJson(QByteArray::fromStdString(answer.substr(body + 4)))
            .object()
            .value(u"icestats"_s)
            .toObject();
    }

    /// The source at \a mount, or nothing.
    [[nodiscard]] QJsonObject source(const QString &mount) const
    {
        const QJsonValue sources = stats().value(u"source"_s);
        const QJsonArray all = sources.isArray() ? sources.toArray() : QJsonArray{sources};
        for (const QJsonValue &entry : all) {
            if (entry.toObject().value(u"listenurl"_s).toString().endsWith(mount)) {
                return entry.toObject();
            }
        }
        return {};
    }

    /// What a listener gets from \a mount: its first \a bytes, after the
    /// server's headers.
    [[nodiscard]] std::vector<char> listen(const std::string &mount, std::size_t bytes) const
    {
        std::string answer = get(mount, bytes);
        const std::size_t body = answer.find("\r\n\r\n");
        if (body == std::string::npos) {
            return {};
        }
        return {answer.begin() + static_cast<std::ptrdiff_t>(body + 4), answer.end()};
    }

    std::uint16_t port = 0;
    std::uint16_t shoutcastPort = 0;
    std::uint16_t tlsPort = 0;
    /// The authority the server's certificate is from, for a client to
    /// trust.
    rb::test::TestAuthority authority;

private:
    Icecast() = default;
    ~Icecast()
    {
        stop();
    }

    [[nodiscard]] bool run(QString &why)
    {
        const Ran ran = podman({u"run"_s,
                                u"-d"_s,
                                u"--rm"_s,
                                u"--network=host"_s,
                                u"--user"_s,
                                u"icecast"_s,
                                u"--name"_s,
                                m_name,
                                u"-v"_s,
                                m_configPath + u":/etc/reverblabr-test-icecast.xml:ro,Z"_s,
                                u"-v"_s,
                                m_certificatePath + u":/etc/reverblabr-test-icecast.pem:ro,Z"_s,
                                QString::fromLatin1(kImage),
                                u"icecast"_s,
                                u"-c"_s,
                                u"/etc/reverblabr-test-icecast.xml"_s});
        if (!ran.ok) {
            why = u"podman could not run the server: "_s + ran.output.trimmed();
            return false;
        }
        const auto deadline = std::chrono::steady_clock::now() + 30s;
        while (std::chrono::steady_clock::now() < deadline) {
            if (get("/status-json.xsl").starts_with("HTTP/1.0 200")) {
                return true;
            }
            std::this_thread::sleep_for(100ms);
        }
        why = u"the server did not answer: "_s + podman({u"logs"_s, m_name}).output.trimmed();
        return false;
    }

    [[nodiscard]] std::string get(const std::string &path, std::size_t bodyBytes = 0) const
    {
        const std::atomic<bool> never{false};
        std::expected<Connection, StreamError> connection = Connection::open("127.0.0.1", port, 2s, never);
        if (!connection) {
            return {};
        }
        const std::string request = "GET " + path + " HTTP/1.0\r\nHost: 127.0.0.1\r\nUser-Agent: test\r\n\r\n";
        if (!connection->send(request, 2s, never)) {
            return {};
        }
        const std::expected<std::string, StreamError> answer = connection->receive(
            [bodyBytes](const std::string &received, bool closed) {
                const std::size_t body = received.find("\r\n\r\n");
                return closed
                    || (bodyBytes > 0 && body != std::string::npos && received.size() >= body + 4 + bodyBytes);
            },
            15s,
            never,
            bodyBytes + 65536);
        return answer.value_or(std::string{});
    }

    QTemporaryDir m_folder;
    QString m_configPath;
    QString m_certificatePath;
    QString m_name;
};

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

class LastStatus
{
public:
    BroadcastStream::StatusSink sink()
    {
        return [this](const BroadcastStatus &status) {
            const std::lock_guard lock(m_mutex);
            m_last = status;
        };
    }

    [[nodiscard]] BroadcastStatus get() const
    {
        const std::lock_guard lock(m_mutex);
        return m_last;
    }

private:
    mutable std::mutex m_mutex;
    BroadcastStatus m_last;
};

[[nodiscard]] bool waitFor(const std::function<bool()> &condition, std::chrono::milliseconds timeout = 20s)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (condition()) {
            return true;
        }
        std::this_thread::sleep_for(100ms);
    }
    return condition();
}

[[nodiscard]] BroadcastSettings settingsFor(StreamFormat format, const std::string &mount)
{
    BroadcastSettings settings;
    settings.server.host = "127.0.0.1";
    settings.server.port = Icecast::instance().port;
    settings.server.mount = mount;
    settings.server.userAgent = "Reverblabr-test";
    settings.station.name = "Reverblabr Test";
    settings.station.genre = "Test";
    settings.format = format;
    settings.bitrateKbps = 128;
    settings.titleTemplate = "%artist - %title";
    return settings;
}

[[nodiscard]] BroadcastOptions options()
{
    BroadcastOptions options;
    options.statusInterval = 200ms;
    options.backoff.first = 500ms;
    options.backoff.longest = 2s;
    return options;
}

struct Case {
    StreamFormat format;
    const char *codec;
    const char *mount;
};

void PrintTo(const Case &wanted, std::ostream *out)
{
    *out << wanted.codec;
}

class IcecastFormatTest : public ::testing::TestWithParam<Case>
{
};

} // namespace

TEST_P(IcecastFormatTest, ItGoesOnTheAirPlaysForAListenerAndTheTitleFollows)
{
    const Case wanted = GetParam();
    if (streamFormatUnavailable(wanted.format)) {
        GTEST_SKIP() << "this FFmpeg has no " << wanted.codec << " encoder";
    }
    Icecast &server = Icecast::instance();
    auto tap = std::make_shared<rt::AudioTap>(2, 480000);
    const Feeder feeder(tap);
    LastStatus status;
    BroadcastStream stream(tap, status.sink(), options());
    stream.configure(settingsFor(wanted.format, wanted.mount));
    stream.setNowPlaying(Track{.artist = "Ana", .title = "Morning"});
    stream.setPassword(kPassword);

    ASSERT_TRUE(waitFor([&] { return status.get().state == State::Connected; }))
        << "state " << broadcastStateName(status.get().state);
    const QString mount = QString::fromLatin1(wanted.mount);
    ASSERT_TRUE(waitFor([&] { return !server.source(mount).isEmpty(); })) << "the server does not list the mount";
    const QJsonObject source = server.source(mount);
    EXPECT_EQ(source.value(u"server_name"_s).toString(), u"Reverblabr Test"_s);
    EXPECT_EQ(source.value(u"server_type"_s).toString(), QString::fromLatin1(contentType(wanted.format).data()));
    // **Where the title shows.** Icecast reads it off its metadata request
    // for MP3 and AAC and out of the comment header for Vorbis, and shows
    // it on its pages. It shows none for Opus, whose comments 2.4 does not
    // read; the stream still carries it, and a listener's player shows it.
    const bool serverShowsTitle = wanted.format != StreamFormat::Opus;
    const auto titleShown = [&](const char *title) {
        if (serverShowsTitle) {
            return waitFor(
                [&] { return server.source(mount).value(u"title"_s).toString() == QString::fromUtf8(title); });
        }
        return waitFor([&] {
            const std::vector<char> heard = server.listen(wanted.mount, 16384);
            return std::string(heard.begin(), heard.end()).find(title) != std::string::npos;
        });
    };
    EXPECT_TRUE(titleShown("Ana - Morning"))
        << "the server shows " << server.source(mount).value(u"title"_s).toString().toStdString();

    // **A listener hears it**: what the server passes on is a stream a
    // player decodes, from wherever it joins.
    std::this_thread::sleep_for(2s);
    const Decoded heard = decode(server.listen(wanted.mount, 48000));
    ASSERT_TRUE(heard.opened) << "a listener could not read the stream";
    EXPECT_EQ(heard.codec, wanted.codec);
    EXPECT_GT(heard.peak, 0.2f);

    stream.setNowPlaying(Track{.artist = "Ana", .title = "Evening"});
    EXPECT_TRUE(titleShown("Ana - Evening"))
        << "the server shows " << server.source(mount).value(u"title"_s).toString().toStdString();
    EXPECT_EQ(status.get().reconnects, 0U);
}

INSTANTIATE_TEST_SUITE_P(Formats,
                         IcecastFormatTest,
                         ::testing::Values(Case{StreamFormat::Mp3, "mp3", "/mp3"},
                                           Case{StreamFormat::Opus, "opus", "/opus"},
                                           Case{StreamFormat::Vorbis, "vorbis", "/vorbis"},
                                           Case{StreamFormat::Aac, "aac", "/aac"}),
                         [](const auto &info) { return std::string(info.param.codec); });

TEST(IcecastTest, AServerThatRestartsIsConnectedToAgainAndTheCountsAreRight)
{
    Icecast &server = Icecast::instance();
    auto tap = std::make_shared<rt::AudioTap>(2, 480000);
    const Feeder feeder(tap);
    LastStatus status;
    BroadcastStream stream(tap, status.sink(), options());
    stream.configure(settingsFor(StreamFormat::Mp3, "/restart"));
    stream.setPassword(kPassword);
    ASSERT_TRUE(waitFor([&] { return status.get().state == State::Connected && status.get().bytesSent > 32000; }));
    const BroadcastStatus before = status.get();

    ASSERT_TRUE(server.restart());
    ASSERT_TRUE(waitFor([&] { return status.get().state == State::Connected && status.get().reconnects == 1; }, 30s))
        << "state " << broadcastStateName(status.get().state);
    ASSERT_TRUE(waitFor([&] { return !server.source(u"/restart"_s).isEmpty(); }));
    // The count goes on from where it was, and the time on the air starts
    // again with the new connection.
    ASSERT_TRUE(waitFor([&] { return status.get().bytesSent > before.bytesSent + 32000; }));
    EXPECT_GT(status.get().connectedSinceMs, before.connectedSinceMs);
    // What the server counts for this connection is no more than the
    // stream counts in all.
    const double serverBytes = server.source(u"/restart"_s).value(u"total_bytes_read"_s).toDouble();
    if (serverBytes > 0) {
        EXPECT_LE(serverBytes, static_cast<double>(status.get().bytesSent));
    }
    ASSERT_TRUE(waitFor([&] { return status.get().kbps > 100.0 && status.get().kbps < 200.0; }));
}

TEST(IcecastTest, AWrongPasswordIsRefusedAndNotTriedAgain)
{
    auto tap = std::make_shared<rt::AudioTap>(2, 480000);
    LastStatus status;
    BroadcastStream stream(tap, status.sink(), options());
    stream.configure(settingsFor(StreamFormat::Mp3, "/refused"));
    stream.setPassword("not-the-password");
    ASSERT_TRUE(waitFor([&] { return status.get().state == State::Failed; }));
    EXPECT_EQ(status.get().error, StreamError::AuthRejected);
}

TEST(IcecastTest, OverTlsTheStreamAndItsTitleReachTheServersTlsPort)
{
    if (streamFormatUnavailable(StreamFormat::Mp3)) {
        GTEST_SKIP() << "this FFmpeg has no MP3 encoder";
    }
    Icecast &server = Icecast::instance();
    auto tap = std::make_shared<rt::AudioTap>(2, 480000);
    const Feeder feeder(tap);
    LastStatus status;
    BroadcastOptions secured = options();
    secured.tls.trustFile = server.authority.trustFile();
    BroadcastStream stream(tap, status.sink(), secured);
    BroadcastSettings settings = settingsFor(StreamFormat::Mp3, "/tls");
    settings.server.port = server.tlsPort;
    settings.server.secure = true;
    stream.configure(settings);
    stream.setNowPlaying(Track{.artist = "Ana", .title = "Dusk"});
    stream.setPassword(kPassword);

    ASSERT_TRUE(waitFor([&] { return status.get().state == State::Connected; }))
        << "state " << broadcastStateName(status.get().state) << ", error "
        << (status.get().error ? streamErrorName(*status.get().error) : "none");
    EXPECT_TRUE(status.get().secure);
    ASSERT_TRUE(waitFor([&] { return !server.source(u"/tls"_s).isEmpty(); })) << "the server does not list the mount";
    // The title request went over TLS too: the stream knows no other way.
    EXPECT_TRUE(waitFor([&] { return server.source(u"/tls"_s).value(u"title"_s).toString() == u"Ana - Dusk"_s; }))
        << "the server shows " << server.source(u"/tls"_s).value(u"title"_s).toString().toStdString();
    std::this_thread::sleep_for(2s);
    const Decoded heard = decode(server.listen("/tls", 48000));
    ASSERT_TRUE(heard.opened) << "a listener could not read the stream";
    EXPECT_EQ(heard.codec, "mp3");
    EXPECT_GT(heard.peak, 0.2f);
}

TEST(IcecastTest, OverTlsACertificateFromAnAuthorityNotTrustedIsRefused)
{
    Icecast &server = Icecast::instance();
    rb::test::TestAuthority stranger("Somebody Else");
    auto tap = std::make_shared<rt::AudioTap>(2, 480000);
    LastStatus status;
    BroadcastOptions secured = options();
    secured.tls.trustFile = stranger.trustFile();
    BroadcastStream stream(tap, status.sink(), secured);
    BroadcastSettings settings = settingsFor(StreamFormat::Mp3, "/untrusted");
    settings.server.port = server.tlsPort;
    settings.server.secure = true;
    stream.configure(settings);
    stream.setPassword(kPassword);
    ASSERT_TRUE(waitFor([&] { return status.get().state == State::Failed; }));
    EXPECT_EQ(status.get().error, StreamError::CertificateUntrusted);
    EXPECT_TRUE(server.source(u"/untrusted"_s).isEmpty());
}

TEST(IcecastTest, ShoutcastsProtocolGoesOnTheAirWithItsTitle)
{
    Icecast &server = Icecast::instance();
    auto tap = std::make_shared<rt::AudioTap>(2, 480000);
    const Feeder feeder(tap);
    LastStatus status;
    BroadcastStream stream(tap, status.sink(), options());
    BroadcastSettings settings = settingsFor(StreamFormat::Mp3, "");
    settings.server.kind = ServerKind::Shoutcast;
    settings.server.port = server.shoutcastPort;
    stream.configure(settings);
    stream.setNowPlaying(Track{.artist = "Ana", .title = "Noon"});
    stream.setPassword(kPassword);
    ASSERT_TRUE(waitFor([&] { return status.get().state == State::Connected; }))
        << "state " << broadcastStateName(status.get().state);
    ASSERT_TRUE(waitFor([&] { return !server.source(u"/shout"_s).isEmpty(); }));
    EXPECT_EQ(server.source(u"/shout"_s).value(u"server_name"_s).toString(), u"Reverblabr Test"_s);
    EXPECT_TRUE(waitFor([&] { return server.source(u"/shout"_s).value(u"title"_s).toString() == u"Ana - Noon"_s; }))
        << "the server shows " << server.source(u"/shout"_s).value(u"title"_s).toString().toStdString();
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    // **Skipped, with the reason, where there is no podman to run the server
    // in**: CI's container, or a machine without it.
    if (QStandardPaths::findExecutable(u"podman"_s).isEmpty()) {
        std::printf("SKIP: podman is not installed\n");
        return 77;
    }
    if (const Ran info = podman({u"info"_s}); !info.ok) {
        std::printf("SKIP: podman cannot run containers here: %s\n", qPrintable(info.output.trimmed().left(300)));
        return 77;
    }
    if (!podman({u"image"_s, u"exists"_s, QString::fromLatin1(kImage)}).ok) {
        std::printf("Building %s from Fedora's icecast package; this needs the network once.\n", kImage);
        const Ran built =
            podman({u"build"_s, u"-t"_s, QString::fromLatin1(kImage), QStringLiteral(RB_ICECAST_IMAGE_DIR)}, 600s);
        if (!built.ok) {
            std::printf("SKIP: the Icecast image could not be built: %s\n",
                        qPrintable(built.output.trimmed().right(500)));
            return 77;
        }
    }
    QString why;
    if (!Icecast::instance().start(why)) {
        std::printf("SKIP: %s\n", qPrintable(why));
        Icecast::instance().stop();
        return 77;
    }
    const int result = RUN_ALL_TESTS();
    Icecast::instance().stop();
    return result;
}
