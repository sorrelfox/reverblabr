// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "dsp/centresplit.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rb::dsp
{

namespace
{

/// sqrt-Hann in and out, four frames over every sample: the windows' products
/// add to 2, so the sum is halved.
constexpr float kOverlapScale = 0.5f;
/// Under the root, so that silence on both sides reads as nothing alike. Far
/// under any level a recording can hold.
constexpr float kEmpty = 1.0e-30f;
/// The hop the smoothing was chosen at: 256 samples at 48 kHz.
constexpr double kChosenHopSeconds = 256.0 / 48000.0;

/// The first band at or above \a hz, in a frame of \a size samples.
[[nodiscard]] std::size_t firstBinFrom(float hz, double sampleRate, std::size_t size) noexcept
{
    return static_cast<std::size_t>(std::ceil(static_cast<double>(hz) * static_cast<double>(size) / sampleRate));
}

/// The band after the last one at or under \a hz.
[[nodiscard]] std::size_t endBinAt(float hz, double sampleRate, std::size_t size) noexcept
{
    return static_cast<std::size_t>(std::floor(static_cast<double>(hz) * static_cast<double>(size) / sampleRate)) + 1;
}

} // namespace

void CentreSplit::prepare(double sampleRate)
{
    m_frameSize = kFrameSize;
    for (double rate = sampleRate; rate > kMostRateOfOneFrame; rate /= 2.0) {
        m_frameSize *= 2;
    }
    m_hop = m_frameSize / kHopsPerFrame;
    const std::size_t bins = m_frameSize / 2 + 1;
    m_firstBin = std::min(firstBinFrom(kLowestHz, sampleRate, m_frameSize), bins);
    m_endBin = std::min(endBinAt(kHighestHz, sampleRate, m_frameSize), bins);
    m_voiceFirstBin = std::min(firstBinFrom(kVoiceLowestHz, sampleRate, m_frameSize), bins);
    m_voiceEndBin = std::min(endBinAt(kVoiceHighestHz, sampleRate, m_frameSize), bins);
    m_smoothing = static_cast<float>(
        std::pow(static_cast<double>(kSmoothingAt48k), static_cast<double>(m_hop) / sampleRate / kChosenHopSeconds));
    const auto size = static_cast<float>(m_frameSize);
    m_powerScale = 8.0f / (size * size);

    m_fft.prepare(m_frameSize);
    m_window.resize(m_frameSize);
    for (std::size_t n = 0; n < m_frameSize; ++n) {
        const double hann =
            0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(n) / static_cast<double>(m_frameSize));
        m_window[n] = static_cast<float>(std::sqrt(hann));
    }
    for (std::size_t side = 0; side < kSides; ++side) {
        m_input[side].resize(m_frameSize);
        m_delayed[side].resize(m_frameSize);
        m_spectrum[side].resize(bins);
    }
    m_frame.resize(m_frameSize);
    m_centre.resize(bins);
    m_output.resize(m_frameSize);
    m_ready.resize(m_hop);
    m_leftPower.resize(bins);
    m_rightPower.resize(bins);
    m_bothPower.resize(bins);
    m_mask.resize(bins);
    reset();
}

void CentreSplit::reset() noexcept RB_NONBLOCKING
{
    for (std::size_t side = 0; side < kSides; ++side) {
        std::ranges::fill(m_input[side], 0.0f);
        std::ranges::fill(m_delayed[side], 0.0f);
    }
    std::ranges::fill(m_centre, std::complex<float>{0.0f, 0.0f});
    std::ranges::fill(m_output, 0.0f);
    std::ranges::fill(m_ready, 0.0f);
    std::ranges::fill(m_leftPower, 0.0f);
    std::ranges::fill(m_rightPower, 0.0f);
    std::ranges::fill(m_bothPower, 0.0f);
    std::ranges::fill(m_mask, 0.0f);
    m_position = m_frameSize - m_hop;
    m_nextDelayed = 0;
    m_hopEnded = false;
    m_centrePower = 0.0f;
    m_widePower = 0.0f;
}

void CentreSplit::processFrame() noexcept RB_NONBLOCKING
{
    for (std::size_t side = 0; side < kSides; ++side) {
        for (std::size_t n = 0; n < m_frameSize; ++n) {
            m_frame[n] = m_input[side][n] * m_window[n];
        }
        m_fft.forward(m_frame, m_spectrum[side]);
        std::copy(
            m_input[side].begin() + static_cast<std::ptrdiff_t>(m_hop), m_input[side].end(), m_input[side].begin());
    }

    const float keep = m_smoothing;
    const float take = 1.0f - keep;
    for (std::size_t bin = m_firstBin; bin < m_endBin; ++bin) {
        const std::complex<float> left = m_spectrum[0][bin];
        const std::complex<float> right = m_spectrum[1][bin];
        const float pl = keep * m_leftPower[bin] + take * std::norm(left);
        const float pr = keep * m_rightPower[bin] + take * std::norm(right);
        const float plr = keep * m_bothPower[bin] + take * (left.real() * right.real() + left.imag() * right.imag());
        m_leftPower[bin] = pl;
        m_rightPower[bin] = pr;
        m_bothPower[bin] = plr;
        const float alike = std::clamp(plr / std::sqrt(pl * pr + kEmpty), 0.0f, 1.0f);
        // Four squarings, the 16th power.
        const float squared = alike * alike;
        const float fourth = squared * squared;
        const float eighth = fourth * fourth;
        const float mask = eighth * eighth;
        m_mask[bin] = mask;
        m_centre[bin] = mask * 0.5f * (left + right);
    }

    float centre = 0.0f;
    float wide = 0.0f;
    for (std::size_t bin = m_voiceFirstBin; bin < m_voiceEndBin; ++bin) {
        centre += std::norm(m_centre[bin]);
        wide += 0.5f * (std::norm(m_spectrum[0][bin]) + std::norm(m_spectrum[1][bin]));
    }
    m_centrePower = centre * m_powerScale;
    m_widePower = wide * m_powerScale;

    m_fft.inverse(m_centre, m_frame);
    for (std::size_t n = 0; n < m_frameSize; ++n) {
        m_output[n] += m_frame[n] * m_window[n] * kOverlapScale;
    }
    // The first hop has had every frame it will get: it is ready.
    const auto hop = static_cast<std::ptrdiff_t>(m_hop);
    std::copy(m_output.begin(), m_output.begin() + hop, m_ready.begin());
    std::copy(m_output.begin() + hop, m_output.end(), m_output.begin());
    std::fill(m_output.end() - hop, m_output.end(), 0.0f);
}

} // namespace rb::dsp
