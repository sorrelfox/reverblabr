// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/echo.h"

#include <algorithm>
#include <cmath>
#include <span>
#include <string>
#include <utility>

using namespace rb::dsp;

namespace rb::blocks
{

std::vector<ParamDescriptor> Echo::makeDescriptors()
{
    std::vector<ParamDescriptor> descriptors;

    ParamDescriptor time;
    time.id = "time";
    time.type = ParamType::Float;
    time.minimum = ParamValue::ofFloat(kShortestTimeMs);
    time.maximum = ParamValue::ofFloat(kLongestTimeMs);
    time.defaultValue = ParamValue::ofFloat(kDefaultTimeMs);
    time.unit = ParamUnit::Milliseconds;
    time.taper = ParamTaper::Logarithmic;
    descriptors.push_back(std::move(time));

    ParamDescriptor feedback;
    feedback.id = "feedback";
    feedback.type = ParamType::Float;
    feedback.minimum = ParamValue::ofFloat(0.0f);
    feedback.maximum = ParamValue::ofFloat(kHighestFeedback);
    feedback.defaultValue = ParamValue::ofFloat(kDefaultFeedback);
    feedback.unit = ParamUnit::Percent;
    feedback.smoothingMs = kGlideMs;
    descriptors.push_back(std::move(feedback));

    ParamDescriptor tone;
    tone.id = "tone";
    tone.type = ParamType::Float;
    tone.minimum = ParamValue::ofFloat(kLowestToneHz);
    tone.maximum = ParamValue::ofFloat(kHighestToneHz);
    tone.defaultValue = ParamValue::ofFloat(kDefaultToneHz);
    tone.unit = ParamUnit::Hertz;
    tone.taper = ParamTaper::Logarithmic;
    tone.smoothingMs = kToneGlideMs;
    descriptors.push_back(std::move(tone));

    ParamDescriptor mix;
    mix.id = "mix";
    mix.type = ParamType::Float;
    mix.minimum = ParamValue::ofFloat(0.0f);
    mix.maximum = ParamValue::ofFloat(100.0f);
    mix.defaultValue = ParamValue::ofFloat(kDefaultMix);
    mix.unit = ParamUnit::Percent;
    mix.smoothingMs = kGlideMs;
    descriptors.push_back(std::move(mix));
    return descriptors;
}

Echo::Echo()
    : m_descriptors(makeDescriptors())
    , m_store(m_descriptors)
{
    m_info.typeId = std::string{kTypeId};
    m_info.category = BlockCategory::Effects;
    m_info.inputs = PortSpec::fixed(kChannels, {"in_FL", "in_FR"});
    m_info.outputs = PortSpec::fixed(kChannels, {"out_FL", "out_FR"});
    m_info.face = FaceKind::Value;
    // **None**: the original goes straight through; see the class comment.
    m_info.latencySamples = 0;
}

std::unique_ptr<Block> Echo::make()
{
    return std::make_unique<Echo>();
}

std::uint32_t Echo::targetSamples() const noexcept RB_NONBLOCKING
{
    const double samples = static_cast<double>(m_store.number(Time)) * 1.0e-3 * m_sampleRate;
    const auto whole = static_cast<std::uint32_t>(std::lround(samples));
    return std::clamp<std::uint32_t>(whole, 1, static_cast<std::uint32_t>(m_length - 1));
}

void Echo::design(float octaves) noexcept RB_NONBLOCKING
{
    m_toneFilter =
        designSvf({FilterShape::LowPass, std::exp2(static_cast<double>(octaves)), kButterworthQ, 0.0}, m_sampleRate);
}

void Echo::settle() noexcept RB_NONBLOCKING
{
    m_feedback.reset(m_store.number(Feedback) * 0.01f);
    m_mix.reset(m_store.number(Mix) * 0.01f);
    m_toneOctaves.reset(std::log2(m_store.number(Tone)));
    design(m_toneOctaves.current());
    m_from = targetSamples();
    m_to = m_from;
    m_fadeLeft = 0;
}

std::expected<void, BlockError> Echo::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, ParamListError> valid = validateParams(m_descriptors); !valid) {
        return std::unexpected(BlockError::InvalidDescriptor);
    }
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    m_sampleRate = spec.sampleRate;
    // One more slot than the longest delay, so the sample about to be read
    // is never the one being written.
    m_length = static_cast<std::size_t>(std::ceil(static_cast<double>(kLongestTimeMs) * 1.0e-3 * spec.sampleRate)) + 1;
    m_line.assign(kChannels * m_length, 0.0f);
    m_write = 0;
    m_fadeLength = std::max<std::uint32_t>(
        1, static_cast<std::uint32_t>(std::lround(static_cast<double>(kFadeMs) * 1.0e-3 * spec.sampleRate)));
    m_feedback.prepare(spec.sampleRate, kGlideMs);
    m_mix.prepare(spec.sampleRate, kGlideMs);
    m_toneOctaves.prepare(spec.sampleRate, kToneGlideMs);
    for (Svf &filter : m_tone) {
        filter.reset();
    }
    settle();
    m_prepared = true;
    return {};
}

void Echo::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    m_feedback.setTarget(m_store.number(Feedback) * 0.01f);
    m_mix.setTarget(m_store.number(Mix) * 0.01f);
    m_toneOctaves.setTarget(std::log2(m_store.number(Tone)));
    // **One fade at a time**: a new time asked for in the middle of a fade
    // waits for it to finish, so the line is never read at three places.
    if (m_fadeLeft == 0) {
        const std::uint32_t target = targetSamples();
        if (target != m_from) {
            m_to = target;
            m_fadeLeft = m_fadeLength;
        }
    }

    std::array<std::span<float>, kChannels> in;
    std::array<std::span<float>, kChannels> out;
    for (std::size_t channel = 0; channel < kChannels; ++channel) {
        in[channel] = context.input.channel(static_cast<std::uint32_t>(channel));
        out[channel] = context.output.channel(static_cast<std::uint32_t>(channel));
    }

    for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
        if (m_toneOctaves.isSmoothing()) {
            design(m_toneOctaves.next());
        }
        const float feedback = m_feedback.next();
        const float mix = m_mix.next();
        // How far across the fade this sample is: 0 is the old time alone,
        // 1 the new one. Equal gains, not equal powers: both taps read the
        // same line, so what they hold is alike, and gains adding to one keep
        // it at its level.
        float across = 1.0f;
        if (m_fadeLeft > 0) {
            --m_fadeLeft;
            across = 1.0f - static_cast<float>(m_fadeLeft) / static_cast<float>(m_fadeLength);
        }
        const std::size_t fromRead = (m_write + m_length - m_from) % m_length;
        const std::size_t toRead = (m_write + m_length - m_to) % m_length;

        for (std::size_t channel = 0; channel < kChannels; ++channel) {
            float *const line = m_line.data() + channel * m_length;
            const float dry = frame < in[channel].size() ? in[channel][frame] : 0.0f;
            const float repeat = line[fromRead] + (line[toRead] - line[fromRead]) * across;
            // The repeat is played as it came back, and goes round again
            // duller and quieter.
            line[m_write] = dry + feedback * m_tone[channel].process(repeat, m_toneFilter);
            if (frame < out[channel].size()) {
                out[channel][frame] = dry + (repeat - dry) * mix;
            }
        }
        m_write = m_write + 1 == m_length ? 0 : m_write + 1;
    }
    if (m_fadeLeft == 0) {
        m_from = m_to;
    }
}

void Echo::reset() noexcept RB_NONBLOCKING
{
    std::ranges::fill(m_line, 0.0f);
    for (Svf &filter : m_tone) {
        filter.reset();
    }
    settle();
}

void Echo::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    // Read at the top of each cycle: glided to, or faded across.
    m_store.applyNumber(index, value);
}

std::expected<void, BlockError> Echo::setParam(std::string_view id, const ParamValue &value)
{
    return m_store.set(id, value);
}

ParamValue Echo::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap Echo::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> Echo::loadState(const ParamValueMap &state)
{
    const std::expected<void, BlockError> loaded = m_store.load(state);
    if (loaded && m_prepared) {
        // A session opened with its settings is heard with them from the
        // start; gliding and fading are for a hand on a control.
        settle();
    }
    return loaded;
}

} // namespace rb::blocks
