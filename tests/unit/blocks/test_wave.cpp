// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Wave block: a test signal at a frequency, a level and a side,
// each measured on what comes out rather than read back from a setting.

#include "allocationtrap.h"
#include "analysis.h"
#include "blocks/builtinblocks.h"
#include "blocks/wave.h"
#include "blocksizeinvariance.h"
#include "dsp/blockregistry.h"
#include "dsp/decibel.h"
#include "offlinerunner.h"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <numbers>
#include <string>

using namespace rb;
using namespace rb::blocks;
using namespace rb::dsp;
using namespace rb::test;

namespace
{

constexpr std::size_t kFrames = 48000;

/// A second of what the block makes, with nothing going in: the block takes
/// no input, and the runner is given one silent channel only to say how long.
[[nodiscard]] MultiSignal generate(Wave &block, std::size_t frames = kFrames, std::uint32_t cycle = 256)
{
    BlockRunner runner(block, 0, 2);
    EXPECT_TRUE(runner.prepare().has_value());
    MultiSignal nothing;
    nothing.push_back(Signal(frames, 0.0f));
    return runner.run(nothing, CycleShape::fixed(cycle));
}

[[nodiscard]] std::unique_ptr<Wave>
wave(std::string_view shape, float hz, float levelDb, std::string_view side = "both")
{
    auto block = std::make_unique<Wave>();
    EXPECT_TRUE(block->setParam("waveform", ParamValue::ofEnum(std::string{shape})).has_value());
    EXPECT_TRUE(block->setParam("frequency", ParamValue::ofFloat(hz)).has_value());
    EXPECT_TRUE(block->setParam("level", ParamValue::ofFloat(levelDb)).has_value());
    EXPECT_TRUE(block->setParam("channels", ParamValue::ofEnum(std::string{side})).has_value());
    return block;
}

/// The root mean square of how much each sample differs from the one before,
/// against the signal's own: about √2 for white noise, far less for anything
/// whose energy is at the bottom of the spectrum.
[[nodiscard]] float roughness(const Signal &signal)
{
    Signal steps(signal.size() - 1);
    for (std::size_t index = 1; index < signal.size(); ++index) {
        steps[index - 1] = signal[index] - signal[index - 1];
    }
    return rms(steps) / rms(signal);
}

} // namespace

TEST(WaveTest, ItIsInTheLibrarysOutputsAndTakesNothingIn)
{
    // **Listed under Outputs**, next to the devices it is used to test, and
    // wired like any block: no inputs, an output connector, and nothing the
    // audio system fills.
    const Wave block;
    EXPECT_EQ(block.info().typeId, "output.wave");
    EXPECT_EQ(block.info().category, BlockCategory::Outputs);
    EXPECT_EQ(block.info().inputs.maxCount, 0U);
    EXPECT_EQ(block.info().outputs.maxCount, 2U);
    EXPECT_FALSE(block.info().inputsFromAudioSystem);
    EXPECT_FALSE(block.info().outputsToAudioSystem);

    BlockRegistry registry;
    ASSERT_TRUE(registerBuiltinBlocks(registry));
    EXPECT_TRUE(registry.contains("output.wave")) << "Wave is not a block a session can add";
}

TEST(WaveTest, ItStartsAsAQuietSineAtAThousandHertzOnBothSides)
{
    // A test tone at full scale is a speaker or an ear put at risk by a slip.
    const Wave block;
    EXPECT_EQ(block.param("waveform").text(), "sine");
    EXPECT_FLOAT_EQ(*block.param("frequency").asFloat(), 1000.0f);
    EXPECT_FLOAT_EQ(*block.param("level").asFloat(), -18.0f);
    EXPECT_EQ(block.param("channels").text(), "both");
}

TEST(WaveTest, ASineIsAtTheFrequencyAndTheLevelAskedFor)
{
    for (const auto &[hz, levelDb] :
         std::vector<std::pair<float, float>>{{50.0f, -6.0f}, {1000.0f, -18.0f}, {12000.0f, 0.0f}, {440.0f, -60.0f}}) {
        const auto block = wave("sine", hz, levelDb);
        const MultiSignal out = generate(*block);
        EXPECT_NEAR(peakDb(out[0]), levelDb, 0.05) << hz << " Hz at " << levelDb << " dBFS";
        EXPECT_NEAR(amplitudeAt(out[0], hz), dbToGain(levelDb), dbToGain(levelDb) * 0.01)
            << "the tone is not at " << hz << " Hz";
        EXPECT_LT(totalHarmonicDistortion(out[0], hz), 0.001f) << "a sine at " << hz << " Hz is not a sine";
    }
}

TEST(WaveTest, EachShapeIsAtItsFrequencyWithTheFundamentalItShouldHave)
{
    // A square's fundamental is 4/π of its peak, a saw's 2/π and a
    // triangle's 8/π²: what a waveform is, measured.
    const float level = dbToGain(-12.0f);
    const std::vector<std::pair<std::string_view, double>> shapes{
        {"square", 4.0 / std::numbers::pi},
        {"saw", 2.0 / std::numbers::pi},
        {"triangle", 8.0 / (std::numbers::pi * std::numbers::pi)},
    };
    for (const auto &[shape, fundamental] : shapes) {
        const auto block = wave(shape, 440.0f, -12.0f);
        const MultiSignal out = generate(*block);
        EXPECT_NEAR(amplitudeAt(out[0], 440.0), fundamental * level, fundamental * level * 0.02) << shape;
        EXPECT_LT(peakDb(out[0]), -12.0f + 1.0f) << shape << " went well past its level";
    }
}

TEST(WaveTest, AHighToneDoesNotFoldBackAsASecondTone)
{
    // At 7919 Hz a square's fifth harmonic, 39595 Hz, would come back at
    // 8405 Hz, and a triangle's too; a saw's fourth, 31676 Hz, at 16324 Hz.
    // Band-limited, what folds back is a small fraction of the fundamental.
    const struct {
        std::string_view shape;
        double aliasHz;
        double atMostDb;
    } cases[] = {{"square", 8405.0, -30.0}, {"triangle", 8405.0, -40.0}, {"saw", 16324.0, -20.0}};
    for (const auto &check : cases) {
        const auto block = wave(check.shape, 7919.0f, -6.0f);
        const MultiSignal out = generate(*block);
        const double relative = 20.0 * std::log10(amplitudeAt(out[0], check.aliasHz) / amplitudeAt(out[0], 7919.0));
        EXPECT_LT(relative, check.atMostDb) << check.shape << " folded back at " << check.aliasHz << " Hz";
    }
}

TEST(WaveTest, NoiseIsNoiseAtItsLevelAndPinkIsDarkerThanWhite)
{
    const auto white = wave("white", 1000.0f, -6.0f);
    const MultiSignal whiteOut = generate(*white);
    // Uniform noise peaking at the level has a root mean square 4.77 dB
    // under it.
    EXPECT_LE(peakDb(whiteOut[0]), -6.0f + 0.01f);
    EXPECT_NEAR(rmsDb(whiteOut[0]), -6.0 - 4.77, 0.3);
    EXPECT_NEAR(roughness(whiteOut[0]), std::numbers::sqrt2, 0.05) << "white noise is not white";

    const auto pink = wave("pink", 1000.0f, -6.0f);
    const MultiSignal pinkOut = generate(*pink);
    EXPECT_LE(peakDb(pinkOut[0]), 0.0f);
    EXPECT_GT(rmsDb(pinkOut[0]), -40.0f) << "pink noise is missing";
    EXPECT_LT(roughness(pinkOut[0]), 0.9f) << "pink noise is not darker than white";
}

TEST(WaveTest, EachSideIsOnlyThatSide)
{
    // A signal on the wrong side is exactly what this block is there to
    // find, so the other side is silence: not quiet, nothing.
    const auto left = wave("sine", 1000.0f, -12.0f, "left");
    const MultiSignal leftOut = generate(*left);
    EXPECT_NEAR(peakDb(leftOut[0]), -12.0f, 0.05);
    EXPECT_EQ(peak(leftOut[1]), 0.0f) << "the right side carried a left-only signal";

    const auto right = wave("sine", 1000.0f, -12.0f, "right");
    const MultiSignal rightOut = generate(*right);
    EXPECT_EQ(peak(rightOut[0]), 0.0f) << "the left side carried a right-only signal";
    EXPECT_NEAR(peakDb(rightOut[1]), -12.0f, 0.05);

    const auto both = wave("sine", 1000.0f, -12.0f, "both");
    const MultiSignal bothOut = generate(*both);
    EXPECT_TRUE(nulls(bothOut[0], bothOut[1])) << "both sides are not the same signal";
}

TEST(WaveTest, ChangingItWhileItPlaysDoesNotClick)
{
    // The level thrown from the floor to full scale, the tone moved an
    // octave and the side switched, one after another while it plays.
    Wave block;
    ASSERT_TRUE(block.setParam("level", ParamValue::ofFloat(-80.0f)).has_value());
    BlockRunner runner(block, 0, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    MultiSignal nothing;
    nothing.push_back(Signal(256, 0.0f));

    // The loudest honest step for a 2 kHz sine at full scale, with room:
    // 2π·f/sr is the steepest a sine gets.
    const float allowed = static_cast<float>(2.0 * std::numbers::pi * 2000.0 / kTestSampleRate) * 1.1f;
    // **One signal across the cycles, not one per cycle.** A setting changed
    // between two cycles steps, if it steps at all, exactly at the boundary,
    // which a step measured inside each cycle on its own never sees.
    Signal left;
    Signal right;
    for (int cycle = 0; cycle < 60; ++cycle) {
        if (cycle == 4) {
            ASSERT_TRUE(block.setParam("level", ParamValue::ofFloat(0.0f)).has_value());
        }
        if (cycle == 20) {
            ASSERT_TRUE(block.setParam("frequency", ParamValue::ofFloat(2000.0f)).has_value());
        }
        if (cycle == 40) {
            ASSERT_TRUE(block.setParam("channels", ParamValue::ofEnum("left")).has_value());
        }
        const MultiSignal out = runner.run(nothing, CycleShape::fixed(256));
        left.insert(left.end(), out[0].begin(), out[0].end());
        right.insert(right.end(), out[1].begin(), out[1].end());
    }
    // And each change arrived: full scale, then the right side gone.
    EXPECT_GT(peak(left), 0.99f) << "the level never reached full scale";
    EXPECT_LT(peak(Signal(right.end() - 2048, right.end())), 1e-4f) << "the right side never went quiet";
    const float worst = std::max(maxSampleStep(left), maxSampleStep(right));
    EXPECT_LT(worst, allowed) << "the loudest step between two samples was " << worst;
}

TEST(WaveTest, ItSoundsTheSameWhateverTheQuantum)
{
    for (const std::string_view shape : {"sine", "square", "triangle", "saw", "white", "pink"}) {
        MultiSignal nothing;
        nothing.push_back(Signal(8192, 0.0f));
        InvarianceOptions options;
        options.inputChannels = 0;
        options.outputChannels = 2;
        const InvarianceReport report = checkBlockSizeInvariance(
            [shape] { return std::unique_ptr<Block>(wave(shape, 997.0f, -6.0f)); }, nothing, options);
        EXPECT_TRUE(report.passed) << shape << ": " << report.summary;
    }
}

TEST(WaveTest, WhatWasSetIsWhatComesBack)
{
    const auto block = wave("triangle", 250.0f, -3.0f, "right");
    Wave opened;
    ASSERT_TRUE(opened.loadState(block->saveState()).has_value());
    EXPECT_EQ(opened.param("waveform").text(), "triangle");
    EXPECT_FLOAT_EQ(*opened.param("frequency").asFloat(), 250.0f);
    EXPECT_FLOAT_EQ(*opened.param("level").asFloat(), -3.0f);
    EXPECT_EQ(opened.param("channels").text(), "right");

    // Past the ends is refused rather than taken quietly.
    Wave refusing;
    EXPECT_FALSE(refusing.setParam("frequency", ParamValue::ofFloat(30000.0f)).has_value());
    EXPECT_FALSE(refusing.setParam("level", ParamValue::ofFloat(3.0f)).has_value());
    EXPECT_FALSE(refusing.setParam("waveform", ParamValue::ofEnum("chirp")).has_value());
}

TEST(WaveTest, ARunTouchesTheAllocatorNotAtAll)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    for (const std::string_view shape : {"sine", "square", "triangle", "saw", "white", "pink"}) {
        const auto block = wave(shape, 440.0f, -6.0f);
        BlockRunner runner(*block, 0, 2);
        ASSERT_TRUE(runner.prepare().has_value());
        MultiSignal nothing;
        nothing.push_back(Signal(9600, 0.0f));
        std::size_t allocations = 1;
        const MultiSignal out = runner.runWatchingForAllocations(nothing, CycleShape::random(1, 512, 3), allocations);
        EXPECT_EQ(allocations, 0U) << shape;
    }
}
