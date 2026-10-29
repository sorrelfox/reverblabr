// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Parametric EQ: its response measured against the cascade it is
// designed to be, nothing at all when it is flat, and bands that are
// dragged, switched, retyped, added and deleted without a click.

#include "allocationtrap.h"
#include "analysis.h"
#include "blocks/builtinblocks.h"
#include "blocks/parametriceq.h"
#include "blocksizeinvariance.h"
#include "dsp/biquad.h"
#include "dsp/blockregistry.h"
#include "offlinerunner.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <complex>
#include <functional>
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

/// What the acceptance criterion allows against the design, in dB.
constexpr double kResponseToleranceDb = 0.1;
/// Below this the design is not compared number for number; the block only
/// has to be down there too.
constexpr double kFloorDb = -90.0;

using Band = ParametricEq::Band;

void set(ParametricEq &block, std::size_t band, ParametricEq::BandParam which, const ParamValue &value)
{
    ASSERT_TRUE(block.setParam(ParametricEq::paramId(band, which), value).has_value())
        << ParametricEq::paramId(band, which);
}

/// Puts \a bands into \a block, slot for slot; a slot past the list is let
/// go.
void place(ParametricEq &block, const std::vector<std::pair<std::string, Band>> &bands)
{
    for (std::size_t slot = 0; slot < ParametricEq::kBandCount; ++slot) {
        if (slot >= bands.size()) {
            set(block, slot, ParametricEq::Used, ParamValue::ofBool(false));
            continue;
        }
        const auto &[type, band] = bands[slot];
        set(block, slot, ParametricEq::Used, ParamValue::ofBool(true));
        set(block, slot, ParametricEq::On, ParamValue::ofBool(band.active));
        set(block, slot, ParametricEq::Type, ParamValue::ofEnum(type));
        set(block, slot, ParametricEq::Frequency, ParamValue::ofFloat(static_cast<float>(band.frequency)));
        set(block, slot, ParametricEq::Gain, ParamValue::ofFloat(static_cast<float>(band.gainDb)));
        set(block, slot, ParametricEq::Q, ParamValue::ofFloat(static_cast<float>(band.q)));
    }
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

/// The loudest a signal gets above \a cornerHz, against its own loudest.
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

/// A low tone through \a block in 256-frame cycles for \a cycles cycles,
/// with \a change called before each one.
[[nodiscard]] Signal playWhile(ParametricEq &block, int cycles, const std::function<void(int)> &change)
{
    BlockRunner runner(block, 2, 2);
    EXPECT_TRUE(runner.prepare().has_value());
    Signal out;
    double phase = 0.0;
    for (int cycle = 0; cycle < cycles; ++cycle) {
        change(cycle);
        Signal piece(256);
        for (float &sample : piece) {
            sample = 0.25f * static_cast<float>(std::sin(phase));
            phase += 2.0 * std::numbers::pi * 150.0 / kTestSampleRate;
        }
        const MultiSignal result = runner.run(spread(piece, 2), CycleShape::fixed(256));
        out.insert(out.end(), result[0].begin(), result[0].end());
    }
    return out;
}

/// Settings of every kind, for the response to be measured over.
[[nodiscard]] std::vector<std::vector<std::pair<std::string, Band>>> settings()
{
    std::vector<std::vector<std::pair<std::string, Band>>> all;
    all.push_back({{"peak", {true, FilterShape::Peaking, 250.0, 9.0, 1.4}},
                   {"peak", {true, FilterShape::Peaking, 700.0, -6.0, 3.0}},
                   {"peak", {true, FilterShape::Peaking, 5600.0, -5.9, 1.4}}});
    all.push_back({{"lowShelf", {true, FilterShape::LowShelf, 120.0, 6.0, 0.7}},
                   {"highShelf", {true, FilterShape::HighShelf, 9000.0, -8.0, 0.7}},
                   {"highPass", {true, FilterShape::HighPass, 40.0, 0.0, 0.707}},
                   {"lowPass", {true, FilterShape::LowPass, 15000.0, 0.0, 0.707}}});
    all.push_back({{"notch", {true, FilterShape::Notch, 60.0, 0.0, 12.0}},
                   {"notch", {true, FilterShape::Notch, 120.0, 0.0, 12.0}},
                   {"bandPass", {true, FilterShape::BandPass, 1000.0, 0.0, 0.5}},
                   {"peak", {false, FilterShape::Peaking, 3000.0, 18.0, 2.0}}});
    std::mt19937 random(85);
    std::uniform_int_distribution<std::size_t> anyType(0, 4);
    std::uniform_real_distribution<float> anyOctave(std::log2(30.0f), std::log2(16000.0f));
    std::uniform_real_distribution<float> anyGain(-18.0f, 18.0f);
    std::uniform_real_distribution<float> anyQ(std::log(0.3f), std::log(8.0f));
    for (int draw = 0; draw < 2; ++draw) {
        std::vector<std::pair<std::string, Band>> ten;
        for (std::size_t slot = 0; slot < ParametricEq::kBandCount; ++slot) {
            const std::size_t type = anyType(random);
            ten.push_back({std::string{ParametricEq::kTypeIds[type]},
                           {true,
                            ParametricEq::shapeOf(type),
                            std::exp2(anyOctave(random)),
                            anyGain(random),
                            std::exp(anyQ(random))}});
        }
        all.push_back(ten);
    }
    return all;
}

} // namespace

TEST(ParametricEqTest, ItIsAnAdvancedBlockWithTenBandsOfSixSettings)
{
    const ParametricEq block;
    EXPECT_EQ(block.info().typeId, "advanced.parametriceq");
    EXPECT_EQ(block.info().category, BlockCategory::Advanced);
    EXPECT_EQ(block.info().face, FaceKind::Curve);
    EXPECT_EQ(block.info().inputs.maxCount, 2U);
    EXPECT_EQ(block.info().outputs.maxCount, 2U);
    EXPECT_EQ(block.info().latencySamples, 0U);
    ASSERT_EQ(block.params().size(), 60U);
    EXPECT_EQ(block.params()[0].id, "band1Used");
    EXPECT_EQ(block.params()[59].id, "band10Q");
    EXPECT_EQ(ParametricEq::paramId(2, ParametricEq::Gain), "band3Gain");

    BlockRegistry registry;
    ASSERT_TRUE(registerBuiltinBlocks(registry));
    EXPECT_TRUE(registry.contains("advanced.parametriceq")) << "the Parametric EQ is not a block a session can add";
}

TEST(ParametricEqTest, ItStartsWithThreeFlatBandsAndChangesNotOneBit)
{
    ParametricEq block;
    const auto bands = block.bands();
    EXPECT_EQ(std::ranges::count_if(bands, [](const Band &band) { return band.active; }), 3);
    EXPECT_EQ(bands[0].frequency, 100.0);
    EXPECT_EQ(bands[1].frequency, 1000.0);
    EXPECT_EQ(bands[2].frequency, 8000.0);

    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    MultiSignal in;
    in.push_back(whiteNoise(9600, 0.8f, 1));
    in.push_back(pinkNoise(9600, 0.8f, 2));
    const MultiSignal out = runner.run(in, CycleShape::random(1, 512, 5));
    EXPECT_EQ(out[0], in[0]);
    EXPECT_EQ(out[1], in[1]);
}

TEST(ParametricEqTest, ItsResponseIsTheCascadesToATenthOfADecibel)
{
    // **The acceptance criterion, measured.** An impulse through the block,
    // read off at every sixth of an octave up to 0.45 of the rate, against
    // every active band's design added up — bells, shelves, pass filters,
    // notches, a band-pass, one band switched off, and ten at random.
    for (const double rate : {44100.0, 48000.0, 96000.0}) {
        for (const auto &setting : settings()) {
            ParametricEq block;
            place(block, setting);
            std::vector<Band> bands;
            for (const auto &entry : setting) {
                bands.push_back(entry.second);
            }
            BlockRunner runner(block, 2, 2, rate);
            ASSERT_TRUE(runner.prepare().has_value());
            const MultiSignal out = runner.run(spread(impulse(static_cast<std::size_t>(rate * 0.5)), 2));
            ASSERT_TRUE(nulls(out[0], out[1])) << "the two sides were treated differently";
            for (double f = 20.0; f <= rate * 0.45; f *= std::pow(2.0, 1.0 / 6.0)) {
                const double expected = ParametricEq::curveDb(f, bands, rate);
                const double measured = measuredDb(out[0], f, rate);
                if (expected < kFloorDb) {
                    EXPECT_LT(measured, kFloorDb + 10.0) << f << " Hz at " << rate;
                } else {
                    EXPECT_NEAR(measured, expected, kResponseToleranceDb)
                        << "at " << f << " Hz, " << rate << " Hz rate, first band " << setting[0].first;
                }
            }
        }
    }
}

TEST(ParametricEqTest, DraggingABandDoesNotClick)
{
    // A bell at +12 dB dragged across the range the way a handle is — a new
    // frequency and gain sixty times a second — and then thrown from one
    // end to the other, under a 150 Hz tone. The tone has nothing above
    // 6 kHz; whatever turns up there is the block's doing.
    ParametricEq block;
    place(block, {{"peak", {true, FilterShape::Peaking, 100.0, 12.0, 2.0}}});
    const Signal out = playWhile(block, 400, [&block](int cycle) {
        if (cycle >= 20 && cycle < 200 && cycle % 3 == 0) {
            const double position = (cycle - 20) / 90.0;
            const double along = position <= 1.0 ? position : 2.0 - position;
            set(block,
                0,
                ParametricEq::Frequency,
                ParamValue::ofFloat(static_cast<float>(100.0 * std::pow(100.0, along))));
            set(block, 0, ParametricEq::Gain, ParamValue::ofFloat(static_cast<float>(12.0 - 24.0 * along)));
        }
        if (cycle == 250) {
            set(block, 0, ParametricEq::Frequency, ParamValue::ofFloat(10000.0f));
            set(block, 0, ParametricEq::Q, ParamValue::ofFloat(18.0f));
        }
        if (cycle == 320) {
            set(block, 0, ParametricEq::Frequency, ParamValue::ofFloat(150.0f));
            set(block, 0, ParametricEq::Q, ParamValue::ofFloat(0.3f));
        }
    });
    const double added = highBandDb(out, 6000.0);
    EXPECT_LT(added, -60.0) << "dragging a band put something " << added << " dB under the tone above 6 kHz";
}

TEST(ParametricEqTest, SwitchingRetypingAddingAndDeletingABandDoesNotClick)
{
    // A deep bell right on the tone, switched off and on, turned into a
    // notch and a low-pass, deleted and added again: each change is a fade.
    ParametricEq block;
    place(block, {{"peak", {true, FilterShape::Peaking, 150.0, -18.0, 1.0}}});
    const Signal out = playWhile(block, 360, [&block](int cycle) {
        switch (cycle) {
        case 40:
            set(block, 0, ParametricEq::On, ParamValue::ofBool(false));
            break;
        case 80:
            set(block, 0, ParametricEq::On, ParamValue::ofBool(true));
            break;
        case 120:
            set(block, 0, ParametricEq::Type, ParamValue::ofEnum("notch"));
            break;
        case 160:
            set(block, 0, ParametricEq::Type, ParamValue::ofEnum("lowPass"));
            break;
        case 200:
            set(block, 0, ParametricEq::Used, ParamValue::ofBool(false));
            break;
        case 240:
            set(block, 0, ParametricEq::Type, ParamValue::ofEnum("highShelf"));
            set(block, 0, ParametricEq::Gain, ParamValue::ofFloat(12.0f));
            set(block, 0, ParametricEq::Frequency, ParamValue::ofFloat(80.0f));
            set(block, 0, ParametricEq::Used, ParamValue::ofBool(true));
            break;
        default:
            break;
        }
    });
    const double added = highBandDb(out, 6000.0);
    EXPECT_LT(added, -60.0) << "a band's change put something " << added << " dB under the tone above 6 kHz";
    // And the last change arrived: a +12 dB shelf from 80 Hz lifts the tone.
    EXPECT_GT(peak(Signal(out.end() - 4096, out.end())), 0.25f * 2.5f);
}

TEST(ParametricEqTest, ItStaysStableWhateverTheBandsDo)
{
    // Full-scale noise, and every band given a new type, frequency, gain
    // and Q every 32-frame cycle, the extremes included.
    ParametricEq block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    std::mt19937 random(58);
    std::uniform_int_distribution<std::size_t> anyType(0, ParametricEq::kTypeIds.size() - 1);
    std::uniform_real_distribution<float> anyOctave(std::log2(20.0f), std::log2(20000.0f));
    std::uniform_real_distribution<float> anyGain(-18.0f, 18.0f);
    std::uniform_real_distribution<float> anyQ(std::log(0.1f), std::log(18.0f));
    float loudest = 0.0f;
    for (int cycle = 0; cycle < 1500; ++cycle) {
        const std::size_t band = static_cast<std::size_t>(cycle) % ParametricEq::kBandCount;
        set(block, band, ParametricEq::Used, ParamValue::ofBool(true));
        set(block, band, ParametricEq::Type, ParamValue::ofEnum(std::string{ParametricEq::kTypeIds[anyType(random)]}));
        set(block, band, ParametricEq::Frequency, ParamValue::ofFloat(std::exp2(anyOctave(random))));
        set(block, band, ParametricEq::Gain, ParamValue::ofFloat(anyGain(random)));
        set(block, band, ParametricEq::Q, ParamValue::ofFloat(std::exp(anyQ(random))));
        const MultiSignal result =
            runner.run(spread(whiteNoise(32, 0.1f, static_cast<std::uint32_t>(cycle) + 1), 2), CycleShape::fixed(32));
        for (const float sample : result[0]) {
            ASSERT_TRUE(std::isfinite(sample));
            loudest = std::max(loudest, std::abs(sample));
        }
    }
    EXPECT_LT(loudest, 100.0f) << "the cascade ran away";
}

TEST(ParametricEqTest, ItSoundsTheSameWhateverTheQuantum)
{
    InvarianceOptions options;
    options.inputChannels = 2;
    options.outputChannels = 2;
    const InvarianceReport report = checkBlockSizeInvariance(
        [] {
            auto block = std::make_unique<ParametricEq>();
            place(*block, settings()[0]);
            return std::unique_ptr<Block>(std::move(block));
        },
        spread(whiteNoise(8192, 0.2f, 7), 2),
        options);
    EXPECT_TRUE(report.passed) << report.summary;
}

TEST(ParametricEqTest, WhatWasSetIsWhatComesBack)
{
    ParametricEq block;
    place(block, settings()[1]);
    ParametricEq opened;
    ASSERT_TRUE(opened.loadState(block.saveState()).has_value());
    EXPECT_EQ(opened.bands(), block.bands());
    EXPECT_EQ(opened.param("band2Type").text(), "highShelf");

    ParametricEq refusing;
    EXPECT_FALSE(refusing.setParam("band1Gain", ParamValue::ofFloat(19.0f)).has_value());
    EXPECT_FALSE(refusing.setParam("band1Q", ParamValue::ofFloat(0.05f)).has_value());
    EXPECT_FALSE(refusing.setParam("band1Frequency", ParamValue::ofFloat(21000.0f)).has_value());
    EXPECT_FALSE(refusing.setParam("band1Type", ParamValue::ofEnum("wobble")).has_value());
}

TEST(ParametricEqTest, ARunTouchesTheAllocatorNotAtAll)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    ParametricEq block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    // Bands gliding, one changing type, one starting and one stopping, so
    // every path of the audio thread is watched.
    place(block, settings()[0]);
    set(block, 0, ParametricEq::Type, ParamValue::ofEnum("notch"));
    set(block, 5, ParametricEq::Used, ParamValue::ofBool(true));
    std::size_t allocations = 1;
    const MultiSignal out = runner.runWatchingForAllocations(
        spread(whiteNoise(9600, 0.2f, 3), 2), CycleShape::random(1, 512, 3), allocations);
    EXPECT_EQ(allocations, 0U);
}
