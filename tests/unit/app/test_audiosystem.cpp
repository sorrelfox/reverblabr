// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What the window knows about the audio system: that it learns it
// from the engine rather than from the system, that it notices the server
// going away, and that a Retry is the thing that brings it back.

#include "audiosystem.h"
#include "core/audiosystem.h"
#include "core/mockengine.h"

#include <QSignalSpy>
#include <gtest/gtest.h>

using namespace rb;
using namespace Qt::StringLiterals;

TEST(AudioSystemTest, ItAsksTheEngineAsSoonAsItHasOne)
{
    // **Not when something next changes.** A window that opens while the
    // audio system is away has to say so straight away, and a window that
    // opens while it is there has versions to show in the Info tab.
    ui::AudioSystem system;
    core::MockEngine engine;
    system.attach(&engine);

    EXPECT_TRUE(system.isConnected());
    EXPECT_EQ(system.state(), QString(core::audioSystemState::connected));
    EXPECT_FALSE(system.serverName().isEmpty());
    EXPECT_FALSE(system.serverVersion().isEmpty());
    EXPECT_FALSE(system.sessionManagerName().isEmpty());
    EXPECT_FALSE(system.sessionManagerVersion().isEmpty());
}

TEST(AudioSystemTest, WithNoEngineItClaimsNothing)
{
    ui::AudioSystem system;
    EXPECT_FALSE(system.isConnected());
    EXPECT_TRUE(system.serverName().isEmpty());
    EXPECT_TRUE(system.driver().isEmpty());
}

TEST(AudioSystemTest, TheServerGoingAwayIsNoticedAndRetryBringsItBack)
{
    ui::AudioSystem system;
    core::MockEngine engine;
    system.attach(&engine);
    ASSERT_TRUE(system.isConnected());

    QSignalSpy changes(&system, &ui::AudioSystem::changed);
    engine.setAudioSystemRunning(false);
    EXPECT_FALSE(system.isConnected());
    EXPECT_EQ(system.state(), QString(core::audioSystemState::waiting));
    EXPECT_EQ(changes.count(), 1);
    // Nothing is known about a server that is not there, and a version
    // remembered from before it went is not knowledge.
    EXPECT_TRUE(system.serverVersion().isEmpty());

    // **A server that is back is not a connection to it.** Something has to
    // ask for one, and until it does the interface is right to say so.
    engine.setAudioSystemRunning(true);
    EXPECT_FALSE(system.isConnected());

    system.reconnect();
    EXPECT_TRUE(system.isConnected());
    EXPECT_FALSE(system.serverVersion().isEmpty());
}

TEST(AudioSystemTest, RetryingWhileTheServerIsStillAwaySaysSoRatherThanNothing)
{
    // A Retry that leaves the interface waiting for a signal that never
    // comes is a Retry that looks like it worked.
    ui::AudioSystem system;
    core::MockEngine engine;
    system.attach(&engine);
    engine.setAudioSystemRunning(false);

    QSignalSpy changes(&system, &ui::AudioSystem::changed);
    system.reconnect();
    EXPECT_FALSE(system.isConnected());
    // The answer arrived; it was the same answer, which is why nothing
    // changed.
    EXPECT_EQ(changes.count(), 0);
    EXPECT_EQ(system.state(), QString(core::audioSystemState::waiting));
}

TEST(AudioSystemTest, TheGraphsClockTravelsWithTheRest)
{
    // The graph's rate and quantum go through the facade's JSON and
    // come out the other side, and a cycle is worked out from them.
    core::AudioSystemInfo info;
    info.state = QString(core::audioSystemState::connected);
    info.driver = u"Built-in Audio Analogue Stereo"_s;
    info.rate = 44100;
    info.quantum = 256;
    const core::AudioSystemInfo back = core::audioSystemFromJson(core::audioSystemToJson(info));
    EXPECT_EQ(back, info);

    ui::AudioSystem system;
    core::MockEngine engine;
    system.attach(&engine);
    EXPECT_EQ(system.rate(), 48000);
    EXPECT_EQ(system.quantum(), 1024);
    EXPECT_NEAR(system.cycleMs(), 21.333, 1e-3);

    // A graph with nothing said about its clock has no cycle to work out.
    ui::AudioSystem unattached;
    EXPECT_EQ(unattached.rate(), 0);
    EXPECT_EQ(unattached.cycleMs(), 0.0);
}
