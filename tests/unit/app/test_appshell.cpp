// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What the window knows about the engine: the state it shows, the
// clock it counts on, and where the status bar's numbers come from. The
// window that draws them is checked in tests/qml.

#include "appshell.h"
#include "core/mockengine.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <gtest/gtest.h>

#include <array>

using namespace rb;
using namespace Qt::StringLiterals;

namespace
{

/// A clock a test owns, so that a minute of running time costs it nothing.
class Clock
{
public:
    [[nodiscard]] std::function<qint64()> hand()
    {
        return [this] { return m_now; };
    }

    void advance(qint64 milliseconds)
    {
        m_now += milliseconds;
    }

private:
    qint64 m_now = 0;
};

/// A session in the mock, made and opened, whose id comes back.
[[nodiscard]] QString openSession(core::MockEngine &engine)
{
    QSignalSpy opened(&engine, &core::EngineFacade::sessionOpened);
    engine.createSession(u"Shell"_s);
    EXPECT_EQ(opened.count(), 1);
    return opened.isEmpty() ? QString{} : opened.back().at(0).toString();
}

} // namespace

TEST(AppShellTest, WithNoSessionThereIsNothingToRunAndNothingToShow)
{
    // The window opens before anything is in it, and has to be honest about
    // that rather than showing a zero that looks like a measurement.
    ui::AppShell shell;
    core::MockEngine engine;
    shell.attach(&engine);

    EXPECT_FALSE(shell.hasSession());
    EXPECT_FALSE(shell.isRunning());
    EXPECT_FALSE(shell.hasStatus());
    EXPECT_EQ(shell.elapsedSeconds(), 0);

    // Run does nothing, rather than asking the engine about a session that
    // is not there.
    shell.start();
    EXPECT_FALSE(shell.isRunning());
}

TEST(AppShellTest, ItFollowsTheStateOfItsOwnSessionAndNoOther)
{
    ui::AppShell shell;
    core::MockEngine engine;
    shell.attach(&engine);

    const QString mine = openSession(engine);
    const QString theirs = openSession(engine);
    shell.setSessionId(mine);
    EXPECT_TRUE(shell.hasSession());

    QSignalSpy states(&shell, &ui::AppShell::stateChanged);
    shell.start();
    EXPECT_TRUE(shell.isRunning());
    EXPECT_FALSE(shell.isBusy());
    EXPECT_GE(states.count(), 1);

    // Another session running is another window's business, or nobody's.
    engine.start(theirs);
    EXPECT_TRUE(shell.isRunning());
    engine.stop(theirs);
    EXPECT_TRUE(shell.isRunning()) << "a state change about another session reached this one";

    shell.stop();
    EXPECT_FALSE(shell.isRunning());
}

TEST(AppShellTest, TheRunningTimeIsOnTheWallClock)
{
    // Not a count of audio cycles: it is a clock on a wall, and it keeps
    // counting through an xrun.
    Clock clock;
    ui::AppShell shell;
    shell.setClock(clock.hand());
    core::MockEngine engine;
    shell.attach(&engine);
    shell.setSessionId(openSession(engine));

    shell.start();
    EXPECT_EQ(shell.elapsedSeconds(), 0);

    clock.advance(1500);
    shell.refresh();
    EXPECT_EQ(shell.elapsedSeconds(), 1);

    clock.advance(60'000);
    shell.refresh();
    EXPECT_EQ(shell.elapsedSeconds(), 61);

    // Stopped is stopped: the readout goes back to nothing rather than
    // freezing at whatever it last said.
    shell.stop();
    shell.refresh();
    EXPECT_EQ(shell.elapsedSeconds(), 0);
}

TEST(AppShellTest, TheRunningTimeKeepsCountingPastADay)
{
    // **A session can be left running over a weekend.** The clock that
    // draws this (ToolBarRow.formatElapsed, checked in tests/qml) counts
    // days separately past the first one; what is checked here is that
    // there is a number left to count with.
    Clock clock;
    ui::AppShell shell;
    shell.setClock(clock.hand());
    core::MockEngine engine;
    shell.attach(&engine);
    shell.setSessionId(openSession(engine));

    shell.start();
    constexpr qint64 kDay = 24 * 60 * 60 * 1000;
    clock.advance(2 * kDay + (3 * 3600 + 4 * 60 + 5) * 1000);
    shell.refresh();
    EXPECT_EQ(shell.elapsedSeconds(), 2 * 24 * 3600 + 3 * 3600 + 4 * 60 + 5);
}

TEST(AppShellTest, TheStatusBarReadsWhatTheNodePublished)
{
    core::TelemetrySlot slot;
    ui::AppShell shell;
    core::MockEngine engine;
    shell.attach(&engine, [&slot](const QString &) { return &slot; });
    shell.setSessionId(openSession(engine));
    shell.start();

    // Nothing published yet: a session that has been told to run but whose
    // node is not being scheduled has nothing to report.
    shell.refresh();
    EXPECT_FALSE(shell.hasStatus());

    core::TelemetrySnapshot snapshot;
    snapshot.running = true;
    snapshot.sampleRate = 48000;
    snapshot.peakDspLoad = 0.42f;
    snapshot.xruns = 3;
    slot.store(snapshot);

    snapshot.frames = 1024;
    slot.store(snapshot);

    QSignalSpy status(&shell, &ui::AppShell::statusChanged);
    shell.refresh();
    EXPECT_TRUE(shell.hasStatus());
    EXPECT_EQ(shell.sampleRate(), 48000);
    EXPECT_EQ(shell.quantum(), 1024);
    // The cycle in milliseconds, which is the part of the delay the graph's
    // own buffering accounts for: 1024 frames at 48 kHz.
    EXPECT_NEAR(shell.latencyMs(), 21.333, 1e-3);
    EXPECT_NEAR(shell.dspLoad(), 0.42, 1e-6);
    EXPECT_EQ(shell.xruns(), 3);
    EXPECT_EQ(status.count(), 1);

    // **Nothing said when nothing changed.** The status is worked out four
    // times a second; four signals a second saying the same thing would
    // rebind everything that reads it.
    shell.refresh();
    EXPECT_EQ(status.count(), 1);

    snapshot.xruns = 4;
    slot.store(snapshot);
    shell.refresh();
    EXPECT_EQ(shell.xruns(), 4);
    EXPECT_EQ(status.count(), 2);

    // **How late the session's processing makes the sound**, from
    // samples to milliseconds, and a change of it is news.
    EXPECT_EQ(shell.processingLatencyMs(), 0.0);
    snapshot.processLatency = 72;
    slot.store(snapshot);
    shell.refresh();
    EXPECT_NEAR(shell.processingLatencyMs(), 1.5, 1e-9);
    EXPECT_EQ(status.count(), 3);

    // A node that has stopped has nothing to say, whatever it last said.
    snapshot.running = false;
    slot.store(snapshot);
    shell.refresh();
    EXPECT_FALSE(shell.hasStatus());
}

TEST(AppShellTest, WithNothingRunningThereIsNoCycleToMeasure)
{
    // **A dash, not a zero, and not a length of time either.** What the
    // status bar draws is gated on hasStatus; what is checked here is that
    // a shell which has never seen a node has no cycle at all.
    ui::AppShell shell;
    core::MockEngine engine;
    shell.attach(&engine);
    shell.setSessionId(openSession(engine));

    EXPECT_FALSE(shell.hasStatus());
    EXPECT_EQ(shell.quantum(), 0);
    EXPECT_EQ(shell.latencyMs(), 0.0);
}

TEST(AppShellTest, ItUpdatesTheStatusAtLeastOnceASecondWhileRunning)
{
    // The criterion, measured rather than assumed: the shell works the
    // status out on a timer of its own while there is something to count.
    EXPECT_LE(ui::AppShell::kStatusIntervalMs, 1000);

    // **A timer needs an event loop to fire in**, and the rest of this
    // binary has none: nothing else here waits for anything.
    int argc = 1;
    std::array<char, 6> name{'s', 'h', 'e', 'l', 'l', '\0'};
    std::array<char *, 2> argv{name.data(), nullptr};
    const QCoreApplication application(argc, argv.data());

    core::TelemetrySlot slot;
    ui::AppShell shell;
    core::MockEngine engine;
    shell.attach(&engine, [&slot](const QString &) { return &slot; });
    shell.setSessionId(openSession(engine));

    core::TelemetrySnapshot snapshot;
    snapshot.running = true;
    snapshot.sampleRate = 48000;
    slot.store(snapshot);

    QSignalSpy status(&shell, &ui::AppShell::statusChanged);
    shell.start();
    ASSERT_GE(status.count(), 1) << "starting did not work the status out at all";

    // **Nobody asks it to.** The node publishes something new and the shell
    // notices on its own, inside a second.
    status.clear();
    snapshot.xruns = 7;
    slot.store(snapshot);
    EXPECT_TRUE(status.wait(1000)) << "the status never caught up on its own";
    EXPECT_EQ(shell.xruns(), 7);

    // And it stops counting: a timer that fires all day to say "idle" is a
    // battery bill.
    shell.stop();
    status.clear();
    snapshot.xruns = 8;
    slot.store(snapshot);
    EXPECT_FALSE(status.wait(ui::AppShell::kStatusIntervalMs * 3)) << "the shell went on ticking with nothing running";
}

TEST(AppShellTest, RunAndStopAreRefusedWhenTheAnswerHasNotArrived)
{
    // Starting and stopping are the two moments in which the button must
    // not be pressed again: the engine has been asked and has not answered.
    ui::AppShell shell;
    core::MockEngine engine;
    shell.attach(&engine);
    shell.setSessionId(openSession(engine));

    shell.start();
    EXPECT_TRUE(shell.isRunning());
    // The mock answers at once, so "busy" is never seen here; what can be
    // measured is that a second Run changes nothing.
    QSignalSpy states(&shell, &ui::AppShell::stateChanged);
    shell.start();
    EXPECT_EQ(states.count(), 0);

    shell.toggleRun();
    EXPECT_FALSE(shell.isRunning());
    shell.stop();
    EXPECT_FALSE(shell.isRunning());
}
