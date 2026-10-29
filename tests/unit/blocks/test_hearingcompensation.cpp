// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Hearing Compensation 16, against its acceptance criteria: at 0 dB
// it passes the sound unchanged but for its latency; a tone at a band's soft,
// average and loud levels gets that band's three gains; nothing gets past a
// band's ceiling or the final limiter; the other ear goes through untouched;
// and it is block-size invariant, allocation-free and costs what the MR says.

#include "allocationtrap.h"
#include "analysis.h"
#include "blocks/hearingcompensation.h"
#include "blocksizeinvariance.h"
#include "dsp/decibel.h"
#include "dsp/erbscale.h"
#include "dsp/fittingrule.h"
#include "dsp/hearingbands.h"
#include "offlinerunner.h"
#include "rt/meter.h"
#include "signals.h"
#include "wavfile.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <numbers>
#include <string>

using namespace rb;
using namespace rb::blocks;
using namespace rb::dsp;
using namespace rb::test;

namespace
{

using Block16 = HearingCompensation;

void set(Block16 &block, const std::string &id, float value)
{
    ASSERT_TRUE(block.setParam(id, ParamValue::ofFloat(value)).has_value()) << id;
}

/// Every band set the same.
void setEveryBand(Block16 &block, float soft, float average, float loud, float ceiling = Block16::kDefaultCeilingDb)
{
    for (std::size_t band = 0; band < block.bandCount(); ++band) {
        set(block, Block16::bandParamId(band, Block16::SoftGain), soft);
        set(block, Block16::bandParamId(band, Block16::AverageGain), average);
        set(block, Block16::bandParamId(band, Block16::LoudGain), loud);
        set(block, Block16::bandParamId(band, Block16::Ceiling), ceiling);
    }
}

void setSpeed(Block16 &block, std::string_view speed)
{
    ASSERT_TRUE(block.setParam("speed", ParamValue::ofEnum(std::string{speed})).has_value());
}

[[nodiscard]] MultiSignal through(Block16 &block, const MultiSignal &input, std::uint32_t cycle = 256)
{
    BlockRunner runner(block, 2, 2);
    EXPECT_TRUE(runner.prepare().has_value());
    return runner.run(input, CycleShape::fixed(cycle));
}

/// The amplitude of \a hz in the last \a seconds of \a signal.
[[nodiscard]] float settledAmplitude(const Signal &signal, double hz, double seconds = 1.0)
{
    const auto frames = static_cast<std::size_t>(seconds * kTestSampleRate);
    const std::span<const float> tail{signal.data() + signal.size() - frames, frames};
    return amplitudeAt(tail, hz, kTestSampleRate);
}

/// The test's own true-peak meter: the signal interpolated \a factor times
/// with a windowed sinc 64 samples a side (as the Limiter's tests measure).
[[nodiscard]] float truePeakOf(const Signal &signal, int factor)
{
    constexpr int half = 64;
    float loudest = 0.0f;
    for (std::size_t n = half; n + half < signal.size(); ++n) {
        loudest = std::max(loudest, std::fabs(signal[n]));
        for (int point = 1; point < factor; ++point) {
            const double at = static_cast<double>(point) / factor;
            double sum = 0.0;
            for (int tap = -half + 1; tap <= half; ++tap) {
                const double x = at - tap;
                const double sinc = std::sin(std::numbers::pi * x) / (std::numbers::pi * x);
                const double window = 0.5 + 0.5 * std::cos(std::numbers::pi * x / half);
                sum += signal[n + tap] * sinc * window;
            }
            loudest = std::max(loudest, static_cast<float>(std::fabs(sum)));
        }
    }
    return loudest;
}

} // namespace

TEST(HearingCompensationTest, IsAHearingBlockForOneEarWithSixteenBands)
{
    Block16 block(Block16::kBands16, Block16::kTypeId16);
    EXPECT_EQ(block.info().typeId, "hearing.compensation16");
    EXPECT_EQ(block.info().category, BlockCategory::Hearing);
    EXPECT_EQ(block.info().face, FaceKind::Curve);
    EXPECT_EQ(block.bandCount(), 16U);
    EXPECT_EQ(Block16::bandParamId(0, Block16::SoftGain), "band0SoftGainDb");
    EXPECT_EQ(Block16::bandParamId(15, Block16::Ceiling), "band15CeilingDb");
    ASSERT_TRUE(block.param("band15CeilingDb").asFloat().has_value());
    EXPECT_TRUE(block.param("audiogram1000Db").asFloat().has_value()) << "the audiogram is the block's own";
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    // The filterbank's 4 ms and the limiter's look-ahead, at 48 kHz.
    EXPECT_EQ(block.info().latencySamples, 192U + 72U + TruePeakEstimator::kDelay);
}

TEST(HearingCompensationTest, AtZeroDecibelsItPassesTheSoundUnchangedButForItsLatency)
{
    Block16 block(Block16::kBands16, Block16::kTypeId16);
    const Signal noise = whiteNoise(48000, 0.05f, 11);
    const MultiSignal out = through(block, MultiSignal{noise, noise});
    const std::optional<int> lag = latencySamples(noise, out[0], 2000);
    ASSERT_TRUE(lag.has_value());
    EXPECT_EQ(*lag, static_cast<int>(block.info().latencySamples));

    float worst = 0.0f;
    for (const double hz : {125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 12000.0}) {
        Block16 fresh(Block16::kBands16, Block16::kTypeId16);
        const Signal tone = sine(48000, hz, kTestSampleRate, 0.1f);
        const MultiSignal toneOut = through(fresh, MultiSignal{tone, tone});
        const float db = gainToDb(settledAmplitude(toneOut[0], hz, 0.5) / 0.1f);
        worst = std::abs(db) > std::abs(worst) ? db : worst;
        EXPECT_NEAR(db, 0.0f, 0.5f) << "at " << hz << " Hz";
    }
    std::printf("[measure] every band at 0 dB: within %.2f dB of the input from 125 Hz to 12 kHz\n",
                static_cast<double>(std::abs(worst)));
}

TEST(HearingCompensationTest, AToneGetsItsBandsSoftAverageAndLoudGain)
{
    // 24, 12 and 0 dB at the soft, average and loud levels: a 2:1-ish
    // compressor that lifts soft sound most.
    float worst = 0.0f;
    for (const std::size_t band : {std::size_t{3}, std::size_t{8}, std::size_t{12}}) {
        const double hz = Block16::bandCentresHz(16)[band];
        const std::array<std::pair<float, float>, 3> cases{{
            {kSoftInputDbfs, 24.0f},
            {kAverageInputDbfs, 12.0f},
            {kLoudInputDbfs, 0.0f},
        }};
        for (const auto &[levelDbfs, gainDb] : cases) {
            Block16 block(Block16::kBands16, Block16::kTypeId16);
            setEveryBand(block, 24.0f, 12.0f, 0.0f);
            setSpeed(block, "slow");
            const float amplitude = dbToGain(levelDbfs);
            const Signal tone = sine(3 * 48000, hz, kTestSampleRate, amplitude);
            const MultiSignal out = through(block, MultiSignal{tone, tone});
            const float got = gainToDb(settledAmplitude(out[0], hz) / amplitude);
            worst = std::max(worst, std::abs(got - gainDb));
            EXPECT_NEAR(got, gainDb, 0.5f) << "band " << band << " at " << hz << " Hz, " << levelDbfs << " dBFS";
        }
    }
    std::printf("[measure] tones at the soft, average and loud levels of three bands: within %.2f dB of the gain\n",
                static_cast<double>(worst));
}

TEST(HearingCompensationTest, ASuddenLoudSoundStaysUnderItsBandsCeiling)
{
    // Every band lifts 20 dB, under a ceiling at -15 dBFS (70 on the
    // listening scale). A tone at a band's centre jumps from -60 to -5 dBFS
    // a second in: from the stretch it arrives in, the band holds it under
    // the ceiling.
    const std::size_t band = 10;
    const double hz = Block16::bandCentresHz(16)[band];
    Block16 block(Block16::kBands16, Block16::kTypeId16);
    setEveryBand(block, 20.0f, 20.0f, 20.0f, dbfsToListening(-15.0f));
    // At the slow speed, whose 50 ms attack would let the jump through for
    // a while if the ceiling waited for the detector.
    setSpeed(block, "slow");
    Signal tone = sine(2 * 48000, hz, kTestSampleRate, dbToGain(-60.0f));
    for (std::size_t frame = 48000; frame < tone.size(); ++frame) {
        tone[frame] *= dbToGain(55.0f);
    }
    const MultiSignal out = through(block, MultiSignal{tone, tone});
    // **The band's own output**: the tone, read over 256 samples at a time.
    // The onset of a sine switched on at once is a click across every band,
    // which the other bands pass with their own lift; their sum is the
    // final limiter's to hold, below.
    float loudest = 0.0f;
    for (std::size_t frame = 48000; frame + 256 < 48000 + 24000; frame += 32) {
        const std::span<const float> window{out[0].data() + frame, 256};
        loudest = std::max(loudest, amplitudeAt(window, hz, kTestSampleRate));
    }
    std::printf("[measure] a tone jumping 55 dB at %.0f Hz under a -15 dBFS ceiling: at most %.2f dBFS in its band\n",
                hz,
                static_cast<double>(gainToDb(loudest)));
    EXPECT_LE(gainToDb(loudest), -15.0f + 1.0f);
    // And it settles at the ceiling, not below it.
    EXPECT_NEAR(gainToDb(settledAmplitude(out[0], hz, 0.5)), -15.0f, 0.5f);
}

TEST(HearingCompensationTest, TheFinalLimiterKeepsTheTruePeakUnderItsCeiling)
{
    // Loud transients with every band lifting 20 dB and no band ceiling to
    // speak of: the bands add up past full scale, and the limiter holds the
    // true peak at -3 dBFS.
    Block16 block(Block16::kBands16, Block16::kTypeId16);
    setEveryBand(block, 20.0f, 20.0f, 20.0f);
    set(block, "limiterCeilingDb", -3.0f);
    setSpeed(block, "syllabic");
    Signal clicks = whiteNoise(48000, 0.02f, 3);
    for (std::size_t at = 24000; at < clicks.size(); at += 4800) {
        const Signal burst = whiteNoise(96, 0.9f, static_cast<std::uint32_t>(at));
        for (std::size_t frame = 0; frame < burst.size() && at + frame < clicks.size(); ++frame) {
            clicks[at + frame] = burst[frame];
        }
    }
    const MultiSignal out = through(block, MultiSignal{clicks, clicks});
    const Signal tail(out[0].begin() + 24000, out[0].end());
    const float peak4 = truePeakOf(tail, 4);
    std::printf("[measure] clicks lifted 20 dB under a -3 dBFS limiter: %.2f dBTP\n",
                static_cast<double>(gainToDb(peak4)));
    EXPECT_LE(peak4, dbToGain(-3.0f) * dbToGain(0.1f));
}

TEST(HearingCompensationTest, TheOtherEarGoesThroughUntouchedAndInLine)
{
    Block16 block(Block16::kBands16, Block16::kTypeId16);
    setEveryBand(block, 20.0f, 10.0f, 0.0f);
    const Signal left = whiteNoise(24000, 0.1f, 5);
    const Signal right = pinkNoise(24000, 0.1f, 6);
    const MultiSignal out = through(block, MultiSignal{left, right});
    const std::uint32_t latency = block.info().latencySamples;
    for (std::size_t frame = latency; frame < right.size(); ++frame) {
        ASSERT_EQ(out[1][frame], right[frame - latency]) << "frame " << frame;
    }
    // The ear it is set to is changed.
    EXPECT_FALSE(
        nulls(std::span<const float>{out[0].data() + latency, 20000}, std::span<const float>{left.data(), 20000}));
}

TEST(HearingCompensationTest, TheSameSoundInCyclesOfAnySizeComesOutTheSame)
{
    const auto make = [] {
        auto block = std::make_unique<Block16>(Block16::kBands16, Block16::kTypeId16);
        setEveryBand(*block, 24.0f, 12.0f, 0.0f, 90.0f);
        return std::unique_ptr<Block>(std::move(block));
    };
    const Signal speechLike = pinkNoise(48000, 0.2f, 9);
    InvarianceOptions options;
    options.inputChannels = 2;
    options.outputChannels = 2;
    const InvarianceReport report = checkBlockSizeInvariance(make, MultiSignal{speechLike, speechLike}, options);
    EXPECT_TRUE(report.passed) << report.summary;
}

TEST(HearingCompensationTest, RunsWithoutAllocating)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "this build cannot see allocations";
    }
    Block16 block(Block16::kBands16, Block16::kTypeId16);
    setEveryBand(block, 24.0f, 12.0f, 0.0f, 90.0f);
    rt::BlockMeter meter;
    meter.setAnalysing(true);
    BlockRunner runner(block, 2, 2);
    runner.setMeter(&meter);
    ASSERT_TRUE(runner.prepare().has_value());
    const Signal noise = pinkNoise(24000, 0.2f, 4);
    std::size_t allocations = 0;
    (void)runner.runWatchingForAllocations(MultiSignal{noise, noise}, CycleShape::random(32, 1024, 3), allocations);
    EXPECT_EQ(allocations, 0U);
}

TEST(HearingCompensationTest, WhileAnalysedItPublishesEachBandsGainAndLevel)
{
    Block16 block(Block16::kBands16, Block16::kTypeId16);
    setEveryBand(block, 24.0f, 12.0f, 0.0f);
    setSpeed(block, "slow");
    rt::BlockMeter meter;
    BlockRunner runner(block, 2, 2);
    runner.setMeter(&meter);
    ASSERT_TRUE(runner.prepare().has_value());
    const double hz = Block16::bandCentresHz(16)[6];
    const Signal tone = sine(2 * 48000, hz, kTestSampleRate, dbToGain(kAverageInputDbfs));
    (void)runner.run(MultiSignal{tone, tone});
    EXPECT_EQ(meter.bands().count, 0U) << "nothing is published while nobody is looking";

    meter.setAnalysing(true);
    (void)runner.run(MultiSignal{tone, tone});
    const rt::BandValues bands = meter.bands();
    ASSERT_EQ(bands.count, 32U) << "sixteen gains, then sixteen levels";
    EXPECT_NEAR(bands.values[6], 12.0f, 0.5f) << "band 6's gain now";
    EXPECT_NEAR(bands.values[16 + 6], kAverageInputDbfs, 0.5f) << "band 6's level";
    EXPECT_LT(bands.values[0], 1.0f) << "a band with nothing in it is below the noise floor, and gets no lift";
    EXPECT_NEAR(
        meter.value(Block16::kMeanGainValue),
        [&] {
            float sum = 0.0f;
            for (std::size_t band = 0; band < 16; ++band) {
                sum += bands.values[band];
            }
            return sum / 16.0f;
        }(),
        0.5f);
}

TEST(HearingCompensationTest, NoBandIsGivenMoreThanTheMostGain)
{
    Block16 block(Block16::kBands16, Block16::kTypeId16);
    setEveryBand(block, 50.0f, 50.0f, 50.0f);
    set(block, "maximumGainDb", 15.0f);
    setSpeed(block, "slow");
    const double hz = Block16::bandCentresHz(16)[8];
    const Signal tone = sine(2 * 48000, hz, kTestSampleRate, dbToGain(-50.0f));
    const MultiSignal out = through(block, MultiSignal{tone, tone});
    EXPECT_NEAR(gainToDb(settledAmplitude(out[0], hz) / dbToGain(-50.0f)), 15.0f, 0.5f);
}

TEST(HearingCompensationTest, CompressingAVowelKeepsMostOfItsFormants)
{
    // ADR-0019's measure, on the block as built: a steady vowel,
    // harmonics of 125 Hz under formants at 500, 1500 and 2500 Hz, whose
    // peaks stand over the harmonics 1.5 ERB either side of them. Compressed
    // 3:1 (30, 20 and 10 dB at the soft, average and loud levels), how much
    // of that contrast is left: each channel's level is the loudest reading
    // within a band's spacing, so the harmonics beside a formant are held
    // back with it rather than pulled up to its level.
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
    Block16 block(Block16::kBands16, Block16::kTypeId16);
    setEveryBand(block, 30.0f, 20.0f, 10.0f);
    setSpeed(block, "slow");
    const MultiSignal out = through(block, MultiSignal{vowel, vowel});
    const double in = contrast(vowel);
    const double kept = contrast(out[0]);
    std::printf("[measure] a vowel's formant contrast, %.1f dB in, %.1f dB out at 3:1 (%.0f %%)\n",
                in,
                kept,
                100.0 * kept / in);
    // Each channel on its own level kept 86 %, as the spike's 16 bands did;
    // the loudest reading within a band's spacing keeps all of it.
    EXPECT_GE(kept / in, 0.95);
}

TEST(HearingCompensationTest, AMinuteCostsLittleOfACore)
{
    Block16 block(Block16::kBands16, Block16::kTypeId16);
    setEveryBand(block, 24.0f, 12.0f, 0.0f, 90.0f);
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const Signal programme = pinkNoise(60 * 48000, 0.2f, 8);
    const auto started = std::chrono::steady_clock::now();
    (void)runner.run(MultiSignal{programme, programme}, CycleShape::fixed(256));
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const double share = took / 60.0;
    std::cerr << "hearing compensation 16: a minute of one ear at quantum 256 in " << took << " s, " << share * 100.0
              << " % of a core\n";
#ifdef NDEBUG
    EXPECT_LT(share, 0.03) << "a minute took " << took << " s";
#else
    GTEST_SKIP() << "the figure above is from a debug build";
#endif
}

namespace
{

/// A voice-like sound: syllables of a buzz at 110-140 Hz under moving
/// formants, with soft "s" sounds between them — high and quiet, which is
/// what a treble lift is for. About -24 dBFS.
[[nodiscard]] Signal speechLike(std::size_t frames)
{
    Signal out(frames, 0.0f);
    const Signal hiss = whiteNoise(frames, 1.0f, 17);
    double phase = 0.0;
    for (std::size_t n = 0; n < frames; ++n) {
        const double t = static_cast<double>(n) / kTestSampleRate;
        const double syllable = std::fmod(t, 0.5);
        const double voiced = syllable < 0.3 ? std::sin(std::numbers::pi * syllable / 0.3) : 0.0;
        const double f0 = 110.0 + 30.0 * std::sin(2.0 * std::numbers::pi * 0.7 * t);
        phase += 2.0 * std::numbers::pi * f0 / kTestSampleRate;
        const double first = 500.0 + 250.0 * std::sin(2.0 * std::numbers::pi * 1.3 * t);
        double sample = 0.0;
        for (int h = 1; h * f0 < 5000.0; ++h) {
            const double hz = h * f0;
            const auto resonance = [hz](double centre, double width) {
                return 1.0 / (1.0 + std::pow((hz - centre) / width, 2.0));
            };
            sample += (resonance(first, 80.0) + 0.5 * resonance(1600.0, 120.0) + 0.25 * resonance(2600.0, 160.0))
                * std::sin(phase * h) / std::sqrt(static_cast<double>(h));
        }
        const double s =
            syllable >= 0.34 && syllable < 0.46 ? std::sin(std::numbers::pi * (syllable - 0.34) / 0.12) : 0.0;
        // A crude high-pass for the hiss: the difference of neighbours.
        const double high = n > 0 ? (hiss[n] - hiss[n - 1]) * 0.5 : 0.0;
        out[n] = static_cast<float>(0.05 * voiced * sample + 0.012 * s * high);
    }
    return out;
}

/// Music-like: three-note chords over a bass, with a hi-hat on the beat.
[[nodiscard]] Signal musicLike(std::size_t frames)
{
    Signal out(frames, 0.0f);
    const Signal hat = whiteNoise(frames, 1.0f, 23);
    const std::array<std::array<double, 4>, 2> chords{{{110.0, 261.6, 329.6, 392.0}, {98.0, 246.9, 293.7, 392.0}}};
    for (std::size_t n = 0; n < frames; ++n) {
        const double t = static_cast<double>(n) / kTestSampleRate;
        const auto &chord = chords[static_cast<std::size_t>(t / 2.0) % 2];
        double sample = 0.0;
        for (const double f : chord) {
            for (int h = 1; h <= 6; ++h) {
                sample += std::sin(2.0 * std::numbers::pi * f * h * t) / (h * h);
            }
        }
        const double beat = std::fmod(t, 0.25);
        const double tick = beat < 0.03 ? std::exp(-beat / 0.008) : 0.0;
        const double high = n > 0 ? (hat[n] - hat[n - 1]) * 0.5 : 0.0;
        out[n] = static_cast<float>(0.03 * sample + 0.05 * tick * high);
    }
    return out;
}

/// Every band from a standard audiogram, by NAL-R, as the presets are made.
void fitFrom(Block16 &block, const std::array<float, 9> &levels)
{
    constexpr std::array<int, 9> kHz{250, 500, 750, 1000, 1500, 2000, 3000, 4000, 6000};
    Audiogram audiogram;
    for (std::size_t index = 0; index < kHz.size(); ++index) {
        for (std::size_t point = 0; point < kAudiogramPoints; ++point) {
            if (kAudiogramFrequenciesHz[point] == kHz[index]) {
                audiogram.points[point] = {AudiogramMark::Heard, levels[index], std::nullopt};
            }
        }
    }
    const std::vector<BandFit> fit =
        fitAudiogram(audiogram, Block16::bandCentresHz(16), defaultFittingOptions(AudiogramUnit::DbHl));
    for (std::size_t band = 0; band < fit.size(); ++band) {
        set(block, Block16::bandParamId(band, Block16::SoftGain), fit[band].softGainDb);
        set(block, Block16::bandParamId(band, Block16::AverageGain), fit[band].averageGainDb);
        set(block, Block16::bandParamId(band, Block16::LoudGain), fit[band].loudGainDb);
        set(block, Block16::bandParamId(band, Block16::Ceiling), fit[band].ceilingDb);
    }
}

} // namespace

TEST(HearingCompensationTest, ListeningFilesWhenAskedFor)
{
    // Only for a person to listen to: RB_LISTEN_DIR=/some/folder writes the
    // inputs and what the block makes of them there. Never played.
    const char *folder = std::getenv("RB_LISTEN_DIR");
    if (folder == nullptr || !audioFilesSupported()) {
        GTEST_SKIP() << "set RB_LISTEN_DIR to write the listening files";
    }
    const std::string dir{folder};
    const Signal speech = speechLike(8 * 48000);
    const Signal music = musicLike(8 * 48000);
    ASSERT_TRUE(writeWavFile(dir + "/input-speech.wav", MultiSignal{speech}).has_value());
    ASSERT_TRUE(writeWavFile(dir + "/input-music.wav", MultiSignal{music}).has_value());
    constexpr std::array<float, 9> kN2{20, 20, 22.5f, 25, 30, 35, 40, 45, 50};
    constexpr std::array<float, 9> kN3{35, 35, 35, 40, 45, 50, 55, 60, 65};
    struct Take {
        const char *name;
        const std::array<float, 9> *audiogram;
        const char *speed;
    };
    for (const Take take : {Take{"gentle-slope-adaptive", &kN2, "adaptive"},
                            Take{"steeper-slope-adaptive", &kN3, "adaptive"},
                            Take{"steeper-slope-syllabic", &kN3, "syllabic"},
                            Take{"steeper-slope-slow", &kN3, "slow"}}) {
        for (const auto &[what, input] : {std::pair{"speech", &speech}, std::pair{"music", &music}}) {
            Block16 block(Block16::kBands16, Block16::kTypeId16);
            fitFrom(block, *take.audiogram);
            setSpeed(block, take.speed);
            const MultiSignal out = through(block, MultiSignal{*input, *input});
            ASSERT_TRUE(writeWavFile(dir + "/" + what + "-" + take.name + ".wav", MultiSignal{out[0]}).has_value());
        }
    }

    // **Hearing Compensation 64**, set through points: the steeper
    // slope fitted at the nine frequencies as its preset is, and a narrow
    // lift of 20 dB over half an octave at 4 kHz (a notch in the ear's
    // hearing, filled in), which sixteen bands cannot draw.
    constexpr std::array<double, 9> kPointsHz{250, 500, 750, 1000, 1500, 2000, 3000, 4000, 6000};
    const auto setPoint =
        [](Block16 &block, std::size_t point, double hz, float soft, float average, float loud, float ceiling) {
            ASSERT_TRUE(block.setParam(Block16::pointParamId(point, Block16::PointOn), ParamValue::ofBool(true)));
            set(block, Block16::pointParamId(point, Block16::PointHz), static_cast<float>(hz));
            set(block, Block16::pointParamId(point, Block16::PointSoftGain), soft);
            set(block, Block16::pointParamId(point, Block16::PointAverageGain), average);
            set(block, Block16::pointParamId(point, Block16::PointLoudGain), loud);
            set(block, Block16::pointParamId(point, Block16::PointCeiling), ceiling);
        };
    const auto offEveryPoint = [](Block16 &block) {
        for (std::size_t point = 0; point < Block16::kMaxPoints; ++point) {
            ASSERT_TRUE(block.setParam(Block16::pointParamId(point, Block16::PointOn), ParamValue::ofBool(false)));
        }
    };
    for (const auto &[what, input] : {std::pair{"speech", &speech}, std::pair{"music", &music}}) {
        Block16 slope(Block16::kBands64, Block16::kTypeId64, Block16::Editing::Points);
        offEveryPoint(slope);
        Audiogram n3;
        const std::array<float, 9> levels{35, 35, 35, 40, 45, 50, 55, 60, 65};
        for (std::size_t index = 0; index < kPointsHz.size(); ++index) {
            for (std::size_t point = 0; point < kAudiogramPoints; ++point) {
                if (kAudiogramFrequenciesHz[point] == static_cast<int>(kPointsHz[index])) {
                    n3.points[point] = {AudiogramMark::Heard, levels[index], std::nullopt};
                }
            }
        }
        const std::vector<BandFit> fit = fitAudiogram(
            n3, std::vector<double>(kPointsHz.begin(), kPointsHz.end()), defaultFittingOptions(AudiogramUnit::DbHl));
        for (std::size_t point = 0; point < fit.size(); ++point) {
            setPoint(slope,
                     point,
                     kPointsHz[point],
                     fit[point].softGainDb,
                     fit[point].averageGainDb,
                     fit[point].loudGainDb,
                     fit[point].ceilingDb);
        }
        const MultiSignal sloped = through(slope, MultiSignal{*input, *input});
        ASSERT_TRUE(
            writeWavFile(dir + "/" + what + "-64-steeper-slope-points.wav", MultiSignal{sloped[0]}).has_value());

        Block16 notch(Block16::kBands64, Block16::kTypeId64, Block16::Editing::Points);
        offEveryPoint(notch);
        const std::array<double, 5> notchHz{4000.0 / std::pow(2.0, 0.75),
                                            4000.0 / std::pow(2.0, 0.25),
                                            4000.0,
                                            4000.0 * std::pow(2.0, 0.25),
                                            4000.0 * std::pow(2.0, 0.75)};
        const std::array<float, 5> lift{0.0f, 20.0f, 20.0f, 20.0f, 0.0f};
        for (std::size_t point = 0; point < notchHz.size(); ++point) {
            setPoint(notch, point, notchHz[point], lift[point], lift[point], lift[point] * 0.5f, 95.0f);
        }
        const MultiSignal lifted = through(notch, MultiSignal{*input, *input});
        ASSERT_TRUE(writeWavFile(dir + "/" + what + "-64-notch-lift-4k.wav", MultiSignal{lifted[0]}).has_value());
    }
}
