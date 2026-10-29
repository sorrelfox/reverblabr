// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The High Pass Filter block: its response measured against the one
// it is designed to have, and a cutoff and a slope that move without a
// sound of their own. The Low Pass Filter's counterpart, built on the same
// CutoffFilter; these are its tests with the sides and the range turned
// round.

#include "allocationtrap.h"
#include "analysis.h"
#include "blocks/builtinblocks.h"
#include "blocks/highpassfilter.h"
#include "blocksizeinvariance.h"
#include "dsp/biquad.h"
#include "dsp/blockregistry.h"
#include "offlinerunner.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <array>
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

[[nodiscard]] std::unique_ptr<HighPassFilter> filter(float cutoff, std::string_view slope = HighPassFilter::kSlope12)
{
    auto block = std::make_unique<HighPassFilter>();
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

/// The loudest a signal gets between 500 Hz and 1.5 kHz, in dB against its
/// own loudest: what a click or a zipper adds to a high tone, which on its
/// own has nothing down there.
[[nodiscard]] double lowBandDb(const Signal &signal, double rate = kTestSampleRate)
{
    std::array<Biquad, 4> band;
    band[0].setCoefficients(designBiquad({FilterShape::HighPass, 500.0, kButterworthQ, 0.0}, rate));
    band[1].setCoefficients(designBiquad({FilterShape::HighPass, 500.0, kButterworthQ, 0.0}, rate));
    band[2].setCoefficients(designBiquad({FilterShape::LowPass, 1500.0, kButterworthQ, 0.0}, rate));
    band[3].setCoefficients(designBiquad({FilterShape::LowPass, 1500.0, kButterworthQ, 0.0}, rate));
    Signal low(signal.size());
    for (std::size_t n = 0; n < signal.size(); ++n) {
        float sample = signal[n];
        for (Biquad &filter : band) {
            sample = filter.process(sample);
        }
        low[n] = sample;
    }
    // The first 20 ms are the band settling on the tone's start, which is
    // the test's own doing.
    const Signal after(low.begin() + static_cast<std::ptrdiff_t>(rate * 0.02), low.end());
    return 20.0 * std::log10(std::max(peak(after), 1.0e-12f) / peak(signal));
}

/// The loudest a signal gets above \a cornerHz, in dB against its own
/// loudest: what a click adds to a low tone, which on its own has nothing
/// up there.
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
    const Signal after(high.begin() + static_cast<std::ptrdiff_t>(rate * 0.01), high.end());
    return 20.0 * std::log10(std::max(peak(after), 1.0e-12f) / peak(signal));
}

} // namespace

TEST(HighPassFilterTest, ItIsABuiltInEffectWithAStereoPairEachSide)
{
    const HighPassFilter block;
    EXPECT_EQ(block.info().typeId, "effect.highpass");
    EXPECT_EQ(block.info().category, BlockCategory::Effects);
    EXPECT_EQ(block.info().inputs.maxCount, 2U);
    EXPECT_EQ(block.info().outputs.maxCount, 2U);
    EXPECT_EQ(block.info().face, FaceKind::Value);
    EXPECT_EQ(block.info().latencySamples, 0U);

    BlockRegistry registry;
    ASSERT_TRUE(registerBuiltinBlocks(registry));
    EXPECT_TRUE(registry.contains("effect.highpass")) << "the High Pass Filter is not a block a session can add";
}

TEST(HighPassFilterTest, ItStartsAtTwentyHertzWhereItChangesNothingAnybodyHears)
{
    const HighPassFilter block;
    EXPECT_FLOAT_EQ(*block.param("cutoff").asFloat(), 20.0f);
    EXPECT_EQ(block.param("slope").text(), "12dB");
    for (double f = 40.0; f <= 20000.0; f *= 2.0) {
        EXPECT_GT(HighPassFilter::magnitudeDb(f, 20.0, false, 48000.0), -0.3) << f;
    }
}

TEST(HighPassFilterTest, ItsResponseIsTheDesignsToAFifthOfADecibelUpToFortyFivePercentOfTheRate)
{
    // **The acceptance criterion, measured.** An impulse through the block,
    // its response read off at every sixth of an octave, against the
    // Butterworth design each slope is built from.
    for (const double rate : {44100.0, 48000.0, 96000.0}) {
        for (const float cutoff : {20.0f, 80.0f, 400.0f, 2000.0f}) {
            for (const std::string_view slope : {HighPassFilter::kSlope12, HighPassFilter::kSlope24}) {
                const bool steep = slope == HighPassFilter::kSlope24;
                const auto block = filter(cutoff, slope);
                BlockRunner runner(*block, 2, 2, rate);
                ASSERT_TRUE(runner.prepare().has_value());
                const MultiSignal out = runner.run(spread(impulse(static_cast<std::size_t>(rate * 0.5)), 2));
                ASSERT_TRUE(nulls(out[0], out[1])) << "the two sides were filtered differently";

                // And the design is the filter it says it is: 3 dB down at
                // the cutoff, whichever the slope.
                EXPECT_NEAR(HighPassFilter::magnitudeDb(cutoff, cutoff, steep, rate), -3.0103, 1e-3);

                for (double f = 20.0; f <= rate * kHighestProbe; f *= std::pow(2.0, 1.0 / 6.0)) {
                    const double expected = HighPassFilter::magnitudeDb(f, cutoff, steep, rate);
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

TEST(HighPassFilterTest, TheSteepSlopeFallsTwiceAsFast)
{
    // Two and three octaves under a 1 kHz cutoff, where both are well into
    // their slopes.
    const double gentle = HighPassFilter::magnitudeDb(250.0, 1000.0, false, 48000.0)
        - HighPassFilter::magnitudeDb(125.0, 1000.0, false, 48000.0);
    const double steep = HighPassFilter::magnitudeDb(250.0, 1000.0, true, 48000.0)
        - HighPassFilter::magnitudeDb(125.0, 1000.0, true, 48000.0);
    EXPECT_NEAR(gentle, 12.0, 0.5);
    EXPECT_NEAR(steep, 24.0, 1.0);
}

TEST(HighPassFilterTest, DraggingTheCutoffDoesNotZipOrThump)
{
    // A 12 kHz tone, above every cutoff the block allows, while the cutoff
    // is dragged up and down in the small steps a slider sends and then
    // thrown from one end of its range to the other. The tone itself has
    // nothing between 500 Hz and 1.5 kHz; whatever turns up there is the
    // filter's doing.
    const auto block = filter(20.0f);
    BlockRunner runner(*block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    Signal out;
    double phase = 0.0;
    for (int cycle = 0; cycle < 400; ++cycle) {
        if (cycle >= 20 && cycle < 200) {
            // Sixty steps a second, as the slider sends them, down and up.
            const double position = (cycle - 20) / 90.0;
            const double down = position <= 1.0 ? position : 2.0 - position;
            const auto hz = static_cast<float>(20.0 * std::pow(2000.0 / 20.0, down));
            if (cycle % 3 == 0) {
                ASSERT_TRUE(block->setParam("cutoff", ParamValue::ofFloat(hz)).has_value());
            }
        }
        if (cycle == 250) {
            ASSERT_TRUE(block->setParam("cutoff", ParamValue::ofFloat(2000.0f)).has_value());
        }
        if (cycle == 320) {
            ASSERT_TRUE(block->setParam("cutoff", ParamValue::ofFloat(20.0f)).has_value());
        }
        // One continuous tone across the cycles.
        Signal piece(256);
        for (float &sample : piece) {
            sample = 0.5f * static_cast<float>(std::sin(phase));
            phase += 2.0 * std::numbers::pi * 12000.0 / kTestSampleRate;
        }
        const MultiSignal result = runner.run(spread(piece, 2), CycleShape::fixed(256));
        out.insert(out.end(), result[0].begin(), result[0].end());
    }
    const double added = lowBandDb(out);
    EXPECT_LT(added, -60.0) << "moving the cutoff put something " << added << " dB under the tone at 0.5–1.5 kHz";
    // And the tone got through all along, the 2 kHz cutoff taking only the
    // little it takes from a tone above it.
    EXPECT_GT(peak(Signal(out.end() - 4096, out.end())), 0.49f);
}

TEST(HighPassFilterTest, ChangingTheSlopeDoesNotClick)
{
    // **A tone the two slopes treat differently**, or there is nothing to
    // click: 150 Hz under a 400 Hz cutoff is 17 dB down at the gentle slope
    // and 34 dB at the steep one. Well above the tone and the cutoff alike
    // the two agree, and a switch there is silent whether it fades or not.
    // The tone has nothing above 3 kHz; whatever turns up there is a click.
    const auto block = filter(400.0f);
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

TEST(HighPassFilterTest, ItStaysStableWhateverTheCutoffDoes)
{
    // Full-scale noise, and the cutoff set somewhere new every cycle, at
    // cycles as short as PipeWire makes them.
    const auto block = filter(1000.0f, HighPassFilter::kSlope24);
    BlockRunner runner(*block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    std::mt19937 random(69);
    std::uniform_real_distribution<float> octaves(std::log2(20.0f), std::log2(2000.0f));
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

TEST(HighPassFilterTest, EachSideIsFilteredOnItsOwn)
{
    const auto block = filter(100.0f);
    BlockRunner runner(*block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    MultiSignal in;
    in.push_back(sine(9600, 3000.0, kTestSampleRate, 0.5f));
    in.push_back(silence(9600));
    const MultiSignal out = runner.run(in);
    EXPECT_GT(peak(out[0]), 0.45f);
    EXPECT_EQ(peak(out[1]), 0.0f) << "the left side's tone reached the right";
}

TEST(HighPassFilterTest, ItSoundsTheSameWhateverTheQuantum)
{
    for (const std::string_view slope : {HighPassFilter::kSlope12, HighPassFilter::kSlope24}) {
        InvarianceOptions options;
        options.inputChannels = 2;
        options.outputChannels = 2;
        const InvarianceReport report =
            checkBlockSizeInvariance([slope] { return std::unique_ptr<Block>(filter(150.0f, slope)); },
                                     spread(whiteNoise(8192, 0.5f, 7), 2),
                                     options);
        EXPECT_TRUE(report.passed) << slope << ": " << report.summary;
    }
}

TEST(HighPassFilterTest, WhatWasSetIsWhatComesBack)
{
    const auto block = filter(315.0f, HighPassFilter::kSlope24);
    HighPassFilter opened;
    ASSERT_TRUE(opened.loadState(block->saveState()).has_value());
    EXPECT_FLOAT_EQ(*opened.param("cutoff").asFloat(), 315.0f);
    EXPECT_EQ(opened.param("slope").text(), "24dB");

    // Past the ends is refused rather than taken quietly.
    HighPassFilter refusing;
    EXPECT_FALSE(refusing.setParam("cutoff", ParamValue::ofFloat(10.0f)).has_value());
    EXPECT_FALSE(refusing.setParam("cutoff", ParamValue::ofFloat(2500.0f)).has_value());
    EXPECT_FALSE(refusing.setParam("slope", ParamValue::ofEnum("48dB")).has_value());
}

TEST(HighPassFilterTest, ARunTouchesTheAllocatorNotAtAll)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    for (const std::string_view slope : {HighPassFilter::kSlope12, HighPassFilter::kSlope24}) {
        const auto block = filter(1000.0f, slope);
        BlockRunner runner(*block, 2, 2);
        ASSERT_TRUE(runner.prepare().has_value());
        // A cutoff on its way somewhere, so the glide's own arithmetic is
        // watched too.
        ASSERT_TRUE(block->setParam("cutoff", ParamValue::ofFloat(40.0f)).has_value());
        std::size_t allocations = 1;
        const MultiSignal out = runner.runWatchingForAllocations(
            spread(whiteNoise(9600, 0.5f, 3), 2), CycleShape::random(1, 512, 3), allocations);
        EXPECT_EQ(allocations, 0U) << slope;
    }
}
