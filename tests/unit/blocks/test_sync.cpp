// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Sync block: a lane held back by an exact number of
// milliseconds, a change of delay faded rather than jumped, and a delay
// that is reported as what it is — intended — rather than as latency for
// compensation to undo.

#include "allocationtrap.h"
#include "analysis.h"
#include "blocks/sync.h"
#include "blocksizeinvariance.h"
#include "offlinerunner.h"
#include "rt/meter.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <memory>

using namespace rb;
using namespace rb::blocks;
using namespace rb::dsp;
using namespace rb::test;

namespace
{

[[nodiscard]] MultiSignal stereo(const Signal &signal)
{
    return MultiSignal{signal, signal};
}

/// Where the loudest sample of \a signal is.
[[nodiscard]] std::size_t loudestAt(const Signal &signal)
{
    return static_cast<std::size_t>(std::distance(
        signal.begin(), std::ranges::max_element(signal, [](float a, float b) { return std::abs(a) < std::abs(b); })));
}

/// Appends \a more to \a into, channel by channel.
void append(MultiSignal &into, const MultiSignal &more)
{
    for (std::size_t channel = 0; channel < into.size(); ++channel) {
        into[channel].insert(into[channel].end(), more[channel].begin(), more[channel].end());
    }
}

[[nodiscard]] Signal slice(const Signal &signal, std::size_t from, std::size_t count)
{
    return Signal(signal.begin() + static_cast<std::ptrdiff_t>(from),
                  signal.begin() + static_cast<std::ptrdiff_t>(from + count));
}

} // namespace

TEST(SyncTest, IsAnAdvancedBlockWhoseDelayIsNotLatency)
{
    const Sync block;
    EXPECT_EQ(block.info().typeId, "advanced.sync");
    EXPECT_EQ(block.info().category, BlockCategory::Advanced);
    EXPECT_EQ(block.info().face, FaceKind::Value);
    // Latency compensation lines other paths up with a block's latency,
    // which would undo the delay this block is for.
    EXPECT_EQ(block.info().latencySamples, 0U);
    EXPECT_EQ(block.param("delay").asNumber().value_or(-1.0), 0.0);
}

TEST(SyncTest, AnImpulseComesOutAsManySamplesLaterAsTheSettingSays)
{
    // The criterion: to the sample, at the two rates anybody runs, across
    // the whole range and at a setting finer than a sample.
    for (const double rate : {44100.0, 48000.0}) {
        for (const double ms : {0.0, 0.02, 1.0, 12.345, 120.0, 1999.99, 2000.0}) {
            Sync block;
            ASSERT_TRUE(block.setParam("delay", ParamValue::ofFloat(static_cast<float>(ms))).has_value());
            BlockRunner runner(block, 2, 2, rate);
            ASSERT_TRUE(runner.prepare().has_value());

            const auto expected = static_cast<std::size_t>(std::lround(ms * 1.0e-3 * rate));
            const std::size_t frames = expected + 2048;
            const MultiSignal out = runner.run(stereo(impulse(frames, 100)), CycleShape::random(1, 512, 3));
            for (std::uint32_t channel = 0; channel < 2; ++channel) {
                const std::size_t at = loudestAt(out[channel]);
                EXPECT_EQ(at, 100 + expected) << ms << " ms at " << rate << " Hz, channel " << channel;
                EXPECT_EQ(out[channel][at], 1.0f) << "the impulse was not passed whole";
            }
            EXPECT_EQ(block.intendedDelaySamples(), expected);
        }
    }
}

TEST(SyncTest, WhatComesOutIsWhatWentInThatMuchLater)
{
    Sync block;
    ASSERT_TRUE(block.setParam("delay", ParamValue::ofFloat(10.0f)).has_value());
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());

    MultiSignal input{whiteNoise(9600, 0.8f, 1), whiteNoise(9600, 0.8f, 2)};
    const MultiSignal out = runner.run(input, CycleShape::random(1, 512, 7));
    for (std::uint32_t channel = 0; channel < 2; ++channel) {
        for (std::size_t frame = 0; frame < 9600; ++frame) {
            const float expected = frame >= 480 ? input[channel][frame - 480] : 0.0f;
            ASSERT_EQ(out[channel][frame], expected) << "channel " << channel << " at " << frame;
        }
    }
}

TEST(SyncTest, ChangingTheDelayWhilePlayingDoesNotClick)
{
    // The other criterion. A tone plays; the delay moves by a little and by
    // a lot. **Nothing steps further between two samples than the tone
    // itself does**: a jump from one moment of the signal to another would
    // be a step of up to twice the amplitude.
    for (const float to : {125.3f, 900.0f, 0.0f}) {
        Sync block;
        ASSERT_TRUE(block.setParam("delay", ParamValue::ofFloat(120.0f)).has_value());
        BlockRunner runner(block, 2, 2);
        ASSERT_TRUE(runner.prepare().has_value());

        const Signal tone = sine(4 * 48000, 440.0, kTestSampleRate, 0.5f);
        MultiSignal out = runner.run(stereo(slice(tone, 0, 48000)));
        block.applyParam(Sync::Delay, to);
        append(out, runner.run(stereo(slice(tone, 48000, 48000))));
        EXPECT_FALSE(block.fading()) << "a second was not long enough to fade";

        // From once the tone has come through the first delay.
        const std::span<const float> heard = std::span<const float>(out[0]).subspan(6000);
        const float step = maxSampleStep(heard);
        const float toneStep = maxSampleStep(tone);
        EXPECT_LE(step, toneStep * 1.05f)
            << "moving to " << to << " ms stepped " << step << " between two samples, the tone itself " << toneStep;
        RecordProperty("stepRatioTo" + std::to_string(static_cast<int>(to)), std::to_string(step / toneStep));
    }
}

TEST(SyncTest, AFadeEndsOnTheNewDelayExactly)
{
    Sync block;
    ASSERT_TRUE(block.setParam("delay", ParamValue::ofFloat(120.0f)).has_value());
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    (void)runner.run(stereo(silence(4800)));

    block.applyParam(Sync::Delay, 250.0f);
    (void)runner.run(stereo(silence(256)));
    EXPECT_TRUE(block.fading());
    EXPECT_EQ(block.intendedDelaySamples(), 12000U) << "the delay being moved to is not what is reported";
    (void)runner.run(stereo(silence(4800)));
    EXPECT_FALSE(block.fading());

    const MultiSignal out = runner.run(stereo(impulse(16000, 10)));
    EXPECT_EQ(loudestAt(out[0]), 10U + 12000U);
    EXPECT_EQ(out[0][10 + 12000], 1.0f);
}

TEST(SyncTest, ADragIsFollowedFadeByFadeToWhereItStopped)
{
    // Settings arriving faster than a fade lasts, as they do under a drag:
    // each fade runs its length, and the last one goes to the last setting.
    Sync block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const Signal tone = sine(48000, 220.0, kTestSampleRate, 0.5f);
    // The tone is already coming through when the drag starts; a drag
    // while the line still held silence would prove nothing.
    block.applyParam(Sync::Delay, 2.0f);
    MultiSignal out = runner.run(stereo(slice(tone, 0, 4800)));
    std::size_t at = 4800;
    for (const float ms : {5.0f, 11.0f, 17.5f, 30.0f, 42.25f}) {
        block.applyParam(Sync::Delay, ms);
        append(out, runner.run(stereo(slice(tone, at, 128))));
        at += 128;
    }
    append(out, runner.run(stereo(slice(tone, at, 48000 - at))));
    EXPECT_FALSE(block.fading());
    EXPECT_EQ(block.intendedDelaySamples(), 2028U) << "42.25 ms at 48 kHz";
    EXPECT_LE(maxSampleStep(out[0]), maxSampleStep(tone) * 1.05f);
}

TEST(SyncTest, TheDelayIsPublishedForTheInterface)
{
    Sync block;
    ASSERT_TRUE(block.setParam("delay", ParamValue::ofFloat(120.0f)).has_value());
    rt::BlockMeter meter;
    BlockRunner runner(block, 2, 2);
    runner.setMeter(&meter);
    ASSERT_TRUE(runner.prepare().has_value());
    (void)runner.run(stereo(silence(512)));
    EXPECT_EQ(meter.value(Sync::kDelaySamplesValue), 5760.0f);
    EXPECT_EQ(block.intendedDelaySamples(), 5760U);
}

TEST(SyncTest, AResetCutsRatherThanFades)
{
    Sync block;
    ASSERT_TRUE(block.setParam("delay", ParamValue::ofFloat(10.0f)).has_value());
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    (void)runner.run(stereo(constant(240, 0.5f)));

    block.applyParam(Sync::Delay, 20.0f);
    block.reset();
    EXPECT_FALSE(block.fading()) << "there is nothing left to fade from";
    const MultiSignal out = runner.run(stereo(silence(2048)));
    EXPECT_EQ(peak(out[0]), 0.0f) << "what the line held came back after a reset";
    EXPECT_EQ(block.intendedDelaySamples(), 960U);
}

TEST(SyncTest, ItSoundsTheSameWhateverTheQuantum)
{
    MultiSignal input{sine(8192, 997.0, kTestSampleRate, 0.4f), sine(8192, 1500.0, kTestSampleRate, 0.3f)};
    InvarianceOptions options;
    options.inputChannels = 2;
    options.outputChannels = 2;
    const InvarianceReport report = checkBlockSizeInvariance(
        [] {
            auto block = std::make_unique<Sync>();
            (void)block->setParam("delay", ParamValue::ofFloat(33.3f));
            return std::unique_ptr<Block>(std::move(block));
        },
        input,
        options);
    EXPECT_TRUE(report.passed) << report.summary;
}

TEST(SyncTest, ARunAndAFadeTouchTheAllocatorNotAtAll)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    Sync block;
    rt::BlockMeter meter;
    BlockRunner runner(block, 2, 2);
    runner.setMeter(&meter);
    ASSERT_TRUE(runner.prepare().has_value());
    block.applyParam(Sync::Delay, 1500.0f);

    std::size_t allocations = 1;
    const MultiSignal out = runner.runWatchingForAllocations(
        stereo(sine(9600, 220.0, kTestSampleRate, 0.5f)), CycleShape::random(1, 512, 4), allocations);
    EXPECT_EQ(allocations, 0U);
    EXPECT_EQ(out[0].size(), 9600U);
}

TEST(SyncTest, WhatWasChosenIsWhatComesBack)
{
    Sync block;
    ASSERT_TRUE(block.setParam("delay", ParamValue::ofFloat(123.45f)).has_value());
    Sync opened;
    ASSERT_TRUE(opened.loadState(block.saveState()).has_value());
    EXPECT_FLOAT_EQ(static_cast<float>(opened.param("delay").asNumber().value_or(0.0)), 123.45f);
    EXPECT_FALSE(block.setParam("delay", ParamValue::ofFloat(2500.0f)).has_value())
        << "a delay past two seconds was taken";
    EXPECT_FALSE(block.setParam("delay", ParamValue::ofFloat(-1.0f)).has_value());
}
