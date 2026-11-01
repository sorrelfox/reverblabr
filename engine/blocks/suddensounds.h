// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "dsp/ear.h"
#include "dsp/paramstore.h"
#include "dsp/transientcut.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace rb::rt
{
class BlockMeter;
}

namespace rb::blocks
{

/// Softens clicks and knocks: rises far above the running level, for moments,
/// one range of the sound at a time. dsp::TransientCut does the work; this is
/// its Ear, its Strength and what it shows.
///
/// **Off changes nothing but the time.** At Off the output is the input, to
/// the bit, as late as at any Strength. Sound arrives no earlier for it, so
/// the choice never moves a lane.
///
/// **One ear.** The block works on the side its Ear setting names and passes
/// the other through, lined up (dsp::EarRouter); a second block does the other
/// ear. It reports GainReach::Adds, the default, on purpose: it only ever
/// takes sound away from a range, yet taking a range out of a waveform can put
/// a peak a little above the input's.
///
/// **What it publishes is kept here and not in the sound's own time.** A cut
/// can last 2 ms and the meters are read sixty times a second, so the deepest
/// cut is held for kHeldSeconds. The count is over kSeconds one-second parts,
/// so a card opened in mid-session shows the minute it missed, and cuts
/// under kJoinedMs apart count once. Both are read after every frame, so they
/// do not depend on how the cycles fall. Neither is written to the session.
class SuddenSounds final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "hearing.suddensounds";

    /// The stable ids of Strength, in the order the stored number counts them.
    static constexpr std::array<std::string_view, 4> kStrengthIds{"off", "gentle", "medium", "strong"};
    /// What each Strength takes of a range's excess, in dB per dB.
    static constexpr std::array<float, 4> kShares{0.0f, 0.267f, 0.467f, 0.66f};

    static constexpr std::size_t kSeconds = 60;
    static constexpr float kHeldSeconds = 1.0f;
    static constexpr double kJoinedMs = 5.0;

    /// What the block publishes (rt::BlockMeter): the deepest cut on any one
    /// range, held, in dB, and the cuts in the last minute.
    static constexpr std::size_t kSoftenedValue = 0;
    static constexpr std::size_t kCountValue = 1;

    enum Param : std::uint32_t {
        Ear = 0,
        Strength,
    };

    SuddenSounds();

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
    /// Takes in one frame's deepest cut, for what the block publishes.
    void heard(float deepestDb) noexcept RB_NONBLOCKING;

    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;
    rt::BlockMeter *m_meter = nullptr;
    dsp::EarRouter m_router;
    dsp::TransientCut m_cut;

    std::uint32_t m_rate = 1;
    std::uint32_t m_hop = 1;
    std::uint32_t m_joinedFrames = 1;
    std::array<std::uint32_t, kSeconds> m_seconds{};
    std::size_t m_second = 0;
    std::uint32_t m_intoSecond = 0;
    std::uint32_t m_count = 0;
    std::uint32_t m_framesSinceCut = 0;
    float m_heldDb = 0.0f;
    std::uint32_t m_heldLeft = 0;
};

} // namespace rb::blocks
