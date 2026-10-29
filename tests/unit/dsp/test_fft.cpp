// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The real FFT (ADR-0015): against a DFT worked out the slow way,
// there and back, and allocation-free.

#include "allocationtrap.h"
#include "dsp/fft.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

using namespace rb::dsp;
using namespace rb::test;

namespace
{

/// The DFT by its definition, in double: slow, and obviously right.
[[nodiscard]] std::vector<std::complex<double>> slowDft(const std::vector<float> &signal)
{
    const std::size_t size = signal.size();
    std::vector<std::complex<double>> spectrum(size / 2 + 1);
    for (std::size_t k = 0; k < spectrum.size(); ++k) {
        std::complex<double> sum = 0.0;
        for (std::size_t n = 0; n < size; ++n) {
            const double angle = -2.0 * std::numbers::pi * static_cast<double>(k * n) / static_cast<double>(size);
            sum += static_cast<double>(signal[n]) * std::complex<double>(std::cos(angle), std::sin(angle));
        }
        spectrum[k] = sum;
    }
    return spectrum;
}

} // namespace

TEST(RealFftTest, ItIsTheDftAtEverySize)
{
    for (const std::size_t size : {4U, 8U, 16U, 64U, 512U, 2048U}) {
        const Signal noise = whiteNoise(size, 1.0f, static_cast<std::uint32_t>(size));
        RealFft fft;
        fft.prepare(size);
        ASSERT_EQ(fft.bins(), size / 2 + 1);
        std::vector<std::complex<float>> spectrum(fft.bins());
        fft.forward(noise, spectrum);
        const std::vector<std::complex<double>> expected = slowDft(noise);
        double worst = 0.0;
        for (std::size_t k = 0; k < spectrum.size(); ++k) {
            worst = std::max(worst, std::abs(std::complex<double>(spectrum[k]) - expected[k]));
        }
        // Relative to the spectrum's own size, which grows as sqrt(N) for
        // noise: float rounding over log2(N) passes.
        EXPECT_LT(worst / std::sqrt(static_cast<double>(size)), 1e-5) << "at " << size << " points";
    }
}

TEST(RealFftTest, ThereAndBackIsTheSignal)
{
    for (const std::size_t size : {4U, 256U, 2048U, 8192U}) {
        const Signal noise = whiteNoise(size, 1.0f, 7);
        RealFft fft;
        fft.prepare(size);
        std::vector<std::complex<float>> spectrum(fft.bins());
        std::vector<float> back(size);
        fft.forward(noise, spectrum);
        fft.inverse(spectrum, back);
        float worst = 0.0f;
        for (std::size_t n = 0; n < size; ++n) {
            worst = std::max(worst, std::fabs(back[n] - noise[n]));
        }
        EXPECT_LT(worst, 2e-6f) << "at " << size << " points";
    }
}

TEST(RealFftTest, ASineIsOneBin)
{
    // A whole number of cycles in the frame: all of it in one bin, at half
    // the frame's length times its amplitude, and nothing anywhere else.
    constexpr std::size_t size = 1024;
    const Signal tone = sine(size, 48000.0 * 37.0 / size, 48000.0, 0.5f);
    RealFft fft;
    fft.prepare(size);
    std::vector<std::complex<float>> spectrum(fft.bins());
    fft.forward(tone, spectrum);
    EXPECT_NEAR(std::abs(spectrum[37]), 0.5f * size / 2.0f, 1e-2f);
    for (std::size_t k = 0; k < spectrum.size(); ++k) {
        if (k != 37) {
            EXPECT_LT(std::abs(spectrum[k]), 1e-2f) << "bin " << k;
        }
    }
}

TEST(RealFftTest, ATransformTouchesTheAllocatorNotAtAll)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    constexpr std::size_t size = 2048;
    RealFft fft;
    fft.prepare(size);
    const Signal noise = whiteNoise(size, 1.0f, 3);
    std::vector<std::complex<float>> spectrum(fft.bins());
    std::vector<float> back(size);
    std::size_t allocations = 1;
    {
        NoAllocations trap(NoAllocations::OnAllocation::Count);
        fft.forward(noise, spectrum);
        fft.inverse(spectrum, back);
        allocations = trap.count();
    }
    EXPECT_EQ(allocations, 0U);
    EXPECT_NEAR(back[100], noise[100], 1e-5f);
}
