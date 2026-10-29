// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What the Peak/RMS block's panel reads: the numbers, all the way
// from a signal through the real block and a telemetry snapshot to the two
// readouts; the clip dot a side, latched and put out; the hold waiting for
// as long as it was asked to; and thirty tiles' worth of meters inside the
// frame.

#include "analysis.h"
#include "blocks/peakrmsmeter.h"
#include "dsp/decibel.h"
#include "meterhub.h"
#include "meteritem.h"
#include "meterscale.h"
#include "offlinerunner.h"
#include "peakhold.h"
#include "signals.h"

#include <QElapsedTimer>
#include <QSGNode>
#include <gtest/gtest.h>

#include <cstring>
#include <memory>

using namespace rb;
using namespace rb::ui;
using namespace Qt::StringLiterals;

namespace
{

/// A clock the test winds by hand: five seconds of holding costs it nothing.
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

/// A snapshot as the sampler would publish it: amplitudes, one block, two
/// sides, and the block's own published values beside them.
class Publisher
{
public:
    Publisher()
    {
        core::BlockTelemetry &block = m_snapshot.blocks[0];
        std::strncpy(block.id, "meter", core::kMaxTelemetryIdBytes - 1);
        block.channelCount = 2;
        m_snapshot.blockCount = 1;
        m_snapshot.running = true;
    }

    void publish(float leftPeak, float rightPeak, float leftRms = 0.0f, float rightRms = 0.0f)
    {
        core::BlockTelemetry &block = m_snapshot.blocks[0];
        block.peak[0] = leftPeak;
        block.peak[1] = rightPeak;
        block.rms[0] = leftRms;
        block.rms[1] = rightRms;
        block.values[blocks::PeakRmsMeter::kRmsLeftValue] = leftRms;
        block.values[blocks::PeakRmsMeter::kRmsRightValue] = rightRms;
        block.active = leftPeak > 0.0f || rightPeak > 0.0f;
        // Exactly what the graph's meter latches on, so that a click at full
        // scale arrives here the way it would from a real session.
        block.clipped = leftPeak >= 1.0f || rightPeak >= 1.0f;
        ++m_snapshot.frame;
        m_slot.store(m_snapshot);
    }

    [[nodiscard]] core::TelemetrySlot &slot()
    {
        return m_slot;
    }

private:
    core::TelemetrySnapshot m_snapshot;
    core::TelemetrySlot m_slot;
};

/// Two meter blocks in one session, which is a lane with a meter at each
/// end: the case one hold for the whole window cannot serve.
class TwoMeters
{
public:
    TwoMeters()
    {
        for (std::uint32_t index = 0; index < 2; ++index) {
            core::BlockTelemetry &block = m_snapshot.blocks[index];
            std::strncpy(block.id, index == 0 ? "in" : "out", core::kMaxTelemetryIdBytes - 1);
            block.channelCount = 2;
        }
        m_snapshot.blockCount = 2;
        m_snapshot.running = true;
    }

    /// The same level into both, so that any difference between the two
    /// meters afterwards is the holding and nothing else.
    void publish(float peak)
    {
        for (std::uint32_t index = 0; index < 2; ++index) {
            core::BlockTelemetry &block = m_snapshot.blocks[index];
            block.peak[0] = peak;
            block.peak[1] = peak;
            block.active = peak > 0.0f;
        }
        ++m_snapshot.frame;
        m_slot.store(m_snapshot);
    }

    [[nodiscard]] core::TelemetrySlot &slot()
    {
        return m_slot;
    }

private:
    core::TelemetrySnapshot m_snapshot;
    core::TelemetrySlot m_slot;
};

/// When each of the two lines first came down, in milliseconds from the loud
/// frame, and -1 for one that never did.
struct FellAt {
    qint64 first = -1;
    qint64 second = -1;
};

/// One loud frame into both meters and then quiet ones, watching both lines
/// at once. **The same run for both**: two runs would only prove that each
/// setting works when it is the only one, which is what the hub could
/// already do.
[[nodiscard]] FellAt watchBothComeDown(MeterHub &hub, Hand &hand, TwoMeters &publisher, qint64 forMs)
{
    publisher.publish(1.0f);
    (void)hub.tick();
    float previousFirst = hub.block(u"in"_s)->holdDb[0];
    float previousSecond = hub.block(u"out"_s)->holdDb[0];

    FellAt fell;
    for (qint64 elapsed = 0; elapsed < forMs; elapsed += 16) {
        hand.wind(16);
        publisher.publish(0.0f);
        (void)hub.tick();
        const float held = hub.block(u"in"_s)->holdDb[0];
        const float other = hub.block(u"out"_s)->holdDb[0];
        if (fell.first < 0 && held < previousFirst - 0.05f) {
            fell.first = elapsed + 16;
        }
        if (fell.second < 0 && other < previousSecond - 0.05f) {
            fell.second = elapsed + 16;
        }
        previousFirst = held;
        previousSecond = other;
    }
    return fell;
}

[[nodiscard]] test::MultiSignal stereoSine(std::size_t frames, double hz, float amplitude)
{
    test::MultiSignal signal;
    signal.push_back(test::sine(frames, hz, test::kTestSampleRate, amplitude));
    signal.push_back(test::sine(frames, hz, test::kTestSampleRate, amplitude));
    return signal;
}

/// A meter with its paint node reachable, so the per-frame cost can be
/// measured without a window to put it in.
class Measurable : public MeterItem
{
public:
    QSGNode *paint(QSGNode *node)
    {
        return updatePaintNode(node, nullptr);
    }
};

} // namespace

TEST(PeakRmsPanelTest, ASineEighteenDownReadsMinusEighteenPeakAndMinusTwentyOneRms)
{
    // The criterion, through the whole chain the panel reads: the real
    // block measures the signal, the snapshot carries what it measured as
    // amplitudes, and the panel turns those into the two numbers.
    //
    // **Plain root mean square**, so a sine reads 3.01 dB below its peak.
    // The panel's tooltip says so, because two meters that disagree by 3 dB
    // are two meters somebody will spend an afternoon on.
    blocks::PeakRmsMeter block;
    test::BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());

    const float amplitude = dsp::dbToGain(-18.0f);
    const test::MultiSignal out = runner.run(stereoSine(48000, 1000.0, amplitude));

    Hand hand;
    Publisher publisher;
    MeterHub hub;
    hub.setClock(hand.clock());
    hub.watch(&publisher.slot());
    publisher.publish(test::peak(out[0]), test::peak(out[1]), block.rms(0), block.rms(1));
    (void)hub.tick();

    const BlockLevels *levels = hub.block(u"meter"_s);
    ASSERT_NE(levels, nullptr);

    const MeterScale scale;
    // Peak: the held line, which is what the panel writes out — a number
    // that changed sixty times a second would be something to watch rather
    // than something to read.
    EXPECT_NEAR(levels->holdDb[0], -18.0f, 0.1f);
    EXPECT_NEAR(levels->holdDb[1], -18.0f, 0.1f);
    EXPECT_NEAR(scale.dbOf(levels->values[blocks::PeakRmsMeter::kRmsLeftValue]), -21.0, 0.1);
    EXPECT_NEAR(scale.dbOf(levels->values[blocks::PeakRmsMeter::kRmsRightValue]), -21.0, 0.1);
}

TEST(PeakRmsPanelTest, ASingleSampleAtFullScaleLightsTheDotOnItsOwnSide)
{
    // The criterion: a single-sample 0 dBFS click lights the clip dot, and
    // clicking it resets. **A dot a side**, because which of the two it was
    // is the first thing somebody wants to know.
    Hand hand;
    Publisher publisher;
    MeterHub hub;
    hub.setClock(hand.clock());
    hub.watch(&publisher.slot());

    // One sample at full scale on the left, in a cycle that is otherwise
    // quiet: the peak the sampler publishes is that one sample.
    publisher.publish(1.0f, 0.02f);
    (void)hub.tick();

    const BlockLevels *levels = hub.block(u"meter"_s);
    ASSERT_NE(levels, nullptr);
    EXPECT_TRUE(levels->clipped);
    EXPECT_TRUE(levels->clippedChannels[0]);
    EXPECT_FALSE(levels->clippedChannels[1]) << "the right side was blamed for the left side's clip";

    // **It stays lit while the sound goes on**, or a click nobody was
    // looking at would be a click nobody ever knew about.
    for (int frame = 0; frame < 20; ++frame) {
        hand.wind(16);
        publisher.publish(0.02f, 0.02f);
        (void)hub.tick();
    }
    EXPECT_TRUE(hub.block(u"meter"_s)->clippedChannels[0]);

    // And clicking puts it out on both sides at once, which is what the
    // panel's reset does.
    hub.clearClip(u"meter"_s);
    EXPECT_FALSE(hub.block(u"meter"_s)->clipped);
    EXPECT_FALSE(hub.block(u"meter"_s)->clippedChannels[0]);

    // It latches again the next time it happens rather than staying
    // cleared.
    hand.wind(16);
    publisher.publish(0.02f, 1.0f);
    (void)hub.tick();
    EXPECT_FALSE(hub.block(u"meter"_s)->clippedChannels[0]);
    EXPECT_TRUE(hub.block(u"meter"_s)->clippedChannels[1]);
}

TEST(PeakRmsPanelTest, EveryHoldSettingMeansSomethingInTime)
{
    const PeakHold hold;

    EXPECT_EQ(hold.choices(), (QStringList{u"off"_s, u"1s"_s, u"2s"_s, u"5s"_s, u"forever"_s}));
    EXPECT_EQ(hold.defaultChoice(), u"2s"_s);

    EXPECT_EQ(hold.holdMs(u"off"_s), 0);
    EXPECT_EQ(hold.holdMs(u"1s"_s), 1000);
    EXPECT_EQ(hold.holdMs(u"2s"_s), 2000);
    EXPECT_EQ(hold.holdMs(u"5s"_s), 5000);

    // **"Forever" is a line that never falls**, not a very long wait: it is
    // the decay that is switched off, so nothing has to pick a number large
    // enough to be indistinguishable from never.
    EXPECT_DOUBLE_EQ(hold.decayDbPerSecond(u"forever"_s), 0.0);
    EXPECT_GT(hold.decayDbPerSecond(u"2s"_s), 0.0);

    // A setting this build has never heard of is the default rather than
    // nothing at all.
    EXPECT_EQ(hold.holdMs(u"9s"_s), hold.holdMs(hold.defaultChoice()));
}

TEST(PeakRmsPanelTest, TheHoldWaitsForAsLongAsItWasAskedToAndThenFalls)
{
    // The criterion: hold peaks holds for the time chosen and then falls.
    // Measured through what the panel actually does — ask PeakHold what the
    // choice means and tell the hub — rather than through the hub's own
    // numbers.
    const PeakHold hold;

    for (const QString &choice : {u"1s"_s, u"2s"_s, u"5s"_s}) {
        Hand hand;
        Publisher publisher;
        MeterHub hub;
        hub.setClock(hand.clock());
        hub.setHold(hold.holdMs(choice));
        hub.setDecay(static_cast<float>(hold.decayDbPerSecond(choice)));
        hub.watch(&publisher.slot());

        publisher.publish(1.0f, 1.0f);
        (void)hub.tick();
        const float top = hub.block(u"meter"_s)->holdDb[0];

        qint64 elapsed = 0;
        qint64 fellAt = -1;
        float previous = top;
        while (elapsed < hold.holdMs(choice) + 2000) {
            hand.wind(16);
            elapsed += 16;
            publisher.publish(0.0f, 0.0f);
            (void)hub.tick();
            const float held = hub.block(u"meter"_s)->holdDb[0];
            if (fellAt < 0 && held < previous - 0.05f) {
                fellAt = elapsed;
            }
            previous = held;
        }

        ASSERT_GE(fellAt, 0) << choice.toStdString() << " never came down";
        EXPECT_NEAR(static_cast<double>(fellAt), static_cast<double>(hold.holdMs(choice)), 100.0)
            << choice.toStdString() << " waited " << fellAt << " ms";
    }

    // And "off" does not wait at all: the line follows the signal down.
    {
        Hand hand;
        Publisher publisher;
        MeterHub hub;
        hub.setClock(hand.clock());
        hub.setHold(hold.holdMs(u"off"_s));
        hub.setDecay(static_cast<float>(hold.decayDbPerSecond(u"off"_s)));
        hub.watch(&publisher.slot());

        publisher.publish(1.0f, 1.0f);
        (void)hub.tick();
        const float top = hub.block(u"meter"_s)->holdDb[0];
        hand.wind(100);
        publisher.publish(0.0f, 0.0f);
        (void)hub.tick();
        EXPECT_LT(hub.block(u"meter"_s)->holdDb[0], top) << "a hold of off waited anyway";
    }
}

TEST(PeakRmsPanelTest, TwoMetersInOneSessionEachHoldForTheirOwnTime)
{
    // `peakHold` is a parameter of the block, so a session with a
    // meter on the way in and another on the way out carries two of them.
    // One clock for the whole window meant whichever panel was touched last
    // decided for both.
    const PeakHold hold;
    Hand hand;
    TwoMeters publisher;
    MeterHub hub;
    hub.setClock(hand.clock());
    hub.watch(&publisher.slot());

    // Each told what its own panel would tell it.
    hub.setHold(u"in"_s, hold.holdMs(u"1s"_s));
    hub.setDecay(u"in"_s, static_cast<float>(hold.decayDbPerSecond(u"1s"_s)));
    hub.setHold(u"out"_s, hold.holdMs(u"5s"_s));
    hub.setDecay(u"out"_s, static_cast<float>(hold.decayDbPerSecond(u"5s"_s)));

    const FellAt fell = watchBothComeDown(hub, hand, publisher, 7000);

    ASSERT_GE(fell.first, 0) << "the meter set to a second never came down";
    ASSERT_GE(fell.second, 0) << "the meter set to five seconds never came down";
    EXPECT_NEAR(static_cast<double>(fell.first), 1000.0, 100.0) << "it waited " << fell.first << " ms";
    EXPECT_NEAR(static_cast<double>(fell.second), 5000.0, 100.0) << "it waited " << fell.second << " ms";

    // And a block nobody has spoken for still uses the window's setting,
    // which is what a session with one meter has always relied on.
    EXPECT_EQ(hub.holdMs(u"nobody"_s), MeterHub::kHoldMs);
    EXPECT_FLOAT_EQ(hub.decayDbPerSecond(u"nobody"_s), MeterHub::kDecayDbPerSecond);
    EXPECT_EQ(hub.holdMs(u"in"_s), hold.holdMs(u"1s"_s));
}

TEST(PeakRmsPanelTest, AMeterHeldUntilResetDoesNotFreezeTheOneBesideIt)
{
    // **The fall rate follows the hold.** "Forever" is spelled as a fall
    // rate of zero rather than as a wait long enough to pass for never, so
    // a fall rate kept for the whole window would let one meter set to
    // "Until reset" hold up every other meter's line with it — the same
    // mix-up as the hold, moved one setting along.
    const PeakHold hold;
    Hand hand;
    TwoMeters publisher;
    MeterHub hub;
    hub.setClock(hand.clock());
    hub.watch(&publisher.slot());

    hub.setHold(u"in"_s, hold.holdMs(u"forever"_s));
    hub.setDecay(u"in"_s, static_cast<float>(hold.decayDbPerSecond(u"forever"_s)));
    hub.setHold(u"out"_s, hold.holdMs(u"1s"_s));
    hub.setDecay(u"out"_s, static_cast<float>(hold.decayDbPerSecond(u"1s"_s)));

    publisher.publish(1.0f);
    (void)hub.tick();
    const float top = hub.block(u"in"_s)->holdDb[0];

    const FellAt fell = watchBothComeDown(hub, hand, publisher, 4000);

    EXPECT_EQ(fell.first, -1) << "the line held until reset came down after " << fell.first << " ms";
    EXPECT_FLOAT_EQ(hub.block(u"in"_s)->holdDb[0], top) << "the line held until reset moved";
    ASSERT_GE(fell.second, 0) << "the meter beside it was frozen by its neighbour";
    EXPECT_NEAR(static_cast<double>(fell.second), 1000.0, 100.0) << "it waited " << fell.second << " ms";
}

TEST(PeakRmsPanelTest, ThirtyTilesCostLessThanAMillisecondOfAFrame)
{
    // The criterion: thirty Peak/RMS tiles on the canvas stay within the
    // frame budget. A tile-sized meter with a scale beside it and a dot a
    // side is what this measures — thirty of them, sixty times a second,
    // beside everything else a canvas has to draw.
    //
    // **Only the optimised build has anything to say about this.** A debug
    // build is several times slower, and a budget measured against it would
    // be a budget for the compiler rather than for the code.
    constexpr int kTiles = 30;
    constexpr int kFrames = 120;

    std::vector<std::unique_ptr<Measurable>> tiles;
    std::vector<QSGNode *> nodes(kTiles, nullptr);
    for (int index = 0; index < kTiles; ++index) {
        auto tile = std::make_unique<Measurable>();
        tile->setWidth(40);
        tile->setHeight(128);
        tile->setChannelCount(2);
        tile->setActive(true);
        tiles.push_back(std::move(tile));
    }

    QElapsedTimer timer;
    timer.start();
    for (int frame = 0; frame < kFrames; ++frame) {
        const qreal moving = -60.0 + (frame % 60);
        // A clip dot a side, lit and put out as the frames go by, so that
        // what is measured is the meter the panel and the face draw.
        const QList<bool> clips{frame % 30 < 15, frame % 20 < 10};
        for (int index = 0; index < kTiles; ++index) {
            tiles[index]->setRmsDb({moving - 3.0, moving - 5.0});
            tiles[index]->setPeakDb({moving, moving - 2.0});
            tiles[index]->setHoldDb({moving + 2.0, moving});
            tiles[index]->setClippedChannels(clips);
            nodes[index] = tiles[index]->paint(nodes[index]);
        }
    }
    const double perFrameMs = static_cast<double>(timer.nsecsElapsed()) / 1.0e6 / kFrames;
    for (QSGNode *node : nodes) {
        delete node;
    }

    std::cout << kTiles << " Peak/RMS tiles cost " << perFrameMs << " ms a frame\n";
#ifdef NDEBUG
    EXPECT_LT(perFrameMs, 1.0) << kTiles << " tiles cost " << perFrameMs << " ms a frame";
#else
    GTEST_SKIP() << "a debug build is several times slower; the figure above is " << perFrameMs << " ms a frame";
#endif
}
