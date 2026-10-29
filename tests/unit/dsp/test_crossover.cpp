// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The three-band crossover under the Multiband Compressor.

#include "analysis.h"
#include "dsp/biquad.h"
#include "dsp/crossover.h"
#include "dsp/decibel.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <vector>

using namespace rb::dsp;
using namespace rb::test;

namespace
{

/// A signal split, one channel, and the three bands kept.
struct Bands {
    Signal low;
    Signal mid;
    Signal high;

    [[nodiscard]] Signal sum() const
    {
        Signal out(low.size());
        for (std::size_t i = 0; i < out.size(); ++i) {
            out[i] = low[i] + mid[i] + high[i];
        }
        return out;
    }
};

/// \a input through a crossover split at \a lowHz and \a highHz, in cycles of
/// \a cycle frames, with \a moveAt (if any) the frame at which the splits are
/// sent to \a movedLowHz and \a movedHighHz.
[[nodiscard]] Bands split(const Signal &input,
                          double lowHz,
                          double highHz,
                          double rate = kTestSampleRate,
                          std::size_t moveAt = 0,
                          double movedLowHz = 0.0,
                          double movedHighHz = 0.0)
{
    ThreeBandCrossover crossover;
    crossover.setSplits(lowHz, highHz);
    crossover.prepare(rate, 1);

    Bands bands{Signal(input.size()), Signal(input.size()), Signal(input.size())};
    Signal scratch = input;
    constexpr std::size_t kCycle = 256;
    for (std::size_t at = 0; at < input.size(); at += kCycle) {
        if (moveAt != 0 && at == moveAt) {
            crossover.setSplits(movedLowHz, movedHighHz);
        }
        const auto frames = static_cast<std::uint32_t>(std::min(kCycle, input.size() - at));
        float *in = scratch.data() + at;
        float *low = bands.low.data() + at;
        float *mid = bands.mid.data() + at;
        float *high = bands.high.data() + at;
        crossover.process(AudioBlock(&in, 1, frames),
                          AudioBlock(&low, 1, frames),
                          AudioBlock(&mid, 1, frames),
                          AudioBlock(&high, 1, frames),
                          frames);
    }
    return bands;
}

/// The last second of \a signal at \a rate: a whole number of cycles of any
/// whole number of hertz, so a level measured over it has no leakage.
[[nodiscard]] Signal lastSecond(const Signal &signal, double rate)
{
    const auto second = static_cast<std::size_t>(rate);
    return Signal(signal.end() - static_cast<Signal::difference_type>(second), signal.end());
}

[[nodiscard]] double levelDb(const Signal &signal, double hz, double rate)
{
    return gainToDb(amplitudeAt(lastSecond(signal, rate), hz, rate));
}

} // namespace

TEST(CrossoverTest, TheThreeBandsAddUpFlatAtEveryFrequency)
{
    // The acceptance criterion: with nothing done to the bands, what comes
    // out is what went in, within ±0.1 dB. It is exact in design; this
    // holds the running filters to it, at every rate, across the splits
    // and away from them — and with the splits as close as they may come,
    // where a low band left out of step with the other two would dip most.
    struct Splits {
        double low;
        double high;
    };
    for (const Splits splits : {Splits{200.0, 3000.0}, Splits{1000.0, 1500.0}}) {
        for (const double rate : {44100.0, 48000.0, 96000.0}) {
            double worst = 0.0;
            for (const double hz : {25.0,
                                    60.0,
                                    120.0,
                                    199.0,
                                    200.0,
                                    201.0,
                                    400.0,
                                    1000.0,
                                    1224.0,
                                    1500.0,
                                    2000.0,
                                    2999.0,
                                    3000.0,
                                    3001.0,
                                    5000.0,
                                    9000.0,
                                    15000.0,
                                    19000.0}) {
                const auto frames = static_cast<std::size_t>(rate * 2);
                const Signal input = sine(frames, hz, rate, 0.5f);
                const Bands bands = split(input, splits.low, splits.high, rate);
                const double gain = levelDb(bands.sum(), hz, rate) - levelDb(input, hz, rate);
                worst = std::max(worst, std::abs(gain));
                EXPECT_NEAR(gain, 0.0, 0.1)
                    << hz << " Hz at " << rate << ", split at " << splits.low << "/" << splits.high;
            }
            std::printf("[measure] crossover %.0f/%.0f Hz at %.0f Hz: the bands add up within %.4f dB\n",
                        splits.low,
                        splits.high,
                        rate,
                        worst);
        }
    }
}

TEST(CrossoverTest, TheSumIsTheTwoAllPassesOneAfterTheOther)
{
    // Not just flat in level: the sum is exactly the input turned by the
    // lower split's all-pass and then the upper one's, sample for sample.
    const Signal input = whiteNoise(48000, 0.5f, 7);
    const Bands bands = split(input, 150.0, 2500.0);

    Biquad lower;
    Biquad upper;
    lower.setCoefficients(designBiquad({FilterShape::AllPass, 150.0, kButterworthQ, 0.0}, kTestSampleRate));
    upper.setCoefficients(designBiquad({FilterShape::AllPass, 2500.0, kButterworthQ, 0.0}, kTestSampleRate));
    Signal expected(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        expected[i] = upper.process(lower.process(input[i]));
    }
    const float residual = nullTestDb(bands.sum(), expected);
    std::printf("[measure] crossover sum against the two all-passes: %.1f dB\n", residual);
    EXPECT_LT(residual, -90.0f);
}

TEST(CrossoverTest, EachBandKeepsItsOwnRange)
{
    // A tone well inside each band is all in that band; at a split, both
    // bands either side carry it at half its level, as Linkwitz–Riley does.
    constexpr double kRate = kTestSampleRate;
    const auto frames = static_cast<std::size_t>(kRate * 2);
    struct Case {
        double hz;
        int band;
    };
    for (const Case tone : {Case{40.0, 0}, Case{775.0, 1}, Case{14000.0, 2}}) {
        const Signal input = sine(frames, tone.hz, kRate, 0.5f);
        const Bands bands = split(input, 200.0, 3000.0);
        const std::array<double, 3> levels{levelDb(bands.low, tone.hz, kRate),
                                           levelDb(bands.mid, tone.hz, kRate),
                                           levelDb(bands.high, tone.hz, kRate)};
        const double own = levelDb(input, tone.hz, kRate);
        for (int band = 0; band < 3; ++band) {
            if (band == tone.band) {
                EXPECT_NEAR(levels[band], own, 0.1) << tone.hz << " Hz in its own band";
            } else {
                EXPECT_LT(levels[band], own - 40.0) << tone.hz << " Hz in band " << band;
            }
        }
    }
    for (const double at : {200.0, 3000.0}) {
        const Signal input = sine(frames, at, kRate, 0.5f);
        const Bands bands = split(input, 200.0, 3000.0);
        const double own = levelDb(input, at, kRate);
        const Signal &below = at == 200.0 ? bands.low : bands.mid;
        const Signal &above = at == 200.0 ? bands.mid : bands.high;
        EXPECT_NEAR(levelDb(below, at, kRate) - own, -6.02, 0.1) << "below the split at " << at;
        EXPECT_NEAR(levelDb(above, at, kRate) - own, -6.02, 0.1) << "above the split at " << at;
    }
}

TEST(CrossoverTest, AMovedSplitGlidesThereWithoutAClick)
{
    // A tone in the middle band while both splits are dragged past it, so
    // it ends up in the low band: the bands still add up to a tone, with no
    // step in it bigger than the tone's own, and the splits get where they
    // were sent.
    constexpr double kHz = 1000.0;
    constexpr float kAmplitude = 0.5f;
    const Signal input = sine(48000, kHz, kTestSampleRate, kAmplitude);
    const Bands bands = split(input, 200.0, 3000.0, kTestSampleRate, 24064, 4000.0, 9000.0);
    const Signal sum = bands.sum();

    // **A glide bends the tone a little, a jump breaks it.** While the
    // all-passes turn, the tone's phase moves with them, which reads as a
    // few hertz more for 30 ms: 1.06 times its own step. The same move made
    // at once put a 0.276 step in it, 4.2 times its own.
    const float ownStep = maxSampleStep(input);
    const float step = maxSampleStep(Signal(sum.begin() + 4800, sum.end()));
    std::printf(
        "[measure] crossover moved past a tone: largest step %.4f against the tone's own %.4f\n", step, ownStep);
    EXPECT_LT(step, ownStep * 1.25f);

    // And afterwards the tone is in the low band, whole: two octaves under
    // the lower split, where a fourth-order low half has it all.
    const Signal tail(bands.low.end() - 9600, bands.low.end());
    EXPECT_NEAR(gainToDb(amplitudeAt(tail, kHz)), gainToDb(kAmplitude), 0.1);
}

TEST(CrossoverTest, TheSplitsGetWhereTheyAreSentAndAreHeldApart)
{
    ThreeBandCrossover crossover;
    crossover.setSplits(300.0, 4000.0);
    crossover.prepare(kTestSampleRate, 2);
    EXPECT_NEAR(crossover.splits().lowHz, 300.0, 1e-6);
    EXPECT_NEAR(crossover.splits().highHz, 4000.0, 1e-6);

    // Crossed or too close: held kMinCrossoverSpacing apart.
    const CrossoverSplits crossed = clampCrossoverSplits(5000.0, 1000.0, kTestSampleRate);
    EXPECT_NEAR(crossed.highHz / crossed.lowHz, kMinCrossoverSpacing, 1e-9);
    EXPECT_NEAR(crossed.lowHz, 5000.0, 1e-9);
    // Past what the rate allows: both inside it, still apart.
    const CrossoverSplits tooHigh = clampCrossoverSplits(30000.0, 40000.0, kTestSampleRate);
    EXPECT_LE(tooHigh.highHz, kTestSampleRate * 0.45);
    EXPECT_NEAR(tooHigh.highHz / tooHigh.lowHz, kMinCrossoverSpacing, 1e-9);
}
