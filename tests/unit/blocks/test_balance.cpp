// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Balance block: one side of a stereo pair trimmed against the
// other, with the stereo picture left where it is.

#include "allocationtrap.h"
#include "analysis.h"
#include "blocks/balance.h"
#include "blocksizeinvariance.h"
#include "dsp/decibel.h"
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

/// A different tone on each side, so that a side which turned up on the
/// other would be found there.
[[nodiscard]] MultiSignal twoTones(std::size_t frames, float amplitude = 0.5f)
{
    MultiSignal signal;
    signal.push_back(sine(frames, 997.0, kTestSampleRate, amplitude));
    signal.push_back(sine(frames, 1499.0, kTestSampleRate, amplitude));
    return signal;
}

[[nodiscard]] MultiSignal through(Balance &block, const MultiSignal &input)
{
    BlockRunner runner(block, 2, 2);
    EXPECT_TRUE(runner.prepare().has_value());
    return runner.run(input, CycleShape::fixed(256));
}

/// What a side came out at against what went in, in dB.
[[nodiscard]] double changeDb(const Signal &out, const Signal &in)
{
    return static_cast<double>(peakDb(out)) - static_cast<double>(peakDb(in));
}

} // namespace

TEST(BalanceTest, TheGainLawAtTheEndsTheMiddleAndHalfWay)
{
    // The criterion: −100, −50, 0, +50 and +100. The near side stays at
    // unity everywhere; the far side is at half its amplitude half-way and
    // gone at the end.
    struct Expected {
        float position;
        double leftDb; ///< kSilenceDb stands for "gone".
        double rightDb;
    };
    const double silent = static_cast<double>(kSilenceDb);
    for (const Expected &expected : {Expected{-100.0f, 0.0, silent},
                                     Expected{-50.0f, 0.0, -6.0206},
                                     Expected{0.0f, 0.0, 0.0},
                                     Expected{50.0f, -6.0206, 0.0},
                                     Expected{100.0f, silent, 0.0}}) {
        Balance block;
        ASSERT_TRUE(block.setParam("balance", ParamValue::ofFloat(expected.position)).has_value());
        const MultiSignal input = twoTones(8192);
        const MultiSignal out = through(block, input);

        if (expected.leftDb == silent) {
            EXPECT_EQ(peak(out[0]), 0.0f) << "the left side was not silenced at " << expected.position;
        } else {
            EXPECT_NEAR(changeDb(out[0], input[0]), expected.leftDb, 0.01) << "left at " << expected.position;
        }
        if (expected.rightDb == silent) {
            EXPECT_EQ(peak(out[1]), 0.0f) << "the right side was not silenced at " << expected.position;
        } else {
            EXPECT_NEAR(changeDb(out[1], input[1]), expected.rightDb, 0.01) << "right at " << expected.position;
        }
    }
}

TEST(BalanceTest, AtTheCentreItIsTheInputAndNothingElse)
{
    Balance block;
    const MultiSignal input = twoTones(4096, 0.9f);
    const MultiSignal out = through(block, input);
    EXPECT_EQ(block.info().latencySamples, 0U);
    for (std::uint32_t channel = 0; channel < 2; ++channel) {
        EXPECT_LE(nullTestDb(out[channel], input[channel]), kSilenceDb) << "channel " << channel << " was changed";
    }
}

TEST(BalanceTest, EachSideStaysOnItsOwnSide)
{
    // Balance is not Pan: however far it is turned, sound on one side never
    // reaches the other. A tone on one side only, and not a sample of it on
    // the other.
    for (const float position : {-100.0f, -30.0f, 30.0f, 100.0f}) {
        const Signal tone = sine(8192, 997.0, kTestSampleRate, 0.5f);
        const Signal quiet = silence(8192);

        Balance leftOnly;
        ASSERT_TRUE(leftOnly.setParam("balance", ParamValue::ofFloat(position)).has_value());
        EXPECT_EQ(peak(through(leftOnly, MultiSignal{tone, quiet})[1]), 0.0f)
            << "the left side reached the right at " << position;

        Balance rightOnly;
        ASSERT_TRUE(rightOnly.setParam("balance", ParamValue::ofFloat(position)).has_value());
        EXPECT_EQ(peak(through(rightOnly, MultiSignal{quiet, tone})[0]), 0.0f)
            << "the right side reached the left at " << position;
    }
}

TEST(BalanceTest, SweepingTheControlDoesNotStepTheSignal)
{
    // A steady level on both sides, and the control dragged across its whole
    // range a little every cycle and then thrown from one end to the other.
    // **One signal across the cycles**: a setting changed between
    // two cycles steps, if it steps at all, at the join.
    Balance block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const MultiSignal input{Signal(256, 0.5f), Signal(256, 0.5f)};

    Signal left;
    Signal right;
    const auto cycle = [&] {
        const MultiSignal out = runner.run(input, CycleShape::fixed(256));
        left.insert(left.end(), out[0].begin(), out[0].end());
        right.insert(right.end(), out[1].begin(), out[1].end());
    };
    for (int position = -100; position <= 100; position += 10) {
        ASSERT_TRUE(block.setParam("balance", ParamValue::ofFloat(static_cast<float>(position))).has_value());
        cycle();
    }
    ASSERT_TRUE(block.setParam("balance", ParamValue::ofFloat(-100.0f)).has_value());
    for (int count = 0; count < 8; ++count) {
        cycle();
    }

    // Throwing it end to end moves a side by the whole 0.5 over 20 ms: 960
    // samples at 48 kHz, so about 0.0005 a sample. A step is 0.5 at once.
    EXPECT_LT(maxSampleStep(left), 0.001f) << "the left side stepped";
    EXPECT_LT(maxSampleStep(right), 0.001f) << "the right side stepped";
    // And it got there: the right side is gone.
    EXPECT_EQ(right.back(), 0.0f);
    EXPECT_EQ(left.back(), 0.5f);
}

TEST(BalanceTest, ItSoundsTheSameWhateverTheQuantum)
{
    // Moved off the centre, so the gains are doing something.
    const MultiSignal input = twoTones(8192, 0.4f);
    InvarianceOptions options;
    options.inputChannels = 2;
    options.outputChannels = 2;
    const InvarianceReport report = checkBlockSizeInvariance(
        [] {
            auto block = std::make_unique<Balance>();
            EXPECT_TRUE(block->setParam("balance", ParamValue::ofFloat(-35.0f)).has_value());
            return std::unique_ptr<Block>(std::move(block));
        },
        input,
        options);
    EXPECT_TRUE(report.passed) << report.summary;
}

TEST(BalanceTest, ARunTouchesTheAllocatorNotAtAll)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    Balance block;
    ASSERT_TRUE(block.setParam("balance", ParamValue::ofFloat(60.0f)).has_value());
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());

    std::size_t allocations = 1;
    const MultiSignal input = twoTones(9600);
    const MultiSignal out = runner.runWatchingForAllocations(input, CycleShape::random(1, 512, 3), allocations);
    EXPECT_EQ(allocations, 0U);
    EXPECT_EQ(out[0].size(), input[0].size());
}

TEST(BalanceTest, WhatWasSetIsWhatComesBack)
{
    Balance block;
    ASSERT_TRUE(block.setParam("balance", ParamValue::ofFloat(-42.0f)).has_value());

    Balance opened;
    ASSERT_TRUE(opened.loadState(block.saveState()).has_value());
    EXPECT_FLOAT_EQ(*opened.param("balance").asFloat(), -42.0f);

    // Past either end is refused rather than taken quietly.
    EXPECT_FALSE(block.setParam("balance", ParamValue::ofFloat(101.0f)).has_value());
    EXPECT_FALSE(block.setParam("balance", ParamValue::ofFloat(-101.0f)).has_value());
}

TEST(BalanceTest, OpeningASavedSettingIsHeardAtOnceNotGlidedTo)
{
    // A session opened with the balance hard left starts hard left: the
    // glide is for a hand on the control, not for a file being read.
    Balance block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    Balance saved;
    ASSERT_TRUE(saved.setParam("balance", ParamValue::ofFloat(-100.0f)).has_value());
    ASSERT_TRUE(block.loadState(saved.saveState()).has_value());

    const MultiSignal out = runner.run(MultiSignal{Signal(64, 0.5f), Signal(64, 0.5f)}, CycleShape::fixed(64));
    EXPECT_EQ(peak(out[1]), 0.0f);
}
