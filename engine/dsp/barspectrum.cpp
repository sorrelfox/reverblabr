// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "dsp/barspectrum.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rb::dsp
{

void FrameSpectrum::prepare(std::size_t frameSize)
{
    m_frameSize = frameSize;
    m_fft.prepare(std::min(frameSize * kPadding, kLargestTransform));
    m_window.resize(frameSize);
    double sum = 0.0;
    for (std::size_t n = 0; n < frameSize; ++n) {
        // The periodic Hann window, which overlaps at half a frame to a
        // constant, so that no moment counts for less than any other.
        const double w = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(n) / frameSize);
        m_window[n] = static_cast<float>(w);
        sum += w;
    }
    // A sine of amplitude A at a bin gives |X| = A * sum / 2.
    const double scale = 2.0 / sum;
    m_scale = static_cast<float>(scale * scale);
    // The padding stays silence: only the first frameSize are ever written.
    m_padded.assign(m_fft.size(), 0.0f);
    m_spectrum.assign(m_fft.bins(), {});
    m_power.assign(m_fft.bins(), 0.0f);
}

void FrameSpectrum::clear() noexcept
{
    std::ranges::fill(m_power, 0.0f);
}

void FrameSpectrum::add(std::span<const float> samples) noexcept
{
    // The window's first value is nought, so that sample is not needed.
    m_padded[0] = 0.0f;
    bool sound = true;
    for (std::size_t n = 1; n < m_frameSize; ++n) {
        sound = sound && std::isfinite(samples[n - 1]);
        m_padded[n] = samples[n - 1] * m_window[n];
    }
    if (!sound) {
        std::ranges::fill(m_power, kMostPower);
        return;
    }
    m_fft.forward(m_padded, m_spectrum);
    for (std::size_t bin = 0; bin < m_power.size(); ++bin) {
        const float power = std::norm(m_spectrum[bin]) * m_scale;
        // Written so that a bin the transform made nonsense of counts too.
        m_power[bin] = power <= kMostPower ? std::max(m_power[bin], power) : kMostPower;
    }
}

double BarMap::highestHz(double sampleRate) noexcept
{
    return std::clamp(sampleRate / 2.0, kLowestHz * 2.0, kHighestHz);
}

double BarMap::positionOf(double hz, BarScale scale, double sampleRate) noexcept
{
    const double top = highestHz(sampleRate);
    const double along = scale == BarScale::Logarithmic
        ? std::log(std::max(hz, kLowestHz) / kLowestHz) / std::log(top / kLowestHz)
        : (hz - kLowestHz) / (top - kLowestHz);
    return std::clamp(along, 0.0, 1.0);
}

void BarMap::prepare(std::size_t bars,
                     BarScale scale,
                     double sampleRate,
                     std::size_t transformSize,
                     double tiltDbPerOctave)
{
    m_scale = scale;
    m_sampleRate = sampleRate;
    m_edges.resize(bars + 1);
    m_tiltDb.resize(bars);
    for (std::size_t edge = 0; edge <= bars; ++edge) {
        m_edges[edge] = edgeHz(edge, bars) * static_cast<double>(transformSize) / sampleRate;
    }
    for (std::size_t bar = 0; bar < bars; ++bar) {
        const double low = edgeHz(bar, bars);
        const double high = edgeHz(bar + 1, bars);
        const double middle = scale == BarScale::Logarithmic ? std::sqrt(low * high) : (low + high) / 2.0;
        m_tiltDb[bar] = static_cast<float>(tiltDbPerOctave * std::log2(middle / kTiltPivotHz));
    }
}

double BarMap::edgeHz(std::size_t edge, std::size_t bars) const noexcept
{
    const double along = static_cast<double>(edge) / static_cast<double>(bars);
    const double top = highestHz(m_sampleRate);
    return m_scale == BarScale::Logarithmic ? kLowestHz * std::pow(top / kLowestHz, along)
                                            : kLowestHz + (top - kLowestHz) * along;
}

double BarMap::edgeHz(std::size_t edge) const noexcept
{
    return edgeHz(edge, bars());
}

std::size_t BarMap::barOf(double hz) const noexcept
{
    const auto count = static_cast<double>(bars());
    const auto bar = static_cast<std::size_t>(positionOf(hz, m_scale, m_sampleRate) * count);
    return std::min(bar, bars() - 1);
}

void BarMap::read(std::span<const float> power, std::span<float> levelsDb) const noexcept
{
    if (power.size() < 2) {
        return;
    }
    const std::size_t lastBin = power.size() - 1;
    const auto at = [&](double position) {
        const auto below = std::min(static_cast<std::size_t>(position), lastBin - 1);
        const auto share = static_cast<float>(position - static_cast<double>(below));
        return power[below] + (power[below + 1] - power[below]) * share;
    };
    for (std::size_t bar = 0; bar < bars(); ++bar) {
        const double from = m_edges[bar];
        const double to = std::min(m_edges[bar + 1], static_cast<double>(lastBin));
        float highest = std::max(at(from), at(to));
        const auto firstInside = static_cast<std::size_t>(std::ceil(from));
        const auto lastInside = static_cast<std::size_t>(to);
        for (std::size_t bin = firstInside; bin <= lastInside; ++bin) {
            highest = std::max(highest, power[bin]);
        }
        const float db = highest > 0.0f ? 10.0f * std::log10(highest) : kSilenceDb;
        levelsDb[bar] = std::max(db, kSilenceDb) + m_tiltDb[bar];
    }
}

} // namespace rb::dsp
