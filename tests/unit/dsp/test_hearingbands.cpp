// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The listening blocks' filterbank and band gains (ADR-0019): the 64
// gammatone channels sum flat and exactly as late as they say; a sine reads
// full scale in its band; the gain law goes through the three gains and
// then the loudness, floor, maximum and ceiling; and the three speeds take
// the time ADR-0019 gives them.

#include "analysis.h"
#include "dsp/decibel.h"
#include "dsp/erbscale.h"
#include "dsp/gammatonebank.h"
#include "dsp/hearingbands.h"
#include "dsp/monotonecurve.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using namespace rb;
using namespace rb::dsp;
using namespace rb::test;

namespace
{

/// A sine of \a amplitude at \a hz through \a bank with every gain at 1,
/// and the amplitude that comes out once it has settled.
[[nodiscard]] float gainThrough(GammatoneBank &bank, double hz, double rate)
{
    bank.reset();
    const auto frames = static_cast<std::size_t>(0.25 * rate);
    Signal signal = sine(frames, hz, rate, 0.25f);
    bank.process(signal, [](std::span<const float>, std::span<float>) noexcept { });
    const std::size_t settled = frames / 2;
    const std::span<const float> tail{signal.data() + settled, frames - settled};
    return amplitudeAt(tail, hz, rate) / 0.25f;
}

/// The frequencies a sum is checked at: 24 steps on the ERB scale, off the
/// channels' centres as well as on them.
[[nodiscard]] std::vector<double> checkedFrequencies()
{
    std::vector<double> hz;
    const double low = erbNumber(kErbLowestHz);
    const double high = erbNumber(kErbHighestHz);
    for (int step = 0; step <= 23; ++step) {
        hz.push_back(hzAtErbNumber(low + (high - low) * step / 23.0));
    }
    return hz;
}

} // namespace

TEST(HearingFilterbank, WithEveryGainAtOneItSumsFlatAtEveryRate)
{
    for (const double rate : {44100.0, 48000.0, 96000.0}) {
        GammatoneBank bank;
        bank.prepare(rate);
        float worst = 0.0f;
        double worstHz = 0.0;
        for (const double hz : checkedFrequencies()) {
            const float db = gainToDb(gainThrough(bank, hz, rate));
            if (std::abs(db) > std::abs(worst)) {
                worst = db;
                worstHz = hz;
            }
        }
        std::printf("[measure] 64 gammatone channels at %.1f kHz sum flat within %.2f dB (worst at %.0f Hz)\n",
                    rate / 1000.0,
                    static_cast<double>(std::abs(worst)),
                    worstHz);
        // ADR-0019's ±0.36 dB, with room for the measurement.
        EXPECT_LE(std::abs(worst), 0.45f) << "at " << worstHz << " Hz and " << rate << " Hz";
    }
}

TEST(HearingFilterbank, IsExactlyAsLateAsItSays)
{
    for (const double rate : {44100.0, 48000.0, 96000.0}) {
        GammatoneBank bank;
        bank.prepare(rate);
        const auto frames = static_cast<std::size_t>(0.5 * rate);
        const Signal noise = whiteNoise(frames, 0.25f, 7);
        Signal out = noise;
        bank.process(out, [](std::span<const float>, std::span<float>) noexcept { });
        const std::optional<int> lag = latencySamples(noise, out, 1000);
        ASSERT_TRUE(lag.has_value());
        EXPECT_EQ(*lag, static_cast<int>(bank.latencySamples())) << "at " << rate << " Hz";
        EXPECT_EQ(bank.latencySamples(), static_cast<std::uint32_t>(std::lround(0.004 * rate)));
    }
}

TEST(HearingFilterbank, ASineReadsOneInTheChannelAtItsCentre)
{
    GammatoneBank bank;
    bank.prepare(48000.0);
    for (const std::size_t channel : {std::size_t{5}, std::size_t{30}, std::size_t{60}}) {
        bank.reset();
        Signal tone = sine(9600, bank.centresHz()[channel], 48000.0, 1.0f);
        float last = 0.0f;
        bank.process(tone, [&](std::span<const float> power, std::span<float>) noexcept { last = power[channel]; });
        EXPECT_NEAR(last, 1.0f, 0.02f) << "channel " << channel << " at " << bank.centresHz()[channel] << " Hz";
        EXPECT_NEAR(bank.sinePower(channel, bank.centresHz()[channel]), 1.0, 1e-6);
    }
}

TEST(HearingFilterbank, AGainAppliesToItsOwnChannelsAndRampsIn)
{
    GammatoneBank bank;
    bank.prepare(48000.0);
    const double hz = bank.centresHz()[40];
    Signal tone = sine(24000, hz, 48000.0, 0.1f);
    bank.process(tone, [](std::span<const float>, std::span<float> gains) noexcept {
        std::fill(gains.begin(), gains.end(), 2.0f);
    });
    const std::span<const float> tail{tone.data() + 12000, 12000};
    EXPECT_NEAR(gainToDb(amplitudeAt(tail, hz, 48000.0) / 0.1f), 6.02f, 0.45f);
    // A gain that jumps is ramped over a stretch: no sample-to-sample step
    // bigger than the tone's own.
    EXPECT_LE(maxSampleStep(tone), 0.2f * 2.0f * static_cast<float>(2.0 * std::numbers::pi * hz / 48000.0) + 0.01f);
}

TEST(HearingBandLayout, ASineReadsItsOwnLevelInTheChannelsAroundIt)
{
    GammatoneBank bank;
    bank.prepare(48000.0);
    BandLayout layout;
    layout.prepare(bank.centresHz(), 16, erbSpacing(16), [&](std::size_t channel, double hz) {
        return bank.sinePower(channel, hz);
    });
    ASSERT_EQ(layout.bandCount(), 16U);
    ASSERT_EQ(layout.channelCount(), 64U);
    std::vector<float> levels(64);
    float worst = 0.0f;
    for (const double hz : checkedFrequencies()) {
        bank.reset();
        Signal tone = sine(9600, hz, 48000.0, 0.5f);
        bank.process(tone, [&](std::span<const float> power, std::span<float>) noexcept {
            layout.channelLevels(power, levels);
        });
        // Every channel within 1.5 ERB of the tone reads its level: the
        // ones whose skirts it passes through as well as the ones it is in.
        for (std::size_t channel = 0; channel < 64; ++channel) {
            if (std::abs(erbNumber(bank.centresHz()[channel]) - erbNumber(hz)) <= 1.5) {
                const float db = 10.0f * std::log10(levels[channel]);
                worst = std::max(worst, std::abs(db + 6.02f));
                EXPECT_NEAR(db, -6.02f, 0.25f) << "channel " << channel << " for a tone at " << hz << " Hz";
            }
        }
    }
    std::printf("[measure] a sine reads its level within %.2f dB in every channel within 1.5 ERB\n",
                static_cast<double>(worst));
}

TEST(HearingBandLayout, EachChannelTakesItsSettingsFromTheBandsEitherSide)
{
    GammatoneBank bank;
    bank.prepare(48000.0);
    BandLayout layout;
    layout.prepare(bank.centresHz(), 16, erbSpacing(16), [&](std::size_t channel, double hz) {
        return bank.sinePower(channel, hz);
    });
    std::vector<HearingBand> bands(16);
    for (std::size_t band = 0; band < 16; ++band) {
        bands[band].softGainDb = static_cast<float>(band);
        bands[band].ceilingDbfs = -static_cast<float>(band);
    }
    std::vector<HearingBand> channels(64);
    layout.channelSettings(bands, channels);
    // A straight line in dB over the ERB scale between band centres: band
    // k sits at channel 4.2k, so channel 21 is band 5 exactly, and every
    // channel lies between its neighbours.
    EXPECT_NEAR(channels.front().softGainDb, 0.0f, 1e-4f);
    EXPECT_NEAR(channels.back().softGainDb, 15.0f, 1e-4f);
    EXPECT_NEAR(channels[21].softGainDb, 5.0f, 1e-3f);
    EXPECT_NEAR(channels[21].ceilingDbfs, -5.0f, 1e-3f);
    EXPECT_EQ(layout.channelOfBand(5), 21U);
    for (std::size_t channel = 1; channel < channels.size(); ++channel) {
        EXPECT_GT(channels[channel].softGainDb, channels[channel - 1].softGainDb);
    }
}

TEST(HearingGainLaw, TheThreeGainsApplyAtTheirThreeLevelsAndInAStraightLineBetween)
{
    const HearingBand band{.softGainDb = 30.0f, .averageGainDb = 20.0f, .loudGainDb = 10.0f, .ceilingDbfs = 60.0f};
    const HearingSettings settings{
        .speed = HearingSpeed::Slow, .loudnessDb = 0.0f, .noiseFloorDbfs = -100.0f, .maximumGainDb = 60.0f};
    EXPECT_FLOAT_EQ(steadyGainDb(band, settings, kSoftInputDbfs), 30.0f);
    EXPECT_FLOAT_EQ(steadyGainDb(band, settings, kAverageInputDbfs), 20.0f);
    EXPECT_FLOAT_EQ(steadyGainDb(band, settings, kLoudInputDbfs), 10.0f);
    EXPECT_FLOAT_EQ(steadyGainDb(band, settings, kSoftInputDbfs + 7.5f), 25.0f);
    // Softer than soft: the soft gain; louder than loud: the same slope on.
    EXPECT_FLOAT_EQ(steadyGainDb(band, settings, -80.0f), 30.0f);
    EXPECT_FLOAT_EQ(steadyGainDb(band, settings, kLoudInputDbfs + 15.0f), 0.0f);
}

TEST(HearingGainLaw, ALouderSoundNeverComesOutQuieter)
{
    // Average to loud falls 30 dB over 15: carried on beyond loud, that
    // would take a louder sound below a quieter one. It is held at -1 dB a
    // decibel, so the output stays level instead.
    const HearingBand band{.softGainDb = 40.0f, .averageGainDb = 40.0f, .loudGainDb = 10.0f, .ceilingDbfs = 60.0f};
    const HearingSettings settings{
        .speed = HearingSpeed::Slow, .loudnessDb = 0.0f, .noiseFloorDbfs = -100.0f, .maximumGainDb = 60.0f};
    float lastOut = -1000.0f;
    for (float level = -90.0f; level <= 0.0f; level += 0.5f) {
        const float out = level + steadyGainDb(band, settings, level);
        EXPECT_GE(out, lastOut - 1e-4f) << "at " << level << " dBFS";
        lastOut = out;
    }
}

TEST(HearingGainLaw, LoudnessFloorMaximumAndCeilingComeAfterTheThreeGains)
{
    const HearingBand band{.softGainDb = 30.0f, .averageGainDb = 20.0f, .loudGainDb = 10.0f, .ceilingDbfs = 60.0f};
    HearingSettings settings{
        .speed = HearingSpeed::Slow, .loudnessDb = 5.0f, .noiseFloorDbfs = -70.0f, .maximumGainDb = 32.0f};
    // Loudness over the three gains, under the maximum.
    EXPECT_FLOAT_EQ(steadyGainDb(band, settings, kAverageInputDbfs), 25.0f);
    EXPECT_FLOAT_EQ(steadyGainDb(band, settings, kSoftInputDbfs), 32.0f);
    // Below the floor the lift falls away a decibel a decibel, to none.
    EXPECT_FLOAT_EQ(steadyGainDb(band, settings, -80.0f), 25.0f);
    EXPECT_FLOAT_EQ(steadyGainDb(band, settings, -200.0f), 0.0f);
    // Nothing goes out above the ceiling, whatever the three gains ask.
    HearingBand low = band;
    low.ceilingDbfs = -30.0f;
    EXPECT_FLOAT_EQ(steadyGainDb(low, settings, kAverageInputDbfs), 10.0f);
    EXPECT_FLOAT_EQ(steadyGainDb(low, settings, kLoudInputDbfs), -30.0f - kLoudInputDbfs);
    settings.loudnessDb = -40.0f;
    // A cut stays a cut, below the floor as above it.
    EXPECT_FLOAT_EQ(steadyGainDb(band, settings, -200.0f), -10.0f);
}

namespace
{

/// A band's level after \a seconds of \a power, on a control fed every
/// kControlFrames frames at 48 kHz.
struct Detector {
    explicit Detector(HearingSpeed speed)
    {
        settings.speed = speed;
        settings.noiseFloorDbfs = -100.0f;
        control.prepare(1, static_cast<double>(GammatoneBank::kControlFrames) / 48000.0);
        control.reset(settings);
    }
    float run(double seconds, float levelDbfs)
    {
        const float power = std::pow(10.0f, levelDbfs / 10.0f);
        const auto updates = static_cast<int>(std::lround(seconds * 48000.0 / GammatoneBank::kControlFrames));
        std::vector<float> gain(1);
        for (int update = 0; update < updates; ++update) {
            control.update(std::span<const float>{&power, 1}, std::span<const HearingBand>{&band, 1}, settings, gain);
        }
        return control.levelsDbfs()[0];
    }
    HearingSettings settings;
    HearingBand band;
    BandGainControl control;
};

} // namespace

TEST(HearingDetectors, EachSpeedTakesItsAttackAndRelease)
{
    // On the standard 35 dB step at 3:1, a gain within 3 dB is a level
    // within 4.5 dB.
    {
        Detector syllabic(HearingSpeed::Syllabic);
        (void)syllabic.run(1.0, -70.0f);
        EXPECT_GT(-35.0f - syllabic.run(0.003, -35.0f), 4.5f) << "the syllabic attack is 5 ms, not 3";
        EXPECT_LE(-35.0f - syllabic.run(0.0025, -35.0f), 4.5f) << "the syllabic attack is 5 ms";
        (void)syllabic.run(1.0, -35.0f);
        EXPECT_LE(syllabic.run(0.075, -70.0f) - -70.0f, 6.0f) << "the syllabic release is 70 ms";
    }
    {
        Detector slow(HearingSpeed::Slow);
        (void)slow.run(1.0, -70.0f);
        EXPECT_GT(-35.0f - slow.run(0.030, -35.0f), 4.5f) << "the slow attack is 50 ms, not 30";
        EXPECT_LE(-35.0f - slow.run(0.022, -35.0f), 4.5f) << "the slow attack is 50 ms";
    }
}

TEST(HearingDetectors, AdaptiveCatchesABurstAndLetsGoOfItSoon)
{
    Detector adaptive(HearingSpeed::Adaptive);
    Detector slow(HearingSpeed::Slow);
    (void)adaptive.run(3.0, -50.0f);
    (void)slow.run(3.0, -50.0f);
    // A 100 ms burst 25 dB up: caught within 10 ms.
    EXPECT_GT(adaptive.run(0.010, -25.0f), -29.0f);
    (void)adaptive.run(0.090, -25.0f);
    (void)slow.run(0.100, -25.0f);
    // 60 ms after it, Adaptive is back where it was; Slow is not.
    const float adaptiveAfter = adaptive.run(0.060, -50.0f);
    const float slowAfter = slow.run(0.060, -50.0f);
    std::printf("[measure] 60 ms after a 100 ms burst 25 dB up: Adaptive %.1f dB above the level, Slow %.1f\n",
                static_cast<double>(adaptiveAfter + 50.0f),
                static_cast<double>(slowAfter + 50.0f));
    EXPECT_LE(adaptiveAfter + 50.0f, 1.5f);
    EXPECT_GT(slowAfter + 50.0f, 10.0f);
}

TEST(HearingDetectors, TheLiftFadesInWhenTheBlockStarts)
{
    Detector detector(HearingSpeed::Slow);
    detector.band = {.softGainDb = 20.0f, .averageGainDb = 20.0f, .loudGainDb = 20.0f, .ceilingDbfs = 60.0f};
    const float power = std::pow(10.0f, -40.0f / 10.0f);
    std::vector<float> gain(1);
    detector.control.update(
        std::span<const float>{&power, 1}, std::span<const HearingBand>{&detector.band, 1}, detector.settings, gain);
    EXPECT_LT(gain[0], 0.1f) << "the first stretch goes out at nearly no lift";
    (void)detector.run(BandGainControl::kFadeInSeconds, -40.0f);
    detector.control.update(
        std::span<const float>{&power, 1}, std::span<const HearingBand>{&detector.band, 1}, detector.settings, gain);
    EXPECT_FLOAT_EQ(gain[0], 20.0f);
}

TEST(HearingDetectors, ACeilingHoldsFromTheFirstStretchWhileTheLiftFadesIn)
{
    // A block that starts on a loud sound: the lift fades in, but the cut
    // that keeps the sound under the ceiling is there at once.
    Detector detector(HearingSpeed::Slow);
    detector.band = {.softGainDb = 20.0f, .averageGainDb = 20.0f, .loudGainDb = 20.0f, .ceilingDbfs = -30.0f};
    const float power = std::pow(10.0f, -10.0f / 10.0f);
    std::vector<float> gain(1);
    detector.control.update(
        std::span<const float>{&power, 1}, std::span<const HearingBand>{&detector.band, 1}, detector.settings, gain);
    EXPECT_FLOAT_EQ(gain[0], -20.0f);
}

// --- control points ---

TEST(HearingPointCurve, BetweenTwoPointsItNeverGoesBeyondEither)
{
    // What Hearing Compensation 64 needs: a curve through the points never
    // overshoots them. Two hundred sets of points at random frequencies and
    // levels, the curve read at a thousand places between each pair: never
    // above the higher of the two, never below the lower.
    std::mt19937 random(254);
    std::uniform_real_distribution<double> step(0.05, 1.0);
    std::uniform_real_distribution<float> level(-30.0f, 60.0f);
    std::size_t overshoots = 0;
    double worst = 0.0;
    for (int set = 0; set < 200; ++set) {
        std::vector<double> x;
        std::vector<float> y;
        double at = std::log(100.0);
        const std::size_t count = 2 + static_cast<std::size_t>(set % (MonotoneCurve::kMaxPoints - 1));
        for (std::size_t point = 0; point < count; ++point) {
            at += step(random);
            x.push_back(at);
            // Every so often two points level with each other, where a
            // curve most likes to bulge.
            y.push_back(point > 0 && set % 3 == 0 && point % 4 == 0 ? y.back() : level(random));
        }
        MonotoneCurve curve;
        curve.fit(x, y);
        for (std::size_t piece = 0; piece + 1 < count; ++piece) {
            const double low = std::min(y[piece], y[piece + 1]);
            const double high = std::max(y[piece], y[piece + 1]);
            for (int sample = 0; sample <= 1000; ++sample) {
                const double where = x[piece] + (x[piece + 1] - x[piece]) * sample / 1000.0;
                const double value = curve.at(where);
                const double beyond = std::max(value - high, low - value);
                if (beyond > 1e-4) {
                    ++overshoots;
                    worst = std::max(worst, beyond);
                }
            }
        }
    }
    std::printf(
        "[measure] 200 random curves through up to 24 points: %zu readings beyond their two points "
        "(worst %.3f dB)\n",
        overshoots,
        worst);
    EXPECT_EQ(overshoots, 0U);
}

TEST(HearingPointCurve, GoesThroughItsPointsAndIsLevelBeyondTheEnds)
{
    const std::vector<double> x{1.0, 2.0, 4.0};
    const std::vector<float> y{0.0f, -20.0f, 10.0f};
    MonotoneCurve curve;
    curve.fit(x, y);
    for (std::size_t point = 0; point < x.size(); ++point) {
        EXPECT_FLOAT_EQ(curve.at(x[point]), y[point]);
    }
    EXPECT_FLOAT_EQ(curve.at(0.0), 0.0f);
    EXPECT_FLOAT_EQ(curve.at(9.0), 10.0f);
    // A dip is level at its bottom, so it is no deeper than its point.
    EXPECT_GT(curve.at(1.99), -20.0f);
    EXPECT_GT(curve.at(2.01), -20.0f);

    MonotoneCurve one;
    one.fit(std::vector<double>{3.0}, std::vector<float>{7.0f});
    EXPECT_FLOAT_EQ(one.at(-5.0), 7.0f);
    EXPECT_FLOAT_EQ(one.at(50.0), 7.0f);
    MonotoneCurve none;
    EXPECT_FLOAT_EQ(none.at(1.0), 0.0f);
}

TEST(HearingPointCurve, EachSettingFollowsItsOwnCurveThroughThePointsThatAreOn)
{
    const std::vector<double> centres = erbCentres(64);
    std::vector<HearingPoint> points(kMaxHearingPoints);
    points[0] = {.on = true, .hz = 500.0, .settings = {10.0f, 5.0f, 0.0f, -20.0f}};
    points[1] = {.on = true, .hz = 4000.0, .settings = {30.0f, 20.0f, 10.0f, -10.0f}};
    // Off, and so no part of the curves, however loud it asks.
    points[2] = {.on = false, .hz = 1000.0, .settings = {60.0f, 60.0f, 60.0f, 40.0f}};
    // On the same frequency as the first: counted once, the first of them.
    points[3] = {.on = true, .hz = 500.0, .settings = {-30.0f, -30.0f, -30.0f, -60.0f}};
    const HearingBand flat{.ceilingDbfs = 15.0f};
    std::vector<HearingBand> bands(64);
    bandsFromPoints(points, centres, flat, bands);
    for (std::size_t band = 0; band < 64; ++band) {
        const HearingBand &at = bands[band];
        if (centres[band] <= 500.0) {
            EXPECT_FLOAT_EQ(at.softGainDb, 10.0f) << band;
            EXPECT_FLOAT_EQ(at.ceilingDbfs, -20.0f) << band;
        } else if (centres[band] >= 4000.0) {
            EXPECT_FLOAT_EQ(at.averageGainDb, 20.0f) << band;
            EXPECT_FLOAT_EQ(at.loudGainDb, 10.0f) << band;
        } else {
            // Between the two, each setting on its own way from one to the
            // other: the soft gain from 10 to 30, the loud from 0 to 10.
            EXPECT_GT(at.softGainDb, 10.0f) << band;
            EXPECT_LT(at.softGainDb, 30.0f) << band;
            EXPECT_GT(at.loudGainDb, 0.0f) << band;
            EXPECT_LT(at.loudGainDb, 10.0f) << band;
        }
    }

    // None on: every band as it would be with nothing set.
    for (HearingPoint &point : points) {
        point.on = false;
    }
    bandsFromPoints(points, centres, flat, bands);
    for (const HearingBand &at : bands) {
        EXPECT_FLOAT_EQ(at.softGainDb, 0.0f);
        EXPECT_FLOAT_EQ(at.ceilingDbfs, 15.0f);
    }
}

TEST(HearingPointCurve, ANarrowDipSetWithPointsIsFollowedByTheSixtyFourBands)
{
    // The narrow dip Hearing Compensation 64 has to follow: -20 dB over half
    // an octave at 4 kHz, set with points at the dip's edges and middle and
    // level ground three quarters of an octave either side. Sixteen bands
    // cannot follow it; sixty-four do.
    const std::array<double, 5> hz{4000.0 / std::pow(2.0, 0.75),
                                   4000.0 / std::pow(2.0, 0.25),
                                   4000.0,
                                   4000.0 * std::pow(2.0, 0.25),
                                   4000.0 * std::pow(2.0, 0.75)};
    const std::array<float, 5> db{0.0f, -20.0f, -20.0f, -20.0f, 0.0f};
    std::vector<HearingPoint> points(kMaxHearingPoints);
    for (std::size_t point = 0; point < hz.size(); ++point) {
        points[point] = {.on = true, .hz = hz[point], .settings = {db[point], db[point], db[point], 0.0f}};
    }
    for (const std::size_t count : {std::size_t{16}, std::size_t{64}}) {
        const std::vector<double> centres = erbCentres(count);
        std::vector<HearingBand> bands(count);
        bandsFromPoints(points, centres, HearingBand{}, bands);
        // How deep the bands inside the dip go, and whether any is outside
        // what the points ask for there.
        float deepest = 0.0f;
        int inside = 0;
        for (std::size_t band = 0; band < count; ++band) {
            if (centres[band] >= hz[1] && centres[band] <= hz[3]) {
                deepest = std::min(deepest, bands[band].averageGainDb);
                ++inside;
                EXPECT_NEAR(bands[band].averageGainDb, -20.0f, 2.0f) << count << " bands, band " << band;
            }
        }
        std::printf(
            "[measure] a -20 dB dip over half an octave at 4 kHz: %d of %zu bands inside it, the deepest "
            "at %.1f dB\n",
            inside,
            count,
            static_cast<double>(deepest));
        if (count == 64) {
            EXPECT_GE(inside, 3) << "sixty-four bands put several inside the dip";
        }
    }
}

TEST(HearingPointCurve, ThePanelsCurveIsTheBlocksToTheLastDecimal)
{
    // The same three points and readings as tst_hearingcompensation64.qml
    // checks HearingBands.qml's curve against: the panel draws what the
    // block does.
    const std::vector<double> x{std::log(250.0), std::log(1000.0), std::log(4000.0)};
    const std::vector<float> y{0.0f, 20.0f, 5.0f};
    MonotoneCurve curve;
    curve.fit(x, y);
    EXPECT_NEAR(curve.at(std::log(500.0)), 12.5f, 1e-4f);
    EXPECT_NEAR(curve.at(std::log(2000.0)), 14.375f, 1e-4f);
    EXPECT_NEAR(curve.at(std::log(3000.0)), 8.6247f, 1e-4f);
}
