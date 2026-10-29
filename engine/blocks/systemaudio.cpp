// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/systemaudio.h"

#include <algorithm>
#include <span>
#include <string>
#include <utility>

using namespace rb::dsp;

namespace rb::blocks
{

std::vector<ParamDescriptor> SystemAudio::makeDescriptors()
{
    std::vector<ParamDescriptor> descriptors;

    ParamDescriptor alerts;
    alerts.id = "includeAlerts";
    alerts.type = ParamType::Bool;
    // **Off.** A notification that arrives in the middle of a recording is
    // not part of the recording.
    alerts.defaultValue = ParamValue::ofBool(false);
    // **Which streams are captured is a matter of links.** Turning alerts
    // on has to link the notification streams that are playing now, which
    // the session plan works out; the block is handed whatever arrives at
    // its ports and has no say in it.
    alerts.flags.automatable = false;
    descriptors.push_back(std::move(alerts));

    ParamDescriptor excluded;
    excluded.id = "excluded";
    excluded.type = ParamType::StringList;
    excluded.defaultValue = ParamValue::ofStringList({});
    // The same: an exclusion is a link that has to go.
    excluded.flags.automatable = false;
    descriptors.push_back(std::move(excluded));

    ParamDescriptor lowFrequency;
    lowFrequency.id = "includeLowFrequency";
    lowFrequency.type = ParamType::Bool;
    lowFrequency.defaultValue = ParamValue::ofBool(false);
    // The downmix is built when the block is prepared, which happens as
    // part of planning the session and nowhere else.
    lowFrequency.flags.automatable = false;
    descriptors.push_back(std::move(lowFrequency));
    return descriptors;
}

SystemAudio::SystemAudio()
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

std::unique_ptr<Block> SystemAudio::make()
{
    return std::make_unique<SystemAudio>();
}

std::expected<void, BlockError> SystemAudio::prepare(const PrepareSpec &spec)
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

    DownmixOptions options;
    options.includeLowFrequency = m_store.number(IncludeLowFrequency) != 0.0f;
    const std::expected<ChannelMapper, ChannelMapError> mapper = ChannelMapper::toStereo(layout, options);
    if (!mapper) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }

    std::vector<std::string> labels;
    labels.reserve(layout.size());
    for (const ChannelPosition position : layout) {
        labels.emplace_back("in_" + std::string{channelPositionName(position)});
    }
    // As for every source: nothing wires it, so the block says how many
    // ports it has once it has been told what it is about to be given.
    m_info.inputs = PortSpec::fixed(static_cast<std::uint32_t>(layout.size()), std::move(labels));

    m_layout = std::move(layout);
    m_mapper = *mapper;
    return {};
}

void SystemAudio::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    if (!m_mapper) {
        for (std::uint32_t channel = 0; channel < context.output.channelCount(); ++channel) {
            const std::span<float> out = context.output.channel(channel);
            std::fill_n(out.data(), std::min<std::size_t>(out.size(), context.frames), 0.0f);
        }
        return;
    }
    // Every stream has already been summed at the ports by PipeWire, which
    // is right: they are different sounds going to one place. All that is
    // left is the layout.
    m_mapper->process(context.input, context.output, context.frames);
}

void SystemAudio::reset() noexcept RB_NONBLOCKING { }

void SystemAudio::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    m_store.applyNumber(index, value);
}

std::expected<void, BlockError> SystemAudio::setParam(std::string_view id, const ParamValue &value)
{
    return m_store.set(id, value);
}

ParamValue SystemAudio::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap SystemAudio::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> SystemAudio::loadState(const ParamValueMap &state)
{
    return m_store.load(state);
}

} // namespace rb::blocks
