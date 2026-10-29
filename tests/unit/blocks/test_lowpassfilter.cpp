// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Low Pass Filter block: its response measured against the one it
// is designed to have, and a cutoff and a slope that move without a sound
// of their own.

#include "allocationtrap.h"
#include "analysis.h"
#include "blocks/builtinblocks.h"
#include "blocks/lowpassfilter.h"
#include "blocksizeinvariance.h"
#include "dsp/biquad.h"
#include "dsp/blockregistry.h"
#include "offlinerunner.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <cmath>
#include <complex>
#include <memory>
#include <numbers>
#include <random>
#include <string>
#include <vector>

using namespace rb;
using namespace rb::blocks;
using namespace rb::dsp;
using namespace rb::test;

namespace
{

/// What the acceptance criterion allows: ±0.2 dB against the design, from
/// the bottom of the range up to 0.45 of the rate.
constexpr double kResponseToleranceDb = 0.2;
constexpr double kHighestProbe = 0.45;
/// Below this the design is not compared number for number: what is left of
/// the signal is below what a float carries, and the filter is doing its
/// job. It only has to be down there too.
constexpr double kFloorDb = -100.0;

[[nodiscard]] std::unique_ptr<LowPassFilter> filter(float cutoff, std::string_view slope = LowPassFilter::kSlope12)
{
    auto block = std::make_unique<LowPassFilter>();
    EXPECT_TRUE(block->setParam("cutoff", ParamValue::ofFloat(cutoff)).has_value());
    EXPECT_TRUE(block->setParam("slope", ParamValue::ofEnum(std::string{slope})).has_value());
    return block;
}

/// The gain \a response gives a sine at \a frequency, in dB.
[[nodiscard]] double measuredDb(const Signal &response, double frequency, double rate)
{
    const std::complex<double> step = std::polar(1.0, -2.0 * std::numbers::pi * frequency / rate);
    std::complex<double> phasor = 1.0;
    std::complex<double> sum = 0.0;
    for (const float sample : response) {
        sum += static_cast<double>(sample) * phasor;
        phasor *= step;
    }
    return 20.0 * std::log10(std::max(std::abs(sum), 1.0e-15));
}

/// The loudest a signal gets above \a cornerHz, in dB against its own
/// loudest: what a click or a zipper adds to a low tone, which on its own
/// has nothing up there.
[[nodiscard]] double highBandDb(const Signal &signal, double cornerHz, double rate = kTestSampleRate)
{
    const FilterSpec spec{FilterShape::HighPass, cornerHz, kButterworthQ, 0.0};
    Biquad first;
    Biquad second;
    first.setCoefficients(designBiquad(spec, rate));
    second.setCoefficients(designBiquad(spec, rate));
    Signal high(signal.size());
    for (std::size_t n = 0; n < signal.size(); ++n) {
        high[n] = second.process(first.process(signal[n]));
    }
    // The first few milliseconds are the high-pass settling on the tone's
    // start, which is the test's own doing.
    const Signal after(high.begin() + static_cast<std::ptrdiff_t>(rate * 0.01), high.end());
    return 20.0 * std::log10(std::max(peak(after), 1.0e-12f) / peak(signal));
}

} // namespace

TEST(LowPassFilterTest, ItIsABuiltInEffectWithAStereoPairEachSide)
{
    const LowPassFilter block;
    EXPECT_EQ(block.info().typeId, "effect.lowpass");
    EXPECT_EQ(block.info().category, BlockCategory::Effects);
    EXPECT_EQ(block.info().inputs.maxCount, 2U);
    EXPECT_EQ(block.info().outputs.maxCount, 2U);
    EXPECT_EQ(block.info().face, FaceKind::Value);
    EXPECT_EQ(block.info().latencySamples, 0U);

    BlockRegistry registry;
    ASSERT_TRUE(registerBuiltinBlocks(registry));
    EXPECT_TRUE(registry.contains("effect.lowpass")) << "the Low Pass Filter is not a block a session can add";
}

TEST(LowPassFilterTest, ItStartsAtTwentyKilohertzWhereItChangesNothingAnybodyHears)
{
    const LowPassFilter block;
    EXPECT_FLOAT_EQ(*block.param("cutoff").asFloat(), 20000.0f);
    EXPECT_EQ(block.param("slope").text(), "12dB");
    for (double f = 20.0; f <= 10000.0; f *= 2.0) {
        EXPECT_GT(LowPassFilter::magnitudeDb(f, 20000.0, false, 48000.0), -0.1) << f;
    }
}

TEST(LowPassFilterTest, ItsResponseIsTheDesignsToAFifthOfADecibelUpToFortyFivePercentOfTheRate)
{
    // **The acceptance criterion, measured.** An impulse through the block,
    // its response read off at every sixth of an octave, against the
    // Butterworth design each slope is built from.
    for (const double rate : {44100.0, 48000.0, 96000.0}) {
        for (const float cutoff : {200.0f, 1000.0f, 6300.0f, 20000.0f}) {
            for (const std::string_view slope : {LowPassFilter::kSlope12, LowPassFilter::kSlope24}) {
                const bool steep = slope == LowPassFilter::kSlope24;
                const auto block = filter(cutoff, slope);
                BlockRunner runner(*block, 2, 2, rate);
                ASSERT_TRUE(runner.prepare().has_value());
                const MultiSignal out = runner.run(spread(impulse(static_cast<std::size_t>(rate * 0.25)), 2));
                ASSERT_TRUE(nulls(out[0], out[1])) << "the two sides were filtered differently";

                // And the design is the filter it says it is: 3 dB down at
                // the cutoff, whichever the slope.
                EXPECT_NEAR(LowPassFilter::magnitudeDb(cutoff, cutoff, steep, rate), -3.0103, 1e-3);

                for (double f = 20.0; f <= rate * kHighestProbe; f *= std::pow(2.0, 1.0 / 6.0)) {
                    const double expected = LowPassFilter::magnitudeDb(f, cutoff, steep, rate);
                    const double measured = measuredDb(out[0], f, rate);
                    const std::string where = std::to_string(cutoff) + " Hz " + std::string{slope} + " at "
                        + std::to_string(rate) + " Hz, probed at " + std::to_string(f) + " Hz";
                    if (expected < kFloorDb) {
                        EXPECT_LT(measured, kFloorDb + 10.0) << where;
                    } else {
                        EXPECT_NEAR(measured, expected, kResponseToleranceDb) << where;
                    }
                }
            }
        }
    }
}

TEST(LowPassFilterTest, TheSteepSlopeFallsTwiceAsFast)
{
    // An octave and two above a 500 Hz cutoff, where both are well into
    // their slopes and well clear of half the rate.
    const double gentle = LowPassFilter::magnitudeDb(2000.0, 500.0, false, 48000.0)
        - LowPassFilter::magnitudeDb(4000.0, 500.0, false, 48000.0);
    const double steep = LowPassFilter::magnitudeDb(2000.0, 500.0, true, 48000.0)
        - LowPassFilter::magnitudeDb(4000.0, 500.0, true, 48000.0);
    EXPECT_NEAR(gentle, 12.0, 0.5);
    EXPECT_NEAR(steep, 24.0, 1.0);
}

TEST(LowPassFilterTest, DraggingTheCutoffDoesNotZipOrThump)
{
    // A 150 Hz tone, under every cutoff the block allows, while the cutoff
    // is dragged down and up in the small steps a slider sends and then
    // thrown from one end of its range to the other. The tone itself has
    // nothing above 3 kHz; whatever turns up there is the filter's doing.
    const auto block = filter(20000.0f);
    BlockRunner runner(*block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    Signal out;
    double phase = 0.0;
    for (int cycle = 0; cycle < 400; ++cycle) {
        if (cycle >= 20 && cycle < 200) {
            // Sixty steps a second, as the slider sends them, down and up.
            const double position = (cycle - 20) / 90.0;
            const double down = position <= 1.0 ? position : 2.0 - position;
            const auto hz = static_cast<float>(20000.0 * std::pow(200.0 / 20000.0, down));
            if (cycle % 3 == 0) {
                ASSERT_TRUE(block->setParam("cutoff", ParamValue::ofFloat(hz)).has_value());
            }
        }
        if (cycle == 250) {
            ASSERT_TRUE(block->setParam("cutoff", ParamValue::ofFloat(200.0f)).has_value());
        }
        if (cycle == 320) {
            ASSERT_TRUE(block->setParam("cutoff", ParamValue::ofFloat(20000.0f)).has_value());
        }
        // One continuous tone across the cycles.
        Signal piece(256);
        for (float &sample : piece) {
            sample = 0.5f * static_cast<float>(std::sin(phase));
            phase += 2.0 * std::numbers::pi * 150.0 / kTestSampleRate;
        }
        const MultiSignal result = runner.run(spread(piece, 2), CycleShape::fixed(256));
        out.insert(out.end(), result[0].begin(), result[0].end());
    }
    const double added = highBandDb(out, 3000.0);
    EXPECT_LT(added, -60.0) << "moving the cutoff put something " << added << " dB under the tone above 3 kHz";
    // And the tone got through all along, the 200 Hz cutoff taking only the
    // little it takes from a tone below it.
    EXPECT_GT(peak(Signal(out.end() - 4096, out.end())), 0.49f);
}

TEST(LowPassFilterTest, ChangingTheSlopeDoesNotClick)
{
    const auto block = filter(800.0f);
    BlockRunner runner(*block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    Signal out;
    double phase = 0.0;
    for (int cycle = 0; cycle < 200; ++cycle) {
        if (cycle == 50) {
            ASSERT_TRUE(block->setParam("slope", ParamValue::ofEnum("24dB")).has_value());
        }
        if (cycle == 120) {
            ASSERT_TRUE(block->setParam("slope", ParamValue::ofEnum("12dB")).has_value());
        }
        Signal piece(256);
        for (float &sample : piece) {
            sample = 0.5f * static_cast<float>(std::sin(phase));
            phase += 2.0 * std::numbers::pi * 150.0 / kTestSampleRate;
        }
        const MultiSignal result = runner.run(spread(piece, 2), CycleShape::fixed(256));
        out.insert(out.end(), result[0].begin(), result[0].end());
    }
    const double added = highBandDb(out, 3000.0);
    EXPECT_LT(added, -60.0) << "changing the slope put something " << added << " dB under the tone above 3 kHz";
}

TEST(LowPassFilterTest, ItStaysStableWhateverTheCutoffDoes)
{
    // Full-scale noise, and the cutoff set somewhere new every cycle, at
    // cycles as short as PipeWire makes them.
    const auto block = filter(1000.0f, LowPassFilter::kSlope24);
    BlockRunner runner(*block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    std::mt19937 random(69);
    std::uniform_real_distribution<float> octaves(std::log2(200.0f), std::log2(20000.0f));
    float loudest = 0.0f;
    for (int cycle = 0; cycle < 3000; ++cycle) {
        ASSERT_TRUE(block->setParam("cutoff", ParamValue::ofFloat(std::exp2(octaves(random)))).has_value());
        const MultiSignal result = runner.run(spread(whiteNoise(32, 1.0f, cycle + 1), 2), CycleShape::fixed(32));
        for (const float sample : result[0]) {
            ASSERT_TRUE(std::isfinite(sample));
            loudest = std::max(loudest, std::abs(sample));
        }
    }
    EXPECT_LT(loudest, 4.0f) << "the filter ran away";
}

TEST(LowPassFilterTest, EachSideIsFilteredOnItsOwn)
{
    const auto block = filter(1000.0f);
    BlockRunner runner(*block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    MultiSignal in;
    in.push_back(sine(9600, 300.0, kTestSampleRate, 0.5f));
    in.push_back(silence(9600));
    const MultiSignal out = runner.run(in);
    EXPECT_GT(peak(out[0]), 0.45f);
    EXPECT_EQ(peak(out[1]), 0.0f) << "the left side's tone reached the right";
}

TEST(LowPassFilterTest, ItSoundsTheSameWhateverTheQuantum)
{
    for (const std::string_view slope : {LowPassFilter::kSlope12, LowPassFilter::kSlope24}) {
        InvarianceOptions options;
        options.inputChannels = 2;
        options.outputChannels = 2;
        const InvarianceReport report =
            checkBlockSizeInvariance([slope] { return std::unique_ptr<Block>(filter(1500.0f, slope)); },
                                     spread(whiteNoise(8192, 0.5f, 7), 2),
                                     options);
        EXPECT_TRUE(report.passed) << slope << ": " << report.summary;
    }
}

TEST(LowPassFilterTest, WhatWasSetIsWhatComesBack)
{
    const auto block = filter(3150.0f, LowPassFilter::kSlope24);
    LowPassFilter opened;
    ASSERT_TRUE(opened.loadState(block->saveState()).has_value());
    EXPECT_FLOAT_EQ(*opened.param("cutoff").asFloat(), 3150.0f);
    EXPECT_EQ(opened.param("slope").text(), "24dB");

    // Past the ends is refused rather than taken quietly.
    LowPassFilter refusing;
    EXPECT_FALSE(refusing.setParam("cutoff", ParamValue::ofFloat(100.0f)).has_value());
    EXPECT_FALSE(refusing.setParam("cutoff", ParamValue::ofFloat(22000.0f)).has_value());
    EXPECT_FALSE(refusing.setParam("slope", ParamValue::ofEnum("48dB")).has_value());
}

TEST(LowPassFilterTest, ARunTouchesTheAllocatorNotAtAll)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    for (const std::string_view slope : {LowPassFilter::kSlope12, LowPassFilter::kSlope24}) {
        const auto block = filter(2000.0f, slope);
        BlockRunner runner(*block, 2, 2);
        ASSERT_TRUE(runner.prepare().has_value());
        // A cutoff on its way somewhere, so the glide's own arithmetic is
        // watched too.
        ASSERT_TRUE(block->setParam("cutoff", ParamValue::ofFloat(400.0f)).has_value());
        std::size_t allocations = 1;
        const MultiSignal out = runner.runWatchingForAllocations(
            spread(whiteNoise(9600, 0.5f, 3), 2), CycleShape::random(1, 512, 3), allocations);
        EXPECT_EQ(allocations, 0U) << slope;
    }
}
