// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/outputdevice.h"

#include "dsp/decibel.h"
#include "rt/meter.h"

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
    // A channel is named, not numbered: "AUX3" means the same thing on the
    // interface it was chosen on whatever order the ports arrive in.
    descriptor.type = ParamType::String;
    descriptor.defaultValue = ParamValue::ofString(std::string{OutputDevice::kNaturalChannel});
    return descriptor;
}

} // namespace

std::vector<ParamDescriptor> OutputDevice::makeDescriptors()
{
    std::vector<ParamDescriptor> descriptors;

    ParamDescriptor device;
    device.id = "device";
    device.type = ParamType::DeviceRef;
    device.defaultValue = ParamValue::ofDeviceRef(std::string{OutputDevice::kDefaultDevice});
    // **A device is chosen, not turned.** Playing to another one means
    // links to other ports, on a node with the channels that device takes,
    // which only the session plan can work out; a number sent to the block
    // would change nothing at all.
    device.flags.automatable = false;
    descriptors.push_back(std::move(device));

    ParamDescriptor volume;
    volume.id = "volume";
    volume.type = ParamType::Float;
    // **An amplitude, not a percentage and not decibels.** Half is half the
    // signal, which is −6.02 dB; the interface shows whatever a person reads
    // best, and the session file holds the number the audio uses.
    volume.minimum = ParamValue::ofFloat(0.0f);
    volume.maximum = ParamValue::ofFloat(1.0f);
    volume.defaultValue = ParamValue::ofFloat(1.0f);
    volume.smoothingMs = OutputDevice::kVolumeSmoothingMs;
    descriptors.push_back(std::move(volume));

    descriptors.push_back(channelParam("leftChannel"));
    descriptors.push_back(channelParam("rightChannel"));

    ParamDescriptor limit;
    limit.id = "limitDb";
    limit.type = ParamType::Float;
    limit.minimum = ParamValue::ofFloat(kLimitLowestDb);
    limit.maximum = ParamValue::ofFloat(kLimitHighestDb);
    limit.defaultValue = ParamValue::ofFloat(kLimitHighestDb);
    limit.unit = ParamUnit::Decibels;
    // Shown by the panel, and only while the limit is there.
    limit.flags.hidden = true;
    descriptors.push_back(std::move(limit));
    return descriptors;
}

OutputDevice::OutputDevice()
    : m_descriptors(makeDescriptors())
    , m_store(m_descriptors)
    , m_layout(stereoLayout())
{
    m_info.typeId = std::string{kTypeId};
    m_info.category = BlockCategory::Outputs;
    // The outputs are the device's; a wire out of them is refused.
    m_info.outputsToAudioSystem = true;
    m_info.inputs = PortSpec::fixed(2, {"in_FL", "in_FR"});
    m_info.outputs = PortSpec{1, kMaxMappedChannels, {}, 0, {}};
    m_info.face = FaceKind::Icon;
    m_info.latencySamples = 0;
}

std::unique_ptr<Block> OutputDevice::make()
{
    return std::make_unique<OutputDevice>();
}

void OutputDevice::buildMatrix()
{
    for (std::array<float, kMaxMappedChannels> &side : m_matrix) {
        side.fill(0.0f);
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
    const bool chosen = left != kNaturalChannel || right != kNaturalChannel;

    if (!chosen) {
        // **The policy, not a guess.** One matrix, built once, from the
        // coefficients the policy lays down: a mono device gets (L+R)/2 and a
        // wider one gets the front pair with the rest left silent.
        const std::expected<ChannelMapper, ChannelMapError> mapper = ChannelMapper::fromStereo(m_layout);
        if (!mapper) {
            return;
        }
        for (std::uint32_t side = 0; side < 2; ++side) {
            for (std::size_t channel = 0; channel < m_layout.size() && channel < kMaxMappedChannels; ++channel) {
                m_matrix[side][channel] = mapper->coefficient(side, static_cast<std::uint32_t>(channel));
            }
        }
        return;
    }

    // A channel named by hand: the side goes there whole and nowhere else.
    // This is what an audio interface is for — outputs 3 and 4 to the
    // monitors on the other side of the room.
    for (std::size_t channel = 0; channel < m_layout.size() && channel < kMaxMappedChannels; ++channel) {
        const std::string_view name = channelPositionName(m_layout[channel]);
        if (name == left) {
            m_matrix[0][channel] = 1.0f;
        }
        if (name == right) {
            m_matrix[1][channel] = 1.0f;
        }
    }
}

std::expected<void, BlockError> OutputDevice::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, ParamListError> valid = validateParams(m_descriptors); !valid) {
        return std::unexpected(BlockError::InvalidDescriptor);
    }
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }

    ChannelLayout layout = spec.outputLayout;
    if (layout.empty()) {
        // Nobody has said what is on the other end — there is no device at
        // the moment, or nobody asked. A stereo pair is what the ports are
        // then, so that there is something for a device to be linked to when
        // one arrives.
        layout = spec.outputChannels == 1 ? monoLayout() : stereoLayout();
    }
    if (layout.size() > kMaxMappedChannels) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }

    m_layout = std::move(layout);
    std::vector<std::string> labels;
    labels.reserve(m_layout.size());
    for (const ChannelPosition position : m_layout) {
        labels.emplace_back("out_" + std::string{channelPositionName(position)});
    }
    m_info.outputs = PortSpec::fixed(static_cast<std::uint32_t>(m_layout.size()), std::move(labels));

    buildMatrix();
    m_volume.prepare(spec.sampleRate, kVolumeSmoothingMs);
    m_volume.reset(m_store.number(Volume));

    // **Linked across the device's channels**, true peak, and as fast as
    // Soft Lift's own, which is what a chain ending in Soft Lift has.
    m_limiting = m_limitWanted;
    if (m_limiting) {
        m_limit.prepare(spec.sampleRate,
                        static_cast<std::uint32_t>(m_layout.size()),
                        m_limitDb.load(std::memory_order_relaxed),
                        LookaheadLimiter::kDefaultLookaheadMs,
                        LookaheadLimiter::kDefaultReleaseMs,
                        true);
        m_limit.setTruePeak(true);
        m_limit.setRiseDbPerSecond(LookaheadLimiter::kLimitRiseDbPerSecond);
    }
    m_info.latencySamples = m_limiting ? m_limit.latencySamples() : 0;
    m_meter = spec.meter;
    m_prepared = true;
    return {};
}

std::uint32_t OutputDevice::preparedShape() const
{
    return m_limitWanted ? 1 : 0;
}

void OutputDevice::setLastLimit(bool wanted, float ceilingDb) noexcept
{
    m_limitWanted = wanted;
    m_limitDb.store(std::clamp(ceilingDb, kLimitLowestDb, kLimitHighestDb), std::memory_order_relaxed);
}

float OutputDevice::limitReductionDb() const noexcept RB_NONBLOCKING
{
    return m_limiting ? std::max(0.0f, -gainToDb(m_limit.lowestGain())) : 0.0f;
}

void OutputDevice::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    const std::uint32_t outputCount = context.output.channelCount();
    m_volume.setTarget(m_store.number(Volume));

    const std::span<float> left = context.input.channel(0);
    const std::span<float> right = context.input.channelCount() > 1 ? context.input.channel(1) : left;

    for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
        // **One glide for the whole matrix**: every channel of the device
        // moves together, so a volume change cannot pull the image to one
        // side while it happens.
        const float volume = m_volume.next();
        const float l = frame < left.size() ? left[frame] * volume : 0.0f;
        const float r = frame < right.size() ? right[frame] * volume : 0.0f;
        for (std::uint32_t channel = 0; channel < outputCount; ++channel) {
            const std::span<float> out = context.output.channel(channel);
            if (out.empty() || channel >= kMaxMappedChannels) {
                continue;
            }
            out[frame] = l * m_matrix[0][channel] + r * m_matrix[1][channel];
        }
    }

    if (m_limiting) {
        const float ceilingDb = m_limitDb.load(std::memory_order_relaxed);
        m_limit.setCeilingGain(dbToGain(ceilingDb));
        m_limit.process(context.output, context.frames);
        if (m_meter != nullptr) {
            m_meter->publish(kLimitReductionValue, limitReductionDb());
            m_meter->publish(kAppliedLimitValue, ceilingDb);
        }
    } else if (m_meter != nullptr) {
        // **Every cycle, and not once at prepare()**: the block this one
        // replaces goes on publishing until the graph has been swapped.
        m_meter->publish(kLimitReductionValue, 0.0f);
        m_meter->publish(kAppliedLimitValue, 0.0f);
    }
}

void OutputDevice::reset() noexcept RB_NONBLOCKING
{
    m_volume.reset(m_store.number(Volume));
    if (m_limiting) {
        m_limit.reset();
    }
}

void OutputDevice::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    // The volume is the one number that reaches the audio thread, and the
    // smoother reads it at the top of every cycle.
    m_store.applyNumber(index, value);
}

std::expected<void, BlockError> OutputDevice::setParam(std::string_view id, const ParamValue &value)
{
    const std::expected<void, BlockError> set = m_store.set(id, value);
    if (set && m_prepared) {
        // A channel picker changes the matrix, which is not something to
        // rebuild on the audio thread — but it is not a recompile either.
        buildMatrix();
    }
    return set;
}

ParamValue OutputDevice::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap OutputDevice::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> OutputDevice::loadState(const ParamValueMap &state)
{
    const std::expected<void, BlockError> loaded = m_store.load(state);
    if (loaded && m_prepared) {
        buildMatrix();
        m_volume.reset(m_store.number(Volume));
    }
    return loaded;
}

float OutputDevice::coefficient(std::uint32_t side, std::size_t channel) const noexcept
{
    if (side > 1 || channel >= kMaxMappedChannels) {
        return 0.0f;
    }
    return m_matrix[side][channel];
}

} // namespace rb::blocks
