// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The live spectrum's analyser: a sine shows at its own frequency, and
// pink noise — equal power in every octave — draws flat.

#include "dsp/spectrum.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <numeric>
#include <vector>

using namespace rb::dsp;
using namespace rb::test;

namespace
{

/// The band a frequency falls in.
[[nodiscard]] std::size_t bandOf(double hz)
{
    for (std::size_t band = 0; band < SpectrumAnalyser::kBands; ++band) {
        if (hz < SpectrumAnalyser::bandEdgeHz(band + 1)) {
            return band;
        }
    }
    return SpectrumAnalyser::kBands - 1;
}

[[nodiscard]] std::size_t loudestBand(const SpectrumAnalyser &analyser)
{
    const auto &bands = analyser.bandsDb();
    return static_cast<std::size_t>(std::distance(bands.begin(), std::ranges::max_element(bands)));
}

} // namespace

TEST(SpectrumTest, TheBandsSpanTheRangeInEqualSteps)
{
    EXPECT_DOUBLE_EQ(SpectrumAnalyser::bandEdgeHz(0), 20.0);
    EXPECT_NEAR(SpectrumAnalyser::bandEdgeHz(SpectrumAnalyser::kBands), 20000.0, 1e-6);
    // Every band the same width in octaves.
    const double first = std::log2(SpectrumAnalyser::bandEdgeHz(1) / SpectrumAnalyser::bandEdgeHz(0));
    const double last = std::log2(SpectrumAnalyser::bandEdgeHz(SpectrumAnalyser::kBands)
                                  / SpectrumAnalyser::bandEdgeHz(SpectrumAnalyser::kBands - 1));
    EXPECT_NEAR(first, last, 1e-9);
}

TEST(SpectrumTest, ASineShowsAtItsFrequencyWithinOneBand)
{
    // **The acceptance criterion.** The loudest band is the sine's own, or
    // the one beside it, from the bottom of the range to the top and at two
    // rates.
    for (const double rate : {44100.0, 48000.0}) {
        for (const double hz : {31.0, 50.0, 120.0, 440.0, 1000.0, 3150.0, 9000.0, 17000.0}) {
            SpectrumAnalyser analyser;
            analyser.prepare(rate);
            analyser.push(sine(static_cast<std::size_t>(rate / 2), hz, rate, 0.5f));
            const std::size_t loudest = loudestBand(analyser);
            const auto away = static_cast<long>(loudest) - static_cast<long>(bandOf(hz));
            EXPECT_LE(std::abs(away), 1) << hz << " Hz at " << rate << " showed in band " << loudest << " ("
                                         << SpectrumAnalyser::bandCentreHz(loudest) << " Hz)";
        }
    }
}

TEST(SpectrumTest, AFullScaleSineReadsNoughtDecibelsWhereABandHoldsIt)
{
    // Where a band is wider than the window's spread of a sine, all of its
    // power is in that band: a sine peaking at 0.5 is -6.02 dB.
    SpectrumAnalyser analyser;
    analyser.prepare(48000.0);
    analyser.push(sine(48000, 5000.0, 48000.0, 0.5f));
    EXPECT_NEAR(analyser.bandsDb()[bandOf(5000.0)], -6.02, 0.5);
}

TEST(SpectrumTest, PinkNoiseDrawsFlat)
{
    // **The acceptance criterion.** Pink noise has the same power in every
    // octave; read in octave-wide groups of bands from 40 Hz to 16 kHz, every
    // group is within 2 dB of their average.
    SpectrumAnalyser analyser;
    analyser.prepare(48000.0);
    analyser.push(pinkNoise(48000 * 10, 0.25f, 232));
    const std::size_t bandsPerOctave = static_cast<std::size_t>(
        std::round(SpectrumAnalyser::kBands / std::log2(SpectrumAnalyser::kHighestHz / SpectrumAnalyser::kLowestHz)));
    std::vector<double> octaves;
    for (std::size_t start = bandOf(40.0); start + bandsPerOctave <= bandOf(16000.0); start += bandsPerOctave) {
        double power = 0.0;
        for (std::size_t band = start; band < start + bandsPerOctave; ++band) {
            power += std::pow(10.0, analyser.bandsDb()[band] / 10.0);
        }
        octaves.push_back(10.0 * std::log10(power));
    }
    ASSERT_GE(octaves.size(), 8U);
    const double mean = std::accumulate(octaves.begin(), octaves.end(), 0.0) / static_cast<double>(octaves.size());
    for (std::size_t octave = 0; octave < octaves.size(); ++octave) {
        EXPECT_NEAR(octaves[octave], mean, 2.0) << "octave " << octave << " from 40 Hz";
    }
}

TEST(SpectrumTest, SilenceIsTheFloorAndResetForgets)
{
    SpectrumAnalyser analyser;
    analyser.prepare(48000.0);
    analyser.push(std::vector<float>(48000, 0.0f));
    for (const float db : analyser.bandsDb()) {
        EXPECT_EQ(db, SpectrumAnalyser::kFloorDb);
    }
    analyser.push(sine(48000, 1000.0, 48000.0, 0.5f));
    EXPECT_GT(analyser.bandsDb()[bandOf(1000.0)], -20.0f);
    analyser.reset();
    EXPECT_EQ(analyser.bandsDb()[bandOf(1000.0)], SpectrumAnalyser::kFloorDb);
    EXPECT_EQ(analyser.frames(), 0U);
}

TEST(SpectrumTest, ItSettlesRatherThanFlickers)
{
    // A tone that stops: a quarter of a second later the band has fallen by
    // no more than the averaging lets it — the picture decays, it does not
    // blink out — and two seconds later it is well on its way down.
    SpectrumAnalyser analyser;
    analyser.prepare(48000.0);
    analyser.push(sine(48000, 1000.0, 48000.0, 0.5f));
    const float playing = analyser.bandsDb()[bandOf(1000.0)];
    analyser.push(std::vector<float>(12000, 0.0f));
    const float soon = analyser.bandsDb()[bandOf(1000.0)];
    EXPECT_LT(soon, playing);
    EXPECT_GT(soon, playing - 30.0f) << "the band dropped from " << playing << " to " << soon << " in 250 ms";
    analyser.push(std::vector<float>(96000, 0.0f));
    EXPECT_LT(analyser.bandsDb()[bandOf(1000.0)], playing - 30.0f);
}
