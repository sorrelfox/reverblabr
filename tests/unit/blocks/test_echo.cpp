// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Echo: repeats where the time says, each quieter by the feedback,
// dying away even at the most feedback allows, and a time moved without a
// click.

#include "allocationtrap.h"
#include "analysis.h"
#include "blocks/echo.h"
#include "blocksizeinvariance.h"
#include "dsp/biquad.h"
#include "dsp/decibel.h"
#include "offlinerunner.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>

using namespace rb;
using namespace rb::blocks;
using namespace rb::dsp;
using namespace rb::test;

namespace
{

[[nodiscard]] MultiSignal through(Echo &block, const MultiSignal &input, std::uint32_t cycle = 256)
{
    BlockRunner runner(block, 2, 2);
    EXPECT_TRUE(runner.prepare().has_value());
    return runner.run(input, CycleShape::fixed(cycle));
}

void set(Echo &block, float timeMs, float feedback, float toneHz, float mix)
{
    EXPECT_TRUE(block.setParam("time", ParamValue::ofFloat(timeMs)).has_value());
    EXPECT_TRUE(block.setParam("feedback", ParamValue::ofFloat(feedback)).has_value());
    EXPECT_TRUE(block.setParam("tone", ParamValue::ofFloat(toneHz)).has_value());
    EXPECT_TRUE(block.setParam("mix", ParamValue::ofFloat(mix)).has_value());
}

/// Where the loudest sample of \a signal is between \a from and \a to.
[[nodiscard]] std::size_t loudestIn(const Signal &signal, std::size_t from, std::size_t to)
{
    std::size_t at = from;
    for (std::size_t frame = from; frame < to && frame < signal.size(); ++frame) {
        if (std::fabs(signal[frame]) > std::fabs(signal[at])) {
            at = frame;
        }
    }
    return at;
}

/// A signal's part from \a from, \a length long.
[[nodiscard]] Signal part(const Signal &signal, std::size_t from, std::size_t length)
{
    return Signal(signal.begin() + static_cast<Signal::difference_type>(from),
                  signal.begin() + static_cast<Signal::difference_type>(std::min(from + length, signal.size())));
}

/// The loudest a signal gets above 12 kHz, once the filters used to look
/// have settled: where a click shows, and a steady tone below it does not.
[[nodiscard]] float highestAbove12k(const Signal &signal)
{
    Biquad first;
    Biquad second;
    first.setCoefficients(designBiquad({FilterShape::HighPass, 12000.0, kButterworth4Q1, 0.0}, kTestSampleRate));
    second.setCoefficients(designBiquad({FilterShape::HighPass, 12000.0, kButterworth4Q2, 0.0}, kTestSampleRate));
    float highest = 0.0f;
    for (std::size_t frame = 0; frame < signal.size(); ++frame) {
        const float high = second.process(first.process(signal[frame]));
        if (frame >= 2560) {
            highest = std::max(highest, std::fabs(high));
        }
    }
    return gainToDb(highest);
}

} // namespace

TEST(EchoTest, TheRepeatsComeWhenTheTimeSays)
{
    // The criterion's timing half. An impulse, the repeats alone, the tone
    // open: the first repeat is the impulse itself, exactly the time later,
    // and each after it the same time again (the loop's filter adds a
    // sample or so of its own).
    for (const float timeMs : {1.0f, 125.0f, 350.0f, 2000.0f}) {
        Echo block;
        set(block, timeMs, 50.0f, 20000.0f, 100.0f);
        const auto delay = static_cast<std::size_t>(std::lround(timeMs * 48.0f));
        Signal impulse(delay * 4 + 400, 0.0f);
        impulse[100] = 1.0f;
        const MultiSignal out = through(block, MultiSignal{impulse, impulse});
        EXPECT_FLOAT_EQ(out[0][100 + delay], 1.0f) << "the first repeat at " << timeMs << " ms";
        EXPECT_EQ(std::fabs(out[0][100]), 0.0f) << "the original was heard with the mix at the repeats alone";
        for (std::size_t repeat = 2; repeat <= 3; ++repeat) {
            const std::size_t expected = 100 + repeat * delay;
            const std::size_t found = loudestIn(out[0], expected - std::min<std::size_t>(delay / 2, 20), expected + 20);
            EXPECT_GE(found, expected) << "repeat " << repeat << " at " << timeMs << " ms came early";
            EXPECT_LE(found, expected + 2) << "repeat " << repeat << " at " << timeMs << " ms";
        }
    }
}

TEST(EchoTest, EachRepeatIsQuieterByTheFeedback)
{
    // The criterion's decay half. A 1 kHz burst, well under the tone's
    // corner, so the loop's filter passes it: repeat k is the feedback to
    // the power k - 1, within a tenth of a decibel.
    for (const float feedback : {25.0f, 50.0f, 80.0f}) {
        Echo block;
        set(block, 200.0f, feedback, 20000.0f, 100.0f);
        Signal burst(48000 * 2, 0.0f);
        const Signal tone = sine(2400, 1000.0, kTestSampleRate, 0.5f);
        std::copy(tone.begin(), tone.end(), burst.begin());
        const MultiSignal out = through(block, MultiSignal{burst, burst});
        const double first = rmsDb(part(out[0], 9600 + 480, 1440));
        for (int repeat = 2; repeat <= 5; ++repeat) {
            const double level = rmsDb(part(out[0], static_cast<std::size_t>(repeat) * 9600 + 480, 1440));
            const double expected = 20.0 * std::log10(feedback * 0.01) * (repeat - 1);
            EXPECT_NEAR(level - first, expected, 0.1) << "repeat " << repeat << " at " << feedback << " %";
        }
    }
}

TEST(EchoTest, TheToneTakesTheTopOffEveryRepeatMore)
{
    // A burst with a high tone and a low one in it: each repeat keeps the
    // low one and has less of the high one than the repeat before.
    Echo block;
    set(block, 200.0f, 80.0f, 2000.0f, 100.0f);
    Signal burst(48000 * 2, 0.0f);
    const Signal low = sine(4800, 300.0, kTestSampleRate, 0.3f);
    const Signal high = sine(4800, 8000.0, kTestSampleRate, 0.3f);
    for (std::size_t frame = 0; frame < 4800; ++frame) {
        burst[frame] = low[frame] + high[frame];
    }
    const MultiSignal out = through(block, MultiSignal{burst, burst});
    // The first repeat is the sound as it went in; the filter is in the
    // loop, so it starts on the second.
    float previous = 0.0f;
    for (int repeat = 1; repeat <= 4; ++repeat) {
        const Signal each = part(out[0], static_cast<std::size_t>(repeat) * 9600 + 1200, 2400);
        const float ratio = amplitudeAt(each, 8000.0) / amplitudeAt(each, 300.0);
        if (repeat == 1) {
            EXPECT_NEAR(ratio, 1.0f, 0.01f) << "the first repeat was filtered";
        } else {
            EXPECT_LT(ratio, previous * 0.5f) << "repeat " << repeat << " is no duller than the one before";
        }
        previous = ratio;
    }
}

TEST(EchoTest, AtTheMostFeedbackTheRepeatsStillDieAway)
{
    // The criterion: stable at the maximum. A second of loud noise, then
    // twenty of nothing, at 95 % with the tone open, where the loop is
    // closest to one.
    // - It stays bounded: never past 1 / (1 - 0.95) = 20 times the input,
    //   the most a loop of that gain could ever add up to.
    // - From two seconds to twenty it falls at least as far as 95 % a repeat
    //   says (further, for what the filter takes), or all the way to
    //   silence. A loop at or over one would hold or grow.
    for (const float timeMs : {1.0f, 50.0f, 500.0f}) {
        Echo block;
        set(block, timeMs, 95.0f, 20000.0f, 100.0f);
        Signal signal = whiteNoise(48000 * 21, 1.0f, 91);
        std::fill(signal.begin() + 48000, signal.end(), 0.0f);
        const MultiSignal out = through(block, MultiSignal{signal, signal}, 512);
        EXPECT_LT(peak(out[0]), peak(signal) * 20.0f) << "it built up at " << timeMs << " ms";
        const double early = rmsDb(part(out[0], 48000 * 2, 48000));
        const double late = rmsDb(part(out[0], 48000 * 20, 48000));
        const double expected = 20.0 * std::log10(0.95) * (18000.0 / timeMs);
        const bool silent = late <= static_cast<double>(kSilenceDb) + 1.0;
        EXPECT_TRUE(silent || late - early <= expected + 1.0)
            << "at " << timeMs << " ms it fell " << late - early << " dB, where " << expected << " was due";
    }
}

TEST(EchoTest, WithTheMixAtNothingItIsTheOriginal)
{
    // The original goes straight through, undelayed: the echo is the
    // effect, not a latency.
    Echo block;
    set(block, 350.0f, 60.0f, 5000.0f, 0.0f);
    const Signal noise = whiteNoise(48000, 0.5f, 92);
    const MultiSignal out = through(block, MultiSignal{noise, noise});
    EXPECT_LE(nullTestDb(out[0], noise), kSilenceDb);
    EXPECT_EQ(block.info().latencySamples, 0U);
    EXPECT_EQ(block.intendedDelaySamples(), 0U) << "the original is not held back, so no path is either";
}

TEST(EchoTest, MovingTheTimeFadesAcrossWithoutAClick)
{
    // A steady tone and its repeats, and the time thrown from 350 ms to
    // 90 ms and back, the feedback and the mix moved too: nothing above
    // 12 kHz, where a jump in the line would show.
    Echo block;
    set(block, 350.0f, 60.0f, 20000.0f, 50.0f);
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    // **Faded in**: a tone switched on at once is a click of its own, and
    // the echo would repeat it three times before anything was moved.
    Signal tone = sine(48000 * 3, 1000.0, kTestSampleRate, 0.3f);
    for (std::size_t frame = 0; frame < 960; ++frame) {
        tone[frame] *= static_cast<float>(0.5 - 0.5 * std::cos(std::numbers::pi * static_cast<double>(frame) / 960.0));
    }
    Signal left;
    for (std::size_t at = 0; at + 256 <= tone.size(); at += 256) {
        if (at == 256 * 250) {
            ASSERT_TRUE(block.setParam("time", ParamValue::ofFloat(90.0f)).has_value());
            ASSERT_TRUE(block.setParam("feedback", ParamValue::ofFloat(20.0f)).has_value());
        }
        if (at == 256 * 400) {
            ASSERT_TRUE(block.setParam("time", ParamValue::ofFloat(350.0f)).has_value());
            ASSERT_TRUE(block.setParam("mix", ParamValue::ofFloat(90.0f)).has_value());
        }
        const MultiSignal out =
            runner.run(MultiSignal{part(tone, at, 256), part(tone, at, 256)}, CycleShape::fixed(256));
        left.insert(left.end(), out[0].begin(), out[0].end());
    }
    EXPECT_LT(highestAbove12k(left), -70.0f);
}

TEST(EchoTest, ItSoundsTheSameWhateverTheQuantum)
{
    const Signal noise = whiteNoise(8192, 0.5f, 93);
    InvarianceOptions options;
    options.inputChannels = 2;
    options.outputChannels = 2;
    const InvarianceReport report = checkBlockSizeInvariance(
        [] {
            auto block = std::make_unique<Echo>();
            set(*block, 20.0f, 70.0f, 3000.0f, 50.0f);
            return std::unique_ptr<Block>(std::move(block));
        },
        MultiSignal{noise, noise},
        options);
    EXPECT_TRUE(report.passed) << report.summary;
}

TEST(EchoTest, ARunTouchesTheAllocatorNotAtAll)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    Echo block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    // A fade and a tone glide in progress too.
    set(block, 40.0f, 90.0f, 1000.0f, 60.0f);
    const Signal noise = whiteNoise(9600, 0.5f, 94);
    std::size_t allocations = 1;
    const MultiSignal out =
        runner.runWatchingForAllocations(MultiSignal{noise, noise}, CycleShape::random(1, 512, 3), allocations);
    EXPECT_EQ(allocations, 0U);
    EXPECT_EQ(out[0].size(), noise.size());
}

TEST(EchoTest, WhatWasSetIsWhatComesBack)
{
    Echo block;
    set(block, 480.0f, 72.0f, 3500.0f, 44.0f);
    Echo opened;
    ASSERT_TRUE(opened.loadState(block.saveState()).has_value());
    EXPECT_FLOAT_EQ(*opened.param("time").asFloat(), 480.0f);
    EXPECT_FLOAT_EQ(*opened.param("feedback").asFloat(), 72.0f);
    EXPECT_FLOAT_EQ(*opened.param("tone").asFloat(), 3500.0f);
    EXPECT_FLOAT_EQ(*opened.param("mix").asFloat(), 44.0f);
    EXPECT_FALSE(block.setParam("feedback", ParamValue::ofFloat(96.0f)).has_value());
    EXPECT_FALSE(block.setParam("time", ParamValue::ofFloat(2001.0f)).has_value());
    EXPECT_FALSE(block.setParam("time", ParamValue::ofFloat(0.5f)).has_value());
}
