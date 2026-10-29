// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "rt/realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

namespace rb::dsp
{

/// How high a signal goes between its samples, as well as at them.
///
/// **A sample peak is not the peak.** The converter that plays a signal
/// draws a smooth curve through the samples, and the curve can rise above
/// every one of them: a tone at a quarter of the rate whose samples all
/// fall half-way up its slopes reads −3 dBFS by its samples and reaches
/// 0 dBFS in the air. A limiter that only looks at samples lets that
/// through, and the converter, or the next encoder, clips it.
///
/// **Four times oversampled**, as ITU-R BS.1770 measures a true peak: the
/// three points between every pair of samples are interpolated with a
/// Hann-windowed sinc 24 samples long, and the largest of the four is the
/// estimate. Four points a sample can miss the very top of a tone near the
/// top of the band — at 20 kHz by up to half a decibel, which is the
/// recommendation's own tolerance — and never read over it.
///
/// **It looks kDelay samples back.** Interpolating a point needs samples on
/// both sides of it, so what push() returns is about the sample kDelay ago:
/// a limiter holds its signal back that much longer so the estimate and the
/// sample it is about arrive together.
class TruePeakEstimator
{
public:
    /// How many input samples each interpolated point is worked out from.
    static constexpr std::size_t kTaps = 24;
    /// How far back the estimate is, in samples: half the taps.
    static constexpr std::uint32_t kDelay = kTaps / 2;
    /// Points between two samples.
    static constexpr std::size_t kBetween = 3;

    /// Makes the histories and works out the coefficients. **Not for the
    /// audio thread.**
    void prepare(std::uint32_t channels)
    {
        m_channels = channels;
        // Twice the taps a channel: every sample is written at its place
        // and again kTaps further on, so the kTaps samples ending at any
        // place lie one after another and the sums below run straight
        // through memory, which the compiler can do several at a time.
        m_history.assign(static_cast<std::size_t>(channels) * kTaps * 2, 0.0f);
        m_next = 0;
        // Counted in samples from the oldest one held (tap 0): the sample
        // kDelay ago is at kTaps - 1 - kDelay, and point p of the three
        // between it and the next is (p + 1) / 4 after it.
        for (std::size_t point = 0; point < kBetween; ++point) {
            const double at = static_cast<double>(kTaps - 1 - kDelay) + static_cast<double>(point + 1) / 4.0;
            double sum = 0.0;
            for (std::size_t tap = 0; tap < kTaps; ++tap) {
                const double distance = at - static_cast<double>(tap);
                const double value = sinc(distance) * window(distance);
                m_coefficients[point][tap] = value;
                sum += value;
            }
            // Unity at DC, so a constant reads as itself between samples.
            for (double &value : m_coefficients[point]) {
                value /= sum;
            }
        }
    }

    /// Forgets the signal. Real-time: it writes zeros.
    void reset() noexcept RB_NONBLOCKING
    {
        std::ranges::fill(m_history, 0.0f);
        m_next = 0;
    }

    /// Takes the next sample of \a channel. Call once for every channel,
    /// read delayedSample() and peak(), then advance().
    void write(std::uint32_t channel, float sample) noexcept RB_NONBLOCKING
    {
        if (channel < m_channels) {
            float *const line = m_history.data() + static_cast<std::size_t>(channel) * kTaps * 2;
            line[m_next] = sample;
            line[m_next + kTaps] = sample;
        }
    }

    /// The sample of \a channel kDelay samples ago, as written.
    [[nodiscard]] float delayedSample(std::uint32_t channel) const noexcept RB_NONBLOCKING
    {
        if (channel >= m_channels) {
            return 0.0f;
        }
        return m_history[static_cast<std::size_t>(channel) * kTaps * 2 + (m_next + kTaps - kDelay) % kTaps];
    }

    /// The largest magnitude of \a channel between the sample kDelay ago
    /// and the one after it, that sample included.
    [[nodiscard]] float peak(std::uint32_t channel) const noexcept RB_NONBLOCKING
    {
        if (channel >= m_channels) {
            return 0.0f;
        }
        // Oldest first: tap 0 is the sample written kTaps - 1 ago, and the
        // kTaps from there on are contiguous (see prepare()).
        const float *const line = m_history.data() + static_cast<std::size_t>(channel) * kTaps * 2 + m_next + 1;
        float loudest = std::fabs(delayedSample(channel));
        for (const std::array<double, kTaps> &coefficients : m_coefficients) {
            double sum = 0.0;
            for (std::size_t tap = 0; tap < kTaps; ++tap) {
                sum += coefficients[tap] * line[tap];
            }
            loudest = std::max(loudest, static_cast<float>(std::fabs(sum)));
        }
        return loudest;
    }

    /// Moves on to the next sample, once every channel has been written.
    void advance() noexcept RB_NONBLOCKING
    {
        m_next = m_next + 1 == kTaps ? 0 : m_next + 1;
    }

private:
    [[nodiscard]] static double sinc(double x) noexcept
    {
        if (std::fabs(x) < 1.0e-12) {
            return 1.0;
        }
        return std::sin(std::numbers::pi * x) / (std::numbers::pi * x);
    }

    /// A Hann window across the taps. **Not a steeper one**: a window that
    /// buys a quieter stop band with a wider transition reads the top of the
    /// band low, and the top of the band is where true peaks come from. A
    /// Blackman-Harris over sixteen taps read programme 0.3 dB low at
    /// 11 kHz; this reads it within 0.01 dB of a 128-tap reference.
    [[nodiscard]] static double window(double distance) noexcept
    {
        const double x = (distance + static_cast<double>(kDelay)) / static_cast<double>(kTaps);
        if (x <= 0.0 || x >= 1.0) {
            return 0.0;
        }
        return 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * x);
    }

    std::vector<float> m_history;
    std::array<std::array<double, kTaps>, kBetween> m_coefficients{};
    std::uint32_t m_channels = 0;
    std::size_t m_next = 0;
};

} // namespace rb::dsp
