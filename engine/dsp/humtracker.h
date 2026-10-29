// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "rt/realtime.h"

#include <array>
#include <complex>
#include <cstdint>

namespace rb::dsp
{

/// Follows the base frequency of a mains hum whose supply, or whose tape,
/// drifts, for Dehum's Auto setting.
///
/// **A phase-locked loop on the fundamental.** An oscillator at the loop's
/// frequency turns the input down to around 0 Hz; a low-pass there keeps
/// the fundamental and throws away everything else, its harmonics and its
/// mirror image included, and the phase of what is left is how far the
/// oscillator has slipped from the hum. A proportional-and-integral loop
/// turns that into a frequency. It is updated a few thousand times a second,
/// not every sample: the low-pass has done the fine work by then.
///
/// **Wide to find it, narrower to follow it.** Until it has locked the loop
/// is kWideLoopHz wide, which pulls in a hum a couple of hertz off within a
/// second. Once locked it narrows as the phase gets noisier, down to
/// kNarrowLoopHz, so a hum buried in other sound is followed calmly and a
/// clean one quickly.
///
/// **Cross-checked against the 2nd and 3rd harmonics.** They are turned down
/// the same way, at twice and three times the loop's phase. How fast each
/// one's phase turns, divided by its number, is what they say the base is
/// off by; weighted by how strong each is, it helps steer the loop, and
/// while it disagrees with the loop by more than kAgreementHz the estimate
/// holds rather than follow.
///
/// **Locked or holding.** The loop counts as locked once the fundamental's
/// phase has kept still for a quarter of a second (the mean of cos 2e, e
/// being the phase error): noise and program keep it turning, a hum does
/// not. Until then, and again whenever it starts turning (silence, or
/// program with nothing steady at the base), estimate() holds where it was
/// and the loop is drawn back towards it.
///
/// **Never a step.** estimate() glides to what the loop reports, and to a
/// new starting point from aim(), over kGlideSeconds.
///
/// Allocates nothing, and does the same whatever the cycle length: every
/// update is counted in samples.
class HumTracker
{
public:
    static constexpr double kLowestHz = 48.0;
    static constexpr double kHighestHz = 62.0;
    /// How often the loop is updated, near enough: the sample rate over a
    /// whole number of samples.
    static constexpr double kLoopRateHz = 3000.0;
    /// The low-pass after turning down: three poles at 20 Hz. Twice the
    /// fundamental (its image) is 50 dB down, the next harmonic 30.
    static constexpr double kDemodulatorHz = 20.0;
    static constexpr double kWideLoopHz = 3.0;
    static constexpr double kNarrowLoopHz = 0.4;
    /// The phase variance, in rad², at which the loop is half as wide.
    static constexpr double kNoiseReference = 0.01;
    static constexpr double kGlideSeconds = 0.02;
    static constexpr double kAgreementHz = 0.05;

    /// Sets up for \a sampleRate. Allocates nothing; meant for prepare().
    void prepare(double sampleRate) noexcept;

    /// Forgets everything and starts from \a startHz, holding it.
    void reset(double startHz) noexcept RB_NONBLOCKING;

    /// Starts looking from \a hz: the loop is re-aimed there, and while it
    /// has nothing locked the estimate glides there too.
    void aim(double hz) noexcept RB_NONBLOCKING;

    /// One sample of the input, both channels mixed.
    void push(float sample) noexcept RB_NONBLOCKING;

    /// The base frequency to put the notches on, in Hz.
    [[nodiscard]] double estimate() const noexcept RB_NONBLOCKING
    {
        return m_estimate;
    }

    /// Whether the loop is locked onto a hum; false means estimate() is
    /// holding.
    [[nodiscard]] bool locked() const noexcept RB_NONBLOCKING
    {
        return m_locked;
    }

    /// The loop's own frequency right now, locked or not: for a test.
    [[nodiscard]] double loopHz() const noexcept
    {
        return m_loopHz;
    }

private:
    static constexpr std::size_t kPoles = 3;
    static constexpr std::size_t kHarmonics = 3;

    using Lowpass = std::array<std::complex<double>, kPoles>;

    void update() noexcept RB_NONBLOCKING;
    void retune() noexcept RB_NONBLOCKING;

    double m_sampleRate = 48000.0;
    std::uint32_t m_decimation = 16;
    std::uint32_t m_count = 0;
    double m_updateSeconds = 16.0 / 48000.0;

    // Per sample.
    double m_lowpass = 0.0;
    std::complex<double> m_phasor{1.0, 0.0};
    std::complex<double> m_step{1.0, 0.0};
    std::array<Lowpass, kHarmonics> m_demodulated{};

    // Per update.
    double m_lockAverage = 0.0;
    double m_lockCoefficient = 0.0;
    double m_driftCoefficient = 0.0;
    double m_crossCoefficient = 0.0;
    double m_powerCoefficient = 0.0;
    std::array<double, kHarmonics> m_averagePower{};
    double m_glideCoefficient = 0.0;
    double m_rippleCoefficient = 0.0;
    double m_smoothError = 0.0;
    double m_smoothStillness = 0.0;
    std::array<std::complex<double>, kHarmonics> m_previous{};
    double m_integral = 50.0;
    double m_loopHz = 50.0;
    double m_estimate = 50.0;
    double m_held = 50.0;
    /// How fast the integral is moving, Hz a second.
    double m_drift = 0.0;
    double m_crossHz = 0.0;
    std::uint32_t m_steady = 0;
    std::uint32_t m_confirmUpdates = 1;
    std::uint32_t m_lockedFor = 0;
    std::uint32_t m_settleUpdates = 1;
    bool m_locked = false;
};

} // namespace rb::dsp
