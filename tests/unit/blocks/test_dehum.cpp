// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Dehum block: hum and its harmonics taken out by 30 dB and more,
// what lies between them left alone, and nothing at all at no amount. And its
// Auto setting: a hum whose base drifts, followed.

#include "allocationtrap.h"
#include "analysis.h"
#include "blocks/builtinblocks.h"
#include "blocks/dehum.h"
#include "blocksizeinvariance.h"
#include "dsp/biquad.h"
#include "dsp/blockregistry.h"
#include "offlinerunner.h"
#include "rt/meter.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <cmath>
#include <complex>
#include <cstdio>
#include <functional>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

using namespace rb;
using namespace rb::blocks;
using namespace rb::dsp;
using namespace rb::test;

namespace
{

[[nodiscard]] std::unique_ptr<Dehum>
dehum(float base, float amount, std::int64_t harmonics = 10, bool automatic = false)
{
    auto block = std::make_unique<Dehum>();
    EXPECT_TRUE(block->setParam("base", ParamValue::ofFloat(base)).has_value());
    EXPECT_TRUE(block->setParam("amount", ParamValue::ofFloat(amount)).has_value());
    EXPECT_TRUE(block->setParam("harmonics", ParamValue::ofInt(harmonics)).has_value());
    EXPECT_TRUE(block->setParam("auto", ParamValue::ofBool(automatic)).has_value());
    return block;
}

/// Hum whose base follows \a frequency(t), with the phase of its base at
/// every sample, so what is left of each harmonic can be measured even
/// while it moves.
struct DriftingHum {
    Signal signal;
    std::vector<double> phase;
    double amplitude = 0.0;
};

[[nodiscard]] DriftingHum driftingHum(std::size_t frames,
                                      const std::function<double(double)> &frequency,
                                      float levelDb,
                                      double on = 0.0,
                                      double rate = kTestSampleRate)
{
    DriftingHum hum;
    hum.signal.assign(frames, 0.0f);
    hum.phase.resize(frames);
    hum.amplitude = std::pow(10.0, levelDb / 20.0);
    double phase = 0.0;
    for (std::size_t n = 0; n < frames; ++n) {
        const double t = static_cast<double>(n) / rate;
        phase += 2.0 * std::numbers::pi * frequency(t) / rate;
        hum.phase[n] = phase;
        if (t < on) {
            continue;
        }
        double sum = 0.0;
        for (int number = 1; number <= 10; ++number) {
            sum += std::cos(number * phase + 0.3 * number);
        }
        hum.signal[n] = static_cast<float>(hum.amplitude * sum);
    }
    return hum;
}

/// What is left of harmonic \a number between \a from and \a to seconds, in
/// dB against what went in: the output turned down by that harmonic's own
/// phase and averaged under a Hann window, which leaves the other harmonics
/// and whatever else is playing far below.
[[nodiscard]] double
leftDb(const Signal &out, const DriftingHum &hum, int number, double from, double to, double rate = kTestSampleRate)
{
    const auto first = static_cast<std::size_t>(from * rate);
    const auto last = std::min(out.size(), static_cast<std::size_t>(to * rate));
    std::complex<double> sum = 0.0;
    double weights = 0.0;
    for (std::size_t n = first; n < last; ++n) {
        const double w = 0.5
            - 0.5
                * std::cos(2.0 * std::numbers::pi * static_cast<double>(n - first) / static_cast<double>(last - first));
        sum += w * static_cast<double>(out[n]) * std::polar(1.0, -(number * hum.phase[n] + 0.3 * number));
        weights += w;
    }
    const double amplitude = 2.0 * std::abs(sum) / weights;
    return 20.0 * std::log10(std::max(amplitude, 1.0e-12) / hum.amplitude);
}

/// Hum: the base and its first ten harmonics, each at \a levelDb.
[[nodiscard]] Signal hum(std::size_t frames, double base, float levelDb, double rate = kTestSampleRate)
{
    Signal signal(frames, 0.0f);
    const auto amplitude = static_cast<float>(std::pow(10.0, levelDb / 20.0));
    for (int number = 1; number <= 10; ++number) {
        const Signal harmonic = sine(frames, base * number, rate, amplitude, 0.3 * number);
        for (std::size_t n = 0; n < frames; ++n) {
            signal[n] += harmonic[n];
        }
    }
    return signal;
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

} // namespace

TEST(DehumTest, ItIsAnAdvancedBlockWithAStereoPairEachSide)
{
    const Dehum block;
    EXPECT_EQ(block.info().typeId, "advanced.dehum");
    EXPECT_EQ(block.info().category, BlockCategory::Advanced);
    EXPECT_EQ(block.info().face, FaceKind::Value);
    EXPECT_EQ(block.info().inputs.maxCount, 2U);
    EXPECT_EQ(block.info().outputs.maxCount, 2U);
    EXPECT_EQ(block.info().latencySamples, 0U);
    EXPECT_FLOAT_EQ(*block.param("base").asFloat(), 50.0f);
    EXPECT_FLOAT_EQ(*block.param("amount").asFloat(), 5.0f);
    EXPECT_EQ(*block.param("harmonics").asInt(), 10);

    BlockRegistry registry;
    ASSERT_TRUE(registerBuiltinBlocks(registry));
    EXPECT_TRUE(registry.contains("advanced.dehum")) << "Dehum is not a block a session can add";
}

TEST(DehumTest, AtNoAmountItChangesNotOneBit)
{
    const auto block = dehum(50.0f, 0.0f);
    BlockRunner runner(*block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    MultiSignal in;
    in.push_back(whiteNoise(9600, 0.8f, 1));
    in.push_back(pinkNoise(9600, 0.8f, 2));
    const MultiSignal out = runner.run(in, CycleShape::random(1, 512, 5));
    EXPECT_EQ(out[0], in[0]);
    EXPECT_EQ(out[1], in[1]);
}

TEST(DehumTest, AtFullAmountHumAndItsHarmonicsGoByThirtyDecibelsAndMore)
{
    // **The acceptance criterion, measured on hum.** The base and its ten
    // harmonics together, at 50 Hz, at 60, and at a supply running 1.3 Hz
    // fast with the base tuned to it.
    for (const double base : {50.0, 60.0, 51.3}) {
        const auto block = dehum(static_cast<float>(base), 10.0f);
        BlockRunner runner(*block, 2, 2);
        ASSERT_TRUE(runner.prepare().has_value());
        const MultiSignal out = runner.run(spread(hum(96000, base, -20.0f), 2));
        // After the notches have rung in: they are narrow, and a narrow
        // filter takes a moment to settle on a tone that starts at once.
        const Signal settled(out[0].begin() + 48000, out[0].end());
        for (int number = 1; number <= 10; ++number) {
            const double left = 20.0 * std::log10(amplitudeAt(settled, base * number) / std::pow(10.0, -20.0 / 20.0));
            EXPECT_LT(left, -30.0) << "harmonic " << number << " of " << base << " Hz is only " << left << " dB down";
        }
    }
}

TEST(DehumTest, WhatLiesBetweenTheHarmonicsIsLeftAlone)
{
    // **The acceptance criterion, measured on everything else.** The
    // block's response from an impulse, half way between every pair of
    // harmonics and well above the tenth, at full amount: within half a
    // decibel of untouched. And the whole response is the design's.
    for (const double rate : {44100.0, 48000.0}) {
        const auto block = dehum(50.0f, 10.0f);
        BlockRunner runner(*block, 2, 2, rate);
        ASSERT_TRUE(runner.prepare().has_value());
        // Long enough for a notch 2 Hz wide to have rung down.
        const MultiSignal out = runner.run(spread(impulse(static_cast<std::size_t>(rate * 3.0)), 2));
        std::vector<double> between;
        for (int number = 1; number <= 10; ++number) {
            between.push_back(50.0 * (number + 0.5));
        }
        for (const double far : {1000.0, 3000.0, 10000.0, 18000.0}) {
            between.push_back(far);
        }
        for (const double f : between) {
            const double measured = measuredDb(out[0], f, rate);
            EXPECT_NEAR(measured, 0.0, 0.5) << f << " Hz at " << rate;
            EXPECT_NEAR(measured, Dehum::magnitudeDb(f, 50.0, 10.0, 10, rate), 0.05) << f << " Hz at " << rate;
        }
        // Where the design says it is at the notches, too.
        for (int number = 1; number <= 10; ++number) {
            const double f = 50.0 * number + 0.5;
            EXPECT_NEAR(measuredDb(out[0], f, rate), Dehum::magnitudeDb(f, 50.0, 10.0, 10, rate), 0.1) << f;
        }
    }
}

TEST(DehumTest, TheAmountIsTheDepth)
{
    // 6 dB a step, at a harmonic's centre.
    for (const double amount : {1.0, 2.5, 5.0}) {
        EXPECT_NEAR(Dehum::magnitudeDb(100.0, 50.0, amount, 10, 48000.0), -6.0 * amount, 0.05) << amount;
    }
    // Fewer harmonics: the fourth is left alone when three are asked for.
    EXPECT_NEAR(Dehum::magnitudeDb(200.0, 50.0, 10.0, 3, 48000.0), 0.0, 0.01);
    EXPECT_LT(Dehum::magnitudeDb(150.0, 50.0, 10.0, 3, 48000.0), -50.0);
}

TEST(DehumTest, TurningItAndRetuningItDoesNotClick)
{
    // A 325 Hz tone, between harmonics whatever the base, while the amount
    // is thrown from nothing to all of it and the base from 48 to 62 Hz. The
    // tone has nothing above 6 kHz; whatever turns up there is the block's
    // doing. (Low, so that the measuring filter's own leak of the tone is
    // far below what is measured: a 1.2 kHz tone leaks through it at -56.)
    const auto block = dehum(50.0f, 0.0f);
    BlockRunner runner(*block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    Signal out;
    double phase = 0.0;
    for (int cycle = 0; cycle < 300; ++cycle) {
        if (cycle == 40) {
            ASSERT_TRUE(block->setParam("amount", ParamValue::ofFloat(10.0f)).has_value());
        }
        if (cycle == 100) {
            ASSERT_TRUE(block->setParam("base", ParamValue::ofFloat(62.0f)).has_value());
        }
        if (cycle == 160) {
            ASSERT_TRUE(block->setParam("base", ParamValue::ofFloat(48.0f)).has_value());
            ASSERT_TRUE(block->setParam("harmonics", ParamValue::ofInt(2)).has_value());
        }
        if (cycle == 220) {
            ASSERT_TRUE(block->setParam("amount", ParamValue::ofFloat(0.0f)).has_value());
        }
        Signal piece(256);
        for (float &sample : piece) {
            sample = 0.25f * static_cast<float>(std::sin(phase));
            phase += 2.0 * std::numbers::pi * 325.0 / kTestSampleRate;
        }
        const MultiSignal result = runner.run(spread(piece, 2), CycleShape::fixed(256));
        out.insert(out.end(), result[0].begin(), result[0].end());
    }
    const double added = highBandDb(out, 6000.0);
    EXPECT_LT(added, -60.0) << "turning it put something " << added << " dB under the tone above 6 kHz";
}

TEST(DehumTest, ItSoundsTheSameWhateverTheQuantum)
{
    InvarianceOptions options;
    options.inputChannels = 2;
    options.outputChannels = 2;
    const InvarianceReport report = checkBlockSizeInvariance(
        [] { return std::unique_ptr<Block>(dehum(60.0f, 8.0f)); }, spread(whiteNoise(8192, 0.3f, 7), 2), options);
    EXPECT_TRUE(report.passed) << report.summary;
}

TEST(DehumTest, WhatWasSetIsWhatComesBack)
{
    const auto block = dehum(59.2f, 3.4f, 6);
    Dehum opened;
    ASSERT_TRUE(opened.loadState(block->saveState()).has_value());
    EXPECT_FLOAT_EQ(*opened.param("base").asFloat(), 59.2f);
    EXPECT_FLOAT_EQ(*opened.param("amount").asFloat(), 3.4f);
    EXPECT_EQ(*opened.param("harmonics").asInt(), 6);

    Dehum refusing;
    EXPECT_FALSE(refusing.setParam("base", ParamValue::ofFloat(45.0f)).has_value());
    EXPECT_FALSE(refusing.setParam("amount", ParamValue::ofFloat(11.0f)).has_value());
    EXPECT_FALSE(refusing.setParam("harmonics", ParamValue::ofInt(11)).has_value());
}

TEST(DehumTest, ARunTouchesTheAllocatorNotAtAll)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    const auto block = dehum(50.0f, 2.0f);
    BlockRunner runner(*block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    // The base and the amount on their way, so the glides are watched too.
    ASSERT_TRUE(block->setParam("base", ParamValue::ofFloat(60.0f)).has_value());
    ASSERT_TRUE(block->setParam("amount", ParamValue::ofFloat(9.0f)).has_value());
    std::size_t allocations = 1;
    const MultiSignal out = runner.runWatchingForAllocations(
        spread(whiteNoise(9600, 0.3f, 3), 2), CycleShape::random(1, 512, 3), allocations);
    EXPECT_EQ(allocations, 0U);
}

TEST(DehumTest, AutoIsOffByDefaultAndSavedWithTheRest)
{
    const Dehum block;
    EXPECT_EQ(*block.param("auto").asBool(), false);
    const auto automatic = dehum(51.0f, 7.0f, 10, true);
    Dehum opened;
    ASSERT_TRUE(opened.loadState(automatic->saveState()).has_value());
    EXPECT_EQ(*opened.param("auto").asBool(), true);
    EXPECT_FLOAT_EQ(*opened.param("base").asFloat(), 51.0f);
}

TEST(DehumTest, WithAutoAHumThatDriftsIsHeldThirtyDecibelsDownAtEveryHarmonic)
{
    // **The acceptance criterion.** A hum on 49.5 Hz, then its base sweeping
    // to 50.5 Hz over ten seconds, the tenth harmonic moving 10 Hz with it.
    // With Auto on and Base at 50, every harmonic is at least 30 dB down in
    // every quarter second of the sweep; the test asks for 33, the margin
    // Auto's wider notches are there for (with the notches as narrow as
    // without Auto, the tenth is 30.9 dB down). With Auto off, the notches
    // stay on 50 Hz and the tenth harmonic escapes them.
    const auto frequency = [](double t) { return 49.5 + std::clamp((t - 2.0) / 10.0, 0.0, 1.0); };
    const DriftingHum hum = driftingHum(static_cast<std::size_t>(12.0 * kTestSampleRate), frequency, -20.0f);
    for (const bool automatic : {true, false}) {
        const auto block = dehum(50.0f, 10.0f, 10, automatic);
        BlockRunner runner(*block, 2, 2);
        ASSERT_TRUE(runner.prepare().has_value());
        const MultiSignal out = runner.run(spread(hum.signal, 2));
        std::vector<double> worst(11, -200.0);
        for (double from = 2.0; from + 0.25 <= 12.0; from += 0.25) {
            for (int number = 1; number <= 10; ++number) {
                worst[number] = std::max(worst[number], leftDb(out[0], hum, number, from, from + 0.25));
            }
        }
        std::printf("[measure] a hum drifting 49.5 to 50.5 Hz, Auto %s: worst left of each harmonic (dB):",
                    automatic ? "on" : "off");
        for (int number = 1; number <= 10; ++number) {
            std::printf(" %.1f", worst[number]);
        }
        std::printf("\n");
        if (automatic) {
            for (int number = 1; number <= 10; ++number) {
                EXPECT_LT(worst[number], -33.0) << "harmonic " << number << " is only " << worst[number] << " dB down";
            }
        } else {
            EXPECT_GT(worst[10], -30.0) << "with Auto off the tenth harmonic should escape a drift of 5 Hz";
        }
    }
}

TEST(DehumTest, WithAutoTheHumIsDownWithinASecondOfAppearing)
{
    // **The acceptance criterion.** A second of silence, then a supply
    // running 0.9 Hz fast: a second after it starts, the tracker is locked
    // and every harmonic is 30 dB down.
    const DriftingHum hum =
        driftingHum(static_cast<std::size_t>(3.0 * kTestSampleRate), [](double) { return 50.9; }, -20.0f, 1.0);
    const auto block = dehum(50.0f, 10.0f, 10, true);
    BlockRunner runner(*block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const MultiSignal first = runner.run(
        spread(Signal(hum.signal.begin(), hum.signal.begin() + static_cast<std::ptrdiff_t>(2.0 * kTestSampleRate)), 2));
    EXPECT_TRUE(block->tracker().locked()) << "not locked a second after the hum appeared";
    const MultiSignal rest = runner.run(
        spread(Signal(hum.signal.begin() + static_cast<std::ptrdiff_t>(2.0 * kTestSampleRate), hum.signal.end()), 2));
    Signal out = first[0];
    out.insert(out.end(), rest[0].begin(), rest[0].end());
    for (int number = 1; number <= 10; ++number) {
        const double left = leftDb(out, hum, number, 2.0, 2.25);
        EXPECT_LT(left, -30.0) << "harmonic " << number << " is only " << left << " dB down a second in";
    }
}

TEST(DehumTest, WithAutoAndNoHumTheNotchesDoNotWander)
{
    // Noise, loud in the hum's range, and nothing steady in it: the notches
    // stay on Base, to the last bit, and the block says it is holding.
    const auto block = dehum(50.0f, 10.0f, 10, true);
    rt::BlockMeter meter;
    BlockRunner runner(*block, 2, 2);
    runner.setMeter(&meter);
    ASSERT_TRUE(runner.prepare().has_value());
    const Signal noise = pinkNoise(static_cast<std::size_t>(8.0 * kTestSampleRate), 0.5f, 3);
    for (std::size_t from = 0; from < noise.size(); from += 4800) {
        const Signal piece(noise.begin() + static_cast<std::ptrdiff_t>(from),
                           noise.begin() + static_cast<std::ptrdiff_t>(std::min(from + 4800, noise.size())));
        (void)runner.run(spread(piece, 2));
        EXPECT_EQ(block->tracker().estimate(), 50.0);
        EXPECT_FALSE(block->tracker().locked());
        EXPECT_EQ(meter.value(Dehum::kBaseValue), 50.0f);
        EXPECT_EQ(meter.value(Dehum::kLockedValue), 0.0f);
    }
}

TEST(DehumTest, WithAutoWhatLiesBetweenTheHarmonicsIsLeftAlone)
{
    // **The acceptance criterion, with Auto on.** Holding, with no hum, the
    // block is a filter like any other, and its response from an impulse is
    // the wider notches' design, within half a decibel of untouched between
    // every pair of harmonics and far above them.
    const auto holding = dehum(50.0f, 10.0f, 10, true);
    BlockRunner impulseRunner(*holding, 2, 2);
    ASSERT_TRUE(impulseRunner.prepare().has_value());
    const MultiSignal response = impulseRunner.run(spread(impulse(static_cast<std::size_t>(kTestSampleRate * 3.0)), 2));
    for (int number = 1; number <= 10; ++number) {
        const double f = 50.0 * (number + 0.5);
        const double measured = measuredDb(response[0], f, kTestSampleRate);
        EXPECT_NEAR(measured, 0.0, 0.5) << f << " Hz";
        EXPECT_NEAR(measured, Dehum::magnitudeDb(f, 50.0, 10.0, 10, kTestSampleRate, Dehum::kAutoWidthHz), 0.05) << f;
    }
    for (const double far : {1000.0, 3000.0, 10000.0, 18000.0}) {
        EXPECT_NEAR(measuredDb(response[0], far, kTestSampleRate), 0.0, 0.5) << far << " Hz";
    }

    // **And tracking a hum**: tones between its harmonics and far above them
    // come out within half a decibel of how they went in, once it has locked.
    const auto hz = [](double) { return 50.3; };
    const DriftingHum hum = driftingHum(static_cast<std::size_t>(6.0 * kTestSampleRate), hz, -30.0f);
    const std::vector<double> program{
        75.4, 125.8, 176.1, 226.4, 276.7, 327.0, 377.2, 427.6, 477.9, 528.2, 1000.0, 3000.0, 10000.0};
    Signal in = hum.signal;
    for (const double f : program) {
        const Signal tone = sine(in.size(), f, kTestSampleRate, 0.03f, f);
        for (std::size_t n = 0; n < in.size(); ++n) {
            in[n] += tone[n];
        }
    }
    const auto tracking = dehum(50.0f, 10.0f, 10, true);
    BlockRunner runner(*tracking, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const MultiSignal out = runner.run(spread(in, 2));
    EXPECT_TRUE(tracking->tracker().locked()) << "it never locked onto the hum under the program";
    const auto from = static_cast<std::ptrdiff_t>(3.0 * kTestSampleRate);
    const Signal inLate(in.begin() + from, in.end());
    const Signal outLate(out[0].begin() + from, out[0].end());
    double worst = 0.0;
    for (const double f : program) {
        const double change = 20.0 * std::log10(amplitudeAt(outLate, f) / amplitudeAt(inLate, f));
        worst = std::max(worst, std::abs(change));
        EXPECT_NEAR(change, 0.0, 0.5) << f << " Hz";
    }
    std::printf("[measure] program between the harmonics, Auto tracking 50.3 Hz: changed by at most %.2f dB\n", worst);
}

TEST(DehumTest, WithAutoItSoundsTheSameWhateverTheQuantum)
{
    const DriftingHum hum = driftingHum(8192 * 4, [](double t) { return 50.6 + 0.05 * t; }, -20.0f);
    Signal in = hum.signal;
    const Signal noise = whiteNoise(in.size(), 0.05f, 7);
    for (std::size_t n = 0; n < in.size(); ++n) {
        in[n] += noise[n];
    }
    InvarianceOptions options;
    options.inputChannels = 2;
    options.outputChannels = 2;
    const InvarianceReport report = checkBlockSizeInvariance(
        [] { return std::unique_ptr<Block>(dehum(50.0f, 8.0f, 10, true)); }, spread(in, 2), options);
    EXPECT_TRUE(report.passed) << report.summary;
}

TEST(DehumTest, WithAutoARunTouchesTheAllocatorNotAtAll)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    const auto block = dehum(50.0f, 9.0f, 10, true);
    BlockRunner runner(*block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const DriftingHum hum =
        driftingHum(static_cast<std::size_t>(2.0 * kTestSampleRate), [](double) { return 50.7; }, -20.0f);
    // Base moved while tracking, so re-aiming is watched too.
    ASSERT_TRUE(block->setParam("base", ParamValue::ofFloat(51.0f)).has_value());
    std::size_t allocations = 1;
    (void)runner.runWatchingForAllocations(spread(hum.signal, 2), CycleShape::random(1, 512, 3), allocations);
    EXPECT_EQ(allocations, 0U);
}

TEST(DehumTest, SwitchingAutoOnAndOffDoesNotClick)
{
    // A 325 Hz tone over a hum at 51.8 Hz, Base at 50: Auto switched on
    // (the notches glide to the hum, the tenth by 18 Hz), then off (they
    // glide back). Nothing turns up above 6 kHz, where neither the tone nor
    // the hum has anything.
    const DriftingHum hum = driftingHum(static_cast<std::size_t>(300 * 256), [](double) { return 51.8; }, -30.0f);
    const auto block = dehum(50.0f, 10.0f, 10, false);
    BlockRunner runner(*block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    Signal out;
    double phase = 0.0;
    for (int cycle = 0; cycle < 300; ++cycle) {
        if (cycle == 40) {
            ASSERT_TRUE(block->setParam("auto", ParamValue::ofBool(true)).has_value());
        }
        if (cycle == 220) {
            ASSERT_TRUE(block->setParam("auto", ParamValue::ofBool(false)).has_value());
        }
        Signal piece(256);
        for (std::size_t n = 0; n < piece.size(); ++n) {
            piece[n] =
                0.25f * static_cast<float>(std::sin(phase)) + hum.signal[static_cast<std::size_t>(cycle) * 256 + n];
            phase += 2.0 * std::numbers::pi * 325.0 / kTestSampleRate;
        }
        const MultiSignal result = runner.run(spread(piece, 2), CycleShape::fixed(256));
        out.insert(out.end(), result[0].begin(), result[0].end());
    }
    const double added = highBandDb(out, 6000.0);
    EXPECT_LT(added, -60.0) << "switching Auto put something " << added << " dB under the tone above 6 kHz";
}

TEST(DehumTest, ItPublishesWhereTheNotchesAreAndWhetherAutoHasLocked)
{
    rt::BlockMeter meter;
    const auto manual = dehum(59.0f, 5.0f);
    BlockRunner manualRunner(*manual, 2, 2);
    manualRunner.setMeter(&meter);
    ASSERT_TRUE(manualRunner.prepare().has_value());
    (void)manualRunner.run(spread(silence(4800), 2));
    EXPECT_FLOAT_EQ(meter.value(Dehum::kBaseValue), 59.0f);
    EXPECT_FLOAT_EQ(meter.value(Dehum::kLockedValue), -1.0f) << "Auto is off";

    const auto automatic = dehum(50.0f, 5.0f, 10, true);
    BlockRunner runner(*automatic, 2, 2);
    runner.setMeter(&meter);
    ASSERT_TRUE(runner.prepare().has_value());
    const DriftingHum hum =
        driftingHum(static_cast<std::size_t>(2.0 * kTestSampleRate), [](double) { return 50.4; }, -20.0f);
    (void)runner.run(spread(hum.signal, 2));
    EXPECT_NEAR(meter.value(Dehum::kBaseValue), 50.4f, 0.01f);
    EXPECT_FLOAT_EQ(meter.value(Dehum::kLockedValue), 1.0f);
}
