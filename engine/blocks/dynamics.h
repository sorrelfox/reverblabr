// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "dsp/dynamics.h"
#include "dsp/paramstore.h"

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

namespace rb::blocks
{

/// A dynamics processor: a compressor above, an expander below, and a curve
/// a person can see and drag.
///
/// **The controls are the curve.** Everything that decides the shape goes
/// into dsp::DynamicsSettings and through dsp::outputDb(), which is the same
/// function the curve editor draws with. There is one definition of what
/// this block does to a level, so what is on the screen and what is in the
/// sound cannot drift apart.
///
/// **The timing is not the curve.** Attack, release, hold and the detector
/// decide how long the sound takes to get to the curve, and are kept apart
/// from it for that reason.
///
/// **Nothing is smoothed but the makeup.** A threshold or a ratio that
/// changes mid-drag changes where the signal is heading, and the attack and
/// the release take it there at the speed they are set to — which is what
/// they are for. The makeup gain is added after all of that, so it has a
/// glide of its own.
class Dynamics final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "effect.dynamics";

    /// The stable ids of the detector settings.
    static constexpr std::string_view kDetectorPeak = "peak";
    static constexpr std::string_view kDetectorRms = "rms";

    /// Which of the block's published values is which (rt::BlockMeter), all
    /// in decibels. These are what the curve editor's live marker sits on:
    /// the input level is where along the curve the sound is, the output
    /// level is how high, and the reduction is the distance between them
    /// before the makeup.
    static constexpr std::size_t kInputLevelValue = 0;
    static constexpr std::size_t kOutputLevelValue = 1;
    static constexpr std::size_t kGainReductionValue = 2;

    enum Param : std::uint32_t {
        Threshold = 0,
        Ratio,
        /// **The top of the ratio slider, not a number on it.** A ratio of
        /// infinity is a limiter, and there is no place on a slider that
        /// runs to 20:1 to put it; this is the click past the end.
        InfiniteRatio,
        Knee,
        ExpanderThreshold,
        ExpanderRatio,
        ExpanderRange,
        ExpanderHold,
        Attack,
        Release,
        Makeup,
        Detector,
    };

    Dynamics();

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

    /// The curve the controls currently describe. **What the editor draws**,
    /// and what a test can put through dsp::outputDb() to find out what the
    /// block is going to do to a level.
    [[nodiscard]] dsp::DynamicsSettings settings() const noexcept RB_NONBLOCKING;

    /// How the sound gets to that curve.
    [[nodiscard]] dsp::DynamicsTiming timing() const noexcept RB_NONBLOCKING;

    /// What the last cycle was like, in decibels. The same three numbers the
    /// block publishes to telemetry.
    [[nodiscard]] float inputLevelDb() const noexcept RB_NONBLOCKING
    {
        return m_processor.inputLevelDb();
    }

    [[nodiscard]] float outputLevelDb() const noexcept RB_NONBLOCKING
    {
        return m_processor.outputLevelDb();
    }

    [[nodiscard]] float gainReduction() const noexcept RB_NONBLOCKING
    {
        return m_processor.gainReduction();
    }

private:
    [[nodiscard]] static std::vector<dsp::ParamDescriptor> makeDescriptors();

    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;
    dsp::DynamicsProcessor m_processor;
    rt::BlockMeter *m_meter = nullptr;
};

} // namespace rb::blocks
