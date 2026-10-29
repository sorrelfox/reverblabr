// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Volume block: a gain a person thinks about in per cent, with a
// limiter that will not let overdrive clip whatever is done to the slider.

#include "allocationtrap.h"
#include "analysis.h"
#include "blocks/volume.h"
#include "blocksizeinvariance.h"
#include "dsp/decibel.h"
#include "offlinerunner.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <random>
#include <thread>

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

/// The part of a run after the smoothers have arrived, so that a level is a
/// level and not a glide.
[[nodiscard]] Signal settled(const Signal &signal, std::size_t from = 4096)
{
    return Signal(signal.begin() + static_cast<Signal::difference_type>(from), signal.end());
}

[[nodiscard]] MultiSignal through(Volume &block, const MultiSignal &input, std::uint32_t cycle = 256)
{
    BlockRunner runner(block, 2, 2);
    EXPECT_TRUE(runner.prepare().has_value());
    return runner.run(input, CycleShape::fixed(cycle));
}

} // namespace

TEST(VolumeTest, PerCentMeansAmplitude)
{
    // The criterion: ±0.01 dB at a quarter, at unity and at four times.
    for (const auto &[gain, decibels] : std::vector<std::pair<float, double>>{{0.25f, -12.04}, {1.0f, 0.0}}) {
        Volume block;
        ASSERT_TRUE(block.setParam("overdrive", ParamValue::ofEnum("4x")).has_value());
        ASSERT_TRUE(block.setParam("gain", ParamValue::ofFloat(gain)).has_value());

        // Well below the ceiling, so the limiter is not part of the answer.
        const MultiSignal input = stereoSine(16384, 1000.0, 0.1f);
        const MultiSignal out = through(block, input);
        const double measured = peakDb(settled(out[0])) - peakDb(settled(input[0]));
        EXPECT_NEAR(measured, decibels, 0.01) << "at a gain of " << gain;
    }

    // Four times, measured where the limiter cannot reach: a quiet input.
    Volume loud;
    ASSERT_TRUE(loud.setParam("overdrive", ParamValue::ofEnum("4x")).has_value());
    ASSERT_TRUE(loud.setParam("gain", ParamValue::ofFloat(4.0f)).has_value());
    const MultiSignal quiet = stereoSine(16384, 1000.0, 0.05f);
    const MultiSignal out = through(loud, quiet);
    EXPECT_NEAR(peakDb(settled(out[0])) - peakDb(settled(quiet[0])), 12.04, 0.01);
}

TEST(VolumeTest, AtUnityItIsTheInputDelayedAndNothingElse)
{
    // The claim that lets the limiter sit in the path for ever: below the
    // ceiling it does nothing at all.
    Volume block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const std::uint32_t latency = block.info().latencySamples;
    EXPECT_GT(latency, 0U) << "a lookahead limiter has a latency and has to report it";

    const MultiSignal input = stereoSine(8192, 997.0, 0.5f); // −6 dBFS, below the ceiling
    const MultiSignal out = runner.run(input);

    for (std::uint32_t channel = 0; channel < 2; ++channel) {
        Signal delayed(input[channel].size(), 0.0f);
        for (std::size_t frame = latency; frame < delayed.size(); ++frame) {
            delayed[frame] = input[channel][frame - latency];
        }
        EXPECT_LE(nullTestDb(out[channel], delayed), kSilenceDb) << "channel " << channel << " was changed";
    }
}

TEST(VolumeTest, TheOutputNeverGoesPastTheCeilingHoweverHardItIsDriven)
{
    Volume block;
    ASSERT_TRUE(block.setParam("overdrive", ParamValue::ofEnum("4x")).has_value());
    ASSERT_TRUE(block.setParam("gain", ParamValue::ofFloat(4.0f)).has_value());

    // A full-scale sine at four times, and then noise bursts, which are the
    // hard case: nothing about them says where the next peak is.
    MultiSignal input = stereoSine(24000, 220.0, 1.0f);
    std::mt19937 random(20260921);
    std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
    for (std::size_t frame = 12000; frame < 24000; ++frame) {
        const bool burst = (frame / 500) % 2 == 0;
        input[0][frame] = burst ? noise(random) : noise(random) * 0.02f;
        input[1][frame] = burst ? noise(random) : noise(random) * 0.02f;
    }

    const MultiSignal out = through(block, input, 128);
    const float ceiling = dbToGain(LookaheadLimiter::kDefaultCeilingDb);
    for (std::uint32_t channel = 0; channel < 2; ++channel) {
        EXPECT_LE(peak(out[channel]), ceiling * 1.0001f)
            << "channel " << channel << " reached " << peakDb(out[channel]) << " dBFS";
    }
}

TEST(VolumeTest, GoingFromNothingToFourTimesDoesNotStepTheSignal)
{
    Volume block;
    ASSERT_TRUE(block.setParam("overdrive", ParamValue::ofEnum("4x")).has_value());
    ASSERT_TRUE(block.setParam("gain", ParamValue::ofFloat(0.0f)).has_value());

    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    MultiSignal input;
    input.push_back(Signal(256, 0.05f));
    input.push_back(Signal(256, 0.05f));

    // A few cycles at nothing, then the slider thrown to the top.
    // **One signal across the cycles, not one per cycle**. A setting
    // changed between two cycles steps, if it steps at all, at the join,
    // which a step measured inside each cycle on its own never sees.
    Signal whole;
    for (int cycle = 0; cycle < 40; ++cycle) {
        if (cycle == 4) {
            ASSERT_TRUE(block.setParam("gain", ParamValue::ofFloat(4.0f)).has_value());
        }
        const MultiSignal out = runner.run(input, CycleShape::fixed(256));
        whole.insert(whole.end(), out[0].begin(), out[0].end());
    }
    const float worst = maxSampleStep(whole);
    // Twenty milliseconds of glide over a step of four times a twentieth of
    // full scale: nothing a person can hear as a click.
    EXPECT_LT(worst, 0.01f) << "the loudest step between two samples was " << worst;
}

TEST(VolumeTest, MutingIsARampAndNotACut)
{
    Volume block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    MultiSignal input;
    input.push_back(Signal(256, 0.5f));
    input.push_back(Signal(256, 0.5f));

    // **One signal across the cycles, not one per cycle**. A setting
    // changed between two cycles steps, if it steps at all, at the join,
    // which a step measured inside each cycle on its own never sees.
    Signal whole;
    Signal last;
    for (int cycle = 0; cycle < 20; ++cycle) {
        if (cycle == 4) {
            ASSERT_TRUE(block.setParam("muted", ParamValue::ofBool(true)).has_value());
        }
        const MultiSignal out = runner.run(input, CycleShape::fixed(256));
        if (cycle > 0) {
            // **Not the first cycle.** The limiter's delay line starts
            // empty, so the signal arrives out of silence: that edge is the
            // signal starting, not the block clicking.
            whole.insert(whole.end(), out[0].begin(), out[0].end());
        }
        last = out[0];
    }
    const float worst = maxSampleStep(whole);
    EXPECT_LT(worst, 0.01f) << "muting stepped the signal by " << worst;
    EXPECT_LT(peak(last), 1e-6f) << "it never went quiet";
}

TEST(VolumeTest, TurningOverdriveDownBringsTheGainWithIt)
{
    Volume block;
    ASSERT_TRUE(block.setParam("overdrive", ParamValue::ofEnum("4x")).has_value());
    ASSERT_TRUE(block.setParam("gain", ParamValue::ofFloat(3.5f)).has_value());
    EXPECT_FLOAT_EQ(block.maximumGain(), 4.0f);

    ASSERT_TRUE(block.setParam("overdrive", ParamValue::ofEnum("2x")).has_value());
    EXPECT_FLOAT_EQ(block.maximumGain(), 2.0f);
    // What is stored is what is heard: a gain the slider cannot show is a
    // number nobody can see.
    EXPECT_FLOAT_EQ(*block.param("gain").asFloat(), 2.0f);

    ASSERT_TRUE(block.setParam("overdrive", ParamValue::ofEnum("off")).has_value());
    EXPECT_FLOAT_EQ(*block.param("gain").asFloat(), 1.0f);
}

TEST(VolumeTest, TheReportedLatencyIsTheDelayThatWasMeasured)
{
    Volume block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());

    MultiSignal input;
    input.push_back(Signal(4096, 0.0f));
    input.push_back(Signal(4096, 0.0f));
    input[0][100] = 0.5f; // an impulse, well below the ceiling
    input[1][100] = 0.5f;

    const MultiSignal out = runner.run(input);
    const std::optional<int> measured = rb::test::latencySamples(input[0], out[0], 1024);
    ASSERT_TRUE(measured.has_value()) << "the impulse never came out";
    EXPECT_EQ(static_cast<std::uint32_t>(*measured), block.info().latencySamples);
}

TEST(VolumeTest, ItSoundsTheSameWhateverTheQuantum)
{
    MultiSignal input;
    input.push_back(sine(8192, 997.0, kTestSampleRate, 0.4f));
    input.push_back(sine(8192, 1500.0, kTestSampleRate, 0.3f));

    InvarianceOptions options;
    options.inputChannels = 2;
    options.outputChannels = 2;
    const InvarianceReport report =
        checkBlockSizeInvariance([] { return std::unique_ptr<Block>(std::make_unique<Volume>()); }, input, options);
    EXPECT_TRUE(report.passed) << report.summary;
}

TEST(VolumeTest, WhatWasSetIsWhatComesBack)
{
    Volume block;
    ASSERT_TRUE(block.setParam("overdrive", ParamValue::ofEnum("3x")).has_value());
    ASSERT_TRUE(block.setParam("gain", ParamValue::ofFloat(2.5f)).has_value());
    ASSERT_TRUE(block.setParam("muted", ParamValue::ofBool(true)).has_value());

    Volume opened;
    ASSERT_TRUE(opened.loadState(block.saveState()).has_value());
    EXPECT_FLOAT_EQ(*opened.param("gain").asFloat(), 2.5f);
    EXPECT_EQ(opened.param("overdrive").text(), "3x");
    EXPECT_TRUE(*opened.param("muted").asBool());

    // A gain above the widest range is refused rather than taken quietly.
    EXPECT_FALSE(block.setParam("gain", ParamValue::ofFloat(5.0f)).has_value());
}

TEST(VolumeTest, ARunTouchesTheAllocatorNotAtAll)
{
    // The limiter makes its buffers in prepare(); a cycle has to be bounded
    // work on memory that is already there, at any quantum the engine
    // allows.
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    Volume block;
    ASSERT_TRUE(block.setParam("overdrive", ParamValue::ofEnum("4x")).has_value());
    ASSERT_TRUE(block.setParam("gain", ParamValue::ofFloat(4.0f)).has_value());
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

TEST(VolumeTest, SavingWhileItPlaysAndItsGainMovesIsSafe)
{
    // The ParamStore data race, through a real block: the audio thread runs
    // cycles and takes a gain that is being dragged while another thread
    // saves, as autosave does. Clean under tsan; and every save holds a gain
    // that was set.
    Volume block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const MultiSignal input = stereoSine(256, 997.0, 0.2f);
    std::atomic<bool> done{false};
    std::atomic<int> strange{0};
    std::thread saver([&] {
        while (!done.load()) {
            const float gain = *block.saveState().at("gain").asFloat();
            if (gain != 0.5f && gain != 0.25f && gain != 1.0f) {
                ++strange;
            }
        }
    });
    for (int cycle = 0; cycle < 2000; ++cycle) {
        block.applyParam(Volume::Gain, cycle % 2 == 0 ? 0.5f : 0.25f);
        (void)runner.run(input, CycleShape::fixed(256));
    }
    done = true;
    saver.join();
    EXPECT_EQ(strange.load(), 0);
}
