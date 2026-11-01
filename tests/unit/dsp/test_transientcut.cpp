// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The frame loop of Sudden Sounds: a frame with no cut is the input to the bit,
// a cut is the share of the excess, and neither silence nor a level that stays
// sets it off.

#include "analysis.h"
#include "dsp/decibel.h"
#include "dsp/fft.h"
#include "dsp/transientcut.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>
#include <vector>

using namespace rb;
using namespace rb::dsp;
using namespace rb::test;

namespace
{

constexpr double kRate = 48000.0;
constexpr std::size_t kSecond = 48000;
constexpr std::size_t kDelay = 35;
constexpr float kGentle = 0.267f;
constexpr float kMedium = 0.467f;
constexpr float kStrong = 0.66f;

[[nodiscard]] Signal run(TransientCut &cut, Signal signal, std::size_t quantum = 256)
{
    const std::span<float> all{signal};
    for (std::size_t at = 0; at < all.size(); at += quantum) {
        cut.process(all.subspan(at, std::min(quantum, all.size() - at)));
    }
    return signal;
}

/// What a range of \a signal's frame holds, by the block's own window, worked
/// out here from nothing but the frame's shape: 48 samples, 13 tapered, 64 bins.
[[nodiscard]] float bandPower(std::span<const float> frame, std::size_t first, std::size_t end)
{
    RealFft fft;
    fft.prepare(64);
    std::vector<float> padded(64, 0.0f);
    for (std::size_t n = 0; n < 48; ++n) {
        const std::size_t edge = std::min(n, 47 - n);
        const float window = edge < 13
            ? static_cast<float>(0.5 - 0.5 * std::cos(std::numbers::pi * (static_cast<double>(edge) + 0.5) / 13.0))
            : 1.0f;
        padded[n] = frame[n] * window;
    }
    std::vector<std::complex<float>> spectrum(fft.bins());
    fft.forward(padded, spectrum);
    float power = 0.0f;
    for (std::size_t bin = first; bin < end; ++bin) {
        power += std::norm(spectrum[bin]);
    }
    return power / static_cast<float>(end - first);
}

/// The mean, over \a signal, of what range 4 (bins 4 to 6) holds in a frame.
[[nodiscard]] float rangeFourPower(std::span<const float> signal)
{
    double sum = 0.0;
    std::size_t frames = 0;
    for (std::size_t at = 0; at + 48 <= signal.size(); at += 22) {
        sum += bandPower(signal.subspan(at, 48), 4, 7);
        ++frames;
    }
    return static_cast<float>(sum / static_cast<double>(frames));
}

TEST(TransientCut, TheFrameFollowsTime)
{
    const auto check = [](double rate, std::uint32_t hop, std::uint32_t delay) {
        TransientCut cut;
        cut.prepare(rate);
        EXPECT_EQ(cut.hopSamples(), hop) << rate;
        EXPECT_EQ(cut.latencySamples(), delay) << rate;
        EXPECT_NEAR(delay / rate * 1000.0, 0.73, 0.01) << rate;
    };
    check(44100.0, 20, 32);
    check(48000.0, 22, 35);
    check(96000.0, 44, 70);
}

TEST(TransientCut, OffIsBitExact)
{
    TransientCut cut;
    cut.prepare(kRate);
    Signal input = whiteNoise(kSecond, 0.2f, 1);
    input[kSecond / 2] = 1.0f;
    const Signal output = run(cut, input);
    for (std::size_t n = 0; n < kDelay; ++n) {
        ASSERT_EQ(output[n], 0.0f) << n;
    }
    for (std::size_t n = kDelay; n < output.size(); ++n) {
        ASSERT_EQ(output[n], input[n - kDelay]) << n;
    }
}

TEST(TransientCut, UnflaggedFramesAreBitExact)
{
    TransientCut cut;
    cut.prepare(kRate);
    cut.setShare(kMedium);
    const Signal input = whiteNoise(kSecond * 3, 0.1f, 2);
    const Signal output = run(cut, input);
    for (std::size_t n = kDelay; n < output.size(); ++n) {
        ASSERT_EQ(output[n], input[n - kDelay]) << n;
    }
}

TEST(TransientCut, OnlyTheFramesWithACutChange)
{
    TransientCut cut;
    cut.prepare(kRate);
    cut.setShare(kMedium);
    Signal input = whiteNoise(kSecond, 0.01f, 3);
    const std::size_t at = kSecond - 2000;
    input[at] = 1.0f;
    const Signal output = run(cut, input);
    std::size_t changed = 0;
    for (std::size_t n = kDelay; n < output.size(); ++n) {
        if (output[n] != input[n - kDelay]) {
            ++changed;
            // A single sample shows in the frames holding it, 1 ms and no more.
            ASSERT_GT(n, at + kDelay - 49) << n;
            ASSERT_LT(n, at + kDelay + 49) << n;
        }
    }
    EXPECT_GT(changed, 0U) << "a click far above the noise was not touched";
}

// Each Strength against the rule: a tone that fills range 4 (bins 4 to 6)
// stands exactly 26 dB above that range's past, measured here apart from the
// code. The cut is the share of 26 dB, or 15 dB where that is more.
TEST(TransientCut, CutsAClickByTheRule)
{
    constexpr float kExcessDb = 26.0f;
    const auto expectCut = [](float share, float wantedDb) {
        constexpr std::size_t kNoise = kSecond;
        constexpr std::size_t kBurst = 600;
        const Signal noise = whiteNoise(kNoise, 0.02f, 4);
        const float noisePower = rangeFourPower(noise);
        const Signal unit = sine(kBurst, 3750.0, kRate);
        const float amplitude = std::sqrt(std::pow(10.0f, kExcessDb / 10.0f) * noisePower / rangeFourPower(unit));

        Signal input = noise;
        input.resize(kNoise + kBurst, 0.0f);
        for (std::size_t n = 0; n < kBurst; ++n) {
            input[kNoise + n] = amplitude * unit[n];
        }
        TransientCut cut;
        cut.prepare(kRate);
        cut.setShare(share);
        Signal output = input;
        output.resize(input.size() + 200, 0.0f);
        float applied = 0.0f;
        for (std::size_t at = 0; at < output.size(); ++at) {
            cut.process(std::span<float>{&output[at], 1});
            if (at >= kNoise + 200 && at < kNoise + kBurst - 48) {
                applied = std::max(applied, cut.cutDb(3));
            }
        }
        EXPECT_NEAR(applied, wantedDb, 0.5f) << "share " << share;

        // The middle of the burst, whole frames in, in whole cycles of the tone.
        const std::span<const float> before = std::span<const float>(input).subspan(kNoise + 200, 128);
        const std::span<const float> after = std::span<const float>(output).subspan(kNoise + 200 + kDelay, 128);
        // What is heard is near it. A tone's skirts reach past the range's
        // edges, and the two parts add at the frame's middle and not at its ends.
        EXPECT_NEAR(rmsDb(before) - rmsDb(after), wantedDb, 1.0f) << "share " << share;
    };
    expectCut(kGentle, kGentle * kExcessDb);
    expectCut(kMedium, kMedium * kExcessDb);
    expectCut(kStrong, TransientCut::kMostCutDb);
}

TEST(TransientCut, UnderItsThresholdNothingHappens)
{
    // 19 dB is under range 4's ratio of 12, which is 21.6 dB.
    const Signal noise = whiteNoise(kSecond, 0.02f, 5);
    const Signal unit = sine(600, 3750.0, kRate);
    const float amplitude = std::sqrt(std::pow(10.0f, 1.9f) * rangeFourPower(noise) / rangeFourPower(unit));
    Signal input = noise;
    for (std::size_t n = 0; n < unit.size(); ++n) {
        input[kSecond - 2000 + n] += amplitude * unit[n];
    }
    TransientCut cut;
    cut.prepare(kRate);
    cut.setShare(kStrong);
    const Signal output = run(cut, input);
    for (std::size_t n = kDelay; n < output.size(); ++n) {
        ASSERT_EQ(output[n], input[n - kDelay]) << n;
    }
}

TEST(TransientCut, TheFirstTenthOfASecondOfSoundIsNeverCut)
{
    Signal input = silence(kSecond);
    const Signal noise = whiteNoise(kSecond / 20, 0.01f, 6);
    std::ranges::copy(noise, input.begin() + kSecond / 2);
    input[kSecond / 2 + 3000] = 1.0f;
    TransientCut cut;
    cut.prepare(kRate);
    cut.setShare(kStrong);
    const Signal output = run(cut, input);
    for (std::size_t n = kDelay; n < output.size(); ++n) {
        ASSERT_EQ(output[n], input[n - kDelay]) << n;
    }
}

TEST(TransientCut, SilenceDoesNotArmIt)
{
    // Five seconds of exact zero, after sound and from the start.
    for (const bool soundFirst : {false, true}) {
        Signal input = soundFirst ? whiteNoise(kSecond, 0.02f, 7) : Signal{};
        input.resize(input.size() + kSecond * 5, 0.0f);
        const std::size_t start = input.size();
        const Signal tone = sine(kSecond / 2, 1000.0, kRate, 0.05f);
        input.insert(input.end(), tone.begin(), tone.end());
        input.resize(input.size() + 1000, 0.0f);

        TransientCut cut;
        cut.prepare(kRate);
        cut.setShare(kMedium);
        Signal output = input;
        float deepest = 0.0f;
        for (std::size_t at = 0; at < output.size(); ++at) {
            cut.process(std::span<float>{&output[at], 1});
            if (at >= start) {
                deepest = std::max(deepest, cut.deepestCutDb());
            }
        }
        EXPECT_LE(deepest, 1.0f) << soundFirst;
    }
}

TEST(TransientCut, AFaintFrameTriggersNothing)
{
    // A low tone leaves the top range's past almost empty, so a tick under the
    // quiet gate stands far above it, and still counts for nothing.
    Signal input = sine(kSecond / 2, 200.0, kRate, 0.002f);
    input.resize(kSecond, 0.0f);
    const Signal tick = sine(200, 7500.0, kRate, 0.0009f);
    std::ranges::copy(tick, input.begin() + kSecond * 3 / 4);
    TransientCut cut;
    cut.prepare(kRate);
    cut.setShare(kStrong);
    const Signal output = run(cut, input);
    for (std::size_t n = kDelay; n < output.size(); ++n) {
        ASSERT_EQ(output[n], input[n - kDelay]) << n;
    }
}

TEST(TransientCut, ASustainedStepIsLetGo)
{
    for (const std::uint32_t seed : {8U, 9U, 10U, 50U, 51U, 52U}) {
        constexpr std::size_t kStep = kSecond;
        constexpr float kLow = 0.008f;
        Signal input = whiteNoise(kSecond * 3, kLow, seed);
        for (std::size_t n = kStep; n < input.size(); ++n) {
            input[n] *= 100.0f;
        }
        TransientCut cut;
        cut.prepare(kRate);
        cut.setShare(kStrong);
        Signal output = input;
        float cutAfter = 0.0f;
        float deepest = 0.0f;
        for (std::size_t at = 0; at < output.size(); ++at) {
            cut.process(std::span<float>{&output[at], 1});
            deepest = std::max(deepest, cut.deepestCutDb());
            if (at >= kStep + kSecond * 3 / 10) {
                cutAfter = std::max(cutAfter, cut.deepestCutDb());
            }
        }
        EXPECT_GT(deepest, 10.0f) << "the step was never softened, so this tested nothing";
        EXPECT_LT(cutAfter, 0.5f) << seed;
        const std::size_t later = kStep + kSecond * 3 / 10;
        EXPECT_LT(
            std::abs(rmsDb(std::span<const float>(input).subspan(later))
                     - rmsDb(std::span<const float>(output).subspan(later + kDelay, input.size() - later - kDelay))),
            0.5f)
            << seed;

        // Output against input, in 5 ms steps: the end of the hold is a slope.
        constexpr std::size_t kFive = 240;
        float lastDb = 0.0f;
        for (std::size_t at = kStep + 2 * kFive; at + kDelay + kFive < input.size(); at += kFive) {
            const float heard = rmsDb(std::span<const float>(output).subspan(at + kDelay, kFive));
            const float came = rmsDb(std::span<const float>(input).subspan(at, kFive));
            const float difference = heard - came;
            EXPECT_LT(difference - lastDb, 4.0f) << seed << " at " << at;
            lastDb = difference;
        }
    }
}

// A step of +40 dB, with \a dipMs of the old level in every \a everyMs (0 for
// none). The hold is time since the rise, so every range has let go by 250 ms
// and stays let go.
void expectLetGoInTime(std::uint32_t seed, double dipMs, double everyMs)
{
    constexpr std::size_t kStep = kSecond;
    constexpr float kLow = 0.008f;
    const auto dip = static_cast<std::size_t>(dipMs * 48.0);
    const auto every = static_cast<std::size_t>(everyMs * 48.0);
    Signal input = whiteNoise(kSecond * 3, kLow, seed);
    for (std::size_t n = kStep; n < input.size(); ++n) {
        if (every == 0 || (n - kStep) % every >= dip) {
            input[n] *= 100.0f;
        }
    }
    TransientCut cut;
    cut.prepare(kRate);
    cut.setShare(kStrong);
    Signal output = input;
    float deepest = 0.0f;
    float worstLate = 0.0f;
    float worstAfter = 0.0f;
    for (std::size_t at = 0; at < output.size(); ++at) {
        cut.process(std::span<float>{&output[at], 1});
        if (at >= kStep && at < kStep + kSecond / 5) {
            deepest = std::max(deepest, cut.deepestCutDb());
        }
        // The sound starts on a dip, so the rise comes after it.
        if (at >= kStep + dip + kSecond * 255 / 1000) {
            worstLate = std::max(worstLate, cut.deepestCutDb());
        }
        if (at >= kStep + kSecond * 3 / 10) {
            worstAfter = std::max(worstAfter, cut.deepestCutDb());
        }
    }
    EXPECT_GT(deepest, 10.0f) << "the step was never softened, so this tested nothing";
    EXPECT_LT(worstAfter, 0.5f) << "seed " << seed << ", " << dipMs << " ms in " << everyMs;
    EXPECT_LT(worstLate, 0.5f) << "seed " << seed << ", " << dipMs << " ms in " << everyMs;
}

TEST(TransientCut, EveryRangeLetsGoOfASteadyStepInTime)
{
    for (std::uint32_t seed = 100; seed < 120; ++seed) {
        expectLetGoInTime(seed, 0.0, 0.0);
    }
}

TEST(TransientCut, ShortDipsDoNotRestartTheHold)
{
    for (std::uint32_t seed = 120; seed < 140; ++seed) {
        expectLetGoInTime(seed, 3.0, 6.0);
        expectLetGoInTime(seed, 7.0, 20.0);
        expectLetGoInTime(seed, 20.0, 60.0);
    }
}

TEST(TransientCut, TheLetGoStartsFromTheCutEvenOnADip)
{
    // One dip, from 190 to 215 ms, with the hold running out inside it: the
    // sound comes back on a slope, not on a step.
    constexpr std::size_t kStep = kSecond;
    for (std::uint32_t seed = 150; seed < 156; ++seed) {
        Signal input = whiteNoise(kStep + kSecond / 2, 0.008f, seed);
        for (std::size_t n = kStep; n < input.size(); ++n) {
            const std::size_t after = n - kStep;
            if (after < kSecond * 190 / 1000 || after >= kSecond * 215 / 1000) {
                input[n] *= 100.0f;
            }
        }
        TransientCut cut;
        cut.prepare(kRate);
        cut.setShare(kStrong);
        cut.process(std::span<float>{input}.first(kStep + kSecond * 212 / 1000));
        for (std::size_t range = 2; range < TransientCut::kRanges; ++range) {
            EXPECT_GT(cut.cutDb(range), 5.0f) << "seed " << seed << " range " << range;
        }
    }
}

TEST(TransientCut, AKnockAfterTheGapGetsAHoldOfItsOwn)
{
    // Further apart than kOverEndsMs, each is a rise of its own.
    Signal input = whiteNoise(kSecond * 2, 0.008f, 151);
    const Signal bang = whiteNoise(96, 0.8f, 152);
    for (const std::size_t at : {std::size_t{48000}, std::size_t{72000}}) {
        for (std::size_t n = 0; n < bang.size(); ++n) {
            input[at + n] += bang[n];
        }
    }
    TransientCut cut;
    cut.prepare(kRate);
    cut.setShare(kStrong);
    float second = 0.0f;
    Signal output = input;
    for (std::size_t at = 0; at < output.size(); ++at) {
        cut.process(std::span<float>{&output[at], 1});
        if (at >= 72000 && at < 72200) {
            second = std::max(second, cut.deepestCutDb());
        }
    }
    EXPECT_GT(second, 5.0f);
}

TEST(TransientCut, TakingTheShareAwayLetsGo)
{
    // In the middle of a cut, as the end of a hold does.
    constexpr float kLow = 0.008f;
    Signal input = whiteNoise(kSecond * 2, kLow, 11);
    for (std::size_t n = kSecond; n < input.size(); ++n) {
        input[n] *= 100.0f;
    }
    TransientCut cut;
    cut.prepare(kRate);
    cut.setShare(kStrong);
    std::vector<float> depths;
    Signal output = input;
    const std::size_t off = kSecond + kSecond / 10;
    for (std::size_t at = 0; at < output.size(); ++at) {
        if (at == off) {
            cut.setShare(0.0f);
        }
        cut.process(std::span<float>{&output[at], 1});
        if (at >= off && at % 22 == 0) {
            depths.push_back(cut.deepestCutDb());
        }
    }
    ASSERT_GT(depths.front(), 10.0f) << "nothing was being cut, so this tested nothing";
    float largestStep = 0.0f;
    for (std::size_t n = 1; n < depths.size(); ++n) {
        largestStep = std::max(largestStep, depths[n - 1] - depths[n]);
    }
    EXPECT_LT(largestStep, 0.2f);
    EXPECT_EQ(depths.back(), 0.0f);
    // 50 ms at most, then the input again, exact.
    for (std::size_t n = off + kSecond / 10; n < output.size(); ++n) {
        ASSERT_EQ(output[n], input[n - kDelay]) << n;
    }
}

TEST(TransientCut, NeverAboveTheInput)
{
    // Every range's share of a click stays at or under what came in.
    Signal input = whiteNoise(kSecond, 0.005f, 12);
    Signal click = whiteNoise(kSecond, 1.0f, 13);
    for (std::size_t n = 30000; n < 30300; ++n) {
        input[n] += 0.3f * click[n];
    }
    TransientCut cut;
    cut.prepare(kRate);
    cut.setShare(kStrong);
    Signal padded = input;
    padded.resize(input.size() + 100, 0.0f);
    const Signal output = run(cut, padded);

    const std::span<const float> in = std::span<const float>(padded).subspan(29900, 600);
    const std::span<const float> out = std::span<const float>(output).subspan(29900 + kDelay, 600);
    EXPECT_LT(rmsDb(out), rmsDb(in));
    for (const float frequency : {750.0f, 2250.0f, 3750.0f, 7500.0f, 15000.0f}) {
        EXPECT_LE(amplitudeAt(out, frequency, kRate), amplitudeAt(in, frequency, kRate) * 1.05f) << frequency;
    }
}

TEST(TransientCut, ACutLeavesNoBinBehindAboveTheTopRange)
{
    // 15 kHz detects nothing, and takes the top range's cut.
    Signal input = whiteNoise(kSecond, 0.01f, 14);
    const Signal hiss = sine(400, 15000.0, kRate, 0.1f);
    Signal bang = whiteNoise(400, 0.5f, 15);
    for (std::size_t n = 0; n < 400; ++n) {
        input[30000 + n] += bang[n] + hiss[n];
    }
    TransientCut cut;
    cut.prepare(kRate);
    cut.setShare(kStrong);
    const Signal output = run(cut, input);
    const std::span<const float> in = std::span<const float>(input).subspan(30100, 150);
    const std::span<const float> out = std::span<const float>(output).subspan(30100 + kDelay, 150);
    EXPECT_LT(amplitudeAt(out, 15000.0f, kRate), amplitudeAt(in, 15000.0f, kRate) * 0.5f);
}

TEST(TransientCut, TheSoundDoesNotDependOnTheQuantum)
{
    Signal input = whiteNoise(kSecond, 0.01f, 16);
    input[30000] = 1.0f;
    TransientCut one;
    one.prepare(kRate);
    one.setShare(kMedium);
    const Signal first = run(one, input, 1);
    for (const std::size_t quantum : {std::size_t{7}, std::size_t{64}, std::size_t{8192}}) {
        TransientCut other;
        other.prepare(kRate);
        other.setShare(kMedium);
        const Signal second = run(other, input, quantum);
        for (std::size_t n = 0; n < first.size(); ++n) {
            ASSERT_EQ(first[n], second[n]) << quantum << " at " << n;
        }
    }
}

TEST(TransientCut, ResetForgetsWhatItHeard)
{
    Signal input = whiteNoise(kSecond / 2, 0.01f, 17);
    input[kSecond / 4 + 3000] = 1.0f;
    TransientCut cut;
    cut.prepare(kRate);
    cut.setShare(kMedium);
    const Signal first = run(cut, input);
    cut.reset();
    const Signal second = run(cut, input);
    EXPECT_EQ(first, second);
}

} // namespace
