// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "dsp/channelmap.h"
#include "dsp/paramstore.h"

#include <array>
#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

namespace rb::blocks
{

/// A microphone, an audio interface or anything else the machine can record
/// from.
///
/// **The mirror image of the Output Device.** The engine links the device's
/// capture ports to this block's input ports — one port per channel, because
/// a link only ever joins two ports of the same channel ([ADR-0007]) — and
/// the block puts what arrives onto the session's stereo bus.
///
/// **A mono microphone is heard on both sides at the level it was recorded
/// at.** Linking one port into two would have PipeWire sum it and hand back
/// 6 dB it never had; the mapping is done here, with coefficients that were
/// written down first.
class InputDevice final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "source.device";

    /// The device that follows `default.audio.source` rather than naming one.
    static constexpr std::string_view kDefaultDevice = "default";
    /// A channel picker left alone: the natural mapping.
    static constexpr std::string_view kNaturalChannel = "default";

    /// Whether the device is there at the moment: 1 or 0
    /// (rt::BlockMeter). The word "unavailable" is the interface's.
    static constexpr std::size_t kDevicePresentValue = 0;

    enum Param : std::uint32_t {
        Device = 0, ///< node.name, or kDefaultDevice.
        LeftChannel, ///< Which of the device's channels feeds the left side.
        RightChannel,
    };

    InputDevice();

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

    /// The layout the block was last prepared for, which is what its input
    /// ports carry.
    [[nodiscard]] const dsp::ChannelLayout &inputLayout() const noexcept
    {
        return m_layout;
    }

    /// How much of one of the device's channels reaches the left or the
    /// right side of the session. The whole of what the block does.
    [[nodiscard]] float coefficient(std::size_t channel, std::uint32_t side) const noexcept;

private:
    [[nodiscard]] static std::vector<dsp::ParamDescriptor> makeDescriptors();
    void buildMatrix();

    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;
    dsp::ChannelLayout m_layout;
    /// Each channel of the device against the two sides of the session.
    std::array<std::array<float, 2>, dsp::kMaxMappedChannels> m_matrix{};
    bool m_prepared = false;
};

} // namespace rb::blocks
