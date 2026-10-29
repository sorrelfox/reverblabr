// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/dynamics.h"

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

/// One ranged control, which is what nearly every one of these is.
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

} // namespace

std::vector<ParamDescriptor> Dynamics::makeDescriptors()
{
    std::vector<ParamDescriptor> descriptors;

    // --- the compressor, above ---
    descriptors.push_back(ranged("threshold", -80.0f, 0.0f, -20.0f, ParamUnit::Decibels));
    // A ratio is a multiplication, so equal distances along the slider should
    // be equal multiples: 2:1 sits as far from 4:1 as 4:1 does from 8:1.
    descriptors.push_back(ranged("ratio", 1.0f, 20.0f, 20.0f, ParamUnit::Ratio, ParamTaper::Logarithmic));

    ParamDescriptor infinite;
    infinite.id = "infiniteRatio";
    infinite.type = ParamType::Bool;
    infinite.defaultValue = ParamValue::ofBool(false);
    descriptors.push_back(std::move(infinite));

    descriptors.push_back(ranged("knee", 0.0f, 24.0f, 12.0f, ParamUnit::Decibels));

    // --- the expander, below ---
    // Off by default: a ratio of 1:1 and a range of nothing leave the quiet
    // end of the curve alone, which is where a general-purpose block should
    // start.
    descriptors.push_back(ranged("expanderThreshold", -120.0f, -20.0f, -120.0f, ParamUnit::Decibels));
    descriptors.push_back(ranged("expanderRatio", 1.0f, 50.0f, 1.0f, ParamUnit::Ratio, ParamTaper::Logarithmic));
    descriptors.push_back(ranged("expanderRange", 0.0f, 80.0f, 0.0f, ParamUnit::Decibels));
    descriptors.push_back(ranged("expanderHold", 0.0f, 500.0f, 0.0f, ParamUnit::Milliseconds));

    // --- the timing ---
    descriptors.push_back(ranged("attack", 0.1f, 200.0f, 1.0f, ParamUnit::Milliseconds, ParamTaper::Logarithmic));
    descriptors.push_back(ranged("release", 10.0f, 3000.0f, 50.0f, ParamUnit::Milliseconds, ParamTaper::Logarithmic));

    descriptors.push_back(ranged("makeup", -40.0f, 40.0f, 0.0f, ParamUnit::Decibels));

    ParamDescriptor detector;
    detector.id = "detector";
    detector.type = ParamType::Enum;
    detector.choices = {std::string{Dynamics::kDetectorPeak}, std::string{Dynamics::kDetectorRms}};
    detector.defaultValue = ParamValue::ofEnum(std::string{Dynamics::kDetectorPeak});
    descriptors.push_back(std::move(detector));
    return descriptors;
}

Dynamics::Dynamics()
    : m_descriptors(makeDescriptors())
    , m_store(m_descriptors)
{
    m_info.typeId = std::string{kTypeId};
    m_info.category = BlockCategory::Effects;
    m_info.inputs = PortSpec::fixed(kChannels, {"in_FL", "in_FR"});
    m_info.outputs = PortSpec::fixed(kChannels, {"out_FL", "out_FR"});
    m_info.face = FaceKind::Curve;
    // **Nothing.** Feed-forward and no lookahead, so a lane with a dynamics
    // block in it is no longer than one without.
    m_info.latencySamples = 0;
}

std::unique_ptr<Block> Dynamics::make()
{
    return std::make_unique<Dynamics>();
}

DynamicsSettings Dynamics::settings() const noexcept RB_NONBLOCKING
{
    DynamicsSettings settings;
    settings.thresholdDb = m_store.number(Threshold);
    // The click past the end of the slider, not a number on it.
    settings.ratio = m_store.number(InfiniteRatio) != 0.0f ? kInfiniteRatio : m_store.number(Ratio);
    settings.kneeDb = m_store.number(Knee);
    settings.expanderThresholdDb = m_store.number(ExpanderThreshold);
    settings.expanderRatio = m_store.number(ExpanderRatio);
    settings.expanderRangeDb = m_store.number(ExpanderRange);
    settings.makeupDb = m_store.number(Makeup);
    return settings;
}

DynamicsTiming Dynamics::timing() const noexcept RB_NONBLOCKING
{
    DynamicsTiming timing;
    timing.attackMs = m_store.number(Attack);
    timing.releaseMs = m_store.number(Release);
    timing.holdMs = m_store.number(ExpanderHold);
    // **The choice's number, not its id.** Asking the store for the value
    // would build a ParamValue holding a string, and a string on the audio
    // thread is an allocation waiting for a longer name.
    timing.detector = m_store.number(Detector) != 0.0f ? DynamicsDetector::Rms : DynamicsDetector::Peak;
    return timing;
}

std::expected<void, BlockError> Dynamics::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, ParamListError> valid = validateParams(m_descriptors); !valid) {
        return std::unexpected(BlockError::InvalidDescriptor);
    }
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }

    m_processor.setSettings(settings());
    m_processor.setTiming(timing());
    // **After the settings**, because preparing works the coefficients out
    // from the timing it has been given.
    m_processor.prepare(spec.sampleRate, kChannels);
    m_meter = spec.meter;
    return {};
}

void Dynamics::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    m_processor.setSettings(settings());
    m_processor.setTiming(timing());

    const std::uint32_t channels = std::min(context.output.channelCount(), context.input.channelCount());
    for (std::uint32_t channel = 0; channel < channels; ++channel) {
        const std::span<float> in = context.input.channel(channel);
        const std::span<float> out = context.output.channel(channel);
        const std::uint32_t frames = std::min<std::uint32_t>(context.frames, static_cast<std::uint32_t>(out.size()));
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            out[frame] = frame < in.size() ? in[frame] : 0.0f;
        }
    }

    // In place on what is about to be sent on, so the levels it reports are
    // the levels of this block's own output.
    m_processor.process(context.output, context.frames);

    if (m_meter != nullptr) {
        m_meter->publish(kInputLevelValue, m_processor.inputLevelDb());
        m_meter->publish(kOutputLevelValue, m_processor.outputLevelDb());
        m_meter->publish(kGainReductionValue, m_processor.gainReduction());
    }
}

void Dynamics::reset() noexcept RB_NONBLOCKING
{
    m_processor.reset();
}

void Dynamics::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    // Every one of them reaches the audio thread this way; the processor is
    // handed the whole curve and the whole timing at the top of each cycle,
    // so a change that arrives mid-drag is in the sound at the next one.
    m_store.applyNumber(index, value);
}

std::expected<void, BlockError> Dynamics::setParam(std::string_view id, const ParamValue &value)
{
    return m_store.set(id, value);
}

ParamValue Dynamics::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap Dynamics::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> Dynamics::loadState(const ParamValueMap &state)
{
    return m_store.load(state);
}

} // namespace rb::blocks
