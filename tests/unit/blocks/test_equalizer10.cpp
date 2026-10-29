// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The 10-Band EQ: its response measured against the ten bells it is
// designed to be, nothing at all when it is flat, sliders that move without
// a click, and a level that stays put when *Keep level* is on.

#include "allocationtrap.h"
#include "analysis.h"
#include "blocks/builtinblocks.h"
#include "blocks/equalizer10.h"
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

using Gains = std::array<float, Equalizer10::kBandCount>;

/// What the acceptance criterion allows against the design, in dB.
constexpr double kResponseToleranceDb = 0.1;

[[nodiscard]] std::unique_ptr<Equalizer10> equalizer(const Gains &gains, float preampDb = 0.0f, bool keepLevel = false)
{
    auto block = std::make_unique<Equalizer10>();
    for (std::size_t index = 0; index < Equalizer10::kBandCount; ++index) {
        EXPECT_TRUE(block->setParam(Equalizer10::kBandIds[index], ParamValue::ofFloat(gains[index])).has_value());
    }
    EXPECT_TRUE(block->setParam("preamp", ParamValue::ofFloat(preampDb)).has_value());
    EXPECT_TRUE(block->setParam("keepLevel", ParamValue::ofBool(keepLevel)).has_value());
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

/// What the block does to one impulse, left side.
[[nodiscard]] Signal impulseResponse(Equalizer10 &block, double rate)
{
    BlockRunner runner(block, 2, 2, rate);
    EXPECT_TRUE(runner.prepare().has_value());
    const MultiSignal out = runner.run(spread(impulse(static_cast<std::size_t>(rate * 0.3)), 2));
    EXPECT_TRUE(nulls(out[0], out[1])) << "the two sides were treated differently";
    return out[0];
}

/// The loudest a signal gets above \a cornerHz, in dB against its own
/// loudest: what a click adds to a tone that has nothing up there.
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

/// Settings from gentle to extreme, for the response to be measured over.
[[nodiscard]] std::vector<Gains> settings()
{
    std::vector<Gains> all;
    for (std::size_t index = 0; index < Equalizer10::kBandCount; ++index) {
        Gains one{};
        one[index] = index % 2 == 0 ? 12.0f : -12.0f;
        all.push_back(one);
    }
    all.push_back(Gains{12, -12, 12, -12, 12, -12, 12, -12, 12, -12});
    all.push_back(Gains{6, 4, 1, -2, -3, -3, -1, 2, 5, 7});
    all.push_back(Gains{12, 12, 12, 12, 12, 12, 12, 12, 12, 12});
    std::mt19937 random(64);
    std::uniform_real_distribution<float> any(-12.0f, 12.0f);
    for (int draw = 0; draw < 3; ++draw) {
        Gains gains{};
        for (float &gain : gains) {
            gain = std::round(any(random) * 2.0f) / 2.0f;
        }
        all.push_back(gains);
    }
    return all;
}

} // namespace

TEST(Equalizer10Test, ItIsABuiltInEffectWithTenBandsAndABarsFace)
{
    const Equalizer10 block;
    EXPECT_EQ(block.info().typeId, "effect.equalizer10");
    EXPECT_EQ(block.info().category, BlockCategory::Effects);
    EXPECT_EQ(block.info().inputs.maxCount, 2U);
    EXPECT_EQ(block.info().outputs.maxCount, 2U);
    EXPECT_EQ(block.info().face, FaceKind::Bars);
    EXPECT_EQ(block.info().latencySamples, 0U);
    // Ten bands, a preamp and the level keeping, in that order.
    ASSERT_EQ(block.params().size(), 12U);
    for (std::size_t index = 0; index < Equalizer10::kBandCount; ++index) {
        EXPECT_EQ(block.params()[index].id, Equalizer10::kBandIds[index]);
    }
    EXPECT_EQ(block.params()[Equalizer10::Preamp].id, "preamp");
    EXPECT_EQ(block.params()[Equalizer10::KeepLevel].id, "keepLevel");

    BlockRegistry registry;
    ASSERT_TRUE(registerBuiltinBlocks(registry));
    EXPECT_TRUE(registry.contains("effect.equalizer10")) << "the 10-Band EQ is not a block a session can add";
}

TEST(Equalizer10Test, FlatItChangesNotOneBit)
{
    Equalizer10 block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    MultiSignal in;
    in.push_back(whiteNoise(9600, 0.8f, 1));
    in.push_back(pinkNoise(9600, 0.8f, 2));
    const MultiSignal out = runner.run(in, CycleShape::random(1, 512, 5));
    EXPECT_EQ(out[0], in[0]);
    EXPECT_EQ(out[1], in[1]);
}

TEST(Equalizer10Test, EachBandIsAnOctaveWideBellAtItsCentre)
{
    // Its whole gain at its centre, where every other band at 0 dB adds
    // nothing, and half of it (in dB) half an octave either side: the bell
    // is an octave wide between its half-gain points, which is what a Q of
    // 1.41 means.
    for (std::size_t index = 0; index < Equalizer10::kBandCount; ++index) {
        Gains gains{};
        gains[index] = 9.0f;
        const double centre = Equalizer10::kBandFrequencies[index];
        EXPECT_NEAR(Equalizer10::curveDb(centre, gains, 48000.0), 9.0, 1e-6) << centre;
        if (centre < 5000.0) {
            // Above that the bilinear warping squeezes the top half.
            EXPECT_NEAR(Equalizer10::curveDb(centre * std::numbers::sqrt2, gains, 48000.0), 4.5, 0.3) << centre;
            EXPECT_NEAR(Equalizer10::curveDb(centre / std::numbers::sqrt2, gains, 48000.0), 4.5, 0.3) << centre;
        }
    }
}

TEST(Equalizer10Test, ItsResponseIsTheDesignsToATenthOfADecibel)
{
    // **The acceptance criterion, measured.** An impulse through the block,
    // read off at every sixth of an octave up to 0.45 of the rate, against
    // the ten bells' design plus the preamp.
    for (const double rate : {44100.0, 48000.0, 96000.0}) {
        for (const Gains &gains : settings()) {
            // The preamp is one gain for every frequency; once is enough.
            for (const float preamp : rate == 48000.0 ? std::vector<float>{0.0f, -6.0f} : std::vector<float>{0.0f}) {
                const auto block = equalizer(gains, preamp);
                const Signal response = impulseResponse(*block, rate);
                for (double f = 20.0; f <= rate * 0.45; f *= std::pow(2.0, 1.0 / 6.0)) {
                    EXPECT_NEAR(measuredDb(response, f, rate),
                                Equalizer10::curveDb(f, gains, rate) + preamp,
                                kResponseToleranceDb)
                        << "at " << f << " Hz, " << rate << " Hz rate, preamp " << preamp << ", first band " << gains[0]
                        << ", sixth " << gains[5];
                }
            }
        }
    }
}

TEST(Equalizer10Test, KeepLevelHoldsTheTopOfTheCurveAtNothing)
{
    // With bands lifted the loudest part of the curve comes back to 0 dB,
    // wherever it is and however it was made; the shape is untouched.
    for (const Gains &gains : {Gains{0, 0, 0, 0, 0, 12, 0, 0, 0, 0},
                               Gains{0, 0, 0, 0, 12, 12, 0, 0, 0, 0},
                               Gains{8, 5, 0, -3, -4, -4, -2, 1, 6, 9},
                               Gains{12, 12, 12, 12, 12, 12, 12, 12, 12, 12}}) {
        const auto kept = equalizer(gains, 0.0f, true);
        const Signal response = impulseResponse(*kept, 48000.0);
        double highest = -100.0;
        for (double f = 20.0; f <= 21600.0; f *= std::pow(2.0, 1.0 / 24.0)) {
            highest = std::max(highest, measuredDb(response, f, 48000.0));
        }
        EXPECT_NEAR(highest, 0.0, 0.25) << "the top of the curve is at " << highest << " dB";
        // And the shape is the same curve, lowered.
        const double lowered = Equalizer10::keepLevelDb(gains, 48000.0);
        EXPECT_NEAR(measuredDb(response, 1000.0, 48000.0),
                    Equalizer10::curveDb(1000.0, gains, 48000.0) + lowered,
                    kResponseToleranceDb);
    }
    // Only cuts: nothing to keep, and the output is what it would be anyway.
    const Gains cuts{-3, -6, 0, 0, -12, 0, 0, -1, 0, 0};
    const auto without = equalizer(cuts, 0.0f, false);
    const auto with = equalizer(cuts, 0.0f, true);
    EXPECT_EQ(Equalizer10::keepLevelDb(cuts, 48000.0), 0.0f);
    EXPECT_EQ(impulseResponse(*without, 48000.0), impulseResponse(*with, 48000.0));
}

TEST(Equalizer10Test, MovingTheSlidersDoesNotClick)
{
    // A 1 kHz tone while every band is thrown from one end to the other and
    // back, and the preamp with them. The tone has nothing above 8 kHz;
    // whatever turns up there is the block's doing.
    auto block = equalizer(Gains{});
    BlockRunner runner(*block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    Signal out;
    double phase = 0.0;
    for (int cycle = 0; cycle < 240; ++cycle) {
        if (cycle == 40 || cycle == 120) {
            const float to = cycle == 40 ? 12.0f : -12.0f;
            for (std::size_t index = 0; index < Equalizer10::kBandCount; ++index) {
                ASSERT_TRUE(
                    block->setParam(Equalizer10::kBandIds[index], ParamValue::ofFloat(index % 2 == 0 ? to : -to))
                        .has_value());
            }
            ASSERT_TRUE(block->setParam("preamp", ParamValue::ofFloat(-to / 2.0f)).has_value());
        }
        if (cycle == 200) {
            ASSERT_TRUE(block->setParam("keepLevel", ParamValue::ofBool(true)).has_value());
        }
        Signal piece(256);
        for (float &sample : piece) {
            sample = 0.1f * static_cast<float>(std::sin(phase));
            phase += 2.0 * std::numbers::pi * 1000.0 / kTestSampleRate;
        }
        const MultiSignal result = runner.run(spread(piece, 2), CycleShape::fixed(256));
        out.insert(out.end(), result[0].begin(), result[0].end());
    }
    const double added = highBandDb(out, 8000.0);
    EXPECT_LT(added, -50.0) << "moving the sliders put something " << added << " dB under the tone above 8 kHz";
}

TEST(Equalizer10Test, EachSideIsTreatedOnItsOwn)
{
    const auto block = equalizer(Gains{12, 12, 12, 12, 12, 12, 12, 12, 12, 12});
    BlockRunner runner(*block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    MultiSignal in;
    in.push_back(sine(9600, 1000.0, kTestSampleRate, 0.1f));
    in.push_back(silence(9600));
    const MultiSignal out = runner.run(in);
    EXPECT_GT(peak(out[0]), 0.3f);
    EXPECT_EQ(peak(out[1]), 0.0f) << "the left side's tone reached the right";
}

TEST(Equalizer10Test, ItSoundsTheSameWhateverTheQuantum)
{
    InvarianceOptions options;
    options.inputChannels = 2;
    options.outputChannels = 2;
    const InvarianceReport report = checkBlockSizeInvariance(
        [] { return std::unique_ptr<Block>(equalizer(Gains{6, 4, 1, -2, -3, -3, -1, 2, 5, 7}, -3.0f, true)); },
        spread(whiteNoise(8192, 0.2f, 7), 2),
        options);
    EXPECT_TRUE(report.passed) << report.summary;
}

TEST(Equalizer10Test, WhatWasSetIsWhatComesBack)
{
    const auto block = equalizer(Gains{1, -2, 3, -4, 5, -6, 7, -8, 9, -10}, -4.5f, true);
    Equalizer10 opened;
    ASSERT_TRUE(opened.loadState(block->saveState()).has_value());
    for (std::size_t index = 0; index < Equalizer10::kBandCount; ++index) {
        EXPECT_FLOAT_EQ(*opened.param(Equalizer10::kBandIds[index]).asFloat(),
                        *block->param(Equalizer10::kBandIds[index]).asFloat());
    }
    EXPECT_FLOAT_EQ(*opened.param("preamp").asFloat(), -4.5f);
    EXPECT_EQ(opened.param("keepLevel").asBool(), true);

    Equalizer10 refusing;
    EXPECT_FALSE(refusing.setParam("gain1k", ParamValue::ofFloat(12.5f)).has_value());
    EXPECT_FALSE(refusing.setParam("preamp", ParamValue::ofFloat(-13.0f)).has_value());
}

TEST(Equalizer10Test, ARunTouchesTheAllocatorNotAtAll)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    const auto block = equalizer(Gains{}, 0.0f, true);
    BlockRunner runner(*block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    // Every gain on its way somewhere, with the level being kept, so the
    // glides and the level's arithmetic are watched too.
    for (std::size_t index = 0; index < Equalizer10::kBandCount; ++index) {
        ASSERT_TRUE(block->setParam(Equalizer10::kBandIds[index], ParamValue::ofFloat(index % 2 == 0 ? 10.0f : -10.0f))
                        .has_value());
    }
    std::size_t allocations = 1;
    const MultiSignal out = runner.runWatchingForAllocations(
        spread(whiteNoise(9600, 0.2f, 3), 2), CycleShape::random(1, 512, 3), allocations);
    EXPECT_EQ(allocations, 0U);
}
