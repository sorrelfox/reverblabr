// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// BS.1770 loudness: the K-weighting against the recommendation's own
// coefficients, and the meter against its calibration.

#include "dsp/loudness.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace rb::dsp;
using namespace rb::test;

TEST(LoudnessTest, TheKWeightingAt48kHzIsTheRecommendationsOwn)
{
    // The coefficients ITU-R BS.1770 lists for 48 kHz.
    const auto stages = kWeighting(48000.0);
    EXPECT_NEAR(stages[0].b0, 1.53512485958697, 1e-9);
    EXPECT_NEAR(stages[0].b1, -2.69169618940638, 1e-9);
    EXPECT_NEAR(stages[0].b2, 1.19839281085285, 1e-9);
    EXPECT_NEAR(stages[0].a1, -1.69065929318241, 1e-9);
    EXPECT_NEAR(stages[0].a2, 0.73248077421585, 1e-9);
    EXPECT_DOUBLE_EQ(stages[1].b0, 1.0);
    EXPECT_DOUBLE_EQ(stages[1].b1, -2.0);
    EXPECT_DOUBLE_EQ(stages[1].b2, 1.0);
    EXPECT_NEAR(stages[1].a1, -1.99004745483398, 1e-9);
    EXPECT_NEAR(stages[1].a2, 0.99007225036621, 1e-9);
}

TEST(LoudnessTest, TheKWeightingIsTheSameCurveAtOtherRates)
{
    // Designed from the analogue prototype, so 44.1 and 96 kHz draw the
    // 48 kHz curve, not 48 kHz numbers run at the wrong speed.
    const auto reference = kWeighting(48000.0);
    for (const double rate : {44100.0, 96000.0}) {
        const auto stages = kWeighting(rate);
        for (const double hz : {30.0, 100.0, 1000.0, 3000.0, 10000.0}) {
            const double expected =
                biquadMagnitudeDb(reference[0], hz, 48000.0) + biquadMagnitudeDb(reference[1], hz, 48000.0);
            const double got = biquadMagnitudeDb(stages[0], hz, rate) + biquadMagnitudeDb(stages[1], hz, rate);
            EXPECT_NEAR(got, expected, 0.05) << hz << " Hz at " << rate;
        }
    }
}

namespace
{

/// The meter after \a left and \a right have gone through it.
[[nodiscard]] GatedLoudness measured(const Signal &left, const Signal &right, double rate = kTestSampleRate)
{
    GatedLoudness meter;
    meter.prepare(rate);
    for (std::size_t frame = 0; frame < left.size(); ++frame) {
        meter.push(left[frame], right[frame]);
    }
    return meter;
}

} // namespace

TEST(LoudnessTest, ASineAtFullScaleOnOneChannelReadsMinusThree)
{
    // The recommendation's calibration: 997 Hz at 0 dBFS on one channel is
    // −3.01 LUFS.
    const Signal tone = sine(4 * 48000, 997.0, kTestSampleRate, 1.0f);
    const GatedLoudness meter = measured(tone, silence(tone.size()));
    EXPECT_NEAR(meter.momentary(), -3.01f, 0.05f);
    EXPECT_NEAR(meter.loudness(), -3.01f, 0.05f);
}

TEST(LoudnessTest, ASineOnBothChannelsReadsItsLevel)
{
    // −20 dBFS on each side: the two sides' powers add, 3 dB above one side
    // alone, which the −3.01 of a sine's own mean square brings back to −20.
    const Signal tone = sine(4 * 48000, 997.0, kTestSampleRate, 0.1f);
    const GatedLoudness meter = measured(tone, tone);
    EXPECT_NEAR(meter.momentary(), -20.0f, 0.05f);
    EXPECT_NEAR(meter.loudness(), -20.0f, 0.05f);
}

TEST(LoudnessTest, TheMomentaryWindowIsFourHundredMilliseconds)
{
    // A tone that stops: 200 ms after it stops the block holds half of it,
    // 3 dB down; 400 ms after, nothing but the filters' ringing is left.
    Signal tone = sine(48000, 997.0, kTestSampleRate, 0.1f);
    tone.resize(48000 + 9600, 0.0f);
    GatedLoudness meter = measured(tone, tone);
    EXPECT_NEAR(meter.momentary(), -23.01f, 0.1f);
    for (std::size_t frame = 0; frame < 9600; ++frame) {
        meter.push(0.0f, 0.0f);
    }
    EXPECT_LT(meter.momentary(), -55.0f);
}

TEST(LoudnessTest, PausesAreGatedOutAndSoftPassagesAreNot)
{
    // Speech-like: a tone in half-second bursts with half-second silences.
    // Measured plainly that is 3 dB under the bursts; gated, the blocks of
    // nothing drop out and only the blocks straddling an edge pull it down,
    // as they would a programme's integrated loudness.
    Signal bursts(4 * 48000);
    const Signal tone = sine(bursts.size(), 997.0, kTestSampleRate, 0.1f);
    for (std::size_t frame = 0; frame < bursts.size(); ++frame) {
        bursts[frame] = (frame / 24000) % 2 == 0 ? tone[frame] : 0.0f;
    }
    const GatedLoudness gaps = measured(bursts, bursts);
    EXPECT_GT(gaps.loudness(), -22.0f) << "the pauses pulled the loudness down as far as a plain mean";
    EXPECT_LT(gaps.loudness(), -20.0f);

    // A passage 6 dB softer is inside the relative gate and counts: the
    // loudness falls, though by less than 6 dB, the louder half weighing
    // more.
    Signal softer = tone;
    for (std::size_t frame = 0; frame < softer.size(); ++frame) {
        softer[frame] *= (frame / 24000) % 2 == 0 ? 1.0f : 0.5f;
    }
    const float mixed = measured(softer, softer).loudness();
    EXPECT_LT(mixed, -21.0f);
    EXPECT_GT(mixed, -23.0f);
}

TEST(LoudnessTest, SilenceIsSilent)
{
    const Signal quiet = silence(4 * 48000);
    EXPECT_EQ(measured(quiet, quiet).loudness(), kSilentLufs);
}

TEST(LoudnessTest, ItReadsTheSameAtAnotherRate)
{
    const Signal tone = sine(4 * 44100, 997.0, 44100.0, 0.1f);
    EXPECT_NEAR(measured(tone, tone, 44100.0).loudness(), -20.0f, 0.05f);
}
