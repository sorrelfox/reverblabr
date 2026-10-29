// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "dsp/envelope.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

using namespace rb::dsp;

namespace
{

constexpr double kSampleRates[] = {44100.0, 48000.0, 96000.0};

/// What is left of a step after one time constant: 1/e.
const double kAfterOneTimeConstant = 1.0 / std::numbers::e;

std::uint32_t samplesIn(float timeMs, double sampleRate)
{
    return static_cast<std::uint32_t>(std::lround(static_cast<double>(timeMs) * 1.0e-3 * sampleRate));
}

/// A sine of \a amplitude and \a frequencyHz, \a frames long.
std::vector<float> sine(float amplitude, double frequencyHz, double sampleRate, std::uint32_t frames)
{
    std::vector<float> samples(frames);
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        const double phase = 2.0 * std::numbers::pi * frequencyHz * static_cast<double>(frame) / sampleRate;
        samples[frame] = amplitude * static_cast<float>(std::sin(phase));
    }
    return samples;
}

} // namespace

TEST(TimeConstant, MatchesTheAnalyticDecay)
{
    // After one time constant an exponential decay is down to 1/e of where it
    // started. The coefficient is checked by running it, not by repeating the
    // formula it was computed with.
    for (const double sampleRate : kSampleRates) {
        for (const float timeMs : {1.0f, 10.0f, 250.0f}) {
            const float coefficient = exponentialDecayCoefficient(timeMs, sampleRate);
            const auto samples = static_cast<int>(samplesIn(timeMs, sampleRate));
            double remaining = 1.0;
            for (int sample = 0; sample < samples; ++sample) {
                remaining *= coefficient;
            }
            EXPECT_NEAR(remaining, kAfterOneTimeConstant, kAfterOneTimeConstant * 0.01)
                << "at " << sampleRate << " Hz over " << timeMs << " ms";
        }
    }
}

TEST(TimeConstant, ZeroOrLessDecaysAtOnce)
{
    EXPECT_EQ(exponentialDecayCoefficient(0.0f, 48000.0), 0.0f);
    EXPECT_EQ(exponentialDecayCoefficient(-5.0f, 48000.0), 0.0f);
}

TEST(TimeConstant, RecomputesForANewSampleRate)
{
    TimeConstant constant(50.0f, 48000.0);
    const float at48k = constant.coefficient();
    constant.prepare(96000.0);
    EXPECT_FLOAT_EQ(constant.timeMs(), 50.0f) << "the time in milliseconds is what stays put";
    EXPECT_GT(constant.coefficient(), at48k) << "a faster rate needs a slower decay per sample";
    // Half the time per sample means the square of the coefficient.
    EXPECT_NEAR(constant.coefficient() * constant.coefficient(), at48k, 1.0e-6f);
}

TEST(TimeConstant, SettingTheTimeKeepsTheSampleRate)
{
    TimeConstant constant(10.0f, 96000.0);
    constant.setTimeMs(20.0f);
    EXPECT_FLOAT_EQ(constant.timeMs(), 20.0f);
    EXPECT_FLOAT_EQ(constant.coefficient(), exponentialDecayCoefficient(20.0f, 96000.0));
}

TEST(PeakFollower, JumpsToAPeakAtOnce)
{
    PeakFollower follower;
    follower.prepare(48000.0, 100.0f);
    EXPECT_EQ(follower.envelope(), 0.0f);

    follower.push(0.5f);
    EXPECT_FLOAT_EQ(follower.envelope(), 0.5f) << "a peak has to be caught on the sample it happens";

    follower.push(-0.8f);
    EXPECT_FLOAT_EQ(follower.envelope(), 0.8f) << "the sign of the sample does not matter";
}

TEST(PeakFollower, ReleasesWithTheConfiguredTimeConstant)
{
    for (const double sampleRate : kSampleRates) {
        constexpr float kReleaseMs = 100.0f;
        PeakFollower follower;
        follower.prepare(sampleRate, kReleaseMs);
        follower.push(1.0f);
        ASSERT_FLOAT_EQ(follower.envelope(), 1.0f);

        const auto samples = static_cast<int>(samplesIn(kReleaseMs, sampleRate));
        for (int sample = 0; sample < samples; ++sample) {
            follower.push(0.0f);
        }
        EXPECT_NEAR(follower.envelope(), kAfterOneTimeConstant, kAfterOneTimeConstant * 0.01)
            << "at " << sampleRate << " Hz";
    }
}

TEST(PeakFollower, FollowsTheCrestOfASine)
{
    // A sine well above the release time holds the envelope at its amplitude,
    // because each crest arrives before the previous one has fallen far.
    constexpr double kSampleRate = 48000.0;
    PeakFollower follower;
    follower.prepare(kSampleRate, 200.0f);
    const std::vector<float> samples = sine(0.25f, 1000.0, kSampleRate, 4800);
    const float envelope = follower.process(samples);
    EXPECT_NEAR(envelope, 0.25f, 0.25f * 0.02f);
}

TEST(PeakFollower, ResetClearsTheEnvelope)
{
    PeakFollower follower;
    follower.prepare(48000.0, 100.0f);
    follower.push(1.0f);
    follower.reset();
    EXPECT_EQ(follower.envelope(), 0.0f);
}

TEST(PeakFollower, ChangingTheReleaseKeepsTheEnvelope)
{
    PeakFollower follower;
    follower.prepare(48000.0, 100.0f);
    follower.push(1.0f);
    follower.setReleaseMs(500.0f);
    EXPECT_FLOAT_EQ(follower.envelope(), 1.0f);
    EXPECT_FLOAT_EQ(follower.releaseMs(), 500.0f);
}

TEST(RmsFollower, MeasuresTheLevelOfASine)
{
    // The root mean square of a sine is its amplitude divided by the square
    // root of two, about 3 dB below the peak.
    constexpr double kSampleRate = 48000.0;
    constexpr float kAmplitude = 0.5f;
    RmsFollower follower;
    follower.prepare(kSampleRate, 100.0f);
    const std::vector<float> samples = sine(kAmplitude, 1000.0, kSampleRate, 48000);
    const float level = follower.process(samples);
    EXPECT_NEAR(level, kAmplitude / std::numbers::sqrt2_v<float>, 0.005f);
}

TEST(RmsFollower, MeasuresTheLevelOfASquareWave)
{
    // A signal that is always at full amplitude has a root mean square equal
    // to that amplitude.
    RmsFollower follower;
    follower.prepare(48000.0, 50.0f);
    std::vector<float> samples(48000);
    for (std::size_t index = 0; index < samples.size(); ++index) {
        samples[index] = (index % 2 == 0) ? 0.7f : -0.7f;
    }
    EXPECT_NEAR(follower.process(samples), 0.7f, 1.0e-4f);
}

TEST(RmsFollower, ForgetsWhatFallsOutOfTheWindow)
{
    constexpr double kSampleRate = 48000.0;
    constexpr float kWindowMs = 20.0f;
    RmsFollower follower;
    follower.prepare(kSampleRate, kWindowMs);
    ASSERT_EQ(follower.windowSamples(), samplesIn(kWindowMs, kSampleRate));

    const std::vector<float> loud(follower.windowSamples(), 1.0f);
    EXPECT_NEAR(follower.process(loud), 1.0f, 1.0e-5f);

    // Exactly one window of silence must empty it again.
    const std::vector<float> silence(follower.windowSamples(), 0.0f);
    EXPECT_NEAR(follower.process(silence), 0.0f, 1.0e-6f);
}

TEST(RmsFollower, RisesOverTheWindowRatherThanAtOnce)
{
    RmsFollower follower;
    follower.prepare(48000.0, 20.0f);
    const std::uint32_t window = follower.windowSamples();

    // A quarter of the window at full amplitude: the mean square is a quarter,
    // so the level is a half.
    for (std::uint32_t sample = 0; sample < window / 4; ++sample) {
        follower.push(1.0f);
    }
    EXPECT_NEAR(follower.level(), 0.5f, 0.01f);
}

TEST(RmsFollower, ResetEmptiesTheWindow)
{
    RmsFollower follower;
    follower.prepare(48000.0, 10.0f);
    const std::vector<float> loud(follower.windowSamples(), 1.0f);
    EXPECT_GT(follower.process(loud), 0.9f);
    follower.reset();
    EXPECT_EQ(follower.level(), 0.0f);
}

TEST(RmsFollower, TheWindowLengthFollowsTheSampleRate)
{
    RmsFollower follower;
    for (const double sampleRate : kSampleRates) {
        follower.prepare(sampleRate, 30.0f);
        EXPECT_EQ(follower.windowSamples(), samplesIn(30.0f, sampleRate)) << "at " << sampleRate << " Hz";
        EXPECT_NEAR(follower.windowMs(), 30.0f, 0.01f);
    }
}

TEST(RmsFollower, AVeryShortWindowStillHasOneSample)
{
    RmsFollower follower;
    follower.prepare(48000.0, 0.0f);
    EXPECT_EQ(follower.windowSamples(), 1u);
    follower.push(0.3f);
    EXPECT_NEAR(follower.level(), 0.3f, 1.0e-6f);
}

TEST(RmsFollower, TheRunningSumStaysAccurateOverALongRun)
{
    // The window sum is kept, not recomputed, so a long run is where rounding
    // would show. Five minutes of audio at 48 kHz goes through here.
    constexpr double kSampleRate = 48000.0;
    RmsFollower follower;
    follower.prepare(kSampleRate, 400.0f);
    const std::vector<float> samples = sine(0.5f, 997.0, kSampleRate, 48000);
    float level = 0.0f;
    for (int second = 0; second < 300; ++second) {
        level = follower.process(samples);
    }
    EXPECT_NEAR(level, 0.5f / std::numbers::sqrt2_v<float>, 0.005f);
}
