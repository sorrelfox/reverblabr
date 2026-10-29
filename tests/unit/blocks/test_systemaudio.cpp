// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The System Audio block on its own. What it leaves out is the
// engine's half — a match, tested in engine.SessionPlanTest and against a
// real server in pw.system — and what arrives here has already been summed
// by PipeWire, so what is left to check is the layout and the settings.

#include "blocks/systemaudio.h"
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

[[nodiscard]] MultiSignal through(SystemAudio &block, const ChannelLayout &layout, const MultiSignal &input)
{
    BlockRunner runner(block, static_cast<std::uint32_t>(layout.size()), 2);
    runner.setInputLayout(layout);
    EXPECT_TRUE(runner.prepare().has_value());
    return runner.run(input);
}

} // namespace

TEST(SystemAudioTest, IsASourceThatLeavesAlertsOutUntilAsked)
{
    const SystemAudio block;
    EXPECT_EQ(block.info().typeId, "source.system");
    EXPECT_EQ(block.info().category, BlockCategory::Sources);
    EXPECT_EQ(block.info().outputs.minCount, 2U);
    // A notification that arrives in the middle of a recording is not part
    // of the recording.
    EXPECT_FALSE(*block.param("includeAlerts").asBool());
    EXPECT_TRUE(block.param("excluded").asStringList()->empty());
}

TEST(SystemAudioTest, WhatArrivesIsPassedOnAtTheLevelItArrived)
{
    // PipeWire has already summed every stream at the ports; the block must
    // not take a decibel off it.
    SystemAudio block;
    MultiSignal input;
    input.push_back(sine(512, 1000.0, kTestSampleRate, 0.5f));
    input.push_back(sine(512, 1000.0, kTestSampleRate, 0.25f));

    const MultiSignal out = through(block, stereoLayout(), input);
    EXPECT_TRUE(nulls(out[0], input[0]));
    EXPECT_TRUE(nulls(out[1], input[1]));
}

TEST(SystemAudioTest, ASurroundStreamIsFoldedByTheDocumentedCoefficients)
{
    SystemAudio block;
    MultiSignal input;
    for (int channel = 0; channel < 6; ++channel) {
        input.push_back(Signal(256, 0.0f));
    }
    input[2].assign(256, 0.5f); // the centre alone

    const MultiSignal out = through(block, surround51Layout(), input);
    EXPECT_NEAR(out[0].back(), kFoldGain * 0.5f, 1e-5f);
    EXPECT_NEAR(out[1].back(), kFoldGain * 0.5f, 1e-5f);
}

TEST(SystemAudioTest, TheExclusionListIsKeptAsItWasGiven)
{
    SystemAudio block;
    ASSERT_TRUE(block.setParam("excluded", ParamValue::ofStringList({"binary:firefox", "flatpak:org.videolan.VLC"}))
                    .has_value());
    ASSERT_TRUE(block.setParam("includeAlerts", ParamValue::ofBool(true)).has_value());

    SystemAudio opened;
    ASSERT_TRUE(opened.loadState(block.saveState()).has_value());
    const std::optional<std::vector<std::string>> excluded = opened.param("excluded").asStringList();
    ASSERT_TRUE(excluded.has_value());
    ASSERT_EQ(excluded->size(), 2U);
    EXPECT_EQ(excluded->at(0), "binary:firefox");
    EXPECT_TRUE(*opened.param("includeAlerts").asBool());
}

TEST(SystemAudioTest, ItSoundsTheSameWhateverTheQuantum)
{
    MultiSignal input;
    input.push_back(sine(4096, 997.0, kTestSampleRate, 0.5f));
    input.push_back(sine(4096, 1500.0, kTestSampleRate, 0.3f));

    InvarianceOptions options;
    options.inputChannels = 2;
    options.outputChannels = 2;
    const InvarianceReport report = checkBlockSizeInvariance(
        [] { return std::unique_ptr<Block>(std::make_unique<SystemAudio>()); }, input, options);
    EXPECT_TRUE(report.passed) << report.summary;
}
