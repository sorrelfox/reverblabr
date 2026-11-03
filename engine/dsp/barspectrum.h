// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/fft.h"

#include <complex>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rb::dsp
{

/// **The spectrum of a short stretch of sound, read off as peaks**: what a
/// Spectrum block's bars stand on. Not for the audio thread: it runs in a
/// window, on samples the audio thread left in a scope slot.
///
/// SpectrumAnalyser answers another question. It folds a fixed frame into
/// fixed bands by the power each band holds, and averages a quarter of a
/// second, which is right behind a curve editor. Here the frame size, the
/// bars and the motion are the person's, each picture stands alone, and a bar
/// should read what a level meter would: a sine is a sine however wide its
/// bar is. So only the transform is shared.
///
/// **Hann-windowed frames, read as amplitudes**: each bin is scaled so that
/// a sine peaking at 1 reads 1 at its peak, whatever the frame size. Noise
/// with equal power per hertz reads level across the bars.
///
/// **Transformed four times as long as they are**, the frame padded with
/// silence, up to kLargestTransform: the same spectrum, read more often, so
/// that a sine between two bins does not read up to 1.4 dB low, and the hump
/// of a low sine has a shape. A frame of 8192 gets twice and not four times,
/// which halves what the largest setting costs for a third of a decibel.
/// kAccuracyDb is what that comes to.
///
/// **A frame needs one sample fewer than its size**, because the periodic
/// Hann window starts at nought. That is what lets 8192 be read from a ring
/// that gives 8191.
class FrameSpectrum
{
public:
    static constexpr std::size_t kSmallestFrame = 256;
    static constexpr std::size_t kLargestFrame = 8192;
    static constexpr std::size_t kPadding = 4;
    static constexpr std::size_t kLargestTransform = 16384;
    /// How far from nought a full-scale sine can read, in dB, from
    /// kNearestBins bins above 0 Hz up to as many below half the rate. Nearer
    /// the ends than that, a sine's own mirror image falls under its hump.
    static constexpr double kAccuracyDb = 0.4;
    static constexpr double kNearestBins = 3.0;
    /// What nonsense reads as: 60 dB over full scale.
    static constexpr float kMostPower = 1.0e6f;

    /// Makes the tables for frames of \a frameSize samples, a power of two.
    /// Allocates.
    void prepare(std::size_t frameSize);

    [[nodiscard]] std::size_t frameSize() const noexcept
    {
        return m_frameSize;
    }
    /// How many samples add() takes.
    [[nodiscard]] std::size_t samplesNeeded() const noexcept
    {
        return m_frameSize - 1;
    }
    /// How far apart frames are, in samples, so that no sound falls between
    /// two: half a frame, where the window of one is at its height.
    [[nodiscard]] std::size_t hop() const noexcept
    {
        return m_frameSize / 2;
    }
    [[nodiscard]] std::size_t transformSize() const noexcept
    {
        return m_fft.size();
    }

    /// Forgets what add() brought.
    void clear() noexcept;

    /// Adds the frame of \a samples, samplesNeeded() of them, oldest first.
    /// A bin keeps the highest it has been since clear(), so that a click
    /// stays however many frames are added around it. A sample that is not a
    /// number sends every bin to the top: a block that has blown up should
    /// look it.
    void add(std::span<const float> samples) noexcept;

    /// Power in each bin since clear(), from 0 Hz up: 1 is a full-scale sine.
    [[nodiscard]] std::span<const float> power() const noexcept
    {
        return m_power;
    }

private:
    std::size_t m_frameSize = 0;
    RealFft m_fft;
    std::vector<float> m_window;
    std::vector<float> m_padded;
    std::vector<std::complex<float>> m_spectrum;
    std::vector<float> m_power;
    float m_scale = 1.0f;
};

/// How the bars are spread over the frequencies.
enum class BarScale : std::uint8_t {
    Logarithmic, ///< Each octave as wide as the next, as an ear has them.
    Linear, ///< Each hertz as wide as the next.
};

/// **Which bins each bar reads**: what a bin's power becomes on the screen.
///
/// **A bar reads the highest point of the spectrum across its width**, taking
/// the spectrum as straight lines between its bins. That is one rule for
/// every bar, and it does the two things bars need. A bar with many bins in
/// it gives a sine its full height however wide it is. And a bar narrower
/// than a bin, as every low bar on a logarithmic scale is, reads the line
/// between the two bins either side, so several bars honestly share a bin:
/// none is left empty, and a low sine draws the hump it has, as wide as the
/// frame size makes it, and no narrower.
///
/// **Tilt turns about 1 kHz**, in decibels an octave, for reading music,
/// whose level falls with frequency: a level on the scale it was measured on
/// stays right at 1 kHz.
class BarMap
{
public:
    /// The range shown: up to kHighestHz, or half the rate below that.
    static constexpr double kLowestHz = 20.0;
    static constexpr double kHighestHz = 20000.0;
    static constexpr double kTiltPivotHz = 1000.0;
    /// What silence reads, in dB.
    static constexpr float kSilenceDb = -200.0f;

    /// The top of the range at \a sampleRate.
    [[nodiscard]] static double highestHz(double sampleRate) noexcept;
    /// Where \a hz stands across the range, from nought to one.
    [[nodiscard]] static double positionOf(double hz, BarScale scale, double sampleRate) noexcept;

    /// Makes the table of \a bars bars over a spectrum of \a transformSize.
    /// Allocates.
    void
    prepare(std::size_t bars, BarScale scale, double sampleRate, std::size_t transformSize, double tiltDbPerOctave);

    [[nodiscard]] std::size_t bars() const noexcept
    {
        return m_tiltDb.size();
    }

    /// Where bar \a edge starts, and where the last ends at bars().
    [[nodiscard]] double edgeHz(std::size_t edge) const noexcept;
    /// Which bar holds \a hz.
    [[nodiscard]] std::size_t barOf(double hz) const noexcept;

    /// Each bar's level from \a power, in dB, into \a levelsDb.
    void read(std::span<const float> power, std::span<float> levelsDb) const noexcept;

private:
    [[nodiscard]] double edgeHz(std::size_t edge, std::size_t bars) const noexcept;

    BarScale m_scale = BarScale::Logarithmic;
    double m_sampleRate = 48000.0;
    /// Each edge's place among the bins, bars() + 1 of them.
    std::vector<double> m_edges;
    std::vector<float> m_tiltDb;
};

} // namespace rb::dsp
