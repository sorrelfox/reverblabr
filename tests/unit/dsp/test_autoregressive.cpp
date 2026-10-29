// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// dsp::Autoregressive: the model fitted to a known autoregressive
// process finds its coefficients, and a gap in a signal the model fits is
// filled with what was there.

#include "allocationtrap.h"
#include "dsp/autoregressive.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>
#include <random>
#include <vector>

using namespace rb;
using namespace rb::dsp;
using namespace rb::test;

namespace
{

/// A second-order resonance driven by white noise: x[n] = 1.6 x[n−1]
/// − 0.8 x[n−2] + w[n].
[[nodiscard]] std::vector<float> resonance(std::size_t frames)
{
    std::vector<float> out(frames, 0.0f);
    std::mt19937 random(7);
    std::normal_distribution<double> noise(0.0, 0.01);
    double one = 0.0;
    double two = 0.0;
    for (float &sample : out) {
        const double x = 1.6 * one - 0.8 * two + noise(random);
        two = one;
        one = x;
        sample = static_cast<float>(x);
    }
    return out;
}

} // namespace

TEST(AutoregressiveTest, AKnownProcessGivesBackItsCoefficients)
{
    const std::vector<float> signal = resonance(48000);
    const std::vector<float> flat(signal.size(), 1.0f);
    Autoregressive model;
    model.prepare(2, 16);
    ASSERT_TRUE(model.fit(signal, flat));
    EXPECT_NEAR(model.filter()[1], -1.6, 0.01);
    EXPECT_NEAR(model.filter()[2], 0.8, 0.01);
}

TEST(AutoregressiveTest, SilenceHasNoModel)
{
    const std::vector<float> silence(1024, 0.0f);
    Autoregressive model;
    model.prepare(8, 16);
    EXPECT_FALSE(model.fit(silence, silence));
    EXPECT_EQ(model.filter()[0], 1.0);
    EXPECT_EQ(model.filter()[1], 0.0);
}

TEST(AutoregressiveTest, AGapIsFilledWithWhatWasThere)
{
    // Two tones and a little noise, 100 samples knocked out of the middle
    // and filled back from the order-32 model of the rest: within 1 % of
    // what was there, by energy (-20 dB and better); where zeroes left the
    // whole of it.
    std::vector<float> signal = sine(1024, 440.0, 48000.0, 0.4f);
    const std::vector<float> other = sine(1024, 1234.0, 48000.0, 0.2f);
    std::mt19937 random(3);
    std::normal_distribution<float> hiss(0.0f, 1.0e-3f);
    for (std::size_t n = 0; n < signal.size(); ++n) {
        signal[n] += other[n] + hiss(random);
    }
    std::vector<float> window(signal.size());
    for (std::size_t n = 0; n < window.size(); ++n) {
        window[n] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * (n + 0.5) / window.size()));
    }
    const std::vector<float> truth = signal;
    Autoregressive model;
    model.prepare(32, 144);
    ASSERT_TRUE(model.fit(signal, window));
    for (std::size_t n = 450; n < 550; ++n) {
        signal[n] = 0.0f;
    }
    {
        const NoAllocations realTime;
        ASSERT_TRUE(model.interpolate(signal, 450, 100));
    }
    double error = 0.0;
    double energy = 0.0;
    for (std::size_t n = 450; n < 550; ++n) {
        error += std::pow(signal[n] - truth[n], 2.0);
        energy += std::pow(truth[n], 2.0);
    }
    EXPECT_LT(10.0 * std::log10(error / energy), -20.0);
}

TEST(AutoregressiveTest, AGapTooLongOrTooNearTheEdgeIsRefused)
{
    std::vector<float> signal(512, 0.1f);
    Autoregressive model;
    model.prepare(32, 64);
    EXPECT_FALSE(model.interpolate(signal, 100, 65)) << "longer than the work space";
    EXPECT_FALSE(model.interpolate(signal, 31, 10)) << "fewer than the order before it";
    EXPECT_FALSE(model.interpolate(signal, 480, 10)) << "fewer than the order after it";
    EXPECT_TRUE(model.interpolate(signal, 32, 10));
}
