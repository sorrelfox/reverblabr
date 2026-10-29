// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "blocks/cutofffilter.h"

#include <memory>
#include <string_view>

namespace rb::blocks
{

/// Rolls off everything below a cutoff, for rumble, handling noise and the
/// thump of a room: a CutoffFilter that keeps the high side, from
/// 20 Hz to 2 kHz, starting at 20 Hz.
///
/// **2 kHz at the top**, not further: past that it is taking the voice
/// itself away rather than what is under it, and a telephone band is the
/// 10-Band EQ's job.
class HighPassFilter final : public CutoffFilter
{
public:
    static constexpr std::string_view kTypeId = "effect.highpass";
    static constexpr float kMinimumCutoff = 20.0f;
    static constexpr float kMaximumCutoff = 2000.0f;
    static constexpr float kDefaultCutoff = 20.0f;

    HighPassFilter()
        : CutoffFilter({kTypeId, dsp::FilterShape::HighPass, kMinimumCutoff, kMaximumCutoff, kDefaultCutoff})
    {
    }

    [[nodiscard]] static std::unique_ptr<dsp::Block> make()
    {
        return std::make_unique<HighPassFilter>();
    }

    /// CutoffFilter::magnitudeDb() for a high-pass.
    [[nodiscard]] static double magnitudeDb(double frequency, double cutoff, bool steep, double sampleRate) noexcept
    {
        return CutoffFilter::magnitudeDb(dsp::FilterShape::HighPass, frequency, cutoff, steep, sampleRate);
    }
};

} // namespace rb::blocks
