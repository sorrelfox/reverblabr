// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Mono Enhancer: width above the crossover, the lows left in the
// middle, and the two sides always adding back up to what came in.

#include "allocationtrap.h"
#include "analysis.h"
#include "blocks/monoenhancer.h"
#include "blocksizeinvariance.h"
#include "dsp/biquad.h"
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

[[nodiscard]] MultiSignal through(MonoEnhancer &block, const MultiSignal &input)
{
    BlockRunner runner(block, 2, 2);
    EXPECT_TRUE(runner.prepare().has_value());
    return runner.run(input, CycleShape::fixed(256));
}

[[nodiscard]] MultiSignal monoNoise(std::size_t frames)
{
    const Signal noise = whiteNoise(frames, 0.3f, 71);
    return MultiSignal{noise, noise};
}

/// \a signal through a fourth-order filter of \a shape at \a hz, to look at
/// one band of it.
[[nodiscard]] Signal band(const Signal &signal, FilterShape shape, double hz)
{
    Biquad first;
    Biquad second;
    first.setCoefficients(designBiquad({shape, hz, kButterworth4Q1, 0.0}, kTestSampleRate));
    second.setCoefficients(designBiquad({shape, hz, kButterworth4Q2, 0.0}, kTestSampleRate));
    Signal out(signal.size());
    for (std::size_t frame = 0; frame < signal.size(); ++frame) {
        out[frame] = second.process(first.process(signal[frame]));
    }
    return out;
}

/// How alike two signals are, from 1 (the same) through 0 (unrelated).
[[nodiscard]] double correlation(const Signal &a, const Signal &b, std::size_t from)
{
    double ab = 0.0;
    double aa = 0.0;
    double bb = 0.0;
    for (std::size_t frame = from; frame < a.size(); ++frame) {
        ab += static_cast<double>(a[frame]) * b[frame];
        aa += static_cast<double>(a[frame]) * a[frame];
        bb += static_cast<double>(b[frame]) * b[frame];
    }
    return ab / std::sqrt(aa * bb);
}

[[nodiscard]] Signal average(const MultiSignal &signal)
{
    Signal out(signal[0].size());
    for (std::size_t frame = 0; frame < out.size(); ++frame) {
        out[frame] = 0.5f * (signal[0][frame] + signal[1][frame]);
    }
    return out;
}

} // namespace

TEST(MonoEnhancerTest, TheTwoSidesAddBackUpToTheInput)
{
    // The criterion is ±1 dB across the spectrum; what the block promises is
    // stronger — the average of its two sides *is* the input, sample for
    // sample, so the spectrum cannot move at all. A null test says both.
    for (const float crossover : {100.0f, 500.0f, 2000.0f}) {
        MonoEnhancer block;
        ASSERT_TRUE(block.setParam("crossover", ParamValue::ofFloat(crossover)).has_value());
        const MultiSignal input = monoNoise(48000);
        const MultiSignal out = through(block, input);
        EXPECT_LE(nullTestDb(average(out), input[0]), -120.0f) << "at a crossover of " << crossover;
    }
}

TEST(MonoEnhancerTest, TheSumIsFlatWithinADecibelBandByBand)
{
    // The criterion as written, one octave band at a time from 63 Hz to
    // 16 kHz, so a sum that was only right on average would still fail.
    MonoEnhancer block;
    const MultiSignal input = monoNoise(96000);
    const Signal sum = average(through(block, input));
    for (double hz = 63.0; hz <= 16000.0; hz *= 2.0) {
        const Signal in = band(band(input[0], FilterShape::HighPass, hz / 1.41), FilterShape::LowPass, hz * 1.41);
        const Signal out = band(band(sum, FilterShape::HighPass, hz / 1.41), FilterShape::LowPass, hz * 1.41);
        EXPECT_NEAR(rmsDb(out) - rmsDb(in), 0.0, 1.0) << "the octave at " << hz << " Hz";
    }
}

TEST(MonoEnhancerTest, AboveTheCrossoverTheSidesDifferAndBelowItTheyDoNot)
{
    // The criterion: measurable decorrelation above the crossover. Mono
    // noise in, so the two sides start out identical (a correlation of 1).
    MonoEnhancer block;
    ASSERT_TRUE(block.setParam("crossover", ParamValue::ofFloat(500.0f)).has_value());
    const MultiSignal out = through(block, monoNoise(96000));
    const std::size_t settled = 4800;

    const double high =
        correlation(band(out[0], FilterShape::HighPass, 2000.0), band(out[1], FilterShape::HighPass, 2000.0), settled);
    const double low =
        correlation(band(out[0], FilterShape::LowPass, 125.0), band(out[1], FilterShape::LowPass, 125.0), settled);
    // (1 − 0.6²) / (1 + 0.6²) = 0.47 in theory.
    EXPECT_LT(high, 0.6) << "above the crossover the sides are still nearly the same";
    EXPECT_GT(high, 0.3) << "above the crossover the sides are further apart than the depth allows";
    EXPECT_GT(low, 0.99) << "below the crossover the sides were pulled apart";
}

TEST(MonoEnhancerTest, TheLowsStayInTheMiddle)
{
    // A bass note an octave and more below the crossover: the difference
    // between the sides is far below the note itself.
    MonoEnhancer block;
    ASSERT_TRUE(block.setParam("crossover", ParamValue::ofFloat(500.0f)).has_value());
    const Signal tone = sine(48000, 60.0, kTestSampleRate, 0.5f);
    const MultiSignal out = through(block, MultiSignal{tone, tone});
    Signal side(tone.size());
    for (std::size_t frame = 0; frame < side.size(); ++frame) {
        side[frame] = out[0][frame] - out[1][frame];
    }
    const Signal settledSide(side.begin() + 4800, side.end());
    EXPECT_LT(peakDb(settledSide) - peakDb(tone), -30.0f);
}

TEST(MonoEnhancerTest, MovingTheCrossoverDoesNotClick)
{
    // A tone above every crossover, and the crossover dragged across the
    // whole range and then thrown back. **A click is heard as the
    // frequencies a tone does not have**: a steady 2 kHz tone has nothing
    // above 12 kHz, however the comb shapes it, and a jump in the signal
    // puts energy everywhere. So what is measured is the output above
    // 12 kHz, against the tone. A step between two samples would not do:
    // the comb makes a tone's own steepest step depend on where it sits.
    MonoEnhancer block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const Signal tone = sine(256 * 80, 2000.0, kTestSampleRate, 0.25f);

    Signal left;
    std::size_t at = 0;
    const auto cycle = [&] {
        const Signal piece(tone.begin() + static_cast<Signal::difference_type>(at),
                           tone.begin() + static_cast<Signal::difference_type>(at + 256));
        at += 256;
        const MultiSignal out = runner.run(MultiSignal{piece, piece}, CycleShape::fixed(256));
        left.insert(left.end(), out[0].begin(), out[0].end());
    };
    for (int count = 0; count < 10; ++count) {
        cycle();
    }
    for (float hz = 100.0f; hz <= 2000.0f; hz *= 1.2f) {
        ASSERT_TRUE(block.setParam("crossover", ParamValue::ofFloat(hz)).has_value());
        cycle();
    }
    ASSERT_TRUE(block.setParam("crossover", ParamValue::ofFloat(100.0f)).has_value());
    while (at + 256 <= tone.size()) {
        cycle();
    }

    const Signal high = band(left, FilterShape::HighPass, 12000.0);
    // After the filters used to look have settled on the tone.
    const Signal settled(high.begin() + 2560, high.end());
    EXPECT_LT(peakDb(settled) - peakDb(tone), -60.0f) << "moving the crossover clicked";
}

TEST(MonoEnhancerTest, ItSoundsTheSameWhateverTheQuantum)
{
    InvarianceOptions options;
    options.inputChannels = 2;
    options.outputChannels = 2;
    const Signal noise = whiteNoise(8192, 0.3f, 7);
    const InvarianceReport report = checkBlockSizeInvariance(
        [] { return std::unique_ptr<Block>(std::make_unique<MonoEnhancer>()); }, MultiSignal{noise, noise}, options);
    EXPECT_TRUE(report.passed) << report.summary;
}

TEST(MonoEnhancerTest, ARunTouchesTheAllocatorNotAtAll)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    MonoEnhancer block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    // A glide in progress too: designing the filter must not allocate either.
    ASSERT_TRUE(block.setParam("crossover", ParamValue::ofFloat(1500.0f)).has_value());

    std::size_t allocations = 1;
    const MultiSignal input = monoNoise(9600);
    const MultiSignal out = runner.runWatchingForAllocations(input, CycleShape::random(1, 512, 3), allocations);
    EXPECT_EQ(allocations, 0U);
    EXPECT_EQ(out[0].size(), input[0].size());
}

TEST(MonoEnhancerTest, WhatWasSetIsWhatComesBack)
{
    MonoEnhancer block;
    EXPECT_FLOAT_EQ(*block.param("crossover").asFloat(), 500.0f);
    ASSERT_TRUE(block.setParam("crossover", ParamValue::ofFloat(800.0f)).has_value());
    MonoEnhancer opened;
    ASSERT_TRUE(opened.loadState(block.saveState()).has_value());
    EXPECT_FLOAT_EQ(*opened.param("crossover").asFloat(), 800.0f);
    EXPECT_FALSE(block.setParam("crossover", ParamValue::ofFloat(50.0f)).has_value());
    EXPECT_FALSE(block.setParam("crossover", ParamValue::ofFloat(2500.0f)).has_value());
    EXPECT_EQ(block.info().latencySamples, 0U);
}
