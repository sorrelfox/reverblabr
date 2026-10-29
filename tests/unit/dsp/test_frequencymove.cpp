// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The move engine of Frequency Lowering and Frequency Raising:
// where a move carries a frequency, a tone carried there and nothing left
// behind, a tone copied an octave down at its level, two moves that each
// read the input, "when needed" leaving a vowel alone and acting on a hiss,
// nothing moved coming out as it went in a frame later, and no step when a
// move changes. Raising: a tone copied two octaves up at its level, a tone
// given its harmonics in the destination and nothing under it, and "when
// needed" facing up.

#include "allocationtrap.h"
#include "analysis.h"
#include "dsp/biquad.h"
#include "dsp/decibel.h"
#include "dsp/fft.h"
#include "dsp/frequencymove.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <random>
#include <span>
#include <vector>

using namespace rb;
using namespace rb::dsp;
using namespace rb::test;

namespace
{

constexpr std::size_t kSecond = 48000;

/// The first move Frequency Lowering has to make: 4–8 kHz squeezed into
/// 2.5–4 kHz, the source emptied.
[[nodiscard]] FrequencyMove compressHighs()
{
    FrequencyMove move;
    move.active = true;
    move.method = MoveMethod::Compress;
    move.sourceLowHz = 4000.0f;
    move.sourceHighHz = 8000.0f;
    move.destinationLowHz = 2500.0f;
    move.destinationHighHz = 4000.0f;
    move.source = MoveSource::Remove;
    return move;
}

/// An octave down, 4–8 kHz into 2–4 kHz, the source kept.
[[nodiscard]] FrequencyMove transposeOctave(float levelDb = 0.0f)
{
    FrequencyMove move;
    move.active = true;
    move.method = MoveMethod::Transpose;
    move.sourceLowHz = 4000.0f;
    move.sourceHighHz = 8000.0f;
    move.destinationLowHz = 2000.0f;
    move.destinationHighHz = 4000.0f;
    move.levelDb = levelDb;
    move.source = MoveSource::Keep;
    return move;
}

/// \a input through a mover with \a moves, and the output.
[[nodiscard]] Signal moved(const Signal &input,
                           std::span<const FrequencyMove> moves,
                           double rate = kTestSampleRate,
                           double frameMs = FrequencyMover::kFrameMs)
{
    FrequencyMover mover;
    mover.prepare(rate, frameMs);
    for (std::size_t slot = 0; slot < moves.size(); ++slot) {
        mover.setMove(slot, moves[slot]);
    }
    mover.reset();
    Signal output(input.size(), 0.0f);
    mover.process(input, output);
    return output;
}

/// The last half second of \a signal: long after any start.
[[nodiscard]] std::span<const float> settled(const Signal &signal)
{
    return std::span<const float>(signal).subspan(signal.size() - kSecond / 2);
}

/// The frequency between \a fromHz and \a toHz that \a signal is loudest
/// at, a Hann window over it and read every hertz.
[[nodiscard]] double loudestHz(std::span<const float> signal, double fromHz, double toHz)
{
    double best = fromHz;
    double bestPower = -1.0;
    const double size = static_cast<double>(signal.size());
    for (double hz = fromHz; hz <= toHz; hz += 1.0) {
        const double step = 2.0 * std::numbers::pi * hz / kTestSampleRate;
        double real = 0.0;
        double imaginary = 0.0;
        for (std::size_t n = 0; n < signal.size(); ++n) {
            const double window = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(n) / size);
            real += window * signal[n] * std::cos(step * static_cast<double>(n));
            imaginary += window * signal[n] * std::sin(step * static_cast<double>(n));
        }
        const double power = real * real + imaginary * imaginary;
        if (power > bestPower) {
            bestPower = power;
            best = hz;
        }
    }
    return best;
}

/// The power of \a signal between \a fromHz and \a toHz, in dB against a
/// full-scale sine: Hann-windowed frames of 4096, half overlapping, their
/// bins in the band summed and the frames averaged.
[[nodiscard]] float bandDb(std::span<const float> signal, double fromHz, double toHz)
{
    constexpr std::size_t size = 4096;
    RealFft fft;
    fft.prepare(size);
    std::vector<float> frame(size);
    std::vector<std::complex<float>> spectrum(fft.bins());
    double windowPower = 0.0;
    for (std::size_t n = 0; n < size; ++n) {
        const double window = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(n) / size);
        windowPower += window * window;
    }
    const auto first = static_cast<std::size_t>(std::ceil(fromHz * size / kTestSampleRate));
    const auto last = static_cast<std::size_t>(std::floor(toHz * size / kTestSampleRate));
    double power = 0.0;
    std::size_t frames = 0;
    for (std::size_t at = 0; at + size <= signal.size(); at += size / 2) {
        for (std::size_t n = 0; n < size; ++n) {
            const double window = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(n) / size);
            frame[n] = static_cast<float>(window * signal[at + n]);
        }
        fft.forward(frame, spectrum);
        for (std::size_t bin = first; bin <= last; ++bin) {
            power += std::norm(spectrum[bin]);
        }
        ++frames;
    }
    // A bin's power over the window's, both sides of zero: a sine of
    // amplitude A reads A² / 2, which is 0 dB for a full-scale one.
    const double mean = 2.0 * power / (static_cast<double>(frames) * windowPower * size);
    return static_cast<float>(10.0 * std::log10(std::max(mean * 2.0, 1.0e-12)));
}

/// A sustained vowel: a 140 Hz buzz with its harmonics up to 3.5 kHz under
/// two formants, as the declick test's speech has, and nothing above.
[[nodiscard]] Signal vowel(std::size_t frames)
{
    Signal out(frames, 0.0f);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const double t = static_cast<double>(frame) / kTestSampleRate;
        double sample = 0.0;
        for (int harmonic = 1; harmonic * 140.0 < 3500.0; ++harmonic) {
            const double hz = harmonic * 140.0;
            const double formants = std::exp(-std::pow((hz - 700.0) / 300.0, 2.0))
                + 0.5 * std::exp(-std::pow((hz - 1200.0) / 400.0, 2.0)) + 0.05;
            sample += formants / harmonic * std::sin(2.0 * std::numbers::pi * hz * t);
        }
        out[frame] = static_cast<float>(0.3 * sample);
    }
    return out;
}

/// A hiss like an s: white noise with its power from 4 to 9 kHz, over a
/// little of the vowel, far quieter.
[[nodiscard]] Signal fricative(std::size_t frames)
{
    Signal hiss = whiteNoise(frames, 0.5f, 7);
    Biquad high1;
    Biquad high2;
    const BiquadCoefficients high = designBiquad(FilterSpec{FilterShape::HighPass, 4500.0}, kTestSampleRate);
    high1.setCoefficients(high);
    high2.setCoefficients(high);
    Biquad low;
    low.setCoefficients(designBiquad(FilterSpec{FilterShape::LowPass, 9000.0}, kTestSampleRate));
    const Signal under = vowel(frames);
    for (std::size_t n = 0; n < frames; ++n) {
        hiss[n] = low.process(high2.process(high1.process(hiss[n]))) + 0.05f * under[n];
    }
    return hiss;
}

} // namespace

TEST(FrequencyMove, CompressSpreadsTheSourceEvenlyInOctavesOverTheDestination)
{
    const FrequencyMove move = compressHighs();
    EXPECT_NEAR(carriedTo(move, 4000.0), 2500.0, 1e-6);
    EXPECT_NEAR(carriedTo(move, 8000.0), 4000.0, 1e-6);
    // Half the source's one octave is half the destination's octaves.
    EXPECT_NEAR(carriedTo(move, 4000.0 * std::sqrt(2.0)), std::sqrt(2500.0 * 4000.0), 1e-6);
    EXPECT_EQ(carriedTo(move, 3999.0), 0.0);
    EXPECT_EQ(carriedTo(move, 8001.0), 0.0);
}

TEST(FrequencyMove, TransposeCarriesByOneRatioAndLeavesOutWhatLandsAbove)
{
    FrequencyMove move = transposeOctave();
    EXPECT_NEAR(carriedTo(move, 6000.0), 3000.0, 1e-6);
    EXPECT_NEAR(carriedTo(move, 8000.0), 4000.0, 1e-6);
    move.destinationHighHz = 3500.0f;
    EXPECT_EQ(carriedTo(move, 7500.0), 0.0) << "3750 Hz is above the destination";
    EXPECT_NEAR(carriedTo(move, 6000.0), 3000.0, 1e-6);
}

TEST(FrequencyMover, WithNothingToMoveTheSoundComesOutAsItWentInOneFrameLater)
{
    for (const double rate : {44100.0, 48000.0, 96000.0}) {
        FrequencyMover mover;
        mover.prepare(rate);
        EXPECT_GE(static_cast<double>(mover.frameSize()), FrequencyMover::kFrameMs * 1.0e-3 * rate);
        EXPECT_LT(static_cast<double>(mover.frameSize()), 2.0 * FrequencyMover::kFrameMs * 1.0e-3 * rate);
        const Signal input = pinkNoise(kSecond, 0.5f, 3);
        Signal output(input.size(), 0.0f);
        mover.process(input, output);
        const std::size_t latency = mover.latencySamples();
        const std::span<const float> late = std::span<const float>(output).subspan(latency);
        const std::span<const float> early = std::span<const float>(input).first(input.size() - latency);
        EXPECT_LT(nullTestDb(late, early), -90.0f) << "at " << rate << " Hz";
    }
}

TEST(FrequencyMover, ACompressedToneLandsWhereTheMapSaysAndNothingIsLeftWhereItWas)
{
    const FrequencyMove move = compressHighs();
    const Signal input = sine(2 * kSecond, 6000.0, kTestSampleRate, 0.5f);
    const Signal output = moved(input, std::span(&move, 1));

    const double expected = carriedTo(move, 6000.0);
    const double landed = loudestHz(settled(output), 2000.0, 5000.0);
    std::printf(
        "[measure] 6 kHz squeezed from 4-8 kHz into 2.5-4 kHz: at %.0f Hz, the map says %.0f Hz\n", landed, expected);
    EXPECT_NEAR(landed, expected, 0.02 * expected);
    const float left = gainToDb(amplitudeAt(settled(output), 6000.0) / 0.5f);
    std::printf("[measure] left at 6 kHz: %.1f dB\n", left);
    EXPECT_LT(left, -40.0f);
}

TEST(FrequencyMover, ATransposedToneIsCopiedAnOctaveDownAtItsLevelAndTheOriginalIsKept)
{
    for (const float levelDb : {0.0f, -6.0f}) {
        const FrequencyMove move = transposeOctave(levelDb);
        const Signal input = sine(2 * kSecond, 6000.0, kTestSampleRate, 0.5f);
        const Signal output = moved(input, std::span(&move, 1));
        const float copy = gainToDb(amplitudeAt(settled(output), 3000.0) / 0.5f);
        const float original = gainToDb(amplitudeAt(settled(output), 6000.0) / 0.5f);
        std::printf(
            "[measure] 6 kHz an octave down at %.0f dB: %.2f dB at 3 kHz, %.2f dB at 6 kHz\n", levelDb, copy, original);
        EXPECT_NEAR(copy, levelDb, 1.0f);
        EXPECT_NEAR(original, 0.0f, 0.5f);
    }
}

TEST(FrequencyMover, TwoMovesEachCarryTheirOwnSourceIntoTheirOwnDestination)
{
    FrequencyMove high = compressHighs();
    high.sourceLowHz = 6000.0f;
    high.sourceHighHz = 8000.0f;
    high.destinationLowHz = 3000.0f;
    high.destinationHighHz = 4000.0f;
    FrequencyMove middle = compressHighs();
    middle.sourceLowHz = 2000.0f;
    middle.sourceHighHz = 2800.0f;
    middle.destinationLowHz = 1000.0f;
    middle.destinationHighHz = 1400.0f;
    const std::array moves{high, middle};

    Signal input = sine(2 * kSecond, 7000.0, kTestSampleRate, 0.25f);
    const Signal other = sine(2 * kSecond, 2400.0, kTestSampleRate, 0.25f);
    for (std::size_t n = 0; n < input.size(); ++n) {
        input[n] += other[n];
    }
    const Signal output = moved(input, moves);
    const double first = loudestHz(settled(output), 2900.0, 4100.0);
    const double second = loudestHz(settled(output), 900.0, 1500.0);
    std::printf("[measure] 7 kHz and 2.4 kHz by two moves: at %.0f Hz (map %.0f) and %.0f Hz (map %.0f)\n",
                first,
                carriedTo(high, 7000.0),
                second,
                carriedTo(middle, 2400.0));
    EXPECT_NEAR(first, carriedTo(high, 7000.0), 0.02 * carriedTo(high, 7000.0));
    EXPECT_NEAR(second, carriedTo(middle, 2400.0), 0.02 * carriedTo(middle, 2400.0));
    EXPECT_NEAR(gainToDb(amplitudeAt(settled(output), first) / 0.25f), 0.0f, 3.0f);
    EXPECT_NEAR(gainToDb(amplitudeAt(settled(output), second) / 0.25f), 0.0f, 3.0f);
}

TEST(FrequencyMover, AMoveNeverCarriesWhatAnotherMoveCarried)
{
    // The second move's source is where the first one's lands. Reading the
    // input, it finds nothing there: the 7 kHz tone is carried once.
    FrequencyMove high = compressHighs();
    high.sourceLowHz = 6000.0f;
    high.sourceHighHz = 8000.0f;
    high.destinationLowHz = 3000.0f;
    high.destinationHighHz = 4000.0f;
    FrequencyMove next = compressHighs();
    next.sourceLowHz = 3000.0f;
    next.sourceHighHz = 4000.0f;
    next.destinationLowHz = 1500.0f;
    next.destinationHighHz = 2000.0f;
    const std::array moves{high, next};
    const Signal input = sine(2 * kSecond, 7000.0, kTestSampleRate, 0.5f);
    const Signal output = moved(input, moves);
    const double once = carriedTo(high, 7000.0);
    const double twice = carriedTo(next, once);
    const float atOnce = gainToDb(amplitudeAt(settled(output), once) / 0.5f);
    const float atTwice = gainToDb(amplitudeAt(settled(output), twice) / 0.5f);
    std::printf(
        "[measure] carried once to %.0f Hz: %.1f dB; twice, to %.0f Hz: %.1f dB\n", once, atOnce, twice, atTwice);
    EXPECT_GT(atOnce, -3.0f);
    EXPECT_LT(atTwice, -40.0f);
}

TEST(FrequencyMover, WhenNeededLeavesAVowelAsItCameAndActsOnAHiss)
{
    FrequencyMove move = compressHighs();
    move.when = MoveWhen::WhenNeeded;

    const Signal vowelIn = vowel(2 * kSecond);
    FrequencyMover mover;
    mover.prepare(kTestSampleRate);
    mover.setMove(0, move);
    mover.reset();
    Signal vowelOut(vowelIn.size(), 0.0f);
    mover.process(vowelIn, vowelOut);
    const std::size_t latency = mover.latencySamples();
    const float change = nullTestDb(std::span<const float>(vowelOut).subspan(latency),
                                    std::span<const float>(vowelIn).first(vowelIn.size() - latency));
    const float levelChange = rmsDb(settled(vowelOut)) - rmsDb(settled(vowelIn));
    std::printf("[measure] a vowel through \"when needed\": changed by %.1f dB below it, level %+.2f dB\n",
                change - rmsDb(vowelIn),
                levelChange);
    EXPECT_LT(std::abs(levelChange), 0.5f);
    EXPECT_LT(change - rmsDb(vowelIn), -60.0f);
    EXPECT_FALSE(mover.acting(0));

    const Signal hissIn = fricative(2 * kSecond);
    const Signal hissOut = moved(hissIn, std::span(&move, 1));
    const float destinationBefore = bandDb(settled(hissIn), 2600.0, 3800.0);
    const float destinationAfter = bandDb(settled(hissOut), 2600.0, 3800.0);
    const float sourceBefore = bandDb(settled(hissIn), 5000.0, 7500.0);
    const float sourceAfter = bandDb(settled(hissOut), 5000.0, 7500.0);
    std::printf("[measure] a hiss through \"when needed\": 2.6-3.8 kHz %.1f -> %.1f dB, 5-7.5 kHz %.1f -> %.1f dB\n",
                destinationBefore,
                destinationAfter,
                sourceBefore,
                sourceAfter);
    EXPECT_GT(destinationAfter, destinationBefore + 10.0f);
    EXPECT_LT(sourceAfter, sourceBefore - 20.0f);
}

TEST(FrequencyMover, TheSameComesOutWhateverTheCyclesAre)
{
    const FrequencyMove move = compressHighs();
    const Signal input = pinkNoise(kSecond, 0.5f, 5);
    const Signal whole = moved(input, std::span(&move, 1));

    FrequencyMover mover;
    mover.prepare(kTestSampleRate);
    mover.setMove(0, move);
    mover.reset();
    Signal pieces(input.size(), 0.0f);
    std::mt19937 random(9);
    std::uniform_int_distribution<std::size_t> length(1, 1024);
    for (std::size_t at = 0; at < input.size();) {
        const std::size_t count = std::min(length(random), input.size() - at);
        mover.process(std::span<const float>(input).subspan(at, count), std::span<float>(pieces).subspan(at, count));
        at += count;
    }
    EXPECT_EQ(pieces, whole);
}

TEST(FrequencyMover, NothingIsAllocatedWhileItRuns)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "this build cannot see allocations";
    }
    FrequencyMover mover;
    mover.prepare(kTestSampleRate);
    FrequencyMove move = compressHighs();
    move.when = MoveWhen::WhenNeeded;
    mover.setMove(0, move);
    mover.setMove(1, transposeOctave());
    mover.reset();
    const Signal input = fricative(kSecond / 2);
    Signal output(input.size(), 0.0f);
    NoAllocations guard;
    mover.process(input, output);
    FrequencyMove other = move;
    other.destinationLowHz = 2000.0f;
    mover.setMove(0, other);
    mover.process(input, output);
    EXPECT_EQ(guard.count(), 0U);
}

TEST(FrequencyMover, AMoveChangedWhileItPlaysMakesNoClick)
{
    // A tone carried down; then, while it plays, the destination changed,
    // the move switched off, and on again quieter with the source kept.
    const Signal input = sine(3 * kSecond, 6000.0, kTestSampleRate, 0.5f);
    FrequencyMover mover;
    mover.prepare(kTestSampleRate);
    FrequencyMove move = compressHighs();
    mover.setMove(0, move);
    mover.reset();
    Signal output(input.size(), 0.0f);
    const std::span<const float> in(input);
    const std::span<float> out(output);
    const auto run = [&](std::size_t from, std::size_t count) {
        mover.process(in.subspan(from, count), out.subspan(from, count));
    };
    const std::array<std::size_t, 3> changes{kSecond, kSecond * 3 / 2, 2 * kSecond};
    run(0, changes[0]);
    move.destinationLowHz = 2000.0f;
    move.destinationHighHz = 3500.0f;
    mover.setMove(0, move);
    run(changes[0], changes[1] - changes[0]);
    move.active = false;
    mover.setMove(0, move);
    run(changes[1], changes[2] - changes[1]);
    move.active = true;
    move.levelDb = -12.0f;
    move.source = MoveSource::Keep;
    mover.setMove(0, move);
    run(changes[2], input.size() - changes[2]);

    // A click is a step steeper than any sine heard here can take, and
    // sound far above them: nothing heard here is above 6 kHz.
    const float steepest = static_cast<float>(2.0 * std::numbers::pi * 6000.0 / kTestSampleRate * 0.5);
    const std::span<const float> heard(output);
    const auto clicked = [&](std::span<const float> signal, std::size_t at, float &step, float &high) {
        const std::size_t latency = mover.latencySamples();
        step = maxSampleStep(signal.subspan(at + latency - 2048, 8192));
        high = bandDb(signal.subspan(at + latency - 2048, 8192), 10000.0, 20000.0);
        return step > steepest * 1.1f || high > -80.0f;
    };
    for (const std::size_t at : changes) {
        float step = 0.0f;
        float high = 0.0f;
        const bool click = clicked(heard, at, step, high);
        std::printf(
            "[measure] a change at %.2f s: largest step %.3f against %.3f for a clean tone, %.1f dB above "
            "10 kHz\n",
            static_cast<double>(at) / kTestSampleRate,
            step,
            steepest,
            high);
        EXPECT_FALSE(click);
    }

    // The same measure finds a click where there is one: the clean tone cut
    // off dead.
    Signal cut = sine(kSecond, 6000.0, kTestSampleRate, 0.5f);
    std::fill(cut.begin() + kSecond / 2 + 3, cut.end(), 0.0f);
    float step = 0.0f;
    float high = 0.0f;
    EXPECT_TRUE(clicked(cut, kSecond / 2 - mover.latencySamples(), step, high));
    std::printf("[measure] a tone cut off dead: largest step %.3f, %.1f dB above 10 kHz\n", step, high);
}

namespace
{

/// The first move Frequency Raising has to make: 250–500 Hz copied two
/// octaves up into 1–2 kHz, the source kept.
[[nodiscard]] FrequencyMove transposeLowsUp(float levelDb = 0.0f)
{
    FrequencyMove move;
    move.active = true;
    move.method = MoveMethod::Transpose;
    move.sourceLowHz = 250.0f;
    move.sourceHighHz = 500.0f;
    move.destinationLowHz = 1000.0f;
    move.destinationHighHz = 2000.0f;
    move.levelDb = levelDb;
    move.source = MoveSource::Keep;
    return move;
}

/// The second move Frequency Raising has to make: a low voice's
/// fundamentals, 80–160 Hz, given their harmonics in 150–450 Hz.
[[nodiscard]] FrequencyMove harmonicsOfLows(float levelDb = 0.0f)
{
    FrequencyMove move;
    move.active = true;
    move.method = MoveMethod::Harmonics;
    move.sourceLowHz = 80.0f;
    move.sourceHighHz = 160.0f;
    move.destinationLowHz = 150.0f;
    move.destinationHighHz = 450.0f;
    move.levelDb = levelDb;
    move.source = MoveSource::Keep;
    return move;
}

} // namespace

TEST(FrequencyMove, ARaisingIsAMoveWhoseDestinationReachesAboveItsSource)
{
    EXPECT_TRUE(transposeLowsUp().raises());
    EXPECT_TRUE(harmonicsOfLows().raises());
    EXPECT_FALSE(compressHighs().raises());
    EXPECT_FALSE(transposeOctave().raises());
}

TEST(FrequencyMove, TransposeCarriesUpByOneRatioToo)
{
    const FrequencyMove move = transposeLowsUp();
    EXPECT_NEAR(carriedTo(move, 300.0), 1200.0, 1e-6);
    EXPECT_NEAR(carriedTo(move, 500.0), 2000.0, 1e-6);
    EXPECT_EQ(carriedTo(move, 200.0), 0.0);
}

TEST(FrequencyMove, HarmonicsLandOnTheLowestHarmonicInsideTheDestination)
{
    const FrequencyMove move = harmonicsOfLows();
    EXPECT_NEAR(carriedTo(move, 100.0), 200.0, 1e-6);
    // 150 Hz's second harmonic, 300, is in; so is 80 Hz's second, 160.
    EXPECT_NEAR(carriedTo(move, 150.0), 300.0, 1e-6);
    EXPECT_NEAR(carriedTo(move, 80.0), 160.0, 1e-6);
    // Outside the source, nothing.
    EXPECT_EQ(carriedTo(move, 200.0), 0.0);
    // No harmonic of 100 Hz inside 510–590 Hz.
    FrequencyMove narrow = move;
    narrow.destinationLowHz = 510.0f;
    narrow.destinationHighHz = 590.0f;
    EXPECT_EQ(carriedTo(narrow, 100.0), 0.0);
}

TEST(FrequencyMover, LowFramesAreLongEnoughToTellAHundredHertzFromItsHarmonics)
{
    FrequencyMover mover;
    mover.prepare(48000.0, FrequencyMover::kLowFrameMs);
    EXPECT_EQ(mover.frameSize(), 4096U);
    EXPECT_EQ(mover.latencySamples(), 4096U);
    mover.prepare(44100.0, FrequencyMover::kLowFrameMs);
    EXPECT_EQ(mover.frameSize(), 4096U);
    mover.prepare(96000.0, FrequencyMover::kLowFrameMs);
    EXPECT_EQ(mover.frameSize(), 8192U);
    // The high ones are what they were.
    mover.prepare(48000.0);
    EXPECT_EQ(mover.frameSize(), 1024U);
}

TEST(FrequencyMover, ATransposedLowToneIsCopiedTwoOctavesUpAtItsLevelAndTheOriginalIsKept)
{
    for (const float levelDb : {0.0f, -6.0f}) {
        const FrequencyMove move = transposeLowsUp(levelDb);
        const Signal input = sine(3 * kSecond, 300.0, kTestSampleRate, 0.5f);
        const Signal output = moved(input, std::span(&move, 1), kTestSampleRate, FrequencyMover::kLowFrameMs);
        const float copy = gainToDb(amplitudeAt(settled(output), 1200.0) / 0.5f);
        const float original = gainToDb(amplitudeAt(settled(output), 300.0) / 0.5f);
        std::printf("[measure] 300 Hz copied two octaves up at %+.0f dB: %+.2f dB at 1.2 kHz, %+.2f dB at 300 Hz\n",
                    levelDb,
                    copy,
                    original);
        EXPECT_NEAR(copy, levelDb, 1.0f);
        EXPECT_NEAR(original, 0.0f, 0.5f);
    }
}

TEST(FrequencyMover, ALowToneIsGivenItsHarmonicsInTheDestinationAtTheSetLevelAndNothingBelowIt)
{
    for (const float levelDb : {0.0f, -6.0f}) {
        const FrequencyMove move = harmonicsOfLows(levelDb);
        FrequencyMover mover;
        mover.prepare(kTestSampleRate, FrequencyMover::kLowFrameMs);
        mover.setMove(0, move);
        mover.setMovedOnly(true);
        mover.reset();
        const Signal input = sine(3 * kSecond, 100.0, kTestSampleRate, 0.5f);
        Signal output(input.size(), 0.0f);
        mover.process(input, output);

        for (const double hz : {200.0, 300.0, 400.0}) {
            const float at = gainToDb(amplitudeAt(settled(output), hz) / 0.5f);
            std::printf("[measure] 100 Hz's harmonic at %.0f Hz, set to %+.0f dB: %+.2f dB\n", hz, levelDb, at);
            EXPECT_NEAR(at, levelDb, 1.0f) << hz << " Hz";
        }
        // Nothing added below the destination, and no fifth: 500 Hz is
        // above it. **Against three clean tones** at the same levels, read
        // the same way: the reading's own window hears a little of a tone
        // 60 Hz away, and that is not the mover's.
        Signal clean(input.size(), 0.0f);
        for (const double hz : {200.0, 300.0, 400.0}) {
            const Signal tone = sine(input.size(), hz, kTestSampleRate, 0.5f * dbToGain(levelDb));
            for (std::size_t n = 0; n < clean.size(); ++n) {
                clean[n] += tone[n];
            }
        }
        const float below = bandDb(settled(output), 20.0, 140.0);
        const float belowClean = bandDb(settled(clean), 20.0, 140.0);
        const float fifth = gainToDb(amplitudeAt(settled(output), 500.0) / 0.5f);
        std::printf(
            "[measure] harmonics of 100 Hz: %.1f dB below 140 Hz (three clean tones read %.1f), %.1f dB at "
            "500 Hz\n",
            below,
            belowClean,
            fifth);
        EXPECT_LT(below, belowClean + 1.0f);
        EXPECT_LT(fifth, -60.0f);
    }
}

TEST(FrequencyMover, WhenNeededRaisesALowToneAloneAndLeavesItUnderAHiss)
{
    FrequencyMove move = transposeLowsUp();
    move.when = MoveWhen::WhenNeeded;

    // A hum on its own is what the low range is for: raised.
    FrequencyMover mover;
    mover.prepare(kTestSampleRate, FrequencyMover::kLowFrameMs);
    mover.setMove(0, move);
    mover.reset();
    const Signal hum = sine(2 * kSecond, 300.0, kTestSampleRate, 0.5f);
    Signal humOut(hum.size(), 0.0f);
    mover.process(hum, humOut);
    EXPECT_TRUE(mover.acting(0)) << "a low tone alone";
    EXPECT_NEAR(gainToDb(amplitudeAt(settled(humOut), 1200.0) / 0.5f), 0.0f, 1.0f);

    // The same tone, quiet, under a loud hiss: most of the sound is above,
    // so nothing is carried up.
    Signal mixed = fricative(2 * kSecond);
    const Signal quiet = sine(2 * kSecond, 300.0, kTestSampleRate, 0.05f);
    for (std::size_t n = 0; n < mixed.size(); ++n) {
        mixed[n] += quiet[n];
    }
    mover.reset();
    Signal mixedOut(mixed.size(), 0.0f);
    mover.process(mixed, mixedOut);
    const float added = bandDb(settled(mixedOut), 1150.0, 1250.0) - bandDb(settled(mixed), 1150.0, 1250.0);
    std::printf("[measure] a quiet 300 Hz under a hiss through \"when needed\": 1.2 kHz changed by %+.2f dB\n", added);
    EXPECT_FALSE(mover.acting(0)) << "a low tone under a hiss";
    EXPECT_LT(std::abs(added), 0.5f);
}
