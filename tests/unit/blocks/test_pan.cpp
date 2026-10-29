// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Pan block: one sound, folded to mono and placed between the
// left and the right at the same loudness wherever it goes.

#include "allocationtrap.h"
#include "analysis.h"
#include "blocks/pan.h"
#include "blocksizeinvariance.h"
#include "dsp/decibel.h"
#include "offlinerunner.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>

using namespace rb;
using namespace rb::blocks;
using namespace rb::dsp;
using namespace rb::test;

namespace
{

[[nodiscard]] MultiSignal through(Pan &block, const MultiSignal &input)
{
    BlockRunner runner(block, 2, 2);
    EXPECT_TRUE(runner.prepare().has_value());
    return runner.run(input, CycleShape::fixed(256));
}

[[nodiscard]] MultiSignal centred(std::size_t frames, double hz = 997.0, float amplitude = 0.5f)
{
    const Signal tone = sine(frames, hz, kTestSampleRate, amplitude);
    return MultiSignal{tone, tone};
}

} // namespace

TEST(PanTest, ThePanLawAtTheEndsTheMiddleAndHalfWay)
{
    // The criterion: −100, −50, 0, +50 and +100, ±0.05 dB, measured against
    // a sound that arrives in the middle of the pair.
    struct Expected {
        float position;
        double leftDb; ///< kSilenceDb stands for "gone".
        double rightDb;
    };
    const double silent = static_cast<double>(kSilenceDb);
    for (const Expected &expected : {Expected{-100.0f, 0.0, silent},
                                     Expected{-50.0f, -0.688, -8.343},
                                     Expected{0.0f, -3.010, -3.010},
                                     Expected{50.0f, -8.343, -0.688},
                                     Expected{100.0f, silent, 0.0}}) {
        Pan block;
        ASSERT_TRUE(block.setParam("pan", ParamValue::ofFloat(expected.position)).has_value());
        const MultiSignal input = centred(8192);
        const MultiSignal out = through(block, input);
        for (std::uint32_t channel = 0; channel < 2; ++channel) {
            const double want = channel == 0 ? expected.leftDb : expected.rightDb;
            if (want == silent) {
                EXPECT_EQ(peak(out[channel]), 0.0f) << "channel " << channel << " at " << expected.position;
            } else {
                const double got = static_cast<double>(peakDb(out[channel])) - peakDb(input[channel]);
                EXPECT_NEAR(got, want, 0.05) << "channel " << channel << " at " << expected.position;
            }
        }
    }
}

TEST(PanTest, TheLoudnessStaysTheSameWhereverTheSoundIsPut)
{
    // Constant power: the two sides' energies add up to the same whatever
    // the setting.
    const MultiSignal input = centred(8192);
    const double reference = [&] {
        Pan block;
        const MultiSignal out = through(block, input);
        return static_cast<double>(rms(out[0])) * rms(out[0]) + static_cast<double>(rms(out[1])) * rms(out[1]);
    }();
    for (int position = -100; position <= 100; position += 25) {
        Pan block;
        ASSERT_TRUE(block.setParam("pan", ParamValue::ofFloat(static_cast<float>(position))).has_value());
        const MultiSignal out = through(block, input);
        const double power =
            static_cast<double>(rms(out[0])) * rms(out[0]) + static_cast<double>(rms(out[1])) * rms(out[1]);
        EXPECT_NEAR(10.0 * std::log10(power / reference), 0.0, 0.05) << "at " << position;
    }
}

TEST(PanTest, AStereoInputIsFoldedToOneSoundFirst)
{
    // What separates Pan from Balance: hard left, a tone that arrived only on
    // the right is on the left, at half its amplitude (the fold averages the
    // two sides), and nothing is left on the right.
    const Signal tone = sine(8192, 997.0, kTestSampleRate, 0.5f);
    Pan block;
    ASSERT_TRUE(block.setParam("pan", ParamValue::ofFloat(-100.0f)).has_value());
    const MultiSignal out = through(block, MultiSignal{silence(8192), tone});
    EXPECT_NEAR(static_cast<double>(peakDb(out[0])) - peakDb(tone), -6.02, 0.05);
    EXPECT_EQ(peak(out[1]), 0.0f);

    // And two different sides come out as one sound, the same on both.
    Pan centre;
    const MultiSignal both = through(centre, MultiSignal{tone, sine(8192, 1499.0, kTestSampleRate, 0.5f)});
    EXPECT_LE(nullTestDb(both[0], both[1]), kSilenceDb);
    EXPECT_GT(amplitudeAt(both[0], 1499.0), 0.1f) << "the right side's tone is not on the left";
}

TEST(PanTest, SweepingTheControlDoesNotStepTheSignal)
{
    // **One signal across the cycles**: a setting changed between two
    // cycles steps, if it steps at all, at the join.
    Pan block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const MultiSignal input{Signal(256, 0.5f), Signal(256, 0.5f)};

    Signal left;
    Signal right;
    const auto cycle = [&] {
        const MultiSignal out = runner.run(input, CycleShape::fixed(256));
        left.insert(left.end(), out[0].begin(), out[0].end());
        right.insert(right.end(), out[1].begin(), out[1].end());
    };
    for (int position = -100; position <= 100; position += 10) {
        ASSERT_TRUE(block.setParam("pan", ParamValue::ofFloat(static_cast<float>(position))).has_value());
        cycle();
    }
    ASSERT_TRUE(block.setParam("pan", ParamValue::ofFloat(-100.0f)).has_value());
    for (int count = 0; count < 8; ++count) {
        cycle();
    }

    // End to end moves a side by the whole 0.5 over 960 samples.
    EXPECT_LT(maxSampleStep(left), 0.001f) << "the left side stepped";
    EXPECT_LT(maxSampleStep(right), 0.001f) << "the right side stepped";
    EXPECT_EQ(right.back(), 0.0f);
    EXPECT_EQ(left.back(), 0.5f);
}

TEST(PanTest, ItSoundsTheSameWhateverTheQuantum)
{
    MultiSignal input;
    input.push_back(sine(8192, 997.0, kTestSampleRate, 0.4f));
    input.push_back(sine(8192, 1499.0, kTestSampleRate, 0.3f));
    InvarianceOptions options;
    options.inputChannels = 2;
    options.outputChannels = 2;
    const InvarianceReport report = checkBlockSizeInvariance(
        [] {
            auto block = std::make_unique<Pan>();
            EXPECT_TRUE(block->setParam("pan", ParamValue::ofFloat(40.0f)).has_value());
            return std::unique_ptr<Block>(std::move(block));
        },
        input,
        options);
    EXPECT_TRUE(report.passed) << report.summary;
}

TEST(PanTest, ARunTouchesTheAllocatorNotAtAll)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    Pan block;
    ASSERT_TRUE(block.setParam("pan", ParamValue::ofFloat(-60.0f)).has_value());
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());

    std::size_t allocations = 1;
    const MultiSignal input = centred(9600);
    const MultiSignal out = runner.runWatchingForAllocations(input, CycleShape::random(1, 512, 3), allocations);
    EXPECT_EQ(allocations, 0U);
    EXPECT_EQ(out[0].size(), input[0].size());
}

TEST(PanTest, WhatWasSetIsWhatComesBack)
{
    Pan block;
    ASSERT_TRUE(block.setParam("pan", ParamValue::ofFloat(40.0f)).has_value());
    Pan opened;
    ASSERT_TRUE(opened.loadState(block.saveState()).has_value());
    EXPECT_FLOAT_EQ(*opened.param("pan").asFloat(), 40.0f);
    EXPECT_FALSE(block.setParam("pan", ParamValue::ofFloat(100.5f)).has_value());
    EXPECT_FALSE(block.setParam("pan", ParamValue::ofFloat(-100.5f)).has_value());
    EXPECT_EQ(block.info().latencySamples, 0U);
}

TEST(PanTest, OpeningASavedSettingIsHeardAtOnceNotGlidedTo)
{
    Pan block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    Pan saved;
    ASSERT_TRUE(saved.setParam("pan", ParamValue::ofFloat(-100.0f)).has_value());
    ASSERT_TRUE(block.loadState(saved.saveState()).has_value());

    const MultiSignal out = runner.run(MultiSignal{Signal(64, 0.5f), Signal(64, 0.5f)}, CycleShape::fixed(64));
    EXPECT_EQ(peak(out[1]), 0.0f);
}
