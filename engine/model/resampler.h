// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rb::model
{

/// **A streaming resampler with an exact delay** (ADR-0031): a windowed sinc
/// in a table of polyphase filters, at a fixed ratio of two whole rates.
///
/// Fed in chunks of any size, it gives the same samples as fed all at once,
/// and always delaySeconds() late. That number is what the host drops from the
/// front of its output, so it has to hold from the first sample. Off the audio
/// thread: it runs in the model host, and it allocates.
class Resampler
{
public:
    /// Whether a table for this pair of rates is small enough to make.
    [[nodiscard]] static bool supported(std::uint32_t inRate, std::uint32_t outRate) noexcept;
    /// How late the output is, in seconds. Zero when the rates are equal.
    [[nodiscard]] static double delaySeconds(std::uint32_t inRate, std::uint32_t outRate) noexcept;

    void prepare(std::uint32_t inRate, std::uint32_t outRate);
    /// Forgets the signal.
    void reset();
    /// Takes \a input and adds what it yields to \a output.
    void process(std::span<const float> input, std::vector<float> &output);

private:
    std::uint32_t m_up = 1;
    std::uint32_t m_down = 1;
    std::uint32_t m_taps = 0;
    std::vector<float> m_table;
    /// The last taps of input, then what has arrived since.
    std::vector<float> m_history;
    /// Where the next output sits: the newest input sample it uses, and how
    /// far past that, in 1/m_up of a sample.
    std::size_t m_position = 0;
    std::uint32_t m_phase = 0;
};

} // namespace rb::model
