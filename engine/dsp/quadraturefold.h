// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "rt/realtime.h"

#include <array>
#include <cstddef>

namespace rb::dsp
{

/// Two fixed all-pass chains a quarter of a cycle apart: what lets two sides
/// be folded into one without the plain sum's 6 dB loss on a panned sound or
/// its cancellation at 180 degrees.
///
/// Each chain is four first-order sections with unity gain at every
/// frequency, so nothing here changes a level by itself. It is the sum that
/// does: A(left) + B(right), scaled by kFoldGain, keeps a centred sound near
/// 0 dB, a sound on one side at -3 dB and a sound in antiphase near 0 dB.
///
/// **It moves the null, and does not remove it.** Any fixed sum with equal
/// gains cancels some phase relation. This one cancels where the right side
/// leads the left by a quarter of a cycle.
///
/// **The corners scale with the rate.** They were fitted against the warp
/// near Nyquist, so a table's corners are used at a fixed share of the rate;
/// the 48 kHz figures unscaled would be 24 to 34 degrees out elsewhere.
class QuadratureFold
{
public:
    static constexpr std::size_t kSections = 4;
    /// Rates under this use the 48 kHz table, the rest the 96 kHz one.
    static constexpr double kHighTableFrom = 64000.0;
    /// Keeps a centred sound at its level.
    static constexpr float kFoldGain = 0.70710678f;

    /// Corners at the table's base rate, in Hz.
    using Corners = std::array<double, kSections>;
    struct Table {
        double baseRate;
        Corners a;
        Corners b;
    };

    /// One frame of the two chains. Handed back apart, because the two ears
    /// can need different sums during a glide.
    struct Frame {
        float a = 0.0f;
        float b = 0.0f;
    };

    /// The table used at \a sampleRate.
    [[nodiscard]] static const Table &tableFor(double sampleRate) noexcept;
    /// A section's coefficient for corner \a cornerHz at \a sampleRate.
    [[nodiscard]] static double coefficient(double cornerHz, double sampleRate) noexcept;

    /// Works out the coefficients and clears the chains. Not for the audio
    /// thread.
    void prepare(double sampleRate) noexcept;

    /// Forgets what has passed through.
    void reset() noexcept RB_NONBLOCKING
    {
        m_a.input = {};
        m_a.output = {};
        m_b.input = {};
        m_b.output = {};
    }

    /// Takes one frame and gives back chain A of \a left and chain B of
    /// \a right.
    [[nodiscard]] Frame process(float left, float right) noexcept RB_NONBLOCKING
    {
        return {static_cast<float>(run(m_a, left)), static_cast<float>(run(m_b, right))};
    }

    /// Chain \a chain's coefficients (0 for A, 1 for B), for a test.
    [[nodiscard]] const std::array<double, kSections> &coefficients(std::size_t chain) const noexcept
    {
        return chain == 0 ? m_a.coefficient : m_b.coefficient;
    }

private:
    struct Chain {
        std::array<double, kSections> coefficient{};
        std::array<double, kSections> input{};
        std::array<double, kSections> output{};
    };

    [[nodiscard]] static double run(Chain &chain, double sample) noexcept RB_NONBLOCKING
    {
        for (std::size_t section = 0; section < kSections; ++section) {
            const double a = chain.coefficient[section];
            const double out = a * sample + chain.input[section] - a * chain.output[section];
            chain.input[section] = sample;
            chain.output[section] = out;
            sample = out;
        }
        return sample;
    }

    Chain m_a;
    Chain m_b;
};

} // namespace rb::dsp
