// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "dsp/limiter.h"
#include "dsp/loudness.h"
#include "dsp/paramstore.h"

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

namespace rb::blocks
{

/// Lifts a source whose loudness keeps changing towards a steady level, and
/// leaves anything already loud enough alone.
///
/// **Upward only.** It measures the loudness of what arrives
/// (dsp::GatedLoudness: K-weighted and gated, over the last 3 s, worked
/// out ten times a second, so pauses don't count) and boosts by how far that
/// is below the target, never more than the maximum and never below 0 dB.
/// Loud material is not turned down: that is a compressor's job, and a
/// leveller that did both would pump.
///
/// **Slow up, quick down.** The boost rises over kRiseMs, so a quiet word
/// is lifted gently rather than jumped, and falls over kFallMs when the
/// source gets loud, so a shout after a whisper is not played at the
/// whisper's boost for long. What the fall misses the limiter catches.
///
/// **A gate below kGateLufs**: under it there is nothing but noise to lift,
/// and the boost drifts back to nothing at the slow rate instead — slowly,
/// so a pause between sentences does not pull it down.
///
/// **The lookahead limiter at −1 dBFS is always in the path** (the one
/// Volume uses), so the latency is a constant and nothing the boost does can
/// clip. Switching the boost off glides it to 0 dB and leaves the limiter.
class AutoLevel final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "effect.autolevel";

    static constexpr float kDefaultTargetLufs = -20.0f;
    static constexpr float kLowestTargetLufs = -30.0f;
    static constexpr float kHighestTargetLufs = -14.0f;
    static constexpr float kDefaultMaximumBoostDb = 18.0f;
    static constexpr float kHighestMaximumBoostDb = 24.0f;

    /// Below this there is nothing worth lifting.
    static constexpr float kGateLufs = -50.0f;
    /// The boost's time constant rising to what the loudness asks for, and
    /// falling back when the source is under the gate or switched off: about
    /// two thirds of the way in this time, nearly all of it in three.
    static constexpr float kRiseMs = 1500.0f;
    /// Its time constant coming down when the source gets loud.
    static constexpr float kFallMs = 100.0f;

    /// Which of the block's published values is which (rt::BlockMeter): the
    /// boost being applied now, in dB, and the loudness it was worked out
    /// from, in LUFS. The face reads the first by this index.
    static constexpr std::size_t kBoostValue = 0;
    static constexpr std::size_t kLoudnessValue = 1;

    enum Param : std::uint32_t {
        Boost = 0, ///< On or off.
        Target, ///< LUFS.
        MaximumBoost, ///< dB.
    };

    AutoLevel();

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

    /// The boost being applied now, in dB.
    [[nodiscard]] float boostDb() const noexcept RB_NONBLOCKING
    {
        return m_boostDb;
    }

    /// The short-term loudness the boost was last worked out from.
    [[nodiscard]] float loudnessLufs() const noexcept RB_NONBLOCKING
    {
        return m_loudness.loudness();
    }

private:
    [[nodiscard]] static std::vector<dsp::ParamDescriptor> makeDescriptors();
    /// The boost the last measurement asks for.
    [[nodiscard]] float wantedBoostDb() const noexcept RB_NONBLOCKING;

    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;
    dsp::GatedLoudness m_loudness;
    dsp::LookaheadLimiter m_limiter;
    rt::BlockMeter *m_meter = nullptr;
    float m_riseCoefficient = 0.0f;
    float m_fallCoefficient = 0.0f;
    float m_boostDb = 0.0f;
    float m_wantedDb = 0.0f;
};

} // namespace rb::blocks
