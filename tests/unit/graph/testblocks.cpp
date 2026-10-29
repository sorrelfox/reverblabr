// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "testblocks.h"

#include "dsp/audioblock.h"

#include <utility>

namespace rb::test
{

using namespace rb::dsp;

int SimpleBlock::s_tick = 0;

SimpleBlock::SimpleBlock(std::string typeId, BlockCategory category, std::uint32_t inputs, std::uint32_t outputs)
{
    m_info.typeId = std::move(typeId);
    m_info.category = category;
    m_info.inputs = PortSpec::fixed(inputs);
    m_info.outputs = PortSpec::fixed(outputs);
    m_info.face = FaceKind::Icon;
}

std::expected<void, BlockError> SimpleBlock::prepare(const PrepareSpec &spec)
{
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    return {};
}

void SimpleBlock::reset() noexcept RB_NONBLOCKING { }

void SimpleBlock::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    static_cast<void>(index);
    static_cast<void>(value);
}

std::expected<void, BlockError> SimpleBlock::setParam(std::string_view id, const ParamValue &value)
{
    static_cast<void>(id);
    static_cast<void>(value);
    return std::unexpected(BlockError::UnknownParam);
}

ParamValue SimpleBlock::param(std::string_view id) const
{
    static_cast<void>(id);
    return {};
}

ParamValueMap SimpleBlock::saveState() const
{
    return {};
}

std::expected<void, BlockError> SimpleBlock::loadState(const ParamValueMap &state)
{
    static_cast<void>(state);
    return {};
}

void SimpleBlock::markRun() noexcept
{
    m_lastRunAt = s_tick++;
    ++m_cycles;
}

ConstantSource::ConstantSource(float value, std::uint32_t outputs)
    : SimpleBlock("test.constant", BlockCategory::Sources, 0, outputs)
    , m_value(value)
{
}

void ConstantSource::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    markRun();
    for (std::uint32_t channel = 0; channel < context.output.channelCount(); ++channel) {
        const std::span<float> out = context.output.channel(channel);
        for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
            out[frame] = m_value;
        }
    }
}

GainStage::GainStage(float gain, std::uint32_t channels)
    : SimpleBlock("test.gainStage", BlockCategory::Effects, channels, channels)
    , m_gain(gain)
{
}

void GainStage::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    markRun();
    for (std::uint32_t channel = 0; channel < context.output.channelCount(); ++channel) {
        const std::span<float> out = context.output.channel(channel);
        const std::span<float> in = context.input.channel(channel);
        for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
            out[frame] = in.empty() ? 0.0f : in[frame] * m_gain;
        }
    }
}

RunningTotal::RunningTotal()
    : SimpleBlock("test.runningTotal", BlockCategory::Effects, 1, 1)
{
}

void RunningTotal::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    markRun();
    const std::span<float> in = context.input.channel(0);
    const std::span<float> out = context.output.channel(0);
    for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
        const float sample = in.empty() ? 0.0f : in[frame];
        m_total += static_cast<double>(sample);
        if (!out.empty()) {
            out[frame] = sample;
        }
    }
}

void RunningTotal::reset() noexcept RB_NONBLOCKING
{
    m_total = 0.0;
}

LatentGain::LatentGain(float gain, std::uint32_t latencySamples, std::uint32_t channels)
    : SimpleBlock("test.latentGain", dsp::BlockCategory::Effects, channels, channels)
    , m_gain(gain)
    , m_latency(latencySamples)
{
    m_info.latencySamples = latencySamples;
}

std::expected<void, dsp::BlockError> LatentGain::prepare(const dsp::PrepareSpec &spec)
{
    if (const std::expected<void, dsp::BlockError> ready = SimpleBlock::prepare(spec); !ready) {
        return ready;
    }
    m_delay.prepare(m_info.outputs.maxCount, m_latency);
    m_delay.setDelaySamples(m_latency);
    return {};
}

void LatentGain::process(dsp::ProcessContext &context) noexcept RB_NONBLOCKING
{
    markRun();
    for (std::uint32_t channel = 0; channel < context.output.channelCount(); ++channel) {
        const std::span<float> out = context.output.channel(channel);
        if (out.empty()) {
            continue;
        }
        const std::span<float> in = context.input.channel(channel);
        for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
            out[frame] = in.empty() ? 0.0f : in[frame] * m_gain;
        }
    }
    m_delay.process(context.output);
}

void LatentGain::reset() noexcept RB_NONBLOCKING
{
    m_delay.reset();
}

AudioSystemEnd::AudioSystemEnd(bool capturing, std::uint32_t channels)
    : SimpleBlock(capturing ? "test.capture" : "test.device",
                  capturing ? BlockCategory::Sources : BlockCategory::Outputs,
                  channels,
                  channels)
{
    m_info.inputsFromAudioSystem = capturing;
    m_info.outputsToAudioSystem = !capturing;
}

void AudioSystemEnd::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    markRun();
    for (std::uint32_t channel = 0; channel < context.output.channelCount(); ++channel) {
        const std::span<float> out = context.output.channel(channel);
        const std::span<float> in = context.input.channel(channel);
        for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
            out[frame] = in.empty() ? 0.0f : in[frame];
        }
    }
}

Sink::Sink(std::uint32_t channels)
    : SimpleBlock("test.sink", BlockCategory::Outputs, channels, 0)
{
}

std::expected<void, BlockError> Sink::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, BlockError> base = SimpleBlock::prepare(spec); !base) {
        return base;
    }
    m_heard.assign(m_info.inputs.maxCount, std::vector<float>(spec.maxBlockFrames, 0.0f));
    return {};
}

void Sink::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    markRun();
    m_frames = context.frames;
    for (std::uint32_t channel = 0; channel < m_heard.size(); ++channel) {
        const std::span<float> in = context.input.channel(channel);
        std::vector<float> &kept = m_heard[channel];
        for (std::uint32_t frame = 0; frame < context.frames && frame < kept.size(); ++frame) {
            kept[frame] = in.empty() ? 0.0f : in[frame];
        }
    }
}

std::span<const float> Sink::heard(std::uint32_t channel) const
{
    if (channel >= m_heard.size()) {
        return {};
    }
    return std::span<const float>{m_heard[channel]}.first(m_frames);
}

} // namespace rb::test
