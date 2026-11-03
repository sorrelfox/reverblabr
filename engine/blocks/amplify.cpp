// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/amplify.h"

#include <algorithm>
#include <span>
#include <string>
#include <utility>

using namespace rb::dsp;

namespace rb::blocks
{

namespace
{

constexpr std::uint32_t kChannels = 2;

} // namespace

std::vector<ParamDescriptor> Amplify::makeDescriptors()
{
    std::vector<ParamDescriptor> descriptors;

    ParamDescriptor gain;
    gain.id = "gain";
    gain.type = ParamType::Float;
    gain.minimum = ParamValue::ofFloat(0.0f);
    gain.maximum = ParamValue::ofFloat(kHighestGain);
    gain.defaultValue = ParamValue::ofFloat(1.0f);
    gain.smoothingMs = kGainSmoothingMs;
    descriptors.push_back(std::move(gain));
    return descriptors;
}

Amplify::Amplify()
    : m_descriptors(makeDescriptors())
    , m_store(m_descriptors)
{
    m_info.typeId = std::string{kTypeId};
    m_info.category = BlockCategory::Effects;
    m_info.inputs = PortSpec::fixed(kChannels, {"in_FL", "in_FR"});
    m_info.outputs = PortSpec::fixed(kChannels, {"out_FL", "out_FR"});
    m_info.face = FaceKind::Value;
    m_info.latencySamples = 0;
}

std::unique_ptr<Block> Amplify::make()
{
    return std::make_unique<Amplify>();
}

std::expected<void, BlockError> Amplify::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, ParamListError> valid = validateParams(m_descriptors); !valid) {
        return std::unexpected(BlockError::InvalidDescriptor);
    }
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    m_gain.prepare(spec.sampleRate, kGainSmoothingMs);
    m_gain.reset(m_store.number(Gain));
    m_prepared = true;
    return {};
}

void Amplify::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    m_gain.setTarget(m_store.number(Gain));
    const std::uint32_t channels = std::min(context.output.channelCount(), context.input.channelCount());
    for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
        // One gain for both sides, so nothing pulls the image over while it
        // moves.
        const float gain = m_gain.next();
        for (std::uint32_t channel = 0; channel < channels; ++channel) {
            const std::span<float> in = context.input.channel(channel);
            const std::span<float> out = context.output.channel(channel);
            if (frame < out.size()) {
                out[frame] = frame < in.size() ? in[frame] * gain : 0.0f;
            }
        }
    }
}

void Amplify::reset() noexcept RB_NONBLOCKING
{
    m_gain.reset(m_store.number(Gain));
}

void Amplify::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    m_store.applyNumber(index, value);
}

std::expected<void, BlockError> Amplify::setParam(std::string_view id, const ParamValue &value)
{
    return m_store.set(id, value);
}

ParamValue Amplify::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap Amplify::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> Amplify::loadState(const ParamValueMap &state)
{
    const std::expected<void, BlockError> loaded = m_store.load(state);
    if (loaded && m_prepared) {
        m_gain.reset(m_store.number(Gain));
    }
    return loaded;
}

} // namespace rb::blocks
