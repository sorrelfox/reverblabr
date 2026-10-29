// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/sync.h"

#include <algorithm>
#include <cmath>
#include <numbers>
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

std::vector<ParamDescriptor> Sync::makeDescriptors()
{
    std::vector<ParamDescriptor> descriptors;

    ParamDescriptor delay;
    delay.id = "delay";
    delay.type = ParamType::Float;
    delay.minimum = ParamValue::ofFloat(0.0);
    delay.maximum = ParamValue::ofFloat(Sync::kMaxDelayMs);
    // Nothing until somebody asks: a block dropped into a lane should not
    // shift it before its setting has been looked at.
    delay.defaultValue = ParamValue::ofFloat(0.0);
    delay.unit = ParamUnit::Milliseconds;
    // **No smoothing**: a delay glided to is a pitch bend. The block fades
    // between the old delay and the new one itself.
    delay.smoothingMs = 0.0f;
    descriptors.push_back(std::move(delay));
    return descriptors;
}

Sync::Sync()
    : m_descriptors(makeDescriptors())
    , m_store(m_descriptors)
{
    m_info.typeId = std::string{kTypeId};
    m_info.category = BlockCategory::Advanced;
    m_info.inputs = PortSpec::fixed(kChannels, {"in_FL", "in_FR"});
    m_info.outputs = PortSpec::fixed(kChannels, {"out_FL", "out_FR"});
    m_info.face = FaceKind::Value;
    // **Zero, on purpose**; see the class comment and intendedDelaySamples().
    m_info.latencySamples = 0;
}

std::unique_ptr<Block> Sync::make()
{
    return std::make_unique<Sync>();
}

std::uint32_t Sync::targetSamples() const noexcept RB_NONBLOCKING
{
    const double samples = static_cast<double>(m_store.number(Delay)) * 1.0e-3 * m_sampleRate;
    const auto rounded = static_cast<std::uint32_t>(std::max(0.0, std::round(samples)));
    return std::min(rounded, m_maxDelay);
}

std::expected<void, BlockError> Sync::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, ParamListError> valid = validateParams(m_descriptors); !valid) {
        return std::unexpected(BlockError::InvalidDescriptor);
    }
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    m_sampleRate = spec.sampleRate;
    m_maxDelay = static_cast<std::uint32_t>(std::ceil(static_cast<double>(kMaxDelayMs) * 1.0e-3 * spec.sampleRate));
    m_line.prepare(kChannels, m_maxDelay);

    m_fadeLength = std::max<std::uint32_t>(
        1, static_cast<std::uint32_t>(std::lround(static_cast<double>(kFadeMs) * 1.0e-3 * spec.sampleRate)));
    const double step = std::numbers::pi / 2.0 / static_cast<double>(m_fadeLength);
    m_stepCos = std::cos(step);
    m_stepSin = std::sin(step);
    m_meter = spec.meter;
    reset();
    return {};
}

void Sync::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    // **A new setting starts a fade only between fades**, so every fade runs
    // its whole length and the one after it goes to wherever the setting is
    // by then.
    if (m_fadeLeft == 0) {
        const std::uint32_t target = targetSamples();
        if (target != m_from) {
            m_to = target;
            m_fadeLeft = m_fadeLength;
            m_cos = 1.0;
            m_sin = 0.0;
        }
    }

    // Every channel walks the same stretch of the same fade, from where the
    // cycle found it; the block moves on once they all have.
    std::uint32_t fadeLeft = m_fadeLeft;
    double cosine = m_cos;
    double sine = m_sin;
    const auto turn = [&]() noexcept RB_NONBLOCKING {
        const double turnedCos = cosine * m_stepCos - sine * m_stepSin;
        sine = sine * m_stepCos + cosine * m_stepSin;
        cosine = turnedCos;
        --fadeLeft;
    };
    bool walked = false;
    for (std::uint32_t channel = 0; channel < kChannels; ++channel) {
        const std::span<float> in = context.input.channel(channel);
        const std::span<float> out = context.output.channel(channel);
        if (out.empty()) {
            m_line.pushSilence(channel, context.frames);
            continue;
        }
        const std::size_t frames = std::min<std::size_t>(out.size(), context.frames);
        for (std::size_t frame = 0; frame < frames; ++frame) {
            out[frame] = frame < in.size() ? in[frame] : 0.0f;
        }

        fadeLeft = m_fadeLeft;
        cosine = m_cos;
        sine = m_sin;
        walked = true;
        m_line.processChannelTwoTaps(
            channel, out.first(frames), m_from, m_to, [&](float from, float to) noexcept RB_NONBLOCKING {
                if (fadeLeft == 0) {
                    return to;
                }
                turn();
                // The last sample of a fade is the new delay alone, whatever
                // rounding the walk round the circle picked up on the way.
                const double gain = fadeLeft == 0 ? 1.0 : sine * sine;
                return static_cast<float>(static_cast<double>(from) * (1.0 - gain) + static_cast<double>(to) * gain);
            });
    }
    m_line.endFrames(context.frames);
    // A cycle with nowhere to write still took its time, and the fade with it.
    if (!walked) {
        for (std::uint32_t frame = 0; frame < context.frames && fadeLeft > 0; ++frame) {
            turn();
        }
    }

    m_fadeLeft = fadeLeft;
    m_cos = cosine;
    m_sin = sine;
    if (m_fadeLeft == 0) {
        m_from = m_to;
    }

    if (m_meter != nullptr) {
        m_meter->publish(kDelaySamplesValue, static_cast<float>(m_to));
    }
}

std::uint32_t Sync::intendedDelaySamples() const noexcept RB_NONBLOCKING
{
    return m_to;
}

void Sync::reset() noexcept RB_NONBLOCKING
{
    // **Cut, not faded**: whatever the line held is gone, so the setting is
    // taken as it is and there is nothing to fade from.
    m_line.reset();
    m_from = targetSamples();
    m_to = m_from;
    m_fadeLeft = 0;
    m_cos = 1.0;
    m_sin = 0.0;
}

void Sync::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    // Read at the top of every cycle, which is where a fade starts.
    m_store.applyNumber(index, value);
}

std::expected<void, BlockError> Sync::setParam(std::string_view id, const ParamValue &value)
{
    return m_store.set(id, value);
}

ParamValue Sync::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap Sync::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> Sync::loadState(const ParamValueMap &state)
{
    return m_store.load(state);
}

} // namespace rb::blocks
