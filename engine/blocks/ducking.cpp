// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/ducking.h"

#include "dsp/decibel.h"
#include "rt/meter.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

using namespace rb::dsp;

namespace rb::blocks
{

namespace
{

constexpr std::uint32_t kLaneWidth = 2;
constexpr std::uint32_t kMainLeft = 0;
constexpr std::uint32_t kBackgroundLeft = kLaneWidth;

[[nodiscard]] ParamDescriptor ranged(std::string id,
                                     float lowest,
                                     float highest,
                                     float byDefault,
                                     ParamUnit unit,
                                     ParamTaper taper = ParamTaper::Linear)
{
    ParamDescriptor descriptor;
    descriptor.id = std::move(id);
    descriptor.type = ParamType::Float;
    descriptor.minimum = ParamValue::ofFloat(lowest);
    descriptor.maximum = ParamValue::ofFloat(highest);
    descriptor.defaultValue = ParamValue::ofFloat(byDefault);
    descriptor.unit = unit;
    descriptor.taper = taper;
    return descriptor;
}

[[nodiscard]] std::uint32_t samplesOf(float ms, double sampleRate) noexcept RB_NONBLOCKING
{
    return static_cast<std::uint32_t>(std::max(1.0, std::round(static_cast<double>(ms) * 1.0e-3 * sampleRate)));
}

} // namespace

std::vector<ParamDescriptor> Ducking::makeDescriptors()
{
    std::vector<ParamDescriptor> descriptors;
    // Where the main signal counts as being there.
    descriptors.push_back(ranged("threshold", -60.0f, 0.0f, -30.0f, ParamUnit::Decibels));
    // **An amplitude, shown as a percentage**, as the Volume block's gain
    // is: 0.4 is the background at 40 % of itself, about −8 dB.
    descriptors.push_back(ranged("duckedVolume", 0.0f, 1.0f, 0.4f, ParamUnit::Percent));
    descriptors.push_back(ranged("attack", 5.0f, 1000.0f, 50.0f, ParamUnit::Milliseconds, ParamTaper::Logarithmic));
    descriptors.push_back(ranged("hold", 0.0f, 2000.0f, 300.0f, ParamUnit::Milliseconds));
    descriptors.push_back(ranged("release", 10.0f, 5000.0f, 500.0f, ParamUnit::Milliseconds, ParamTaper::Logarithmic));
    return descriptors;
}

Ducking::Ducking()
    : m_descriptors(makeDescriptors())
    , m_store(m_descriptors)
{
    m_info.typeId = std::string{kTypeId};
    m_info.category = BlockCategory::Advanced;
    m_info.inputs =
        PortSpec::lanes(kLaneWidth, {"main", "background"}, {"main_FL", "main_FR", "background_FL", "background_FR"});
    m_info.outputs = PortSpec::fixed(2, {"out_FL", "out_FR"});
    m_info.face = FaceKind::Value;
    m_info.latencySamples = 0;
}

std::unique_ptr<Block> Ducking::make()
{
    return std::make_unique<Ducking>();
}

std::expected<void, BlockError> Ducking::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, ParamListError> valid = validateParams(m_descriptors); !valid) {
        return std::unexpected(BlockError::InvalidDescriptor);
    }
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    m_sampleRate = spec.sampleRate;
    m_detector.prepare(spec.sampleRate, kDetectorReleaseMs);
    m_meter = spec.meter;
    reset();
    return {};
}

void Ducking::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    const float threshold = dbToGain(m_store.number(Threshold));
    const float ducked = std::clamp(m_store.number(DuckedVolume), 0.0f, 1.0f);
    const auto attackSamples = static_cast<float>(samplesOf(m_store.number(Attack), m_sampleRate));
    const auto releaseSamples = static_cast<float>(samplesOf(m_store.number(Release), m_sampleRate));
    const std::uint32_t holdSamples = samplesOf(m_store.number(Hold), m_sampleRate);
    const float depth = 1.0f - ducked;

    const std::uint32_t inputs = context.input.channelCount();
    const std::uint32_t outputs = context.output.channelCount();
    const auto channel = [&](std::uint32_t index) -> std::span<float> {
        return index < inputs ? context.input.channel(index) : std::span<float>{};
    };

    for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
        // **The louder side of the main signal decides**, so a voice panned
        // to one side ducks the music as surely as one in the middle.
        float loudest = 0.0f;
        for (std::uint32_t side = 0; side < kLaneWidth; ++side) {
            const std::span<float> main = channel(kMainLeft + side);
            if (frame < main.size()) {
                loudest = std::max(loudest, std::fabs(main[frame]));
            }
        }
        m_detector.push(loudest);
        m_present = m_detector.envelope() >= threshold;
        if (m_present) {
            m_hold = holdSamples;
        } else if (m_hold > 0) {
            --m_hold;
        }
        const float target = m_present || m_hold > 0 ? ducked : 1.0f;

        // **The whole distance in the whole time**: a step a sample of the
        // depth over the attack or the release. After the ducked volume has
        // been moved further than the depth still to go, the step is the
        // distance itself over the time, so a gain the depth no longer
        // spans still comes home.
        const float distance = std::fabs(target - m_gain);
        if (distance > 0.0f) {
            const float span = std::max(depth, distance);
            const float step = span / (target < m_gain ? attackSamples : releaseSamples);
            m_gain = distance <= step ? target : m_gain + (target < m_gain ? -step : step);
        }

        for (std::uint32_t side = 0; side < outputs; ++side) {
            const std::span<float> out = context.output.channel(side);
            if (frame >= out.size()) {
                continue;
            }
            const std::span<float> main = channel(kMainLeft + side);
            const std::span<float> background =
                side < kLaneWidth ? channel(kBackgroundLeft + side) : std::span<float>{};
            const float voice = frame < main.size() ? main[frame] : 0.0f;
            const float music = frame < background.size() ? background[frame] : 0.0f;
            out[frame] = voice + music * m_gain;
        }
    }

    if (m_meter != nullptr) {
        m_meter->publish(kDuckingValue, m_gain < 1.0f ? gainToDb(m_gain) : 0.0f);
        m_meter->publish(kActiveValue, m_present || m_hold > 0 ? 1.0f : 0.0f);
    }
}

void Ducking::reset() noexcept RB_NONBLOCKING
{
    m_detector.reset();
    m_gain = 1.0f;
    m_hold = 0;
    m_present = false;
}

void Ducking::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    // Every one of them is read at the top of the next cycle; the ramp is
    // what keeps a change of ducked volume from being heard as a step.
    m_store.applyNumber(index, value);
}

std::expected<void, BlockError> Ducking::setParam(std::string_view id, const ParamValue &value)
{
    return m_store.set(id, value);
}

ParamValue Ducking::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap Ducking::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> Ducking::loadState(const ParamValueMap &state)
{
    return m_store.load(state);
}

} // namespace rb::blocks
