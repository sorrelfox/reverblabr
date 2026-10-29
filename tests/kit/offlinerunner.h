// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/audiobuffer.h"
#include "dsp/block.h"
#include "graph/compiledgraph.h"
#include "signals.h"

#include <cstdint>
#include <expected>
#include <map>
#include <utility>
#include <vector>

namespace rb::test
{

/// How a run chops the signal into cycles.
///
/// PipeWire's quantum changes while a session runs, so "the same signal in
/// different-sized pieces" is the thing most worth varying: a block that
/// keeps something per cycle rather than per sample shows up at once.
struct CycleShape {
    /// The shortest and longest cycle. Equal means fixed cycles.
    std::uint32_t minFrames = 256;
    std::uint32_t maxFrames = 256;
    /// Fixed, so a failure can be reproduced.
    std::uint32_t seed = 1;

    [[nodiscard]] static CycleShape fixed(std::uint32_t frames)
    {
        return CycleShape{frames, frames, 1};
    }

    /// Cycles drawn at random between \a minFrames and \a maxFrames.
    [[nodiscard]] static CycleShape random(std::uint32_t minFrames, std::uint32_t maxFrames, std::uint32_t seed)
    {
        return CycleShape{minFrames, maxFrames, seed};
    }
};

/// Runs one block over a whole signal, with no audio server anywhere near it.
///
/// The buffers are made once, so the run itself allocates nothing and can be
/// watched with NoAllocations.
class BlockRunner
{
public:
    /// \a block is not owned and must outlive the runner.
    BlockRunner(dsp::Block &block,
                std::uint32_t inputChannels,
                std::uint32_t outputChannels,
                double sampleRate = kTestSampleRate,
                std::uint32_t maxBlockFrames = 2048);

    /// What the input channels carry, for a block that mixes them — a
    /// source block downmixing a 5.1 stream has to know which one is the
    /// centre. Set it before prepare(); empty leaves the block to make what
    /// it can of the channel count.
    void setInputLayout(dsp::ChannelLayout layout);
    /// The same for the output ports: an output block putting the stereo bus
    /// onto a device has to know which channel is which.
    void setOutputLayout(dsp::ChannelLayout layout);
    /// Where a second, separate group of input channels starts inside the
    /// input layout, for a block that takes audio from two places at once.
    /// Zero — the default — means there is one group.
    void setSecondInputFrom(std::uint32_t channel);
    /// Where the block may publish numbers of its own, for a block that has
    /// some — a meter's levels, a gain reduction. Null is what a block sees
    /// when nobody is watching.
    void setMeter(rt::BlockMeter *meter);

    /// Where a recording block leaves what it hears (PrepareSpec::recording).
    /// Null, the default, is what a block sees when nobody is recording.
    void setRecording(rt::AudioTap *tap);

    /// Prepares the block for the shape this runner will use.
    [[nodiscard]] std::expected<void, dsp::BlockError> prepare();

    /// Feeds \a input and gives back what each output channel produced.
    /// A channel the input does not have arrives at the block as silence.
    [[nodiscard]] MultiSignal run(const MultiSignal &input, const CycleShape &shape = CycleShape::fixed(256));

    /// The same, watching for allocations while process() runs. The count is
    /// what a test asserts on; with the guard in its aborting mode the test
    /// stops at the first one instead.
    [[nodiscard]] MultiSignal
    runWatchingForAllocations(const MultiSignal &input, const CycleShape &shape, std::size_t &allocations);

    [[nodiscard]] std::uint32_t maxBlockFrames() const noexcept
    {
        return m_maxBlockFrames;
    }

private:
    MultiSignal runInternal(const MultiSignal &input, const CycleShape &shape, std::size_t *allocations);

    dsp::Block &m_block;
    dsp::AudioBuffer m_input;
    dsp::AudioBuffer m_output;
    std::uint32_t m_inputChannels;
    std::uint32_t m_outputChannels;
    double m_sampleRate;
    std::uint32_t m_maxBlockFrames;
    dsp::ChannelLayout m_inputLayout;
    dsp::ChannelLayout m_outputLayout;
    std::uint32_t m_secondInputFrom = 0;
    rt::BlockMeter *m_meter = nullptr;
    rt::AudioTap *m_recording = nullptr;
};

/// Runs a whole compiled graph, feeding the ports no wire reaches and keeping
/// what the ports being watched produced.
class GraphRunner
{
public:
    /// \a graph is not owned and must outlive the runner.
    explicit GraphRunner(graph::CompiledGraph &graph);

    /// Feeds \a signal into a node's input port. The port must be one no wire
    /// feeds; this is what a PipeWire buffer does in the real engine.
    void feed(std::size_t node, std::uint32_t port, Signal signal);

    /// Keeps what a node's output port produces.
    void capture(std::size_t node, std::uint32_t port);

    /// Runs \a frames frames. Every fed signal shorter than that runs out into
    /// silence.
    void run(std::size_t frames, const CycleShape &shape = CycleShape::fixed(256));

    /// The same, watching for allocations while the graph executes.
    void runWatchingForAllocations(std::size_t frames, const CycleShape &shape, std::size_t &allocations);

    /// What a captured port produced. Empty if it was never captured.
    [[nodiscard]] const Signal &captured(std::size_t node, std::uint32_t port) const;

private:
    void runInternal(std::size_t frames, const CycleShape &shape, std::size_t *allocations);

    graph::CompiledGraph &m_graph;
    std::map<std::pair<std::size_t, std::uint32_t>, Signal> m_fed;
    /// Where each fed signal is copied to, one cycle at a time, so that the
    /// pointer the graph is given never moves.
    std::map<std::pair<std::size_t, std::uint32_t>, Signal> m_feedBuffers;
    std::map<std::pair<std::size_t, std::uint32_t>, Signal> m_captured;
    Signal m_nothing;
};

} // namespace rb::test
