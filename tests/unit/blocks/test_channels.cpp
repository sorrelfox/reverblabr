// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Channels block: each action is a 2×2 matrix, checked by what it
// does to a different signal on each side, and a change of action glides.

#include "allocationtrap.h"
#include "analysis.h"
#include "blocks/channels.h"
#include "blocksizeinvariance.h"
#include "dsp/decibel.h"
#include "offlinerunner.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

using namespace rb;
using namespace rb::blocks;
using namespace rb::dsp;
using namespace rb::test;

namespace
{

constexpr std::size_t kFrames = 2048;

/// Two signals nothing could mistake for each other: a constant on the left
/// and a sine on the right, so each output sample says exactly how much of
/// each side went into it.
[[nodiscard]] MultiSignal distinctSides()
{
    return MultiSignal{constant(kFrames, 0.3f), sine(kFrames, 1499.0, kTestSampleRate, 0.5f)};
}

[[nodiscard]] MultiSignal through(Channels &block, const MultiSignal &input)
{
    BlockRunner runner(block, 2, 2);
    EXPECT_TRUE(runner.prepare().has_value());
    return runner.run(input, CycleShape::fixed(256));
}

/// What \a left and \a right weights of the two input sides add up to.
[[nodiscard]] Signal mix(const MultiSignal &input, float left, float right)
{
    Signal out(input[0].size());
    for (std::size_t frame = 0; frame < out.size(); ++frame) {
        out[frame] = left * input[0][frame] + right * input[1][frame];
    }
    return out;
}

} // namespace

TEST(ChannelsTest, EveryActionDoesWhatItsNameSays)
{
    // The criterion, written out by hand rather than read back from the
    // table it is checking: which input side, and how much of it, reaches
    // each output side.
    struct Expected {
        std::string id;
        float leftFromLeft, leftFromRight, rightFromLeft, rightFromRight;
    };
    const std::vector<Expected> table{
        {"none", 1, 0, 0, 1},
        {"swap", 0, 1, 1, 0},
        {"mono", 0.5f, 0.5f, 0.5f, 0.5f},
        {"leftToBoth", 1, 0, 1, 0},
        {"rightToBoth", 0, 1, 0, 1},
        {"muteLeft", 0, 0, 0, 1},
        {"muteRight", 1, 0, 0, 0},
        {"invertLeft", -1, 0, 0, 1},
        {"invertRight", 1, 0, 0, -1},
        {"difference", 0.5f, -0.5f, 0.5f, -0.5f},
    };
    ASSERT_EQ(table.size(), Channels::actions().size()) << "an action is missing from this test";

    const MultiSignal input = distinctSides();
    for (const Expected &expected : table) {
        Channels block;
        ASSERT_TRUE(block.setParam("action", ParamValue::ofEnum(expected.id)).has_value()) << expected.id;
        const MultiSignal out = through(block, input);
        EXPECT_LE(nullTestDb(out[0], mix(input, expected.leftFromLeft, expected.leftFromRight)), kSilenceDb)
            << "the left side of " << expected.id;
        EXPECT_LE(nullTestDb(out[1], mix(input, expected.rightFromLeft, expected.rightFromRight)), kSilenceDb)
            << "the right side of " << expected.id;
    }
}

TEST(ChannelsTest, NoChangeIsTheInputBitForBit)
{
    Channels block;
    const MultiSignal input = distinctSides();
    const MultiSignal out = through(block, input);
    EXPECT_EQ(out[0], input[0]);
    EXPECT_EQ(out[1], input[1]);
    EXPECT_EQ(block.info().latencySamples, 0U);
}

TEST(ChannelsTest, MonoAndDifferenceAddBackUpToTheLeft)
{
    // (L+R)/2 + (L−R)/2 = L: the two folds are halves of one thing.
    const MultiSignal input = distinctSides();
    Channels mono;
    ASSERT_TRUE(mono.setParam("action", ParamValue::ofEnum("mono")).has_value());
    Channels difference;
    ASSERT_TRUE(difference.setParam("action", ParamValue::ofEnum("difference")).has_value());
    const MultiSignal sum = through(mono, input);
    const MultiSignal side = through(difference, input);
    Signal back(kFrames);
    for (std::size_t frame = 0; frame < kFrames; ++frame) {
        back[frame] = sum[0][frame] + side[0][frame];
    }
    EXPECT_LE(nullTestDb(back, input[0]), kSilenceDb);
}

TEST(ChannelsTest, SwitchingActionsDoesNotClick)
{
    // A steady level on each side and every action in turn, each held for a
    // few cycles, then straight back to the first. **One signal across the
    // cycles**: a change made between two cycles steps at the join.
    Channels block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const MultiSignal input{Signal(256, 0.4f), Signal(256, -0.3f)};

    Signal left;
    Signal right;
    const auto cycles = [&](int count) {
        for (int each = 0; each < count; ++each) {
            const MultiSignal out = runner.run(input, CycleShape::fixed(256));
            left.insert(left.end(), out[0].begin(), out[0].end());
            right.insert(right.end(), out[1].begin(), out[1].end());
        }
    };
    cycles(2);
    for (const Channels::Action &action : Channels::actions()) {
        ASSERT_TRUE(block.setParam("action", ParamValue::ofEnum(std::string{action.id})).has_value());
        cycles(3);
    }
    ASSERT_TRUE(block.setParam("action", ParamValue::ofEnum("none")).has_value());
    cycles(3);

    // The biggest jump any action makes is 0.8 (inverting the left, 0.4 to
    // −0.4). Over 10 ms, 480 samples at 48 kHz, that is 0.0017 a sample.
    EXPECT_LT(maxSampleStep(left), 0.002f) << "the left side stepped";
    EXPECT_LT(maxSampleStep(right), 0.002f) << "the right side stepped";
    // And it arrived back where it started.
    EXPECT_EQ(left.back(), 0.4f);
    EXPECT_EQ(right.back(), -0.3f);
}

TEST(ChannelsTest, AChangeTakesTheCrossfadeAndNoLonger)
{
    Channels block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    ASSERT_TRUE(block.setParam("action", ParamValue::ofEnum("muteLeft")).has_value());
    const MultiSignal out = runner.run(MultiSignal{Signal(1024, 0.5f), Signal(1024, 0.5f)}, CycleShape::fixed(1024));
    // 10 ms is 480 samples: half-way there at 240, silent from 480 on.
    EXPECT_NEAR(out[0][239], 0.25f, 0.002f);
    EXPECT_EQ(out[0][480], 0.0f);
    EXPECT_EQ(out[0][1023], 0.0f);
}

TEST(ChannelsTest, ItSoundsTheSameWhateverTheQuantum)
{
    InvarianceOptions options;
    options.inputChannels = 2;
    options.outputChannels = 2;
    MultiSignal input{sine(8192, 997.0, kTestSampleRate, 0.4f), sine(8192, 1499.0, kTestSampleRate, 0.3f)};
    for (const char *id : {"swap", "difference"}) {
        const InvarianceReport report = checkBlockSizeInvariance(
            [id] {
                auto block = std::make_unique<Channels>();
                EXPECT_TRUE(block->setParam("action", ParamValue::ofEnum(id)).has_value());
                return std::unique_ptr<Block>(std::move(block));
            },
            input,
            options);
        EXPECT_TRUE(report.passed) << id << ": " << report.summary;
    }
}

TEST(ChannelsTest, ARunTouchesTheAllocatorNotAtAll)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    Channels block;
    ASSERT_TRUE(block.setParam("action", ParamValue::ofEnum("invertRight")).has_value());
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());

    std::size_t allocations = 1;
    const MultiSignal input{sine(9600, 997.0, kTestSampleRate, 0.4f), sine(9600, 1499.0, kTestSampleRate, 0.3f)};
    const MultiSignal out = runner.runWatchingForAllocations(input, CycleShape::random(1, 512, 3), allocations);
    EXPECT_EQ(allocations, 0U);
    EXPECT_EQ(out[0].size(), input[0].size());
}

TEST(ChannelsTest, WhatWasSetIsWhatComesBackAndAnUnknownActionIsRefused)
{
    Channels block;
    ASSERT_TRUE(block.setParam("action", ParamValue::ofEnum("rightToBoth")).has_value());
    Channels opened;
    ASSERT_TRUE(opened.loadState(block.saveState()).has_value());
    EXPECT_EQ(opened.param("action").text(), "rightToBoth");
    EXPECT_FALSE(block.setParam("action", ParamValue::ofEnum("sideways")).has_value());
}

TEST(ChannelsTest, OpeningASavedActionIsHeardAtOnceNotFadedTo)
{
    Channels block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    Channels saved;
    ASSERT_TRUE(saved.setParam("action", ParamValue::ofEnum("muteLeft")).has_value());
    ASSERT_TRUE(block.loadState(saved.saveState()).has_value());
    const MultiSignal out = runner.run(MultiSignal{Signal(64, 0.5f), Signal(64, 0.5f)}, CycleShape::fixed(64));
    EXPECT_EQ(peak(out[0]), 0.0f);
}
