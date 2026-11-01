// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// When a surface may still believe what a block published.

#include "telemetrywatch.h"

#include <gtest/gtest.h>

#include <cstring>

using namespace rb;
using ui::TelemetryWatch;

namespace
{

[[nodiscard]] core::TelemetrySnapshot running(std::uint64_t frame)
{
    core::TelemetrySnapshot snapshot;
    snapshot.frame = frame;
    snapshot.running = true;
    return snapshot;
}

} // namespace

TEST(TelemetryWatch, NothingSeenIsStopped)
{
    const TelemetryWatch watch;
    EXPECT_EQ(watch.stateOf(true, 0), TelemetryWatch::kStopped);
}

TEST(TelemetryWatch, ABlockInARunningSessionIsWatched)
{
    TelemetryWatch watch;
    watch.saw(running(1), 1000);
    EXPECT_EQ(watch.stateOf(true, 1000), TelemetryWatch::kWatching);
}

TEST(TelemetryWatch, NoPublishedBlockIsStopped)
{
    TelemetryWatch watch;
    watch.saw(running(1), 1000);
    EXPECT_EQ(watch.stateOf(false, 1000), TelemetryWatch::kStopped);

    core::TelemetrySnapshot stopped;
    stopped.frame = 2;
    watch.saw(stopped, 1016);
    EXPECT_EQ(watch.stateOf(true, 1016), TelemetryWatch::kStopped);
}

TEST(TelemetryWatch, OverloadWithFramesStillComingIsOverloaded)
{
    TelemetryWatch watch;
    core::TelemetrySnapshot snapshot = running(1);
    snapshot.shedding = true;
    watch.saw(snapshot, 1000);
    EXPECT_EQ(watch.stateOf(true, 1000), TelemetryWatch::kOverloaded);
    snapshot.frame = 2;
    snapshot.shedding = false;
    watch.saw(snapshot, 1016);
    EXPECT_EQ(watch.stateOf(true, 1016), TelemetryWatch::kWatching);
}

TEST(TelemetryWatch, AFrameThatStandsStillIsNotBelieved)
{
    TelemetryWatch watch;
    watch.saw(running(7), 1000);
    watch.saw(running(7), 1499);
    EXPECT_EQ(watch.stateOf(true, 1499), TelemetryWatch::kWatching);
    EXPECT_FALSE(watch.isUnmoved(1499));
    watch.saw(running(7), 1500);
    EXPECT_EQ(watch.stateOf(true, 1500), TelemetryWatch::kNoNews);
    EXPECT_TRUE(watch.isUnmoved(1500));
    // It takes no further read: a timer that keeps asking is enough.
    EXPECT_EQ(watch.stateOf(true, 5000), TelemetryWatch::kNoNews);

    watch.saw(running(8), 5001);
    EXPECT_EQ(watch.stateOf(true, 5001), TelemetryWatch::kWatching);
}

TEST(TelemetryWatch, AStoppedSessionsLastFrameIsNoAlarm)
{
    TelemetryWatch watch;
    core::TelemetrySnapshot stopped;
    stopped.frame = 3;
    watch.saw(stopped, 1000);
    EXPECT_FALSE(watch.isUnmoved(9000)) << "a sampler stops after its last word";
    EXPECT_EQ(watch.stateOf(true, 9000), TelemetryWatch::kStopped);
}

TEST(TelemetryWatch, ForgettingStartsAgain)
{
    TelemetryWatch watch;
    watch.saw(running(7), 1000);
    watch.forget();
    EXPECT_EQ(watch.stateOf(true, 1000), TelemetryWatch::kStopped);
    watch.saw(running(7), 9000);
    EXPECT_EQ(watch.stateOf(true, 9000), TelemetryWatch::kWatching) << "the same number from another session";
}

TEST(TelemetryWatch, FindsABlockByItsId)
{
    core::TelemetrySnapshot snapshot;
    snapshot.blockCount = 2;
    std::strncpy(snapshot.blocks[0].id, "first", core::kMaxTelemetryIdBytes - 1);
    std::strncpy(snapshot.blocks[1].id, "second", core::kMaxTelemetryIdBytes - 1);
    EXPECT_EQ(TelemetryWatch::find(snapshot, QStringLiteral("second")), &snapshot.blocks[1]);
    EXPECT_EQ(TelemetryWatch::find(snapshot, QStringLiteral("third")), nullptr);
}
