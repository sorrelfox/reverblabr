// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Hearing Compensation 64, against its acceptance criteria: a narrow
// dip set with points is heard within 2 dB; neighbouring bands move with a
// loud one (ADR-0019's band coupling, measured on the block as built); and
// Hearing Compensation 16's criteria hold with 64 bands — at 0 dB it passes
// the sound unchanged but for its latency, a tone gets its band's three
// gains, nothing gets past a band's ceiling or the final limiter, the other
// ear goes through untouched, and it is block-size invariant, allocation-free
// and cheap enough for both ears.

#include "allocationtrap.h"
#include "analysis.h"
#include "blocks/hearingcompensation.h"
#include "blocksizeinvariance.h"
#include "dsp/decibel.h"
#include "dsp/erbscale.h"
#include "dsp/hearingbands.h"
#include "offlinerunner.h"
#include "rt/meter.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <iostream>
#include <numbers>
#include <string>

using namespace rb;
using namespace rb::blocks;
using namespace rb::dsp;
using namespace rb::test;

namespace
{

using Block64 = HearingCompensation;

[[nodiscard]] Block64 make64()
{
    return Block64(Block64::kBands64, Block64::kTypeId64, Block64::Editing::Points);
}

void set(Block64 &block, const std::string &id, float value)
{
    ASSERT_TRUE(block.setParam(id, ParamValue::ofFloat(value)).has_value()) << id;
}

void setOn(Block64 &block, std::size_t point, bool on)
{
    ASSERT_TRUE(block.setParam(Block64::pointParamId(point, Block64::PointOn), ParamValue::ofBool(on)).has_value());
}

/// A point at \a hz asking for the same gain for soft, average and loud
/// sound, under \a ceiling.
void place(Block64 &block, std::size_t point, double hz, float gain, float ceiling = Block64::kDefaultCeilingDb)
{
    setOn(block, point, true);
    set(block, Block64::pointParamId(point, Block64::PointHz), static_cast<float>(hz));
    for (const auto which : {Block64::PointSoftGain, Block64::PointAverageGain, Block64::PointLoudGain}) {
        set(block, Block64::pointParamId(point, which), gain);
    }
    set(block, Block64::pointParamId(point, Block64::PointCeiling), ceiling);
}

/// Every point the block starts with set the same, so every band is.
void setEveryPoint(Block64 &block, float soft, float average, float loud, float ceiling = Block64::kDefaultCeilingDb)
{
    for (std::size_t point = 0; point < Block64::kDefaultPointsHz.size(); ++point) {
        set(block, Block64::pointParamId(point, Block64::PointSoftGain), soft);
        set(block, Block64::pointParamId(point, Block64::PointAverageGain), average);
        set(block, Block64::pointParamId(point, Block64::PointLoudGain), loud);
        set(block, Block64::pointParamId(point, Block64::PointCeiling), ceiling);
    }
}

void setSpeed(Block64 &block, std::string_view speed)
{
    ASSERT_TRUE(block.setParam("speed", ParamValue::ofEnum(std::string{speed})).has_value());
}

[[nodiscard]] MultiSignal through(Block64 &block, const MultiSignal &input, std::uint32_t cycle = 256)
{
    BlockRunner runner(block, 2, 2);
    EXPECT_TRUE(runner.prepare().has_value());
    return runner.run(input, CycleShape::fixed(cycle));
}

[[nodiscard]] float settledAmplitude(const Signal &signal, double hz, double seconds = 1.0)
{
    const auto frames = static_cast<std::size_t>(seconds * kTestSampleRate);
    const std::span<const float> tail{signal.data() + signal.size() - frames, frames};
    return amplitudeAt(tail, hz, kTestSampleRate);
}

/// The gain a steady tone at \a hz and \a levelDbfs gets, in dB.
[[nodiscard]] float toneGainDb(Block64 &block, double hz, float levelDbfs)
{
    const float amplitude = dbToGain(levelDbfs);
    const Signal tone = sine(3 * 48000, hz, kTestSampleRate, amplitude);
    const MultiSignal out = through(block, MultiSignal{tone, tone});
    return gainToDb(settledAmplitude(out[0], hz) / amplitude);
}

} // namespace

TEST(HearingCompensation64Test, IsAHearingBlockForOneEarWithSixtyFourBandsSetThroughPoints)
{
    Block64 block = make64();
    EXPECT_EQ(block.info().typeId, "hearing.compensation64");
    EXPECT_EQ(block.info().category, BlockCategory::Hearing);
    EXPECT_EQ(block.info().face, FaceKind::Curve);
    EXPECT_EQ(block.bandCount(), 64U);
    EXPECT_EQ(block.editing(), Block64::Editing::Points);
    EXPECT_EQ(Block64::pointParamId(0, Block64::PointOn), "point0On");
    EXPECT_EQ(Block64::pointParamId(23, Block64::PointCeiling), "point23CeilingDb");
    EXPECT_EQ(Block64::pointParamId(5, Block64::PointAverageGain), "point5AverageGainDb");
    // Six points to start with, an octave apart from 250 Hz; the rest are
    // off until they are placed. No band settings of its own.
    EXPECT_EQ(block.param("point0Hz").asFloat(), 250.0f);
    EXPECT_EQ(block.param("point5Hz").asFloat(), 8000.0f);
    EXPECT_EQ(block.param("point5On").asBool(), true);
    EXPECT_EQ(block.param("point6On").asBool(), false);
    EXPECT_EQ(block.param("point23On").asBool(), false);
    EXPECT_FALSE(block.param("point24On").asBool().has_value());
    EXPECT_FALSE(block.param("band0SoftGainDb").asFloat().has_value());
    EXPECT_TRUE(block.param("audiogram1000Db").asFloat().has_value()) << "the audiogram is the block's own";
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    // The same filterbank and limiter as the 16-band block, so the same
    // latency.
    EXPECT_EQ(block.info().latencySamples, 192U + 72U + TruePeakEstimator::kDelay);
}

TEST(HearingCompensation64Test, TheBandsFollowTheCurvesThroughThePoints)
{
    Block64 block = make64();
    place(block, 0, 500.0, 10.0f);
    place(block, 1, 4000.0, 30.0f);
    for (std::size_t point = 2; point < Block64::kDefaultPointsHz.size(); ++point) {
        setOn(block, point, false);
    }
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    (void)runner.run(MultiSignal{Signal(256, 0.0f), Signal(256, 0.0f)});
    const std::vector<double> centres = erbCentres(64);
    const std::span<const HearingBand> bands = block.bandSettings();
    ASSERT_EQ(bands.size(), 64U);
    for (std::size_t band = 0; band < 64; ++band) {
        if (centres[band] <= 500.0) {
            EXPECT_FLOAT_EQ(bands[band].averageGainDb, 10.0f) << band;
        } else if (centres[band] >= 4000.0) {
            EXPECT_FLOAT_EQ(bands[band].averageGainDb, 30.0f) << band;
        } else {
            EXPECT_GT(bands[band].averageGainDb, 10.0f) << band;
            EXPECT_LT(bands[band].averageGainDb, 30.0f) << band;
        }
    }
    // Moving a point moves the curve.
    set(block, Block64::pointParamId(1, Block64::PointAverageGain), 0.0f);
    (void)runner.run(MultiSignal{Signal(256, 0.0f), Signal(256, 0.0f)});
    EXPECT_FLOAT_EQ(block.bandSettings()[63].averageGainDb, 0.0f);
}

TEST(HearingCompensation64Test, ANarrowDipSetWithPointsIsHeardWithinTwoDecibels)
{
    // The narrow dip Hearing Compensation 64 has to follow: -20 dB over half
    // an octave at 4 kHz, the points at the dip's edges and middle and level
    // ground three quarters of an octave either side. What comes out of the
    // block, tone by tone, at the level of average sound.
    const std::array<double, 5> hz{4000.0 / std::pow(2.0, 0.75),
                                   4000.0 / std::pow(2.0, 0.25),
                                   4000.0,
                                   4000.0 * std::pow(2.0, 0.25),
                                   4000.0 * std::pow(2.0, 0.75)};
    const std::array<float, 5> gain{0.0f, -20.0f, -20.0f, -20.0f, 0.0f};
    const auto dip = [&](Block64 &block) {
        for (std::size_t point = 0; point < Block64::kDefaultPointsHz.size(); ++point) {
            setOn(block, point, false);
        }
        for (std::size_t point = 0; point < hz.size(); ++point) {
            place(block, point, hz[point], gain[point]);
        }
        setSpeed(block, "slow");
    };
    float worst = 0.0f;
    for (const double at : {hz[1], 4000.0 / std::pow(2.0, 0.125), 4000.0, 4000.0 * std::pow(2.0, 0.125), hz[3]}) {
        Block64 block = make64();
        dip(block);
        const float got = toneGainDb(block, at, kAverageInputDbfs);
        worst = std::max(worst, std::abs(got + 20.0f));
        EXPECT_NEAR(got, -20.0f, 2.0f) << "at " << at << " Hz";
    }
    // And the ground either side is left where it was.
    for (const double at : {1000.0, 12000.0}) {
        Block64 block = make64();
        dip(block);
        EXPECT_NEAR(toneGainDb(block, at, kAverageInputDbfs), 0.0f, 0.5f) << "at " << at << " Hz";
    }
    std::printf(
        "[measure] a -20 dB dip over half an octave at 4 kHz, set with points: tones across it within %.2f "
        "dB of -20\n",
        static_cast<double>(worst));
}

TEST(HearingCompensation64Test, ALoudBandPullsItsNeighboursGainsWithIt)
{
    // ADR-0019's band coupling on the block as built: every point lifts soft
    // sound 30 dB, average 20 and loud 10 (3:1), and no noise floor gets in
    // the way. A loud tone at one band's centre over faint noise: the bands
    // around it take the tone's level, and its loud gain, rather than
    // lifting the tone's skirts by their own soft gain; bands far off keep
    // the soft gain of the faint noise.
    Block64 block = make64();
    setEveryPoint(block, 30.0f, 20.0f, 10.0f);
    set(block, "noiseFloorDb", -100.0f);
    setSpeed(block, "slow");
    rt::BlockMeter meter;
    meter.setAnalysing(true);
    BlockRunner runner(block, 2, 2);
    runner.setMeter(&meter);
    ASSERT_TRUE(runner.prepare().has_value());
    const std::vector<double> centres = erbCentres(64);
    const std::size_t loud = 36;
    Signal input = pinkNoise(3 * 48000, dbToGain(-70.0f), 12);
    const Signal tone = sine(input.size(), centres[loud], kTestSampleRate, dbToGain(kLoudInputDbfs));
    for (std::size_t frame = 0; frame < input.size(); ++frame) {
        input[frame] += tone[frame];
    }
    (void)runner.run(MultiSignal{input, input});
    const rt::BandValues values = meter.bands();
    ASSERT_EQ(values.count, 128U) << "sixty-four gains, then sixty-four levels";
    const double at = erbNumber(centres[loud]);
    std::string profile;
    float widestHeldErbs = 0.0f;
    for (std::size_t band = 0; band < 64; ++band) {
        const double away = std::abs(erbNumber(centres[band]) - at);
        const float gain = values.values[band];
        if (away <= 6.0) {
            char line[64];
            std::snprintf(line, sizeof line, " %+.1f:%.1f", erbNumber(centres[band]) - at, static_cast<double>(gain));
            profile += line;
        }
        if (std::abs(gain - values.values[loud]) <= 1.0f) {
            widestHeldErbs = std::max(widestHeldErbs, static_cast<float>(away));
        }
        if (away <= 2.0) {
            // ADR-0019: a band's level over about two auditory filters either
            // side, so the tone's neighbours out to 2 ERB move with it.
            EXPECT_NEAR(gain, values.values[loud], 1.0f) << "band " << band << ", " << away << " ERB away";
        } else if (away >= 6.0) {
            EXPECT_GT(gain, 25.0f) << "band " << band << ", " << away << " ERB away, took the tone's level";
        }
    }
    EXPECT_NEAR(values.values[loud], 10.0f, 1.0f) << "the tone's own band, at the loud level";
    std::printf("[measure] a loud tone's neighbours (ERB away:gain dB):%s\n", profile.c_str());
    std::printf("[measure] bands within 1 dB of the loud band's gain out to %.1f ERB either side\n",
                static_cast<double>(widestHeldErbs));
}

TEST(HearingCompensation64Test, AtZeroDecibelsItPassesTheSoundUnchangedButForItsLatency)
{
    Block64 block = make64();
    const Signal noise = whiteNoise(48000, 0.05f, 11);
    const MultiSignal out = through(block, MultiSignal{noise, noise});
    const std::optional<int> lag = latencySamples(noise, out[0], 2000);
    ASSERT_TRUE(lag.has_value());
    EXPECT_EQ(*lag, static_cast<int>(block.info().latencySamples));
    float worst = 0.0f;
    for (const double hz : {125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 12000.0}) {
        Block64 fresh = make64();
        const Signal tone = sine(48000, hz, kTestSampleRate, 0.1f);
        const MultiSignal toneOut = through(fresh, MultiSignal{tone, tone});
        const float db = gainToDb(settledAmplitude(toneOut[0], hz, 0.5) / 0.1f);
        worst = std::abs(db) > std::abs(worst) ? db : worst;
        EXPECT_NEAR(db, 0.0f, 0.5f) << "at " << hz << " Hz";
    }
    std::printf("[measure] 64 bands at 0 dB: within %.2f dB of the input from 125 Hz to 12 kHz\n",
                static_cast<double>(std::abs(worst)));
}

TEST(HearingCompensation64Test, AToneGetsItsBandsSoftAverageAndLoudGain)
{
    float worst = 0.0f;
    const std::vector<double> centres = erbCentres(64);
    for (const std::size_t band : {std::size_t{12}, std::size_t{32}, std::size_t{50}}) {
        const std::array<std::pair<float, float>, 3> cases{{
            {kSoftInputDbfs, 24.0f},
            {kAverageInputDbfs, 12.0f},
            {kLoudInputDbfs, 0.0f},
        }};
        for (const auto &[levelDbfs, gainDb] : cases) {
            Block64 block = make64();
            setEveryPoint(block, 24.0f, 12.0f, 0.0f);
            setSpeed(block, "slow");
            const float got = toneGainDb(block, centres[band], levelDbfs);
            worst = std::max(worst, std::abs(got - gainDb));
            EXPECT_NEAR(got, gainDb, 0.5f) << "band " << band << ", " << levelDbfs << " dBFS";
        }
    }
    std::printf("[measure] 64 bands: tones at the soft, average and loud levels within %.2f dB of the gain\n",
                static_cast<double>(worst));
}

TEST(HearingCompensation64Test, ASuddenLoudSoundStaysUnderItsBandsCeiling)
{
    const double hz = erbCentres(64)[40];
    Block64 block = make64();
    setEveryPoint(block, 20.0f, 20.0f, 20.0f, dbfsToListening(-15.0f));
    setSpeed(block, "slow");
    Signal tone = sine(2 * 48000, hz, kTestSampleRate, dbToGain(-60.0f));
    for (std::size_t frame = 48000; frame < tone.size(); ++frame) {
        tone[frame] *= dbToGain(55.0f);
    }
    const MultiSignal out = through(block, MultiSignal{tone, tone});
    float loudest = 0.0f;
    for (std::size_t frame = 48000; frame + 256 < 48000 + 24000; frame += 32) {
        const std::span<const float> window{out[0].data() + frame, 256};
        loudest = std::max(loudest, amplitudeAt(window, hz, kTestSampleRate));
    }
    std::printf("[measure] 64 bands: a tone jumping 55 dB under a -15 dBFS ceiling peaks at %.2f dBFS in its band\n",
                static_cast<double>(gainToDb(loudest)));
    EXPECT_LE(gainToDb(loudest), -15.0f + 1.0f);
    EXPECT_NEAR(gainToDb(settledAmplitude(out[0], hz, 0.5)), -15.0f, 0.5f);
}

TEST(HearingCompensation64Test, TheOtherEarGoesThroughUntouchedAndInLine)
{
    Block64 block = make64();
    setEveryPoint(block, 20.0f, 10.0f, 0.0f);
    ASSERT_TRUE(block.setParam("ear", ParamValue::ofEnum("right")).has_value());
    const Signal left = pinkNoise(24000, 0.1f, 6);
    const Signal right = whiteNoise(24000, 0.1f, 5);
    const MultiSignal out = through(block, MultiSignal{left, right});
    const std::uint32_t latency = block.info().latencySamples;
    for (std::size_t frame = latency; frame < left.size(); ++frame) {
        ASSERT_EQ(out[0][frame], left[frame - latency]) << "frame " << frame;
    }
    EXPECT_FALSE(
        nulls(std::span<const float>{out[1].data() + latency, 20000}, std::span<const float>{right.data(), 20000}));
}

TEST(HearingCompensation64Test, TheSameSoundInCyclesOfAnySizeComesOutTheSame)
{
    const auto make = [] {
        auto block = std::make_unique<Block64>(Block64::kBands64, Block64::kTypeId64, Block64::Editing::Points);
        setEveryPoint(*block, 24.0f, 12.0f, 0.0f, 90.0f);
        set(*block, Block64::pointParamId(2, Block64::PointAverageGain), -10.0f);
        return std::unique_ptr<dsp::Block>(std::move(block));
    };
    const Signal speechLike = pinkNoise(48000, 0.2f, 9);
    InvarianceOptions options;
    options.inputChannels = 2;
    options.outputChannels = 2;
    const InvarianceReport report = checkBlockSizeInvariance(make, MultiSignal{speechLike, speechLike}, options);
    EXPECT_TRUE(report.passed) << report.summary;
}

TEST(HearingCompensation64Test, RunsWithoutAllocatingWhileItsPointsMove)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "this build cannot see allocations";
    }
    Block64 block = make64();
    setEveryPoint(block, 24.0f, 12.0f, 0.0f, 90.0f);
    rt::BlockMeter meter;
    meter.setAnalysing(true);
    BlockRunner runner(block, 2, 2);
    runner.setMeter(&meter);
    ASSERT_TRUE(runner.prepare().has_value());
    const Signal noise = pinkNoise(24000, 0.2f, 4);
    std::size_t allocations = 0;
    (void)runner.runWatchingForAllocations(MultiSignal{noise, noise}, CycleShape::random(32, 1024, 3), allocations);
    EXPECT_EQ(allocations, 0U);
    // A point moved and one switched on, as a drag does, on the audio
    // thread's side of the store.
    block.applyParam(Block64::FirstBandParam + 2 * Block64::PointParamCount + Block64::PointHz, 1500.0f);
    block.applyParam(Block64::FirstBandParam + 10 * Block64::PointParamCount + Block64::PointOn, 1.0f);
    (void)runner.runWatchingForAllocations(MultiSignal{noise, noise}, CycleShape::random(32, 1024, 5), allocations);
    EXPECT_EQ(allocations, 0U);
}

TEST(HearingCompensation64Test, WhileAnalysedItPublishesEachBandsGainAndLevel)
{
    Block64 block = make64();
    setEveryPoint(block, 24.0f, 12.0f, 0.0f);
    setSpeed(block, "slow");
    rt::BlockMeter meter;
    BlockRunner runner(block, 2, 2);
    runner.setMeter(&meter);
    ASSERT_TRUE(runner.prepare().has_value());
    const std::size_t band = 30;
    const Signal tone = sine(2 * 48000, erbCentres(64)[band], kTestSampleRate, dbToGain(kAverageInputDbfs));
    (void)runner.run(MultiSignal{tone, tone});
    EXPECT_EQ(meter.bands().count, 0U) << "nothing is published while nobody is looking";
    meter.setAnalysing(true);
    (void)runner.run(MultiSignal{tone, tone});
    const rt::BandValues bands = meter.bands();
    ASSERT_EQ(bands.count, 128U);
    EXPECT_NEAR(bands.values[band], 12.0f, 0.5f) << "the band's gain now";
    EXPECT_NEAR(bands.values[64 + band], kAverageInputDbfs, 0.5f) << "the band's level";
    EXPECT_LT(bands.values[63], 1.0f) << "a band with nothing in it gets no lift";
}

TEST(HearingCompensation64Test, CompressingAVowelKeepsMostOfItsFormants)
{
    // ADR-0019's measure for the 64-band block as built: formants at
    // 500, 1500 and 2500 Hz over harmonics of 125 Hz, compressed 3:1.
    constexpr double kF0 = 125.0;
    const std::size_t frames = 3 * 48000;
    Signal vowel(frames, 0.0f);
    std::vector<double> amplitude;
    for (int h = 1; h * kF0 < 4000.0; ++h) {
        const double hz = h * kF0;
        const auto resonance = [hz](double centre, double width) {
            return 1.0 / (1.0 + std::pow((hz - centre) / width, 2.0));
        };
        amplitude.push_back(resonance(500.0, 60.0) + 0.5 * resonance(1500.0, 100.0) + 0.3 * resonance(2500.0, 140.0)
                            + 0.02);
    }
    for (std::size_t n = 0; n < frames; ++n) {
        const double t = static_cast<double>(n) / kTestSampleRate;
        double sample = 0.0;
        for (std::size_t h = 0; h < amplitude.size(); ++h) {
            sample += amplitude[h] * std::sin(2.0 * std::numbers::pi * kF0 * static_cast<double>(h + 1) * t);
        }
        vowel[n] = static_cast<float>(sample * 0.02);
    }
    const auto contrast = [&](const Signal &signal) {
        const std::span<const float> settled(signal.data() + frames / 2, frames / 2);
        double total = 0.0;
        for (const double formant : {500.0, 1500.0, 2500.0}) {
            const double peak = amplitudeAt(settled, formant, kTestSampleRate);
            const double below = hzAtErbNumber(erbNumber(formant) - 1.5);
            const double above = hzAtErbNumber(erbNumber(formant) + 1.5);
            const auto harmonic = [](double hz) { return std::round(hz / kF0) * kF0; };
            const double sides = 0.5
                * (amplitudeAt(settled, harmonic(below), kTestSampleRate)
                   + amplitudeAt(settled, harmonic(above), kTestSampleRate));
            total += 20.0 * std::log10(peak / sides);
        }
        return total / 3.0;
    };
    Block64 block = make64();
    setEveryPoint(block, 30.0f, 20.0f, 10.0f);
    setSpeed(block, "slow");
    const MultiSignal out = through(block, MultiSignal{vowel, vowel});
    const double in = contrast(vowel);
    const double kept = contrast(out[0]);
    std::printf("[measure] 64 bands: a vowel's formant contrast, %.1f dB in, %.1f dB out at 3:1 (%.0f %%)\n",
                in,
                kept,
                100.0 * kept / in);
    EXPECT_GE(kept / in, 0.95);
}

TEST(HearingCompensation64Test, BothEarsForAMinuteCostLittleOfACore)
{
    // What Hearing Compensation 64 has to be cheap enough for: two
    // instances, one an ear, at quantum 256.
    Block64 left = make64();
    Block64 right = make64();
    ASSERT_TRUE(right.setParam("ear", ParamValue::ofEnum("right")).has_value());
    for (Block64 *block : {&left, &right}) {
        setEveryPoint(*block, 24.0f, 12.0f, 0.0f, 90.0f);
    }
    BlockRunner leftRunner(left, 2, 2);
    BlockRunner rightRunner(right, 2, 2);
    ASSERT_TRUE(leftRunner.prepare().has_value());
    ASSERT_TRUE(rightRunner.prepare().has_value());
    const Signal programme = pinkNoise(60 * 48000, 0.2f, 8);
    const auto started = std::chrono::steady_clock::now();
    const MultiSignal once = leftRunner.run(MultiSignal{programme, programme}, CycleShape::fixed(256));
    (void)rightRunner.run(once, CycleShape::fixed(256));
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const double share = took / 60.0;
    std::cerr << "hearing compensation 64: a minute of both ears (two blocks) at quantum 256 in " << took << " s, "
              << share * 100.0 << " % of a core\n";
#ifdef NDEBUG
    EXPECT_LT(share, 0.06) << "a minute took " << took << " s";
#else
    GTEST_SKIP() << "the figure above is from a debug build";
#endif
}
