// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// **One Reverblabr a desktop**: the real application, twice, on a
// private bus in a PID namespace of its own (tests/bus/CMakeLists.txt). A
// second launch, such as the desktop entry's New Session or All Sessions,
// hands its arguments to the first and ends, and the first does what they
// ask. On a desktop's bus the second launch would reach the Reverblabr the
// person has open, which is why this refuses to run anywhere else.
//
// Both run offscreen, in a home of the test's own, with the engine in the
// window and no PipeWire to reach. What the first one was asked is read
// from its log, the line LaunchRequests::deliver() writes.

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusReply>
#include <QDir>
#include <QElapsedTimer>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <gtest/gtest.h>

#include <memory>
#include <vector>

using namespace Qt::StringLiterals;

namespace
{

template <typename Predicate>
bool waitFor(Predicate predicate, int milliseconds)
{
    QElapsedTimer clock;
    clock.start();
    while (!predicate() && clock.elapsed() < milliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(10);
    }
    return predicate();
}

/// KDBusService's name for Reverblabr: the reversed organization domain
/// main() sets, then the application's name.
const QString kService = u"io.github.reverblabr.reverblabr"_s;

/// Long enough for the window to come up under a sanitizer on a busy machine.
constexpr int kStartMs = 90000;

class Launch : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // **Refuse to run anywhere but a private bus**: a second launch on a
        // desktop's bus is handed to the Reverblabr the person has open.
        const QByteArray address = qgetenv("DBUS_SESSION_BUS_ADDRESS");
        ASSERT_FALSE(address.isEmpty());
        ASSERT_FALSE(address.contains(QByteArray("/run/user/")))
            << "this is the real session bus; run it through ctest";
        ASSERT_TRUE(bus.isConnected());
        ASSERT_TRUE(home.isValid());
        for (const char *folder : {"data", "config", "state", "cache"}) {
            ASSERT_TRUE(QDir(home.path()).mkpath(QString::fromLatin1(folder)));
        }
    }

    void TearDown() override
    {
        for (const auto &[process, log] : running) {
            if (process->state() != QProcess::NotRunning) {
                process->terminate();
                if (!process->waitForFinished(10000)) {
                    process->kill();
                    process->waitForFinished(5000);
                }
            }
        }
        QDBusConnection::disconnectFromBus(u"launch-probe"_s);
    }

    /// Starts Reverblabr with \a arguments; its log is collected as it comes.
    QProcess *start(const QStringList &arguments)
    {
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(u"HOME"_s, home.path());
        environment.insert(u"XDG_DATA_HOME"_s, home.filePath(u"data"_s));
        environment.insert(u"XDG_CONFIG_HOME"_s, home.filePath(u"config"_s));
        environment.insert(u"XDG_STATE_HOME"_s, home.filePath(u"state"_s));
        environment.insert(u"XDG_CACHE_HOME"_s, home.filePath(u"cache"_s));
        environment.insert(u"QT_QPA_PLATFORM"_s, u"offscreen"_s);
        environment.insert(u"PIPEWIRE_REMOTE"_s, u"rb-test-no-such-socket"_s);
        // The request line, on stderr rather than in the journal.
        environment.insert(u"QT_FORCE_STDERR_LOGGING"_s, u"1"_s);
        environment.insert(u"QT_LOGGING_RULES"_s, u"reverblabr.launch.info=true"_s);

        auto process = std::make_unique<QProcess>();
        process->setProcessEnvironment(environment);
        process->setProcessChannelMode(QProcess::MergedChannels);
        auto log = std::make_shared<QByteArray>();
        QObject::connect(process.get(), &QProcess::readyRead, process.get(), [raw = process.get(), log] {
            log->append(raw->readAll());
        });
        process->start(QStringLiteral(RB_APP), arguments);
        EXPECT_TRUE(process->waitForStarted(10000));
        running.emplace_back(std::move(process), log);
        return running.back().first.get();
    }

    /// What the process started by start() has logged so far.
    [[nodiscard]] QByteArray logOf(const QProcess *process) const
    {
        for (const auto &[candidate, log] : running) {
            if (candidate.get() == process) {
                return *log;
            }
        }
        return {};
    }

    [[nodiscard]] bool asked(const QProcess *process, QByteArrayView request) const
    {
        const QByteArray line = QByteArray("launch request: ") + request.toByteArray();
        return logOf(process).contains(line);
    }

    [[nodiscard]] bool serviceTaken()
    {
        return bus.interface()->isServiceRegistered(kService).value();
    }

    QDBusConnection bus = QDBusConnection::connectToBus(QDBusConnection::SessionBus, u"launch-probe"_s);
    QTemporaryDir home;
    std::vector<std::pair<std::unique_ptr<QProcess>, std::shared_ptr<QByteArray>>> running;
};

TEST_F(Launch, ASecondLaunchHandsItsRequestToTheFirstAndEnds)
{
    QProcess *first = start({u"--in-process"_s});
    ASSERT_TRUE(waitFor([&] { return serviceTaken() && asked(first, "Show"); }, kStartMs))
        << "the first launch never took " << kService.toStdString() << " or never came up:\n"
        << logOf(first).toStdString();
    const uint owner = bus.interface()->servicePid(kService).value();

    // **All Sessions** from the launcher, while Reverblabr runs.
    QProcess *second = start({u"--in-process"_s, u"--sessions"_s});
    ASSERT_TRUE(waitFor([&] { return second->state() == QProcess::NotRunning; }, kStartMs))
        << "the second launch did not end:\n"
        << logOf(second).toStdString();
    EXPECT_EQ(second->exitStatus(), QProcess::NormalExit);
    EXPECT_EQ(second->exitCode(), 0);
    ASSERT_TRUE(waitFor([&] { return asked(first, "Sessions"); }, 20000))
        << "the running Reverblabr was never asked for the Sessions window:\n"
        << logOf(first).toStdString();
    EXPECT_FALSE(asked(second, "Sessions")) << "the second launch did it itself";

    // **New Session** the same way.
    QProcess *third = start({u"--in-process"_s, u"--new-session"_s});
    ASSERT_TRUE(waitFor([&] { return third->state() == QProcess::NotRunning; }, kStartMs));
    EXPECT_EQ(third->exitCode(), 0);
    EXPECT_TRUE(waitFor([&] { return asked(first, "NewSession"); }, 20000)) << logOf(first).toStdString();

    EXPECT_EQ(bus.interface()->servicePid(kService).value(), owner) << "the name changed hands";
    EXPECT_EQ(first->state(), QProcess::Running) << "the first Reverblabr went";
}

TEST_F(Launch, AWindowOnTheMockEngineStandsApart)
{
    // **--mock is for trying the window out beside a real one**: it takes no
    // name, so it neither hands itself over nor is handed anything.
    QProcess *mock = start({u"--mock"_s});
    ASSERT_TRUE(waitFor([&] { return asked(mock, "Show"); }, kStartMs)) << logOf(mock).toStdString();
    EXPECT_FALSE(serviceTaken()) << "a window on the mock engine took Reverblabr's name";

    QProcess *another = start({u"--mock"_s, u"--sessions"_s});
    ASSERT_TRUE(waitFor([&] { return asked(another, "Sessions"); }, kStartMs))
        << "a second window on the mock engine did not come up on its own:\n"
        << logOf(another).toStdString();
    EXPECT_EQ(another->state(), QProcess::Running);
    EXPECT_FALSE(asked(mock, "Sessions"));
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
