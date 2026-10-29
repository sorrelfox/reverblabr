// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The true-peak estimate: what a signal reaches between its samples,
// four times oversampled.

#include "dsp/truepeak.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

using namespace rb::dsp;
using namespace rb::test;

namespace
{

/// The largest estimate over a signal on one channel, once the start has
/// passed: a tone that begins at full height out of silence is a step, and
/// a step really does ring above itself between its samples.
[[nodiscard]] float estimatedPeak(const Signal &signal)
{
    TruePeakEstimator estimator;
    estimator.prepare(1);
    float loudest = 0.0f;
    for (std::size_t frame = 0; frame < signal.size(); ++frame) {
        estimator.write(0, signal[frame]);
        if (frame >= 4 * TruePeakEstimator::kTaps) {
            loudest = std::max(loudest, estimator.peak(0));
        }
        estimator.advance();
    }
    return loudest;
}

} // namespace

TEST(TruePeakTest, AToneWhoseSamplesMissItsPeakReadsItsPeak)
{
    // A quarter of the rate, a quarter of a turn out: every sample sits at
    // 0.707 of the amplitude, 3 dB under a peak no sample touches.
    const Signal tone = sine(4800, 12000.0, kTestSampleRate, 1.0f, std::numbers::pi / 4.0);
    float samplePeak = 0.0f;
    for (const float sample : tone) {
        samplePeak = std::max(samplePeak, std::fabs(sample));
    }
    ASSERT_NEAR(samplePeak, 0.7071f, 1e-3f);
    EXPECT_NEAR(estimatedPeak(tone), 1.0f, 0.01f);
}

TEST(TruePeakTest, ItReadsLittleHighAndLessThanHalfADecibelLow)
{
    // Across the band, at phases that put the peak anywhere between the
    // points. Low is the direction that matters to a limiter, and four
    // points a sample miss a 20 kHz peak by up to half a decibel; high is
    // only a limiter holding back a little more than it had to.
    for (const double hz : {100.0, 1000.0, 5000.0, 10000.0, 15000.0, 20000.0}) {
        for (const double phase : {0.0, 0.3, 0.7, 1.1}) {
            const float peak = estimatedPeak(sine(9600, hz, kTestSampleRate, 0.5f, phase));
            EXPECT_LE(20.0f * std::log10(peak / 0.5f), 0.15f) << hz << " Hz at phase " << phase;
            EXPECT_GE(20.0f * std::log10(peak / 0.5f), -0.5f) << hz << " Hz at phase " << phase;
        }
    }
}

TEST(TruePeakTest, TheSampleItIsAboutIsKDelayBack)
{
    TruePeakEstimator estimator;
    estimator.prepare(2);
    for (int sample = 0; sample < 40; ++sample) {
        estimator.write(0, static_cast<float>(sample));
        estimator.write(1, -static_cast<float>(sample));
        if (sample >= static_cast<int>(TruePeakEstimator::kDelay)) {
            EXPECT_EQ(estimator.delayedSample(0),
                      static_cast<float>(sample - static_cast<int>(TruePeakEstimator::kDelay)));
            EXPECT_EQ(estimator.delayedSample(1),
                      -static_cast<float>(sample - static_cast<int>(TruePeakEstimator::kDelay)));
        }
        estimator.advance();
    }
}

TEST(TruePeakTest, AConstantReadsAsItself)
{
    EXPECT_NEAR(estimatedPeak(Signal(200, 0.25f)), 0.25f, 1e-4f);
}
