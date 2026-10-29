// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Which engine a window gets: the daemon's when the bus can give it,
// and its own otherwise, with the reason.
//
// **One bus each.** CMakeLists.txt runs this binary once per situation, each
// on a private bus of its own, in a PID namespace of its own as bus.engine is,
// and says which situation it is in RB_REACH_BUS; a test that is about
// another situation skips. The situations:
//   nobus      no session bus at all (an address with nothing behind it)
//   empty      a bus with no activation files
//   refused    a bus whose activation file starts a program that exits
//   activated  a bus whose activation file starts the reverblabrd just built

#include "bus/daemonreach.h"
#include "bus/enginebus.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusReply>
#include <QElapsedTimer>
#include <QThread>
#include <gtest/gtest.h>
#include <signal.h>
#include <unistd.h>

#include <chrono>
#include <limits>

using namespace rb;
using namespace Qt::StringLiterals;
using namespace std::chrono_literals;

namespace
{

[[nodiscard]] QString situation()
{
    return qEnvironmentVariable("RB_REACH_BUS");
}

/// **Never the real session bus.** Every situation here is about starting
/// or not starting a daemon by its bus name.
void refuseTheRealBus()
{
    const QByteArray address = qgetenv("DBUS_SESSION_BUS_ADDRESS");
    ASSERT_FALSE(address.isEmpty()) << "run through ctest, which gives each situation a bus of its own";
    ASSERT_FALSE(address.contains(QByteArray("/run/user/"))) << "this is the real session bus";
}

/// A test about another situation skips; one about this one first makes
/// sure this is a private bus.
#define RB_ONLY_ON(name)                                                                                               \
    if (situation() != QLatin1StringView(name)) {                                                                      \
        GTEST_SKIP() << "about the " name " bus, and this is " << situation().toStdString();                           \
    }                                                                                                                  \
    ASSERT_NO_FATAL_FAILURE(refuseTheRealBus())

/// Turns the event loop until \a predicate holds or \a milliseconds pass.
template <typename Predicate>
bool waitFor(Predicate predicate, int milliseconds)
{
    QElapsedTimer clock;
    clock.start();
    while (!predicate() && clock.elapsed() < milliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return predicate();
}

/// kill(2) for the daemon this test started, and nothing else, as in
/// test_enginebus.cpp: kill(0) signals the process group and kill(-1) every
/// process the user owns, so anything but an ordinary pid of another process
/// is a failed test, never a signal.
[[nodiscard]] bool signalProcess(qint64 pid, int signal)
{
    if (pid <= 1 || pid > std::numeric_limits<pid_t>::max() || pid == ::getpid()) {
        ADD_FAILURE() << "refusing to signal pid " << pid;
        return false;
    }
    return ::kill(static_cast<pid_t>(pid), signal) == 0;
}

} // namespace

TEST(DaemonReach, WithoutASessionBusThereIsNoDaemonToBeHad)
{
    RB_ONLY_ON("nobus");
    const bus::DaemonReachResult result = bus::reachDaemon(QDBusConnection::sessionBus());
    EXPECT_EQ(result.reach, bus::DaemonReach::NoSessionBus) << result.detail.toStdString();
}

TEST(DaemonReach, ABusThatCannotStartItSaysItIsNotInstalled)
{
    RB_ONLY_ON("empty");
    QElapsedTimer clock;
    clock.start();
    const bus::DaemonReachResult result = bus::reachDaemon(QDBusConnection::sessionBus());
    EXPECT_EQ(result.reach, bus::DaemonReach::NotInstalled) << result.detail.toStdString();
    // Told apart without trying: no activation to wait for.
    EXPECT_LT(clock.elapsed(), 2000);
}

TEST(DaemonReach, ADaemonAlreadyThereIsReachedWithoutAnActivationFile)
{
    RB_ONLY_ON("empty");
    // Somebody else has the name, as a daemon started by hand would.
    QDBusConnection other = QDBusConnection::connectToBus(QDBusConnection::SessionBus, u"other"_s);
    ASSERT_TRUE(other.registerService(bus::kServiceName));
    const bus::DaemonReachResult result = bus::reachDaemon(QDBusConnection::sessionBus());
    EXPECT_EQ(result.reach, bus::DaemonReach::Reached) << result.detail.toStdString();
    ASSERT_TRUE(other.unregisterService(bus::kServiceName));
    QDBusConnection::disconnectFromBus(u"other"_s);
}

TEST(DaemonReach, ADaemonThatWillNotStartIsRefusedWithinTheWait)
{
    RB_ONLY_ON("refused");
    QElapsedTimer clock;
    clock.start();
    const bus::DaemonReachResult result = bus::reachDaemon(QDBusConnection::sessionBus(), 5s);
    EXPECT_EQ(result.reach, bus::DaemonReach::Refused);
    EXPECT_FALSE(result.detail.isEmpty()) << "what the bus said goes to the log";
    EXPECT_LT(clock.elapsed(), 6000);
}

TEST(DaemonReach, TheBusStartsTheDaemonAndItIsReached)
{
    RB_ONLY_ON("activated");
    QDBusConnectionInterface *daemon = QDBusConnection::sessionBus().interface();
    ASSERT_NE(daemon, nullptr);
    ASSERT_FALSE(daemon->isServiceRegistered(bus::kServiceName).value()) << "nothing has started it yet";
    const bus::DaemonReachResult result = bus::reachDaemon(QDBusConnection::sessionBus());
    EXPECT_EQ(result.reach, bus::DaemonReach::Reached) << result.detail.toStdString();
    EXPECT_TRUE(daemon->isServiceRegistered(bus::kServiceName).value()) << "reached, but nobody has the name";

    // **The daemon goes before the bus does.** Without a PID namespace (in a
    // container, unshare is refused) nothing else stops it: it would outlive
    // the bus, idle or still starting, and hold the test's output open until
    // ctest gives up on it.
    const QDBusReply<uint> pid = daemon->servicePid(bus::kServiceName);
    ASSERT_TRUE(pid.isValid()) << pid.error().message().toStdString();
    const auto gone = [daemon] { return !daemon->isServiceRegistered(bus::kServiceName).value(); };
    ASSERT_TRUE(signalProcess(static_cast<qint64>(pid.value()), SIGTERM));
    if (!waitFor(gone, 5000)) {
        (void)signalProcess(static_cast<qint64>(pid.value()), SIGKILL);
        EXPECT_TRUE(waitFor(gone, 5000)) << "the daemon kept its name after SIGKILL";
    }
}

// **The choice itself** needs no bus: what the command line asked for, and
// what the bus answered, stood in for.

namespace
{

struct Asked {
    int count = 0;
    bus::DaemonReachResult answer;

    [[nodiscard]] std::function<bus::DaemonReachResult()> reach()
    {
        return [this] {
            ++count;
            return answer;
        };
    }
};

} // namespace

TEST(EngineChoice, WithNothingAskedTheDaemonIsUsedWhenItIsThere)
{
    Asked asked{.answer = {bus::DaemonReach::Reached, {}}};
    const bus::EngineChoice choice = bus::chooseEngine({}, asked.reach());
    EXPECT_EQ(choice.kind, bus::EngineKind::Daemon);
    EXPECT_TRUE(choice.fallbackReason.isEmpty());
    EXPECT_EQ(asked.count, 1);
}

TEST(EngineChoice, WithNothingAskedAWindowThatCannotReachItRunsItsOwnAndSaysWhy)
{
    const std::pair<bus::DaemonReach, QString> cases[] = {
        {bus::DaemonReach::NoSessionBus, u"noSessionBus"_s},
        {bus::DaemonReach::NotInstalled, u"daemonNotInstalled"_s},
        {bus::DaemonReach::Refused, u"daemonRefused"_s},
    };
    for (const auto &[reach, reason] : cases) {
        Asked asked{.answer = {reach, u"what the bus said"_s}};
        const bus::EngineChoice choice = bus::chooseEngine({}, asked.reach());
        EXPECT_EQ(choice.kind, bus::EngineKind::InProcess) << reason.toStdString();
        EXPECT_EQ(choice.fallbackReason, reason);
    }
}

TEST(EngineChoice, WhatWasAskedForIsWhatIsGivenAndTheBusIsNotAsked)
{
    Asked asked{.answer = {bus::DaemonReach::NoSessionBus, {}}};
    EXPECT_EQ(bus::chooseEngine({.inProcess = true}, asked.reach()).kind, bus::EngineKind::InProcess);
    EXPECT_EQ(bus::chooseEngine({.mock = true}, asked.reach()).kind, bus::EngineKind::Mock);
    // **An explicit --daemon keeps to the daemon**, whatever the bus says now.
    const bus::EngineChoice daemon = bus::chooseEngine({.daemon = true}, asked.reach());
    EXPECT_EQ(daemon.kind, bus::EngineKind::Daemon);
    EXPECT_TRUE(daemon.fallbackReason.isEmpty());
    // --mock, then --in-process, then --daemon.
    EXPECT_EQ(bus::chooseEngine({.mock = true, .inProcess = true, .daemon = true}, asked.reach()).kind,
              bus::EngineKind::Mock);
    EXPECT_EQ(bus::chooseEngine({.inProcess = true, .daemon = true}, asked.reach()).kind, bus::EngineKind::InProcess);
    EXPECT_EQ(asked.count, 0) << "the bus was asked although the command line had decided";
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
