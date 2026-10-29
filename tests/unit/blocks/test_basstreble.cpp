// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Bass & Treble block: two shelves measured against the ones
// they are designed to be, nothing at all when both are at 0 dB, and knobs
// that turn without a click.

#include "allocationtrap.h"
#include "analysis.h"
#include "blocks/basstreble.h"
#include "blocks/builtinblocks.h"
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
#include <string>
#include <utility>
#include <vector>

using namespace rb;
using namespace rb::blocks;
using namespace rb::dsp;
using namespace rb::test;

namespace
{

/// What the acceptance criterion allows against the design, in dB.
constexpr double kResponseToleranceDb = 0.1;

[[nodiscard]] std::unique_ptr<BassTreble> tone(float bassDb, float trebleDb)
{
    auto block = std::make_unique<BassTreble>();
    EXPECT_TRUE(block->setParam("bass", ParamValue::ofFloat(bassDb)).has_value());
    EXPECT_TRUE(block->setParam("treble", ParamValue::ofFloat(trebleDb)).has_value());
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

/// The loudest a signal gets between 1.5 and 3 kHz, in dB against its own
/// loudest: where a 60 Hz tone has nothing and neither shelf reaches, so
/// whatever is there is a click.
[[nodiscard]] double clickDb(const Signal &signal, double rate = kTestSampleRate)
{
    Biquad highA;
    Biquad highB;
    Biquad lowA;
    Biquad lowB;
    highA.setCoefficients(designBiquad({FilterShape::HighPass, 1500.0, kButterworthQ, 0.0}, rate));
    highB.setCoefficients(designBiquad({FilterShape::HighPass, 1500.0, kButterworthQ, 0.0}, rate));
    lowA.setCoefficients(designBiquad({FilterShape::LowPass, 3000.0, kButterworthQ, 0.0}, rate));
    lowB.setCoefficients(designBiquad({FilterShape::LowPass, 3000.0, kButterworthQ, 0.0}, rate));
    Signal band(signal.size());
    for (std::size_t n = 0; n < signal.size(); ++n) {
        band[n] = lowB.process(lowA.process(highB.process(highA.process(signal[n]))));
    }
    // The first 20 ms are the band filters settling on the tone's start,
    // which is the test's own doing.
    const Signal after(band.begin() + static_cast<std::ptrdiff_t>(rate * 0.02), band.end());
    return 20.0 * std::log10(std::max(peak(after), 1.0e-12f) / peak(signal));
}

/// A 60 Hz tone through \a block in 256-frame cycles, with \a change made
/// to the block before the cycle it is keyed to.
[[nodiscard]] Signal playWhileChanging(BassTreble &block,
                                       const std::vector<std::pair<int, std::pair<std::string, float>>> &changes)
{
    BlockRunner runner(block, 2, 2);
    EXPECT_TRUE(runner.prepare().has_value());
    Signal out;
    double phase = 0.0;
    for (int cycle = 0; cycle < 240; ++cycle) {
        for (const auto &[at, change] : changes) {
            if (at == cycle) {
                EXPECT_TRUE(block.setParam(change.first, ParamValue::ofFloat(change.second)).has_value());
            }
        }
        Signal piece(256);
        for (float &sample : piece) {
            sample = 0.25f * static_cast<float>(std::sin(phase));
            phase += 2.0 * std::numbers::pi * 60.0 / kTestSampleRate;
        }
        const MultiSignal result = runner.run(spread(piece, 2), CycleShape::fixed(256));
        out.insert(out.end(), result[0].begin(), result[0].end());
    }
    return out;
}

} // namespace

TEST(BassTrebleTest, ItIsABuiltInEffectWithAStereoPairEachSide)
{
    const BassTreble block;
    EXPECT_EQ(block.info().typeId, "effect.basstreble");
    EXPECT_EQ(block.info().category, BlockCategory::Effects);
    EXPECT_EQ(block.info().inputs.maxCount, 2U);
    EXPECT_EQ(block.info().outputs.maxCount, 2U);
    EXPECT_EQ(block.info().face, FaceKind::Value);
    EXPECT_EQ(block.info().latencySamples, 0U);

    BlockRegistry registry;
    ASSERT_TRUE(registerBuiltinBlocks(registry));
    EXPECT_TRUE(registry.contains("effect.basstreble")) << "Bass & Treble is not a block a session can add";
}

TEST(BassTrebleTest, AtZeroDecibelsItChangesNotOneBit)
{
    // Both shelves at their defaults pass the signal through untouched:
    // not quieter, not shifted, not rounded.
    const BassTreble fresh;
    EXPECT_FLOAT_EQ(*fresh.param("bass").asFloat(), 0.0f);
    EXPECT_FLOAT_EQ(*fresh.param("treble").asFloat(), 0.0f);

    BassTreble block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    MultiSignal in;
    in.push_back(whiteNoise(9600, 0.8f, 1));
    in.push_back(pinkNoise(9600, 0.8f, 2));
    const MultiSignal out = runner.run(in, CycleShape::random(1, 512, 5));
    EXPECT_EQ(out[0], in[0]);
    EXPECT_EQ(out[1], in[1]);
}

TEST(BassTrebleTest, EachShelfDoesItsOwnEnd)
{
    // Full lift or cut well past the frequency, half of it at the
    // frequency, and the middle of the range left nearly alone.
    constexpr double rate = 48000.0;
    EXPECT_NEAR(BassTreble::magnitudeDb(10.0, 12.0, 0.0, rate), 12.0, 0.1);
    EXPECT_NEAR(BassTreble::magnitudeDb(100.0, 12.0, 0.0, rate), 6.0, 1e-6);
    EXPECT_LT(std::abs(BassTreble::magnitudeDb(1000.0, 12.0, 0.0, rate)), 0.2);
    EXPECT_NEAR(BassTreble::magnitudeDb(22000.0, 0.0, -12.0, rate), -12.0, 0.2);
    EXPECT_NEAR(BassTreble::magnitudeDb(10000.0, 0.0, -12.0, rate), -6.0, 1e-6);
    EXPECT_LT(std::abs(BassTreble::magnitudeDb(1000.0, 0.0, -12.0, rate)), 0.2);
    // **No overshoot**: a shelf at the steepest slope that stays monotonic
    // never lifts past its gain nor dips below nothing on the way. A
    // steeper one rings a bump and a dip either side of the frequency,
    // which is heard as a boom rather than as more bass.
    for (double f = 5.0; f < 23000.0; f *= 1.05) {
        const double bass = BassTreble::magnitudeDb(f, 12.0, 0.0, rate);
        const double treble = BassTreble::magnitudeDb(f, 0.0, 12.0, rate);
        EXPECT_LE(bass, 12.0 + 1e-6) << f;
        EXPECT_GE(bass, -1e-6) << f;
        EXPECT_LE(treble, 12.0 + 1e-6) << f;
        EXPECT_GE(treble, -1e-6) << f;
    }
    // And a voice's middle is where it was with both turned all the way.
    EXPECT_LT(std::abs(BassTreble::magnitudeDb(1000.0, 12.0, 12.0, rate)), 0.4);
}

TEST(BassTrebleTest, ItsResponseIsTheDesignsToATenthOfADecibel)
{
    // **The acceptance criterion, measured.** An impulse through the block,
    // read off at every sixth of an octave up to 0.45 of the rate, against
    // the two shelves' design.
    const std::vector<std::pair<float, float>> settings{
        {-12.0f, 0.0f}, {12.0f, 0.0f}, {0.0f, -12.0f}, {0.0f, 12.0f}, {6.0f, -9.0f}, {12.0f, 12.0f}, {-12.0f, -12.0f}};
    for (const double rate : {44100.0, 48000.0, 96000.0}) {
        for (const auto &[bassDb, trebleDb] : settings) {
            const auto block = tone(bassDb, trebleDb);
            BlockRunner runner(*block, 2, 2, rate);
            ASSERT_TRUE(runner.prepare().has_value());
            const MultiSignal out = runner.run(spread(impulse(static_cast<std::size_t>(rate * 0.25)), 2));
            ASSERT_TRUE(nulls(out[0], out[1])) << "the two sides were treated differently";
            for (double f = 20.0; f <= rate * 0.45; f *= std::pow(2.0, 1.0 / 6.0)) {
                EXPECT_NEAR(measuredDb(out[0], f, rate),
                            BassTreble::magnitudeDb(f, bassDb, trebleDb, rate),
                            kResponseToleranceDb)
                    << "bass " << bassDb << ", treble " << trebleDb << " at " << rate << " Hz, probed at " << f
                    << " Hz";
            }
        }
    }
}

TEST(BassTrebleTest, TurningTheKnobsDoesNotClick)
{
    // Each gain thrown from one end to the other, and both at once, under a
    // low tone. Whatever turns up between 1.5 and 3 kHz — where the tone has
    // nothing and neither shelf reaches — is a click.
    BassTreble block;
    const Signal out = playWhileChanging(block,
                                         {{40, {"bass", -12.0f}},
                                          {80, {"bass", 12.0f}},
                                          {120, {"treble", 12.0f}},
                                          {160, {"treble", -12.0f}},
                                          {200, {"bass", -12.0f}},
                                          {200, {"treble", 12.0f}}});
    // **What a glide leaves is far below this.** A gain that glides still
    // changes how loud the tone is over 20 ms, and the corners of that ramp
    // spread a little of the tone upwards: about −58 dB for a 12 dB turn of
    // the bass, −53 dB for the full 24 dB swing, and nothing measurable for
    // the treble, which the tone is below. Stepped instead, the same turns
    // put a click at −17 dB.
    const double added = clickDb(out);
    EXPECT_LT(added, -45.0) << "turning a knob put something " << added << " dB under the tone at 1.5–3 kHz";
    // And the last change arrived: the tone is where the bass cut puts a
    // 60 Hz tone, about 10 dB down.
    const double expected = 0.25 * std::pow(10.0, BassTreble::magnitudeDb(60.0, -12.0, 12.0, kTestSampleRate) / 20.0);
    EXPECT_NEAR(peak(Signal(out.end() - 4096, out.end())), expected, expected * 0.02);
}

TEST(BassTrebleTest, EachSideIsTreatedOnItsOwn)
{
    const auto block = tone(12.0f, 12.0f);
    BlockRunner runner(*block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    MultiSignal in;
    in.push_back(sine(9600, 50.0, kTestSampleRate, 0.2f));
    in.push_back(silence(9600));
    const MultiSignal out = runner.run(in);
    EXPECT_GT(peak(out[0]), 0.5f) << "the bass lift did not reach the left side";
    EXPECT_EQ(peak(out[1]), 0.0f) << "the left side's tone reached the right";
}

TEST(BassTrebleTest, ItSoundsTheSameWhateverTheQuantum)
{
    for (const auto &[bassDb, trebleDb] : std::vector<std::pair<float, float>>{{9.0f, -6.0f}, {-12.0f, 12.0f}}) {
        InvarianceOptions options;
        options.inputChannels = 2;
        options.outputChannels = 2;
        const InvarianceReport report =
            checkBlockSizeInvariance([bassDb, trebleDb] { return std::unique_ptr<Block>(tone(bassDb, trebleDb)); },
                                     spread(whiteNoise(8192, 0.3f, 7), 2),
                                     options);
        EXPECT_TRUE(report.passed) << bassDb << "/" << trebleDb << ": " << report.summary;
    }
}

TEST(BassTrebleTest, WhatWasSetIsWhatComesBack)
{
    const auto block = tone(4.5f, -7.0f);
    BassTreble opened;
    ASSERT_TRUE(opened.loadState(block->saveState()).has_value());
    EXPECT_FLOAT_EQ(*opened.param("bass").asFloat(), 4.5f);
    EXPECT_FLOAT_EQ(*opened.param("treble").asFloat(), -7.0f);

    // Past the ends is refused rather than taken quietly.
    BassTreble refusing;
    EXPECT_FALSE(refusing.setParam("bass", ParamValue::ofFloat(12.5f)).has_value());
    EXPECT_FALSE(refusing.setParam("treble", ParamValue::ofFloat(-13.0f)).has_value());
}

TEST(BassTrebleTest, ARunTouchesTheAllocatorNotAtAll)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    const auto block = tone(-6.0f, 6.0f);
    BlockRunner runner(*block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    // Both gains on their way somewhere, so the glide's own arithmetic is
    // watched too.
    ASSERT_TRUE(block->setParam("bass", ParamValue::ofFloat(10.0f)).has_value());
    ASSERT_TRUE(block->setParam("treble", ParamValue::ofFloat(-10.0f)).has_value());
    std::size_t allocations = 1;
    const MultiSignal out = runner.runWatchingForAllocations(
        spread(whiteNoise(9600, 0.3f, 3), 2), CycleShape::random(1, 512, 3), allocations);
    EXPECT_EQ(allocations, 0U);
}
