// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "stream/visuals.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rb::stream
{

namespace
{

/// The spectrum's length: 85 ms at 48 kHz, which resolves the bass bands
/// and still follows a beat.
constexpr std::size_t kFftSize = 4096;

/// The bottom of every scale, in dBFS.
constexpr float kFloorDb = -60.0f;

/// How fast the peak and the spectrum fall back, in the scale's units a
/// second: from the top to the floor in a second and a half.
constexpr float kFallPerSecond = 1.0f / 1.5f;

[[nodiscard]] float scaled(float amplitude) noexcept
{
    if (amplitude <= 0.0f) {
        return 0.0f;
    }
    const float db = 20.0f * std::log10(amplitude);
    return std::clamp((db - kFloorDb) / -kFloorDb, 0.0f, 1.0f);
}

} // namespace

AudioAnalyser::AudioAnalyser(std::uint32_t sampleRate)
    : m_sampleRate(std::max<std::uint32_t>(sampleRate, 8000))
    , m_ring(kFftSize, 0.0f)
    , m_window(kFftSize)
    , m_frame(kFftSize)
{
    m_fft.prepare(kFftSize);
    m_bins.resize(m_fft.bins());
    for (std::size_t index = 0; index < kFftSize; ++index) {
        m_window[index] = static_cast<float>(
            0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(index) / static_cast<double>(kFftSize)));
    }
    // **Spaced as hearing spaces them**: equal steps of the logarithm of the
    // frequency, from the bottom of a bass guitar to where most streams
    // stop, each band at least one bin wide.
    const double lowest = 40.0;
    const double highest = std::min(16000.0, m_sampleRate / 2.0);
    const double binHz = static_cast<double>(m_sampleRate) / static_cast<double>(kFftSize);
    std::size_t previous = 0;
    for (std::size_t band = 0; band <= kSpectrumBands; ++band) {
        const double hz = lowest * std::pow(highest / lowest, static_cast<double>(band) / kSpectrumBands);
        std::size_t bin =
            std::clamp<std::size_t>(static_cast<std::size_t>(std::lround(hz / binHz)), 1, m_bins.size() - 1);
        if (band > 0) {
            bin = std::max(bin, previous + 1);
        }
        bin = std::min(bin, m_bins.size() - 1);
        m_bandEdges.push_back(bin);
        previous = bin;
    }
    m_visuals.waveform.assign(kWaveformSamples, 0.0f);
    m_visuals.spectrum.assign(kSpectrumBands, 0.0f);
}

void AudioAnalyser::feed(std::span<const float> samples, std::uint32_t channels) noexcept
{
    if (channels == 0) {
        return;
    }
    const std::size_t frames = samples.size() / channels;
    const std::size_t window = m_sampleRate / 10;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const float left = samples[frame * channels];
        const float right = channels > 1 ? samples[frame * channels + 1] : left;
        m_ring[m_written % kFftSize] = 0.5f * (left + right);
        ++m_written;
        m_squares[0] += static_cast<double>(left) * left;
        m_squares[1] += static_cast<double>(right) * right;
        m_heldPeak[0] = std::max(m_heldPeak[0], scaled(std::abs(left)));
        m_heldPeak[1] = std::max(m_heldPeak[1], scaled(std::abs(right)));
        if (++m_squaredFrames >= window) {
            for (std::size_t side = 0; side < 2; ++side) {
                m_lastRms[side] = scaled(static_cast<float>(std::sqrt(m_squares[side] / static_cast<double>(window))));
                m_squares[side] = 0.0;
            }
            m_squaredFrames = 0;
        }
    }
    m_framesSinceVisuals += frames;
}

const AudioVisuals &AudioAnalyser::visuals() noexcept
{
    const float elapsed = static_cast<float>(m_framesSinceVisuals) / static_cast<float>(m_sampleRate);
    m_framesSinceVisuals = 0;
    const float fall = kFallPerSecond * elapsed;

    for (std::size_t index = 0; index < kWaveformSamples; ++index) {
        m_visuals.waveform[index] = m_ring[(m_written + kFftSize - kWaveformSamples + index) % kFftSize];
    }

    for (std::size_t index = 0; index < kFftSize; ++index) {
        m_frame[index] = m_ring[(m_written + index) % kFftSize] * m_window[index];
    }
    m_fft.forward(m_frame, m_bins);
    // A full-scale sine through a Hann window peaks at a quarter of the
    // length in one bin; that is 0 dBFS.
    const float reference = static_cast<float>(kFftSize) / 4.0f;
    for (std::size_t band = 0; band < kSpectrumBands; ++band) {
        float strongest = 0.0f;
        for (std::size_t bin = m_bandEdges[band]; bin < std::max(m_bandEdges[band + 1], m_bandEdges[band] + 1); ++bin) {
            strongest = std::max(strongest, std::abs(m_bins[bin]));
        }
        const float now = scaled(strongest / reference);
        m_visuals.spectrum[band] = std::max(now, m_visuals.spectrum[band] - fall);
    }

    for (std::size_t side = 0; side < 2; ++side) {
        m_visuals.level[side] = m_lastRms[side];
        m_visuals.peak[side] = std::max(m_heldPeak[side], m_visuals.peak[side] - fall);
        m_heldPeak[side] = 0.0f;
    }
    return m_visuals;
}

} // namespace rb::stream
