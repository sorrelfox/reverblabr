// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/barspectrum.h"
#include "rt/scope.h"
#include "scopeview.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rb::ui
{

/// What a surface was asked to show: a Spectrum block's settings as they
/// reach the drawing.
struct SpectrumSettings {
    double refreshHz = 30.0;
    bool smooth = false;
    std::size_t bars = 64;
    dsp::BarScale scale = dsp::BarScale::Logarithmic;
    std::size_t frameSize = 4096;
    double tiltDb = 0.0;
    /// Which trace of a slot is read.
    std::size_t trace = rt::ScopeMid;
};

/// **The spectrum of one look at a slot**, kept by ScopeFeed for every surface
/// showing the block, so that it is worked out once. It runs in the window
/// and only when a surface draws, so the audio thread never pays for it.
///
/// It takes every sample from the finest ring, and takes nothing from a
/// coarser one: extremes of every eighth sample have no spectrum to speak of,
/// and what they seem to have would be wrong.
///
/// **No sound falls between two looks.** A look brings however much sound went
/// by since the last, which a slow refresh and a short frame can make many
/// frames' worth, so it takes as many half-overlapping frames as cover it, as
/// far back as the ring reaches, and each bin keeps the highest. A click reads
/// the same at any refresh rate, and the work per second depends on the frame
/// size and not on the refresh.
class SpectrumAnalysis
{
public:
    /// Works out the spectrum of \a frame's trace \a trace, in frames of
    /// \a frameSize, unless that has been done.
    void update(const ScopeFrame &frame, std::size_t frameSize, std::size_t trace);

    /// Power in each bin, once there is enough sound to fill a frame.
    [[nodiscard]] bool isReady() const noexcept
    {
        return m_ready;
    }
    [[nodiscard]] std::span<const float> power() const noexcept
    {
        return m_spectrum.power();
    }
    [[nodiscard]] std::size_t transformSize() const noexcept
    {
        return m_spectrum.transformSize();
    }
    [[nodiscard]] std::uint32_t sampleRate() const noexcept
    {
        return m_sampleRate;
    }
    /// Counts up with each spectrum.
    [[nodiscard]] std::uint64_t serial() const noexcept
    {
        return m_serial;
    }
    /// Counts up with each sound the bins do not continue.
    [[nodiscard]] std::uint64_t run() const noexcept
    {
        return m_run;
    }
    /// How many transforms have been done, for a test.
    [[nodiscard]] std::uint64_t transforms() const noexcept
    {
        return m_transforms;
    }

private:
    dsp::FrameSpectrum m_spectrum;
    std::vector<float> m_samples;
    std::size_t m_trace = 0;
    std::uint32_t m_sampleRate = 0;
    bool m_ready = false;
    double m_analysedTo = 0.0;
    std::uint64_t m_seenSerial = 0;
    std::uint64_t m_seenRun = 0;
    std::uint64_t m_serial = 0;
    std::uint64_t m_run = 0;
    std::uint64_t m_transforms = 0;
};

/// **What one surface draws**, worked out from a spectrum: how high each bar
/// stands, and where its peak mark is. Knows no window, so a test can run it
/// with a clock of its own. One a surface, since a tile has room for fewer
/// bars than a card.
///
/// **What Real-time and Smooth mean here.** Real-time works out a picture when
/// a refresh brings news, with each bar where the spectrum says, and leaves it
/// until the next. Smooth works one out at every frame the screen draws, and
/// each bar travels towards where the spectrum says: quickly when it rises, so
/// that an attack is not late, and easing down when it falls. Neither depends
/// on how fast the screen is: a slower one takes bigger steps of the same
/// curve.
class SpectrumView
{
public:
    /// How fast a smooth bar goes, as time constants in milliseconds: never
    /// quicker than this, and over as long as a refresh, so that a slow
    /// refresh still moves instead of jumping.
    static constexpr double kRiseMs = 25.0;
    static constexpr double kFallMs = 350.0;
    /// How much of a refresh a rise takes, and a fall, at the least.
    static constexpr double kRiseRefreshes = 0.5;
    static constexpr double kFallRefreshes = 3.0;
    /// How long a peak mark stays, in milliseconds, and how fast it then
    /// falls, in dB a second.
    static constexpr double kPeakHoldMs = 1000.0;
    static constexpr double kPeakFallDbPerSecond = 20.0;
    /// The top of the view, where 0 dBFS is.
    static constexpr double kTopDb = 0.0;
    /// What a bar keeps track of: from the lowest floor that can be chosen,
    /// which is only the drawing's, to as far above the top as a tilt lifts
    /// it. A bar falling to silence takes as long whatever the floor.
    static constexpr float kLowestDb = -120.0f;
    static constexpr float kHighestDb = 60.0f;

    /// Works out the picture at \a nowMs. Without news a Real-time view stays
    /// as it was. Returns whether the picture changed.
    bool update(const SpectrumAnalysis &analysis, const SpectrumSettings &settings, double nowMs);

    /// Forgets the picture: a view starting again shows no easing from the
    /// last thing it saw.
    void clear() noexcept;

    [[nodiscard]] std::size_t bars() const noexcept
    {
        return m_shown.size();
    }
    /// Each bar's level now, and its peak mark's, in dB. Empty until a frame
    /// has been filled.
    [[nodiscard]] std::span<const float> levels() const noexcept
    {
        return m_shown;
    }
    [[nodiscard]] std::span<const float> peaks() const noexcept
    {
        return m_peaks;
    }
    /// What the bars stand on now.
    [[nodiscard]] const dsp::BarMap &map() const noexcept
    {
        return m_map;
    }

    /// How far up the height a level is, from nought at \a floorDb to one at
    /// the top.
    [[nodiscard]] static float heightOf(float levelDb, double floorDb) noexcept;

private:
    void movePeaks(double dtMs, double nowMs);

    dsp::BarMap m_map;
    SpectrumSettings m_seen;
    std::size_t m_seenTransform = 0;
    std::uint32_t m_seenRate = 0;
    std::uint64_t m_seenSerial = 0;
    std::uint64_t m_seenRun = 0;
    double m_lastMs = 0.0;
    std::vector<float> m_target;
    std::vector<float> m_shown;
    std::vector<float> m_peaks;
    std::vector<double> m_peakSinceMs;
};

} // namespace rb::ui
