// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/simplecompressor.h"

#include <algorithm>
#include <array>
#include <string>

using namespace rb::dsp;

namespace rb::blocks
{

namespace
{

constexpr std::uint32_t kChannels = 2;

/// **Our own tuning.** Gentle, Moderate and Heavy each start lower, hold
/// back harder and react faster than the one before. Every makeup gain puts
/// back between a third and two fifths of what a full-scale signal loses, so
/// a firmer mode sounds denser rather than quieter. A full-scale input comes
/// out at −6 dBFS or below in every mode but Off, so no mode can make a
/// clean signal clip.
///
/// The two gentler modes follow an average (RMS), which is what a person
/// hears as loudness; the three firmer ones follow the peaks, because what
/// they are for — speech and anything going out on air — is where a peak
/// slipping through is the thing heard.
constexpr std::array kModes{
    // A flat curve: 1:1 from the top, nothing added.
    SimpleCompressor::Mode{"off",
                           {.thresholdDb = 0.0f, .ratio = 1.0f, .kneeDb = 0.0f, .makeupDb = 0.0f},
                           {.attackMs = 10.0f, .releaseMs = 100.0f, .detector = DynamicsDetector::Peak}},
    SimpleCompressor::Mode{"gentle",
                           {.thresholdDb = -18.0f, .ratio = 2.0f, .kneeDb = 12.0f, .makeupDb = 3.0f},
                           {.attackMs = 20.0f, .releaseMs = 250.0f, .detector = DynamicsDetector::Rms}},
    SimpleCompressor::Mode{"moderate",
                           {.thresholdDb = -20.0f, .ratio = 3.0f, .kneeDb = 10.0f, .makeupDb = 5.0f},
                           {.attackMs = 10.0f, .releaseMs = 200.0f, .detector = DynamicsDetector::Rms}},
    SimpleCompressor::Mode{"heavy",
                           {.thresholdDb = -24.0f, .ratio = 6.0f, .kneeDb = 6.0f, .makeupDb = 8.0f},
                           {.attackMs = 5.0f, .releaseMs = 150.0f, .detector = DynamicsDetector::Peak}},
    // Quick enough to catch a syllable, slow enough to let the breath
    // between two of them come back up.
    SimpleCompressor::Mode{"voice",
                           {.thresholdDb = -22.0f, .ratio = 4.0f, .kneeDb = 8.0f, .makeupDb = 6.0f},
                           {.attackMs = 3.0f, .releaseMs = 120.0f, .detector = DynamicsDetector::Peak}},
    // Dense and even: close to a limiter above a high threshold.
    SimpleCompressor::Mode{"broadcast",
                           {.thresholdDb = -16.0f, .ratio = 10.0f, .kneeDb = 4.0f, .makeupDb = 6.0f},
                           {.attackMs = 1.0f, .releaseMs = 80.0f, .detector = DynamicsDetector::Peak}},
};

} // namespace

std::span<const SimpleCompressor::Mode> SimpleCompressor::modes() noexcept
{
    return kModes;
}

const SimpleCompressor::Mode *SimpleCompressor::findMode(std::string_view id) noexcept
{
    const auto found = std::ranges::find(kModes, id, &Mode::id);
    return found == kModes.end() ? nullptr : &*found;
}

std::vector<ParamDescriptor> SimpleCompressor::makeDescriptors()
{
    ParamDescriptor mode;
    mode.id = "mode";
    mode.type = ParamType::Enum;
    for (const Mode &each : kModes) {
        mode.choices.emplace_back(each.id);
    }
    mode.defaultValue = ParamValue::ofEnum(std::string{kDefaultMode});

    std::vector<ParamDescriptor> descriptors;
    descriptors.push_back(std::move(mode));
    return descriptors;
}

SimpleCompressor::SimpleCompressor()
    : m_descriptors(makeDescriptors())
    , m_store(m_descriptors)
{
    m_info.typeId = std::string{kTypeId};
    m_info.category = BlockCategory::Effects;
    m_info.inputs = PortSpec::fixed(kChannels, {"in_FL", "in_FR"});
    m_info.outputs = PortSpec::fixed(kChannels, {"out_FL", "out_FR"});
    m_info.face = FaceKind::Text;
    // Feed-forward with no lookahead, like the Dynamics block it is built on.
    m_info.latencySamples = 0;
}

std::unique_ptr<Block> SimpleCompressor::make()
{
    return std::make_unique<SimpleCompressor>();
}

const SimpleCompressor::Mode &SimpleCompressor::mode() const noexcept RB_NONBLOCKING
{
    // **The choice's position, not its id**, which would be a string on the
    // audio thread. The store keeps it within the list.
    const auto index = static_cast<std::size_t>(m_store.number(ModeParam));
    return kModes[std::min(index, kModes.size() - 1)];
}

std::expected<void, BlockError> SimpleCompressor::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, ParamListError> valid = validateParams(m_descriptors); !valid) {
        return std::unexpected(BlockError::InvalidDescriptor);
    }
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }

    m_processor.setSettings(settings());
    m_processor.setTiming(timing());
    // After the settings, because preparing works the coefficients out from
    // the timing it has been given.
    m_processor.prepare(spec.sampleRate, kChannels);
    return {};
}

void SimpleCompressor::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    // **A change of mode is a change of target, not of state.** The gain the
    // processor is applying stays where it is and the new mode's attack or
    // release takes it to the new curve; the makeup has its own glide. That
    // is what keeps switching modes from being heard as a click.
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
    m_processor.process(context.output, context.frames);
}

void SimpleCompressor::reset() noexcept RB_NONBLOCKING
{
    m_processor.reset();
}

void SimpleCompressor::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    m_store.applyNumber(index, value);
}

std::expected<void, BlockError> SimpleCompressor::setParam(std::string_view id, const ParamValue &value)
{
    return m_store.set(id, value);
}

ParamValue SimpleCompressor::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap SimpleCompressor::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> SimpleCompressor::loadState(const ParamValueMap &state)
{
    return m_store.load(state);
}

} // namespace rb::blocks
