// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/waveformmeter.h"

#include <algorithm>
#include <cmath>
#include <span>
#include <string>
#include <utility>

using namespace rb::dsp;

namespace rb::blocks
{

namespace
{

constexpr std::uint32_t kChannels = 2;

[[nodiscard]] ParamDescriptor choiceParam(const char *id, std::vector<std::string> choices, const char *fallback)
{
    ParamDescriptor descriptor;
    descriptor.id = id;
    descriptor.type = ParamType::Enum;
    descriptor.choices = std::move(choices);
    descriptor.defaultValue = ParamValue::ofEnum(fallback);
    return descriptor;
}

[[nodiscard]] ParamDescriptor switchParam(const char *id, bool fallback)
{
    ParamDescriptor descriptor;
    descriptor.id = id;
    descriptor.type = ParamType::Bool;
    descriptor.defaultValue = ParamValue::ofBool(fallback);
    return descriptor;
}

} // namespace

std::vector<ParamDescriptor> WaveformMeter::makeDescriptors()
{
    std::vector<ParamDescriptor> descriptors;
    // **Above 60 is honest here**: a window reads the rings the audio thread
    // writes, and the sampler's rate does not come into it. What limits it is
    // the screen and the length of a cycle, and the card says so.
    descriptors.push_back(choiceParam("refresh", {"hz10", "hz15", "hz20", "hz30", "hz60", "hz120"}, "hz30"));
    descriptors.push_back(choiceParam("motion", {"realTime", "smooth"}, "realTime"));
    descriptors.push_back(choiceParam("style", {"line", "filled", "bars", "dots"}, "line"));

    ParamDescriptor span;
    span.id = "spanMs";
    span.type = ParamType::Float;
    span.unit = ParamUnit::Milliseconds;
    span.taper = ParamTaper::Logarithmic;
    span.minimum = ParamValue::ofFloat(kShortestSpanMs);
    span.maximum = ParamValue::ofFloat(kLongestSpanMs);
    span.defaultValue = ParamValue::ofFloat(kDefaultSpanMs);
    descriptors.push_back(std::move(span));

    ParamDescriptor gain;
    gain.id = "gainDb";
    gain.type = ParamType::Float;
    gain.unit = ParamUnit::Decibels;
    gain.minimum = ParamValue::ofFloat(kLowestGainDb);
    gain.maximum = ParamValue::ofFloat(kHighestGainDb);
    gain.defaultValue = ParamValue::ofFloat(0.0f);
    descriptors.push_back(std::move(gain));

    descriptors.push_back(switchParam("autoFit", false));
    descriptors.push_back(choiceParam("channels", {"both", "left", "right", "mid", "side", "stacked"}, "both"));
    descriptors.push_back(switchParam("trigger", false));

    // A moment somebody stopped to look at, and no way of looking.
    ParamDescriptor hold = switchParam("hold", false);
    hold.flags.inPresets = false;
    descriptors.push_back(std::move(hold));

    descriptors.push_back(switchParam("grid", true));
    descriptors.push_back(choiceParam("scale", {"linear", "decibels"}, "linear"));
    return descriptors;
}

WaveformMeter::WaveformMeter()
    : m_descriptors(makeDescriptors())
    , m_store(m_descriptors)
{
    m_info.typeId = std::string{kTypeId};
    m_info.category = BlockCategory::Meters;
    m_info.gainReach = GainReach::Unity;
    m_info.inputs = PortSpec::fixed(kChannels, {"in_FL", "in_FR"});
    m_info.outputs = PortSpec::fixed(kChannels, {"out_FL", "out_FR"});
    m_info.face = FaceKind::Meter;
    m_info.latencySamples = 0;
}

std::unique_ptr<Block> WaveformMeter::make()
{
    return std::make_unique<WaveformMeter>();
}

std::expected<void, BlockError> WaveformMeter::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, ParamListError> valid = validateParams(m_descriptors); !valid) {
        return std::unexpected(BlockError::InvalidDescriptor);
    }
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    m_sampleRate = static_cast<std::uint32_t>(std::lround(spec.sampleRate));
    m_meter = spec.meter;
    reset();
    return {};
}

void WaveformMeter::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    const std::uint32_t channels = std::min({context.output.channelCount(), context.input.channelCount(), kChannels});
    const float *sides[kChannels] = {nullptr, nullptr};
    for (std::uint32_t channel = 0; channel < channels; ++channel) {
        const std::span<const float> in = context.input.channel(channel);
        const std::span<float> out = context.output.channel(channel);
        const std::size_t frames = std::min({in.size(), out.size(), static_cast<std::size_t>(context.frames)});
        // **Sample for sample.** Something to look at, not something in the
        // way.
        if (out.data() != in.data()) {
            std::copy_n(in.data(), frames, out.data());
        }
        sides[channel] = frames >= context.frames ? out.data() : nullptr;
    }
    if (m_meter != nullptr) {
        m_meter->feedScope(m_scope, sides[0], sides[1], context.frames, m_sampleRate);
    }
}

void WaveformMeter::reset() noexcept RB_NONBLOCKING
{
    // What is half formed in the coarser rings is of the sound before this.
    m_scope.forget();
}

void WaveformMeter::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    m_store.applyNumber(index, value);
}

std::expected<void, BlockError> WaveformMeter::setParam(std::string_view id, const ParamValue &value)
{
    return m_store.set(id, value);
}

ParamValue WaveformMeter::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap WaveformMeter::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> WaveformMeter::loadState(const ParamValueMap &state)
{
    return m_store.load(state);
}

} // namespace rb::blocks
