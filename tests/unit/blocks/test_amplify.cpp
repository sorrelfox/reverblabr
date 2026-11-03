// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Amplify: a gain and nothing else. Per cent is amplitude up to ten times,
// no latency, nothing held back at full scale, no step when it moves.

#include "allocationtrap.h"
#include "analysis.h"
#include "blocks/amplify.h"
#include "blocksizeinvariance.h"
#include "dsp/decibel.h"
#include "offlinerunner.h"
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
    return MultiSignal{sine(frames, hz, kTestSampleRate, amplitude), sine(frames, hz, kTestSampleRate, amplitude)};
}

[[nodiscard]] MultiSignal through(Amplify &block, const MultiSignal &input, std::uint32_t cycle = 256)
{
    BlockRunner runner(block, 2, 2);
    EXPECT_TRUE(runner.prepare().has_value());
    return runner.run(input, CycleShape::fixed(cycle));
}

} // namespace

TEST(AmplifyTest, PerCentMeansAmplitudeUpToTenTimes)
{
    for (const float gain : {0.0f, 0.5f, 1.0f, 2.0f, 4.0f, 10.0f}) {
        Amplify block;
        ASSERT_TRUE(block.setParam("gain", ParamValue::ofFloat(gain)).has_value());
        const MultiSignal in = stereoSine(4800, 997.0, 0.05f);
        const MultiSignal out = through(block, in);
        for (std::size_t channel = 0; channel < 2; ++channel) {
            for (std::size_t n = 0; n < in[channel].size(); ++n) {
                ASSERT_FLOAT_EQ(out[channel][n], in[channel][n] * gain) << "at " << gain << ", sample " << n;
            }
        }
    }
}

TEST(AmplifyTest, ItHoldsNothingBackAndAddsNoDelay)
{
    // No limiter: ten times a sine at half of full scale is five times full
    // scale, sent on as it is, on the same sample it came in.
    Amplify block;
    ASSERT_TRUE(block.setParam("gain", ParamValue::ofFloat(10.0f)).has_value());
    EXPECT_EQ(block.info().latencySamples, 0U);
    const MultiSignal out = through(block, stereoSine(4800, 220.0, 0.5f));
    EXPECT_NEAR(peak(out[0]), 5.0f, 0.01f);
    Signal impulse(1024, 0.0f);
    impulse[100] = 0.1f;
    Amplify unity;
    const MultiSignal passed = through(unity, MultiSignal{impulse, impulse});
    EXPECT_EQ(passed[0], impulse);
}

TEST(AmplifyTest, OnlyZeroToTenTimesIsTaken)
{
    Amplify block;
    EXPECT_FALSE(block.setParam("gain", ParamValue::ofFloat(10.5f)).has_value());
    EXPECT_FALSE(block.setParam("gain", ParamValue::ofFloat(-0.1f)).has_value());
    EXPECT_TRUE(block.setParam("gain", ParamValue::ofFloat(10.0f)).has_value());
    ASSERT_EQ(block.params().size(), 1U) << "a gain and nothing else";
}

TEST(AmplifyTest, GoingFromNothingToTenTimesDoesNotStepTheSignal)
{
    // The gain glides over 20 ms: no sample moves by more than the glide
    // allows a sine at this level.
    Amplify block;
    ASSERT_TRUE(block.setParam("gain", ParamValue::ofFloat(0.0f)).has_value());
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const MultiSignal in = stereoSine(9600, 100.0, 0.05f);
    block.applyParam(Amplify::Gain, 10.0f);
    const MultiSignal out = runner.run(in, CycleShape::fixed(256));
    EXPECT_LT(maxSampleStep(out[0]), 0.05f);
    EXPECT_NEAR(peak(Signal(out[0].begin() + 4800, out[0].end())), 0.5f, 0.005f);
}

TEST(AmplifyTest, ItSoundsTheSameWhateverTheQuantum)
{
    InvarianceOptions options;
    options.inputChannels = 2;
    options.outputChannels = 2;
    const InvarianceReport report = checkBlockSizeInvariance(
        [] {
            auto block = std::make_unique<Amplify>();
            EXPECT_TRUE(block->setParam("gain", ParamValue::ofFloat(3.0f)).has_value());
            return std::unique_ptr<Block>(std::move(block));
        },
        MultiSignal{sine(8192, 997.0, kTestSampleRate, 0.2f), sine(8192, 1500.0, kTestSampleRate, 0.2f)},
        options);
    EXPECT_TRUE(report.passed) << report.summary;
}

TEST(AmplifyTest, WhatWasSetIsWhatComesBack)
{
    Amplify block;
    ASSERT_TRUE(block.setParam("gain", ParamValue::ofFloat(7.5f)).has_value());
    Amplify opened;
    ASSERT_TRUE(opened.loadState(block.saveState()).has_value());
    EXPECT_FLOAT_EQ(*opened.param("gain").asFloat(), 7.5f);
}

TEST(AmplifyTest, ARunTouchesTheAllocatorNotAtAll)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    Amplify block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    block.applyParam(Amplify::Gain, 8.0f);
    std::size_t allocations = 1;
    const MultiSignal out =
        runner.runWatchingForAllocations(stereoSine(9600, 220.0, 0.1f), CycleShape::random(1, 512, 3), allocations);
    EXPECT_EQ(allocations, 0U);
    EXPECT_EQ(out[0].size(), 9600U);
}
