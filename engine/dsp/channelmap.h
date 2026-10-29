// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/audioblock.h"
#include "rt/realtime.h"

#include <array>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <vector>

namespace rb::dsp
{

/// Where a channel sits, named the way PipeWire's `audio.channel` property
/// names it, because that is what a port arrives carrying.
enum class ChannelPosition : std::uint8_t {
    Unknown,
    Mono,
    FrontLeft,
    FrontRight,
    FrontCentre,
    LowFrequency,
    SideLeft,
    SideRight,
    RearLeft,
    RearRight,
    Aux0,
    Aux1,
    Aux2,
    Aux3,
    Aux4,
    Aux5,
    Aux6,
    Aux7,
};

/// The most channels a mapping handles in either direction. A source exposes
/// at most this many ports, which covers 7.1 and every layout below it.
inline constexpr std::uint32_t kMaxMappedChannels = 8;

/// PipeWire's name for a position: "MONO", "FL", "FR", "FC", "LFE", "SL",
/// "SR", "RL", "RR", "AUX0" … "AUX7", or "UNK".
[[nodiscard]] std::string_view channelPositionName(ChannelPosition position) noexcept;

/// The position a PipeWire `audio.channel` value names, or Unknown.
[[nodiscard]] ChannelPosition channelPositionFromName(std::string_view name) noexcept;

/// The nth auxiliary channel, or Unknown past the eighth.
[[nodiscard]] ChannelPosition auxChannel(std::uint32_t index) noexcept;

/// What a node's ports are, in port order.
using ChannelLayout = std::vector<ChannelPosition>;

[[nodiscard]] ChannelLayout monoLayout();
[[nodiscard]] ChannelLayout stereoLayout();
/// FL, FR, FC, LFE, RL, RR — the order a 5.1 sink presents.
[[nodiscard]] ChannelLayout surround51Layout();
/// FL, FR, FC, LFE, RL, RR, SL, SR.
[[nodiscard]] ChannelLayout surround71Layout();
/// AUX0 … AUX(count-1).
[[nodiscard]] ChannelLayout auxLayout(std::uint32_t count);

/// **−3 dB.** What a channel folded into both sides of a stereo pair is taken
/// down by, so that the power it contributes is kept rather than doubled.
/// This is the coefficient the centre and the surrounds of a 5.1 or 7.1
/// signal get.
inline constexpr float kFoldGain = 0.70710678f;

/// **−6 dB.** What each side of a stereo pair is taken down by when the two
/// are summed to mono, so that two correlated full-scale channels come out at
/// full scale rather than 6 dB above it.
inline constexpr float kStereoToMonoGain = 0.5f;

/// Which side of the stereo bus an auxiliary channel belongs to.
enum class AuxSide : std::uint8_t {
    Drop, ///< It carries something that is not part of this signal.
    Left,
    Right,
    Both, ///< Folded into both, at kFoldGain.
};

/// The choices a block makes about a downmix.
struct DownmixOptions {
    /// Whether the low-frequency channel is folded into the stereo pair.
    /// Off by default: it holds effects a stereo pair is not meant to carry,
    /// and folding it in makes everything sound heavy.
    bool includeLowFrequency = false;
    /// The gain it gets when it is included.
    float lowFrequencyGain = kFoldGain;
    /// Where each auxiliary channel goes. The default sends AUX0 left, AUX1
    /// right and drops the rest, which is what an unlabelled stereo pair on
    /// an AUX sink turns out to be.
    std::array<AuxSide, kMaxMappedChannels> aux{AuxSide::Left,
                                                AuxSide::Right,
                                                AuxSide::Drop,
                                                AuxSide::Drop,
                                                AuxSide::Drop,
                                                AuxSide::Drop,
                                                AuxSide::Drop,
                                                AuxSide::Drop};
};

/// The choices a block makes about sending the stereo bus out.
struct UpmixOptions {
    /// Where each auxiliary channel of the sink takes its signal from. The
    /// default feeds AUX0 from the left and AUX1 from the right.
    std::array<AuxSide, kMaxMappedChannels> aux{AuxSide::Left,
                                                AuxSide::Right,
                                                AuxSide::Drop,
                                                AuxSide::Drop,
                                                AuxSide::Drop,
                                                AuxSide::Drop,
                                                AuxSide::Drop,
                                                AuxSide::Drop};
};

/// Why a layout could not be mapped.
enum class ChannelMapError : std::uint8_t {
    EmptyLayout, ///< A layout with no channels in it.
    TooManyChannels, ///< More than kMaxMappedChannels.
    DuplicatePosition, ///< Two ports claiming the same channel.
};

[[nodiscard]] std::string_view channelMapErrorName(ChannelMapError error) noexcept;

/// How much of one channel reaches each side of the stereo bus.
struct StereoContribution {
    float toLeft = 0.0f;
    float toRight = 0.0f;

    [[nodiscard]] bool operator==(const StereoContribution &other) const = default;
};

/// The documented coefficient for one channel of a downmix. This is the one
/// place the numbers live; the mapper is built out of it and the tests check
/// it directly.
[[nodiscard]] StereoContribution downmixContribution(ChannelPosition position, const DownmixOptions &options = {});

/// A prepared matrix of coefficients, applied one cycle at a time.
///
/// **This is the only place channels are mixed.** PipeWire sums everything
/// arriving at an input port at unity gain, which is right for mixing several
/// streams and wrong for a downmix: a centre channel linked into both L and R
/// comes out 3 dB hot, and a stereo pair linked into a mono port 6 dB hot.
/// So a link only ever joins two ports of the same position ([ADR-0007]),
/// and the mixing happens here with coefficients that can be written down
/// and measured.
class ChannelMapper
{
public:
    /// The matrix that takes a node's layout onto the session's stereo bus.
    [[nodiscard]] static std::expected<ChannelMapper, ChannelMapError> toStereo(const ChannelLayout &from,
                                                                                const DownmixOptions &options = {});

    /// The matrix that takes the session's stereo bus onto a node's layout.
    [[nodiscard]] static std::expected<ChannelMapper, ChannelMapError> fromStereo(const ChannelLayout &to,
                                                                                  const UpmixOptions &options = {});

    [[nodiscard]] std::uint32_t inputChannels() const noexcept RB_NONBLOCKING
    {
        return m_inputChannels;
    }

    [[nodiscard]] std::uint32_t outputChannels() const noexcept RB_NONBLOCKING
    {
        return m_outputChannels;
    }

    /// How much of \a inputChannel reaches \a outputChannel. Zero for a pair
    /// that is not connected at all.
    [[nodiscard]] float coefficient(std::uint32_t inputChannel,
                                    std::uint32_t outputChannel) const noexcept RB_NONBLOCKING;

    /// Maps one cycle. Every output channel is written; an input channel with
    /// no storage counts as silence. Real-time: bounded work, no allocation.
    void process(const AudioBlock &input, const AudioBlock &output, std::uint32_t frames) const noexcept RB_NONBLOCKING;

private:
    ChannelMapper() = default;

    std::array<float, kMaxMappedChannels * kMaxMappedChannels> m_coefficients{};
    std::uint32_t m_inputChannels = 0;
    std::uint32_t m_outputChannels = 0;
};

} // namespace rb::dsp
