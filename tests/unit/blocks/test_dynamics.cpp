// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Dynamics block: the controls on the panel wired to the curve in
// engine/dsp, and the three numbers the curve editor's live marker sits on.
// What the curve itself does is measured in tests/unit/dsp/test_dynamics.cpp;
// what is checked here is that the block says the same thing.

#include "allocationtrap.h"
#include "analysis.h"
#include "blocks/dynamics.h"
#include "blocksizeinvariance.h"
#include "dsp/decibel.h"
#include "dsp/dynamics.h"
#include "offlinerunner.h"
#include "rt/meter.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <memory>

using namespace rb;
using namespace rb::blocks;
using namespace rb::dsp;
using namespace rb::test;

namespace
{

[[nodiscard]] MultiSignal stereoSine(std::size_t frames, double hz, float amplitude)
{
    MultiSignal signal;
    signal.push_back(sine(frames, hz, kTestSampleRate, amplitude));
    signal.push_back(sine(frames, hz, kTestSampleRate, amplitude));
    return signal;
}

/// The part of a run after the attack and the release have arrived, so that
/// a level is a level and not a glide.
[[nodiscard]] Signal settled(const Signal &signal, std::size_t from = 24000)
{
    return Signal(signal.begin() + static_cast<Signal::difference_type>(from), signal.end());
}

[[nodiscard]] MultiSignal through(Dynamics &block, const MultiSignal &input, std::uint32_t cycle = 256)
{
    BlockRunner runner(block, 2, 2);
    EXPECT_TRUE(runner.prepare().has_value());
    return runner.run(input, CycleShape::fixed(cycle));
}

} // namespace

TEST(DynamicsBlockTest, TheControlsAreTheCurve)
{
    // The criterion at the block's own level: what a steady tone comes out
    // at is what dsp::outputDb() says for the settings the controls hold.
    // One definition, read by the editor and by the sound alike.
    Dynamics block;
    ASSERT_TRUE(block.setParam("threshold", ParamValue::ofFloat(-24.0f)).has_value());
    ASSERT_TRUE(block.setParam("ratio", ParamValue::ofFloat(4.0f)).has_value());
    ASSERT_TRUE(block.setParam("knee", ParamValue::ofFloat(6.0f)).has_value());
    ASSERT_TRUE(block.setParam("makeup", ParamValue::ofFloat(3.0f)).has_value());
    ASSERT_TRUE(block.setParam("attack", ParamValue::ofFloat(1.0f)).has_value());
    ASSERT_TRUE(block.setParam("release", ParamValue::ofFloat(50.0f)).has_value());

    const DynamicsSettings settings = block.settings();
    EXPECT_FLOAT_EQ(settings.thresholdDb, -24.0f);
    EXPECT_FLOAT_EQ(settings.ratio, 4.0f);
    EXPECT_FLOAT_EQ(settings.kneeDb, 6.0f);
    EXPECT_FLOAT_EQ(settings.makeupDb, 3.0f);

    for (const float inputDb : {-40.0f, -30.0f, -24.0f, -18.0f, -12.0f, -6.0f}) {
        Dynamics tone;
        ASSERT_TRUE(tone.loadState(block.saveState()).has_value());
        const MultiSignal input = stereoSine(32000, 1000.0, dbToGain(inputDb));
        const MultiSignal out = through(tone, input);
        const double measured = peakDb(settled(out[0]));
        EXPECT_NEAR(measured, outputDb(inputDb, settings), 0.2) << "at " << inputDb << " dBFS in";
    }
}

TEST(DynamicsBlockTest, TheRatioClicksPastTheEndOfItsSliderToInfinity)
{
    // There is no place on a slider that runs to 20:1 for a ratio of
    // infinity, so it is the click past the end rather than a number on it.
    Dynamics block;
    ASSERT_TRUE(block.setParam("threshold", ParamValue::ofFloat(-20.0f)).has_value());
    ASSERT_TRUE(block.setParam("knee", ParamValue::ofFloat(0.0f)).has_value());
    ASSERT_TRUE(block.setParam("ratio", ParamValue::ofFloat(2.0f)).has_value());
    EXPECT_FLOAT_EQ(block.settings().ratio, 2.0f);

    ASSERT_TRUE(block.setParam("infiniteRatio", ParamValue::ofBool(true)).has_value());
    EXPECT_FLOAT_EQ(block.settings().ratio, kInfiniteRatio);
    // The number under it is kept, so turning infinity off again gives back
    // the ratio that was there rather than a default.
    EXPECT_FLOAT_EQ(*block.param("ratio").asFloat(), 2.0f);

    // Everything above the threshold comes out at the threshold: 6 dB over
    // and 20 dB over both land on −20 dBFS.
    for (const float inputDb : {-14.0f, 0.0f}) {
        Dynamics tone;
        ASSERT_TRUE(tone.loadState(block.saveState()).has_value());
        const MultiSignal out = through(tone, stereoSine(32000, 1000.0, dbToGain(inputDb)));
        EXPECT_NEAR(peakDb(settled(out[0])), -20.0, 0.2) << "at " << inputDb << " dBFS in";
    }
}

TEST(DynamicsBlockTest, TheTimingControlsReachTheDetector)
{
    Dynamics block;
    ASSERT_TRUE(block.setParam("attack", ParamValue::ofFloat(12.0f)).has_value());
    ASSERT_TRUE(block.setParam("release", ParamValue::ofFloat(400.0f)).has_value());
    ASSERT_TRUE(block.setParam("expanderHold", ParamValue::ofFloat(250.0f)).has_value());
    ASSERT_TRUE(block.setParam("detector", ParamValue::ofEnum("rms")).has_value());

    const DynamicsTiming timing = block.timing();
    EXPECT_FLOAT_EQ(timing.attackMs, 12.0f);
    EXPECT_FLOAT_EQ(timing.releaseMs, 400.0f);
    EXPECT_FLOAT_EQ(timing.holdMs, 250.0f);
    EXPECT_EQ(timing.detector, DynamicsDetector::Rms);

    ASSERT_TRUE(block.setParam("detector", ParamValue::ofEnum("peak")).has_value());
    EXPECT_EQ(block.timing().detector, DynamicsDetector::Peak);
}

TEST(DynamicsBlockTest, BothSidesGetTheSameGain)
{
    // A stereo-linked processor cannot let the image wander: one side loud
    // and the other quiet still gives one gain, so the quiet side comes down
    // by exactly as much as the loud one.
    Dynamics block;
    ASSERT_TRUE(block.setParam("threshold", ParamValue::ofFloat(-30.0f)).has_value());
    ASSERT_TRUE(block.setParam("ratio", ParamValue::ofFloat(8.0f)).has_value());
    ASSERT_TRUE(block.setParam("knee", ParamValue::ofFloat(0.0f)).has_value());

    MultiSignal input;
    input.push_back(sine(32000, 1000.0, kTestSampleRate, dbToGain(-6.0f)));
    input.push_back(sine(32000, 1000.0, kTestSampleRate, dbToGain(-26.0f)));
    const MultiSignal out = through(block, input);

    const double leftChange = peakDb(settled(out[0])) - peakDb(settled(input[0]));
    const double rightChange = peakDb(settled(out[1])) - peakDb(settled(input[1]));
    EXPECT_NEAR(leftChange, rightChange, 0.05) << "the sides were moved by different amounts";
}

TEST(DynamicsBlockTest, ItPublishesWhatTheLiveMarkerNeeds)
{
    Dynamics block;
    ASSERT_TRUE(block.setParam("threshold", ParamValue::ofFloat(-30.0f)).has_value());
    ASSERT_TRUE(block.setParam("ratio", ParamValue::ofFloat(4.0f)).has_value());
    ASSERT_TRUE(block.setParam("knee", ParamValue::ofFloat(0.0f)).has_value());

    rt::BlockMeter meter;
    BlockRunner runner(block, 2, 2);
    runner.setMeter(&meter);
    ASSERT_TRUE(runner.prepare().has_value());
    const MultiSignal input = stereoSine(32000, 1000.0, dbToGain(-10.0f));
    (void)runner.run(input, CycleShape::fixed(256));

    const float inputLevel = meter.value(Dynamics::kInputLevelValue);
    const float outputLevel = meter.value(Dynamics::kOutputLevelValue);
    const float reduction = meter.value(Dynamics::kGainReductionValue);

    EXPECT_NEAR(inputLevel, -10.0f, 0.2f);
    EXPECT_NEAR(outputLevel, outputDb(-10.0f, block.settings()), 0.3f);
    // The reduction is the distance between them, and it is never positive.
    EXPECT_LE(reduction, 0.0f);
    EXPECT_NEAR(outputLevel - inputLevel, reduction, 0.2f);
    EXPECT_FLOAT_EQ(inputLevel, block.inputLevelDb());
    EXPECT_FLOAT_EQ(outputLevel, block.outputLevelDb());
    EXPECT_FLOAT_EQ(reduction, block.gainReduction());
}

TEST(DynamicsBlockTest, ChangingTheMakeupDoesNotStepTheSignal)
{
    // The makeup is added after the attack and the release have done their
    // work, so it is the one control with nothing in front of it to smooth a
    // jump. It has a glide of its own.
    Dynamics block;
    ASSERT_TRUE(block.setParam("threshold", ParamValue::ofFloat(0.0f)).has_value());
    ASSERT_TRUE(block.setParam("knee", ParamValue::ofFloat(0.0f)).has_value());

    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    MultiSignal input;
    input.push_back(Signal(256, 0.05f));
    input.push_back(Signal(256, 0.05f));

    // **One signal across the cycles, not one per cycle**. A setting
    // changed between two cycles steps, if it steps at all, at the join,
    // which a step measured inside each cycle on its own never sees.
    Signal whole;
    for (int cycle = 0; cycle < 40; ++cycle) {
        if (cycle == 8) {
            ASSERT_TRUE(block.setParam("makeup", ParamValue::ofFloat(20.0f)).has_value());
        }
        const MultiSignal out = runner.run(input, CycleShape::fixed(256));
        if (cycle > 0) {
            whole.insert(whole.end(), out[0].begin(), out[0].end());
        }
    }
    const float worst = maxSampleStep(whole);
    EXPECT_LT(worst, 0.01f) << "the makeup stepped the signal by " << worst;
}

TEST(DynamicsBlockTest, ItSoundsTheSameWhateverTheQuantum)
{
    MultiSignal input;
    input.push_back(sine(16384, 997.0, kTestSampleRate, 0.5f));
    input.push_back(sine(16384, 1500.0, kTestSampleRate, 0.2f));

    InvarianceOptions options;
    options.inputChannels = 2;
    options.outputChannels = 2;
    const InvarianceReport report = checkBlockSizeInvariance(
        [] {
            auto block = std::make_unique<Dynamics>();
            // Working hard, so that anything kept per cycle rather than per
            // sample would show up rather than sit under the threshold.
            EXPECT_TRUE(block->setParam("threshold", ParamValue::ofFloat(-30.0f)).has_value());
            EXPECT_TRUE(block->setParam("ratio", ParamValue::ofFloat(10.0f)).has_value());
            EXPECT_TRUE(block->setParam("attack", ParamValue::ofFloat(0.5f)).has_value());
            EXPECT_TRUE(block->setParam("release", ParamValue::ofFloat(80.0f)).has_value());
            return std::unique_ptr<Block>(std::move(block));
        },
        input,
        options);
    EXPECT_TRUE(report.passed) << report.summary;
}

TEST(DynamicsBlockTest, WhatWasSetIsWhatComesBack)
{
    Dynamics block;
    ASSERT_TRUE(block.setParam("threshold", ParamValue::ofFloat(-33.0f)).has_value());
    ASSERT_TRUE(block.setParam("ratio", ParamValue::ofFloat(6.5f)).has_value());
    ASSERT_TRUE(block.setParam("infiniteRatio", ParamValue::ofBool(true)).has_value());
    ASSERT_TRUE(block.setParam("knee", ParamValue::ofFloat(18.0f)).has_value());
    ASSERT_TRUE(block.setParam("expanderThreshold", ParamValue::ofFloat(-55.0f)).has_value());
    ASSERT_TRUE(block.setParam("expanderRatio", ParamValue::ofFloat(4.0f)).has_value());
    ASSERT_TRUE(block.setParam("expanderRange", ParamValue::ofFloat(24.0f)).has_value());
    ASSERT_TRUE(block.setParam("expanderHold", ParamValue::ofFloat(120.0f)).has_value());
    ASSERT_TRUE(block.setParam("attack", ParamValue::ofFloat(5.0f)).has_value());
    ASSERT_TRUE(block.setParam("release", ParamValue::ofFloat(600.0f)).has_value());
    ASSERT_TRUE(block.setParam("makeup", ParamValue::ofFloat(-7.5f)).has_value());
    ASSERT_TRUE(block.setParam("detector", ParamValue::ofEnum("rms")).has_value());

    Dynamics opened;
    ASSERT_TRUE(opened.loadState(block.saveState()).has_value());
    EXPECT_FLOAT_EQ(*opened.param("threshold").asFloat(), -33.0f);
    EXPECT_FLOAT_EQ(*opened.param("ratio").asFloat(), 6.5f);
    EXPECT_TRUE(*opened.param("infiniteRatio").asBool());
    EXPECT_FLOAT_EQ(*opened.param("knee").asFloat(), 18.0f);
    EXPECT_FLOAT_EQ(*opened.param("expanderThreshold").asFloat(), -55.0f);
    EXPECT_FLOAT_EQ(*opened.param("expanderRatio").asFloat(), 4.0f);
    EXPECT_FLOAT_EQ(*opened.param("expanderRange").asFloat(), 24.0f);
    EXPECT_FLOAT_EQ(*opened.param("expanderHold").asFloat(), 120.0f);
    EXPECT_FLOAT_EQ(*opened.param("attack").asFloat(), 5.0f);
    EXPECT_FLOAT_EQ(*opened.param("release").asFloat(), 600.0f);
    EXPECT_FLOAT_EQ(*opened.param("makeup").asFloat(), -7.5f);
    EXPECT_EQ(opened.param("detector").text(), "rms");

    // The ends of the ranges are the ends: a value past one is refused
    // rather than taken quietly.
    EXPECT_FALSE(block.setParam("threshold", ParamValue::ofFloat(6.0f)).has_value());
    EXPECT_FALSE(block.setParam("attack", ParamValue::ofFloat(0.0f)).has_value());
    EXPECT_FALSE(block.setParam("release", ParamValue::ofFloat(4000.0f)).has_value());
}

TEST(DynamicsBlockTest, TheDefaultsAreTheFactoryPreset)
{
    // The "Default" preset is what the block is when it is made, so there is
    // nothing to load to get it.
    const Dynamics block;
    const DynamicsSettings settings = block.settings();
    const DynamicsTiming timing = block.timing();

    EXPECT_FLOAT_EQ(settings.thresholdDb, -20.0f);
    EXPECT_FLOAT_EQ(settings.ratio, 20.0f);
    EXPECT_FLOAT_EQ(settings.kneeDb, 12.0f);
    EXPECT_FLOAT_EQ(settings.makeupDb, 0.0f);
    EXPECT_FLOAT_EQ(timing.attackMs, 1.0f);
    EXPECT_FLOAT_EQ(timing.releaseMs, 50.0f);
    EXPECT_EQ(timing.detector, DynamicsDetector::Peak);

    // The expander is off: a ratio of 1:1 and a range of nothing leave the
    // quiet end of the curve exactly as it arrived.
    EXPECT_FLOAT_EQ(settings.expanderRatio, 1.0f);
    EXPECT_FLOAT_EQ(settings.expanderRangeDb, 0.0f);
    // Below where the wide knee starts, which is half of it under the
    // threshold: −26 dBFS. Anything above that is the compressor beginning
    // gently, which is the point of a wide knee.
    for (const float inputDb : {-80.0f, -60.0f, -40.0f, -30.0f}) {
        EXPECT_FLOAT_EQ(gainReductionDb(inputDb, settings), 0.0f) << "at " << inputDb << " dBFS in";
    }

    // Nothing in the way: feed-forward, and no lookahead in M1.
    EXPECT_EQ(block.info().latencySamples, 0U);
}

TEST(DynamicsBlockTest, ARunTouchesTheAllocatorNotAtAll)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    Dynamics block;
    ASSERT_TRUE(block.setParam("threshold", ParamValue::ofFloat(-30.0f)).has_value());
    ASSERT_TRUE(block.setParam("expanderThreshold", ParamValue::ofFloat(-45.0f)).has_value());
    ASSERT_TRUE(block.setParam("expanderRatio", ParamValue::ofFloat(4.0f)).has_value());
    ASSERT_TRUE(block.setParam("expanderRange", ParamValue::ofFloat(30.0f)).has_value());
    ASSERT_TRUE(block.setParam("expanderHold", ParamValue::ofFloat(100.0f)).has_value());
    ASSERT_TRUE(block.setParam("detector", ParamValue::ofEnum("rms")).has_value());

    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    MultiSignal input;
    input.push_back(sine(9600, 220.0, kTestSampleRate, 1.0f));
    input.push_back(sine(9600, 220.0, kTestSampleRate, 1.0f));

    std::size_t allocations = 1;
    const MultiSignal out = runner.runWatchingForAllocations(input, CycleShape::random(1, 512, 3), allocations);
    EXPECT_EQ(allocations, 0U);
    EXPECT_EQ(out[0].size(), input[0].size());
}
