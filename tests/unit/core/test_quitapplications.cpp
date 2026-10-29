// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What a timer set to quit the captured applications asks to quit: from
// what the server says of each stream's client, and a process only while it
// is still the one it was.

#include "core/quitapplications.h"

#include <QCoreApplication>
#include <QProcess>
#include <gtest/gtest.h>

#include <csignal>

using namespace rb;
using namespace rb::core;
using namespace Qt::StringLiterals;

namespace
{

pw::PwNode stream(std::uint32_t client, const QString &binary, qint64 claimedPid)
{
    static std::uint32_t nextId = 500;
    pw::PwNode node;
    node.object.id = nextId++;
    node.kind = pw::NodeKind::PlaybackStream;
    node.clientId = client;
    node.applicationBinary = binary;
    node.applicationPid = claimedPid;
    return node;
}

pw::PwClient client(std::uint32_t id, const QHash<QString, QString> &props)
{
    pw::PwClient made;
    made.object.id = id;
    made.props = props;
    return made;
}

} // namespace

// **The server's word, not the application's**: the pid on the client's
// socket, whatever the stream claims, and Flatpak only for a client that
// came through the portal.
TEST(QuitApplications, TheTargetsAreWhatTheServerSaysOfEachClient)
{
    const QList<pw::PwClient> clients{
        client(10, {{u"pipewire.sec.pid"_s, u"4100"_s}}),
        client(11, {{u"pipewire.sec.pid"_s, u"4200"_s}, {u"pipewire.access.portal.app_id"_s, u"org.example.Player"_s}}),
        // An application id the client set itself is not the portal's.
        client(12, {{u"pipewire.sec.pid"_s, u"4300"_s}, {u"application.id"_s, u"org.example.Liar"_s}}),
        client(13, {}),
    };
    const QList<QuitTarget> targets = quitTargets(
        {
            stream(10, u"firefox"_s, 1),
            stream(10, u"firefox"_s, 1), // a second stream of the same process
            stream(11, u"player"_s, 2),
            stream(12, u"liar"_s, 3),
            stream(13, u"nobody"_s, 4), // the server says no pid
            stream(99, u"gone"_s, 5), // no such client
        },
        clients);
    ASSERT_EQ(targets.size(), 3);
    EXPECT_EQ(targets.at(0), (QuitTarget{4100, u"firefox"_s, {}}));
    EXPECT_EQ(targets.at(1), (QuitTarget{-1, {}, u"org.example.Player"_s}));
    EXPECT_EQ(targets.at(2), (QuitTarget{4300, u"liar"_s, {}}));
}

// **Only the process it was**: one running another program is left alone,
// and the one running what PipeWire said is asked to quit.
TEST(QuitApplications, AProcessIsAskedToQuitOnlyWhileItRunsTheProgramItWasSaidToRun)
{
    if (QCoreApplication::instance() == nullptr) {
        static int argc = 1;
        static char name[] = "reverblabr-engine-test";
        static char *argv[] = {name, nullptr};
        // Never destroyed. A static application would be destroyed at
        // exit after Qt's own globals, and Qt 6.10's SQL module then
        // crashes on its destroyed() signal.
        [[maybe_unused]] static auto *application = new QCoreApplication(argc, argv);
    }
    QProcess child;
    child.start(u"bash"_s, {u"-c"_s, u"while :; do sleep 1; done"_s});
    ASSERT_TRUE(child.waitForStarted());
    const qint64 pid = child.processId();

    EXPECT_EQ(quitProcess(pid, u"firefox"_s), QuitResult::WrongProgram);
    EXPECT_EQ(child.state(), QProcess::Running);
    EXPECT_EQ(quitProcess(1, u"systemd"_s), QuitResult::WrongProgram) << "never the init process";
    EXPECT_EQ(quitProcess(pid, QString()), QuitResult::WrongProgram) << "nothing to check it against";

    EXPECT_EQ(quitProcess(pid, u"bash"_s), QuitResult::Signalled);
    ASSERT_TRUE(child.waitForFinished(5000));
    EXPECT_EQ(child.exitStatus(), QProcess::CrashExit);
    EXPECT_EQ(child.exitCode(), SIGTERM);
    EXPECT_EQ(quitProcess(pid, u"bash"_s), QuitResult::Gone);
}
