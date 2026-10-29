// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "gainblock.h"

#include "dsp/audioblock.h"
#include "dsp/decibel.h"
#include "dsp/denormals.h"

#include <span>

namespace rb::test
{

using namespace rb::dsp;

std::vector<ParamDescriptor> GainBlock::makeDescriptors()
{
    std::vector<ParamDescriptor> descriptors;

    ParamDescriptor gain;
    gain.id = "gainDb";
    gain.type = ParamType::Float;
    gain.minimum = ParamValue::ofFloat(-60.0f);
    gain.maximum = ParamValue::ofFloat(12.0f);
    gain.defaultValue = ParamValue::ofFloat(0.0f);
    gain.unit = ParamUnit::Decibels;
    gain.taper = ParamTaper::Decibels;
    gain.smoothingMs = 20.0f;
    descriptors.push_back(std::move(gain));

    ParamDescriptor muted;
    muted.id = "muted";
    muted.type = ParamType::Bool;
    muted.defaultValue = ParamValue::ofBool(false);
    descriptors.push_back(std::move(muted));

    ParamDescriptor channels;
    channels.id = "channels";
    channels.type = ParamType::Enum;
    channels.choices = {"both", "left", "right"};
    channels.defaultValue = ParamValue::ofEnum("both");
    descriptors.push_back(std::move(channels));

    ParamDescriptor device;
    device.id = "device";
    device.type = ParamType::DeviceRef;
    device.defaultValue = ParamValue::ofDeviceRef("default");
    device.flags.automatable = false;
    descriptors.push_back(std::move(device));

    ParamDescriptor applied;
    applied.id = "appliedGainDb";
    applied.type = ParamType::Float;
    applied.minimum = ParamValue::ofFloat(-60.0f);
    applied.maximum = ParamValue::ofFloat(12.0f);
    applied.defaultValue = ParamValue::ofFloat(0.0f);
    applied.unit = ParamUnit::Decibels;
    applied.flags.readOnly = true;
    applied.flags.automatable = false;
    descriptors.push_back(std::move(applied));

    return descriptors;
}

GainBlock::GainBlock()
    : m_descriptors(makeDescriptors())
    , m_store(m_descriptors)
{
    m_info.typeId = std::string{kTypeId};
    m_info.category = BlockCategory::Effects;
    m_info.inputs = PortSpec::fixed(2, {"in_FL", "in_FR"});
    m_info.outputs = PortSpec::fixed(2, {"out_FL", "out_FR"});
    m_info.face = FaceKind::Value;
    m_info.latencySamples = 0;
}

std::unique_ptr<Block> GainBlock::make()
{
    return std::make_unique<GainBlock>();
}

std::expected<void, BlockError> GainBlock::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, ParamListError> valid = validateParams(m_descriptors); !valid) {
        return std::unexpected(BlockError::InvalidDescriptor);
    }
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    m_gain.prepare(spec.sampleRate, m_descriptors[GainDb].smoothingMs);
    m_gain.reset(dbToGain(m_store.number(GainDb)));
    m_prepared = true;
    return {};
}

void GainBlock::reset() noexcept RB_NONBLOCKING
{
    m_gain.reset(dbToGain(m_store.number(GainDb)));
}

void GainBlock::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    m_store.applyNumber(index, value);
}

void GainBlock::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    const ScopedDenormalsOff denormalsOff;
    ++m_cycles;

    const bool muted = m_store.number(Muted) != 0.0f;
    const auto channels = static_cast<std::uint32_t>(m_store.number(Channels));
    m_gain.setTarget(muted ? 0.0f : dbToGain(m_store.number(GainDb)));

    const std::uint32_t outputCount = context.output.channelCount();
    for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
        const float gain = m_gain.next();
        for (std::uint32_t channel = 0; channel < outputCount; ++channel) {
            const std::span<float> out = context.output.channel(channel);
            if (out.empty()) {
                continue;
            }
            // A channel with no storage is silence, which is what an
            // unconnected input port arrives as.
            const std::span<float> in = context.input.channel(channel);
            const float sample = in.empty() ? 0.0f : in[frame];
            // "left" is channel 0 and "right" is channel 1; a channel the
            // enum leaves out passes through untouched.
            const bool touched = channels == 0 || channels == channel + 1;
            out[frame] = touched ? sample * gain : sample;
        }
    }

    m_store.applyNumber(AppliedGainDb, muted ? kSilenceDb : m_store.number(GainDb));
}

std::expected<void, BlockError> GainBlock::setParam(std::string_view id, const ParamValue &value)
{
    return m_store.set(id, value);
}

ParamValue GainBlock::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap GainBlock::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> GainBlock::loadState(const ParamValueMap &state)
{
    return m_store.load(state);
}

} // namespace rb::test
