// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Multiband Compressor.

#include "allocationtrap.h"
#include "analysis.h"
#include "blocks/multibandcompressor.h"
#include "blocksizeinvariance.h"
#include "dsp/biquad.h"
#include "dsp/decibel.h"
#include "dsp/dynamics.h"
#include "offlinerunner.h"
#include "rt/meter.h"
#include "signals.h"
#include "wavfile.h"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <numbers>
#include <string>

using namespace rb;
using namespace rb::blocks;
using namespace rb::dsp;
using namespace rb::test;

namespace
{

using Multiband = MultibandCompressor;
constexpr std::size_t kSecond = 48000;

/// A tone well inside each band at the default splits (200 Hz and 3 kHz),
/// where the other bands have it more than 38 dB down. 12 kHz is a quarter
/// of the rate, whose samples can all miss the peak by 3 dB; the detector
/// reads between them, so the high band is tested there.
constexpr std::array<double, Multiband::kBands> kBandTone{50.0, 775.0, 12000.0};

/// The loudest \a signal gets above \a hz, in dBFS: a fourth-order
/// high-pass, then the peak. A click is broadband, so it shows here, where a
/// tone well below and a change of its level do not.
[[nodiscard]] float peakAboveDb(const Signal &signal, double hz)
{
    Biquad first;
    Biquad second;
    const BiquadCoefficients c = designBiquad({FilterShape::HighPass, hz, kButterworthQ, 0.0}, kTestSampleRate);
    first.setCoefficients(c);
    second.setCoefficients(c);
    Signal high(signal.size());
    for (std::size_t n = 0; n < signal.size(); ++n) {
        high[n] = second.process(first.process(signal[n]));
    }
    // After the filters have settled on the start of the signal.
    return peakDb(Signal(high.begin() + 480, high.end()));
}

void set(Multiband &block, const std::string &id, float value)
{
    ASSERT_TRUE(block.setParam(id, ParamValue::ofFloat(value)).has_value()) << id;
}

void setBand(Multiband &block, std::uint32_t band, float thresholdDb, float ratio, float makeupDb = 0.0f)
{
    const std::string name{Multiband::kBandNames[band]};
    set(block, name + "Threshold", thresholdDb);
    set(block, name + "Ratio", ratio);
    set(block, name + "Makeup", makeupDb);
}

/// Every band left alone: a ratio of 1:1 and no makeup.
void leaveEveryBandAlone(Multiband &block)
{
    for (std::uint32_t band = 0; band < Multiband::kBands; ++band) {
        setBand(block, band, 0.0f, 1.0f);
    }
}

[[nodiscard]] MultiSignal stereo(const Signal &signal)
{
    return MultiSignal{signal, signal};
}

[[nodiscard]] MultiSignal through(Multiband &block, const MultiSignal &input, std::uint32_t cycle = 256)
{
    BlockRunner runner(block, 2, 2);
    EXPECT_TRUE(runner.prepare().has_value());
    return runner.run(input, CycleShape::fixed(cycle));
}

/// The last second of a signal: a whole number of cycles of any whole number
/// of hertz, so a level measured over it has no leakage.
[[nodiscard]] Signal lastSecond(const Signal &signal)
{
    return Signal(signal.end() - static_cast<Signal::difference_type>(kSecond), signal.end());
}

/// Something with a voice's shape and an "s" in every syllable, over a bass
/// that thumps: what a multiband compressor is for.
[[nodiscard]] Signal voiceOverBass(std::size_t frames, std::uint32_t seed)
{
    Signal out(frames, 0.0f);
    const Signal hiss = whiteNoise(frames, 1.0f, seed);
    double phase = 0.0;
    for (std::size_t n = 0; n < frames; ++n) {
        const double t = static_cast<double>(n) / static_cast<double>(kSecond);
        const double syllable = std::fmod(t, 0.4);
        const double voiced = syllable < 0.25 ? std::sin(std::numbers::pi * syllable / 0.25) : 0.0;
        const double f0 = 130.0 + 20.0 * std::sin(2.0 * std::numbers::pi * 0.9 * t);
        phase += 2.0 * std::numbers::pi * f0 / static_cast<double>(kSecond);
        double voice = 0.0;
        for (int h = 1; h * f0 < 4500.0; ++h) {
            const double hz = h * f0;
            const double formants =
                1.0 / (1.0 + std::pow((hz - 600.0) / 150.0, 2.0)) + 0.5 / (1.0 + std::pow((hz - 1900.0) / 250.0, 2.0));
            voice += formants * std::sin(phase * h) / std::sqrt(static_cast<double>(h));
        }
        // The "s": bright hiss, a tenth of a second, at the end of each syllable.
        const double s =
            syllable >= 0.27 && syllable < 0.37 ? std::sin(std::numbers::pi * (syllable - 0.27) / 0.10) : 0.0;
        const double bright = n > 0 ? (hiss[n] - hiss[n - 1]) * 0.5 : 0.0;
        // A kick every half second: a falling 60 Hz thump.
        const double beat = std::fmod(t, 0.5);
        const double kick =
            std::exp(-beat / 0.12) * std::sin(2.0 * std::numbers::pi * (55.0 + 40.0 * std::exp(-beat / 0.03)) * beat);
        out[n] = static_cast<float>(0.06 * voiced * voice + 0.08 * s * bright + 0.35 * kick);
    }
    return out;
}

} // namespace

TEST(MultibandCompressorTest, WithTheBandsLeftAloneItChangesNothingYouCanHear)
{
    // The first acceptance criterion at the block's own level: with every
    // band at 1:1, what comes out is what went in, within ±0.1 dB at every
    // frequency — the crossover adds back up, and nothing else touches it.
    double worst = 0.0;
    for (const double hz : {30.0, 50.0, 100.0, 200.0, 500.0, 775.0, 1000.0, 2000.0, 3000.0, 5000.0, 12000.0, 18000.0}) {
        Multiband block;
        leaveEveryBandAlone(block);
        const Signal tone = sine(2 * kSecond, hz, kTestSampleRate, 0.5f);
        const MultiSignal out = through(block, stereo(tone));
        const double gain = gainToDb(amplitudeAt(lastSecond(out[0]), hz)) - gainToDb(0.5f);
        worst = std::max(worst, std::abs(gain));
        EXPECT_NEAR(gain, 0.0, 0.1) << hz << " Hz";
        EXPECT_NEAR(gainToDb(amplitudeAt(lastSecond(out[1]), hz)), gainToDb(amplitudeAt(lastSecond(out[0]), hz)), 1e-4)
            << "both sides alike at " << hz << " Hz";
    }
    std::printf("[measure] multiband with the bands left alone: within %.4f dB of the input, 30 Hz to 18 kHz\n", worst);
}

TEST(MultibandCompressorTest, EachBandFollowsTheDynamicsCurveForItsSettings)
{
    // The second acceptance criterion: a steady tone in a band comes out
    // where dsp::outputDb() says for that band's settings — the Dynamics
    // Processor's own curve, which the panel draws — and the other bands'
    // settings have nothing to do with it.
    for (std::uint32_t band = 0; band < Multiband::kBands; ++band) {
        Multiband block;
        leaveEveryBandAlone(block);
        set(block, "knee", 6.0f);
        setBand(block, band, -30.0f, 4.0f, 3.0f);
        // The other two set to hold back hard, which must not reach this one.
        for (std::uint32_t other = 0; other < Multiband::kBands; ++other) {
            if (other != band) {
                setBand(block, other, -50.0f, 20.0f, -10.0f);
            }
        }
        const DynamicsSettings curve = block.settings(band);
        EXPECT_FLOAT_EQ(curve.thresholdDb, -30.0f);
        EXPECT_FLOAT_EQ(curve.ratio, 4.0f);
        EXPECT_FLOAT_EQ(curve.kneeDb, 6.0f);
        EXPECT_FLOAT_EQ(curve.makeupDb, 3.0f);

        for (const float inputDb : {-45.0f, -33.0f, -30.0f, -27.0f, -18.0f, -8.0f}) {
            Multiband tone;
            ASSERT_TRUE(tone.loadState(block.saveState()).has_value());
            const Signal input = sine(3 * kSecond, kBandTone[band], kTestSampleRate, dbToGain(inputDb));
            const MultiSignal out = through(tone, stereo(input));
            const double measured = gainToDb(amplitudeAt(lastSecond(out[0]), kBandTone[band]));
            EXPECT_NEAR(measured, outputDb(inputDb, curve), 0.25)
                << Multiband::kBandNames[band] << " band, " << inputDb << " dBFS in";
            EXPECT_NEAR(tone.gainReduction(band), gainReductionDb(inputDb, curve), 0.25)
                << Multiband::kBandNames[band] << " band's reduction at " << inputDb;
        }
    }
}

TEST(MultibandCompressorTest, ALoudBassDoesNotPullTheVoiceDown)
{
    // What a multiband compressor is for: the bass band held back hard does
    // not take the middle down with it, where one compressor over the whole
    // signal would.
    Multiband block;
    leaveEveryBandAlone(block);
    setBand(block, 0, -40.0f, 10.0f);
    Signal mixed(3 * kSecond);
    const Signal bass = sine(mixed.size(), 50.0, kTestSampleRate, 0.7f);
    const Signal voice = sine(mixed.size(), 775.0, kTestSampleRate, 0.05f);
    for (std::size_t n = 0; n < mixed.size(); ++n) {
        mixed[n] = bass[n] + voice[n];
    }
    const MultiSignal out = through(block, stereo(mixed));
    const double bassOut = gainToDb(amplitudeAt(lastSecond(out[0]), 50.0));
    const double voiceOut = gainToDb(amplitudeAt(lastSecond(out[0]), 775.0));
    std::printf("[measure] a bass held back %.1f dB, the voice beside it moved %.3f dB\n",
                bassOut - gainToDb(0.7f),
                voiceOut - gainToDb(0.05f));
    EXPECT_LT(bassOut - gainToDb(0.7f), -20.0) << "the bass was not held back";
    EXPECT_NEAR(voiceOut, gainToDb(0.05f), 0.1) << "the voice moved with the bass";
}

TEST(MultibandCompressorTest, ItPublishesEachBandsGainReductionAndWhileAskedItsLevels)
{
    Multiband block;
    leaveEveryBandAlone(block);
    set(block, "knee", 0.0f);
    setBand(block, 1, -30.0f, 4.0f);
    rt::BlockMeter meter;
    meter.setAnalysing(true);
    BlockRunner runner(block, 2, 2);
    runner.setMeter(&meter);
    ASSERT_TRUE(runner.prepare().has_value());
    (void)runner.run(stereo(sine(2 * kSecond, kBandTone[1], kTestSampleRate, dbToGain(-10.0f))),
                     CycleShape::fixed(256));

    // The tile's three bars: only the middle band is holding back.
    const float expected = gainReductionDb(-10.0f, block.settings(1));
    EXPECT_NEAR(meter.value(Multiband::kGainReductionValue + 1), expected, 0.25f);
    EXPECT_NEAR(meter.value(Multiband::kGainReductionValue + 0), 0.0f, 0.05f);
    EXPECT_NEAR(meter.value(Multiband::kGainReductionValue + 2), 0.0f, 0.05f);

    // The panel's live marker, three values a band.
    const rt::BandValues bands = meter.bands();
    ASSERT_EQ(bands.count, Multiband::kBands * Multiband::kValuesPerBand);
    const std::size_t mid = Multiband::kValuesPerBand;
    EXPECT_NEAR(bands.values[mid + Multiband::kBandInputValue], -10.0f, 0.25f);
    EXPECT_NEAR(bands.values[mid + Multiband::kBandOutputValue], outputDb(-10.0f, block.settings(1)), 0.3f);
    EXPECT_NEAR(bands.values[mid + Multiband::kBandReductionValue], expected, 0.25f);

    // Not asked: nothing new is kept.
    meter.setAnalysing(false);
    const std::uint64_t before = meter.bandPublishCount();
    (void)runner.run(stereo(sine(4800, kBandTone[1], kTestSampleRate, 0.3f)), CycleShape::fixed(256));
    EXPECT_EQ(meter.bandPublishCount(), before);
}

TEST(MultibandCompressorTest, ItAddsNoLatency)
{
    // Nothing looks ahead and nothing is framed: the block says so, and a
    // tone in the middle of a band comes out on time. (A click would come
    // out turned by the crossover's all-passes, which is a change of phase
    // across frequencies, not a delay.)
    Multiband block;
    EXPECT_EQ(block.info().latencySamples, 0U);
    leaveEveryBandAlone(block);
    const Signal tone = sine(kSecond, kBandTone[1], kTestSampleRate, 0.5f);
    const MultiSignal out = through(block, stereo(tone));
    const std::optional<int> lag =
        latencySamples(Signal(tone.begin() + 24000, tone.end()), Signal(out[0].begin() + 24000, out[0].end()), 256);
    ASSERT_TRUE(lag.has_value());
    EXPECT_LE(std::abs(*lag), 2);
}

TEST(MultibandCompressorTest, ChangingItsSettingsWhileSoundPlaysDoesNotClick)
{
    // A 1 kHz tone, with every setting that reaches the sound moved between
    // cycles: the threshold and ratio (smoothed by the attack and release),
    // the makeup (its own glide) and both splits (a glide in octaves), past
    // the tone and back. **One signal across the cycles**, so a step
    // at a join is seen. A click is broadband: it is looked for above 8 kHz,
    // where a 1 kHz tone and a change of its level put nothing.
    Multiband block;
    leaveEveryBandAlone(block);
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    Signal whole;
    Signal clean;
    for (int n = 0; n < 400; ++n) {
        switch (n) {
        case 40:
            setBand(block, 1, -30.0f, 8.0f);
            break;
        case 80:
            set(block, "midMakeup", 12.0f);
            break;
        case 120:
            set(block, "lowCrossover", 1500.0f);
            set(block, "highCrossover", 9000.0f);
            break;
        case 200:
            set(block, "lowCrossover", 100.0f);
            set(block, "highCrossover", 600.0f);
            break;
        case 280:
            set(block, "highMakeup", -12.0f);
            set(block, "midMakeup", 0.0f);
            break;
        default:
            break;
        }
        // The same tone continued, so a cycle is not a new start.
        Signal part(256);
        for (std::size_t i = 0; i < part.size(); ++i) {
            const double t = static_cast<double>(n * 256 + static_cast<int>(i)) / kTestSampleRate;
            part[i] = static_cast<float>(0.3 * std::sin(2.0 * std::numbers::pi * 1000.0 * t));
        }
        const MultiSignal out = runner.run(stereo(part), CycleShape::fixed(256));
        whole.insert(whole.end(), out[0].begin(), out[0].end());
        clean.insert(clean.end(), part.begin(), part.end());
    }
    // What a click reads there: the same tone cut off dead half-way.
    Signal cut = clean;
    std::fill(cut.begin() + static_cast<Signal::difference_type>(cut.size() / 2) + 7, cut.end(), 0.0f);
    const float clickDb = peakAboveDb(cut, 8000.0);
    const float movedDb = peakAboveDb(whole, 8000.0);
    const float toneDb = peakAboveDb(clean, 8000.0);
    std::printf(
        "[measure] multiband with every setting moved: %.1f dBFS above 8 kHz (the tone alone %.1f, a click "
        "%.1f)\n",
        movedDb,
        toneDb,
        clickDb);
    EXPECT_LT(movedDb, clickDb - 30.0f);
}

TEST(MultibandCompressorTest, ItSoundsTheSameWhateverTheQuantum)
{
    MultiSignal input{voiceOverBass(24000, 3), voiceOverBass(24000, 4)};
    InvarianceOptions options;
    options.inputChannels = 2;
    options.outputChannels = 2;
    const InvarianceReport report = checkBlockSizeInvariance(
        [] {
            auto block = std::make_unique<Multiband>();
            // Working hard in every band, quickly, so anything kept per cycle
            // rather than per sample would show.
            for (std::uint32_t band = 0; band < Multiband::kBands; ++band) {
                EXPECT_TRUE(
                    block->setParam(std::string{Multiband::kBandNames[band]} + "Threshold", ParamValue::ofFloat(-40.0f))
                        .has_value());
                EXPECT_TRUE(
                    block->setParam(std::string{Multiband::kBandNames[band]} + "Ratio", ParamValue::ofFloat(8.0f))
                        .has_value());
                EXPECT_TRUE(
                    block->setParam(std::string{Multiband::kBandNames[band]} + "Attack", ParamValue::ofFloat(0.5f))
                        .has_value());
            }
            return std::unique_ptr<dsp::Block>(std::move(block));
        },
        input,
        options);
    EXPECT_TRUE(report.passed) << report.summary;
}

TEST(MultibandCompressorTest, ARunTouchesTheAllocatorNotAtAll)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    Multiband block;
    for (std::uint32_t band = 0; band < Multiband::kBands; ++band) {
        setBand(block, band, -40.0f, 6.0f, 4.0f);
    }
    rt::BlockMeter meter;
    meter.setAnalysing(true);
    BlockRunner runner(block, 2, 2);
    runner.setMeter(&meter);
    ASSERT_TRUE(runner.prepare().has_value());
    // Splits moving during the run, so the glide's per-sample redesign is
    // under the trap too.
    set(block, "lowCrossover", 800.0f);
    const Signal music = voiceOverBass(9600, 5);
    std::size_t allocations = 1;
    const MultiSignal out = runner.runWatchingForAllocations(stereo(music), CycleShape::random(1, 512, 3), allocations);
    EXPECT_EQ(allocations, 0U);
    EXPECT_EQ(out[0].size(), music.size());
}

TEST(MultibandCompressorTest, WhatWasSetIsWhatComesBack)
{
    Multiband block;
    set(block, "lowCrossover", 150.0f);
    set(block, "highCrossover", 5000.0f);
    set(block, "knee", 3.0f);
    setBand(block, 2, -12.0f, 6.0f, -2.0f);
    set(block, "highAttack", 0.5f);
    set(block, "highRelease", 40.0f);

    Multiband copy;
    ASSERT_TRUE(copy.loadState(block.saveState()).has_value());
    EXPECT_EQ(copy.saveState(), block.saveState());
    EXPECT_FLOAT_EQ(copy.settings(2).thresholdDb, -12.0f);
    EXPECT_FLOAT_EQ(copy.settings(2).makeupDb, -2.0f);
    EXPECT_FLOAT_EQ(copy.timing(2).attackMs, 0.5f);
    EXPECT_FLOAT_EQ(copy.timing(2).releaseMs, 40.0f);
    // Each band has its own; the others kept their defaults.
    EXPECT_FLOAT_EQ(copy.settings(0).thresholdDb, -24.0f);
    EXPECT_FLOAT_EQ(copy.timing(0).attackMs, 20.0f);
}

TEST(MultibandCompressorTest, AMinuteCostsLessThanThreePerCentOfACore)
{
    Multiband block;
    for (std::uint32_t band = 0; band < Multiband::kBands; ++band) {
        setBand(block, band, -30.0f, 4.0f, 3.0f);
    }
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const Signal music = voiceOverBass(60 * kSecond, 6);
    const auto started = std::chrono::steady_clock::now();
    (void)runner.run(stereo(music), CycleShape::fixed(256));
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const double share = took / 60.0;
    std::cerr << "multiband: a minute of stereo in " << took << " s, " << share * 100.0 << " % of a core\n";
#ifdef NDEBUG
    EXPECT_LT(share, 0.03) << "a minute took " << took << " s";
#else
    GTEST_SKIP() << "the figure above is from a debug build";
#endif
}

TEST(MultibandCompressorTest, ListeningFilesWhenAskedFor)
{
    // Only for a person to listen to: RB_LISTEN_DIR=/some/folder writes the
    // input and what the block makes of it there. Never played.
    const char *folder = std::getenv("RB_LISTEN_DIR");
    if (folder == nullptr || !audioFilesSupported()) {
        GTEST_SKIP() << "set RB_LISTEN_DIR to write the listening files";
    }
    const std::string dir{folder};
    const Signal input = voiceOverBass(10 * kSecond, 8);
    ASSERT_TRUE(writeWavFile(dir + "/input-voice-over-bass.wav", MultiSignal{input}).has_value());

    struct Take {
        const char *name;
        std::array<std::array<float, 3>, Multiband::kBands> bands; // threshold, ratio, makeup
    };
    for (const Take &take : {Take{"left-alone", {{{0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}}}},
                             Take{"defaults", {{{-24.0f, 3.0f, 0.0f}, {-24.0f, 3.0f, 0.0f}, {-24.0f, 3.0f, 0.0f}}}},
                             Take{"bass-held", {{{-36.0f, 8.0f, 3.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}}}},
                             Take{"de-ess", {{{0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {-42.0f, 8.0f, 0.0f}}}},
                             Take{"broadcast", {{{-30.0f, 4.0f, 6.0f}, {-30.0f, 3.0f, 6.0f}, {-34.0f, 4.0f, 4.0f}}}}}) {
        Multiband block;
        for (std::uint32_t band = 0; band < Multiband::kBands; ++band) {
            setBand(block, band, take.bands[band][0], take.bands[band][1], take.bands[band][2]);
        }
        const MultiSignal out = through(block, stereo(input));
        // Kept under full scale, so nothing is written clipped.
        Signal kept = out[0];
        const float top = peak(kept);
        if (top > dbToGain(-1.0f)) {
            for (float &sample : kept) {
                sample *= dbToGain(-1.0f) / top;
            }
        }
        ASSERT_TRUE(writeWavFile(dir + "/multiband-" + take.name + ".wav", MultiSignal{kept}).has_value());
    }
}
