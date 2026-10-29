// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The interface's side of the telemetry pipeline: one read per frame
// for the whole session, and everything about time — how long a peak stays
// up, how fast it falls, how long a clip light stays on — computed here,
// where the clock a person watches is.

#include "dsp/decibel.h"
#include "meterhub.h"

#include <QElapsedTimer>
#include <gtest/gtest.h>

#include <cstring>

using namespace rb;
using namespace rb::ui;
using namespace Qt::StringLiterals;

namespace
{

/// A clock the test winds by hand: a second of decay costs it nothing.
class Hand
{
public:
    void wind(qint64 milliseconds)
    {
        m_now += milliseconds;
    }

    [[nodiscard]] std::function<qint64()> clock()
    {
        return [this] { return m_now; };
    }

private:
    qint64 m_now = 1;
};

/// A snapshot as the sampler would publish it.
class Publisher
{
public:
    [[nodiscard]] core::BlockTelemetry &block(std::size_t index, const char *id, std::uint32_t channels = 2)
    {
        core::BlockTelemetry &into = m_snapshot.blocks[index];
        std::strncpy(into.id, id, core::kMaxTelemetryIdBytes - 1);
        into.channelCount = channels;
        m_snapshot.blockCount = std::max<std::uint32_t>(m_snapshot.blockCount, static_cast<std::uint32_t>(index) + 1);
        return into;
    }

    void publish()
    {
        ++m_snapshot.frame;
        m_snapshot.running = true;
        m_slot.store(m_snapshot);
    }

    /// Publishes the same snapshot again, with everything measured cleared,
    /// which is what a quiet frame looks like: the accumulators were emptied
    /// by the last read.
    void publishSilence()
    {
        for (std::uint32_t index = 0; index < m_snapshot.blockCount; ++index) {
            core::BlockTelemetry &block = m_snapshot.blocks[index];
            for (float &peak : block.peak) {
                peak = 0.0f;
            }
            for (float &rms : block.rms) {
                rms = 0.0f;
            }
            block.clipped = false;
            block.active = false;
        }
        publish();
    }

    [[nodiscard]] core::TelemetrySlot &slot()
    {
        return m_slot;
    }

    /// Forgets the blocks without forgetting where the numbering is: a
    /// snapshot whose frame number went backwards is not something the
    /// engine can produce.
    void clearBlocks()
    {
        const std::uint64_t frame = m_snapshot.frame;
        m_snapshot = core::TelemetrySnapshot{};
        m_snapshot.frame = frame;
    }

    [[nodiscard]] core::TelemetrySnapshot &snapshot()
    {
        return m_snapshot;
    }

private:
    core::TelemetrySnapshot m_snapshot;
    core::TelemetrySlot m_slot;
};

} // namespace

TEST(MeterHubTest, NothingToWatchIsNotACrash)
{
    MeterHub hub;
    EXPECT_FALSE(hub.tick());
    EXPECT_TRUE(hub.blocks().isEmpty());
    EXPECT_EQ(hub.block(u"b1"_s), nullptr);
    EXPECT_FALSE(hub.isRunning());
}

TEST(MeterHubTest, LevelsArriveInDecibelsAgainstTheBlockThatMadeThem)
{
    Publisher published;
    core::BlockTelemetry &block = published.block(0, "b1");
    block.peak[0] = 0.5f; // −6 dBFS
    block.rms[0] = 0.25f; // −12 dBFS
    block.peak[1] = 1.0f;
    block.rms[1] = 0.5f;
    block.active = true;
    published.block(1, "b2").peak[0] = 0.0f;
    published.publish();

    MeterHub hub;
    hub.watch(&published.slot());
    ASSERT_TRUE(hub.tick());

    ASSERT_EQ(hub.blocks().size(), 2);
    const BlockLevels *first = hub.block(u"b1"_s);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->channelCount, 2U);
    EXPECT_NEAR(first->peakDb[0], -6.02f, 0.01f);
    EXPECT_NEAR(first->rmsDb[0], -12.04f, 0.01f);
    EXPECT_NEAR(first->peakDb[1], 0.0f, 0.01f);
    EXPECT_TRUE(first->active);

    // Nothing measured is silence, not zero decibels.
    const BlockLevels *second = hub.block(u"b2"_s);
    ASSERT_NE(second, nullptr);
    EXPECT_FLOAT_EQ(second->peakDb[0], dsp::kSilenceDb);
    EXPECT_FALSE(second->active);
}

TEST(MeterHubTest, ThePeakLineJumpsUpHoldsAndThenFalls)
{
    Publisher published;
    Hand hand;
    published.block(0, "b1").peak[0] = 1.0f;
    published.publish();

    MeterHub hub;
    hub.setClock(hand.clock());
    hub.watch(&published.slot());
    ASSERT_TRUE(hub.tick());
    EXPECT_NEAR(hub.block(u"b1"_s)->holdDb[0], 0.0f, 0.01f);

    // It stays where it was put for as long as the hold lasts, however
    // quiet things go.
    published.publishSilence();
    hand.wind(MeterHub::kHoldMs - 100);
    ASSERT_TRUE(hub.tick());
    EXPECT_NEAR(hub.block(u"b1"_s)->holdDb[0], 0.0f, 0.01f);
    EXPECT_FLOAT_EQ(hub.block(u"b1"_s)->peakDb[0], dsp::kSilenceDb) << "the peak itself should not hold";

    // And then it falls at a steady number of decibels a second.
    published.publishSilence();
    hand.wind(1000);
    ASSERT_TRUE(hub.tick());
    EXPECT_NEAR(hub.block(u"b1"_s)->holdDb[0], -MeterHub::kDecayDbPerSecond, 1.0f);

    // A louder peak on the way down puts it straight back up.
    published.block(0, "b1").peak[0] = 0.5f;
    published.publish();
    hand.wind(16);
    ASSERT_TRUE(hub.tick());
    EXPECT_NEAR(hub.block(u"b1"_s)->holdDb[0], -6.02f, 0.01f);
}

TEST(MeterHubTest, TheHoldFallsOnTheClockAndNotOnTheFrames)
{
    // Two hubs, the same snapshots, different numbers of frames: what the
    // meter shows must depend on how long it has been, not on how often the
    // interface got round to looking.
    Publisher published;
    published.block(0, "b1").peak[0] = 1.0f;
    published.publish();

    Hand fast;
    MeterHub often;
    often.setClock(fast.clock());
    often.watch(&published.slot());
    ASSERT_TRUE(often.tick());

    Hand slow;
    MeterHub rarely;
    rarely.setClock(slow.clock());
    rarely.watch(&published.slot());
    ASSERT_TRUE(rarely.tick());

    // The same 2400 ms — the hold and then a second of falling — in sixty
    // frames and in two.
    for (int frame = 0; frame < 60; ++frame) {
        published.publishSilence();
        fast.wind(40);
        (void)often.tick();
    }
    for (int frame = 0; frame < 2; ++frame) {
        published.publishSilence();
        slow.wind(1200);
        (void)rarely.tick();
    }

    EXPECT_NEAR(often.block(u"b1"_s)->holdDb[0], rarely.block(u"b1"_s)->holdDb[0], 1.0f);
    EXPECT_NEAR(often.block(u"b1"_s)->holdDb[0], -24.0f, 1.0f) << "a second of falling is twenty decibels";
}

TEST(MeterHubTest, AClipLightStaysOnLongEnoughToSee)
{
    Publisher published;
    Hand hand;
    published.block(0, "b1").clipped = true;
    published.publish();

    MeterHub hub;
    hub.setClock(hand.clock());
    hub.watch(&published.slot());
    ASSERT_TRUE(hub.tick());
    EXPECT_TRUE(hub.block(u"b1"_s)->clipped);

    published.publishSilence();
    hand.wind(MeterHub::kClipHoldMs - 100);
    ASSERT_TRUE(hub.tick());
    EXPECT_TRUE(hub.block(u"b1"_s)->clipped) << "one sample over full scale was gone before anybody saw it";

    published.publishSilence();
    hand.wind(200);
    ASSERT_TRUE(hub.tick());
    EXPECT_FALSE(hub.block(u"b1"_s)->clipped);
}

TEST(MeterHubTest, TheSameSnapshotTwiceIsNotNews)
{
    Publisher published;
    published.block(0, "b1").peak[0] = 0.5f;
    published.publish();

    MeterHub hub;
    int updates = 0;
    QObject::connect(&hub, &MeterHub::updated, &hub, [&updates] { ++updates; });
    hub.watch(&published.slot());

    EXPECT_TRUE(hub.tick());
    EXPECT_FALSE(hub.tick()) << "a display faster than the sampler read the same frame as news";
    EXPECT_EQ(updates, 1);
    // It is still read, though: the holds go on falling.
    EXPECT_NEAR(hub.block(u"b1"_s)->holdDb[0], -6.02f, 0.01f);

    published.publish();
    EXPECT_TRUE(hub.tick());
    EXPECT_EQ(updates, 2);
}

TEST(MeterHubTest, ABlockThatIsReplacedDoesNotInheritTheMeterOfTheOneBefore)
{
    Publisher published;
    published.block(0, "b1").peak[0] = 1.0f;
    published.publish();

    MeterHub hub;
    Hand hand;
    hub.setClock(hand.clock());
    hub.watch(&published.slot());
    ASSERT_TRUE(hub.tick());
    EXPECT_NEAR(hub.block(u"b1"_s)->holdDb[0], 0.0f, 0.01f);

    // The session is edited and another block takes that slot.
    published.clearBlocks();
    published.block(0, "b9").peak[0] = 0.0f;
    published.publish();
    hand.wind(16);
    ASSERT_TRUE(hub.tick());

    EXPECT_EQ(hub.block(u"b1"_s), nullptr) << "a block that is gone is still being shown";
    ASSERT_NE(hub.block(u"b9"_s), nullptr);
    EXPECT_FLOAT_EQ(hub.block(u"b9"_s)->holdDb[0], dsp::kSilenceDb) << "it started at the level of the block before it";
}

TEST(MeterHubTest, TheStatusBarFiguresComeThrough)
{
    Publisher published;
    published.snapshot().dspLoad = 0.12f;
    published.snapshot().peakDspLoad = 0.4f;
    published.snapshot().xruns = 3;
    published.snapshot().sampleRate = 48000;
    published.snapshot().frames = 1024;
    published.publish();

    MeterHub hub;
    hub.watch(&published.slot());
    ASSERT_TRUE(hub.tick());
    EXPECT_FLOAT_EQ(hub.dspLoad(), 0.12f);
    EXPECT_FLOAT_EQ(hub.peakDspLoad(), 0.4f);
    EXPECT_EQ(hub.xruns(), 3U);
    EXPECT_EQ(hub.sampleRate(), 48000U);
    EXPECT_EQ(hub.frames(), 1024U);
    EXPECT_TRUE(hub.isRunning());
}

TEST(MeterHubTest, FiftyMetersCostLessThanAMillisecondAFrame)
{
    // The whole point of one snapshot per session: a session with fifty
    // meters on the screen must not cost the interface its frame.
    Publisher published;
    constexpr int kBlocks = 25; // fifty meters, two channels each
    for (int block = 0; block < kBlocks; ++block) {
        core::BlockTelemetry &into = published.block(static_cast<std::size_t>(block),
                                                     QByteArray("block" + QByteArray::number(block)).constData());
        into.peak[0] = 0.5f;
        into.peak[1] = 0.25f;
        into.rms[0] = 0.3f;
        into.rms[1] = 0.1f;
        into.active = true;
    }

    MeterHub hub;
    hub.watch(&published.slot());
    constexpr int kFrames = 2000;
    QElapsedTimer timer;
    timer.start();
    for (int frame = 0; frame < kFrames; ++frame) {
        published.publish();
        (void)hub.tick();
        // What a face does with what it reads: fifty lookups by id.
        for (int block = 0; block < kBlocks; ++block) {
            const BlockLevels *levels = hub.block(QString::fromLatin1("block%1").arg(block));
            ASSERT_NE(levels, nullptr);
            EXPECT_GT(levels->holdDb[0], dsp::kSilenceDb);
        }
    }
    const double perFrameMs = static_cast<double>(timer.nsecsElapsed()) / kFrames / 1e6;
    EXPECT_LT(perFrameMs, 1.0) << "a frame of meters took " << perFrameMs << " ms";
}

TEST(MeterHubTest, ABlocksLatencyArrivesInMilliseconds)
{
    // How late a block makes the sound, which its tile says. Samples
    // in the snapshot, milliseconds at the snapshot's rate here.
    Publisher published;
    published.block(0, "volume").latencySamples = 72;
    (void)published.block(1, "meter");
    published.snapshot().sampleRate = 48000;
    published.publish();

    MeterHub hub;
    hub.watch(&published.slot());
    ASSERT_TRUE(hub.tick());
    EXPECT_NEAR(hub.block(u"volume"_s)->latencyMs, 1.5f, 1e-6f);
    EXPECT_FLOAT_EQ(hub.block(u"meter"_s)->latencyMs, 0.0f);
}
