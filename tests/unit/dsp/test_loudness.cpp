// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// BS.1770 loudness: the K-weighting against the recommendation's own
// coefficients, and the meter against its calibration.

#include "allocationtrap.h"
#include "dsp/loudness.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <vector>

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

TEST(LoudnessFastTest, ItReadsWhatTheLongerReadingsReadOnASteadySound)
{
    const Signal tone = sine(2 * 48000, 997.0, kTestSampleRate, 0.1f);
    const GatedLoudness meter = measured(tone, tone);
    EXPECT_NEAR(meter.fast(), -20.0f, 0.05f);
    EXPECT_NEAR(meter.fast(), meter.momentary(), 0.05f);
}

TEST(LoudnessFastTest, ItWaitsForItsHundredMilliseconds)
{
    GatedLoudness meter;
    meter.prepare(kTestSampleRate);
    const Signal tone = sine(4800, 997.0, kTestSampleRate, 0.1f);
    for (std::size_t frame = 0; frame + 1 < tone.size(); ++frame) {
        meter.push(tone[frame], tone[frame]);
        ASSERT_EQ(meter.fast(), kSilentLufs) << "at frame " << frame;
    }
    meter.push(tone.back(), tone.back());
    EXPECT_GT(meter.fast(), -25.0f);
}

TEST(LoudnessFastTest, ItIsNewAtEverySliceAndSaysSo)
{
    GatedLoudness meter;
    meter.prepare(kTestSampleRate);
    const Signal tone = sine(4800, 997.0, kTestSampleRate, 0.1f);
    std::size_t slices = 0;
    for (std::size_t frame = 0; frame < tone.size(); ++frame) {
        meter.push(tone[frame], tone[frame]);
        if (meter.sliceEnded()) {
            ++slices;
            EXPECT_EQ((frame + 1) % 480, 0u);
        }
    }
    EXPECT_EQ(slices, 10u);
}

TEST(LoudnessFastTest, AJumpShowsWithinAHundredMillisecondsAndGoesAsSoon)
{
    // A tone 30 dB up: half of the hundred milliseconds is 3 dB under it at
    // 50 ms, while the 400 ms reading has not moved.
    Signal quiet = sine(48000, 997.0, kTestSampleRate, 0.00316f);
    GatedLoudness meter = measured(quiet, quiet);
    const float before = meter.momentary();
    const Signal loud = sine(4800, 997.0, kTestSampleRate, 0.1f);
    for (std::size_t frame = 0; frame < 2400; ++frame) {
        meter.push(loud[frame], loud[frame]);
    }
    EXPECT_NEAR(meter.fast(), -23.0f, 0.3f);
    for (std::size_t frame = 2400; frame < 4800; ++frame) {
        meter.push(loud[frame], loud[frame]);
    }
    EXPECT_NEAR(meter.fast(), -20.0f, 0.3f);
    EXPECT_LT(meter.momentary(), -25.0f) << "the slower reading cannot have got there; it was " << before;
    for (std::size_t frame = 0; frame < 4800; ++frame) {
        meter.push(0.0f, 0.0f);
    }
    EXPECT_LT(meter.fast(), -40.0f);
}

namespace
{

/// A voice's stand-in: steady noise, the same on both sides.
[[nodiscard]] Signal steadyNoise(std::size_t frames, float level, std::uint32_t seed = 21)
{
    return pinkNoise(frames, level, seed);
}

[[nodiscard]] double plainMeanSquare(const Signal &signal, std::size_t from, std::size_t to)
{
    const auto stages = kWeighting(kTestSampleRate);
    Biquad shelf;
    Biquad highPass;
    shelf.setCoefficients(stages[0]);
    highPass.setCoefficients(stages[1]);
    double sum = 0.0;
    for (std::size_t frame = 0; frame < to; ++frame) {
        const double y = highPass.process(shelf.process(signal[frame]));
        if (frame >= from) {
            sum += 2.0 * y * y;
        }
    }
    return sum / static_cast<double>(to - from);
}

void feed(VoicedLoudness &meter, const Signal &signal, bool voiced)
{
    for (const float sample : signal) {
        meter.push(sample, sample, voiced);
    }
}

} // namespace

TEST(VoicedLoudnessTest, AllVoicedItIsAPlainMean)
{
    const Signal noise = steadyNoise(5 * 48000, 0.2f);
    // The 27 blocks up to the end span the last three seconds; the hops land
    // on the end exactly.
    const float plain = lufsOfMeanSquare(plainMeanSquare(noise, 2 * 48000, 5 * 48000));
    VoicedLoudness meter;
    meter.prepare(kTestSampleRate);
    feed(meter, noise, true);
    EXPECT_NEAR(meter.reading(), plain, 0.1f);
}

TEST(VoicedLoudnessTest, TheFirstNumberTakesASecondOfVoice)
{
    const Signal noise = steadyNoise(48000, 0.2f);
    VoicedLoudness meter;
    meter.prepare(kTestSampleRate);
    feed(meter, Signal(noise.begin(), noise.end() - 480), true);
    EXPECT_EQ(meter.reading(), kSilentLufs);
    feed(meter, Signal(noise.end() - 480, noise.end()), true);
    EXPECT_GT(meter.reading(), kSilentLufs);
    EXPECT_EQ(meter.blocks(), VoicedLoudness::kFirstReadingBlocks);
}

TEST(VoicedLoudnessTest, WithoutAVoiceNothingCounts)
{
    VoicedLoudness meter;
    meter.prepare(kTestSampleRate);
    feed(meter, steadyNoise(5 * 48000, 0.2f), false);
    EXPECT_EQ(meter.reading(), kSilentLufs);
    EXPECT_EQ(meter.blocks(), 0u);
}

TEST(VoicedLoudnessTest, ABlockNeedsSixTenthsOfVoice)
{
    // The voiced share of a block sits exactly at the line, then just under.
    const auto blocksAt = [](std::size_t voicedSlices) {
        VoicedLoudness meter;
        meter.prepare(kTestSampleRate);
        const Signal noise = steadyNoise(40 * 480, 0.2f);
        for (std::size_t frame = 0; frame < noise.size(); ++frame) {
            meter.push(noise[frame], noise[frame], frame / 480 < voicedSlices);
        }
        return meter.blocks();
    };
    EXPECT_EQ(blocksAt(24), 1u);
    EXPECT_EQ(blocksAt(23), 0u);
}

TEST(VoicedLoudnessTest, TheReadingDoesNotMoveInAPause)
{
    VoicedLoudness meter;
    meter.prepare(kTestSampleRate);
    feed(meter, steadyNoise(4 * 48000, 0.2f), true);
    // A block still six tenths voice counts, so the first hop or two of a
    // pause go in. From then on nothing does.
    const Signal room = steadyNoise(30 * 48000, 0.001f, 22);
    feed(meter, Signal(room.begin(), room.begin() + 9600), false);
    const float held = meter.reading();
    const std::size_t blocks = meter.blocks();
    ASSERT_GT(held, -40.0f);

    // Thirty seconds of a much quieter room, not voiced.
    feed(meter, Signal(room.begin() + 9600, room.end()), false);
    EXPECT_EQ(meter.reading(), held);
    EXPECT_EQ(meter.blocks(), blocks);

    // The voice comes back softer, and the same ring goes on from there.
    feed(meter, steadyNoise(4 * 48000, 0.02f, 23), true);
    EXPECT_LT(meter.reading(), held - 10.0f);
}

TEST(VoicedLoudnessTest, ALongSilenceForgetsItAll)
{
    VoicedLoudness meter;
    meter.prepare(kTestSampleRate);
    meter.setForgetSeconds(10.0f);
    feed(meter, steadyNoise(4 * 48000, 0.2f), true);
    ASSERT_GT(meter.reading(), kSilentLufs);

    feed(meter, silence(9 * 48000 + 47000), false);
    EXPECT_GT(meter.reading(), kSilentLufs) << "not yet";
    feed(meter, silence(1000), false);
    EXPECT_EQ(meter.reading(), kSilentLufs);
    EXPECT_EQ(meter.blocks(), 0u);
}

TEST(VoicedLoudnessTest, TheWholeLevelWaitsForTwentySecondsOfVoice)
{
    VoicedLoudness meter;
    meter.prepare(kTestSampleRate);
    feed(meter, steadyNoise(20 * 48000 - 4800, 0.2f), true);
    EXPECT_EQ(meter.wholeLevel(), kSilentLufs);
    EXPECT_EQ(meter.blocks(), VoicedLoudness::kBlocks - 1);
    feed(meter, steadyNoise(4800, 0.2f, 24), true);
    EXPECT_EQ(meter.blocks(), VoicedLoudness::kBlocks);
    EXPECT_NEAR(meter.wholeLevel(), meter.reading(), 0.2f);
}

TEST(VoicedLoudnessTest, ALongerReadingAnswersAtOnceAndMovesMoreSlowly)
{
    VoicedLoudness meter;
    meter.prepare(kTestSampleRate);
    feed(meter, steadyNoise(10 * 48000, 0.02f), true);
    feed(meter, steadyNoise(2 * 48000, 0.2f, 25), true);
    const float threeSeconds = meter.reading();
    meter.setReadingSeconds(1.0f);
    const float oneSecond = meter.reading();
    meter.setReadingSeconds(10.0f);
    const float tenSeconds = meter.reading();
    EXPECT_GT(oneSecond, threeSeconds);
    EXPECT_GT(threeSeconds, tenSeconds);
}

TEST(VoicedLoudnessTest, NothingIsAllocated)
{
    const Signal noise = steadyNoise(3 * 48000, 0.2f);
    VoicedLoudness meter;
    meter.prepare(kTestSampleRate);
    GatedLoudness gated;
    gated.prepare(kTestSampleRate);
    {
        const NoAllocations realTime;
        for (const float sample : noise) {
            meter.push(sample, sample, true);
            gated.push(sample, sample);
        }
        meter.setReadingSeconds(5.0f);
        meter.reset();
    }
}

TEST(PivotGainTest, AtSlopeOneItIsAutoLevelsClampBitForBit)
{
    const PivotLaw zeroLaw{};
    EXPECT_EQ(pivotGainDb(-20.0f, -20.0f, zeroLaw), 0.0f);
    for (float maximum : {0.0f, 6.0f, 18.0f, 24.0f}) {
        const PivotLaw upwardOnly{.cutSlope = 0.0f, .mostLiftDb = maximum};
        for (float target = -30.0f; target <= -14.0f; target += 0.37f) {
            for (float reading = -120.0f; reading <= 3.0f; reading += 0.0713f) {
                const float expected = std::clamp(target - reading, 0.0f, maximum);
                ASSERT_EQ(std::bit_cast<std::uint32_t>(pivotGainDb(reading, target, upwardOnly)),
                          std::bit_cast<std::uint32_t>(expected))
                    << reading << " against " << target << " at most " << maximum;
            }
        }
    }
}

TEST(PivotGainTest, BothWaysItStopsAtItsLimits)
{
    const PivotLaw law{.mostCutDb = 6.0f, .mostLiftDb = 12.0f};
    EXPECT_FLOAT_EQ(pivotGainDb(-23.0f, -20.0f, law), 3.0f);
    EXPECT_FLOAT_EQ(pivotGainDb(-17.0f, -20.0f, law), -3.0f);
    EXPECT_FLOAT_EQ(pivotGainDb(-50.0f, -20.0f, law), 12.0f);
    EXPECT_FLOAT_EQ(pivotGainDb(0.0f, -20.0f, law), -6.0f);
}

TEST(PivotGainTest, ABandHasNoStepAtItsEdge)
{
    const PivotLaw law{.bandAboveLu = 1.0f, .bandBelowLu = 1.0f};
    EXPECT_EQ(pivotGainDb(-19.5f, -20.0f, law), 0.0f);
    EXPECT_EQ(pivotGainDb(-20.5f, -20.0f, law), 0.0f);
    EXPECT_NEAR(pivotGainDb(-18.99f, -20.0f, law), -0.01f, 1.0e-3f);
    EXPECT_NEAR(pivotGainDb(-21.01f, -20.0f, law), 0.01f, 1.0e-3f);
}

namespace
{

/// The night law at \a cut and \a lift, each a share of its full slope.
[[nodiscard]] PivotLaw nightLaw(float cut, float lift)
{
    return {.cutSlope = 0.6f * cut,
            .liftSlope = 0.4f * lift,
            .bandAboveLu = 3.0f,
            .bandBelowLu = 5.0f,
            .floorLu = -30.0f,
            .mostCutDb = 24.0f,
            .mostLiftDb = 24.0f};
}

} // namespace

TEST(PivotGainTest, TheNightTableComesOut)
{
    constexpr float pivot = -27.0f;
    struct Row {
        float cut;
        float lift;
        /// Out, in LU from the pivot, for each of the readings below.
        std::array<float, 6> out;
    };
    constexpr std::array<float, 6> in{30.0f, 20.0f, 10.0f, -10.0f, -20.0f, -30.0f};
    const std::array<Row, 3> rows{Row{0.5f, 0.0f, {21.9f, 14.9f, 7.9f, -10.0f, -20.0f, -30.0f}},
                                  Row{1.0f, 0.5f, {13.8f, 9.8f, 5.8f, -9.0f, -17.0f, -25.0f}},
                                  Row{1.0f, 1.0f, {13.8f, 9.8f, 5.8f, -8.0f, -14.0f, -20.0f}}};
    for (const Row &row : rows) {
        const PivotLaw law = nightLaw(row.cut, row.lift);
        for (std::size_t n = 0; n < in.size(); ++n) {
            EXPECT_NEAR(in[n] + pivotGainDb(pivot + in[n], pivot, law), row.out[n], 1.0e-3f)
                << in[n] << " LU at cut " << row.cut << " and lift " << row.lift;
        }
        // Speech is left alone, across the whole band, edges included.
        for (const float d : {-5.0f, -2.0f, 0.0f, 3.0f}) {
            EXPECT_EQ(pivotGainDb(pivot + d, pivot, law), 0.0f);
        }
        // Under the floor nothing is lifted.
        EXPECT_EQ(pivotGainDb(pivot - 30.5f, pivot, law), 0.0f);
    }
}

TEST(PivotGainTest, TheMostItEverLiftsIsAtTheFloor)
{
    const PivotLaw strong = nightLaw(1.0f, 1.0f);
    EXPECT_NEAR(pivotGainDb(-57.0f, -27.0f, strong), 10.0f, 1.0e-3f);
    const PivotLaw night = nightLaw(1.0f, 0.5f);
    EXPECT_NEAR(pivotGainDb(-57.0f, -27.0f, night), 5.0f, 1.0e-3f);
}

TEST(PivotGainTest, ItHoldsAtTheHardLimitOfTheLaw)
{
    EXPECT_EQ(pivotGainDb(60.0f, -27.0f, nightLaw(1.0f, 1.0f)), -24.0f);
}
