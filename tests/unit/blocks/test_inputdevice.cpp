// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Input Device block on its own: what it does with the channels a
// device gives it. Which device that is, and what happens when it is
// unplugged, is measured against a real server in tests/pw/test_devices.cpp.

#include "blocks/inputdevice.h"
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

[[nodiscard]] MultiSignal through(InputDevice &block, const ChannelLayout &layout, const MultiSignal &input)
{
    BlockRunner runner(block, static_cast<std::uint32_t>(layout.size()), 2);
    runner.setInputLayout(layout);
    EXPECT_TRUE(runner.prepare().has_value());
    return runner.run(input);
}

[[nodiscard]] MultiSignal channels(std::size_t count, std::size_t frames, float value)
{
    MultiSignal signal;
    for (std::size_t channel = 0; channel < count; ++channel) {
        signal.push_back(Signal(frames, value));
    }
    return signal;
}

} // namespace

TEST(InputDeviceTest, IsASourceThatGivesTheStereoBus)
{
    const InputDevice block;
    EXPECT_EQ(block.info().typeId, "source.device");
    EXPECT_EQ(block.info().category, BlockCategory::Sources);
    EXPECT_EQ(block.info().inputs.maxCount, kMaxMappedChannels);
    EXPECT_EQ(block.info().outputs.minCount, 2U);
    EXPECT_EQ(block.param("device").text(), "default");
}

TEST(InputDeviceTest, AMonoMicrophoneIsHeardOnBothSidesAtItsOwnLevel)
{
    // The criterion, and the reason the mapping is done here: linking one
    // port into two would have PipeWire sum it and hand back 6 dB it never
    // had.
    InputDevice block;
    MultiSignal input;
    input.push_back(sine(512, 1000.0, kTestSampleRate, 0.5f));

    const MultiSignal out = through(block, monoLayout(), input);
    ASSERT_EQ(out.size(), 2U);
    EXPECT_TRUE(nulls(out[0], input[0])) << "the left side was not what the microphone heard";
    EXPECT_TRUE(nulls(out[1], input[0])) << "the right side was not what the microphone heard";
    EXPECT_FLOAT_EQ(block.coefficient(0, 0), 1.0f);
    EXPECT_FLOAT_EQ(block.coefficient(0, 1), 1.0f);
}

TEST(InputDeviceTest, AStereoDeviceComesThroughUntouched)
{
    InputDevice block;
    MultiSignal input;
    input.push_back(sine(512, 1000.0, kTestSampleRate, 0.5f));
    input.push_back(sine(512, 1000.0, kTestSampleRate, 0.25f));

    const MultiSignal out = through(block, stereoLayout(), input);
    EXPECT_TRUE(nulls(out[0], input[0]));
    EXPECT_TRUE(nulls(out[1], input[1]));
}

TEST(InputDeviceTest, AChannelChosenByHandIsWhereTheSideComesFrom)
{
    // Input 3 to the left and input 4 to the right, which is what a
    // multichannel interface is for.
    InputDevice block;
    ASSERT_TRUE(block.setParam("leftChannel", ParamValue::ofString("AUX2")).has_value());
    ASSERT_TRUE(block.setParam("rightChannel", ParamValue::ofString("AUX3")).has_value());

    MultiSignal input = channels(4, 256, 0.0f);
    input[2].assign(256, 0.5f);
    input[3].assign(256, 0.25f);

    const MultiSignal out = through(block, auxLayout(4), input);
    EXPECT_NEAR(out[0].back(), 0.5f, 1e-5f);
    EXPECT_NEAR(out[1].back(), 0.25f, 1e-5f);
}

TEST(InputDeviceTest, TheSidesCanBeSwapped)
{
    InputDevice block;
    ASSERT_TRUE(block.setParam("leftChannel", ParamValue::ofString("FR")).has_value());
    ASSERT_TRUE(block.setParam("rightChannel", ParamValue::ofString("FL")).has_value());

    MultiSignal input;
    input.push_back(Signal(128, 0.4f));
    input.push_back(Signal(128, 0.2f));
    const MultiSignal out = through(block, stereoLayout(), input);

    EXPECT_NEAR(out[0].back(), 0.2f, 1e-5f);
    EXPECT_NEAR(out[1].back(), 0.4f, 1e-5f);
}

TEST(InputDeviceTest, ASideThatNamesAChannelTheDeviceDoesNotHaveIsSilent)
{
    // A session set up for an eight-channel interface, opened on a laptop.
    // Silence is honest; guessing at another channel would put the signal
    // somewhere nobody chose.
    InputDevice block;
    ASSERT_TRUE(block.setParam("leftChannel", ParamValue::ofString("AUX7")).has_value());
    ASSERT_TRUE(block.setParam("rightChannel", ParamValue::ofString("FR")).has_value());

    MultiSignal input;
    input.push_back(Signal(128, 0.4f));
    input.push_back(Signal(128, 0.2f));
    const MultiSignal out = through(block, stereoLayout(), input);

    EXPECT_FLOAT_EQ(out[0].back(), 0.0f);
    EXPECT_NEAR(out[1].back(), 0.2f, 1e-5f);
}

TEST(InputDeviceTest, AWideDeviceIsFoldedByTheDocumentedCoefficients)
{
    InputDevice block;
    MultiSignal input = channels(6, 256, 0.0f);
    input[2].assign(256, 0.5f); // the centre alone

    const MultiSignal out = through(block, surround51Layout(), input);
    EXPECT_NEAR(out[0].back(), kFoldGain * 0.5f, 1e-5f);
    EXPECT_NEAR(out[1].back(), kFoldGain * 0.5f, 1e-5f);
}

TEST(InputDeviceTest, ItSoundsTheSameWhateverTheQuantum)
{
    MultiSignal input;
    input.push_back(sine(4096, 997.0, kTestSampleRate, 0.5f));
    input.push_back(sine(4096, 1500.0, kTestSampleRate, 0.3f));

    InvarianceOptions options;
    options.inputChannels = 2;
    options.outputChannels = 2;
    const InvarianceReport report = checkBlockSizeInvariance(
        [] { return std::unique_ptr<Block>(std::make_unique<InputDevice>()); }, input, options);
    EXPECT_TRUE(report.passed) << report.summary;
}

TEST(InputDeviceTest, WhatWasChosenIsWhatComesBack)
{
    InputDevice block;
    ASSERT_TRUE(block.setParam("device", ParamValue::ofDeviceRef("alsa_input.usb-Microphone")).has_value());
    ASSERT_TRUE(block.setParam("leftChannel", ParamValue::ofString("AUX0")).has_value());

    InputDevice opened;
    ASSERT_TRUE(opened.loadState(block.saveState()).has_value());
    EXPECT_EQ(opened.param("device").text(), "alsa_input.usb-Microphone");
    EXPECT_EQ(opened.param("leftChannel").text(), "AUX0");
    EXPECT_EQ(opened.param("rightChannel").text(), "default");
}
