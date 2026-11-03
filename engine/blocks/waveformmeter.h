// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "dsp/paramstore.h"
#include "rt/scope.h"

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace rb::blocks
{

/// Draws the sound at this point in a lane as it goes by: an oscilloscope
/// that can be dropped anywhere.
///
/// **It changes nothing.** The audio comes out the way it went in, sample for
/// sample with no delay, so it can be put anywhere without anybody having to
/// think about what it does to the sound.
///
/// **Every setting is the surface's.** How much of the past is shown, how
/// often, in what style: the block keeps what was chosen, for the session and
/// for presets, and does none of it. Its one job is to hand each cycle to
/// rt::BlockMeter::feedScope(), which writes all four rings of a slot while
/// somebody is drawing the block and is a single read while nobody is. That
/// is why changing a setting never disturbs the sound or starts a trace again,
/// and why a block nobody looks at costs nothing to speak of.
class WaveformMeter final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "meter.waveform";

    /// The widest and the narrowest stretch of time shown, in milliseconds.
    static constexpr float kShortestSpanMs = 10.0f;
    static constexpr float kLongestSpanMs = 10000.0f;
    static constexpr float kDefaultSpanMs = 200.0f;
    /// How far the trace can be enlarged or reduced, in dB.
    static constexpr float kLowestGainDb = -24.0f;
    static constexpr float kHighestGainDb = 48.0f;

    enum Param : std::uint32_t {
        Refresh = 0, ///< hz10, hz15, hz20, hz30, hz60, hz120.
        Motion, ///< realTime, smooth.
        Style, ///< line, filled, bars, dots.
        SpanMs, ///< How much time is shown.
        GainDb, ///< How much the trace is enlarged.
        AutoFit, ///< Enlarges it to fill the height by itself.
        Channels, ///< both, left, right, mid, side, stacked.
        Trigger, ///< Starts each trace on a rising zero crossing.
        Hold, ///< Keeps the trace shown, and stops taking new ones.
        Grid, ///< Draws the lines behind the trace.
        Scale, ///< linear, decibels.
    };

    WaveformMeter();

    [[nodiscard]] static std::unique_ptr<dsp::Block> make();

    [[nodiscard]] const dsp::BlockInfo &info() const noexcept override
    {
        return m_info;
    }

    [[nodiscard]] std::span<const dsp::ParamDescriptor> params() const noexcept override
    {
        return m_descriptors;
    }

    [[nodiscard]] std::expected<void, dsp::BlockError> prepare(const dsp::PrepareSpec &spec) override;
    void process(dsp::ProcessContext &context) noexcept RB_NONBLOCKING override;
    void reset() noexcept RB_NONBLOCKING override;
    void applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING override;

    [[nodiscard]] std::expected<void, dsp::BlockError> setParam(std::string_view id,
                                                                const dsp::ParamValue &value) override;
    [[nodiscard]] dsp::ParamValue param(std::string_view id) const override;
    [[nodiscard]] dsp::ParamValueMap saveState() const override;
    [[nodiscard]] std::expected<void, dsp::BlockError> loadState(const dsp::ParamValueMap &state) override;

private:
    [[nodiscard]] static std::vector<dsp::ParamDescriptor> makeDescriptors();

    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;

    rt::ScopeWriter m_scope;
    rt::BlockMeter *m_meter = nullptr;
    std::uint32_t m_sampleRate = 0;
};

} // namespace rb::blocks
