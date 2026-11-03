// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Who is drawing what, and what can honestly be drawn: a surface holds a feed
// while it shows, the feed asks for the block about once a second and reads
// the slot at the block's refresh rate, and whenever the sound can't be
// vouched for it says so rather than leave a trace standing.

#include "core/sharedtelemetry.h"
#include "meterhub.h"
#include "scopehub.h"

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <memory>
#include <vector>

using namespace rb;
using namespace rb::ui;
using namespace Qt::StringLiterals;

namespace
{

constexpr auto kBlock = "scope";

class ScopeHubTest : public ::testing::Test
{
protected:
    ScopeHubTest()
        : hub(&meters)
    {
        meters.setClock([this] { return static_cast<qint64>(now); });
        hub.setClock([this] { return now; });
        hub.setAsker([this](const QString &session, const QString &block) { asked.push_back(session + u'/' + block); });
        meters.watch(&memory.slot());
        hub.watch(u"session"_s, &memory.scopes());
        std::strncpy(block().id, kBlock, core::kMaxTelemetryIdBytes - 1);
        block().enabled = true;
        snapshot.running = true;
        snapshot.blockCount = 1;
        snapshot.sampleRate = 48000;
        snapshot.frames = 1024;
        rt::claimScopeSlot(memory.scopes().slots[2]);
    }

    [[nodiscard]] core::BlockTelemetry &block()
    {
        return snapshot.blocks[0];
    }

    /// One tick of the sampler and of the window's meters.
    void publish()
    {
        ++snapshot.frame;
        memory.slot().store(snapshot);
        (void)meters.tick();
    }

    void hear(std::uint32_t frames)
    {
        std::vector<float> left(frames, 0.5f);
        writer.write(memory.scopes().slots[2], left.data(), nullptr, frames, 48000);
    }

    void giveSlot()
    {
        block().scopeSlot = 2;
    }

    /// Everything going as it should, for a feed that has been read once.
    void goLive(ScopeFeed &feed)
    {
        giveSlot();
        hear(4800);
        publish();
        feed.tick();
        ASSERT_EQ(feed.state(), ScopeFeed::State::Live);
    }

    double now = 1000.0;
    core::PublishedSlot memory;
    core::TelemetrySnapshot snapshot;
    MeterHub meters;
    ScopeHub hub;
    rt::ScopeWriter writer;
    QStringList asked;
};

} // namespace

TEST_F(ScopeHubTest, TwoSurfacesOfOneBlockShareOneFeed)
{
    ScopeFeed *first = hub.subscribe(u"scope"_s);
    ScopeFeed *second = hub.subscribe(u"scope"_s);
    EXPECT_EQ(first, second);
    EXPECT_EQ(hub.feedCount(), 1);
    hub.unsubscribe(second);
    EXPECT_EQ(hub.feedCount(), 1) << "the other surface is still showing";
    hub.unsubscribe(first);
    EXPECT_EQ(hub.feedCount(), 0);
}

TEST_F(ScopeHubTest, ABlockIsAskedForAboutOnceASecondWhileItShows)
{
    ScopeFeed *feed = hub.subscribe(u"scope"_s);
    publish();
    for (int tick = 0; tick < 100; ++tick) {
        feed->tick();
        now += 30.0;
    }
    EXPECT_EQ(asked.size(), 3) << "three seconds of ticks should ask three times";
    EXPECT_EQ(asked.front(), u"session/scope"_s);

    hub.unsubscribe(feed);
    EXPECT_EQ(hub.feedCount(), 0) << "and then nobody is left to ask";
}

TEST_F(ScopeHubTest, AHeldTraceStopsAskingForMore)
{
    ScopeFeed *feed = hub.subscribe(u"scope"_s);
    goLive(*feed);
    feed->setHold(true);
    const qsizetype asks = asked.size();
    for (int tick = 0; tick < 10; ++tick) {
        now += 1000.0;
        feed->tick();
    }
    EXPECT_EQ(asked.size(), asks);
    feed->setHold(false);
    feed->tick();
    EXPECT_EQ(asked.size(), asks + 1) << "let go of, it asks straight away";
    hub.unsubscribe(feed);
}

TEST_F(ScopeHubTest, ATraceComesFromTheSlotTheBlockWasGiven)
{
    ScopeFeed *feed = hub.subscribe(u"scope"_s);
    ScopeSettings settings;
    settings.spanMs = 100.0;
    feed->setSettings(settings);
    goLive(*feed);
    EXPECT_TRUE(feed->hasTrace());
    const ScopeFrame &frame = feed->frame();
    EXPECT_EQ(frame.sampleRate, 48000U);
    EXPECT_EQ(frame.framesPerPoint, 1U);
    EXPECT_EQ(frame.points.back().low[rt::ScopeLeft], 0.5f);
    EXPECT_DOUBLE_EQ(frame.newestFrame, 4800.0);
    EXPECT_NEAR(frame.cycleMs, 1024.0 / 48.0, 1.0e-9);
    hub.unsubscribe(feed);
}

TEST_F(ScopeHubTest, ALongerViewReadsACoarserRingAndKeepsCountingInSamples)
{
    ScopeFeed *feed = hub.subscribe(u"scope"_s);
    goLive(*feed);
    ScopeSettings settings;
    settings.spanMs = 2000.0;
    feed->setSettings(settings);
    feed->tick();
    EXPECT_EQ(feed->frame().framesPerPoint, 64U);
    EXPECT_DOUBLE_EQ(feed->frame().newestFrame, 4800.0) << "75 points of 64";
    hub.unsubscribe(feed);
}

TEST_F(ScopeHubTest, NothingNewIsNoNewFrame)
{
    ScopeFeed *feed = hub.subscribe(u"scope"_s);
    goLive(*feed);
    const std::uint64_t serial = feed->frame().serial;
    feed->tick();
    EXPECT_EQ(feed->frame().serial, serial);
    hear(480);
    feed->tick();
    EXPECT_EQ(feed->frame().serial, serial + 1);
    hub.unsubscribe(feed);
}

TEST_F(ScopeHubTest, ASlotGivenOutAgainIsAnotherSound)
{
    ScopeFeed *feed = hub.subscribe(u"scope"_s);
    goLive(*feed);
    const std::uint64_t run = feed->frame().run;
    rt::claimScopeSlot(memory.scopes().slots[2]);
    hear(960);
    feed->tick();
    EXPECT_NE(feed->frame().run, run);
    EXPECT_DOUBLE_EQ(feed->frame().newestFrame, 960.0) << "the count starts again with it";
    hub.unsubscribe(feed);
}

TEST_F(ScopeHubTest, ASessionNotRunningShowsNoTrace)
{
    ScopeFeed *feed = hub.subscribe(u"scope"_s);
    goLive(*feed);
    snapshot.running = false;
    snapshot.blockCount = 0;
    publish();
    feed->tick();
    EXPECT_EQ(feed->state(), ScopeFeed::State::Stopped);
    EXPECT_FALSE(feed->hasTrace()) << "a frozen trace looks like a steady sound";
    hub.unsubscribe(feed);
}

TEST_F(ScopeHubTest, AnOverloadedSessionShowsNoTrace)
{
    ScopeFeed *feed = hub.subscribe(u"scope"_s);
    goLive(*feed);
    snapshot.shedding = true;
    publish();
    feed->tick();
    EXPECT_EQ(feed->state(), ScopeFeed::State::Overloaded);
    EXPECT_FALSE(feed->hasTrace());
    hub.unsubscribe(feed);
}

TEST_F(ScopeHubTest, AnEngineThatHasGoneShowsNoTrace)
{
    ScopeFeed *feed = hub.subscribe(u"scope"_s);
    goLive(*feed);
    // Its last snapshot stands in the memory, still saying it runs.
    now += static_cast<double>(TelemetryWatch::kUnmovedMs) + 1.0;
    (void)meters.tick();
    feed->tick();
    EXPECT_EQ(feed->state(), ScopeFeed::State::NoNews);
    EXPECT_FALSE(feed->hasTrace());
    hub.unsubscribe(feed);
}

TEST_F(ScopeHubTest, ABlockThatStoppedRunningShowsNoTrace)
{
    // Frames still come, and only the ring stands still: a node no longer
    // being scheduled.
    ScopeFeed *feed = hub.subscribe(u"scope"_s);
    goLive(*feed);
    now += static_cast<double>(ScopeFeed::kUnmovedMs) - 1.0;
    publish();
    feed->tick();
    EXPECT_EQ(feed->state(), ScopeFeed::State::Live);
    now += 2.0;
    publish();
    feed->tick();
    EXPECT_EQ(feed->state(), ScopeFeed::State::NoNews);
    EXPECT_FALSE(feed->hasTrace());
    hear(480);
    feed->tick();
    EXPECT_EQ(feed->state(), ScopeFeed::State::Live) << "and it comes back by itself";
    hub.unsubscribe(feed);
}

TEST_F(ScopeHubTest, ALongCycleIsNoReasonToDoubtTheSound)
{
    ScopeFeed *feed = hub.subscribe(u"scope"_s);
    snapshot.frames = 96000; // Two seconds a cycle: nothing real, but it shows the rule.
    goLive(*feed);
    now += 3000.0;
    publish();
    feed->tick();
    EXPECT_EQ(feed->state(), ScopeFeed::State::Live);
    hub.unsubscribe(feed);
}

TEST_F(ScopeHubTest, ABlockSwitchedOffShowsNoTrace)
{
    ScopeFeed *feed = hub.subscribe(u"scope"_s);
    goLive(*feed);
    block().enabled = false;
    publish();
    feed->tick();
    EXPECT_EQ(feed->state(), ScopeFeed::State::Off);
    EXPECT_FALSE(feed->hasTrace());
    hub.unsubscribe(feed);
}

TEST_F(ScopeHubTest, ABlockThatGetsNoSlotSaysSoAfterAMoment)
{
    ScopeFeed *feed = hub.subscribe(u"scope"_s);
    publish();
    feed->tick();
    EXPECT_EQ(feed->state(), ScopeFeed::State::Waiting);
    now += static_cast<double>(ScopeFeed::kWaitForSlotMs) + 1.0;
    publish();
    feed->tick();
    EXPECT_EQ(feed->state(), ScopeFeed::State::NoRoom);
    hub.unsubscribe(feed);
}

TEST_F(ScopeHubTest, AHeldTraceStaysThroughAnything)
{
    ScopeFeed *feed = hub.subscribe(u"scope"_s);
    goLive(*feed);
    const double held = feed->frame().newestFrame;
    feed->setHold(true);
    hear(4800);
    snapshot.running = false;
    snapshot.blockCount = 0;
    publish();
    feed->tick();
    EXPECT_TRUE(feed->hasTrace()) << "somebody asked for this one to stay";
    EXPECT_DOUBLE_EQ(feed->frame().newestFrame, held);
    hub.unsubscribe(feed);
}

TEST_F(ScopeHubTest, AnotherSessionForgetsTheTrace)
{
    ScopeFeed *feed = hub.subscribe(u"scope"_s);
    goLive(*feed);
    const std::uint64_t run = feed->frame().run;
    core::PublishedSlot other;
    hub.watch(u"another"_s, &other.scopes());
    EXPECT_FALSE(feed->hasTrace());
    EXPECT_NE(feed->frame().run, run);
    feed->tick();
    EXPECT_EQ(asked.back(), u"another/scope"_s);
    hub.watch(u"session"_s, &memory.scopes());
    hub.unsubscribe(feed);
}

TEST_F(ScopeHubTest, TheSameMemoryAgainChangesNothing)
{
    // What every snapshot of a telemetry notice comes to: said again.
    ScopeFeed *feed = hub.subscribe(u"scope"_s);
    goLive(*feed);
    hub.watch(u"session"_s, &memory.scopes());
    EXPECT_TRUE(feed->hasTrace());
    hub.unsubscribe(feed);
}

TEST_F(ScopeHubTest, TheCycleLengthSaysHowMuchARefreshCanShow)
{
    ScopeFeed *feed = hub.subscribe(u"scope"_s);
    goLive(*feed);
    EXPECT_NEAR(feed->arrivalsPerSecond(), 48000.0 / 1024.0, 1.0e-6);
    hub.unsubscribe(feed);
}
