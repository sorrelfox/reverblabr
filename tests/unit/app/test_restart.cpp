// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Restart Now (ADR-0036): what the copy of the program is started with,
// the environment it gets, and its wait for the process it replaces, which
// it looks at and never signals. The copy itself, started on a private bus
// while the old one runs, is tests/bus/test_launch.cpp.

#include "language.h"
#include "restart.h"

#include <QElapsedTimer>
#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace rb;
using namespace Qt::StringLiterals;

TEST(RestartTest, TheCopyKeepsOnlyWhatChoseTheEngineAndIsToldWhichProcessItReplaces)
{
    // **Launch requests are this launch's**, not the copy's: a session to
    // open, the Sessions window, a new session. An option's value is not an
    // option, an old restart's pid is not this one's, and after "--" there
    // are no options.
    const QStringList arguments{u"/usr/bin/reverblabr"_s,
                                u"--in-process"_s,
                                u"--open-session"_s,
                                u"--mock"_s,
                                u"--new-session"_s,
                                u"--sessions"_s,
                                u"--daemon"_s,
                                u"--restart-of=12"_s,
                                u"--open-session=4f3c"_s,
                                u"--in-process"_s,
                                u"--"_s,
                                u"--mock"_s};
    EXPECT_EQ(ui::Restart::argumentsFor(arguments, 4321),
              (QStringList{u"--in-process"_s, u"--daemon"_s, u"--restart-of=4321"_s}));
    EXPECT_EQ(ui::Restart::argumentsFor({u"reverblabr"_s}, 99), QStringList{u"--restart-of=99"_s});
    EXPECT_EQ(ui::Restart::argumentsFor({u"reverblabr"_s, u"--mock"_s, u"--sessions"_s}, 7),
              (QStringList{u"--mock"_s, u"--restart-of=7"_s}));
}

TEST(RestartTest, TheCopyGetsTheEnvironmentThisOneWasStartedWith)
{
    // **LANGUAGE as it was before the choice was put in it**, so that the
    // copy reads the choice for itself and takes the desktop's for the
    // desktop's.
    qputenv("LANGUAGE", "fr");
    notify::ProcessLanguage::applyAtStart(u"de"_s);
    qputenv("XDG_ACTIVATION_TOKEN", "taken-already");
    QProcessEnvironment environment = ui::Restart::environmentFor(QProcessEnvironment::systemEnvironment());
    EXPECT_EQ(environment.value(u"LANGUAGE"_s), u"fr"_s);
    // **Not the token this launch came with**, which the compositor has
    // taken already.
    EXPECT_FALSE(environment.contains(u"XDG_ACTIVATION_TOKEN"_s));
    EXPECT_EQ(environment.value(u"PATH"_s), qEnvironmentVariable("PATH")) << "the rest is as it was";

    qunsetenv("LANGUAGE");
    notify::ProcessLanguage::applyAtStart(u"de"_s);
    environment = ui::Restart::environmentFor(QProcessEnvironment::systemEnvironment());
    EXPECT_FALSE(environment.contains(u"LANGUAGE"_s)) << "a LANGUAGE nobody set was given to the copy";
}

TEST(RestartTest, TheCopyWaitsUntilTheOldProcessHasGoneAndNoLonger)
{
    // **A process of this program's name** that ends after a moment: a child
    // of this one, which is not collected while the copy waits, so it is a
    // zombie for a while, and a zombie has gone.
    const pid_t ending = ::fork();
    ASSERT_GE(ending, 0);
    if (ending == 0) {
        ::usleep(300 * 1000);
        ::_exit(0);
    }
    QElapsedTimer clock;
    clock.start();
    EXPECT_TRUE(ui::Restart::waitForExitOf(ending, std::chrono::seconds(10)));
    EXPECT_GE(clock.elapsed(), 250) << "the copy did not wait";
    EXPECT_LT(clock.elapsed(), 5000) << "a zombie was taken for a process still there";
    int status = 0;
    EXPECT_EQ(::waitpid(ending, &status, 0), ending);

    // **No longer than the limit**: one that stays until its pipe is closed.
    int pipe[2] = {-1, -1};
    ASSERT_EQ(::pipe(pipe), 0);
    const pid_t staying = ::fork();
    ASSERT_GE(staying, 0);
    if (staying == 0) {
        ::close(pipe[1]);
        char byte = 0;
        while (::read(pipe[0], &byte, 1) > 0) { }
        ::_exit(0);
    }
    ::close(pipe[0]);
    clock.restart();
    EXPECT_FALSE(ui::Restart::waitForExitOf(staying, std::chrono::milliseconds(300)));
    EXPECT_GE(clock.elapsed(), 280);
    // Then it ends by itself: its read comes to the end of the pipe.
    ::close(pipe[1]);
    EXPECT_EQ(::waitpid(staying, &status, 0), staying);

    // **Nothing to wait for**: this process, no pid at all, the first one,
    // another program (whatever started the test), one that is not there.
    clock.restart();
    EXPECT_TRUE(ui::Restart::waitForExitOf(::getpid()));
    EXPECT_TRUE(ui::Restart::waitForExitOf(0));
    EXPECT_TRUE(ui::Restart::waitForExitOf(1));
    EXPECT_TRUE(ui::Restart::waitForExitOf(-4));
    EXPECT_TRUE(ui::Restart::waitForExitOf(::getppid()));
    EXPECT_TRUE(ui::Restart::waitForExitOf(ending));
    EXPECT_LT(clock.elapsed(), 1000) << "the copy waited for something it should not";
}
