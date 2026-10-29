// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "dsp/fft.h"

#include <cmath>
#include <numbers>
#include <utility>

namespace rb::dsp
{

void RealFft::prepare(std::size_t size)
{
    m_size = size;
    m_half = size / 2;
    const std::size_t half = m_half;

    // Worked out in double, so the tables are as exact as a float holds.
    m_twiddles.resize(half / 2 > 0 ? half / 2 : 1);
    for (std::size_t k = 0; k < m_twiddles.size(); ++k) {
        const double angle = -2.0 * std::numbers::pi * static_cast<double>(k) / static_cast<double>(half);
        m_twiddles[k] = {static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle))};
    }
    m_realTwiddles.resize(half + 1);
    for (std::size_t k = 0; k <= half; ++k) {
        const double angle = -2.0 * std::numbers::pi * static_cast<double>(k) / static_cast<double>(size);
        m_realTwiddles[k] = {static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle))};
    }

    std::uint32_t bits = 0;
    while ((std::size_t{1} << bits) < half) {
        ++bits;
    }
    m_reversed.resize(half);
    for (std::size_t index = 0; index < half; ++index) {
        std::uint32_t reversed = 0;
        for (std::uint32_t bit = 0; bit < bits; ++bit) {
            if ((index >> bit) & 1U) {
                reversed |= 1U << (bits - 1 - bit);
            }
        }
        m_reversed[index] = reversed;
    }
    m_work.assign(half, {0.0f, 0.0f});
}

void RealFft::transform(bool inverse) noexcept RB_NONBLOCKING
{
    const std::size_t half = m_half;
    for (std::size_t index = 0; index < half; ++index) {
        const std::size_t reversed = m_reversed[index];
        if (reversed > index) {
            std::swap(m_work[index], m_work[reversed]);
        }
    }
    // Butterflies, doubling the span each pass; the twiddle for span s and
    // position k is e^(-2πik/s), which is table entry k * half / s.
    for (std::size_t span = 2; span <= half; span *= 2) {
        const std::size_t step = half / span;
        const std::size_t middle = span / 2;
        for (std::size_t start = 0; start < half; start += span) {
            for (std::size_t k = 0; k < middle; ++k) {
                std::complex<float> twiddle = m_twiddles[k * step];
                if (inverse) {
                    twiddle = std::conj(twiddle);
                }
                const std::complex<float> odd = m_work[start + k + middle] * twiddle;
                const std::complex<float> even = m_work[start + k];
                m_work[start + k] = even + odd;
                m_work[start + k + middle] = even - odd;
            }
        }
    }
}

void RealFft::forward(std::span<const float> signal, std::span<std::complex<float>> spectrum) noexcept RB_NONBLOCKING
{
    const std::size_t half = m_half;
    // The even samples as the real parts and the odd ones as the imaginary:
    // one complex transform of half the length does the work of a real one.
    for (std::size_t m = 0; m < half; ++m) {
        m_work[m] = {signal[2 * m], signal[2 * m + 1]};
    }
    transform(false);
    // Z[k] holds both halves' spectra at once; they are pulled apart with
    // Z[half - k], and the odd half turned by e^(-2πik/N).
    for (std::size_t k = 0; k <= half; ++k) {
        const std::complex<float> z = m_work[k == half ? 0 : k];
        const std::complex<float> mirrored = std::conj(m_work[k == 0 ? 0 : half - k]);
        const std::complex<float> even = 0.5f * (z + mirrored);
        const std::complex<float> odd = std::complex<float>(0.0f, -0.5f) * (z - mirrored);
        spectrum[k] = even + odd * m_realTwiddles[k];
    }
}

void RealFft::inverse(std::span<const std::complex<float>> spectrum, std::span<float> signal) noexcept RB_NONBLOCKING
{
    const std::size_t half = m_half;
    // The two half-length spectra back out of the real one, put together as
    // one complex spectrum, and transformed back.
    for (std::size_t k = 0; k < half; ++k) {
        std::complex<float> x = spectrum[k];
        std::complex<float> mirrored = std::conj(spectrum[half - k]);
        if (k == 0) {
            // A real signal's first and last bins are real.
            x = {x.real(), 0.0f};
            mirrored = {spectrum[half].real(), 0.0f};
        }
        const std::complex<float> even = x + mirrored;
        const std::complex<float> odd = (x - mirrored) * std::conj(m_realTwiddles[k]);
        m_work[k] = even + std::complex<float>(0.0f, 1.0f) * odd;
    }
    transform(true);
    const float scale = 1.0f / static_cast<float>(m_size);
    for (std::size_t m = 0; m < half; ++m) {
        signal[2 * m] = m_work[m].real() * scale;
        signal[2 * m + 1] = m_work[m].imag() * scale;
    }
}

} // namespace rb::dsp
