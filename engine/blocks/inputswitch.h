// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "dsp/paramstore.h"

#include <cstddef>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace rb::blocks
{

/// Passes one of two inputs, A or B, and crossfades from one to the other
/// when it is switched.
///
/// **Two lanes in, one out** (dsp::PortSpec::lanes): A on the first pair of
/// ports, B on the second.
///
/// **An equal-power crossfade.** The fade's position runs in a straight
/// line from one side to the other, and the two gains are its cosine and
/// its sine, so two unrelated signals of the same loudness keep that
/// loudness all the way across rather than dipping in the middle.
///
/// **The crossfade time is sent with the switch**, as the Mixer's ramp is
/// with a volume: the interface sends the time, the input, and the time put
/// back to half a second, in one edit, so a click fades over 0.5 s and a
/// Shift-click over 2 s, and a switch from anywhere else — a command line,
/// a shortcut — takes the half second. The switch is started in
/// applyParam() as it arrives, while the time it was sent with is the one
/// in the store; by the top of the next cycle it has been put back.
///
/// **Switched back half way**, the position turns round where it is and
/// goes back at the same speed, so a change of mind takes as long as the
/// part of the fade it undoes.
class InputSwitch final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "advanced.inputswitch";

    static constexpr std::uint32_t kLaneWidth = 2;
    static constexpr float kFadeMs = 500.0f;
    static constexpr float kSlowFadeMs = 2000.0f;

    /// The one value it publishes (rt::BlockMeter): where the crossfade is,
    /// from nought (all A) to one (all B).
    static constexpr std::size_t kPositionValue = 0;

    enum Param : std::uint32_t {
        Input = 0,
        Crossfade,
    };

    InputSwitch();

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

    /// Where the crossfade is now: nought is A alone, one is B alone.
    [[nodiscard]] float position() const noexcept RB_NONBLOCKING
    {
        return m_position;
    }

private:
    [[nodiscard]] static std::vector<dsp::ParamDescriptor> makeDescriptors();

    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;
    rt::BlockMeter *m_meter = nullptr;

    double m_sampleRate = 48000.0;
    float m_position = 0.0f;
    float m_target = 0.0f;
    /// How far the position moves in one sample while it is moving, and
    /// for how many samples more.
    float m_step = 0.0f;
    std::uint32_t m_remaining = 0;
};

} // namespace rb::blocks
