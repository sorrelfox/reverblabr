// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The shared level meter: that its timing is in wall-clock and not in
// frames, that a clip light can be put out, that a stopped session looks
// switched off rather than quiet, and that fifty of them cost the frame
// almost nothing.

#include "dsp/decibel.h"
#include "meterhub.h"
#include "meteritem.h"
#include "meterscale.h"

#include <QElapsedTimer>
#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>
#include <QSGNode>
#include <gtest/gtest.h>

#include <cstring>
#include <memory>

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
    Publisher()
    {
        core::BlockTelemetry &block = m_snapshot.blocks[0];
        std::strncpy(block.id, "b1", core::kMaxTelemetryIdBytes - 1);
        block.channelCount = 2;
        m_snapshot.blockCount = 1;
    }

    void publishPeak(float amplitude)
    {
        core::BlockTelemetry &block = m_snapshot.blocks[0];
        for (std::uint32_t channel = 0; channel < 2; ++channel) {
            block.peak[channel] = amplitude;
            block.rms[channel] = amplitude * 0.7f;
        }
        block.active = amplitude > 0.0f;
        block.clipped = amplitude >= 1.0f;
        ++m_snapshot.frame;
        m_snapshot.running = true;
        m_slot.store(m_snapshot);
    }

    void stop()
    {
        ++m_snapshot.frame;
        m_snapshot.running = false;
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

/// How long the hold line stays at the top, and how fast it falls, read at
/// a given frame rate. **The point of the test**: neither answer may depend
/// on the rate.
struct Timing {
    qint64 heldMs = 0;
    float decayDbPerSecond = 0.0f;
};

[[nodiscard]] Timing measureAt(int frameMs, qint64 hold, float decay)
{
    Hand hand;
    Publisher publisher;
    MeterHub hub;
    hub.setClock(hand.clock());
    hub.setHold(hold);
    hub.setDecay(decay);
    hub.watch(&publisher.slot());

    // One loud frame, and then nothing but quiet ones.
    publisher.publishPeak(1.0f);
    (void)hub.tick();
    const float top = hub.block(u"b1"_s)->holdDb[0];

    Timing timing;
    qint64 elapsed = 0;
    float previous = top;
    qint64 fellAt = -1;
    while (elapsed < 4000) {
        hand.wind(frameMs);
        elapsed += frameMs;
        publisher.publishPeak(0.0f);
        (void)hub.tick();
        const float held = hub.block(u"b1"_s)->holdDb[0];
        if (fellAt < 0 && held < previous - 0.05f) {
            fellAt = elapsed;
            timing.heldMs = elapsed;
        }
        if (fellAt >= 0 && elapsed >= fellAt + 1000 && timing.decayDbPerSecond == 0.0f) {
            // A second after it started falling, how far has it come.
            timing.decayDbPerSecond = (top - held) / (static_cast<float>(elapsed - fellAt) / 1000.0f);
        }
        previous = held;
    }
    return timing;
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

TEST(LevelMeterTest, TheHoldAndTheDecayAreTheSameAtSixtyHertzAndAtOneHundredAndFortyFour)
{
    // The criterion: within ±10 % at both. **Wall-clock time, not frames**
    // — a meter whose peak line fell twice as fast on a faster screen would
    // be a meter that means something different on every machine.
    constexpr qint64 kHold = 2000;
    constexpr float kDecay = 20.0f;

    const Timing sixty = measureAt(17, kHold, kDecay); // 60 Hz
    const Timing fast = measureAt(7, kHold, kDecay); // 144 Hz

    for (const auto &[name, timing] : {std::pair{"60 Hz", sixty}, std::pair{"144 Hz", fast}}) {
        EXPECT_NEAR(static_cast<double>(timing.heldMs), static_cast<double>(kHold), kHold * 0.1)
            << name << " held for " << timing.heldMs << " ms";
        EXPECT_NEAR(timing.decayDbPerSecond, kDecay, kDecay * 0.1f)
            << name << " fell at " << timing.decayDbPerSecond << " dB a second";
    }
}

TEST(LevelMeterTest, AHoldOfNoneAndADecayOfNoneAreBothThingsSomebodyCanAskFor)
{
    // The Peak/RMS block offers "off" and "forever", and both have to mean
    // something here rather than being rounded to the default.
    Hand hand;
    Publisher publisher;
    MeterHub hub;
    hub.setClock(hand.clock());
    hub.watch(&publisher.slot());

    hub.setDecay(0.0f); // forever
    publisher.publishPeak(1.0f);
    (void)hub.tick();
    const float top = hub.block(u"b1"_s)->holdDb[0];

    for (int frame = 0; frame < 300; ++frame) {
        hand.wind(17);
        publisher.publishPeak(0.0f);
        (void)hub.tick();
    }
    EXPECT_FLOAT_EQ(hub.block(u"b1"_s)->holdDb[0], top) << "a hold of forever fell anyway";
}

TEST(LevelMeterTest, AClipLightCanBePutOut)
{
    // **A latch nobody can clear is a light that is on for the rest of the
    // session** and stops meaning anything.
    Hand hand;
    Publisher publisher;
    MeterHub hub;
    hub.setClock(hand.clock());
    hub.watch(&publisher.slot());

    publisher.publishPeak(1.0f);
    (void)hub.tick();
    ASSERT_TRUE(hub.block(u"b1"_s)->clipped);

    hub.clearClip(u"b1"_s);
    EXPECT_FALSE(hub.block(u"b1"_s)->clipped);

    // And it latches again the next time it happens, rather than staying
    // cleared.
    hand.wind(17);
    publisher.publishPeak(1.0f);
    (void)hub.tick();
    EXPECT_TRUE(hub.block(u"b1"_s)->clipped);

    // Clearing a block nobody has heard of is not an error.
    hub.clearClip(u"nosuch"_s);
    EXPECT_TRUE(hub.block(u"b1"_s)->clipped);
}

TEST(LevelMeterTest, TheRmsBarIsDrawnOverThePeakAndNotUnderIt)
{
    // The peak bar was drawn after the RMS bar, and being the taller of
    // the two it covered it, so every meter showed its peak alone.
    Measurable meter;
    meter.setSize(QSizeF(20.0, 100.0));
    meter.setChannelCount(1);
    meter.setActive(true);
    meter.setRmsDb({-20.0});
    meter.setPeakDb({-6.0});
    meter.setRmsColor(Qt::red);
    meter.setPeakColor(Qt::green);

    QSGNode *root = meter.paint(nullptr);
    ASSERT_NE(root, nullptr);
    int rmsAt = -1;
    int peakAt = -1;
    for (int index = 0; index < root->childCount(); ++index) {
        const auto *layer = static_cast<QSGGeometryNode *>(root->childAtIndex(index));
        const auto *material = static_cast<const QSGFlatColorMaterial *>(layer->material());
        if (material->color() == QColor(Qt::red)) {
            rmsAt = index;
        } else if (material->color() == QColor(Qt::green)) {
            peakAt = index;
        }
    }
    ASSERT_GE(rmsAt, 0);
    ASSERT_GE(peakAt, 0);
    // Children are drawn in order, so a later child is drawn on top.
    EXPECT_GT(rmsAt, peakAt) << "the peak bar is drawn over the RMS bar and hides it";
    delete root;
}

TEST(LevelMeterTest, ABarTurnsHotAboveItsLevelAndNowhereElse)
{
    // A meter can be told where it turns red. What is red is the part
    // of the bar above that level, so a bar that stays below it has none.
    const auto hotVertices = [](qreal peakDb) {
        Measurable meter;
        meter.setSize(QSizeF(20.0, 100.0));
        meter.setChannelCount(1);
        meter.setActive(true);
        meter.setRmsDb({-20.0});
        meter.setPeakDb({peakDb});
        meter.setHotDb(-6.0);
        meter.setHotColor(Qt::blue);
        QSGNode *root = meter.paint(nullptr);
        int vertices = -1;
        for (int index = 0; root != nullptr && index < root->childCount(); ++index) {
            const auto *layer = static_cast<QSGGeometryNode *>(root->childAtIndex(index));
            if (static_cast<const QSGFlatColorMaterial *>(layer->material())->color() == QColor(Qt::blue)) {
                vertices = layer->geometry()->vertexCount();
            }
        }
        delete root;
        return vertices;
    };

    EXPECT_GT(hotVertices(-3.0), 0) << "a peak above the hot level drew nothing hot";
    EXPECT_EQ(hotVertices(-10.0), 0) << "a peak below the hot level was drawn hot";
}

TEST(LevelMeterTest, AStoppedSessionIsIdleAndNotQuiet)
{
    // **Idle is not silence.** A meter at the bottom says "this is playing
    // nothing"; a meter that is switched off says "nothing is running", and
    // a person glancing at the window has to be able to tell which.
    Hand hand;
    Publisher publisher;
    MeterHub hub;
    hub.setClock(hand.clock());
    hub.watch(&publisher.slot());

    publisher.publishPeak(0.5f);
    (void)hub.tick();
    EXPECT_TRUE(hub.isRunning());
    EXPECT_TRUE(hub.block(u"b1"_s)->active);

    hand.wind(17);
    publisher.stop();
    (void)hub.tick();
    EXPECT_FALSE(hub.isRunning()) << "a stopped session still said it was running";
}

TEST(LevelMeterTest, FiftyMetersCostLessThanAMillisecondOfAFrame)
{
    // The criterion, and the spike's budget. A meter changes sixty times a
    // second and there may be fifty of them; if drawing them is not nearly
    // free, nothing else on the canvas can be.
    //
    // **Only the optimised build has anything to say about this.** A debug
    // build is several times slower, and a budget measured against it would
    // be a budget for the compiler rather than for the code.
    constexpr int kMeters = 50;
    constexpr int kFrames = 120;

    std::vector<std::unique_ptr<Measurable>> meters;
    std::vector<QSGNode *> nodes(kMeters, nullptr);
    for (int index = 0; index < kMeters; ++index) {
        auto meter = std::make_unique<Measurable>();
        meter->setWidth(24);
        meter->setHeight(96);
        meter->setChannelCount(2);
        meter->setActive(true);
        meters.push_back(std::move(meter));
    }

    QElapsedTimer timer;
    timer.start();
    for (int frame = 0; frame < kFrames; ++frame) {
        const qreal moving = -60.0 + (frame % 60);
        for (int index = 0; index < kMeters; ++index) {
            meters[index]->setRmsDb({moving, moving - 3.0});
            meters[index]->setPeakDb({moving + 2.0, moving - 1.0});
            meters[index]->setHoldDb({moving + 4.0, moving + 1.0});
            nodes[index] = meters[index]->paint(nodes[index]);
        }
    }
    const double perFrameMs = static_cast<double>(timer.nsecsElapsed()) / 1.0e6 / kFrames;
    for (QSGNode *node : nodes) {
        delete node;
    }

    std::cout << kMeters << " meters cost " << perFrameMs << " ms a frame\n";
#ifdef NDEBUG
    EXPECT_LT(perFrameMs, 1.0) << kMeters << " meters cost " << perFrameMs << " ms a frame";
#else
    GTEST_SKIP() << "a debug build is several times slower; the figure above is " << perFrameMs << " ms a frame";
#endif
}
