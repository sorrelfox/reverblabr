// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "rt/realtime.h"

#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace rb::dsp
{

/// The per-sample factor of an exponential decay with a time constant of
/// \a timeMs: after exactly that time, what is left of a step is 1/e, about
/// 36.8 % of it. This is the usual meaning of an attack or release time in a
/// compressor or a meter. A time of zero or less returns 0, which decays at
/// once.
[[nodiscard]] inline float exponentialDecayCoefficient(float timeMs, double sampleRate) noexcept
{
    const double samples = static_cast<double>(timeMs) * 1.0e-3 * sampleRate;
    if (!(samples > 0.0)) {
        return 0.0f;
    }
    return static_cast<float>(std::exp(-1.0 / samples));
}

/// An attack or release time, kept together with the per-sample coefficient
/// it produces.
///
/// The time in milliseconds is what the user and the presets talk about; the
/// coefficient is what the audio thread uses, and it depends on the sample
/// rate. Keeping both here means a sample-rate change is one prepare() call
/// and no block has to remember how to convert.
class TimeConstant
{
public:
    TimeConstant() = default;

    TimeConstant(float timeMs, double sampleRate) noexcept
    {
        set(timeMs, sampleRate);
    }

    void set(float timeMs, double sampleRate) noexcept
    {
        m_timeMs = timeMs;
        m_sampleRate = sampleRate;
        m_coefficient = exponentialDecayCoefficient(timeMs, sampleRate);
    }

    /// Recomputes the coefficient for a new sample rate, keeping the time.
    void prepare(double sampleRate) noexcept
    {
        set(m_timeMs, sampleRate);
    }

    /// Changes the time, keeping the sample rate.
    void setTimeMs(float timeMs) noexcept
    {
        set(timeMs, m_sampleRate);
    }

    [[nodiscard]] float timeMs() const noexcept RB_NONBLOCKING
    {
        return m_timeMs;
    }

    [[nodiscard]] float coefficient() const noexcept RB_NONBLOCKING
    {
        return m_coefficient;
    }

private:
    float m_timeMs = 0.0f;
    double m_sampleRate = 48000.0;
    float m_coefficient = 0.0f;
};

/// Follows the peak level of a signal: it jumps to a new peak at once and
/// falls away exponentially afterwards. This is what a peak meter's needle
/// and a limiter's detector do.
///
/// The envelope is a linear amplitude, never negative; convert it with
/// gainToDb() for display.
class PeakFollower
{
public:
    /// Sets the sample rate and the release time, and clears the envelope.
    void prepare(double sampleRate, float releaseMs) noexcept
    {
        m_release.set(releaseMs, sampleRate);
        reset();
    }

    /// Changes the release time without disturbing the envelope.
    void setReleaseMs(float releaseMs) noexcept
    {
        m_release.setTimeMs(releaseMs);
    }

    [[nodiscard]] float releaseMs() const noexcept RB_NONBLOCKING
    {
        return m_release.timeMs();
    }

    void reset() noexcept RB_NONBLOCKING
    {
        m_envelope = 0.0f;
    }

    [[nodiscard]] float envelope() const noexcept RB_NONBLOCKING
    {
        return m_envelope;
    }

    /// Feeds one sample. Read the result with envelope().
    void push(float sample) noexcept RB_NONBLOCKING
    {
        const float magnitude = std::abs(sample);
        if (magnitude >= m_envelope) {
            m_envelope = magnitude; // instant attack
        } else {
            m_envelope *= m_release.coefficient();
        }
    }

    /// Feeds a whole cycle and returns the envelope after it.
    float process(std::span<const float> samples) noexcept RB_NONBLOCKING
    {
        for (const float sample : samples) {
            push(sample);
        }
        return m_envelope;
    }

private:
    TimeConstant m_release;
    float m_envelope = 0.0f;
};

/// Follows the root mean square of a signal over a rectangular window of a
/// fixed length: the level of the last \a windowMs milliseconds, with every
/// sample in the window counting the same. That is the measure a loudness
/// readout wants, where a peak follower would jump on a single click.
///
/// The window is allocated in prepare() and never grows, so processing costs
/// the same for every sample whatever the window length.
class RmsFollower
{
public:
    /// Allocates a window of \a windowMs at \a sampleRate and clears it.
    /// Not real-time safe; call it from prepare().
    void prepare(double sampleRate, float windowMs)
    {
        const double samples = static_cast<double>(windowMs) * 1.0e-3 * sampleRate;
        const std::uint32_t length = samples > 1.0 ? static_cast<std::uint32_t>(std::lround(samples)) : 1;
        m_window.assign(length, 0.0f);
        m_sampleRate = sampleRate;
        reset();
    }

    /// The window length in samples, at least one.
    [[nodiscard]] std::uint32_t windowSamples() const noexcept RB_NONBLOCKING
    {
        return static_cast<std::uint32_t>(m_window.size());
    }

    [[nodiscard]] float windowMs() const noexcept RB_NONBLOCKING
    {
        return static_cast<float>(static_cast<double>(m_window.size()) * 1.0e3 / m_sampleRate);
    }

    /// Empties the window, so the level starts again from silence.
    void reset() noexcept RB_NONBLOCKING
    {
        for (float &square : m_window) {
            square = 0.0f;
        }
        m_sum = 0.0;
        m_writeIndex = 0;
    }

    /// The root mean square over the window, as a linear amplitude.
    [[nodiscard]] float level() const noexcept RB_NONBLOCKING
    {
        if (m_window.empty()) {
            return 0.0f;
        }
        const double mean = m_sum / static_cast<double>(m_window.size());
        return mean > 0.0 ? static_cast<float>(std::sqrt(mean)) : 0.0f;
    }

    /// Feeds one sample. Read the result with level(), which is where the
    /// square root is taken, so a meter pays for it once per cycle and not
    /// once per sample.
    void push(float sample) noexcept RB_NONBLOCKING
    {
        if (m_window.empty()) {
            return;
        }
        const double square = static_cast<double>(sample) * static_cast<double>(sample);
        m_sum += square - static_cast<double>(m_window[m_writeIndex]);
        m_window[m_writeIndex] = static_cast<float>(square);
        if (++m_writeIndex == m_window.size()) {
            m_writeIndex = 0;
        }
        if (m_sum < 0.0) {
            m_sum = 0.0; // rounding can take the running sum just below zero
        }
    }

    /// Feeds a whole cycle and returns the level after it.
    float process(std::span<const float> samples) noexcept RB_NONBLOCKING
    {
        for (const float sample : samples) {
            push(sample);
        }
        return level();
    }

private:
    std::vector<float> m_window;
    std::size_t m_writeIndex = 0;
    double m_sum = 0.0;
    double m_sampleRate = 48000.0;
};

} // namespace rb::dsp
