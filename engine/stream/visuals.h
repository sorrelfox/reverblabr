// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/fft.h"

#include <array>
#include <complex>
#include <cstdint>
#include <span>
#include <vector>

namespace rb::stream
{

/// **What a frame of the video shows of the sound**: measured from the
/// audio the stream sends, on the stream's thread, so the picture moves
/// with exactly what is heard and the audio thread does nothing more
/// for it.
struct AudioVisuals {
    /// The most recent stretch of the sound, both sides mixed, oldest first,
    /// from -1 to 1: what a waveform draws.
    std::vector<float> waveform;
    /// How loud each band is, from 0 (silence, or -60 dBFS) to 1 (0 dBFS),
    /// lowest first, spaced as hearing spaces them.
    std::vector<float> spectrum;
    /// Each side's level, from 0 to 1 on the same scale: the RMS over the
    /// last tenth of a second, and the peak, which falls back slowly.
    std::array<float, 2> level{};
    std::array<float, 2> peak{};
};

/// Measures the audio a stream sends for the picture: a ring of the last
/// samples for the waveform, a spectrum of the last 4096 of them, and the
/// levels with a meter's fall-back. Everything is made when it is; feed()
/// and visuals() then allocate nothing.
class AudioAnalyser
{
public:
    /// How many samples the waveform shows, and how many bands the
    /// spectrum has.
    static constexpr std::size_t kWaveformSamples = 2048;
    static constexpr std::size_t kSpectrumBands = 48;

    explicit AudioAnalyser(std::uint32_t sampleRate);

    [[nodiscard]] std::uint32_t sampleRate() const noexcept
    {
        return m_sampleRate;
    }

    /// Interleaved frames of \a channels channels at the rate given.
    void feed(std::span<const float> samples, std::uint32_t channels) noexcept;

    /// What the frame drawn now shows. **Falls back with time**, not with
    /// calls: the peak and the spectrum come down at a rate a second.
    [[nodiscard]] const AudioVisuals &visuals() noexcept;

private:
    std::uint32_t m_sampleRate;
    std::vector<float> m_ring;
    std::size_t m_written = 0;
    /// Per side: the sum of squares over the window, and how far into it.
    std::array<double, 2> m_squares{};
    std::size_t m_squaredFrames = 0;
    std::array<float, 2> m_lastRms{};
    std::array<float, 2> m_heldPeak{};
    std::uint64_t m_framesSinceVisuals = 0;
    dsp::RealFft m_fft;
    std::vector<float> m_window;
    std::vector<float> m_frame;
    std::vector<std::complex<float>> m_bins;
    /// The first bin of each band, and one past the last band's.
    std::vector<std::size_t> m_bandEdges;
    AudioVisuals m_visuals;
};

} // namespace rb::stream
