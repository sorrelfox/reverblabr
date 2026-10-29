// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The VU meter. The clip latch and the activity flag come from the
// graph, as they do for every block; what is measured here is the needle —
// how fast it rises, how far it swings past, what a sine reads against each
// reference — and that the block changes nothing on the way through.

#include "allocationtrap.h"
#include "blocks/vumeter.h"
#include "blocksizeinvariance.h"
#include "dsp/decibel.h"
#include "offlinerunner.h"
#include "rt/meter.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using namespace rb;
using namespace rb::blocks;
using namespace rb::dsp;
using namespace rb::test;

namespace
{

/// A sine whose peaks reach −18 dBFS: 0 VU at the default reference.
const float kReferenceSine = dbToGain(-18.0f);

/// How finely the needle is followed: a quarter of a millisecond.
constexpr std::size_t kStepFrames = 12;
constexpr double kStepSeconds = static_cast<double>(kStepFrames) / kTestSampleRate;

[[nodiscard]] MultiSignal stereoSine(std::size_t frames, double hz, float amplitude)
{
    MultiSignal signal;
    signal.push_back(sine(frames, hz, kTestSampleRate, amplitude));
    signal.push_back(sine(frames, hz, kTestSampleRate, amplitude));
    return signal;
}

/// The left needle every kStepFrames while \a input plays, from a block
/// that has heard nothing before it.
[[nodiscard]] std::vector<float> follow(VuMeter &block, BlockRunner &runner, const MultiSignal &input)
{
    std::vector<float> trace;
    const std::size_t frames = frameCount(input);
    for (std::size_t start = 0; start + kStepFrames <= frames; start += kStepFrames) {
        MultiSignal piece;
        for (const Signal &channel : input) {
            piece.emplace_back(channel.begin() + static_cast<std::ptrdiff_t>(start),
                               channel.begin() + static_cast<std::ptrdiff_t>(start + kStepFrames));
        }
        (void)runner.run(piece, CycleShape::fixed(kStepFrames));
        trace.push_back(block.level(0));
    }
    return trace;
}

/// The mean of the last \a seconds of \a trace: where the needle settled,
/// with the ripple of the rectified sine averaged out.
[[nodiscard]] double settledOf(const std::vector<float> &trace, double seconds)
{
    const auto count = static_cast<std::size_t>(seconds / kStepSeconds);
    double sum = 0.0;
    for (std::size_t i = trace.size() - count; i < trace.size(); ++i) {
        sum += trace[i];
    }
    return sum / static_cast<double>(count);
}

} // namespace

TEST(VuMeterTest, IsAMeterThatChangesNothing)
{
    const VuMeter block;
    EXPECT_EQ(block.info().typeId, "meter.vu");
    EXPECT_EQ(block.info().category, BlockCategory::Meters);
    EXPECT_EQ(block.info().face, FaceKind::Vu);
    EXPECT_EQ(faceKindName(block.info().face), "vu");
    EXPECT_EQ(block.info().latencySamples, 0U);
}

TEST(VuMeterTest, TheAudioComesOutTheWayItWentIn)
{
    VuMeter block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());

    MultiSignal input;
    input.push_back(whiteNoise(8192, 0.8f, 3));
    input.push_back(sine(8192, 997.0, kTestSampleRate, 1.5f)); // over full scale, and still untouched
    const MultiSignal out = runner.run(input, CycleShape::random(1, 512, 5));
    for (std::uint32_t channel = 0; channel < 2; ++channel) {
        EXPECT_EQ(out[channel], input[channel]) << "channel " << channel << " was not bit for bit what arrived";
    }
}

TEST(VuMeterTest, ASineEighteenDecibelsDownReadsZeroVuAtTheDefaultReference)
{
    VuMeter block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    EXPECT_EQ(block.param("reference").text(), "-18");
    EXPECT_FLOAT_EQ(block.referenceDb(), -18.0f);

    const std::vector<float> trace = follow(block, runner, stereoSine(96000, 1000.0, kReferenceSine));
    const double settled = settledOf(trace, 0.5);
    EXPECT_NEAR(gainToDb(static_cast<float>(settled)) - block.referenceDb(), 0.0f, 0.05f);
    EXPECT_NEAR(block.vu(0), 0.0f, 0.05f);
    EXPECT_NEAR(block.vu(1), 0.0f, 0.05f);
}

TEST(VuMeterTest, TheReferenceMovesZeroVuAndNothingElse)
{
    // The same sine against the other two references: +2 VU when 0 VU is
    // −20 dBFS, −4 VU when it is −14.
    for (const auto &[choice, expected] : {std::pair{"-20", 2.0f}, std::pair{"-14", -4.0f}}) {
        VuMeter block;
        ASSERT_TRUE(block.setParam("reference", ParamValue::ofEnum(choice)).has_value());
        BlockRunner runner(block, 2, 2);
        ASSERT_TRUE(runner.prepare().has_value());
        (void)runner.run(stereoSine(96000, 1000.0, kReferenceSine));
        EXPECT_NEAR(block.vu(0), expected, 0.05f) << "reference " << choice;
    }
}

TEST(VuMeterTest, ANeedleReachesNinetyNinePercentInThreeHundredMilliseconds)
{
    // **The VU characteristic**: a tone switched on at the reference level
    // takes the needle to 99 % of its reading in 300 ms (±10 %), and it
    // swings past by between 1 and 1.5 % before it settles.
    VuMeter block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());

    const std::vector<float> trace = follow(block, runner, stereoSine(2 * 48000, 1000.0, kReferenceSine));
    const double settled = settledOf(trace, 0.5);

    const auto reached = std::ranges::find_if(trace, [settled](float value) { return value >= 0.99 * settled; });
    ASSERT_NE(reached, trace.end()) << "the needle never reached 99 %";
    const double riseMs = static_cast<double>(std::distance(trace.begin(), reached) + 1) * kStepSeconds * 1000.0;
    EXPECT_NEAR(riseMs, 300.0, 30.0) << "rise time to 99 %";

    const double overshoot = static_cast<double>(*std::ranges::max_element(trace)) / settled - 1.0;
    EXPECT_GE(overshoot, 0.010) << "the needle does not swing past its reading as a VU needle does";
    EXPECT_LE(overshoot, 0.015) << "the needle swings too far past its reading";

    // Recorded for the issue: what the needle actually did.
    RecordProperty("riseMs", std::to_string(riseMs));
    RecordProperty("overshootPercent", std::to_string(overshoot * 100.0));
}

TEST(VuMeterTest, TheNeedleFallsBackAsItRose)
{
    // The same mechanism brings it down: from the reference level to 1 % of
    // it in about the time it took to rise, and never below the pin.
    VuMeter block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    (void)runner.run(stereoSine(96000, 1000.0, kReferenceSine));
    const float from = block.level(0);

    MultiSignal quiet;
    quiet.push_back(silence(96000));
    quiet.push_back(silence(96000));
    const std::vector<float> trace = follow(block, runner, quiet);
    const auto fallen = std::ranges::find_if(trace, [from](float value) { return value <= 0.01f * from; });
    ASSERT_NE(fallen, trace.end());
    const double fallMs = static_cast<double>(std::distance(trace.begin(), fallen) + 1) * kStepSeconds * 1000.0;
    EXPECT_NEAR(fallMs, 300.0, 30.0);
    EXPECT_GE(*std::ranges::min_element(trace), 0.0f) << "the needle went below the pin";
    EXPECT_EQ(block.level(0), 0.0f) << "two seconds of silence did not bring the needle to rest";
}

TEST(VuMeterTest, ItReadsTheSameAcrossTheBand)
{
    // A tone at the reference level reads 0 VU from the bass to the top of
    // the band: the rectifier does not care about frequency, and the ripple
    // it leaves is far above what the needle can follow.
    for (const double hz : {40.0, 100.0, 1000.0, 10000.0}) {
        VuMeter block;
        BlockRunner runner(block, 2, 2);
        ASSERT_TRUE(runner.prepare().has_value());
        const std::vector<float> trace = follow(block, runner, stereoSine(96000, hz, kReferenceSine));
        EXPECT_NEAR(gainToDb(static_cast<float>(settledOf(trace, 0.5))) + 18.0f, 0.0f, 0.1f) << hz << " Hz";
    }
}

TEST(VuMeterTest, EachSideHasItsOwnNeedle)
{
    VuMeter block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    MultiSignal input;
    input.push_back(sine(96000, 1000.0, kTestSampleRate, kReferenceSine));
    input.push_back(silence(96000));
    (void)runner.run(input);
    EXPECT_NEAR(block.vu(0), 0.0f, 0.05f);
    EXPECT_EQ(block.level(1), 0.0f);
}

TEST(VuMeterTest, ItPublishesWhatItMeasuredWhereTheInterfaceLooks)
{
    VuMeter block;
    rt::BlockMeter meter;
    BlockRunner runner(block, 2, 2);
    runner.setMeter(&meter);
    ASSERT_TRUE(runner.prepare().has_value());

    MultiSignal input;
    input.push_back(sine(96000, 1000.0, kTestSampleRate, kReferenceSine));
    input.push_back(sine(96000, 1000.0, kTestSampleRate, dbToGain(-24.0f)));
    (void)runner.run(input);

    EXPECT_FLOAT_EQ(meter.value(VuMeter::kLevelLeftValue), block.level(0));
    EXPECT_FLOAT_EQ(meter.value(VuMeter::kLevelRightValue), block.level(1));
    EXPECT_NEAR(gainToDb(meter.value(VuMeter::kLevelLeftValue)), -18.0f, 0.1f);
    EXPECT_NEAR(gainToDb(meter.value(VuMeter::kLevelRightValue)), -24.0f, 0.1f);
}

TEST(VuMeterTest, NobodyWatchingIsNotACrash)
{
    VuMeter block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const MultiSignal out = runner.run(stereoSine(1024, 1000.0, 0.5f));
    EXPECT_EQ(out[0].size(), 1024U);
}

TEST(VuMeterTest, ItSoundsTheSameWhateverTheQuantum)
{
    MultiSignal input;
    input.push_back(sine(8192, 997.0, kTestSampleRate, 0.4f));
    input.push_back(sine(8192, 1500.0, kTestSampleRate, 0.3f));

    InvarianceOptions options;
    options.inputChannels = 2;
    options.outputChannels = 2;
    const InvarianceReport report =
        checkBlockSizeInvariance([] { return std::unique_ptr<Block>(std::make_unique<VuMeter>()); }, input, options);
    EXPECT_TRUE(report.passed) << report.summary;
}

TEST(VuMeterTest, TheNeedleIsTheSameWhateverTheQuantum)
{
    // The audio is trivially the same; this is the needle, which is state
    // carried from one cycle to the next.
    const MultiSignal input = stereoSine(9600, 440.0, kReferenceSine);
    VuMeter even;
    BlockRunner evenRunner(even, 2, 2);
    ASSERT_TRUE(evenRunner.prepare().has_value());
    (void)evenRunner.run(input, CycleShape::fixed(256));

    VuMeter ragged;
    BlockRunner raggedRunner(ragged, 2, 2);
    ASSERT_TRUE(raggedRunner.prepare().has_value());
    (void)raggedRunner.run(input, CycleShape::random(1, 512, 9));

    EXPECT_EQ(even.level(0), ragged.level(0));
    EXPECT_EQ(even.level(1), ragged.level(1));
}

TEST(VuMeterTest, ARunTouchesTheAllocatorNotAtAll)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    VuMeter block;
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

TEST(VuMeterTest, WhatWasChosenIsWhatComesBack)
{
    VuMeter block;
    ASSERT_TRUE(block.setParam("reference", ParamValue::ofEnum("-14")).has_value());
    ASSERT_TRUE(block.setParam("showInTray", ParamValue::ofBool(true)).has_value());

    VuMeter opened;
    ASSERT_TRUE(opened.loadState(block.saveState()).has_value());
    EXPECT_EQ(opened.param("reference").text(), "-14");
    EXPECT_FLOAT_EQ(opened.referenceDb(), -14.0f);
    EXPECT_TRUE(*opened.param("showInTray").asBool());
    EXPECT_FALSE(block.setParam("reference", ParamValue::ofEnum("-12")).has_value())
        << "a reference the block does not offer was taken";
}
