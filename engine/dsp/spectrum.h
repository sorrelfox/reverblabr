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
#include <cstddef>
#include <span>
#include <vector>

namespace rb::dsp
{

/// What a signal's spectrum looks like, in bands a person can read:
/// the live spectrum behind a curve editor.
///
/// **Not for the audio thread, and not needing to be.** It is given samples
/// that the audio thread copied out (rt::AudioTap) and works on them on the
/// engine thread, sixty times a second at most; the audio thread's only cost
/// is the copy, and only while somebody is looking.
///
/// **4096-point frames, Hann-windowed, half overlapping**, each one's power
/// folded into kBands bands spaced evenly in octaves from kLowestHz to
/// kHighestHz, and averaged across frames with a time constant of
/// kAveragingMs, so the picture settles rather than flickers.
///
/// **Transformed four times as long as they are**, the frame padded with
/// silence: the same frame's spectrum, but read every quarter of a bin, so
/// the window's hump round a low sine is drawn in its shape rather than in
/// four steps 12 Hz wide. Without it a 31 Hz sine peaked in the band at
/// 40 Hz, since every band below about 150 Hz is narrower than a bin.
///
/// **A band is the power in it, not a bin's.** Each bin stands for the
/// stretch of frequencies half a bin either side of it, and a band takes
/// each bin in proportion to how much of that stretch falls inside it. So a
/// band narrower than a bin — every band below about 150 Hz at 48 kHz — is
/// the share of the bin's power its width is owed, and noise whose power
/// falls by 3 dB an octave (pink) reads the same in every band.
///
/// **Decibels against a full-scale sine**: a sine peaking at 1 reads 0 dB in
/// the band it falls in; silence reads kFloorDb.
class SpectrumAnalyser
{
public:
    static constexpr std::size_t kFrameSize = 4096;
    static constexpr std::size_t kFftSize = kFrameSize * 4;
    static constexpr std::size_t kHop = kFrameSize / 2;
    static constexpr std::size_t kBands = 96;
    static constexpr double kLowestHz = 20.0;
    static constexpr double kHighestHz = 20000.0;
    static constexpr double kAveragingMs = 250.0;
    static constexpr float kFloorDb = -120.0f;

    SpectrumAnalyser();

    /// Makes the tables for \a sampleRate and forgets anything heard.
    /// Allocates; not for the audio thread.
    void prepare(double sampleRate);
    [[nodiscard]] double sampleRate() const noexcept
    {
        return m_sampleRate;
    }

    /// Forgets the signal so far, for a start with nothing on the screen.
    void reset();

    /// Adds \a samples, one channel. Works out a frame each time kHop more
    /// have arrived.
    void push(std::span<const float> samples);

    /// Each band's level now, in dB, low to high.
    [[nodiscard]] const std::array<float, kBands> &bandsDb() const noexcept
    {
        return m_bandsDb;
    }

    /// Where band \a band is centred, and where it starts and ends, in Hz.
    [[nodiscard]] static double bandCentreHz(std::size_t band) noexcept;
    [[nodiscard]] static double bandEdgeHz(std::size_t edge) noexcept;

    /// How many frames have been worked out since prepare() or reset().
    [[nodiscard]] std::size_t frames() const noexcept
    {
        return m_frames;
    }

private:
    void analyseFrame();

    double m_sampleRate = 48000.0;
    RealFft m_fft;
    std::vector<float> m_window;
    /// The last kFrameSize samples, oldest first, and how many are new since
    /// the last frame.
    std::vector<float> m_history;
    std::size_t m_fresh = 0;
    std::vector<float> m_frame;
    std::vector<std::complex<float>> m_spectrum;
    /// Each band's power, averaged.
    std::array<double, kBands> m_power{};
    std::array<float, kBands> m_bandsDb{};
    /// How much of the average a new frame is.
    double m_blend = 1.0;
    /// Scales a bin's |X|² to its share of the signal's mean square.
    double m_scale = 1.0;
    std::size_t m_frames = 0;
};

} // namespace rb::dsp
