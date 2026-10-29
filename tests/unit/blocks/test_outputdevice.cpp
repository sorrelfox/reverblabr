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

#include "analysis.h"
#include "blocks/outputdevice.h"
#include "blocksizeinvariance.h"
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

/// Runs a stereo signal through a block prepared for one device layout.
[[nodiscard]] MultiSignal through(OutputDevice &block, const ChannelLayout &layout, const MultiSignal &input)
{
    BlockRunner runner(block, 2, static_cast<std::uint32_t>(layout.size()));
    runner.setOutputLayout(layout);
    EXPECT_TRUE(runner.prepare().has_value());
    return runner.run(input);
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
