// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "dsp/smoothing.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

using namespace rb::dsp;

namespace
{

/// The sample rates the engine has to work at. A smoother that is right at one
/// of them and wrong at another has hard-coded something.
constexpr double kSampleRates[] = {44100.0, 48000.0, 96000.0};

/// The number of samples a glide of \a timeMs lasts at \a sampleRate.
std::uint32_t samplesIn(float timeMs, double sampleRate)
{
    return static_cast<std::uint32_t>(std::lround(static_cast<double>(timeMs) * 1.0e-3 * sampleRate));
}

/// Runs \a smoother from 0 to 1 and returns the number of samples it took to
/// cover 99 % of the distance.
template <typename Smoother>
std::uint32_t samplesTo99Percent(Smoother &smoother)
{
    smoother.reset(0.0f);
    smoother.setTarget(1.0f);
    for (std::uint32_t sample = 1; sample <= 1'000'000; ++sample) {
        if (smoother.next() >= 0.99f) {
            return sample;
        }
    }
    return 0;
}

} // namespace

TEST(LinearSmoother, ReachesNinetyNinePercentWithinTheConfiguredTime)
{
    for (const double sampleRate : kSampleRates) {
        LinearSmoother smoother;
        smoother.prepare(sampleRate, 20.0f);
        const auto expected = static_cast<double>(samplesIn(20.0f, sampleRate));
        const auto measured = static_cast<double>(samplesTo99Percent(smoother));
        EXPECT_NEAR(measured, expected, expected * 0.05) << "at " << sampleRate << " Hz";
    }
}

TEST(OnePoleSmoother, ReachesNinetyNinePercentWithinTheConfiguredTime)
{
    for (const double sampleRate : kSampleRates) {
        OnePoleSmoother smoother;
        smoother.prepare(sampleRate, 20.0f);
        const auto expected = static_cast<double>(samplesIn(20.0f, sampleRate));
        const auto measured = static_cast<double>(samplesTo99Percent(smoother));
        EXPECT_NEAR(measured, expected, expected * 0.05) << "at " << sampleRate << " Hz";
    }
}

TEST(LinearSmoother, AGainRampDoesNotStep)
{
    // A 20 ms ramp at 48 kHz is 960 samples, so an even ramp moves by exactly
    // 1/960 of full scale per sample. Nothing in the glide may move further:
    // a bigger step is the zipper noise the smoother exists to prevent.
    constexpr double kSampleRate = 48000.0;
    constexpr std::uint32_t kRampSamples = 960;
    constexpr float kLargestEvenStep = 1.0f / static_cast<float>(kRampSamples);
    // The ramp is computed in single precision, so a step can miss the even
    // one by a few units in the last place of the value itself. That is four
    // orders of magnitude below the step and is not a step of its own.
    constexpr float kRounding = 4.0f * std::numeric_limits<float>::epsilon();

    LinearSmoother smoother;
    smoother.prepare(kSampleRate, 20.0f);
    ASSERT_EQ(smoother.rampSamples(), kRampSamples);
    smoother.reset(0.0f);
    smoother.setTarget(1.0f);

    float previous = smoother.current();
    float largestStep = 0.0f;
    for (std::uint32_t sample = 0; sample < 2 * kRampSamples; ++sample) {
        const float value = smoother.next();
        largestStep = std::max(largestStep, std::abs(value - previous));
        previous = value;
    }
    EXPECT_LE(largestStep, kLargestEvenStep + kRounding);
    EXPECT_FLOAT_EQ(previous, 1.0f);
}

TEST(LinearSmoother, LandsExactlyOnTheTarget)
{
    LinearSmoother smoother;
    smoother.prepare(48000.0, 5.0f);
    smoother.reset(0.25f);
    smoother.setTarget(0.75f);
    for (std::uint32_t sample = 0; sample < smoother.rampSamples(); ++sample) {
        EXPECT_TRUE(smoother.isSmoothing());
        (void)smoother.next();
    }
    EXPECT_FALSE(smoother.isSmoothing());
    EXPECT_FLOAT_EQ(smoother.current(), 0.75f);
    EXPECT_FLOAT_EQ(smoother.next(), 0.75f);
}

TEST(LinearSmoother, ANewTargetRestartsFromWhereTheGlideIs)
{
    LinearSmoother smoother;
    smoother.prepare(48000.0, 10.0f);
    smoother.reset(0.0f);
    smoother.setTarget(1.0f);
    for (int sample = 0; sample < 100; ++sample) {
        (void)smoother.next();
    }
    const float midway = smoother.current();
    ASSERT_GT(midway, 0.0f);
    ASSERT_LT(midway, 1.0f);

    smoother.setTarget(0.0f);
    EXPECT_FLOAT_EQ(smoother.current(), midway) << "the new target must not make the value jump";
    const float afterOneSample = smoother.next();
    EXPECT_LT(afterOneSample, midway);
    EXPECT_GT(afterOneSample, 0.0f);
}

TEST(LinearSmoother, SettingTheSameTargetAgainDoesNotSlowTheGlideDown)
{
    // A block works out its target at the top of every cycle and sets it. If
    // that restarted the ramp, a glide would be stretched over a fresh full
    // time every few hundred samples and would never arrive.
    LinearSmoother restated;
    LinearSmoother once;
    restated.prepare(48000.0, 20.0f);
    once.prepare(48000.0, 20.0f);
    restated.reset(0.0f);
    once.reset(0.0f);
    restated.setTarget(1.0f);
    once.setTarget(1.0f);

    for (std::uint32_t sample = 0; sample < 2 * restated.rampSamples(); ++sample) {
        if (sample % 256 == 0) {
            restated.setTarget(1.0f); // as a block would, once a cycle
        }
        (void)restated.next();
        (void)once.next();
    }
    EXPECT_FLOAT_EQ(restated.current(), once.current());
    EXPECT_FLOAT_EQ(restated.current(), 1.0f);
    EXPECT_FALSE(restated.isSmoothing());
}

TEST(LinearSmoother, ResetJumpsWithNoGlide)
{
    LinearSmoother smoother;
    smoother.prepare(48000.0, 20.0f);
    smoother.reset(0.0f);
    smoother.setTarget(1.0f);
    (void)smoother.next();
    smoother.reset(-3.0f);
    EXPECT_FALSE(smoother.isSmoothing());
    EXPECT_FLOAT_EQ(smoother.current(), -3.0f);
    EXPECT_FLOAT_EQ(smoother.target(), -3.0f);
}

TEST(LinearSmoother, AZeroTimeArrivesAtOnce)
{
    LinearSmoother smoother;
    smoother.prepare(48000.0, 0.0f);
    smoother.reset(0.0f);
    smoother.setTarget(1.0f);
    EXPECT_FALSE(smoother.isSmoothing());
    EXPECT_FLOAT_EQ(smoother.current(), 1.0f);
}

TEST(LinearSmoother, AdvanceSkipsTheSameDistanceAsStepping)
{
    LinearSmoother stepped;
    LinearSmoother skipped;
    stepped.prepare(48000.0, 20.0f);
    skipped.prepare(48000.0, 20.0f);
    stepped.reset(0.0f);
    skipped.reset(0.0f);
    stepped.setTarget(1.0f);
    skipped.setTarget(1.0f);

    for (int sample = 0; sample < 400; ++sample) {
        (void)stepped.next();
    }
    skipped.advance(400);
    EXPECT_NEAR(skipped.current(), stepped.current(), 1.0e-5f);

    skipped.advance(10'000);
    EXPECT_FALSE(skipped.isSmoothing());
    EXPECT_FLOAT_EQ(skipped.current(), 1.0f);
}

TEST(OnePoleSmoother, ApproachesButNeverOvershoots)
{
    OnePoleSmoother smoother;
    smoother.prepare(48000.0, 20.0f);
    smoother.reset(0.0f);
    smoother.setTarget(1.0f);
    float previous = 0.0f;
    for (int sample = 0; sample < 5000; ++sample) {
        const float value = smoother.next();
        EXPECT_GE(value, previous);
        EXPECT_LE(value, 1.0f);
        previous = value;
    }
    EXPECT_FLOAT_EQ(previous, 1.0f);
}

TEST(OnePoleSmoother, SettlesSoThatSmoothingEnds)
{
    // Without a settle threshold the residual shrinks for ever, keeps
    // isSmoothing() true and ends up in the denormal range.
    OnePoleSmoother smoother;
    smoother.prepare(48000.0, 1.0f);
    smoother.reset(0.0f);
    smoother.setTarget(1.0f);
    for (int sample = 0; sample < 48000; ++sample) {
        (void)smoother.next();
    }
    EXPECT_FALSE(smoother.isSmoothing());
    EXPECT_EQ(smoother.current(), 1.0f);
}

TEST(OnePoleSmoother, AdvanceSkipsTheSameDistanceAsStepping)
{
    OnePoleSmoother stepped;
    OnePoleSmoother skipped;
    stepped.prepare(48000.0, 20.0f);
    skipped.prepare(48000.0, 20.0f);
    stepped.reset(0.0f);
    skipped.reset(0.0f);
    stepped.setTarget(1.0f);
    skipped.setTarget(1.0f);

    for (int sample = 0; sample < 400; ++sample) {
        (void)stepped.next();
    }
    skipped.advance(400);
    EXPECT_NEAR(skipped.current(), stepped.current(), 1.0e-4f);
}

TEST(OnePoleSmoother, CoefficientLeavesOnePercentAfterTheConfiguredTime)
{
    for (const double sampleRate : kSampleRates) {
        const float coefficient = onePoleSmoothingCoefficient(20.0f, sampleRate);
        const auto samples = static_cast<int>(samplesIn(20.0f, sampleRate));
        double remaining = 1.0;
        for (int sample = 0; sample < samples; ++sample) {
            remaining *= coefficient;
        }
        EXPECT_NEAR(remaining, 0.01, 0.0005) << "at " << sampleRate << " Hz";
    }
    EXPECT_EQ(onePoleSmoothingCoefficient(0.0f, 48000.0), 0.0f);
    EXPECT_EQ(onePoleSmoothingCoefficient(-1.0f, 48000.0), 0.0f);
}

TEST(Smoothing, PreparingForAnotherSampleRateKeepsTheTimeInSeconds)
{
    // The same glide, timed in milliseconds, must take twice as many samples
    // at twice the sample rate.
    LinearSmoother linear;
    linear.prepare(48000.0, 20.0f);
    const std::uint32_t at48k = linear.rampSamples();
    linear.prepare(96000.0, 20.0f);
    EXPECT_EQ(linear.rampSamples(), 2 * at48k);

    OnePoleSmoother onePole;
    onePole.prepare(48000.0, 20.0f);
    const float coefficientAt48k = onePole.coefficient();
    onePole.prepare(96000.0, 20.0f);
    EXPECT_GT(onePole.coefficient(), coefficientAt48k) << "a faster rate needs a slower decay per sample";
    EXPECT_NEAR(onePole.coefficient() * onePole.coefficient(), coefficientAt48k, 1.0e-6f);
}

TEST(Smoothing, AGlideInProgressSurvivesANewSampleRate)
{
    LinearSmoother smoother;
    smoother.prepare(48000.0, 20.0f);
    smoother.reset(0.0f);
    smoother.setTarget(1.0f);
    for (int sample = 0; sample < 100; ++sample) {
        (void)smoother.next();
    }
    const float midway = smoother.current();

    smoother.prepare(96000.0, 20.0f);
    EXPECT_FLOAT_EQ(smoother.current(), midway) << "changing the sample rate must not make the value jump";
    EXPECT_TRUE(smoother.isSmoothing());
    smoother.advance(smoother.rampSamples());
    EXPECT_FLOAT_EQ(smoother.current(), 1.0f);
}
