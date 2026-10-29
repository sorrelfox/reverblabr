// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Peak/RMS meter. The peak, the clip latch and the activity flag
// come from the graph, which meters every block's output where it is made
// and is tested there at every quantum. What this block adds — and
// what is measured here — is that it changes nothing and that its root mean
// square is the calibrated number the scale is drawn against.

#include "allocationtrap.h"
#include "analysis.h"
#include "blocks/peakrmsmeter.h"
#include "blocksizeinvariance.h"
#include "dsp/decibel.h"
#include "offlinerunner.h"
#include "rt/meter.h"
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
    MultiSignal signal;
    signal.push_back(sine(frames, hz, kTestSampleRate, amplitude));
    signal.push_back(sine(frames, hz, kTestSampleRate, amplitude));
    return signal;
}

} // namespace

TEST(PeakRmsMeterTest, IsAMeterThatChangesNothing)
{
    const PeakRmsMeter block;
    EXPECT_EQ(block.info().typeId, "meter.peakrms");
    EXPECT_EQ(block.info().category, BlockCategory::Meters);
    EXPECT_EQ(block.info().face, FaceKind::Meter);
    // A meter that delayed the signal would have to be compensated for
    // everywhere one was dropped in.
    EXPECT_EQ(block.info().latencySamples, 0U);
}

TEST(PeakRmsMeterTest, TheAudioComesOutTheWayItWentIn)
{
    PeakRmsMeter block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());

    const MultiSignal input = stereoSine(8192, 997.0, 0.5f);
    const MultiSignal out = runner.run(input, CycleShape::random(1, 512, 5));
    for (std::uint32_t channel = 0; channel < 2; ++channel) {
        EXPECT_EQ(out[channel], input[channel]) << "channel " << channel << " was not bit for bit what arrived";
    }
}

TEST(PeakRmsMeterTest, ASineEighteenDecibelsDownReadsTwentyOneDownInRms)
{
    // The calibration this block exists for: plain RMS, so a sine reads
    // 3.01 dB below its peak. The panel says which convention it is.
    PeakRmsMeter block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());

    const float amplitude = dbToGain(-18.0f);
    const MultiSignal input = stereoSine(48000, 1000.0, amplitude);
    const MultiSignal out = runner.run(input);

    EXPECT_NEAR(peakDb(out[0]), -18.0f, 0.1f);
    EXPECT_NEAR(gainToDb(block.rms(0)), -21.0f, 0.1f);
    EXPECT_NEAR(gainToDb(block.rms(1)), -21.0f, 0.1f);
}

TEST(PeakRmsMeterTest, WhatIsAboveFullScaleIsMeasuredRatherThanFlattened)
{
    // A float over is a real thing to see on a meter: something upstream is
    // too loud, and hiding it at 0 dB would hide the reason.
    PeakRmsMeter block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());

    const MultiSignal input = stereoSine(48000, 1000.0, 2.0f); // +6 dBFS
    const MultiSignal out = runner.run(input);
    EXPECT_NEAR(peakDb(out[0]), 6.02f, 0.1f);
    EXPECT_NEAR(gainToDb(block.rms(0)), 3.01f, 0.1f);
}

TEST(PeakRmsMeterTest, TheWindowIsThreeHundredMillisecondsLong)
{
    PeakRmsMeter block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());

    // Fill the window, then feed it silence and watch it empty.
    const MultiSignal loud = stereoSine(24000, 1000.0, 0.5f); // half a second
    (void)runner.run(loud);
    const float settled = block.rms(0);
    EXPECT_NEAR(gainToDb(settled), -9.03f, 0.1f) << "a sine at half scale is −6.02 dB peak and 3.01 below it";

    MultiSignal quiet;
    quiet.push_back(Signal(7200, 0.0f)); // 150 ms, half the window
    quiet.push_back(Signal(7200, 0.0f));
    (void)runner.run(quiet);
    const float halfway = block.rms(0);
    // Half the window is silence, so the mean square is halved: 3 dB down.
    EXPECT_NEAR(gainToDb(halfway) - gainToDb(settled), -3.01f, 0.2f);

    (void)runner.run(quiet);
    EXPECT_LT(gainToDb(block.rms(0)), -100.0f) << "the window should be empty by now";
}

TEST(PeakRmsMeterTest, ItPublishesWhatItMeasuredWhereTheInterfaceLooks)
{
    PeakRmsMeter block;
    rt::BlockMeter meter;
    BlockRunner runner(block, 2, 2);
    runner.setMeter(&meter);
    ASSERT_TRUE(runner.prepare().has_value());

    const MultiSignal input = stereoSine(24000, 1000.0, 0.5f);
    (void)runner.run(input);

    EXPECT_NEAR(gainToDb(meter.value(PeakRmsMeter::kRmsLeftValue)), -9.03f, 0.1f);
    EXPECT_NEAR(gainToDb(meter.value(PeakRmsMeter::kRmsRightValue)), -9.03f, 0.1f);
}

TEST(PeakRmsMeterTest, NobodyWatchingIsNotACrash)
{
    // The meter is null when nothing is looking, which a block has to cope
    // with rather than assume.
    PeakRmsMeter block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const MultiSignal out = runner.run(stereoSine(1024, 1000.0, 0.5f));
    EXPECT_EQ(out[0].size(), 1024U);
}

TEST(PeakRmsMeterTest, ItSoundsTheSameWhateverTheQuantum)
{
    MultiSignal input;
    input.push_back(sine(8192, 997.0, kTestSampleRate, 0.4f));
    input.push_back(sine(8192, 1500.0, kTestSampleRate, 0.3f));

    InvarianceOptions options;
    options.inputChannels = 2;
    options.outputChannels = 2;
    const InvarianceReport report = checkBlockSizeInvariance(
        [] { return std::unique_ptr<Block>(std::make_unique<PeakRmsMeter>()); }, input, options);
    EXPECT_TRUE(report.passed) << report.summary;
}

TEST(PeakRmsMeterTest, ARunTouchesTheAllocatorNotAtAll)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    PeakRmsMeter block;
    rt::BlockMeter meter;
    BlockRunner runner(block, 2, 2);
    runner.setMeter(&meter);
    ASSERT_TRUE(runner.prepare().has_value());

    std::size_t allocations = 1;
    const MultiSignal out =
        runner.runWatchingForAllocations(stereoSine(9600, 220.0, 0.5f), CycleShape::random(1, 512, 4), allocations);
    EXPECT_EQ(allocations, 0U);
    EXPECT_EQ(out[0].size(), 9600U);
}

TEST(PeakRmsMeterTest, WhatWasChosenIsWhatComesBack)
{
    PeakRmsMeter block;
    ASSERT_TRUE(block.setParam("peakHold", ParamValue::ofEnum("5s")).has_value());
    ASSERT_TRUE(block.setParam("showInTray", ParamValue::ofBool(true)).has_value());

    PeakRmsMeter opened;
    ASSERT_TRUE(opened.loadState(block.saveState()).has_value());
    EXPECT_EQ(opened.param("peakHold").text(), "5s");
    EXPECT_TRUE(*opened.param("showInTray").asBool());
    // Two seconds out of the box, which is what a person expects a peak to
    // hang for.
    EXPECT_EQ(PeakRmsMeter{}.param("peakHold").text(), "2s");
}
