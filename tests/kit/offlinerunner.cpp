// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "offlinerunner.h"

#include "allocationtrap.h"
#include "dsp/audioblock.h"

#include <algorithm>
#include <random>
#include <span>
#include <utility>

namespace rb::test
{
namespace
{

/// The cycle lengths a run will use, worked out up front so that the run
/// itself does not draw random numbers while the trap is armed.
std::vector<std::uint32_t> cycleLengths(std::size_t frames, const CycleShape &shape, std::uint32_t limit)
{
    const std::uint32_t low = std::clamp(shape.minFrames, dsp::kMinBlockFrames, limit);
    const std::uint32_t high = std::clamp(std::max(shape.maxFrames, low), low, limit);

    std::vector<std::uint32_t> lengths;
    std::mt19937 random(shape.seed);
    std::uniform_int_distribution<std::uint32_t> between(low, high);
    for (std::size_t done = 0; done < frames;) {
        const std::uint32_t wanted = low == high ? low : between(random);
        const auto length = static_cast<std::uint32_t>(std::min<std::size_t>(wanted, frames - done));
        lengths.push_back(length);
        done += length;
    }
    return lengths;
}

} // namespace

BlockRunner::BlockRunner(dsp::Block &block,
                         std::uint32_t inputChannels,
                         std::uint32_t outputChannels,
                         double sampleRate,
                         std::uint32_t maxBlockFrames)
    : m_block(block)
    , m_input(inputChannels, maxBlockFrames)
    , m_output(outputChannels, maxBlockFrames)
    , m_inputChannels(inputChannels)
    , m_outputChannels(outputChannels)
    , m_sampleRate(sampleRate)
    , m_maxBlockFrames(maxBlockFrames)
{
}

void BlockRunner::setInputLayout(dsp::ChannelLayout layout)
{
    m_inputLayout = std::move(layout);
}

void BlockRunner::setOutputLayout(dsp::ChannelLayout layout)
{
    m_outputLayout = std::move(layout);
}

void BlockRunner::setSecondInputFrom(std::uint32_t channel)
{
    m_secondInputFrom = channel;
}

void BlockRunner::setMeter(rt::BlockMeter *meter)
{
    m_meter = meter;
}

void BlockRunner::setRecording(rt::AudioTap *tap)
{
    m_recording = tap;
}

std::expected<void, dsp::BlockError> BlockRunner::prepare()
{
    dsp::PrepareSpec spec;
    spec.sampleRate = m_sampleRate;
    spec.maxBlockFrames = m_maxBlockFrames;
    spec.inputChannels = m_inputChannels;
    spec.outputChannels = m_outputChannels;
    spec.inputLayout = m_inputLayout;
    spec.outputLayout = m_outputLayout;
    spec.secondInputFrom = m_secondInputFrom;
    spec.meter = m_meter;
    spec.recording = m_recording;
    return m_block.prepare(spec);
}

MultiSignal BlockRunner::run(const MultiSignal &input, const CycleShape &shape)
{
    return runInternal(input, shape, nullptr);
}

MultiSignal
BlockRunner::runWatchingForAllocations(const MultiSignal &input, const CycleShape &shape, std::size_t &allocations)
{
    return runInternal(input, shape, &allocations);
}

MultiSignal BlockRunner::runInternal(const MultiSignal &input, const CycleShape &shape, std::size_t *allocations)
{
    const std::size_t frames = frameCount(input);
    const std::vector<std::uint32_t> lengths = cycleLengths(frames, shape, m_maxBlockFrames);

    MultiSignal produced(m_outputChannels);
    for (Signal &channel : produced) {
        channel.reserve(frames);
    }
    std::size_t counted = 0;

    std::size_t offset = 0;
    for (const std::uint32_t length : lengths) {
        for (std::uint32_t channel = 0; channel < m_inputChannels; ++channel) {
            const std::span<float> destination = m_input.channel(channel);
            const bool hasChannel = channel < input.size();
            for (std::uint32_t frame = 0; frame < length; ++frame) {
                destination[frame] = hasChannel ? input[channel][offset + frame] : 0.0f;
            }
        }
        m_output.clear();

        dsp::ProcessContext context{m_input.block(length), m_output.block(length), length};
        if (allocations != nullptr) {
            const NoAllocations guard(NoAllocations::OnAllocation::Count);
            m_block.process(context);
            counted += guard.count();
        } else {
            m_block.process(context);
        }

        for (std::uint32_t channel = 0; channel < m_outputChannels; ++channel) {
            const std::span<const float> heard = m_output.channel(channel);
            produced[channel].insert(produced[channel].end(), heard.begin(), heard.begin() + length);
        }
        offset += length;
    }

    if (allocations != nullptr) {
        *allocations = counted;
    }
    return produced;
}

GraphRunner::GraphRunner(graph::CompiledGraph &graph)
    : m_graph(graph)
{
}

void GraphRunner::feed(std::size_t node, std::uint32_t port, Signal signal)
{
    m_fed[{node, port}] = std::move(signal);
    m_feedBuffers[{node, port}] = Signal(dsp::kMaxBlockFrames, 0.0f);
}

void GraphRunner::capture(std::size_t node, std::uint32_t port)
{
    m_captured.try_emplace({node, port}, Signal{});
}

void GraphRunner::run(std::size_t frames, const CycleShape &shape)
{
    runInternal(frames, shape, nullptr);
}

void GraphRunner::runWatchingForAllocations(std::size_t frames, const CycleShape &shape, std::size_t &allocations)
{
    runInternal(frames, shape, &allocations);
}

void GraphRunner::runInternal(std::size_t frames, const CycleShape &shape, std::size_t *allocations)
{
    const std::vector<std::uint32_t> lengths = cycleLengths(frames, shape, dsp::kMaxBlockFrames);
    for (auto &[where, signal] : m_captured) {
        signal.clear();
        signal.reserve(frames);
    }
    std::size_t counted = 0;

    std::size_t offset = 0;
    for (const std::uint32_t length : lengths) {
        for (auto &[where, buffer] : m_feedBuffers) {
            const Signal &source = m_fed.at(where);
            for (std::uint32_t frame = 0; frame < length; ++frame) {
                const std::size_t index = offset + frame;
                buffer[frame] = index < source.size() ? source[index] : 0.0f;
            }
            static_cast<void>(m_graph.bindExternalInput(where.first, where.second, buffer.data()));
        }

        if (allocations != nullptr) {
            const NoAllocations guard(NoAllocations::OnAllocation::Count);
            m_graph.execute(length);
            counted += guard.count();
        } else {
            m_graph.execute(length);
        }

        for (auto &[where, signal] : m_captured) {
            const std::span<const float> heard = m_graph.outputBuffer(where.first, where.second, length);
            signal.insert(signal.end(), heard.begin(), heard.end());
        }
        offset += length;
    }

    if (allocations != nullptr) {
        *allocations = counted;
    }
}

const Signal &GraphRunner::captured(std::size_t node, std::uint32_t port) const
{
    const auto found = m_captured.find({node, port});
    return found == m_captured.end() ? m_nothing : found->second;
}

} // namespace rb::test
