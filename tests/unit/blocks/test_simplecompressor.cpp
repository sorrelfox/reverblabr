// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Simple Compressor block: one mode control over the Dynamics
// processor. What the curve and the detector do on their own is measured in
// tests/unit/dsp/test_dynamics.cpp; what is checked here is that each mode
// is the curve and the timing its table says, and that moving between modes
// is not heard.

#include "allocationtrap.h"
#include "analysis.h"
#include "blocks/simplecompressor.h"
#include "blocksizeinvariance.h"
#include "dsp/decibel.h"
#include "dsp/dynamics.h"
#include "offlinerunner.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <memory>
#include <string>

using namespace rb;
using namespace rb::blocks;
using namespace rb::dsp;
using namespace rb::test;

namespace
{

constexpr double kRate = kTestSampleRate;

void setMode(SimpleCompressor &block, std::string_view id)
{
    EXPECT_TRUE(block.setParam("mode", ParamValue::ofEnum(std::string{id})).has_value()) << id;
}

/// A steady level on both sides. **Constant, not a tone**, so that the level
/// the peak detector sees and the level the RMS one sees are the same number
/// and one curve answers for both kinds of mode.
[[nodiscard]] MultiSignal steady(std::size_t frames, float db)
{
    return spread(constant(frames, dbToGain(db)), 2);
}

/// What the block took off, sample by sample, in decibels, with the makeup
/// it added taken back out: the reduction as the processor is applying it.
[[nodiscard]] std::vector<float> reductionOf(const Signal &in, const Signal &out, float makeupDb)
{
    std::vector<float> reduction(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        reduction[i] = gainToDb(std::fabs(out[i]) / std::fabs(in[i])) - makeupDb;
    }
    return reduction;
}

/// How long, in milliseconds from \a from, the reduction took to cover
/// 1 − 1/e of the way from where it was to \a target: one time constant.
[[nodiscard]] double timeConstantMs(const std::vector<float> &reduction, std::size_t from, float target)
{
    const float start = reduction[from];
    const float mark = start + (target - start) * (1.0f - std::exp(-1.0f));
    for (std::size_t i = from; i < reduction.size(); ++i) {
        const bool arrived = target < start ? reduction[i] <= mark : reduction[i] >= mark;
        if (arrived) {
            return 1000.0 * static_cast<double>(i - from) / kRate;
        }
    }
    return 1.0e9;
}

} // namespace

TEST(SimpleCompressorTest, ItOffersItsModesByStableIdAndStartsGentle)
{
    const SimpleCompressor block;
    ASSERT_EQ(block.params().size(), 1U);
    const ParamDescriptor &mode = block.params()[0];
    EXPECT_EQ(mode.id, "mode");
    EXPECT_EQ(mode.type, ParamType::Enum);
    const std::vector<std::string> expected{"off", "gentle", "moderate", "heavy", "voice", "broadcast"};
    EXPECT_EQ(mode.choices, expected);

    // A compressor somebody has just put in a lane is there to compress.
    EXPECT_EQ(block.param("mode").text(), "gentle");
    EXPECT_EQ(block.mode().id, "gentle");
    EXPECT_EQ(block.info().face, FaceKind::Text);
    EXPECT_EQ(block.info().latencySamples, 0U);
}

TEST(SimpleCompressorTest, EachModeIsTheCurveItsTableSays)
{
    // The criterion: a steady level comes out where dsp::outputDb() says for
    // the mode's settings — the same function the Dynamics block's curve
    // editor draws, so a mode and the same numbers typed into that block
    // cannot sound different.
    for (const SimpleCompressor::Mode &mode : SimpleCompressor::modes()) {
        for (const float inputDb : {-50.0f, -30.0f, -20.0f, -12.0f, -6.0f, 0.0f}) {
            // Chosen once the block is running, as a person chooses it.
            SimpleCompressor block;
            BlockRunner runner(block, 2, 2);
            ASSERT_TRUE(runner.prepare().has_value());
            setMode(block, mode.id);
            const MultiSignal out = runner.run(steady(48000, inputDb), CycleShape::fixed(256));
            const float measured = gainToDb(std::fabs(out[0].back()));
            EXPECT_NEAR(measured, outputDb(inputDb, mode.settings), 0.05) << mode.id << " at " << inputDb << " dBFS in";
            EXPECT_FLOAT_EQ(out[0].back(), out[1].back()) << mode.id << ": the sides were held back differently";
        }
    }
}

TEST(SimpleCompressorTest, TheModesAreTheOnesWeTuned)
{
    // Written out once more, so that retuning a mode is a change somebody
    // sees in a review, not something a table edit does quietly.
    struct Expected {
        std::string_view id;
        float threshold, ratio, knee, makeup, attack, release;
        DynamicsDetector detector;
    };
    const std::array<Expected, 6> table{{
        {"off", 0.0f, 1.0f, 0.0f, 0.0f, 10.0f, 100.0f, DynamicsDetector::Peak},
        {"gentle", -18.0f, 2.0f, 12.0f, 3.0f, 20.0f, 250.0f, DynamicsDetector::Rms},
        {"moderate", -20.0f, 3.0f, 10.0f, 5.0f, 10.0f, 200.0f, DynamicsDetector::Rms},
        {"heavy", -24.0f, 6.0f, 6.0f, 8.0f, 5.0f, 150.0f, DynamicsDetector::Peak},
        {"voice", -22.0f, 4.0f, 8.0f, 6.0f, 3.0f, 120.0f, DynamicsDetector::Peak},
        {"broadcast", -16.0f, 10.0f, 4.0f, 6.0f, 1.0f, 80.0f, DynamicsDetector::Peak},
    }};
    for (const Expected &row : table) {
        SimpleCompressor block;
        setMode(block, row.id);
        const DynamicsSettings settings = block.settings();
        const DynamicsTiming timing = block.timing();
        EXPECT_FLOAT_EQ(settings.thresholdDb, row.threshold) << row.id;
        EXPECT_FLOAT_EQ(settings.ratio, row.ratio) << row.id;
        EXPECT_FLOAT_EQ(settings.kneeDb, row.knee) << row.id;
        EXPECT_FLOAT_EQ(settings.makeupDb, row.makeup) << row.id;
        EXPECT_FLOAT_EQ(timing.attackMs, row.attack) << row.id;
        EXPECT_FLOAT_EQ(timing.releaseMs, row.release) << row.id;
        EXPECT_EQ(timing.detector, row.detector) << row.id;
        // No mode reaches below the compressor into the expander.
        EXPECT_FLOAT_EQ(settings.expanderRatio, 1.0f) << row.id;

        // **Nothing clips a clean signal**: a full-scale input comes out at
        // −6 dBFS or below in every mode that compresses at all.
        if (row.id != "off") {
            EXPECT_LE(outputDb(0.0f, settings), -6.0f + 1.0e-4f) << row.id;
        }
    }
}

TEST(SimpleCompressorTest, OffLeavesTheSoundExactlyAsItArrived)
{
    SimpleCompressor block;
    setMode(block, "off");
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    MultiSignal input;
    input.push_back(sine(24000, 997.0, kRate, 1.0f));
    input.push_back(pinkNoise(24000, 0.7f, 5));
    const MultiSignal out = runner.run(input, CycleShape::fixed(256));
    EXPECT_EQ(out[0], input[0]);
    EXPECT_EQ(out[1], input[1]);
}

TEST(SimpleCompressorTest, EachModeTakesTheTimeItsTableSays)
{
    // The attack and the release measured through the block: a step up from
    // below the knee to full scale, then down a few decibels. **Down to
    // another level, not to silence**, because the detector has to fall as
    // well, and between two nearby levels it has settled in a few
    // milliseconds and what is left is the release itself.
    //
    // What is allowed on top of each constant is the detector's own delay:
    // the peak detector's sliding window (at most two of them), or the RMS
    // detector's average finding the new level.
    for (const SimpleCompressor::Mode &mode : SimpleCompressor::modes()) {
        if (mode.id == "off") {
            continue;
        }
        const DynamicsSettings &settings = mode.settings;
        const DynamicsTiming &timing = mode.timing;
        const double detectorMs = timing.detector == DynamicsDetector::Rms ? 3.0 * timing.rmsMs : 2.0 * kPeakWindowMs;

        const float quiet = settings.thresholdDb - settings.kneeDb - 10.0f;
        const float loud = 0.0f;
        const float lessLoud = -6.0f;
        const std::size_t second = static_cast<std::size_t>(kRate);

        Signal level = constant(second, dbToGain(quiet));
        const Signal up = constant(second, dbToGain(loud));
        const Signal down = constant(second, dbToGain(lessLoud));
        level.insert(level.end(), up.begin(), up.end());
        level.insert(level.end(), down.begin(), down.end());

        // **Chosen after the block is running**, as it is in a session: a
        // mode that only took effect when the block was prepared would show
        // the default's timing here.
        SimpleCompressor block;
        BlockRunner runner(block, 2, 2);
        ASSERT_TRUE(runner.prepare().has_value());
        setMode(block, mode.id);
        const MultiSignal out = runner.run(spread(level, 2), CycleShape::fixed(256));
        const std::vector<float> reduction = reductionOf(level, out[0], settings.makeupDb);

        const double attack = timeConstantMs(reduction, second, gainReductionDb(loud, settings));
        EXPECT_GE(attack, 0.9 * timing.attackMs) << mode.id;
        EXPECT_LE(attack, 1.1 * timing.attackMs + detectorMs) << mode.id;

        const double release = timeConstantMs(reduction, 2 * second, gainReductionDb(lessLoud, settings));
        EXPECT_GE(release, 0.9 * timing.releaseMs) << mode.id;
        EXPECT_LE(release, 1.1 * timing.releaseMs + detectorMs) << mode.id;
    }
}

TEST(SimpleCompressorTest, SwitchingModesIsNotHeard)
{
    // The criterion: a change of mode is a change of where the gain is
    // heading, and the mode's attack or release takes it there, with the
    // makeup gliding on its own. **Through every mode and back to Off**,
    // which is the change a bypass would make as a jump.
    //
    // A steady level makes every step in the output a step in the gain, so
    // the largest one is the click, if there is one. One signal across all
    // the cycles, so a step at a join between two of them is seen.
    SimpleCompressor block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const MultiSignal input = steady(256, -10.0f);

    std::vector<std::string> order;
    for (const SimpleCompressor::Mode &mode : SimpleCompressor::modes()) {
        order.emplace_back(mode.id);
    }
    order.emplace_back("off");
    order.emplace_back("broadcast");
    order.emplace_back("gentle");

    Signal whole;
    for (std::size_t cycle = 0; cycle < order.size() * 60; ++cycle) {
        if (cycle % 60 == 30) {
            ASSERT_TRUE(block.setParam("mode", ParamValue::ofEnum(order[cycle / 60])).has_value());
        }
        const MultiSignal out = runner.run(input, CycleShape::fixed(256));
        if (cycle > 0) {
            whole.insert(whole.end(), out[0].begin(), out[0].end());
        }
    }
    const float worst = maxSampleStep(whole);
    EXPECT_LT(worst, 0.01f) << "a change of mode stepped the signal by " << worst;
}

TEST(SimpleCompressorTest, ItSoundsTheSameWhateverTheQuantum)
{
    MultiSignal input;
    input.push_back(sine(16384, 997.0, kTestSampleRate, 0.9f));
    input.push_back(sine(16384, 1500.0, kTestSampleRate, 0.3f));

    InvarianceOptions options;
    options.inputChannels = 2;
    options.outputChannels = 2;
    // One mode of each detector, both working hard.
    for (const std::string_view id : {"broadcast", "moderate"}) {
        const InvarianceReport report = checkBlockSizeInvariance(
            [id] {
                auto block = std::make_unique<SimpleCompressor>();
                EXPECT_TRUE(block->setParam("mode", ParamValue::ofEnum(std::string{id})).has_value());
                return std::unique_ptr<Block>(std::move(block));
            },
            input,
            options);
        EXPECT_TRUE(report.passed) << id << ": " << report.summary;
    }
}

TEST(SimpleCompressorTest, WhatWasSetIsWhatComesBack)
{
    SimpleCompressor block;
    setMode(block, "voice");
    SimpleCompressor opened;
    ASSERT_TRUE(opened.loadState(block.saveState()).has_value());
    EXPECT_EQ(opened.param("mode").text(), "voice");
    EXPECT_EQ(opened.mode().id, "voice");

    // A mode this build does not have is refused rather than guessed at.
    SimpleCompressor other;
    EXPECT_FALSE(other.setParam("mode", ParamValue::ofEnum("loudest")).has_value());
    EXPECT_EQ(other.mode().id, "gentle");

    EXPECT_NE(SimpleCompressor::findMode("heavy"), nullptr);
    EXPECT_EQ(SimpleCompressor::findMode("loudest"), nullptr);
}

TEST(SimpleCompressorTest, ARunTouchesTheAllocatorNotAtAll)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    SimpleCompressor block;
    setMode(block, "gentle");
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    MultiSignal input;
    input.push_back(sine(9600, 220.0, kTestSampleRate, 1.0f));
    input.push_back(sine(9600, 220.0, kTestSampleRate, 1.0f));

    std::size_t allocations = 1;
    const MultiSignal out = runner.runWatchingForAllocations(input, CycleShape::random(1, 512, 3), allocations);
    EXPECT_EQ(allocations, 0U);
    EXPECT_EQ(out[0].size(), input[0].size());

    // And a change of mode on the audio thread, which is a number and
    // nothing else.
    allocations = 1;
    block.applyParam(SimpleCompressor::ModeParam, 5.0f);
    (void)runner.runWatchingForAllocations(input, CycleShape::fixed(128), allocations);
    EXPECT_EQ(allocations, 0U);
    EXPECT_EQ(block.mode().id, "broadcast");
}
