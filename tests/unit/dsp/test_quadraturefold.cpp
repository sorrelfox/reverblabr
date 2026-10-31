// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The two all-pass chains: their coefficients pinned, so nobody retunes them
// by accident, and what the fold does to a tone across the band at each of
// the rates a session is likely to run at.

#include "analysis.h"
#include "dsp/decibel.h"
#include "dsp/quadraturefold.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

using namespace rb::dsp;
using namespace rb::test;

namespace
{

constexpr std::array kRates{44100.0, 48000.0, 96000.0, 192000.0};
constexpr double kTwoPi = 2.0 * std::numbers::pi;

/// A log grid from 100 Hz to 16 kHz, a third of an octave apart and the ends.
[[nodiscard]] std::vector<double> band()
{
    std::vector<double> hz;
    for (double at = 100.0; at < 16000.0; at *= std::pow(2.0, 1.0 / 3.0)) {
        hz.push_back(at);
    }
    hz.push_back(16000.0);
    return hz;
}

/// The level of the fold of a tone, in dB against the tone. The right side
/// is the left at \a rightGain, \a rightPhase radians ahead of it.
[[nodiscard]] double foldLevel(double rate, double hz, double leftGain, double rightGain, double rightPhase)
{
    QuadratureFold fold;
    fold.prepare(rate);
    // Long enough for the low corners to settle, then whole cycles.
    const auto settle = static_cast<std::size_t>(0.3 * rate);
    const auto cycles = static_cast<std::size_t>(std::ceil(0.05 * hz)) + 1;
    const auto measured = static_cast<std::size_t>(std::llround(static_cast<double>(cycles) * rate / hz));
    double energy = 0.0;
    for (std::size_t frame = 0; frame < settle + measured; ++frame) {
        const double phase = kTwoPi * hz * static_cast<double>(frame) / rate;
        const auto left = static_cast<float>(leftGain * std::sin(phase));
        const auto right = static_cast<float>(rightGain * std::sin(phase + rightPhase));
        const QuadratureFold::Frame out = fold.process(left, right);
        const double y = QuadratureFold::kFoldGain * (static_cast<double>(out.a) + static_cast<double>(out.b));
        if (frame >= settle) {
            energy += y * y;
        }
    }
    return 10.0 * std::log10(2.0 * energy / static_cast<double>(measured));
}

/// A chain's phase at \a hz, in radians, unwrapped by adding its sections.
[[nodiscard]] double chainPhase(const std::array<double, QuadratureFold::kSections> &chain, double rate, double hz)
{
    const double w = kTwoPi * hz / rate;
    const std::complex<double> z1 = std::polar(1.0, -w);
    double phase = 0.0;
    for (const double a : chain) {
        phase += std::arg((a + z1) / (1.0 + a * z1));
    }
    return phase;
}

} // namespace

TEST(QuadratureFold, EachRateTakesTheTableItsFamilyWasFittedOn)
{
    EXPECT_EQ(QuadratureFold::tableFor(44100.0).baseRate, 48000.0);
    EXPECT_EQ(QuadratureFold::tableFor(48000.0).baseRate, 48000.0);
    EXPECT_EQ(QuadratureFold::tableFor(63999.0).baseRate, 48000.0);
    EXPECT_EQ(QuadratureFold::tableFor(64000.0).baseRate, 96000.0);
    EXPECT_EQ(QuadratureFold::tableFor(96000.0).baseRate, 96000.0);
    EXPECT_EQ(QuadratureFold::tableFor(192000.0).baseRate, 96000.0);
}

TEST(QuadratureFold, EveryCornerHasItsCoefficientAtEveryRate)
{
    // Written out, not worked out: scaling a corner with the rate keeps its
    // share of the rate, so each table has one set of numbers, whatever the
    // rate. A test that derived them would pass a retuned table.
    constexpr std::array<double, 4> kLowA{-0.9982788, -0.9768955, -0.8090247, 0.0118352};
    constexpr std::array<double, 4> kLowB{-0.9925131, -0.9320580, -0.5150374, 0.6477349};
    constexpr std::array<double, 4> kHighA{-0.9992143, -0.9905571, -0.9291579, -0.5405877};
    constexpr std::array<double, 4> kHighB{-0.9967361, -0.9738858, -0.8140779, 0.1094306};
    for (const double rate : kRates) {
        QuadratureFold fold;
        fold.prepare(rate);
        const bool low = rate < QuadratureFold::kHighTableFrom;
        for (std::size_t section = 0; section < QuadratureFold::kSections; ++section) {
            EXPECT_NEAR(fold.coefficients(0)[section], (low ? kLowA : kHighA)[section], 1e-6) << rate << ' ' << section;
            EXPECT_NEAR(fold.coefficients(1)[section], (low ? kLowB : kHighB)[section], 1e-6) << rate << ' ' << section;
        }
    }
}

TEST(QuadratureFold, TheFortyEightKilohertzCornersAreScaledAndNotUsedAsTheyStand)
{
    QuadratureFold fold;
    fold.prepare(44100.0);
    const double unscaled = QuadratureFold::coefficient(QuadratureFold::tableFor(48000.0).b[3], 44100.0);
    EXPECT_GT(std::abs(fold.coefficients(1)[3] - unscaled), 0.01);
}

TEST(QuadratureFold, EverySectionIsStable)
{
    for (const double rate : {8000.0, 22050.0, 44100.0, 48000.0, 64000.0, 96000.0, 192000.0, 384000.0}) {
        QuadratureFold fold;
        fold.prepare(rate);
        for (std::size_t chain = 0; chain < 2; ++chain) {
            for (const double a : fold.coefficients(chain)) {
                EXPECT_LT(std::abs(a), 1.0) << rate;
            }
        }
    }
}

TEST(QuadratureFold, CentredSoundKeepsItsLevelAcrossTheBand)
{
    for (const double rate : kRates) {
        for (const double hz : band()) {
            const double db = foldLevel(rate, hz, 1.0, 1.0, 0.0);
            EXPECT_GE(db, -0.4) << rate << " Hz " << hz;
            EXPECT_LE(db, 0.8) << rate << " Hz " << hz;
        }
    }
}

TEST(QuadratureFold, SoundOnOneSideDropsThreeDecibelsAcrossTheBand)
{
    for (const double rate : kRates) {
        for (const double hz : band()) {
            EXPECT_NEAR(foldLevel(rate, hz, 1.0, 0.0, 0.0), -3.0, 0.1) << rate << " left, Hz " << hz;
            EXPECT_NEAR(foldLevel(rate, hz, 0.0, 1.0, 0.0), -3.0, 0.1) << rate << " right, Hz " << hz;
        }
    }
}

TEST(QuadratureFold, AntiphaseSoundStaysAcrossTheBand)
{
    for (const double rate : kRates) {
        for (const double hz : band()) {
            const double db = foldLevel(rate, hz, 1.0, -1.0, 0.0);
            EXPECT_GE(db, -1.0) << rate << " Hz " << hz;
            EXPECT_LE(db, 0.4) << rate << " Hz " << hz;
        }
    }
}

TEST(QuadratureFold, TheChainsAreAQuarterOfACycleApart)
{
    struct Limit {
        double rate;
        double degrees;
    };
    for (const Limit limit :
         {Limit{44100.0, 4.05}, Limit{48000.0, 4.05}, Limit{96000.0, 2.15}, Limit{192000.0, 1.85}}) {
        QuadratureFold fold;
        fold.prepare(limit.rate);
        for (const double hz : band()) {
            const double apart =
                chainPhase(fold.coefficients(0), limit.rate, hz) - chainPhase(fold.coefficients(1), limit.rate, hz);
            const double wrapped = std::remainder(apart * 180.0 / std::numbers::pi, 360.0);
            EXPECT_NEAR(std::abs(wrapped), 90.0, limit.degrees) << limit.rate << " Hz " << hz;
        }
    }
}

TEST(QuadratureFold, TheNullIsWhereTheRightSideLeadsByAQuarterOfACycle)
{
    EXPECT_NEAR(foldLevel(48000.0, 1000.0, 1.0, 1.0, std::numbers::pi / 2.0), -30.2, 0.2);
    EXPECT_NEAR(foldLevel(48000.0, 1000.0, 1.0, 1.0, -std::numbers::pi / 2.0), 3.0, 0.1);
}

TEST(QuadratureFold, ThePeakCanRiseNoFurtherThanTheFiltersAllow)
{
    // The sum of the impulse responses' magnitudes, for full scale on both
    // sides. Ten seconds holds all of a tail 51 ms long.
    QuadratureFold other;
    other.prepare(48000.0);
    const QuadratureFold::Frame first = other.process(1.0f, 1.0f);
    double reach = std::abs(first.a) + std::abs(first.b);
    for (int frame = 0; frame < 480000; ++frame) {
        const QuadratureFold::Frame out = other.process(0.0f, 0.0f);
        reach += std::abs(out.a) + std::abs(out.b);
    }
    EXPECT_NEAR(20.0 * std::log10(QuadratureFold::kFoldGain * reach), 17.2, 0.1);
}

TEST(QuadratureFold, ResetForgetsWhatPassed)
{
    QuadratureFold fold;
    fold.prepare(48000.0);
    QuadratureFold fresh;
    fresh.prepare(48000.0);
    for (int frame = 0; frame < 100; ++frame) {
        (void)fold.process(0.5f, -0.5f);
    }
    fold.reset();
    for (int frame = 0; frame < 100; ++frame) {
        const QuadratureFold::Frame one = fold.process(0.25f, 0.5f);
        const QuadratureFold::Frame two = fresh.process(0.25f, 0.5f);
        ASSERT_EQ(one.a, two.a);
        ASSERT_EQ(one.b, two.b);
    }
}
