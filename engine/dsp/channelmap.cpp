// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "dsp/channelmap.h"

#include <algorithm>
#include <span>

namespace rb::dsp
{
namespace
{

/// The session's stereo bus, which every mapping goes to or comes from.
constexpr std::uint32_t kLeft = 0;
constexpr std::uint32_t kRight = 1;

[[nodiscard]] std::uint32_t auxIndex(ChannelPosition position) noexcept
{
    return static_cast<std::uint32_t>(position) - static_cast<std::uint32_t>(ChannelPosition::Aux0);
}

/// What one side of the stereo bus gives an auxiliary channel.
[[nodiscard]] StereoContribution auxContribution(AuxSide side)
{
    switch (side) {
    case AuxSide::Drop:
        return {};
    case AuxSide::Left:
        return StereoContribution{1.0f, 0.0f};
    case AuxSide::Right:
        return StereoContribution{0.0f, 1.0f};
    case AuxSide::Both:
        return StereoContribution{kFoldGain, kFoldGain};
    }
    return {};
}

/// Checks a layout before anything is built out of it.
[[nodiscard]] std::expected<void, ChannelMapError> validate(const ChannelLayout &layout)
{
    if (layout.empty()) {
        return std::unexpected(ChannelMapError::EmptyLayout);
    }
    if (layout.size() > kMaxMappedChannels) {
        return std::unexpected(ChannelMapError::TooManyChannels);
    }
    for (std::size_t index = 0; index < layout.size(); ++index) {
        if (layout[index] == ChannelPosition::Unknown) {
            continue; // a port we cannot place is dropped, not a failure
        }
        for (std::size_t other = index + 1; other < layout.size(); ++other) {
            if (layout[index] == layout[other]) {
                return std::unexpected(ChannelMapError::DuplicatePosition);
            }
        }
    }
    return {};
}

} // namespace

std::string_view channelPositionName(ChannelPosition position) noexcept
{
    switch (position) {
    case ChannelPosition::Unknown:
        return "UNK";
    case ChannelPosition::Mono:
        return "MONO";
    case ChannelPosition::FrontLeft:
        return "FL";
    case ChannelPosition::FrontRight:
        return "FR";
    case ChannelPosition::FrontCentre:
        return "FC";
    case ChannelPosition::LowFrequency:
        return "LFE";
    case ChannelPosition::SideLeft:
        return "SL";
    case ChannelPosition::SideRight:
        return "SR";
    case ChannelPosition::RearLeft:
        return "RL";
    case ChannelPosition::RearRight:
        return "RR";
    case ChannelPosition::Aux0:
        return "AUX0";
    case ChannelPosition::Aux1:
        return "AUX1";
    case ChannelPosition::Aux2:
        return "AUX2";
    case ChannelPosition::Aux3:
        return "AUX3";
    case ChannelPosition::Aux4:
        return "AUX4";
    case ChannelPosition::Aux5:
        return "AUX5";
    case ChannelPosition::Aux6:
        return "AUX6";
    case ChannelPosition::Aux7:
        return "AUX7";
    }
    return "UNK";
}

ChannelPosition channelPositionFromName(std::string_view name) noexcept
{
    for (std::uint8_t value = 0; value <= static_cast<std::uint8_t>(ChannelPosition::Aux7); ++value) {
        const auto position = static_cast<ChannelPosition>(value);
        if (position != ChannelPosition::Unknown && channelPositionName(position) == name) {
            return position;
        }
    }
    return ChannelPosition::Unknown;
}

ChannelPosition auxChannel(std::uint32_t index) noexcept
{
    if (index >= kMaxMappedChannels) {
        return ChannelPosition::Unknown;
    }
    return static_cast<ChannelPosition>(static_cast<std::uint32_t>(ChannelPosition::Aux0) + index);
}

ChannelLayout monoLayout()
{
    return {ChannelPosition::Mono};
}

ChannelLayout stereoLayout()
{
    return {ChannelPosition::FrontLeft, ChannelPosition::FrontRight};
}

ChannelLayout surround51Layout()
{
    return {ChannelPosition::FrontLeft,
            ChannelPosition::FrontRight,
            ChannelPosition::FrontCentre,
            ChannelPosition::LowFrequency,
            ChannelPosition::RearLeft,
            ChannelPosition::RearRight};
}

ChannelLayout surround71Layout()
{
    return {ChannelPosition::FrontLeft,
            ChannelPosition::FrontRight,
            ChannelPosition::FrontCentre,
            ChannelPosition::LowFrequency,
            ChannelPosition::RearLeft,
            ChannelPosition::RearRight,
            ChannelPosition::SideLeft,
            ChannelPosition::SideRight};
}

ChannelLayout auxLayout(std::uint32_t count)
{
    ChannelLayout layout;
    for (std::uint32_t index = 0; index < count; ++index) {
        layout.push_back(auxChannel(index));
    }
    return layout;
}

std::string_view channelMapErrorName(ChannelMapError error) noexcept
{
    switch (error) {
    case ChannelMapError::EmptyLayout:
        return "emptyLayout";
    case ChannelMapError::TooManyChannels:
        return "tooManyChannels";
    case ChannelMapError::DuplicatePosition:
        return "duplicatePosition";
    }
    return "unknown";
}

StereoContribution downmixContribution(ChannelPosition position, const DownmixOptions &options)
{
    switch (position) {
    case ChannelPosition::Mono:
        // Full level to both sides. Not half: a mono source has to come out
        // as loud as it went in, and the two sides are the same signal, not
        // two halves of one.
        return StereoContribution{1.0f, 1.0f};
    case ChannelPosition::FrontLeft:
        return StereoContribution{1.0f, 0.0f};
    case ChannelPosition::FrontRight:
        return StereoContribution{0.0f, 1.0f};
    case ChannelPosition::FrontCentre:
        return StereoContribution{kFoldGain, kFoldGain};
    case ChannelPosition::LowFrequency:
        return options.includeLowFrequency ? StereoContribution{options.lowFrequencyGain, options.lowFrequencyGain}
                                           : StereoContribution{};
    case ChannelPosition::SideLeft:
    case ChannelPosition::RearLeft:
        return StereoContribution{kFoldGain, 0.0f};
    case ChannelPosition::SideRight:
    case ChannelPosition::RearRight:
        return StereoContribution{0.0f, kFoldGain};
    case ChannelPosition::Aux0:
    case ChannelPosition::Aux1:
    case ChannelPosition::Aux2:
    case ChannelPosition::Aux3:
    case ChannelPosition::Aux4:
    case ChannelPosition::Aux5:
    case ChannelPosition::Aux6:
    case ChannelPosition::Aux7:
        return auxContribution(options.aux[auxIndex(position)]);
    case ChannelPosition::Unknown:
        break;
    }
    return {}; // a channel we cannot place carries nothing into the bus
}

std::expected<ChannelMapper, ChannelMapError> ChannelMapper::toStereo(const ChannelLayout &from,
                                                                      const DownmixOptions &options)
{
    if (const std::expected<void, ChannelMapError> valid = validate(from); !valid) {
        return std::unexpected(valid.error());
    }

    ChannelMapper mapper;
    mapper.m_inputChannels = static_cast<std::uint32_t>(from.size());
    mapper.m_outputChannels = 2;
    for (std::uint32_t channel = 0; channel < mapper.m_inputChannels; ++channel) {
        const StereoContribution contribution = downmixContribution(from[channel], options);
        mapper.m_coefficients[channel * kMaxMappedChannels + kLeft] = contribution.toLeft;
        mapper.m_coefficients[channel * kMaxMappedChannels + kRight] = contribution.toRight;
    }
    return mapper;
}

std::expected<ChannelMapper, ChannelMapError> ChannelMapper::fromStereo(const ChannelLayout &to,
                                                                        const UpmixOptions &options)
{
    if (const std::expected<void, ChannelMapError> valid = validate(to); !valid) {
        return std::unexpected(valid.error());
    }

    ChannelMapper mapper;
    mapper.m_inputChannels = 2;
    mapper.m_outputChannels = static_cast<std::uint32_t>(to.size());

    const auto give = [&mapper](std::uint32_t source, std::uint32_t destination, float gain) {
        mapper.m_coefficients[source * kMaxMappedChannels + destination] = gain;
    };

    for (std::uint32_t channel = 0; channel < mapper.m_outputChannels; ++channel) {
        switch (to[channel]) {
        case ChannelPosition::Mono:
            // Half of each, so that two correlated full-scale channels come
            // out at full scale and not 6 dB above it.
            give(kLeft, channel, kStereoToMonoGain);
            give(kRight, channel, kStereoToMonoGain);
            break;
        case ChannelPosition::FrontLeft:
            give(kLeft, channel, 1.0f);
            break;
        case ChannelPosition::FrontRight:
            give(kRight, channel, 1.0f);
            break;
        case ChannelPosition::Aux0:
        case ChannelPosition::Aux1:
        case ChannelPosition::Aux2:
        case ChannelPosition::Aux3:
        case ChannelPosition::Aux4:
        case ChannelPosition::Aux5:
        case ChannelPosition::Aux6:
        case ChannelPosition::Aux7: {
            const StereoContribution contribution = auxContribution(options.aux[auxIndex(to[channel])]);
            give(kLeft, channel, contribution.toLeft);
            give(kRight, channel, contribution.toRight);
            break;
        }
        case ChannelPosition::FrontCentre:
        case ChannelPosition::LowFrequency:
        case ChannelPosition::SideLeft:
        case ChannelPosition::SideRight:
        case ChannelPosition::RearLeft:
        case ChannelPosition::RearRight:
        case ChannelPosition::Unknown:
            // A stereo signal has nothing to say through a centre, a
            // subwoofer or a surround, and inventing something for them is a
            // decision the user has not made. They stay silent unless the
            // block's advanced settings say otherwise.
            break;
        }
    }
    return mapper;
}

float ChannelMapper::coefficient(std::uint32_t inputChannel, std::uint32_t outputChannel) const noexcept RB_NONBLOCKING
{
    if (inputChannel >= m_inputChannels || outputChannel >= m_outputChannels) {
        return 0.0f;
    }
    return m_coefficients[inputChannel * kMaxMappedChannels + outputChannel];
}

void ChannelMapper::process(const AudioBlock &input,
                            const AudioBlock &output,
                            std::uint32_t frames) const noexcept RB_NONBLOCKING
{
    const std::uint32_t outputs = std::min(m_outputChannels, output.channelCount());
    const std::uint32_t inputs = std::min(m_inputChannels, input.channelCount());

    for (std::uint32_t out = 0; out < outputs; ++out) {
        const std::span<float> destination = output.channel(out);
        if (destination.empty()) {
            continue;
        }
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            destination[frame] = 0.0f;
        }
        for (std::uint32_t in = 0; in < inputs; ++in) {
            const float gain = m_coefficients[in * kMaxMappedChannels + out];
            if (gain == 0.0f) {
                continue;
            }
            const std::span<float> source = input.channel(in);
            if (source.empty()) {
                continue; // a port with no storage is silence
            }
            for (std::uint32_t frame = 0; frame < frames; ++frame) {
                destination[frame] += gain * source[frame];
            }
        }
    }
}

} // namespace rb::dsp
