// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// One ear at a time: a block set to one side changes that side only,
// and passes the other through late by exactly its own latency, so two
// blocks for the two ears keep them sample-aligned. Changing the ear while
// the sound plays fades across rather than clicking.

#include "allocationtrap.h"
#include "analysis.h"
#include "blocksizeinvariance.h"
#include "dsp/decibel.h"
#include "dsp/ear.h"
#include "earblock.h"
#include "offlinerunner.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <optional>

using namespace rb;
using namespace rb::dsp;
using namespace rb::test;

namespace
{

constexpr std::size_t kFrames = 8192;

[[nodiscard]] MultiSignal through(OneEarBlock &block, const MultiSignal &input)
{
    BlockRunner runner(block, 2, 2);
    EXPECT_TRUE(runner.prepare().has_value());
    return runner.run(input, CycleShape::random(32, 512, 7));
}

[[nodiscard]] Signal delayed(const Signal &signal, std::size_t by, float gain = 1.0f)
{
    Signal out(signal.size(), 0.0f);
    for (std::size_t frame = by; frame < signal.size(); ++frame) {
        out[frame] = signal[frame - by] * gain;
    }
    return out;
}

/// Where the first sample above half of \a signal's peak is.
[[nodiscard]] std::optional<std::size_t> clickAt(const Signal &signal)
{
    const float threshold = 0.5f * peak(signal);
    for (std::size_t frame = 0; frame < signal.size(); ++frame) {
        if (threshold > 0.0f && std::abs(signal[frame]) >= threshold) {
            return frame;
        }
    }
    return std::nullopt;
}

} // namespace

TEST(EarParam, IsTheSameOnEveryBlockAndKeptOutOfPresets)
{
    const ParamDescriptor ear = earParam();
    EXPECT_EQ(ear.id, "ear");
    EXPECT_EQ(ear.type, ParamType::Enum);
    ASSERT_EQ(ear.choices.size(), 2U);
    EXPECT_EQ(ear.choices[0], "left");
    EXPECT_EQ(ear.choices[1], "right");
    EXPECT_EQ(ear.defaultValue, ParamValue::ofEnum("left"));
    EXPECT_FALSE(ear.flags.inPresets) << "a preset for one ear would move the other block";
    EXPECT_FALSE(ear.flags.automatable) << "a change of ear is a fade across, not a glide";
    EXPECT_EQ(earFromNumber(0.0f), Ear::Left);
    EXPECT_EQ(earFromNumber(1.0f), Ear::Right);
}

TEST(EarRouter, ABlockChangesItsOwnSideAndPassesTheOtherThroughExactlyItsLatencyLate)
{
    const Signal leftIn = whiteNoise(kFrames, 0.5f, 11);
    const Signal rightIn = whiteNoise(kFrames, 0.5f, 12);
    for (const char *ear : {"left", "right"}) {
        const bool left = std::string_view{ear} == "left";
        OneEarBlock block(97);
        ASSERT_TRUE(block.setParam("ear", ParamValue::ofEnum(ear)).has_value());
        ASSERT_TRUE(block.setParam("gainDb", ParamValue::ofFloat(-6.0f)).has_value());
        const MultiSignal out = through(block, MultiSignal{leftIn, rightIn});
        const float gain = dbToGain(-6.0f);

        const Signal &processed = left ? out[0] : out[1];
        const Signal &passed = left ? out[1] : out[0];
        const Signal &processedIn = left ? leftIn : rightIn;
        const Signal &passedIn = left ? rightIn : leftIn;
        // **Exactly**: the other ear is the input, not something like it.
        EXPECT_EQ(passed, delayed(passedIn, 97)) << "the " << (left ? "right" : "left") << " side was not passed "
                                                 << "through 97 samples late, untouched";
        EXPECT_TRUE(nulls(processed, delayed(processedIn, 97, gain))) << "the " << ear << " side was not processed";
    }
}

TEST(EarRouter, TwoBlocksInSeriesKeepTheEarsSampleAligned)
{
    // A click on both sides, through a left-ear block and then a right-ear
    // one with different latencies: each side is late by both blocks, so
    // the two clicks come out in the same sample.
    const Signal click = impulse(kFrames, 100, 1.0f);
    OneEarBlock left(64);
    OneEarBlock right(250);
    ASSERT_TRUE(left.setParam("ear", ParamValue::ofEnum("left")).has_value());
    ASSERT_TRUE(right.setParam("ear", ParamValue::ofEnum("right")).has_value());

    const MultiSignal once = through(left, MultiSignal{click, click});
    const MultiSignal twice = through(right, once);
    const std::optional<std::size_t> leftClick = clickAt(twice[0]);
    const std::optional<std::size_t> rightClick = clickAt(twice[1]);
    ASSERT_TRUE(leftClick.has_value());
    ASSERT_TRUE(rightClick.has_value());
    EXPECT_EQ(*leftClick, 100U + 64U + 250U);
    EXPECT_EQ(*rightClick, *leftClick) << "the ears came out " << (*rightClick - *leftClick) << " samples apart";
}

TEST(EarRouter, ChangingTheEarWhilePlayingFadesAcrossWithoutAClick)
{
    // A steady level on both sides and a 12 dB cut on the block's ear. The
    // ear is thrown from left to right between two cycles. **One signal
    // across the cycles**: a change that steps, steps at the join.
    OneEarBlock block(64);
    ASSERT_TRUE(block.setParam("gainDb", ParamValue::ofFloat(-12.0f)).has_value());
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const MultiSignal input{Signal(256, 0.5f), Signal(256, 0.5f)};

    Signal left;
    Signal right;
    const auto cycle = [&](bool keep) {
        const MultiSignal out = runner.run(input, CycleShape::fixed(256));
        if (keep) {
            left.insert(left.end(), out[0].begin(), out[0].end());
            right.insert(right.end(), out[1].begin(), out[1].end());
        }
    };
    // Past the block's latency, so the start of the delay lines is not what
    // is being measured.
    cycle(false);
    for (int count = 0; count < 4; ++count) {
        cycle(true);
    }
    ASSERT_TRUE(block.setParam("ear", ParamValue::ofEnum("right")).has_value());
    for (int count = 0; count < 12; ++count) {
        cycle(true);
    }

    // The cut moves a side by 0.5 − 0.126 over 10 ms each way: 480 samples
    // at 48 kHz, well under 0.001 a sample. A jump is the whole 0.37.
    std::printf("[measure] a change of ear: the largest step %.5f on the left, %.5f on the right\n",
                static_cast<double>(maxSampleStep(left)),
                static_cast<double>(maxSampleStep(right)));
    EXPECT_LT(maxSampleStep(left), 0.001f) << "the left side clicked";
    EXPECT_LT(maxSampleStep(right), 0.001f) << "the right side clicked";
    // And it got there: the right side is the one cut now, the left passed.
    EXPECT_EQ(block.router().ear(), Ear::Right);
    EXPECT_FLOAT_EQ(left.back(), 0.5f);
    EXPECT_NEAR(right.back(), 0.5f * dbToGain(-12.0f), 1e-6f);
    EXPECT_EQ(block.resets(), 1U) << "the block was not told to forget the other ear";
}

TEST(EarRouter, ItSoundsTheSameWhateverTheQuantum)
{
    MultiSignal input{sine(kFrames, 997.0, kTestSampleRate, 0.4f), sine(kFrames, 1499.0, kTestSampleRate, 0.4f)};
    InvarianceOptions options;
    options.inputChannels = 2;
    options.outputChannels = 2;
    for (const char *ear : {"left", "right"}) {
        const InvarianceReport report = checkBlockSizeInvariance(
            [ear] {
                auto block = std::make_unique<OneEarBlock>(150);
                EXPECT_TRUE(block->setParam("ear", ParamValue::ofEnum(ear)).has_value());
                return std::unique_ptr<Block>(std::move(block));
            },
            input,
            options);
        EXPECT_TRUE(report.passed) << ear << ": " << report.summary;
    }
}

TEST(EarRouter, NothingIsAllocatedOnTheAudioThreadEvenWhileTheEarChanges)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    OneEarBlock block(64);
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const MultiSignal input{whiteNoise(4096, 0.5f, 3), whiteNoise(4096, 0.5f, 4)};
    std::size_t before = 0;
    (void)runner.runWatchingForAllocations(input, CycleShape::random(16, 1024, 5), before);
    ASSERT_TRUE(block.setParam("ear", ParamValue::ofEnum("right")).has_value());
    std::size_t during = 0;
    (void)runner.runWatchingForAllocations(input, CycleShape::random(16, 1024, 6), during);
    EXPECT_EQ(before, 0U);
    EXPECT_EQ(during, 0U) << "changing the ear allocated";
    EXPECT_EQ(block.router().ear(), Ear::Right);
}
