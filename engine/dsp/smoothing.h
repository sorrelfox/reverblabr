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

namespace rb::dsp
{

/// Parameter smoothing.
///
/// A parameter that jumps from one value to the next between two cycles makes
/// the signal step, which is heard as a click or, on a slider, as zipper
/// noise. Both smoothers here turn such a jump into a glide that the audio
/// thread advances one sample at a time.
///
/// **Smoothing time** means the same thing for both: the time in which the
/// value covers 99 % of the distance to a new target. The linear smoother
/// arrives exactly at the end of it; the one-pole arrives asymptotically and
/// is within 1 % by then.
///
/// Both are prepared for a sample rate, and reset() puts a value in place with
/// no glide, which is what loading a session or starting a block does.

/// The per-sample factor an exponential smoother multiplies the remaining
/// distance by, so that only 1 % of it is left after \a timeMs.
/// Returns 0 (arrive at once) for a time of zero or less.
[[nodiscard]] inline float onePoleSmoothingCoefficient(float timeMs, double sampleRate) noexcept
{
    const double samples = static_cast<double>(timeMs) * 1.0e-3 * sampleRate;
    if (!(samples > 0.0)) {
        return 0.0f;
    }
    return static_cast<float>(std::pow(0.01, 1.0 / samples));
}

/// Glides to a new target along a straight line, which gives the smallest
/// possible sample-to-sample step for a given time. This is what a gain or a
/// mix parameter uses: an N-sample ramp steps by exactly 1/N of the distance.
class LinearSmoother
{
public:
    /// Sets the sample rate and the smoothing time, and recomputes the ramp.
    /// A glide in progress continues from where it is, over the new time.
    /// Allocates nothing, but is meant for prepare(), not the audio thread.
    void prepare(double sampleRate, float timeMs) noexcept
    {
        m_timeMs = timeMs;
        const double samples = static_cast<double>(timeMs) * 1.0e-3 * sampleRate;
        m_rampSamples = samples > 0.0 ? static_cast<std::uint32_t>(std::lround(samples)) : 0;
        if (m_remaining > 0) {
            restart();
        }
    }

    [[nodiscard]] float timeMs() const noexcept
    {
        return m_timeMs;
    }

    /// The length of a full glide in samples.
    [[nodiscard]] std::uint32_t rampSamples() const noexcept
    {
        return m_rampSamples;
    }

    /// Puts \a value in place at once, ending any glide.
    void reset(float value) noexcept RB_NONBLOCKING
    {
        m_current = value;
        m_target = value;
        m_remaining = 0;
        m_increment = 0.0f;
    }

    /// Starts gliding towards \a value.
    ///
    /// Setting the target it is already heading for does nothing, so a block
    /// may work out its target at the top of every cycle and set it, without
    /// having to remember whether it changed. Restarting the glide instead
    /// would stretch it over a fresh full time from wherever it had got to,
    /// and at a cycle a few hundred samples long it would never arrive.
    void setTarget(float value) noexcept RB_NONBLOCKING
    {
        if (value == m_target) {
            return;
        }
        m_target = value;
        restart();
    }

    [[nodiscard]] float current() const noexcept RB_NONBLOCKING
    {
        return m_current;
    }

    [[nodiscard]] float target() const noexcept RB_NONBLOCKING
    {
        return m_target;
    }

    [[nodiscard]] bool isSmoothing() const noexcept RB_NONBLOCKING
    {
        return m_remaining > 0;
    }

    /// Advances one sample and returns the value to use for it.
    [[nodiscard]] float next() noexcept RB_NONBLOCKING
    {
        if (m_remaining == 0) {
            return m_current;
        }
        --m_remaining;
        // Measured back from the target rather than added up from the last
        // value: adding would let rounding accumulate over a long ramp, and
        // the error shows up as an uneven step, which is what this class is
        // here to avoid. It also lands on the target exactly.
        m_current = m_target - m_increment * static_cast<float>(m_remaining);
        return m_current;
    }

    /// Advances \a frames samples without producing them, for a stretch of
    /// signal the parameter doesn't touch (a bypassed or silent block).
    void advance(std::uint32_t frames) noexcept RB_NONBLOCKING
    {
        if (m_remaining == 0) {
            return;
        }
        if (frames >= m_remaining) {
            m_current = m_target;
            m_remaining = 0;
            return;
        }
        m_remaining -= frames;
        m_current = m_target - m_increment * static_cast<float>(m_remaining);
    }

private:
    void restart() noexcept RB_NONBLOCKING
    {
        if (m_rampSamples == 0 || m_target == m_current) {
            m_current = m_target;
            m_remaining = 0;
            m_increment = 0.0f;
            return;
        }
        m_remaining = m_rampSamples;
        m_increment = (m_target - m_current) / static_cast<float>(m_rampSamples);
    }

    float m_timeMs = 0.0f;
    std::uint32_t m_rampSamples = 0;
    std::uint32_t m_remaining = 0;
    float m_current = 0.0f;
    float m_target = 0.0f;
    float m_increment = 0.0f;
};

/// Glides to a new target along an exponential curve: fast at first, then
/// ever slower. It never steps at a cycle boundary, whatever the target does,
/// which suits parameters that are dragged continuously, such as a filter
/// frequency. For a click-free gain change prefer LinearSmoother, whose steps
/// are even.
class OnePoleSmoother
{
public:
    /// Sets the sample rate and the smoothing time, and recomputes the
    /// coefficient. A glide in progress continues from where it is.
    void prepare(double sampleRate, float timeMs) noexcept
    {
        m_timeMs = timeMs;
        m_coefficient = onePoleSmoothingCoefficient(timeMs, sampleRate);
    }

    [[nodiscard]] float timeMs() const noexcept
    {
        return m_timeMs;
    }

    /// The per-sample factor the remaining distance is multiplied by.
    [[nodiscard]] float coefficient() const noexcept
    {
        return m_coefficient;
    }

    void reset(float value) noexcept RB_NONBLOCKING
    {
        m_current = value;
        m_target = value;
        m_remaining = 0.0f;
    }

    void setTarget(float value) noexcept RB_NONBLOCKING
    {
        m_remaining = m_current - value;
        m_target = value;
    }

    [[nodiscard]] float current() const noexcept RB_NONBLOCKING
    {
        return m_current;
    }

    [[nodiscard]] float target() const noexcept RB_NONBLOCKING
    {
        return m_target;
    }

    [[nodiscard]] bool isSmoothing() const noexcept RB_NONBLOCKING
    {
        return m_remaining != 0.0f;
    }

    /// Advances one sample and returns the value to use for it.
    [[nodiscard]] float next() noexcept RB_NONBLOCKING
    {
        if (m_remaining == 0.0f) {
            return m_current;
        }
        m_remaining *= m_coefficient;
        settle();
        m_current = m_target + m_remaining;
        return m_current;
    }

    /// Advances \a frames samples without producing them.
    void advance(std::uint32_t frames) noexcept RB_NONBLOCKING
    {
        if (frames == 0 || m_remaining == 0.0f) {
            return;
        }
        m_remaining *= std::pow(m_coefficient, static_cast<float>(frames));
        settle();
        m_current = m_target + m_remaining;
    }

private:
    /// Ends the glide once the rest of the distance is inaudible, so that
    /// isSmoothing() finishes and the tail never reaches the denormal range.
    void settle() noexcept RB_NONBLOCKING
    {
        if (std::abs(m_remaining) <= kSettled * (1.0f + std::abs(m_target))) {
            m_remaining = 0.0f;
        }
    }

    /// Distance below which the glide is over, relative to the target.
    static constexpr float kSettled = 1.0e-6f;

    float m_timeMs = 0.0f;
    float m_coefficient = 0.0f;
    float m_current = 0.0f;
    float m_target = 0.0f;
    /// How far the value still is from the target. Kept on its own rather
    /// than worked out from current() - target(): near the target the two are
    /// so close that their difference loses most of its digits, and the glide
    /// would stall a few millionths short instead of arriving.
    float m_remaining = 0.0f;
};

} // namespace rb::dsp
