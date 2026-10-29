// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Second-order filters: the design against the numbers every textbook
// gives for it, and both ways of running it measured against the design.

#include "dsp/biquad.h"

#include <gtest/gtest.h>

#include <cmath>
#include <complex>
#include <functional>
#include <numbers>
#include <random>
#include <string>
#include <vector>

using namespace rb::dsp;

namespace
{

constexpr double kRate = 48000.0;
/// How long an impulse response is kept, in seconds: long enough for the
/// slowest filter here (40 Hz at a Q of 2.5) to have rung down by 150 dB, so
/// what is cut off is below what a float can say.
constexpr double kImpulseSeconds = 0.4;

/// How far a measured gain may be from the design's, in dB. Both ways of
/// running a filter are the same arithmetic as the design, so this is
/// rounding and a truncated impulse response, nothing more.
constexpr double kToleranceDb = 0.01;
/// Below this the design's gain is not compared: a float output cannot say
/// −140 dB from −150, and a filter that far down is doing its job.
constexpr double kFloorDb = -80.0;

/// What a filter does to one impulse.
[[nodiscard]] std::vector<float> impulseResponse(const std::function<float(float)> &filter, double rate = kRate)
{
    std::vector<float> response(static_cast<std::size_t>(rate * kImpulseSeconds));
    for (std::size_t n = 0; n < response.size(); ++n) {
        response[n] = filter(n == 0 ? 1.0f : 0.0f);
    }
    return response;
}

/// The gain an impulse response gives a sine at \a frequency, in dB: its
/// Fourier transform at that one frequency.
[[nodiscard]] double measuredDb(const std::vector<float> &response, double frequency, double rate = kRate)
{
    // The phasor turned a step at a time rather than worked out afresh for
    // every sample, which keeps a test of a few hundred filters quick.
    const std::complex<double> step = std::polar(1.0, -2.0 * std::numbers::pi * frequency / rate);
    std::complex<double> phasor = 1.0;
    std::complex<double> sum = 0.0;
    for (const float sample : response) {
        sum += static_cast<double>(sample) * phasor;
        phasor *= step;
    }
    return 20.0 * std::log10(std::max(std::abs(sum), 1.0e-15));
}

/// Frequencies from 10 Hz to just under half the rate, half an octave
/// apart, and the one the filter is set to.
[[nodiscard]] std::vector<double> probeFrequencies(double rate, double at)
{
    std::vector<double> frequencies{at};
    for (double f = 10.0; f < rate * 0.49; f *= std::numbers::sqrt2) {
        frequencies.push_back(f);
    }
    return frequencies;
}

[[nodiscard]] std::string describe(const FilterSpec &spec)
{
    return "shape " + std::to_string(static_cast<int>(spec.shape)) + " at " + std::to_string(spec.frequency) + " Hz, Q "
        + std::to_string(spec.q) + ", " + std::to_string(spec.gainDb) + " dB";
}

/// Every shape, low and high and in the middle, with and without a gain.
[[nodiscard]] std::vector<FilterSpec> everyKindOfFilter()
{
    std::vector<FilterSpec> specs;
    for (const FilterShape shape : {FilterShape::LowPass,
                                    FilterShape::HighPass,
                                    FilterShape::Peaking,
                                    FilterShape::LowShelf,
                                    FilterShape::HighShelf,
                                    FilterShape::Notch,
                                    FilterShape::BandPass,
                                    FilterShape::AllPass}) {
        for (const double frequency : {40.0, 1000.0, 15000.0}) {
            for (const double q : {0.5, kButterworthQ, 2.5}) {
                // A pass filter, a notch and a band-pass have no gain to vary.
                const bool hasGain =
                    shape == FilterShape::Peaking || shape == FilterShape::LowShelf || shape == FilterShape::HighShelf;
                for (const double gainDb : hasGain ? std::vector<double>{-12.0, 6.0} : std::vector<double>{0.0}) {
                    specs.push_back(FilterSpec{shape, frequency, q, gainDb});
                }
            }
        }
    }
    return specs;
}

} // namespace

TEST(BiquadTest, ALowPassIsFlatBelowThreeDecibelsDownAtItsFrequencyAndFallsAtTwelvePerOctave)
{
    const FilterSpec spec{FilterShape::LowPass, 100.0, kButterworthQ, 0.0};
    EXPECT_NEAR(filterMagnitudeDb(spec, 1.0, kRate), 0.0, 1e-6);
    EXPECT_NEAR(filterMagnitudeDb(spec, 100.0, kRate), -3.0103, 1e-3);
    // An octave apart well above the corner and well below half the rate,
    // where the warping adds a little more.
    const double drop = filterMagnitudeDb(spec, 1600.0, kRate) - filterMagnitudeDb(spec, 3200.0, kRate);
    EXPECT_NEAR(drop, 12.0, 0.5);
    // And nothing at all at half the rate: the bilinear transform puts a
    // zero there.
    EXPECT_LT(filterMagnitudeDb(spec, kRate / 2.0, kRate), -200.0);
}

TEST(BiquadTest, TwoButterworthSectionsMakeAFourthOrderOne)
{
    // Three decibels down at the corner, and 24 dB an octave above it.
    const FilterSpec first{FilterShape::LowPass, 200.0, kButterworth4Q1, 0.0};
    const FilterSpec second{FilterShape::LowPass, 200.0, kButterworth4Q2, 0.0};
    const auto both = [&](double f) {
        return filterMagnitudeDb(first, f, kRate) + filterMagnitudeDb(second, f, kRate);
    };
    EXPECT_NEAR(both(200.0), -3.0103, 1e-3);
    EXPECT_NEAR(both(2.0), 0.0, 1e-6);
    // Flat: no bump anywhere in the pass band.
    for (double f = 2.0; f < 200.0; f *= 1.1) {
        EXPECT_LE(both(f), 1e-9) << f;
    }
    EXPECT_NEAR(both(1600.0) - both(3200.0), 24.0, 1.0);
}

TEST(BiquadTest, APeakAndAShelfLiftByWhatTheyAreAskedTo)
{
    EXPECT_NEAR(filterMagnitudeDb({FilterShape::Peaking, 1000.0, 1.41, 9.0}, 1000.0, kRate), 9.0, 1e-9);
    EXPECT_NEAR(filterMagnitudeDb({FilterShape::Peaking, 1000.0, 1.41, -9.0}, 1000.0, kRate), -9.0, 1e-9);
    EXPECT_NEAR(filterMagnitudeDb({FilterShape::Peaking, 1000.0, 1.41, 9.0}, 20.0, kRate), 0.0, 0.01);

    // A shelf: its gain at the far end, half of it at the frequency.
    EXPECT_NEAR(filterMagnitudeDb({FilterShape::LowShelf, 200.0, kButterworthQ, 8.0}, 1.0, kRate), 8.0, 1e-3);
    EXPECT_NEAR(filterMagnitudeDb({FilterShape::LowShelf, 200.0, kButterworthQ, 8.0}, 200.0, kRate), 4.0, 1e-9);
    EXPECT_NEAR(filterMagnitudeDb({FilterShape::LowShelf, 200.0, kButterworthQ, 8.0}, 15000.0, kRate), 0.0, 0.01);
    EXPECT_NEAR(
        filterMagnitudeDb({FilterShape::HighShelf, 4000.0, kButterworthQ, -6.0}, kRate / 2.0, kRate), -6.0, 1e-6);
    EXPECT_NEAR(filterMagnitudeDb({FilterShape::HighShelf, 4000.0, kButterworthQ, -6.0}, 4000.0, kRate), -3.0, 1e-9);
    EXPECT_NEAR(filterMagnitudeDb({FilterShape::HighShelf, 4000.0, kButterworthQ, -6.0}, 50.0, kRate), 0.0, 0.01);
}

TEST(BiquadTest, ANotchTakesOutItsFrequencyAndABandPassKeepsIt)
{
    // Nothing at all at the notch's centre, and the band-pass exactly 0 dB
    // at its own whatever the Q; both leave the far ends where they were
    // (the notch) or take them away (the band-pass).
    for (const double q : {0.5, 2.0, 10.0}) {
        EXPECT_LT(filterMagnitudeDb({FilterShape::Notch, 1000.0, q, 0.0}, 1000.0, kRate), -200.0) << q;
        EXPECT_NEAR(filterMagnitudeDb({FilterShape::Notch, 1000.0, q, 0.0}, 20.0, kRate), 0.0, 0.01) << q;
        EXPECT_NEAR(filterMagnitudeDb({FilterShape::BandPass, 1000.0, q, 0.0}, 1000.0, kRate), 0.0, 1e-9) << q;
        EXPECT_LT(filterMagnitudeDb({FilterShape::BandPass, 1000.0, q, 0.0}, 20.0, kRate), -20.0) << q;
    }
    // A narrower notch takes out less either side.
    EXPECT_GT(filterMagnitudeDb({FilterShape::Notch, 1000.0, 10.0, 0.0}, 1100.0, kRate),
              filterMagnitudeDb({FilterShape::Notch, 1000.0, 1.0, 0.0}, 1100.0, kRate));
}

TEST(BiquadTest, AnAllPassKeepsEveryFrequencyAndIsWhatALinkwitzRileyPairAddsUpTo)
{
    // 0 dB everywhere, whatever the Q.
    for (const double q : {0.5, kButterworthQ, 2.0}) {
        for (const double hz : {20.0, 250.0, 1000.0, 4000.0, 20000.0}) {
            EXPECT_NEAR(filterMagnitudeDb({FilterShape::AllPass, 1000.0, q, 0.0}, hz, kRate), 0.0, 1e-9)
                << q << " at " << hz;
        }
    }
    // A fourth-order Linkwitz–Riley crossover is two Butterworth sections a
    // side; its low and high halves added together are this all-pass at the
    // Butterworth Q, in gain and in phase, which is what lets a third band
    // be kept in step with the two a split made.
    const double frequency = 800.0;
    const BiquadCoefficients low = designBiquad({FilterShape::LowPass, frequency, kButterworthQ, 0.0}, kRate);
    const BiquadCoefficients high = designBiquad({FilterShape::HighPass, frequency, kButterworthQ, 0.0}, kRate);
    const BiquadCoefficients allPass = designBiquad({FilterShape::AllPass, frequency, kButterworthQ, 0.0}, kRate);
    for (const double hz : {20.0, 200.0, 800.0, 3000.0, 18000.0}) {
        const std::complex<double> lowSide = biquadResponse(low, hz, kRate) * biquadResponse(low, hz, kRate);
        const std::complex<double> highSide = biquadResponse(high, hz, kRate) * biquadResponse(high, hz, kRate);
        const std::complex<double> sum = lowSide + highSide;
        const std::complex<double> expected = biquadResponse(allPass, hz, kRate);
        EXPECT_NEAR(std::abs(sum - expected), 0.0, 1e-9) << "at " << hz << " Hz";
    }
}

TEST(BiquadTest, TheDirectFormGivesWhatTheDesignSays)
{
    for (const FilterSpec &spec : everyKindOfFilter()) {
        const BiquadCoefficients coefficients = designBiquad(spec, kRate);
        Biquad filter;
        filter.setCoefficients(coefficients);
        const std::vector<float> response = impulseResponse([&](float x) { return filter.process(x); });
        for (const double f : probeFrequencies(kRate, spec.frequency)) {
            const double expected = biquadMagnitudeDb(coefficients, f, kRate);
            if (expected < kFloorDb) {
                continue;
            }
            EXPECT_NEAR(measuredDb(response, f), expected, kToleranceDb) << describe(spec) << ", at " << f << " Hz";
        }
    }
}

TEST(BiquadTest, TheStateVariableFilterGivesWhatTheDesignSaysForEveryShape)
{
    // **The same filter, run another way.** If the state-variable form's
    // mix of outputs were wrong for a shape, the curve on screen and the
    // sound would disagree; this is what holds them together.
    for (const double rate : {44100.0, 48000.0, 96000.0}) {
        for (const FilterSpec &spec : everyKindOfFilter()) {
            const SvfCoefficients coefficients = designSvf(spec, rate);
            Svf filter;
            const std::vector<float> response =
                impulseResponse([&](float x) { return filter.process(x, coefficients); }, rate);
            for (const double f : probeFrequencies(rate, spec.frequency)) {
                const double expected = filterMagnitudeDb(spec, f, rate);
                if (expected < kFloorDb) {
                    continue;
                }
                EXPECT_NEAR(measuredDb(response, f, rate), expected, kToleranceDb)
                    << describe(spec) << ", at " << f << " Hz, " << rate << " Hz rate";
            }
        }
    }
}

TEST(BiquadTest, AFrequencyPastWhatTheRateAllowsIsHeldUnderIt)
{
    // Half the rate or more would put the warped frequency at infinity.
    for (const double asked : {kRate * 0.5, kRate, 1.0e9}) {
        const FilterSpec spec{FilterShape::LowPass, asked, kButterworthQ, 0.0};
        const SvfCoefficients svf = designSvf(spec, kRate);
        const BiquadCoefficients biquad = designBiquad(spec, kRate);
        EXPECT_TRUE(std::isfinite(svf.a1) && std::isfinite(svf.a2) && std::isfinite(svf.a3)) << asked;
        EXPECT_TRUE(std::isfinite(biquad.b0) && std::isfinite(biquad.a1) && std::isfinite(biquad.a2)) << asked;
        EXPECT_NEAR(biquadMagnitudeDb(biquad, 1000.0, kRate), 0.0, 0.01) << asked;
    }
    EXPECT_DOUBLE_EQ(clampFilterFrequency(-5.0, kRate), kMinFrequencyHz);
}

TEST(BiquadTest, TheStateVariableFilterStaysStableWhileItsFrequencyIsThrownAbout)
{
    // **This is what it is for.** A cutoff flung between 20 Hz and 20 kHz
    // every few samples, at a high Q, with full-scale noise going in: the
    // output stays bounded and comes back to silence when the input does.
    std::mt19937 random(69);
    std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
    std::uniform_real_distribution<double> octaves(std::log2(20.0), std::log2(20000.0));
    Svf filter;
    SvfCoefficients coefficients;
    float loudest = 0.0f;
    for (int n = 0; n < 96000; ++n) {
        if (n % 3 == 0) {
            coefficients = designSvf({FilterShape::LowPass, std::exp2(octaves(random)), 4.0, 0.0}, kRate);
        }
        loudest = std::max(loudest, std::abs(filter.process(noise(random), coefficients)));
    }
    // A Q of 4 can lift a band by 12 dB; this is well past that, and far
    // short of what a filter running away would reach.
    EXPECT_LT(loudest, 16.0f);
    float tail = 0.0f;
    for (int n = 0; n < 48000; ++n) {
        tail = std::abs(filter.process(0.0f, coefficients));
    }
    EXPECT_LT(tail, 1e-6f) << "the filter was still ringing a second after the input stopped";
}

TEST(BiquadTest, ResetForgetsTheSignal)
{
    const FilterSpec spec{FilterShape::LowPass, 100.0, kButterworthQ, 0.0};
    const SvfCoefficients svfCoefficients = designSvf(spec, kRate);
    Svf svf;
    Biquad biquad;
    biquad.setCoefficients(designBiquad(spec, kRate));
    for (int n = 0; n < 100; ++n) {
        (void)svf.process(1.0f, svfCoefficients);
        (void)biquad.process(1.0f);
    }
    svf.reset();
    biquad.reset();
    EXPECT_EQ(svf.process(0.0f, svfCoefficients), 0.0f);
    EXPECT_EQ(biquad.process(0.0f), 0.0f);
}
