// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Ducking block: a background turned down under a voice, within
// the attack of the voice starting, and back up after the hold and the
// release of it stopping — without a step anywhere.
//
// **The background is a steady level** in every test here, so what came
// out minus the voice, over that level, is the gain the background was
// given sample by sample, measured from the audio itself.

#include "allocationtrap.h"
#include "analysis.h"
#include "blocks/ducking.h"
#include "blocksizeinvariance.h"
#include "dsp/decibel.h"
#include "offlinerunner.h"
#include "rt/meter.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace rb;
using namespace rb::blocks;
using namespace rb::dsp;
using namespace rb::test;

namespace
{

constexpr double kRate = kTestSampleRate;
constexpr float kBackground = 0.5f;

[[nodiscard]] std::size_t samplesOf(double ms)
{
    return static_cast<std::size_t>(std::lround(ms * 1.0e-3 * kRate));
}

[[nodiscard]] double msOf(std::size_t samples)
{
    return 1000.0 * static_cast<double>(samples) / kRate;
}

/// A voice that is there from \a from for \a length samples: a 300 Hz tone
/// at −12 dBFS, well above the default threshold, and silence round it.
void speak(Signal &voice, std::size_t from, std::size_t length, float db = -12.0f)
{
    const Signal tone = sine(length, 300.0, kRate, dbToGain(db));
    std::copy(tone.begin(), tone.end(), voice.begin() + static_cast<Signal::difference_type>(from));
}

/// The four input channels: the voice on both sides of the main lane, the
/// steady background on both sides of the other.
[[nodiscard]] MultiSignal lanes(const Signal &voice)
{
    return {voice, voice, constant(voice.size(), kBackground), constant(voice.size(), kBackground)};
}

/// The background's gain, sample by sample, read from what came out.
[[nodiscard]] std::vector<float> backgroundGain(const Signal &out, const Signal &voice)
{
    std::vector<float> gain(out.size());
    for (std::size_t i = 0; i < out.size(); ++i) {
        gain[i] = (out[i] - voice[i]) / kBackground;
    }
    return gain;
}

/// The first sample at or after \a from where \a arrived says yes.
template <typename Arrived>
[[nodiscard]] std::size_t firstFrom(const std::vector<float> &gain, std::size_t from, Arrived arrived)
{
    for (std::size_t i = from; i < gain.size(); ++i) {
        if (arrived(gain[i])) {
            return i;
        }
    }
    return gain.size();
}

[[nodiscard]] MultiSignal through(Ducking &block, const MultiSignal &input, std::uint32_t cycle = 256)
{
    BlockRunner runner(block, 4, 2);
    EXPECT_TRUE(runner.prepare().has_value());
    return runner.run(input, CycleShape::fixed(cycle));
}

} // namespace

TEST(DuckingTest, ItTakesAMainLaneAndABackgroundLane)
{
    const Ducking block;
    const PortSpec &inputs = block.info().inputs;
    EXPECT_EQ(inputs.minCount, 4U);
    EXPECT_EQ(inputs.maxCount, 4U);
    EXPECT_EQ(inputs.laneWidth, 2U);
    EXPECT_EQ(inputs.laneLabels, (std::vector<std::string>{"main", "background"}));
    EXPECT_EQ(inputs.laneOf(1), 0U);
    EXPECT_EQ(inputs.laneOf(2), 1U);
    EXPECT_EQ(block.info().outputs.maxCount, 2U);
    EXPECT_EQ(block.info().category, BlockCategory::Advanced);
    EXPECT_EQ(block.info().face, FaceKind::Value);
    EXPECT_EQ(block.info().latencySamples, 0U);

    // The design's starting point, and the issue's timing.
    EXPECT_FLOAT_EQ(*block.param("threshold").asFloat(), -30.0f);
    EXPECT_FLOAT_EQ(*block.param("duckedVolume").asFloat(), 0.4f);
    EXPECT_FLOAT_EQ(*block.param("attack").asFloat(), 50.0f);
    EXPECT_FLOAT_EQ(*block.param("hold").asFloat(), 300.0f);
    EXPECT_FLOAT_EQ(*block.param("release").asFloat(), 500.0f);
}

TEST(DuckingTest, AVoiceDucksTheBackgroundWithinTheAttackAndLetsItBackAfterTheHoldAndRelease)
{
    // The criterion, at two ducked volumes: the attack and the release are
    // the whole way there and back, however deep the duck.
    for (const float ducked : {0.4f, 0.1f}) {
        Ducking block;
        ASSERT_TRUE(block.setParam("duckedVolume", ParamValue::ofFloat(ducked)).has_value());

        const std::size_t start = samplesOf(500.0);
        const std::size_t length = samplesOf(1000.0);
        Signal voice = silence(samplesOf(3000.0));
        speak(voice, start, length);
        const MultiSignal out = through(block, lanes(voice));
        const std::vector<float> gain = backgroundGain(out[0], voice);

        // Nothing before the voice.
        EXPECT_NEAR(*std::min_element(gain.begin(), gain.begin() + static_cast<std::ptrdiff_t>(start)), 1.0f, 1e-6f);

        // Down to the ducked volume within the attack of the first sample.
        const std::size_t down = firstFrom(gain, start, [&](float g) { return g <= ducked + 1e-5f; });
        EXPECT_LE(msOf(down - start), 50.0 + 0.2) << "at " << ducked;
        EXPECT_GE(msOf(down - start), 49.0) << "at " << ducked << ": faster than the attack is a step";
        // And held there, exactly, while the voice goes on.
        for (std::size_t i = down; i < start + length; ++i) {
            ASSERT_NEAR(gain[i], ducked, 1e-5f) << "at " << ducked << ", sample " << i;
        }

        // Back at full after the hold and the release. What is allowed on
        // top is the detector letting go of the voice's last peak: its
        // 10 ms falling the 18 dB from −12 dBFS to the −30 dB threshold.
        const std::size_t end = start + length;
        const std::size_t up = firstFrom(gain, end, [](float g) { return g >= 1.0f - 1e-6f; });
        const double back = msOf(up - end);
        EXPECT_GE(back, 300.0 + 500.0) << "at " << ducked;
        EXPECT_LE(back, 300.0 + 500.0 + 25.0) << "at " << ducked;
        // Still ducked all through the hold.
        const std::size_t holdEnds = end + samplesOf(300.0);
        EXPECT_NEAR(gain[holdEnds - 1], ducked, 1e-5f) << "the hold let go early at " << ducked;
    }
}

TEST(DuckingTest, TheGapsBetweenWordsDoNotPump)
{
    // Speech: 200 ms of voice and 150 ms of nothing, over and over. The
    // gaps are shorter than the hold, so once ducked the background stays
    // down, which is the pumping a ducker exists not to make.
    Ducking block;
    const std::size_t word = samplesOf(200.0);
    const std::size_t gap = samplesOf(150.0);
    Signal voice = silence(samplesOf(3000.0));
    std::size_t at = samplesOf(100.0);
    for (int words = 0; words < 7; ++words, at += word + gap) {
        speak(voice, at, word);
    }
    const MultiSignal out = through(block, lanes(voice));
    const std::vector<float> gain = backgroundGain(out[0], voice);

    const std::size_t settled = samplesOf(100.0 + 60.0);
    const std::size_t lastWordEnds = at - gap;
    const auto [lowest, highest] = std::minmax_element(gain.begin() + static_cast<std::ptrdiff_t>(settled),
                                                       gain.begin() + static_cast<std::ptrdiff_t>(lastWordEnds));
    EXPECT_NEAR(*lowest, 0.4f, 1e-5f);
    EXPECT_NEAR(*highest, 0.4f, 1e-5f) << "the background came up between two words";
}

TEST(DuckingTest, NothingSteps)
{
    // No clicks: the background's gain moves by at most the depth over the
    // attack in one sample, going down and coming up — and when the ducked
    // volume is dragged while the background is down, the gain walks to the
    // new level rather than jumping to it.
    Ducking block;
    BlockRunner runner(block, 4, 2);
    ASSERT_TRUE(runner.prepare().has_value());

    Signal voice = silence(256);
    speak(voice, 0, 256);
    const MultiSignal speaking = lanes(voice);
    const MultiSignal quiet = lanes(silence(256));

    std::vector<float> gain;
    for (int cycle = 0; cycle < 300; ++cycle) {
        if (cycle == 60) {
            ASSERT_TRUE(block.setParam("duckedVolume", ParamValue::ofFloat(0.05f)).has_value());
        }
        const MultiSignal &in = cycle < 120 ? speaking : quiet;
        const MultiSignal out = runner.run(in, CycleShape::fixed(256));
        const std::vector<float> part = backgroundGain(out[0], in[0]);
        gain.insert(gain.end(), part.begin(), part.end());
    }
    float worst = 0.0f;
    for (std::size_t i = 1; i < gain.size(); ++i) {
        worst = std::max(worst, std::fabs(gain[i] - gain[i - 1]));
    }
    // The steepest there is: a duck from full to 5 % over a 50 ms attack.
    const float steepest = 0.95f / static_cast<float>(samplesOf(50.0));
    EXPECT_LE(worst, steepest * 1.001f) << "the background's gain stepped by " << worst;
    EXPECT_NEAR(gain[120 * 256 - 1], 0.05f, 1e-5f) << "the dragged volume was never reached";
}

TEST(DuckingTest, TheVoiceIsNeverTouchedAndTheBackgroundIsFullWithoutOne)
{
    Ducking block;
    Signal voice = silence(48000);
    speak(voice, 12000, 24000, -6.0f);
    MultiSignal in = lanes(voice);
    // Nothing on the background: what comes out is the voice exactly.
    in[2] = silence(voice.size());
    in[3] = silence(voice.size());
    const MultiSignal out = through(block, in);
    EXPECT_EQ(out[0], voice);
    EXPECT_EQ(out[1], voice);

    // A voice below the threshold ducks nothing.
    Ducking quietBlock;
    Signal whisper = silence(48000);
    speak(whisper, 0, 48000, -40.0f);
    const MultiSignal heard = through(quietBlock, lanes(whisper));
    const std::vector<float> gain = backgroundGain(heard[0], whisper);
    EXPECT_NEAR(*std::min_element(gain.begin(), gain.end()), 1.0f, 1e-5f);
}

TEST(DuckingTest, ItSaysHowFarDownTheBackgroundIs)
{
    Ducking block;
    rt::BlockMeter meter;
    BlockRunner runner(block, 4, 2);
    runner.setMeter(&meter);
    ASSERT_TRUE(runner.prepare().has_value());

    (void)runner.run(lanes(silence(4800)), CycleShape::fixed(256));
    EXPECT_FLOAT_EQ(meter.value(Ducking::kDuckingValue), 0.0f);
    EXPECT_FLOAT_EQ(meter.value(Ducking::kActiveValue), 0.0f);

    Signal voice = silence(9600);
    speak(voice, 0, 9600);
    (void)runner.run(lanes(voice), CycleShape::fixed(256));
    EXPECT_NEAR(meter.value(Ducking::kDuckingValue), gainToDb(0.4f), 1e-3f);
    EXPECT_FLOAT_EQ(meter.value(Ducking::kActiveValue), 1.0f);
}

TEST(DuckingTest, ItSoundsTheSameWhateverTheQuantum)
{
    Signal voice = silence(16384);
    for (std::size_t at = 1000; at + 3000 < voice.size(); at += 5000) {
        speak(voice, at, 2500);
    }
    MultiSignal input{voice, voice, sine(16384, 997.0, kRate, 0.5f), sine(16384, 1500.0, kRate, 0.3f)};

    InvarianceOptions options;
    options.inputChannels = 4;
    options.outputChannels = 2;
    const InvarianceReport report = checkBlockSizeInvariance(
        [] {
            auto block = std::make_unique<Ducking>();
            // Quick, so the ramps and the hold turn over many times inside
            // the signal and anything kept per cycle would show.
            EXPECT_TRUE(block->setParam("attack", ParamValue::ofFloat(5.0f)).has_value());
            EXPECT_TRUE(block->setParam("hold", ParamValue::ofFloat(20.0f)).has_value());
            EXPECT_TRUE(block->setParam("release", ParamValue::ofFloat(30.0f)).has_value());
            return std::unique_ptr<Block>(std::move(block));
        },
        input,
        options);
    EXPECT_TRUE(report.passed) << report.summary;
}

TEST(DuckingTest, WhatWasSetIsWhatComesBack)
{
    Ducking block;
    ASSERT_TRUE(block.setParam("threshold", ParamValue::ofFloat(-42.0f)).has_value());
    ASSERT_TRUE(block.setParam("duckedVolume", ParamValue::ofFloat(0.25f)).has_value());
    ASSERT_TRUE(block.setParam("attack", ParamValue::ofFloat(20.0f)).has_value());
    ASSERT_TRUE(block.setParam("hold", ParamValue::ofFloat(800.0f)).has_value());
    ASSERT_TRUE(block.setParam("release", ParamValue::ofFloat(1200.0f)).has_value());

    Ducking opened;
    ASSERT_TRUE(opened.loadState(block.saveState()).has_value());
    EXPECT_FLOAT_EQ(*opened.param("threshold").asFloat(), -42.0f);
    EXPECT_FLOAT_EQ(*opened.param("duckedVolume").asFloat(), 0.25f);
    EXPECT_FLOAT_EQ(*opened.param("attack").asFloat(), 20.0f);
    EXPECT_FLOAT_EQ(*opened.param("hold").asFloat(), 800.0f);
    EXPECT_FLOAT_EQ(*opened.param("release").asFloat(), 1200.0f);

    EXPECT_FALSE(block.setParam("duckedVolume", ParamValue::ofFloat(1.5f)).has_value());
    EXPECT_FALSE(block.setParam("threshold", ParamValue::ofFloat(3.0f)).has_value());
}

TEST(DuckingTest, ARunTouchesTheAllocatorNotAtAll)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    Ducking block;
    BlockRunner runner(block, 4, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    Signal voice = silence(9600);
    speak(voice, 2000, 3000);

    std::size_t allocations = 1;
    const MultiSignal out = runner.runWatchingForAllocations(lanes(voice), CycleShape::random(1, 512, 3), allocations);
    EXPECT_EQ(allocations, 0U);
    EXPECT_EQ(out[0].size(), voice.size());
}
