// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Output Device block on its own: what it does to the signal on
// the way to a device. Which device it reaches, and what happens when that
// device is unplugged, is the engine's half and is measured against a real
// server in tests/pw/test_devices.cpp.

#include "allocationtrap.h"
#include "analysis.h"
#include "blocks/frequencylowering.h"
#include "blocks/hearingcompensation.h"
#include "blocks/outputdevice.h"
#include "blocksizeinvariance.h"
#include "dsp/decibel.h"
#include "dsp/truepeak.h"
#include "offlinerunner.h"
#include "rt/meter.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <numbers>

using namespace rb;
using namespace rb::blocks;
using namespace rb::dsp;
using namespace rb::test;

namespace
{

/// Runs a stereo signal through a block prepared for one device layout.
[[nodiscard]] MultiSignal through(OutputDevice &block, const ChannelLayout &layout, const MultiSignal &input)
{
    BlockRunner runner(block, 2, static_cast<std::uint32_t>(layout.size()));
    runner.setOutputLayout(layout);
    EXPECT_TRUE(runner.prepare().has_value());
    return runner.run(input);
}

/// A true peak read finely, as nothing in the block does: \a signal windowed
/// sinc upsampled sixteen times.
[[nodiscard]] float truePeakOf(const Signal &signal)
{
    constexpr int kFactor = 16;
    constexpr int kReach = 64;
    float highest = 0.0f;
    const auto frames = static_cast<int>(signal.size());
    for (int frame = kReach; frame + kReach < frames; ++frame) {
        for (int step = 0; step < kFactor; ++step) {
            const double at = static_cast<double>(step) / kFactor;
            double sum = 0.0;
            for (int tap = -kReach + 1; tap <= kReach; ++tap) {
                const double x = static_cast<double>(tap) - at;
                const double sinc = std::abs(x) < 1e-9 ? 1.0 : std::sin(std::numbers::pi * x) / (std::numbers::pi * x);
                const double window = 0.5 + 0.5 * std::cos(std::numbers::pi * x / kReach);
                sum += static_cast<double>(signal[static_cast<std::size_t>(frame + tap)]) * sinc * window;
            }
            highest = std::max(highest, static_cast<float>(std::abs(sum)));
        }
    }
    return highest;
}

[[nodiscard]] Signal square(std::size_t frames, std::size_t period, float amplitude)
{
    Signal signal(frames);
    for (std::size_t n = 0; n < frames; ++n) {
        signal[n] = (n / period) % 2 == 0 ? amplitude : -amplitude;
    }
    return signal;
}

[[nodiscard]] MultiSignal stereoTone(float amplitude, std::size_t frames)
{
    MultiSignal signal;
    signal.push_back(sine(frames, 1000.0, kTestSampleRate, amplitude));
    signal.push_back(sine(frames, 1000.0, kTestSampleRate, amplitude));
    return signal;
}

} // namespace

TEST(OutputDeviceTest, IsAnOutputThatTakesTheStereoBus)
{
    const OutputDevice block;
    EXPECT_EQ(block.info().typeId, "output.device");
    EXPECT_EQ(block.info().category, BlockCategory::Outputs);
    EXPECT_EQ(block.info().inputs.minCount, 2U);
    EXPECT_EQ(block.info().inputs.maxCount, 2U);
    // Its output ports follow the device, up to the widest layout the
    // mapping policy handles.
    EXPECT_EQ(block.info().outputs.maxCount, kMaxMappedChannels);
    // Out of the box it follows whatever WirePlumber calls the default.
    EXPECT_EQ(block.param("device").text(), "default");
    EXPECT_FLOAT_EQ(*block.param("volume").asFloat(), 1.0f);
}

TEST(OutputDeviceTest, AtFullVolumeAStereoDeviceGetsWhatItWasGiven)
{
    OutputDevice block;
    const MultiSignal input = stereoTone(0.5f, 512);
    const MultiSignal out = through(block, stereoLayout(), input);

    ASSERT_EQ(out.size(), 2U);
    EXPECT_TRUE(nulls(out[0], input[0])) << "the left side was changed on the way out";
    EXPECT_TRUE(nulls(out[1], input[1]));
}

TEST(OutputDeviceTest, HalfTheVolumeIsSixDecibelsDown)
{
    // The criterion, and the reason the parameter is an amplitude rather
    // than a percentage or a number of decibels: half is half.
    OutputDevice block;
    ASSERT_TRUE(block.setParam("volume", ParamValue::ofFloat(0.5f)).has_value());

    const MultiSignal input = stereoTone(0.5f, 4096);
    const MultiSignal out = through(block, stereoLayout(), input);
    ASSERT_EQ(out.size(), 2U);
    ASSERT_EQ(out[0].size(), input[0].size());

    // Measured after the smoother has arrived, which it does in 20 ms.
    const auto settledFrom = static_cast<Signal::difference_type>(2048);
    const Signal settled(out[0].begin() + settledFrom, out[0].end());
    const Signal expected(input[0].begin() + settledFrom, input[0].end());
    EXPECT_NEAR(peakDb(settled) - peakDb(expected), -6.02f, 0.05f);
}

TEST(OutputDeviceTest, AVolumeSweepDoesNotStepTheSignal)
{
    // Zipper noise is what an unsmoothed volume sounds like: every change
    // steps the waveform, and a slider dragged across its range crackles.
    OutputDevice block;
    BlockRunner runner(block, 2, 2);
    runner.setOutputLayout(stereoLayout());
    ASSERT_TRUE(runner.prepare().has_value());

    // A hundred steps from full to nothing, a cycle apart, on a signal that
    // is itself perfectly steady so that every step shows.
    MultiSignal input;
    input.push_back(Signal(256, 1.0f));
    input.push_back(Signal(256, 1.0f));
    // **One signal across the cycles, not one per cycle**. A setting
    // changed between two cycles steps, if it steps at all, at the join,
    // which a step measured inside each cycle on its own never sees.
    Signal whole;
    for (int step = 100; step >= 0; --step) {
        ASSERT_TRUE(block.setParam("volume", ParamValue::ofFloat(static_cast<float>(step) / 100.0f)).has_value());
        const MultiSignal out = runner.run(input, CycleShape::fixed(256));
        whole.insert(whole.end(), out[0].begin(), out[0].end());
    }
    const float worst = maxSampleStep(whole);

    // **A tenth of one volume step.** Unsmoothed, each change is a step of
    // one per cent of full scale between two samples, exactly 0.01, which
    // a limit of 0.01 would only just catch. Glided over 20 ms it is
    // a thousandth of that.
    EXPECT_LT(worst, 0.001f) << "the loudest step between two samples was " << worst;
}

TEST(OutputDeviceTest, AMonoDeviceGetsHalfOfEachSide)
{
    // The policy, written down in docs/architecture.md: two correlated sides
    // summed at unity would come out 6 dB hot.
    OutputDevice block;
    MultiSignal input;
    input.push_back(Signal(256, 0.4f));
    input.push_back(Signal(256, 0.2f));

    const MultiSignal out = through(block, monoLayout(), input);
    ASSERT_EQ(out.size(), 1U);
    EXPECT_NEAR(out[0].back(), 0.5f * 0.4f + 0.5f * 0.2f, 1e-5f);
    EXPECT_FLOAT_EQ(block.coefficient(0, 0), kStereoToMonoGain);
}

TEST(OutputDeviceTest, ASurroundDeviceGetsTheStereoPairOnItsFrontChannels)
{
    OutputDevice block;
    MultiSignal input;
    input.push_back(Signal(256, 0.5f));
    input.push_back(Signal(256, 0.25f));

    const MultiSignal out = through(block, surround51Layout(), input);
    ASSERT_EQ(out.size(), 6U);
    EXPECT_NEAR(out[0].back(), 0.5f, 1e-5f); // FL
    EXPECT_NEAR(out[1].back(), 0.25f, 1e-5f); // FR
    // **Nothing is invented.** A stereo session has nothing to say about a
    // centre or a surround channel, so it says nothing rather than a copy.
    for (std::size_t channel = 2; channel < out.size(); ++channel) {
        EXPECT_FLOAT_EQ(out[channel].back(), 0.0f) << "channel " << channel << " was given something to play";
    }
}

TEST(OutputDeviceTest, AChannelChosenByHandIsWhereTheSideGoes)
{
    // What an audio interface is for: outputs 3 and 4 to the monitors on the
    // other side of the room.
    OutputDevice block;
    ASSERT_TRUE(block.setParam("leftChannel", ParamValue::ofString("AUX2")).has_value());
    ASSERT_TRUE(block.setParam("rightChannel", ParamValue::ofString("AUX3")).has_value());

    MultiSignal input;
    input.push_back(Signal(256, 0.5f));
    input.push_back(Signal(256, 0.25f));
    const MultiSignal out = through(block, auxLayout(4), input);

    ASSERT_EQ(out.size(), 4U);
    EXPECT_FLOAT_EQ(out[0].back(), 0.0f);
    EXPECT_FLOAT_EQ(out[1].back(), 0.0f);
    EXPECT_NEAR(out[2].back(), 0.5f, 1e-5f);
    EXPECT_NEAR(out[3].back(), 0.25f, 1e-5f);
}

TEST(OutputDeviceTest, BothSidesMayGoToOneChannelOrNowhere)
{
    OutputDevice block;
    // The left side twice, and the right side nowhere at all: a person
    // listening on one monitor.
    ASSERT_TRUE(block.setParam("leftChannel", ParamValue::ofString("FL")).has_value());
    ASSERT_TRUE(block.setParam("rightChannel", ParamValue::ofString("FL")).has_value());

    MultiSignal input;
    input.push_back(Signal(128, 0.4f));
    input.push_back(Signal(128, 0.2f));
    const MultiSignal out = through(block, stereoLayout(), input);

    EXPECT_NEAR(out[0].back(), 0.6f, 1e-5f) << "both sides should have arrived on the left channel";
    EXPECT_FLOAT_EQ(out[1].back(), 0.0f);
}

TEST(OutputDeviceTest, ItSoundsTheSameWhateverTheQuantum)
{
    MultiSignal input;
    input.push_back(sine(4096, 997.0, kTestSampleRate, 0.5f));
    input.push_back(sine(4096, 1500.0, kTestSampleRate, 0.3f));

    InvarianceOptions options;
    options.inputChannels = 2;
    options.outputChannels = 2;
    const InvarianceReport report = checkBlockSizeInvariance(
        [] { return std::unique_ptr<Block>(std::make_unique<OutputDevice>()); }, input, options);
    EXPECT_TRUE(report.passed) << report.summary;
}

TEST(OutputDeviceTest, WhatWasChosenIsWhatComesBack)
{
    OutputDevice block;
    ASSERT_TRUE(block.setParam("device", ParamValue::ofDeviceRef("alsa_output.usb-interface")).has_value());
    ASSERT_TRUE(block.setParam("volume", ParamValue::ofFloat(0.25f)).has_value());
    ASSERT_TRUE(block.setParam("leftChannel", ParamValue::ofString("AUX0")).has_value());

    const ParamValueMap saved = block.saveState();
    OutputDevice opened;
    ASSERT_TRUE(opened.loadState(saved).has_value());
    EXPECT_EQ(opened.param("device").text(), "alsa_output.usb-interface");
    EXPECT_FLOAT_EQ(*opened.param("volume").asFloat(), 0.25f);
    EXPECT_EQ(opened.param("leftChannel").text(), "AUX0");

    // A volume outside the range is refused rather than clamped quietly.
    EXPECT_FALSE(block.setParam("volume", ParamValue::ofFloat(2.0f)).has_value());
}

TEST(OutputDeviceTest, WithNoLimitItIsWhatItWas)
{
    OutputDevice block;
    const MultiSignal input{whiteNoise(4096, 1.0f, 5), whiteNoise(4096, 1.0f, 6)};
    const MultiSignal out = through(block, stereoLayout(), input);
    EXPECT_EQ(block.info().latencySamples, 0U);
    EXPECT_EQ(block.preparedShape(), 0U);
    EXPECT_EQ(out[0], input[0]);
    EXPECT_EQ(out[1], input[1]);
}

TEST(OutputDeviceTest, ItsLimitIsLastAndHolds)
{
    for (const float ceilingDb : {-1.0f, -12.0f}) {
        // Far over full scale, as a chain with every gain up can be.
        for (const Signal &signal :
             {sine(8000, 11025.0, kTestSampleRate, 8.0f), sine(8000, 997.0, kTestSampleRate, 8.0f)}) {
            OutputDevice block;
            block.setLastLimit(true, ceilingDb);
            EXPECT_EQ(block.preparedShape(), 1U);
            const MultiSignal out = through(block, stereoLayout(), MultiSignal{signal, signal});
            EXPECT_EQ(block.info().latencySamples, 84U);
            EXPECT_LE(truePeakOf(out[0]), dbToGain(ceilingDb + 0.1f)) << gainToDb(truePeakOf(out[0]));
            EXPECT_LE(peak(out[1]), dbToGain(ceilingDb)) << "no sample passes";
        }
    }
}

TEST(OutputDeviceTest, NoSampleOfAFullBandSignalPassesAndItsTruePeakStaysNear)
{
    // **Sound with energy up to the top of the band has peaks between the
    // points of a four times estimate**, and a square wave has them in its
    // ringing: this is the same limit Soft Lift ends in, measured. No sample
    // passes the ceiling. The true peak read finely is over it by what the
    // estimate cannot see, so this figure is a fact about the estimate.
    const float ceilingDb = -1.0f;
    for (const Signal &signal : {whiteNoise(8000, 1.0f, 3),
                                 whiteNoise(8000, 8.0f, 4),
                                 pinkNoise(8000, 1.5f, 3),
                                 pinkNoise(8000, 8.0f, 3),
                                 square(8000, 40, 8.0f)}) {
        OutputDevice block;
        block.setLastLimit(true, ceilingDb);
        const MultiSignal out = through(block, stereoLayout(), MultiSignal{signal, signal});
        EXPECT_LE(peak(out[0]), dbToGain(ceilingDb));
        EXPECT_LE(truePeakOf(out[0]), dbToGain(ceilingDb + 1.2f)) << gainToDb(truePeakOf(out[0]));
    }
}

TEST(OutputDeviceTest, ItsLimitComesAfterTheVolume)
{
    // At a quarter of the volume, four times full scale is only just there.
    OutputDevice block;
    ASSERT_TRUE(block.setParam("volume", ParamValue::ofFloat(0.25f)).has_value());
    block.setLastLimit(true, -1.0f);
    const MultiSignal out = through(block, stereoLayout(), stereoTone(2.0f, 8000));
    EXPECT_NEAR(gainToDb(peak(std::span<const float>(out[0]).subspan(4000))), gainToDb(0.5f), 0.01f)
        << "the limit acted on what the volume had already turned down";
}

TEST(OutputDeviceTest, ABothSidesOnOneChannelStaysUnder)
{
    OutputDevice block;
    ASSERT_TRUE(block.setParam("leftChannel", ParamValue::ofString("FL")).has_value());
    ASSERT_TRUE(block.setParam("rightChannel", ParamValue::ofString("FL")).has_value());
    block.setLastLimit(true, -1.0f);
    const MultiSignal out = through(block, stereoLayout(), stereoTone(1.0f, 8000));
    EXPECT_LE(truePeakOf(out[0]), dbToGain(-0.9f));
    EXPECT_GT(peak(out[0]), dbToGain(-2.0f));
}

TEST(OutputDeviceTest, UnderItsLimitItIsTheInputLateAndNothingElse)
{
    OutputDevice block;
    block.setLastLimit(true, -1.0f);
    const MultiSignal input{pinkNoise(9600, 0.1f, 2), pinkNoise(9600, 0.1f, 4)};
    const MultiSignal out = through(block, stereoLayout(), input);
    for (std::size_t channel = 0; channel < 2; ++channel) {
        for (std::size_t frame = 84; frame < 9600; ++frame) {
            ASSERT_EQ(out[channel][frame], input[channel][frame - 84]) << frame;
        }
    }
}

TEST(OutputDeviceTest, APeakOnOneChannelTakesBothDownAlike)
{
    OutputDevice block;
    block.setLastLimit(true, -6.0f);
    Signal loud = sine(9600, 1000.0, kTestSampleRate, 0.1f);
    for (std::size_t frame = 4800; frame < 5000; ++frame) {
        loud[frame] = frame % 2 == 0 ? 1.0f : -1.0f;
    }
    const Signal quiet = sine(9600, 1000.0, kTestSampleRate, 0.1f);
    const MultiSignal out = through(block, stereoLayout(), MultiSignal{loud, quiet});
    const float expected = 0.1f;
    float lowest = 1.0f;
    for (std::size_t frame = 5000; frame < 5040; ++frame) {
        const float in = quiet[frame - 84];
        if (std::abs(in) > 0.05f) {
            lowest = std::min(lowest, out[1][frame] / in);
        }
    }
    EXPECT_LT(lowest, 0.6f) << "the quiet side did not come down with the loud one (" << expected << ")";
}

TEST(OutputDeviceTest, ACeilingRisesAtTwelveDecibelsASecondAndFallsAtOnce)
{
    OutputDevice block;
    block.setLastLimit(true, -24.0f);
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const MultiSignal tone = stereoTone(0.9f, 4800);
    (void)runner.run(tone);
    block.setLastLimit(true, -1.0f);
    MultiSignal out;
    for (int tenth = 0; tenth < 10; ++tenth) {
        out = runner.run(tone);
    }
    // Less the 80 ms the gain trails the ceiling by.
    EXPECT_NEAR(peakDb(out[0]), -24.0f + 12.0f - 0.96f, 0.4f);

    block.setLastLimit(true, -24.0f);
    (void)runner.run(tone);
    out = runner.run(tone);
    EXPECT_NEAR(peakDb(out[0]), -24.0f, 0.2f);
}

TEST(OutputDeviceTest, WhatTheLimitIsDoingIsPublished)
{
    OutputDevice block;
    rt::BlockMeter meter;
    block.setLastLimit(true, -12.0f);
    BlockRunner runner(block, 2, 2);
    runner.setMeter(&meter);
    ASSERT_TRUE(runner.prepare().has_value());
    (void)runner.run(stereoTone(1.0f, 4800));
    EXPECT_FLOAT_EQ(meter.value(OutputDevice::kAppliedLimitValue), -12.0f);
    EXPECT_NEAR(meter.value(OutputDevice::kLimitReductionValue), 11.0f + 1.0f, 0.5f);

    OutputDevice plain;
    rt::BlockMeter none;
    none.publish(OutputDevice::kAppliedLimitValue, -12.0f);
    BlockRunner other(plain, 2, 2);
    other.setMeter(&none);
    ASSERT_TRUE(other.prepare().has_value());
    (void)other.run(stereoTone(0.5f, 256));
    EXPECT_FLOAT_EQ(none.value(OutputDevice::kAppliedLimitValue), 0.0f);
}

TEST(OutputDeviceTest, ItsLimitSoundsTheSameWhateverTheQuantum)
{
    MultiSignal input;
    input.push_back(whiteNoise(9600, 1.5f, 5));
    input.push_back(sine(9600, 1500.0, kTestSampleRate, 2.0f));

    InvarianceOptions options;
    options.inputChannels = 2;
    options.outputChannels = 2;
    options.maxBlockFrames = 8192;
    const InvarianceReport report = checkBlockSizeInvariance(
        [] {
            auto block = std::make_unique<OutputDevice>();
            block->setLastLimit(true, -3.0f);
            return std::unique_ptr<Block>(std::move(block));
        },
        input,
        options);
    EXPECT_TRUE(report.passed) << report.summary;
}

TEST(OutputDeviceTest, ItsLimitAllocatesNothing)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    OutputDevice block;
    block.setLastLimit(true, -1.0f);
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const MultiSignal input = stereoTone(2.0f, 4096);
    std::size_t allocations = 1;
    (void)runner.runWatchingForAllocations(input, CycleShape::random(1, 512, 4), allocations);
    EXPECT_EQ(allocations, 0U);
}

TEST(OutputDeviceTest, AMinuteOfItsLimitCostsLittleOfACore)
{
    OutputDevice block;
    block.setLastLimit(true, -1.0f);
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const Signal programme = pinkNoise(60 * 48000, 0.5f, 8);
    const auto started = std::chrono::steady_clock::now();
    (void)runner.run(MultiSignal{programme, programme}, CycleShape::fixed(256));
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const double share = took / 60.0;
    std::cerr << "last limit: a minute of stereo at quantum 256 in " << took << " s, " << share * 100.0
              << " % of a core\n";
#ifdef NDEBUG
    EXPECT_LT(share, 0.01) << "a minute took " << took << " s";
#else
    GTEST_SKIP() << "the figure above is from a debug build";
#endif
}

TEST(OutputDeviceTest, ItsLimitHasTheRangeOfAnOutputCeilingByEveryRoute)
{
    OutputDevice block;
    // Refused, by a control, a command or a file, and it stays what it was.
    EXPECT_FALSE(block.setParam("limitDb", ParamValue::ofFloat(-40.0f)).has_value());
    EXPECT_FALSE(block.setParam("limitDb", ParamValue::ofFloat(6.0f)).has_value());
    EXPECT_FLOAT_EQ(*block.param("limitDb").asFloat(), OutputDevice::kLimitHighestDb);
    EXPECT_FALSE(block.loadState({{"limitDb", ParamValue::ofFloat(-30.0f)}}).has_value());
    EXPECT_FLOAT_EQ(*block.param("limitDb").asFloat(), OutputDevice::kLimitHighestDb);

    // And what the runtime hands it, whatever it was given.
    block.setLastLimit(true, 3.0f);
    EXPECT_FLOAT_EQ(block.lastLimitDb(), OutputDevice::kLimitHighestDb);
}

TEST(OutputDeviceTest, ItsLimitIsNoSetting)
{
    OutputDevice block;
    block.setLastLimit(true, -9.0f);
    const ParamValueMap saved = block.saveState();
    EXPECT_FALSE(saved.contains("lastLimit"));
    EXPECT_EQ(saved.size(), 5U) << "a setting more than the five a session keeps";

    OutputDevice fresh;
    ASSERT_TRUE(fresh.loadState(saved).has_value());
    EXPECT_EQ(fresh.preparedShape(), 0U) << "only the runtime says this";
}

TEST(OutputDeviceTest, AChainWithALimitIsAsLateAsItsBlocksSay)
{
    // Soft Lift, then Frequency Lowering, then the limit, by correlation.
    const Signal programme = pinkNoise(16384, 0.05f, 6);
    const MultiSignal input{programme, programme};
    HearingCompensation lift(HearingCompensation::kBands16, HearingCompensation::kTypeId16);
    BlockRunner liftRunner(lift, 2, 2);
    ASSERT_TRUE(liftRunner.prepare().has_value());
    FrequencyLowering lowering;
    BlockRunner loweringRunner(lowering, 2, 2);
    ASSERT_TRUE(loweringRunner.prepare().has_value());
    OutputDevice block;
    block.setLastLimit(true, -1.0f);
    const MultiSignal out = through(block, stereoLayout(), loweringRunner.run(liftRunner.run(input)));
    EXPECT_EQ(lift.info().latencySamples + lowering.info().latencySamples + block.info().latencySamples, 1384U);

    std::size_t best = 0;
    double highest = 0.0;
    for (std::size_t lag = 1200; lag < 1600; ++lag) {
        double sum = 0.0;
        for (std::size_t frame = lag; frame < programme.size(); ++frame) {
            sum += static_cast<double>(out[1][frame]) * programme[frame - lag];
        }
        if (sum > highest) {
            highest = sum;
            best = lag;
        }
    }
    EXPECT_EQ(best, 1384U);
}
