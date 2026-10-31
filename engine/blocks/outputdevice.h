// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "dsp/channelmap.h"
#include "dsp/limiter.h"
#include "dsp/paramstore.h"
#include "dsp/smoothing.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

namespace rb::rt
{
class BlockMeter;
}

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
///
/// **It may hold the last limit of a chain**: after the channel matrix and
/// the volume, so that what is limited is the level on the wire. Whether it
/// does follows from the session's shape, not from anything in here, and the
/// runtime says so through setLastLimit(); without it the block is what it
/// always was, to the bit and with no delay.
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
    /// How far the last limit took the gain down in the last cycle, in dB.
    static constexpr std::size_t kLimitReductionValue = 1;
    /// The ceiling the last limit is at, in dBFS; nought while there is none,
    /// which no ceiling can be. **A published value and not a parameter**: it
    /// follows from the session, and a parameter is saved.
    static constexpr std::size_t kAppliedLimitValue = 3;

    /// Where each parameter sits in params().
    enum Param : std::uint32_t {
        Device = 0, ///< node.name, or kDefaultDevice.
        Volume, ///< 0…1 as an amplitude; the interface shows a percentage.
        LeftChannel, ///< Which of the device's channels the left side goes to.
        RightChannel,
        Limit, ///< The highest level the last limit lets out, dBFS true peak.
    };

    /// The range of Limit, as a Hearing block's Output ceiling has.
    static constexpr float kLimitHighestDb = -1.0f;
    static constexpr float kLimitLowestDb = -24.0f;

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
    /// 1 with the last limit: it is a delay, and a delay is prepared for.
    [[nodiscard]] std::uint32_t preparedShape() const override;
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

    /// **Says whether the last limit is wanted, and at what ceiling.** The
    /// ceiling takes effect in the next cycle. Whether the limit is there at
    /// all takes prepare(), and preparedShape() says so. **Not a setting**, so
    /// neither of these is saved or carried over by loadState(): a block made
    /// afresh is told again. Off the audio thread.
    void setLastLimit(bool wanted, float ceilingDb) noexcept;
    [[nodiscard]] bool lastLimitWanted() const noexcept
    {
        return m_limitWanted;
    }
    [[nodiscard]] float lastLimitDb() const noexcept
    {
        return m_limitDb.load(std::memory_order_relaxed);
    }

    /// How far the last limit took the gain down in the last cycle, in dB.
    [[nodiscard]] float limitReductionDb() const noexcept RB_NONBLOCKING;

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
    dsp::LookaheadLimiter m_limit;
    /// What the runtime last asked for.
    bool m_limitWanted = false;
    std::atomic<float> m_limitDb{kLimitHighestDb};
    /// Whether this preparation has the limit: fixed until the next one.
    bool m_limiting = false;
    rt::BlockMeter *m_meter = nullptr;
    bool m_prepared = false;
};

} // namespace rb::blocks
