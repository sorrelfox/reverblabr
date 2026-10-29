// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/smoothing.h"
#include "rt/realtime.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rb::dsp
{

/// What each side of a stereo pair is multiplied by.
struct StereoGains {
    float left = 1.0f;
    float right = 1.0f;

    friend bool operator==(const StereoGains &, const StereoGains &) = default;
};

/// The balance law: a position from −1 (all the way left) through 0 to +1
/// (all the way right), as the gain on each side.
///
/// **Only the side being turned away from moves.** The other stays at
/// exactly one, so a balance at the centre is the input untouched and a
/// balance anywhere else never makes anything louder — which is what
/// separates it from a pan, whose job is to move the sound and whose law
/// has to keep the loudness while it does.
///
/// **Linear in amplitude**, the way Volume's per cent is: "L 30" leaves the
/// right side at 70 % of what arrived (−3.10 dB), "L 50" at half (−6.02 dB),
/// and "L 100" silences it. Half-way down is where a person hears one side as
/// clearly quieter without it having gone, and the number on the control is
/// the number the audio uses.
[[nodiscard]] constexpr StereoGains balanceGains(float position) noexcept RB_NONBLOCKING
{
    const float clamped = std::clamp(position, -1.0f, 1.0f);
    // Towards the left turns the right down, and the other way round.
    return {.left = clamped > 0.0f ? 1.0f - clamped : 1.0f, .right = clamped < 0.0f ? 1.0f + clamped : 1.0f};
}

/// The pan law: a position from −1 (all the way left) through 0 to +1 (all
/// the way right), as the gain on each side for one signal being placed
/// between them.
///
/// **Constant power**: the two gains are the cosine and the sine of a
/// quarter turn spread along the control, so the sum of their squares is
/// always one and a sound keeps its loudness wherever it is put. The centre
/// is 0.7071 on each side (−3.01 dB), half-way to a side is 0.9239 near
/// (−0.69 dB) and 0.3827 far (−8.34 dB), and the end is one side alone.
[[nodiscard]] inline StereoGains panGains(float position) noexcept RB_NONBLOCKING
{
    const float clamped = std::clamp(position, -1.0f, 1.0f);
    // **Worked out from the distance to the centre, then given a side**, so
    // "L 30" and "R 30" are each other's mirror to the last bit, and the end
    // is sin(0) — exactly nothing — rather than cos(π/2), which in floats is
    // 4e-8: the whole signal 147 dB down on a side meant to be silent.
    const float angle = (1.0f - std::abs(clamped)) * std::numbers::pi_v<float> / 4.0f;
    const float near = std::cos(angle);
    const float far = std::sin(angle);
    return clamped < 0.0f ? StereoGains{.left = near, .right = far} : StereoGains{.left = far, .right = near};
}

/// Two gains gliding together, one for each side.
///
/// **The gains are smoothed, not the position.** A law is a curve, and a
/// position gliding along it would give each side a curved ramp whose
/// steepest step depends on the law; two straight ramps between the gains
/// before and after take the same time and step the same amount whichever
/// law produced them, so a block can change its law without its clicks
/// changing with it.
class StereoGainSmoother
{
public:
    /// Allocates nothing, but is meant for prepare(), not the audio thread.
    void prepare(double sampleRate, float timeMs) noexcept
    {
        m_left.prepare(sampleRate, timeMs);
        m_right.prepare(sampleRate, timeMs);
    }

    /// Puts \a gains in place at once, ending any glide.
    void reset(StereoGains gains) noexcept RB_NONBLOCKING
    {
        m_left.reset(gains.left);
        m_right.reset(gains.right);
    }

    /// Starts gliding towards \a gains; the same target twice does nothing,
    /// so a block may set it at the top of every cycle.
    void setTarget(StereoGains gains) noexcept RB_NONBLOCKING
    {
        m_left.setTarget(gains.left);
        m_right.setTarget(gains.right);
    }

    /// Advances one sample and returns the gains to use for it.
    [[nodiscard]] StereoGains next() noexcept RB_NONBLOCKING
    {
        return {.left = m_left.next(), .right = m_right.next()};
    }

    [[nodiscard]] StereoGains current() const noexcept RB_NONBLOCKING
    {
        return {.left = m_left.current(), .right = m_right.current()};
    }

    [[nodiscard]] bool isSmoothing() const noexcept RB_NONBLOCKING
    {
        return m_left.isSmoothing() || m_right.isSmoothing();
    }

private:
    LinearSmoother m_left;
    LinearSmoother m_right;
};

} // namespace rb::dsp
