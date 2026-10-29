// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "sessionblocks.h"

#include "dsp/audioblock.h"
#include "dsp/paramvalue.h"
#include "rt/clock.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace rb::test
{

using namespace rb::dsp;

PassThroughSource::PassThroughSource(std::uint32_t channels)
{
    m_info.typeId = std::string{kTypeId};
    m_info.category = BlockCategory::Sources;
    std::vector<std::string> inputNames;
    std::vector<std::string> outputNames;
    for (std::uint32_t channel = 0; channel < channels; ++channel) {
        inputNames.push_back("in_" + std::to_string(channel));
        outputNames.push_back("out_" + std::to_string(channel));
    }
    m_info.inputs = PortSpec::fixed(channels, inputNames);
    m_info.outputs = PortSpec::fixed(channels, outputNames);
    // **Its inputs are the audio system's**, like every real capture block:
    // what arrives at them is put there by PipeWire and not by a wire. The
    // compiler reads this to know that switching the block off leaves it
    // nothing to pass through.
    m_info.inputsFromAudioSystem = true;
    m_info.face = FaceKind::Value;
    m_info.latencySamples = 0;
}

std::unique_ptr<Block> PassThroughSource::make()
{
    return std::make_unique<PassThroughSource>();
}

std::expected<void, BlockError> PassThroughSource::prepare(const PrepareSpec &spec)
{
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    return {};
}

void PassThroughSource::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    ++m_cycles;
    for (std::uint32_t channel = 0; channel < context.output.channelCount(); ++channel) {
        const std::span<float> out = context.output.channel(channel);
        if (out.empty()) {
            continue;
        }
        const std::span<float> in = context.input.channel(channel);
        for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
            const float sample = in.empty() ? 0.0f : in[frame];
            m_peak = std::max(m_peak, std::abs(sample));
            out[frame] = sample;
        }
    }
}

void PassThroughSource::reset() noexcept RB_NONBLOCKING
{
    m_peak = 0.0f;
}

void PassThroughSource::applyParam(std::uint32_t, float) noexcept RB_NONBLOCKING { }

std::expected<void, BlockError> PassThroughSource::setParam(std::string_view, const ParamValue &)
{
    return std::unexpected(BlockError::UnknownParam);
}

ParamValue PassThroughSource::param(std::string_view) const
{
    return {};
}

ParamValueMap PassThroughSource::saveState() const
{
    return {};
}

std::expected<void, BlockError> PassThroughSource::loadState(const ParamValueMap &)
{
    return {};
}

FixedGainEffect::FixedGainEffect(float gain, std::uint32_t channels)
    : m_gain(gain)
{
    m_info.typeId = std::string{kTypeId};
    m_info.category = BlockCategory::Effects;
    std::vector<std::string> inputNames;
    std::vector<std::string> outputNames;
    for (std::uint32_t channel = 0; channel < channels; ++channel) {
        inputNames.push_back("in_" + std::to_string(channel));
        outputNames.push_back("out_" + std::to_string(channel));
    }
    m_info.inputs = PortSpec::fixed(channels, inputNames);
    m_info.outputs = PortSpec::fixed(channels, outputNames);
    m_info.face = FaceKind::Value;
    m_info.latencySamples = 0;
}

std::unique_ptr<Block> FixedGainEffect::make()
{
    return std::make_unique<FixedGainEffect>(0.5f, 2);
}

std::expected<void, BlockError> FixedGainEffect::prepare(const PrepareSpec &spec)
{
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    return {};
}

void FixedGainEffect::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    ++m_cycles;
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
}

void FixedGainEffect::reset() noexcept RB_NONBLOCKING { }

void FixedGainEffect::applyParam(std::uint32_t, float) noexcept RB_NONBLOCKING { }

std::expected<void, BlockError> FixedGainEffect::setParam(std::string_view, const ParamValue &)
{
    return std::unexpected(BlockError::UnknownParam);
}

ParamValue FixedGainEffect::param(std::string_view) const
{
    return {};
}

ParamValueMap FixedGainEffect::saveState() const
{
    return {};
}

std::expected<void, BlockError> FixedGainEffect::loadState(const ParamValueMap &)
{
    return {};
}

BandWriterEffect::BandWriterEffect()
{
    m_info.typeId = std::string{kTypeId};
    m_info.category = BlockCategory::Effects;
    m_info.inputs = PortSpec::fixed(2, {"in_0", "in_1"});
    m_info.outputs = PortSpec::fixed(2, {"out_0", "out_1"});
    m_info.face = FaceKind::Value;
    for (std::size_t band = 0; band < m_bands.size(); ++band) {
        m_bands[band] = bandValue(band);
    }
}

std::unique_ptr<Block> BandWriterEffect::make()
{
    return std::make_unique<BandWriterEffect>();
}

std::expected<void, BlockError> BandWriterEffect::prepare(const PrepareSpec &spec)
{
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    m_meter = spec.meter;
    return {};
}

void BandWriterEffect::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    for (std::uint32_t channel = 0; channel < context.output.channelCount(); ++channel) {
        const std::span<float> out = context.output.channel(channel);
        if (out.empty()) {
            continue;
        }
        const std::span<float> in = context.input.channel(channel);
        for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
            out[frame] = in.empty() ? 0.0f : in[frame] * 0.5f;
        }
    }
    if (m_meter != nullptr) {
        m_meter->publishBands(m_bands);
    }
}

LifetimeWatch &lifetimeWatch() noexcept
{
    static LifetimeWatch watch;
    return watch;
}

LifetimeWatchedEffect::LifetimeWatchedEffect(float gain, std::uint32_t channels)
    : m_gain(gain)
    , m_watch(&lifetimeWatch())
{
    m_info.typeId = std::string{kTypeId};
    m_info.category = BlockCategory::Effects;
    std::vector<std::string> inputNames;
    std::vector<std::string> outputNames;
    for (std::uint32_t channel = 0; channel < channels; ++channel) {
        inputNames.push_back("in_" + std::to_string(channel));
        outputNames.push_back("out_" + std::to_string(channel));
    }
    m_info.inputs = PortSpec::fixed(channels, inputNames);
    m_info.outputs = PortSpec::fixed(channels, outputNames);
}

LifetimeWatchedEffect::~LifetimeWatchedEffect()
{
    LifetimeWatch &watch = lifetimeWatch();
    watch.cyclesWhenDestroyed.store(watch.cycles.load());
    watch.destructions.fetch_add(1);
}

std::unique_ptr<Block> LifetimeWatchedEffect::make()
{
    return std::make_unique<LifetimeWatchedEffect>(0.5f, 2);
}

std::expected<void, BlockError> LifetimeWatchedEffect::prepare(const PrepareSpec &spec)
{
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    LifetimeWatch &watch = lifetimeWatch();
    watch.prepares.fetch_add(1);
    // **A block that has run is a block the audio thread may be inside.**
    // Preparing reallocates what a cycle reads, so this must never be true.
    if (m_memory > 0) {
        watch.preparedAfterRunning.store(true);
    }
    m_memory = 0;
    return {};
}

void LifetimeWatchedEffect::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    m_watch->cycles.fetch_add(1, std::memory_order_relaxed);
    ++m_memory;
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
}

void LifetimeWatchedEffect::reset() noexcept RB_NONBLOCKING { }

void LifetimeWatchedEffect::applyParam(std::uint32_t, float) noexcept RB_NONBLOCKING { }

std::expected<void, BlockError> LifetimeWatchedEffect::setParam(std::string_view, const ParamValue &)
{
    return std::unexpected(BlockError::UnknownParam);
}

ParamValue LifetimeWatchedEffect::param(std::string_view) const
{
    return {};
}

ParamValueMap LifetimeWatchedEffect::saveState() const
{
    return {};
}

std::expected<void, BlockError> LifetimeWatchedEffect::loadState(const ParamValueMap &)
{
    return {};
}

StallingEffect::StallingEffect(std::uint32_t channels)
{
    m_info.typeId = std::string{kTypeId};
    m_info.category = BlockCategory::Effects;
    std::vector<std::string> inputNames;
    std::vector<std::string> outputNames;
    for (std::uint32_t channel = 0; channel < channels; ++channel) {
        inputNames.push_back("in_" + std::to_string(channel));
        outputNames.push_back("out_" + std::to_string(channel));
    }
    m_info.inputs = PortSpec::fixed(channels, inputNames);
    m_info.outputs = PortSpec::fixed(channels, outputNames);
    m_info.face = FaceKind::Value;
    m_info.latencySamples = 0;
}

std::unique_ptr<Block> StallingEffect::make()
{
    return std::make_unique<StallingEffect>();
}

std::expected<void, BlockError> StallingEffect::prepare(const PrepareSpec &spec)
{
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    return {};
}

void StallingEffect::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    const std::uint64_t cycle = m_cycles.fetch_add(1, std::memory_order_relaxed) + 1;
    for (std::uint32_t channel = 0; channel < context.output.channelCount(); ++channel) {
        const std::span<float> out = context.output.channel(channel);
        if (out.empty()) {
            continue;
        }
        const std::span<float> in = context.input.channel(channel);
        for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
            out[frame] = in.empty() ? 0.0f : in[frame];
        }
    }

    const bool ownHold = m_everyNth.load(std::memory_order_relaxed) != 0;
    const std::uint32_t everyNth = (ownHold ? m_everyNth : s_everyNth).load(std::memory_order_relaxed);
    const std::uint32_t microseconds = (ownHold ? m_microseconds : s_microseconds).load(std::memory_order_relaxed);
    if (everyNth == 0 || microseconds == 0 || cycle % everyNth != 0) {
        return;
    }
    // The sound is already written, so what is held up is the callback and
    // not the signal: the cycle this node is late for is the next one.
    m_held.fetch_add(1, std::memory_order_relaxed);
    const std::uint64_t until = rt::monotonicNanos() + std::uint64_t{microseconds} * 1000ULL;
    while (rt::monotonicNanos() < until) {
        // Nothing: being here is the whole of what this block does.
    }
}

void StallingEffect::reset() noexcept RB_NONBLOCKING { }

void StallingEffect::applyParam(std::uint32_t, float) noexcept RB_NONBLOCKING { }

std::expected<void, BlockError> StallingEffect::setParam(std::string_view, const ParamValue &)
{
    return std::unexpected(BlockError::UnknownParam);
}

ParamValue StallingEffect::param(std::string_view) const
{
    return {};
}

ParamValueMap StallingEffect::saveState() const
{
    return {};
}

std::expected<void, BlockError> StallingEffect::loadState(const ParamValueMap &)
{
    return {};
}

PassThroughOutput::PassThroughOutput(std::uint32_t channels)
{
    m_info.typeId = std::string{kTypeId};
    m_info.category = BlockCategory::Outputs;
    std::vector<std::string> names;
    for (std::uint32_t channel = 0; channel < channels; ++channel) {
        names.push_back("in_" + std::to_string(channel));
    }
    m_info.inputs = PortSpec::fixed(channels, names);
    m_info.outputs = PortSpec::fixed(channels, names);
    // And its outputs are the audio system's: they go to the device.
    m_info.outputsToAudioSystem = true;
    m_info.face = FaceKind::Icon;
    m_info.latencySamples = 0;
}

std::unique_ptr<Block> PassThroughOutput::make()
{
    return std::make_unique<PassThroughOutput>();
}

std::expected<void, BlockError> PassThroughOutput::prepare(const PrepareSpec &spec)
{
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    return {};
}

void PassThroughOutput::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    for (std::uint32_t channel = 0; channel < context.output.channelCount(); ++channel) {
        const std::span<float> out = context.output.channel(channel);
        if (out.empty()) {
            continue;
        }
        const std::span<float> in = context.input.channel(channel);
        for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
            out[frame] = in.empty() ? 0.0f : in[frame];
        }
    }
}

void PassThroughOutput::reset() noexcept RB_NONBLOCKING { }

void PassThroughOutput::applyParam(std::uint32_t, float) noexcept RB_NONBLOCKING { }

std::expected<void, BlockError> PassThroughOutput::setParam(std::string_view, const ParamValue &)
{
    return std::unexpected(BlockError::UnknownParam);
}

ParamValue PassThroughOutput::param(std::string_view) const
{
    return {};
}

ParamValueMap PassThroughOutput::saveState() const
{
    return {};
}

std::expected<void, BlockError> PassThroughOutput::loadState(const ParamValueMap &)
{
    return {};
}

} // namespace rb::test
