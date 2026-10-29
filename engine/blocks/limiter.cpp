// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/limiter.h"

#include "dsp/decibel.h"

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

std::vector<ParamDescriptor> Limiter::makeDescriptors()
{
    std::vector<ParamDescriptor> descriptors;

    ParamDescriptor ceiling;
    ceiling.id = "ceiling";
    ceiling.type = ParamType::Float;
    ceiling.minimum = ParamValue::ofFloat(kLowestCeilingDb);
    ceiling.maximum = ParamValue::ofFloat(0.0f);
    ceiling.defaultValue = ParamValue::ofFloat(kDefaultCeilingDb);
    ceiling.unit = ParamUnit::Decibels;
    ceiling.smoothingMs = LookaheadLimiter::kCeilingGlideMs;
    descriptors.push_back(std::move(ceiling));

    ParamDescriptor release;
    release.id = "release";
    release.type = ParamType::Float;
    release.minimum = ParamValue::ofFloat(kShortestReleaseMs);
    release.maximum = ParamValue::ofFloat(kLongestReleaseMs);
    release.defaultValue = ParamValue::ofFloat(kDefaultReleaseMs);
    release.unit = ParamUnit::Milliseconds;
    release.taper = ParamTaper::Logarithmic;
    descriptors.push_back(std::move(release));

    ParamDescriptor truePeak;
    truePeak.id = "truePeak";
    truePeak.type = ParamType::Bool;
    truePeak.defaultValue = ParamValue::ofBool(true);
    descriptors.push_back(std::move(truePeak));
    return descriptors;
}

Limiter::Limiter()
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

std::unique_ptr<Block> Limiter::make()
{
    return std::make_unique<Limiter>();
}

std::expected<void, BlockError> Limiter::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, ParamListError> valid = validateParams(m_descriptors); !valid) {
        return std::unexpected(BlockError::InvalidDescriptor);
    }
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    // **Always ready for true peaks**, so the latency is one number
    // whichever way it is detecting.
    m_limiter.prepare(spec.sampleRate,
                      kChannels,
                      m_store.number(Ceiling),
                      LookaheadLimiter::kDefaultLookaheadMs,
                      m_store.number(Release),
                      true);
    m_info.latencySamples = m_limiter.latencySamples();
    m_meter = spec.meter;
    follow();
    return {};
}

void Limiter::follow() noexcept RB_NONBLOCKING
{
    m_limiter.setCeilingGain(dbToGain(m_store.number(Ceiling)));
    m_limiter.setReleaseMs(m_store.number(Release));
    m_limiter.setTruePeak(m_store.number(TruePeak) != 0.0f);
}

void Limiter::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    follow();
    const std::uint32_t channels = std::min(context.output.channelCount(), kChannels);
    for (std::uint32_t channel = 0; channel < channels; ++channel) {
        const std::span<float> in = context.input.channel(channel);
        const std::span<float> out = context.output.channel(channel);
        const std::uint32_t frames = std::min<std::uint32_t>(context.frames, static_cast<std::uint32_t>(out.size()));
        // Copied forwards and in place when the graph hands one buffer for
        // both, which a copy of each sample onto itself leaves alone.
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            out[frame] = frame < in.size() ? in[frame] : 0.0f;
        }
    }
    m_limiter.process(context.output, context.frames);

    if (m_meter != nullptr) {
        m_meter->publish(kGainReductionValue, gainReductionDb());
    }
}

float Limiter::gainReductionDb() const noexcept RB_NONBLOCKING
{
    return std::max(0.0f, -gainToDb(m_limiter.lowestGain()));
}

void Limiter::reset() noexcept RB_NONBLOCKING
{
    m_limiter.reset();
}

void Limiter::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    // Handed to the limiter at the top of the next cycle; the ceiling glides
    // there.
    m_store.applyNumber(index, value);
}

std::expected<void, BlockError> Limiter::setParam(std::string_view id, const ParamValue &value)
{
    return m_store.set(id, value);
}

ParamValue Limiter::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap Limiter::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> Limiter::loadState(const ParamValueMap &state)
{
    return m_store.load(state);
}

} // namespace rb::blocks
