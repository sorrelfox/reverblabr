// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "dsp/humtracker.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rb::dsp
{

namespace
{

constexpr double kTwoPi = 2.0 * std::numbers::pi;

/// How long the phase's stillness is averaged over.
constexpr double kLockSeconds = 0.25;
/// How long both signs of a hum have to hold before it counts as locked.
constexpr double kConfirmSeconds = 0.25;
/// Locked when the mean of cos 2e passes this, unlocked when it falls under
/// kUnlockStillness.
constexpr double kLockStillness = 0.6;
constexpr double kUnlockStillness = 0.3;
/// The estimate only follows while the phase is at least this still.
constexpr double kFollowStillness = 0.8;
/// The fundamental's level (after turning down, so half its amplitude)
/// below which there is nothing to follow: about −100 dBFS.
constexpr double kQuietest = 1.0e-5;
/// How strongly the harmonics steer, as a bandwidth.
constexpr double kHarmonicHz = 1.0;
/// How fast an unlocked loop is drawn back to what the estimate holds.
constexpr double kReturnPerSecond = 0.5;
/// The loop's damping.
constexpr double kDamping = 1.0;
/// How long the loop's drift, and the harmonics' say, are averaged over.
constexpr double kDriftSeconds = 0.3;
constexpr double kCrossSeconds = 0.1;
/// **Settled** — locked for this long — the estimate glides more slowly the
/// noisier the phase: kGlideSeconds times 1 + variance / kGlideNoise, up to
/// kLongestGlideSeconds. A clean hum's variance is next to nothing, so it is
/// followed as fast as ever; one heard through other sound is averaged.
constexpr double kSettleSeconds = 1.0;
constexpr double kGlideNoise = 0.0005;
constexpr double kLongestGlideSeconds = 0.5;
/// The phase error is smoothed over this before its variance is taken for
/// the glide: it takes out most of the ripple the next harmonic leaves at
/// the fundamental's own frequency, which is not noise, and little of what
/// is.
constexpr double kRippleSeconds = 0.01;
/// How long the fundamental's and the harmonics' power are averaged over,
/// for how much each harmonic is heard. **Not the
/// power of the moment**: a tone near a harmonic makes its power swell as
/// its phase turns one way, and weighting what a harmonic says by that
/// would pull the estimate towards the tone.
constexpr double kPowerSeconds = 0.1;
/// The harmonics' share of the power below which they are not asked.
constexpr double kHarmonicShare = 0.2;

[[nodiscard]] double clampHz(double hz) noexcept
{
    return std::clamp(hz, HumTracker::kLowestHz, HumTracker::kHighestHz);
}

} // namespace

void HumTracker::prepare(double sampleRate) noexcept
{
    m_sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    m_decimation = static_cast<std::uint32_t>(std::max(1L, std::lround(m_sampleRate / kLoopRateHz)));
    m_updateSeconds = static_cast<double>(m_decimation) / m_sampleRate;
    m_lowpass = std::exp(-kTwoPi * kDemodulatorHz / m_sampleRate);
    m_lockCoefficient = std::exp(-m_updateSeconds / kLockSeconds);
    m_driftCoefficient = 1.0 - std::exp(-m_updateSeconds / kDriftSeconds);
    m_crossCoefficient = 1.0 - std::exp(-m_updateSeconds / kCrossSeconds);
    m_powerCoefficient = 1.0 - std::exp(-m_updateSeconds / kPowerSeconds);
    m_glideCoefficient = 1.0 - std::exp(-m_updateSeconds / kGlideSeconds);
    m_settleUpdates = static_cast<std::uint32_t>(std::lround(kSettleSeconds / m_updateSeconds));
    m_rippleCoefficient = 1.0 - std::exp(-m_updateSeconds / kRippleSeconds);
    m_confirmUpdates = static_cast<std::uint32_t>(std::max(1L, std::lround(kConfirmSeconds / m_updateSeconds)));
    reset(m_estimate);
}

void HumTracker::reset(double startHz) noexcept RB_NONBLOCKING
{
    const double start = clampHz(startHz);
    m_count = 0;
    m_phasor = {1.0, 0.0};
    for (Lowpass &lowpass : m_demodulated) {
        lowpass.fill({0.0, 0.0});
    }
    m_previous.fill({0.0, 0.0});
    m_averagePower.fill(0.0);
    m_lockAverage = 0.0;
    m_integral = start;
    m_loopHz = start;
    m_estimate = start;
    m_held = start;
    m_drift = 0.0;
    m_crossHz = 0.0;
    m_steady = 0;
    m_lockedFor = 0;
    m_smoothError = 0.0;
    m_smoothStillness = 0.0;
    m_locked = false;
    retune();
}

void HumTracker::aim(double hz) noexcept RB_NONBLOCKING
{
    const double start = clampHz(hz);
    m_integral = start;
    m_loopHz = start;
    if (!m_locked) {
        m_held = start;
    }
    retune();
}

void HumTracker::retune() noexcept RB_NONBLOCKING
{
    m_step = std::polar(1.0, kTwoPi * m_loopHz / m_sampleRate);
}

void HumTracker::push(float sample) noexcept RB_NONBLOCKING
{
    const auto x = static_cast<double>(sample);

    // Turned down by the loop's phase, and by twice and three times it.
    const std::complex<double> down = std::conj(m_phasor);
    std::complex<double> turn = down;
    for (Lowpass &lowpass : m_demodulated) {
        std::complex<double> value = x * turn;
        for (std::complex<double> &pole : lowpass) {
            pole = value + (pole - value) * m_lowpass;
            value = pole;
        }
        turn *= down;
    }
    m_phasor *= m_step;

    if (++m_count >= m_decimation) {
        m_count = 0;
        update();
    }
}

void HumTracker::update() noexcept RB_NONBLOCKING
{
    // The oscillator's length drifts by rounding over millions of turns.
    m_phasor /= std::abs(m_phasor);

    const std::complex<double> fundamental = m_demodulated[0].back();
    const double level = std::abs(fundamental);
    for (std::size_t index = 0; index < kHarmonics; ++index) {
        m_averagePower[index] += m_powerCoefficient * (std::norm(m_demodulated[index].back()) - m_averagePower[index]);
    }
    const bool live = level > kQuietest;
    const double error = live ? std::arg(fundamental) : 0.0;

    // The 2nd and 3rd harmonics: how far off each says the base is.
    double harmonicPower = 0.0;
    double harmonicSays = 0.0;
    for (std::size_t index = 1; index < kHarmonics; ++index) {
        const std::complex<double> harmonic = m_demodulated[index].back();
        const double power = m_averagePower[index];
        if (std::abs(harmonic) > kQuietest && std::abs(m_previous[index]) > kQuietest) {
            const double says = std::arg(harmonic * std::conj(m_previous[index]))
                / (kTwoPi * m_updateSeconds * static_cast<double>(index + 1));
            harmonicSays += power * says;
            harmonicPower += power;
        }
        m_previous[index] = harmonic;
    }
    const double totalPower = m_averagePower[0] + harmonicPower;
    const double harmonicShare = totalPower > 0.0 ? harmonicPower / totalPower : 0.0;
    harmonicSays = harmonicPower > 0.0 ? harmonicSays / harmonicPower : 0.0;
    m_crossHz += m_crossCoefficient * (harmonicSays - m_crossHz);

    // Locked or holding.
    m_smoothError += m_rippleCoefficient * (error - m_smoothError);
    m_smoothStillness = m_lockCoefficient * m_smoothStillness
        + (1.0 - m_lockCoefficient) * (live ? std::cos(2.0 * m_smoothError) : 0.0);
    m_lockAverage =
        m_lockCoefficient * m_lockAverage + (1.0 - m_lockCoefficient) * (live ? std::cos(2.0 * error) : 0.0);
    if (m_locked) {
        if (m_lockAverage < kUnlockStillness || !live) {
            m_locked = false;
            m_lockedFor = 0;
            m_held = m_estimate;
        }
    } else {
        const bool steady = live && m_lockAverage > kLockStillness;
        m_steady = steady ? m_steady + 1 : 0;
        if (m_steady >= m_confirmUpdates) {
            m_locked = true;
            m_steady = 0;
            m_drift = 0.0;
        }
    }

    // The loop: as wide as the phase is still.
    double bandwidth = kWideLoopHz;
    double variance = 0.0;
    if (m_locked) {
        variance = -std::log(std::max(m_lockAverage, 1.0e-6)) / 2.0;
        bandwidth = std::max(kNarrowLoopHz, kWideLoopHz / (1.0 + variance / kNoiseReference));
    }
    const double natural = kTwoPi * bandwidth;
    const double proportional = 2.0 * kDamping * natural / kTwoPi;
    const double integral = natural * natural / kTwoPi * m_updateSeconds;
    const double before = m_integral;
    m_integral += integral * error + kTwoPi * kHarmonicHz * m_updateSeconds * harmonicShare * harmonicSays;
    if (!m_locked) {
        m_integral += kReturnPerSecond * m_updateSeconds * (m_held - m_integral);
    }
    m_integral = clampHz(m_integral);
    m_loopHz = clampHz(m_integral + proportional * error);
    retune();

    // The estimate. **The integral, less the loop's lag behind a drift**: a
    // loop like this one follows a frequency moving at r Hz a second with
    // its integral 2ζr/ωn behind, and the proportional term making up the
    // rest out of a phase error that is mostly noise. Working the lag out
    // from how fast the integral itself moves keeps the noise out of it.
    m_drift += m_driftCoefficient * ((m_integral - before) / m_updateSeconds - m_drift);
    const bool agreed = harmonicShare < kHarmonicShare || std::abs(m_crossHz) < kAgreementHz;
    if (m_locked) {
        m_lockedFor = std::min(m_lockedFor + 1, m_settleUpdates);
        double glide = m_glideCoefficient;
        if (m_lockedFor >= m_settleUpdates) {
            const double noise = -std::log(std::max(m_smoothStillness, 1.0e-6)) / 2.0;
            const double seconds = std::min(kLongestGlideSeconds, kGlideSeconds * (1.0 + noise / kGlideNoise));
            glide = 1.0 - std::exp(-m_updateSeconds / seconds);
        }
        if (m_lockAverage > kFollowStillness && agreed) {
            m_estimate += glide * (clampHz(m_integral + 2.0 * kDamping * m_drift / natural) - m_estimate);
        }
        m_held = m_estimate;
    } else {
        m_estimate += m_glideCoefficient * (m_held - m_estimate);
    }
}

} // namespace rb::dsp
