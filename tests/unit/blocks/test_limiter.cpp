// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Limiter block: nothing past the ceiling, sample peak or true
// peak, and a sine held down without being bent.

#include "allocationtrap.h"
#include "analysis.h"
#include "blocks/limiter.h"
#include "blocksizeinvariance.h"
#include "dsp/biquad.h"
#include "dsp/decibel.h"
#include "dsp/truepeak.h"
#include "offlinerunner.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <numbers>
#include <random>
#include <utility>
#include <vector>

using namespace rb;
using namespace rb::blocks;
using namespace rb::dsp;
using namespace rb::test;

namespace
{

[[nodiscard]] MultiSignal through(Limiter &block, const MultiSignal &input, std::uint32_t cycle = 256)
{
    BlockRunner runner(block, 2, 2);
    EXPECT_TRUE(runner.prepare().has_value());
    return runner.run(input, CycleShape::fixed(cycle));
}

/// The test's own true-peak meter: the signal interpolated \a factor times
/// with a windowed sinc 64 samples a side, much longer and finer than the
/// block's, so the block is not measured by the estimate it uses.
[[nodiscard]] float truePeakOf(const Signal &signal, int factor)
{
    constexpr int half = 64;
    float loudest = 0.0f;
    for (std::size_t n = half; n + half < signal.size(); ++n) {
        loudest = std::max(loudest, std::fabs(signal[n]));
        for (int point = 1; point < factor; ++point) {
            const double at = static_cast<double>(point) / factor;
            double sum = 0.0;
            for (int tap = -half + 1; tap <= half; ++tap) {
                const double x = at - tap;
                const double sinc = std::sin(std::numbers::pi * x) / (std::numbers::pi * x);
                const double window = 0.5 + 0.5 * std::cos(std::numbers::pi * x / half);
                sum += signal[n + tap] * sinc * window;
            }
            loudest = std::max(loudest, static_cast<float>(std::fabs(sum)));
        }
    }
    return loudest;
}

/// Something shaped like programme: pink noise in phrases at levels a few
/// decibels apart, with a tone near the top of the band riding on it —
/// the kind of content whose peaks fall between samples. Band-limited to
/// 20 kHz, as a recording is: what lies between that and half the rate is
/// where any short interpolator, BS.1770's own included, reads low.
[[nodiscard]] Signal programme(std::size_t frames, float level, std::uint32_t seed)
{
    Signal noise = pinkNoise(frames, 1.0f, seed);
    Biquad first;
    Biquad second;
    first.setCoefficients(designBiquad({FilterShape::LowPass, 20000.0, kButterworth4Q1, 0.0}, kTestSampleRate));
    second.setCoefficients(designBiquad({FilterShape::LowPass, 20000.0, kButterworth4Q2, 0.0}, kTestSampleRate));
    for (float &sample : noise) {
        sample = second.process(first.process(sample));
    }
    const Signal tone = sine(frames, 11025.0, kTestSampleRate, 0.4f, 0.5);
    std::mt19937 random(seed);
    std::uniform_real_distribution<float> phrase(0.5f, 1.0f);
    Signal out(frames);
    float gain = 1.0f;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        if (frame % 4800 == 0) {
            gain = phrase(random);
        }
        out[frame] = level * gain * (noise[frame] + tone[frame]);
    }
    return out;
}

} // namespace

TEST(LimiterTest, NothingGetsPastTheCeilingBySample)
{
    for (const float ceilingDb : {0.0f, -1.0f, -6.0f, -12.0f}) {
        Limiter block;
        ASSERT_TRUE(block.setParam("ceiling", ParamValue::ofFloat(ceilingDb)).has_value());
        ASSERT_TRUE(block.setParam("truePeak", ParamValue::ofBool(false)).has_value());
        const Signal loud = programme(48000, 2.0f, 89);
        const MultiSignal out = through(block, MultiSignal{loud, loud});
        const float ceiling = dbToGain(ceilingDb);
        EXPECT_LE(peak(out[0]), ceiling * 1.0001f) << "at a ceiling of " << ceilingDb << " dB";
        EXPECT_LE(peak(out[1]), ceiling * 1.0001f);
    }
}

TEST(LimiterTest, InTruePeakModeNothingGetsPastItBetweenTheSamplesEither)
{
    // The criterion. A tone whose samples all miss its peak by 3 dB: at
    // 0 dBTP its samples read -3 dBFS, under a -1 dB ceiling, so a
    // sample-peak limiter passes it and it reaches 0 dB in the converter.
    const Signal tone = sine(24000, 12000.0, kTestSampleRate, 1.0f, std::numbers::pi / 4.0);
    const float ceiling = dbToGain(-1.0f);

    Limiter bySample;
    ASSERT_TRUE(bySample.setParam("truePeak", ParamValue::ofBool(false)).has_value());
    const MultiSignal sampled = through(bySample, MultiSignal{tone, tone});
    EXPECT_GT(truePeakOf(sampled[0], 4), ceiling * 1.1f) << "the sample-peak limiter should have let it through";

    Limiter byTruePeak;
    const MultiSignal limited = through(byTruePeak, MultiSignal{tone, tone});
    EXPECT_LE(truePeakOf(limited[0], 4), ceiling * 1.0001f)
        << "4x true peak " << gainToDb(truePeakOf(limited[0], 4)) << " dBTP";
    EXPECT_LE(truePeakOf(limited[0], 16), ceiling * dbToGain(0.1f))
        << "16x true peak " << gainToDb(truePeakOf(limited[0], 16)) << " dBTP";
}

TEST(LimiterTest, ProgrammeStaysUnderTheCeilingByTruePeak)
{
    const Signal loud = programme(96000, 1.5f, 90);
    Limiter block;
    const MultiSignal out = through(block, MultiSignal{loud, loud});
    const float ceiling = dbToGain(-1.0f);
    EXPECT_LE(truePeakOf(out[0], 4), ceiling * 1.0001f)
        << "4x true peak " << gainToDb(truePeakOf(out[0], 4)) << " dBTP";
    EXPECT_LE(truePeakOf(out[0], 16), ceiling * dbToGain(0.1f))
        << "16x true peak " << gainToDb(truePeakOf(out[0], 16)) << " dBTP";
}

TEST(LimiterTest, ASineHeldDownIsNotBent)
{
    // The criterion: no audible distortion at moderate gain reduction. A
    // sine 6 dB over the ceiling, measured once the gain has settled. A
    // low tone is the hard case: its peaks are further apart than the
    // lookahead, so the gain moves within every cycle.
    for (const auto &[hz, allowed] :
         std::vector<std::pair<double, float>>{{1000.0, 0.0005f}, {200.0, 0.003f}, {80.0, 0.01f}}) {
        Limiter block;
        const float amplitude = dbToGain(-1.0f + 6.0f);
        const MultiSignal out = through(
            block,
            MultiSignal{sine(96000, hz, kTestSampleRate, amplitude), sine(96000, hz, kTestSampleRate, amplitude)});
        const Signal settled(out[0].begin() + 48000, out[0].end());
        const float thd = totalHarmonicDistortion(settled, hz);
        EXPECT_LT(thd, allowed) << hz << " Hz: THD " << thd * 100.0f << " %";
        EXPECT_NEAR(block.gainReductionDb(), 6.0f, 0.1f);
    }
}

TEST(LimiterTest, BelowTheCeilingItIsADelayAndNothingElse)
{
    for (const bool truePeak : {false, true}) {
        Limiter block;
        ASSERT_TRUE(block.setParam("truePeak", ParamValue::ofBool(truePeak)).has_value());
        const Signal quiet = programme(9600, 0.1f, 91);
        const MultiSignal out = through(block, MultiSignal{quiet, quiet});
        const std::uint32_t latency = block.info().latencySamples;
        Signal delayed(quiet.size(), 0.0f);
        std::copy(quiet.begin(), quiet.end() - latency, delayed.begin() + latency);
        EXPECT_LE(nullTestDb(out[0], delayed), kSilenceDb) << (truePeak ? "true peak" : "sample peak");
        EXPECT_EQ(block.gainReductionDb(), 0.0f);
    }
}

TEST(LimiterTest, TheLatencyIsReportedAndTheSameInBothModes)
{
    Limiter block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    // 1.5 ms of lookahead at 48 kHz, and the samples the true-peak
    // estimate looks back.
    EXPECT_EQ(block.info().latencySamples, 72U + TruePeakEstimator::kDelay);

    MultiSignal input{Signal(4096, 0.0f), Signal(4096, 0.0f)};
    input[0][100] = 0.5f;
    input[1][100] = 0.5f;
    const MultiSignal out = runner.run(input);
    const std::optional<int> measured = rb::test::latencySamples(input[0], out[0], 1024);
    ASSERT_TRUE(measured.has_value());
    EXPECT_EQ(static_cast<std::uint32_t>(*measured), block.info().latencySamples);

    ASSERT_TRUE(block.setParam("truePeak", ParamValue::ofBool(false)).has_value());
    EXPECT_EQ(block.info().latencySamples, 72U + TruePeakEstimator::kDelay) << "switching modes moved the lane";
}

TEST(LimiterTest, MovingTheCeilingDoesNotClick)
{
    // A tone held down, and the ceiling thrown from 0 to -12 dB and back.
    // **A click is heard as frequencies the tone does not have**: a 1 kHz
    // tone, however the limiter shapes it, has almost nothing above 12 kHz,
    // and a step in the gain puts energy everywhere.
    const auto highestAbove12k = [](bool moveCeiling) {
        Limiter block;
        EXPECT_TRUE(block.setParam("ceiling", ParamValue::ofFloat(0.0f)).has_value());
        BlockRunner runner(block, 2, 2);
        EXPECT_TRUE(runner.prepare().has_value());
        const Signal tone = sine(256 * 60, 1000.0, kTestSampleRate, 2.0f);
        Signal left;
        for (std::size_t at = 0; at + 256 <= tone.size(); at += 256) {
            if (moveCeiling && at == 256 * 20) {
                EXPECT_TRUE(block.setParam("ceiling", ParamValue::ofFloat(-12.0f)).has_value());
            }
            if (moveCeiling && at == 256 * 40) {
                EXPECT_TRUE(block.setParam("ceiling", ParamValue::ofFloat(0.0f)).has_value());
            }
            const Signal piece(tone.begin() + static_cast<Signal::difference_type>(at),
                               tone.begin() + static_cast<Signal::difference_type>(at + 256));
            const MultiSignal out = runner.run(MultiSignal{piece, piece}, CycleShape::fixed(256));
            left.insert(left.end(), out[0].begin(), out[0].end());
        }
        Biquad first;
        Biquad second;
        first.setCoefficients(designBiquad({FilterShape::HighPass, 12000.0, kButterworth4Q1, 0.0}, kTestSampleRate));
        second.setCoefficients(designBiquad({FilterShape::HighPass, 12000.0, kButterworth4Q2, 0.0}, kTestSampleRate));
        float highest = 0.0f;
        for (std::size_t frame = 0; frame < left.size(); ++frame) {
            const float high = second.process(first.process(left[frame]));
            if (frame >= 2560) {
                highest = std::max(highest, std::fabs(high));
            }
        }
        return gainToDb(highest);
    };
    const float still = highestAbove12k(false);
    const float moved = highestAbove12k(true);
    EXPECT_LT(moved, -75.0f) << "moving the ceiling put " << moved << " dB above 12 kHz, against " << still
                             << " dB with it still";
}

TEST(LimiterTest, ItSoundsTheSameWhateverTheQuantum)
{
    const Signal loud = programme(8192, 1.5f, 92);
    InvarianceOptions options;
    options.inputChannels = 2;
    options.outputChannels = 2;
    const InvarianceReport report = checkBlockSizeInvariance(
        [] { return std::unique_ptr<Block>(std::make_unique<Limiter>()); }, MultiSignal{loud, loud}, options);
    EXPECT_TRUE(report.passed) << report.summary;
}

TEST(LimiterTest, ARunTouchesTheAllocatorNotAtAll)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    Limiter block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    ASSERT_TRUE(block.setParam("ceiling", ParamValue::ofFloat(-6.0f)).has_value());
    const Signal loud = programme(9600, 1.5f, 93);
    std::size_t allocations = 1;
    const MultiSignal out =
        runner.runWatchingForAllocations(MultiSignal{loud, loud}, CycleShape::random(1, 512, 3), allocations);
    EXPECT_EQ(allocations, 0U);
    EXPECT_EQ(out[0].size(), loud.size());
}

TEST(LimiterTest, WhatWasSetIsWhatComesBack)
{
    Limiter block;
    ASSERT_TRUE(block.setParam("ceiling", ParamValue::ofFloat(-3.0f)).has_value());
    ASSERT_TRUE(block.setParam("release", ParamValue::ofFloat(250.0f)).has_value());
    ASSERT_TRUE(block.setParam("truePeak", ParamValue::ofBool(false)).has_value());
    Limiter opened;
    ASSERT_TRUE(opened.loadState(block.saveState()).has_value());
    EXPECT_FLOAT_EQ(*opened.param("ceiling").asFloat(), -3.0f);
    EXPECT_FLOAT_EQ(*opened.param("release").asFloat(), 250.0f);
    EXPECT_FALSE(*opened.param("truePeak").asBool());
    EXPECT_FALSE(block.setParam("ceiling", ParamValue::ofFloat(0.5f)).has_value());
    EXPECT_FALSE(block.setParam("release", ParamValue::ofFloat(5.0f)).has_value());
}
