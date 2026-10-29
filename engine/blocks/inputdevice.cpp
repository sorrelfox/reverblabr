// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/inputdevice.h"

#include <algorithm>
#include <span>
#include <string>
#include <utility>

using namespace rb::dsp;

namespace rb::blocks
{

namespace
{

[[nodiscard]] ParamDescriptor channelParam(std::string_view id)
{
    ParamDescriptor descriptor;
    descriptor.id = std::string{id};
    descriptor.type = ParamType::String;
    descriptor.defaultValue = ParamValue::ofString(std::string{InputDevice::kNaturalChannel});
    return descriptor;
}

} // namespace

std::vector<ParamDescriptor> InputDevice::makeDescriptors()
{
    std::vector<ParamDescriptor> descriptors;

    ParamDescriptor device;
    device.id = "device";
    device.type = ParamType::DeviceRef;
    device.defaultValue = ParamValue::ofDeviceRef(std::string{InputDevice::kDefaultDevice});
    // **A device is chosen, not turned.** Recording from another one means
    // links from other ports, which only the session plan can work out; a
    // number sent to the block would change nothing at all.
    device.flags.automatable = false;
    descriptors.push_back(std::move(device));

    descriptors.push_back(channelParam("leftChannel"));
    descriptors.push_back(channelParam("rightChannel"));
    return descriptors;
}

InputDevice::InputDevice()
    : m_descriptors(makeDescriptors())
    , m_store(m_descriptors)
    , m_layout(stereoLayout())
{
    m_info.typeId = std::string{kTypeId};
    m_info.category = BlockCategory::Sources;
    // The audio system fills the inputs; a wire into them is refused.
    m_info.inputsFromAudioSystem = true;
    m_info.inputs = PortSpec{1, kMaxMappedChannels, {}, 0, {}};
    m_info.outputs = PortSpec::fixed(2, {"out_FL", "out_FR"});
    m_info.face = FaceKind::Icon;
    m_info.latencySamples = 0;
}

std::unique_ptr<Block> InputDevice::make()
{
    return std::make_unique<InputDevice>();
}

void InputDevice::buildMatrix()
{
    for (std::array<float, 2> &channel : m_matrix) {
        channel.fill(0.0f);
    }

    // **The value is named, and the view looks into the name.** `value()`
    // hands back a ParamValue by value and `asText()` looks into that
    // object's own string, so reading the two in one expression leaves a
    // view onto a string that died at the semicolon — which a debug build
    // hides and an optimised one turns into the wrong channel. The rvalue
    // overloads of asText() are deleted, so this is the shape that compiles.
    const ParamValue leftChoice = m_store.value(LeftChannel);
    const ParamValue rightChoice = m_store.value(RightChannel);
    const std::string_view left = leftChoice.asText().value_or(kNaturalChannel);
    const std::string_view right = rightChoice.asText().value_or(kNaturalChannel);

    if (left == kNaturalChannel && right == kNaturalChannel) {
        // The policy: a mono device on both sides at unity, a stereo pair
        // straight through, a wider device folded down.
        const std::expected<ChannelMapper, ChannelMapError> mapper = ChannelMapper::toStereo(m_layout);
        if (!mapper) {
            return;
        }
        for (std::size_t channel = 0; channel < m_layout.size() && channel < kMaxMappedChannels; ++channel) {
            for (std::uint32_t side = 0; side < 2; ++side) {
                m_matrix[channel][side] = mapper->coefficient(static_cast<std::uint32_t>(channel), side);
            }
        }
        return;
    }

    // A channel chosen by hand: input 3 to the left, input 4 to the right,
    // which is what a multichannel interface is for. A side that names a
    // channel the device does not have hears nothing, which is honest: the
    // device it was set up for is not this one.
    for (std::size_t channel = 0; channel < m_layout.size() && channel < kMaxMappedChannels; ++channel) {
        const std::string_view name = channelPositionName(m_layout[channel]);
        if (name == left) {
            m_matrix[channel][0] = 1.0f;
        }
        if (name == right) {
            m_matrix[channel][1] = 1.0f;
        }
    }
}

std::expected<void, BlockError> InputDevice::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, ParamListError> valid = validateParams(m_descriptors); !valid) {
        return std::unexpected(BlockError::InvalidDescriptor);
    }
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }

    ChannelLayout layout = spec.inputLayout;
    if (layout.empty()) {
        layout = spec.inputChannels == 1 ? monoLayout() : stereoLayout();
    }
    if (layout.size() > kMaxMappedChannels) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }

    m_layout = std::move(layout);
    std::vector<std::string> labels;
    labels.reserve(m_layout.size());
    for (const ChannelPosition position : m_layout) {
        labels.emplace_back("in_" + std::string{channelPositionName(position)});
    }
    // As for every source: the compiler works a flexible port count out from
    // the wires that arrive, and nothing wires a device.
    m_info.inputs = PortSpec::fixed(static_cast<std::uint32_t>(m_layout.size()), std::move(labels));

    buildMatrix();
    m_prepared = true;
    return {};
}

void InputDevice::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    const std::uint32_t inputCount = context.input.channelCount();
    const std::span<float> left = context.output.channel(0);
    const std::span<float> right = context.output.channelCount() > 1 ? context.output.channel(1) : left;

    for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
        float l = 0.0f;
        float r = 0.0f;
        for (std::uint32_t channel = 0; channel < inputCount && channel < kMaxMappedChannels; ++channel) {
            const std::span<float> in = context.input.channel(channel);
            // An input port nothing is linked to arrives as silence, not as
            // whatever was in the memory.
            const float sample = frame < in.size() ? in[frame] : 0.0f;
            l += sample * m_matrix[channel][0];
            r += sample * m_matrix[channel][1];
        }
        if (frame < left.size()) {
            left[frame] = l;
        }
        if (frame < right.size()) {
            right[frame] = r;
        }
    }
}

void InputDevice::reset() noexcept RB_NONBLOCKING
{
    // Nothing is remembered between cycles: the matrix is the whole block.
}

void InputDevice::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    m_store.applyNumber(index, value);
}

std::expected<void, BlockError> InputDevice::setParam(std::string_view id, const ParamValue &value)
{
    const std::expected<void, BlockError> set = m_store.set(id, value);
    if (set && m_prepared) {
        buildMatrix();
    }
    return set;
}

ParamValue InputDevice::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap InputDevice::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> InputDevice::loadState(const ParamValueMap &state)
{
    const std::expected<void, BlockError> loaded = m_store.load(state);
    if (loaded && m_prepared) {
        buildMatrix();
    }
    return loaded;
}

float InputDevice::coefficient(std::size_t channel, std::uint32_t side) const noexcept
{
    if (channel >= kMaxMappedChannels || side > 1) {
        return 0.0f;
    }
    return m_matrix[channel][side];
}

} // namespace rb::blocks
