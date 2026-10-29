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

/// Rolls off everything above a cutoff, for hiss and other high noise:
/// a CutoffFilter that keeps the low side, from 200 Hz to 20 kHz, starting
/// at 20 kHz.
class LowPassFilter final : public CutoffFilter
{
public:
    static constexpr std::string_view kTypeId = "effect.lowpass";
    static constexpr float kMinimumCutoff = 200.0f;
    static constexpr float kMaximumCutoff = 20000.0f;
    static constexpr float kDefaultCutoff = 20000.0f;

    LowPassFilter()
        : CutoffFilter({kTypeId, dsp::FilterShape::LowPass, kMinimumCutoff, kMaximumCutoff, kDefaultCutoff})
    {
    }

    [[nodiscard]] static std::unique_ptr<dsp::Block> make()
    {
        return std::make_unique<LowPassFilter>();
    }

    /// CutoffFilter::magnitudeDb() for a low-pass.
    [[nodiscard]] static double magnitudeDb(double frequency, double cutoff, bool steep, double sampleRate) noexcept
    {
        return CutoffFilter::magnitudeDb(dsp::FilterShape::LowPass, frequency, cutoff, steep, sampleRate);
    }
};

} // namespace rb::blocks
