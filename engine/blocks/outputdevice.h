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
#include "dsp/smoothing.h"

#include <array>
#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

namespace rb::blocks
{

/// The session's sound on a speaker, a pair of headphones or anything else
/// that plays.
///
/// **The engine links; the block decides what goes where.** Which sink the
/// audio reaches is the link manager's business, and the block's ports are
/// laid out to match that sink so a link only ever joins two ports of the
/// same channel ([ADR-0007]). What this block does is put the session's
/// stereo bus onto those channels, at the volume the user set.
///
/// **The device is stored by `node.name`**, or as "default", which follows
/// whatever WirePlumber has made the default output. A name that is not
/// there at the moment is not an error: the block goes quiet and comes back
/// when the device does, because a headset that was unplugged is usually
/// going to be plugged in again.
class OutputDevice final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "output.device";

    /// The device that follows `default.audio.sink` rather than naming one.
    static constexpr std::string_view kDefaultDevice = "default";
    /// A channel picker left alone: the natural mapping, which is what the
    /// channel policy says ([dsp::ChannelMapper]).
    static constexpr std::string_view kNaturalChannel = "default";

    /// Which of the block's published values is which (rt::BlockMeter).
    /// Whether the device is there at the moment: 1 or 0. The words —
    /// "unavailable" — are the interface's.
    static constexpr std::size_t kDevicePresentValue = 0;

    /// Where each parameter sits in params().
    enum Param : std::uint32_t {
        Device = 0, ///< node.name, or kDefaultDevice.
        Volume, ///< 0…1 as an amplitude; the interface shows a percentage.
        LeftChannel, ///< Which of the device's channels the left side goes to.
        RightChannel,
    };

    /// How long the volume takes to arrive, in milliseconds. Long enough
    /// that a slider dragged across its range does not step the signal, and
    /// short enough that a person does not notice the delay.
    static constexpr float kVolumeSmoothingMs = 20.0f;

    OutputDevice();

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

    /// The layout the block was last prepared for, which is what its output
    /// ports carry.
    [[nodiscard]] const dsp::ChannelLayout &outputLayout() const noexcept
    {
        return m_layout;
    }

    /// How much of the left or the right side reaches one of the device's
    /// channels. **The whole of what the block does to the signal**, so a
    /// test can read the policy off it rather than off the sound.
    [[nodiscard]] float coefficient(std::uint32_t side, std::size_t channel) const noexcept;

private:
    [[nodiscard]] static std::vector<dsp::ParamDescriptor> makeDescriptors();
    void buildMatrix();

    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;
    dsp::ChannelLayout m_layout;
    /// Left and right, against each channel of the device.
    std::array<std::array<float, dsp::kMaxMappedChannels>, 2> m_matrix{};
    dsp::LinearSmoother m_volume;
    bool m_prepared = false;
};

} // namespace rb::blocks
