// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// CentreSplit: what is alike comes out lined up with the delayed input, what
// is not stays out, its band ends where it says, and its frame follows the rate.

#include "dsp/centresplit.h"
#include "dsp/decibel.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <vector>

using namespace rb;
using namespace rb::dsp;
using namespace rb::test;

namespace
{

[[nodiscard]] double powerOf(std::span<const float> samples)
{
    double sum = 0.0;
    for (const float sample : samples) {
        sum += static_cast<double>(sample) * static_cast<double>(sample);
    }
    return sum / static_cast<double>(samples.size());
}

/// What is left of \a left and \a right as centre, in dB of the input, read
/// over the second half.
[[nodiscard]] float centreDb(CentreSplit &split, const Signal &left, const Signal &right)
{
    std::vector<float> centre;
    std::vector<float> in;
    for (std::size_t n = 0; n < left.size(); ++n) {
        const CentreSplit::Parts parts = split.push(left[n], right[n]);
        if (n > left.size() / 2) {
            centre.push_back(parts.centre);
            in.push_back(0.5f * (parts.left + parts.right));
        }
    }
    return static_cast<float>(10.0 * std::log10(powerOf(centre) / std::max(powerOf(in), 1.0e-20)));
}

} // namespace

TEST(CentreSplit, TheSameOnBothSidesComesOutLinedUpWithTheDelayedInput)
{
    CentreSplit split;
    split.prepare(48000.0);
    const Signal tone = sine(48000, 1000.0, 48000.0, 0.5f);
    double left = 0.0;
    double error = 0.0;
    for (std::size_t n = 0; n < tone.size(); ++n) {
        const CentreSplit::Parts parts = split.push(tone[n], tone[n]);
        ASSERT_EQ(parts.left, n >= 1024 ? tone[n - 1024] : 0.0f);
        ASSERT_EQ(parts.right, parts.left);
        if (n > 24000) {
            left += static_cast<double>(parts.left) * parts.left;
            error += static_cast<double>(parts.centre - parts.left) * (parts.centre - parts.left);
        }
    }
    EXPECT_LT(10.0 * std::log10(error / left), -60.0) << "the centre and the input are not one signal";
}

TEST(CentreSplit, ItsBandEndsWhereItSays)
{
    for (const auto &[hz, inside] :
         {std::pair{60.0, false}, std::pair{150.0, true}, std::pair{11000.0, true}, std::pair{14000.0, false}}) {
        CentreSplit split;
        split.prepare(48000.0);
        const Signal tone = sine(24000, hz, 48000.0, 0.5f);
        const float db = centreDb(split, tone, tone);
        if (inside) {
            EXPECT_NEAR(db, 0.0f, 0.5f) << hz;
        } else {
            EXPECT_LT(db, -20.0f) << hz;
        }
    }
}

TEST(CentreSplit, OpposedOrUnrelatedSidesGiveNoCentre)
{
    const Signal left = whiteNoise(48000, 0.3f, 11);
    Signal opposed = left;
    for (float &sample : opposed) {
        sample = -sample;
    }
    const Signal unrelated = whiteNoise(48000, 0.3f, 12);
    const Signal silent = silence(48000);

    CentreSplit first;
    first.prepare(48000.0);
    double energy = 0.0;
    for (std::size_t n = 0; n < left.size(); ++n) {
        const float centre = first.push(left[n], opposed[n]).centre;
        energy += static_cast<double>(centre) * centre;
    }
    EXPECT_LT(energy, 1.0e-6);

    CentreSplit second;
    second.prepare(48000.0);
    EXPECT_LT(centreDb(second, left, unrelated), -15.0f);

    CentreSplit third;
    third.prepare(48000.0);
    EXPECT_LT(centreDb(third, left, silent), -100.0f);
}

TEST(CentreSplit, ItWorksFromThePhaseNotTheBalance)
{
    // The trap the block's help warns of: a source panned part-way in the
    // same phase scores as centre.
    const Signal left = whiteNoise(48000, 0.4f, 13);
    Signal right = left;
    for (float &sample : right) {
        sample *= 0.25f;
    }
    CentreSplit split;
    split.prepare(48000.0);
    for (std::size_t n = 0; n < left.size(); ++n) {
        (void)split.push(left[n], right[n]);
    }
    EXPECT_NEAR(split.mask()[100], 1.0f, 1.0e-3f);
}

TEST(CentreSplit, AFullScaleToneReadsOneInTheVoiceBand)
{
    CentreSplit split;
    split.prepare(48000.0);
    const Signal tone = sine(24000, 1000.0, 48000.0, 1.0f);
    for (const float sample : tone) {
        (void)split.push(sample, sample);
    }
    EXPECT_NEAR(split.widePower(), 1.0f, 0.02f);
    EXPECT_NEAR(split.centrePower(), 1.0f, 0.02f);

    CentreSplit outside;
    outside.prepare(48000.0);
    const Signal high = sine(24000, 8000.0, 48000.0, 1.0f);
    for (const float sample : high) {
        (void)outside.push(sample, sample);
    }
    EXPECT_LT(outside.widePower(), 1.0e-6f) << "8 kHz counted in the voice band";
}

TEST(CentreSplit, TheFrameFollowsTheRate)
{
    for (const auto &[rate, frame] : {std::pair{22050.0, std::size_t{1024}},
                                      std::pair{44100.0, std::size_t{1024}},
                                      std::pair{48000.0, std::size_t{1024}},
                                      std::pair{96000.0, std::size_t{2048}},
                                      std::pair{192000.0, std::size_t{4096}}}) {
        CentreSplit split;
        split.prepare(rate);
        EXPECT_EQ(split.frameSize(), frame) << rate;
        EXPECT_EQ(split.hop(), frame / 4) << rate;
        const Signal tone = sine(static_cast<std::size_t>(rate / 2.0), 1000.0, rate, 0.5f);
        EXPECT_NEAR(centreDb(split, tone, tone), 0.0f, 0.5f) << rate;
    }
}

TEST(CentreSplit, ResetForgetsTheSignal)
{
    CentreSplit split;
    split.prepare(48000.0);
    const Signal noise = whiteNoise(4800, 0.5f, 14);
    for (const float sample : noise) {
        (void)split.push(sample, sample);
    }
    split.reset();
    EXPECT_EQ(split.centrePower(), 0.0f);
    for (std::size_t n = 0; n < 4096; ++n) {
        const CentreSplit::Parts parts = split.push(0.0f, 0.0f);
        ASSERT_EQ(parts.centre, 0.0f);
        ASSERT_EQ(parts.left, 0.0f);
    }
}
