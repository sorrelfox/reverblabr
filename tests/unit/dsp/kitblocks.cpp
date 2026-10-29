// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "kitblocks.h"

#include "dsp/audioblock.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>
#include <vector>

namespace rb::test
{

using namespace rb::dsp;

KitBlock::KitBlock(std::string typeId, BlockCategory category, std::uint32_t inputs, std::uint32_t outputs)
{
    m_info.typeId = std::move(typeId);
    m_info.category = category;
    m_info.inputs = PortSpec::fixed(inputs);
    m_info.outputs = PortSpec::fixed(outputs);
}

std::expected<void, BlockError> KitBlock::prepare(const PrepareSpec &spec)
{
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    return {};
}

void KitBlock::reset() noexcept RB_NONBLOCKING { }

void KitBlock::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    static_cast<void>(index);
    static_cast<void>(value);
}

std::expected<void, BlockError> KitBlock::setParam(std::string_view id, const ParamValue &value)
{
    static_cast<void>(id);
    static_cast<void>(value);
    return std::unexpected(BlockError::UnknownParam);
}

ParamValue KitBlock::param(std::string_view id) const
{
    static_cast<void>(id);
    return {};
}

ParamValueMap KitBlock::saveState() const
{
    return {};
}

std::expected<void, BlockError> KitBlock::loadState(const ParamValueMap &state)
{
    static_cast<void>(state);
    return {};
}

DelayByOneHundred::DelayByOneHundred()
    : KitBlock("test.delay100", BlockCategory::Effects, 1, 1)
{
    m_info.latencySamples = kDelaySamples;
}

std::expected<void, BlockError> DelayByOneHundred::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, BlockError> base = KitBlock::prepare(spec); !base) {
        return base;
    }
    m_line.assign(kDelaySamples + 1, 0.0f);
    m_writeIndex = 0;
    return {};
}

void DelayByOneHundred::reset() noexcept RB_NONBLOCKING
{
    for (float &sample : m_line) {
        sample = 0.0f;
    }
    m_writeIndex = 0;
}

void DelayByOneHundred::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    const std::span<float> in = context.input.channel(0);
    const std::span<float> out = context.output.channel(0);
    if (m_line.empty() || out.empty()) {
        return;
    }
    const std::size_t capacity = m_line.size();
    std::size_t read = (m_writeIndex + 1) % capacity;
    for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
        m_line[m_writeIndex] = in.empty() ? 0.0f : in[frame];
        out[frame] = m_line[read];
        m_writeIndex = (m_writeIndex + 1) % capacity;
        read = (read + 1) % capacity;
    }
}

SoftClipper::SoftClipper()
    : KitBlock("test.softClipper", BlockCategory::Effects, 1, 1)
{
}

void SoftClipper::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    const std::span<float> in = context.input.channel(0);
    const std::span<float> out = context.output.channel(0);
    for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
        const float sample = in.empty() ? 0.0f : in[frame];
        // A cubic bend: gentle in the middle, flattening towards the ends,
        // which puts odd harmonics on a sine.
        const float driven = std::clamp(sample * 1.5f, -1.0f, 1.0f);
        out[frame] = driven - (driven * driven * driven) / 3.0f;
    }
}

ChunkDependent::ChunkDependent()
    : KitBlock("test.chunkDependent", BlockCategory::Effects, 1, 1)
{
}

void ChunkDependent::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    const std::span<float> in = context.input.channel(0);
    const std::span<float> out = context.output.channel(0);
    // The mistake: one gain for the whole cycle, taken from its first sample.
    // Where the cycle boundaries fall then changes the result.
    const float gain = in.empty() ? 0.0f : 0.5f + 0.5f * std::abs(in[0]);
    for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
        out[frame] = (in.empty() ? 0.0f : in[frame]) * gain;
    }
}

void AllocatesWhileProcessing::process(ProcessContext &context)
{
    // The mistake the trap exists to catch: scratch space taken per cycle
    // instead of in prepare().
    std::vector<float> scratch(context.frames, 0.0f);
    const std::span<float> in = context.input.channel(0);
    const std::span<float> out = context.output.channel(0);
    for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
        scratch[frame] = in.empty() ? 0.0f : in[frame];
        out[frame] = scratch[frame];
    }
}

SineSource::SineSource(double frequencyHz, float amplitude)
    : KitBlock("test.sineSource", BlockCategory::Sources, 0, 1)
    , m_frequencyHz(frequencyHz)
    , m_amplitude(amplitude)
{
}

std::expected<void, BlockError> SineSource::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, BlockError> base = KitBlock::prepare(spec); !base) {
        return base;
    }
    m_sampleRate = spec.sampleRate;
    return {};
}

void SineSource::reset() noexcept RB_NONBLOCKING
{
    m_phase = 0.0;
}

void SineSource::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    const std::span<float> out = context.output.channel(0);
    const double step = 2.0 * std::numbers::pi * m_frequencyHz / m_sampleRate;
    for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
        if (!out.empty()) {
            out[frame] = m_amplitude * static_cast<float>(std::sin(m_phase));
        }
        m_phase += step;
        if (m_phase > 2.0 * std::numbers::pi) {
            m_phase -= 2.0 * std::numbers::pi;
        }
    }
}

FixedGain::FixedGain(float gain)
    : KitBlock("test.fixedGain", BlockCategory::Effects, 1, 1)
    , m_gain(gain)
{
}

void FixedGain::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    const std::span<float> in = context.input.channel(0);
    const std::span<float> out = context.output.channel(0);
    for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
        out[frame] = (in.empty() ? 0.0f : in[frame]) * m_gain;
    }
}

} // namespace rb::test
