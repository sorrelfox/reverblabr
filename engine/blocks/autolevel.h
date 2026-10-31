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

/// Brings a source whose loudness keeps changing towards a steady level.
///
/// **Up, and down if asked.** It measures the loudness of what arrives
/// (dsp::GatedLoudness: K-weighted and gated, over the last 3 s, worked out
/// ten times a second, so pauses don't count) and moves the sound by how far
/// that is from the target, never past Maximum boost or Maximum cut. Bringing
/// sound down is off until a person turns it on, so a block saved before it
/// existed does what it did. A slow two-way reading moves the gain far less
/// than a fast one would: it follows talkers, not syllables.
///
/// **Sound near the target is left alone.** `band` is taken off the distance
/// from the target, so at 0 every level is moved and the curve has no step at
/// the band's edge.
///
/// **Slow up, quick down.** The gain rises over kRiseMs, so a quiet word is
/// lifted gently rather than jumped, and falls over kFallMs when the source
/// gets loud, so a shout after a whisper is not played at the whisper's boost
/// for long.
///
/// **The guard takes back what the fall misses.** The 3 s reading is slow
/// and its fall starts only when a 400 ms block has seen the new voice, so
/// every 10 ms the last 100 ms is read too, and what would play more than
/// kJumpLu over the target is pulled off the gain at once and let go over
/// kPullReleaseMs. It has no setting. It never takes the gain below what the
/// switches allow, so with the cut off it can take back a boost and no more.
///
/// **A gate below kGateLufs**: under it there is nothing but noise to lift,
/// and the gain drifts back to nothing at the slow rate instead, slowly, so a
/// pause between sentences does not pull it down.
///
/// **The lookahead limiter at −1 dBFS is always in the path** (the one
/// Volume uses), so the latency is a constant and nothing the boost does can
/// clip. Switching both directions off glides the gain to 0 dB and leaves the
/// limiter.
class AutoLevel final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "effect.autolevel";

    static constexpr float kDefaultTargetLufs = -20.0f;
    static constexpr float kLowestTargetLufs = -30.0f;
    static constexpr float kHighestTargetLufs = -14.0f;
    static constexpr float kDefaultMaximumBoostDb = 18.0f;
    static constexpr float kHighestMaximumBoostDb = 24.0f;

    static constexpr float kDefaultMaximumCutDb = 18.0f;
    static constexpr float kHighestMaximumCutDb = 24.0f;
    static constexpr float kHighestBandLu = 6.0f;

    /// Below this there is nothing worth lifting.
    static constexpr float kGateLufs = -50.0f;
    /// The boost's time constant rising to what the loudness asks for, and
    /// falling back when the source is under the gate or switched off: about
    /// two thirds of the way in this time, nearly all of it in three.
    static constexpr float kRiseMs = 1500.0f;
    /// Its time constant coming down when the source gets loud.
    static constexpr float kFallMs = 100.0f;

    /// The guard: how far over the target the last 100 ms may play, how
    /// long a pull takes to let go (a time constant), and how long it takes
    /// to come in, so that it never clicks.
    static constexpr float kJumpLu = 9.0f;
    static constexpr float kPullReleaseMs = 20.0f;
    static constexpr float kPullGlideMs = 5.0f;

    /// Which of the block's published values is which (rt::BlockMeter): the
    /// gain being applied now, in dB of change, the loudness it was worked
    /// out from, in LUFS, and how much of it the guard is holding back, in
    /// dB. The face and the panel read them by these indices.
    static constexpr std::size_t kBoostValue = 0;
    static constexpr std::size_t kLoudnessValue = 1;
    static constexpr std::size_t kPullValue = 2;

    enum Param : std::uint32_t {
        Boost = 0, ///< On or off.
        Target, ///< LUFS.
        MaximumBoost, ///< dB.
        Cut, ///< On or off.
        MaximumCut, ///< dB.
        Band, ///< ± LU around the target left alone.
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

    /// The gain being applied now, in dB of change, the guard's pull included.
    [[nodiscard]] float boostDb() const noexcept RB_NONBLOCKING
    {
        return m_appliedDb;
    }

    /// How much the guard is taking off the gain now, in dB.
    [[nodiscard]] float pullDb() const noexcept RB_NONBLOCKING
    {
        return m_gainDb - m_appliedDb;
    }

    /// The short-term loudness the boost was last worked out from.
    [[nodiscard]] float loudnessLufs() const noexcept RB_NONBLOCKING
    {
        return m_loudness.loudness();
    }

private:
    [[nodiscard]] static std::vector<dsp::ParamDescriptor> makeDescriptors();
    /// The gain the last measurement asks for, against \a pivotLufs.
    [[nodiscard]] float wantedGainDb(float pivotLufs) const noexcept RB_NONBLOCKING;
    /// The lowest gain the switches allow, in dB: at most 0.
    [[nodiscard]] float lowestGainDb() const noexcept RB_NONBLOCKING;
    /// What the guard asks for now that a 10 ms slice has ended.
    void updatePull() noexcept RB_NONBLOCKING;

    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;
    dsp::GatedLoudness m_loudness;
    dsp::LookaheadLimiter m_limiter;
    rt::BlockMeter *m_meter = nullptr;
    float m_riseCoefficient = 0.0f;
    float m_fallCoefficient = 0.0f;
    float m_pullReleaseCoefficient = 0.0f;
    float m_pullGlideCoefficient = 0.0f;
    float m_gainDb = 0.0f; ///< Where the glide is, before the guard.
    float m_wantedDb = 0.0f;
    float m_pullDb = 0.0f; ///< What the guard asks for.
    float m_smoothPullDb = 0.0f;
    float m_appliedDb = 0.0f;
};

} // namespace rb::blocks
