// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Speech Denoise: noisy speech comes out closer to the speech (SI-SDR),
// the noise between words down, the words kept; the latency is what it
// reports; RNNoise costs under 2 % of a core an instance.

#include "allocationtrap.h"
#include "analysis.h"
#include "blocks/speechdenoise.h"
#include "blocksizeinvariance.h"
#include "dsp/biquad.h"
#include "dsp/decibel.h"
#include "offlinerunner.h"
#include "rt/meter.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <numbers>
#include <random>

using namespace rb;
using namespace rb::blocks;
using namespace rb::dsp;
using namespace rb::test;

namespace
{

constexpr std::size_t kSecond = 48000;
constexpr std::size_t kLatency = SpeechDenoise::kLatency;

/// A room's background: pink noise, darkened a little, as a fan or an air
/// conditioner sounds, at \a levelDb RMS.
[[nodiscard]] Signal fanNoise(std::size_t frames, float levelDb, std::uint32_t seed)
{
    Signal noise = pinkNoise(frames, 1.0f, seed);
    Biquad shelf;
    shelf.setCoefficients(designBiquad({FilterShape::LowPass, 6000.0, kButterworthQ, 0.0}, kTestSampleRate));
    for (float &sample : noise) {
        sample = shelf.process(sample);
    }
    const float scale = dbToGain(levelDb) / rms(noise);
    for (float &sample : noise) {
        sample *= scale;
    }
    return noise;
}

/// Something with a voice's shape: a buzz whose pitch wanders between 100
/// and 180 Hz, its harmonics up to 4 kHz under two formants that move, in
/// syllables of a fifth of a second at their own levels, and half a second
/// of silence every two seconds. Nothing like intelligible, but what a
/// speech model listens for — a pitch, formants, onsets, gaps — is there.
[[nodiscard]] Signal voice(std::size_t frames)
{
    Signal out(frames, 0.0f);
    std::mt19937 random(83);
    std::uniform_real_distribution<float> level(-6.0f, 0.0f);
    std::uniform_real_distribution<double> vowel(0.0, 1.0);
    float syllableGain = 1.0f;
    double first = 600.0;
    double second = 1800.0;
    const std::size_t syllable = kSecond / 5;
    double phase = 0.0;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const double t = static_cast<double>(frame) / kTestSampleRate;
        const double pitch = 140.0 + 40.0 * std::sin(2.0 * std::numbers::pi * 0.7 * t);
        phase += 2.0 * std::numbers::pi * pitch / kTestSampleRate;
        if (frame % syllable == 0) {
            syllableGain = dbToGain(level(random));
            first = 400.0 + 400.0 * vowel(random);
            second = 1200.0 + 1200.0 * vowel(random);
        }
        const bool pause = frame % (2 * kSecond) >= 2 * kSecond - kSecond / 2;
        if (pause) {
            continue;
        }
        double sample = 0.0;
        for (int harmonic = 1; harmonic * pitch < 4000.0; ++harmonic) {
            const double hz = harmonic * pitch;
            const double formants = std::exp(-std::pow((hz - first) / 250.0, 2.0))
                + 0.5 * std::exp(-std::pow((hz - second) / 400.0, 2.0)) + 0.03;
            sample += formants / harmonic * std::sin(harmonic * phase);
        }
        const double along = static_cast<double>(frame % syllable) / static_cast<double>(syllable);
        out[frame] = static_cast<float>(sample * (0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * along))) * syllableGain;
    }
    const float scale = dbToGain(-18.0f) / peak(out);
    for (float &sample : out) {
        sample *= scale;
    }
    return out;
}

[[nodiscard]] Signal add(const Signal &a, const Signal &b)
{
    Signal sum(a.size());
    for (std::size_t n = 0; n < a.size(); ++n) {
        sum[n] = a[n] + b[n];
    }
    return sum;
}

/// \a signal from \a from for \a length samples.
[[nodiscard]] Signal part(const Signal &signal, std::size_t from, std::size_t length)
{
    return Signal(signal.begin() + static_cast<Signal::difference_type>(from),
                  signal.begin() + static_cast<Signal::difference_type>(std::min(from + length, signal.size())));
}

/// Scale-invariant signal-to-distortion ratio of \a estimate against
/// \a reference, in dB: how much of the estimate is the reference, at
/// whatever level, against everything else in it.
[[nodiscard]] double siSdr(const Signal &estimate, const Signal &reference)
{
    double dot = 0.0;
    double energy = 0.0;
    for (std::size_t n = 0; n < reference.size(); ++n) {
        dot += static_cast<double>(estimate[n]) * reference[n];
        energy += static_cast<double>(reference[n]) * reference[n];
    }
    const double scale = dot / energy;
    double target = 0.0;
    double error = 0.0;
    for (std::size_t n = 0; n < reference.size(); ++n) {
        const double wanted = scale * reference[n];
        target += wanted * wanted;
        error += (estimate[n] - wanted) * (estimate[n] - wanted);
    }
    return 10.0 * std::log10(target / error);
}

void setReduction(SpeechDenoise &block, float db)
{
    ASSERT_TRUE(block.setParam("reduction", ParamValue::ofFloat(db)).has_value());
}

} // namespace

TEST(SpeechDenoiseTest, NoisySpeechComesOutCloserToTheSpeech)
{
    // The criterion. Eight seconds of the voice over the fan at 5 dB SNR:
    // after the block, the output is closer to the voice alone by SI-SDR,
    // the fan between words is far down, and the words are about as loud.
    SpeechDenoise block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    ASSERT_TRUE(block.active());

    const Signal speech = voice(kSecond * 8);
    const Signal fan = fanNoise(kSecond * 8, rmsDb(speech) - 5.0f, 1);
    Signal mixed = add(speech, fan);
    mixed.resize(mixed.size() + kLatency, 0.0f);
    const MultiSignal out = runner.run(MultiSignal{mixed, mixed}, CycleShape::fixed(256));
    const Signal heard = part(out[0], kLatency, speech.size());

    // After the first second, which the model spends settling.
    const Signal reference = part(speech, kSecond, kSecond * 7);
    const double before = siSdr(part(mixed, kSecond, kSecond * 7), reference);
    const double after = siSdr(part(heard, kSecond, kSecond * 7), reference);
    std::cout << "speech denoise: SI-SDR " << before << " dB in, " << after << " dB out, " << after - before
              << " dB better\n";
    EXPECT_GT(after - before, 6.0);

    // The pause from 3.5 s to 4 s, less the gate's hold and fade, and a
    // word at 2.1 s.
    const std::size_t pause = kSecond * 7 / 2 + kSecond / 4;
    const float fanIn = rmsDb(part(fan, pause, kSecond / 5));
    const float fanOut = rmsDb(part(heard, pause, kSecond / 5));
    const float wordIn = rmsDb(part(speech, kSecond * 21 / 10, kSecond / 5));
    const float wordOut = rmsDb(part(heard, kSecond * 21 / 10, kSecond / 5));
    std::cout << "speech denoise: between words " << fanIn << " dB in, " << fanOut << " dB out; a word " << wordIn
              << " dB alone, " << wordOut << " dB out\n";
    EXPECT_LT(fanOut, fanIn - 30.0f);
    EXPECT_NEAR(wordOut, wordIn, 3.0f);
}

TEST(SpeechDenoiseTest, TheReductionLimitsHowFarAnythingGoesDown)
{
    // With the limit at 10 dB, the fan between words is 10 dB down and no
    // further; at 0 dB the block is the input, late by the latency.
    const Signal speech = voice(kSecond * 5);
    const Signal fan = fanNoise(kSecond * 5, rmsDb(speech) - 5.0f, 2);
    const Signal mixed = add(speech, fan);
    const std::size_t pause = kSecond * 7 / 2 + kSecond / 4;

    SpeechDenoise limited;
    setReduction(limited, 10.0f);
    BlockRunner runner(limited, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const Signal out = runner.run(MultiSignal{mixed, mixed})[0];
    const float down = rmsDb(part(fan, pause, kSecond / 5)) - rmsDb(part(out, pause + kLatency, kSecond / 5));
    std::cout << "speech denoise: limited to 10 dB, the fan between words went down " << down << " dB\n";
    EXPECT_NEAR(down, 10.0f, 0.5f);

    SpeechDenoise none;
    setReduction(none, 0.0f);
    BlockRunner through(none, 2, 2);
    ASSERT_TRUE(through.prepare().has_value());
    const Signal same = through.run(MultiSignal{mixed, mixed})[0];
    for (std::size_t n = kLatency; n < same.size(); ++n) {
        ASSERT_NEAR(same[n], mixed[n - kLatency], 1e-6f) << "at " << n;
    }
}

TEST(SpeechDenoiseTest, WhatTheModelKeepsBetweenWordsGoesDownToTheLimit)
{
    // A steady 300 Hz hum is close enough to a voice that the model keeps
    // most of it. Between words the gate takes it down to the limit anyway:
    // the model says no voice is there.
    const Signal speech = voice(kSecond * 5);
    const Signal hum = sine(kSecond * 5, 300.0, kTestSampleRate, 0.03f);
    const Signal mixed = add(speech, hum);
    SpeechDenoise block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const Signal out = runner.run(MultiSignal{mixed, mixed})[0];
    // The end of the pause from 3.5 s to 4 s, after the hold and the fade.
    const std::size_t from = kSecond * 38 / 10;
    const float in = rmsDb(part(hum, from, kSecond * 3 / 20));
    const float left = rmsDb(part(out, from + kLatency, kSecond * 3 / 20));
    std::cout << "speech denoise: a hum between words " << in << " dB in, " << left << " dB out\n";
    EXPECT_LT(left, in - 35.0f);
}

TEST(SpeechDenoiseTest, MonoIsOneModelForBothSidesAndSwitchingNeitherClicksNorDips)
{
    // Different on each side going in, the same coming out once folded.
    // Switched on and off again while it runs, in the middle of syllables
    // (1.2 to 1.4 s, 2.2 to 2.4 s): no step bigger than the voice's own,
    // and no frame more than 3 dB under both the block left apart and the
    // block left folded — a model started again from nothing is silent for
    // its first frames.
    const Signal speech = voice(kSecond * 3);
    const Signal fan = fanNoise(kSecond * 3, -40.0f, 8);
    const Signal left = add(speech, fan);
    Signal right = speech;
    for (float &sample : right) {
        sample *= 0.5f;
    }

    SpeechDenoise block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const std::size_t on = kSecond * 13 / 10;
    const std::size_t off = kSecond * 23 / 10;
    MultiSignal out = runner.run(MultiSignal{part(left, 0, on), part(right, 0, on)});
    ASSERT_TRUE(block.setParam("mono", ParamValue::ofBool(true)).has_value());
    const MultiSignal folded = runner.run(MultiSignal{part(left, on, off - on), part(right, on, off - on)});
    ASSERT_TRUE(block.setParam("mono", ParamValue::ofBool(false)).has_value());
    const MultiSignal after =
        runner.run(MultiSignal{part(left, off, kSecond * 3 - off), part(right, off, kSecond * 3 - off)});
    for (std::size_t side = 0; side < 2; ++side) {
        out[side].insert(out[side].end(), folded[side].begin(), folded[side].end());
        out[side].insert(out[side].end(), after[side].begin(), after[side].end());
    }

    const auto steady = [&](bool mono) {
        SpeechDenoise fixed;
        EXPECT_TRUE(fixed.setParam("mono", ParamValue::ofBool(mono)).has_value());
        BlockRunner fixedRunner(fixed, 2, 2);
        EXPECT_TRUE(fixedRunner.prepare().has_value());
        return fixedRunner.run(MultiSignal{left, right});
    };
    const MultiSignal apart = steady(false);
    const MultiSignal together = steady(true);

    // Folded: both sides the same, once the fold is in.
    for (std::size_t n = kSecond / 10; n < folded[0].size(); ++n) {
        ASSERT_EQ(folded[0][n], folded[1][n]) << "at " << n;
    }

    const float own = maxSampleStep(std::span<const float>{left}.subspan(kSecond / 2));
    float largest = 0.0f;
    float deepest = 0.0f;
    for (const std::size_t at : {on, off}) {
        for (std::size_t side = 0; side < 2; ++side) {
            largest = std::max(largest, maxSampleStep(std::span<const float>{out[side]}.subspan(at, kSecond / 10)));
            for (std::size_t frame = at; frame < at + kSecond / 10; frame += SpeechDenoise::kFrame) {
                const float either = std::min(rmsDb(part(apart[side], frame, SpeechDenoise::kFrame)),
                                              rmsDb(part(together[side], frame, SpeechDenoise::kFrame)));
                const float came = rmsDb(part(out[side], frame, SpeechDenoise::kFrame));
                deepest = std::max(deepest, either - came);
            }
        }
    }
    std::cout << "speech denoise: switching mono on and off, the largest step " << largest << " (the voice's own "
              << own << "), the deepest dip " << deepest << " dB\n";
    EXPECT_LT(largest, own);
    EXPECT_LT(deepest, 3.0f);
}

TEST(SpeechDenoiseTest, ItIsAsLateAsItSays)
{
    // What the model keeps of the voice lines up with the voice exactly the
    // reported latency later: the correlation peaks there and nowhere else.
    SpeechDenoise block;
    EXPECT_EQ(block.info().latencySamples, kLatency);
    EXPECT_EQ(kLatency, 1440U);
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const Signal speech = voice(kSecond * 3);
    const Signal out = runner.run(MultiSignal{speech, speech}, CycleShape::random(1, 1024, 5))[0];

    std::size_t best = 0;
    double bestSum = 0.0;
    for (std::size_t lag = 0; lag < 3 * SpeechDenoise::kFrame * 2; ++lag) {
        double sum = 0.0;
        for (std::size_t n = kSecond; n + lag < out.size(); ++n) {
            sum += static_cast<double>(speech[n]) * out[n + lag];
        }
        if (sum > bestSum) {
            bestSum = sum;
            best = lag;
        }
    }
    std::cout << "speech denoise: the voice comes out " << best << " samples later\n";
    EXPECT_NEAR(static_cast<double>(best), static_cast<double>(kLatency), 1.0);
}

TEST(SpeechDenoiseTest, AtAnotherRateItPassesTheSoundThroughAndSaysWhy)
{
    SpeechDenoise block;
    rt::BlockMeter meter;
    BlockRunner runner(block, 2, 2, 44100.0);
    runner.setMeter(&meter);
    ASSERT_TRUE(runner.prepare().has_value());
    EXPECT_FALSE(block.active());
    const Signal noise = whiteNoise(8192, 0.3f, 3);
    const Signal out = runner.run(MultiSignal{noise, noise})[0];
    for (std::size_t n = kLatency; n < out.size(); ++n) {
        ASSERT_EQ(out[n], noise[n - kLatency]) << "at " << n;
    }
    EXPECT_EQ(meter.value(SpeechDenoise::kStateValue), SpeechDenoise::kWrongRate);
}

TEST(SpeechDenoiseTest, ItPublishesTheVoiceAndTheGate)
{
    SpeechDenoise block;
    rt::BlockMeter meter;
    BlockRunner runner(block, 2, 2);
    runner.setMeter(&meter);
    ASSERT_TRUE(runner.prepare().has_value());
    const Signal speech = voice(kSecond * 2);
    (void)runner.run(MultiSignal{part(speech, 0, kSecond), part(speech, 0, kSecond)});
    EXPECT_EQ(meter.value(SpeechDenoise::kStateValue), SpeechDenoise::kActive);
    EXPECT_GT(meter.value(SpeechDenoise::kVoiceValue), 0.5f);
    EXPECT_EQ(meter.value(SpeechDenoise::kGateValue), 1.0f);
    // A second of silence: the gate has closed.
    (void)runner.run(MultiSignal{Signal(kSecond, 0.0f), Signal(kSecond, 0.0f)});
    EXPECT_LT(meter.value(SpeechDenoise::kVoiceValue), 0.5f);
    EXPECT_EQ(meter.value(SpeechDenoise::kGateValue), 0.0f);
}

TEST(SpeechDenoiseTest, ItSoundsTheSameWhateverTheQuantum)
{
    InvarianceOptions options;
    options.inputChannels = 2;
    options.outputChannels = 2;
    const Signal speech = add(voice(kSecond * 2), fanNoise(kSecond * 2, -40.0f, 4));
    const InvarianceReport report = checkBlockSizeInvariance(
        [] { return std::unique_ptr<Block>(std::make_unique<SpeechDenoise>()); }, MultiSignal{speech, speech}, options);
    EXPECT_TRUE(report.passed) << report.summary;
}

TEST(SpeechDenoiseTest, ARunAndAResetTouchTheAllocatorNotAtAll)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    SpeechDenoise block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const Signal speech = add(voice(kSecond), fanNoise(kSecond, -40.0f, 5));
    std::size_t allocations = 1;
    (void)runner.runWatchingForAllocations(MultiSignal{speech, speech}, CycleShape::random(1, 512, 6), allocations);
    EXPECT_EQ(allocations, 0U);
    {
        const NoAllocations guard(NoAllocations::OnAllocation::Count);
        block.reset();
        EXPECT_EQ(guard.count(), 0U);
    }
}

TEST(SpeechDenoiseTest, AnInstanceCostsUnderTwoPerCentOfACore)
{
    // The criterion is per RNNoise instance; the block runs one a side, so
    // a minute of stereo is two instances' minute.
    SpeechDenoise block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const Signal speech = add(voice(kSecond * 60), fanNoise(kSecond * 60, -40.0f, 7));
    const auto started = std::chrono::steady_clock::now();
    (void)runner.run(MultiSignal{speech, speech}, CycleShape::fixed(256));
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const double share = took / 60.0 / 2.0;
    std::cerr << "speech denoise: a minute of stereo in " << took << " s, " << share * 100.0
              << " % of a core an instance\n";
#ifdef NDEBUG
    EXPECT_LT(share, 0.02) << "a minute took " << took << " s";
#else
    GTEST_SKIP() << "the figure above is from a debug build";
#endif
}
