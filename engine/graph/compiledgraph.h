// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "graph/graphspec.h"
#include "rt/realtime.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace rb::graph
{

/// Why a graph could not be compiled. All of these are reported before
/// anything runs; a compiled graph is one that has already been proved sound.
enum class GraphError : std::uint8_t {
    NoBlock, ///< A node has no block.
    NoSource, ///< Nothing in the graph produces audio, so it cannot be run.
    Cycle, ///< The wires feed a block's output back into its own input.
    NodeOutOfRange, ///< A wire names a node that is not in the graph.
    InputPortOutOfRange, ///< A wire arrives at a port the block does not have.
    OutputPortOutOfRange, ///< A wire leaves from a port the block does not have.
    DuplicateEdge, ///< The same wire twice.
    /// A wire arrives at a port the audio system fills, which would be two
    /// things writing one signal.
    InputFilledByAudioSystem,
    /// A wire leaves a port that belongs to the device.
    OutputTakenByAudioSystem,
    BlockSizeTooLarge, ///< More frames per cycle than the engine handles.
};

[[nodiscard]] std::string_view graphErrorName(GraphError error) noexcept;

/// An immutable plan for running one graph, with every buffer already there.
///
/// Compiling works out three things: the order the blocks run in, which
/// buffer each port reads and writes, and where a hidden sum is needed. None
/// of it changes afterwards, so a cycle is a walk down a list.
///
/// **Buffers are made for the largest cycle the engine allows**, so a change
/// of PipeWire's quantum costs nothing: execute() takes the frame count for
/// this cycle and uses the front of each buffer. Only a change of sample rate
/// makes a block need re-preparing, and that is the runtime's business.
///
/// **Blocks are not owned here.** A recompile builds a new plan around the
/// same blocks, which is what keeps envelopes and filter memory across an
/// edit. Nothing is freed on the audio thread; a retired graph goes to
/// rt::RetireQueue.
class CompiledGraph
{
public:
    /// How a graph is built. Preparing the blocks is **not** part of it: only
    /// the runtime knows what has actually changed, and re-preparing on every
    /// compile would reset the envelopes a recompile exists to preserve.
    /// portCounts() says what each block should be prepared for.
    [[nodiscard]] static std::expected<CompiledGraph, GraphError> compile(const GraphSpec &spec);

    CompiledGraph(CompiledGraph &&) noexcept;
    CompiledGraph &operator=(CompiledGraph &&) noexcept;
    ~CompiledGraph();

    CompiledGraph(const CompiledGraph &) = delete;
    CompiledGraph &operator=(const CompiledGraph &) = delete;

    /// How many ports of each direction a node ended up with, which is what
    /// its block has to be prepared for.
    struct PortCounts {
        std::uint32_t inputs = 0;
        std::uint32_t outputs = 0;
    };

    /// Runs one cycle of \a frames frames. Real-time: it allocates nothing,
    /// takes no lock and does bounded work.
    ///
    /// Denormals are the caller's business: the guard belongs at the top of
    /// the process callback, once per cycle, not once per graph.
    ///
    /// \a sourcePresent is handed to every block as it is
    /// (ProcessContext::sourcePresent): which of the node's input lanes had
    /// audio arriving this cycle.
    void execute(std::uint32_t frames, std::uint32_t sourcePresent = dsp::kEveryLanePresent) noexcept RB_NONBLOCKING;

    /// The same cycle, **timing each block**: the nanoseconds a
    /// node's block took are added to \a nanos at the node's index in the
    /// spec. A node past the end of \a nanos, and a hidden sum, are not
    /// timed. Two clock reads a block, which is why the ordinary cycle does
    /// not do it: the session node asks only while a cycle is running long.
    void
    execute(std::uint32_t frames, std::uint32_t sourcePresent, std::span<std::uint64_t> nanos) noexcept RB_NONBLOCKING;

    /// The nodes of the original spec, in the order they were given.
    [[nodiscard]] std::size_t nodeCount() const noexcept RB_NONBLOCKING;

    /// The order the nodes run in, as indices into the spec's nodes. Hidden
    /// sums are not in it; they have no place in the session.
    [[nodiscard]] std::span<const std::size_t> scheduleOfNodes() const noexcept;

    [[nodiscard]] PortCounts portCounts(std::size_t node) const noexcept;

    /// The id the spec gave node \a node.
    [[nodiscard]] std::string_view nodeId(std::size_t node) const noexcept RB_NONBLOCKING;

    /// How many samples of delay the signal has picked up by the time it
    /// leaves \a node, along the longest path to it. Latency compensation
    /// lines the paths up with these, so every path to a node has
    /// this much by then.
    [[nodiscard]] std::uint32_t latencyAt(std::size_t node) const noexcept;

    /// The longest of those, which is the graph's own latency.
    [[nodiscard]] std::uint32_t latencySamples() const noexcept RB_NONBLOCKING;

    /// How many inputs are held back to line up with a later one at a merge
    /// point, and the longest any of them is held.
    [[nodiscard]] std::size_t compensationCount() const noexcept;
    [[nodiscard]] std::uint32_t longestCompensation() const noexcept;

    /// Takes over what \a previous was holding in its delay lines — the
    /// inputs held back at merge points, and the bypassed blocks' — wherever
    /// this graph has the same line for the same wire or block, and answers
    /// how many it took.
    ///
    /// **Called on the audio thread, between two cycles**, where the graph
    /// that is going out is still the one being run and nothing else is
    /// touching either. A recompile makes new lines, and a new line is
    /// silence for as long as its delay; without this, an edit anywhere in
    /// the session would put a gap of that long into every path lined up
    /// with another. Real-time: copies, no allocation.
    std::size_t carryOverFrom(const CompiledGraph &previous) noexcept RB_NONBLOCKING;

    /// How many hidden sum nodes were needed. Nothing depends on it; it is
    /// what a test and the diagnostics look at.
    [[nodiscard]] std::size_t sumNodeCount() const noexcept;

    /// How many buffers the plan uses. A pool that grows with the graph
    /// rather than with the number of ports is the point of the liveness
    /// pass, so this is worth being able to see.
    [[nodiscard]] std::size_t bufferCount() const noexcept;

    /// What a node's output port holds after execute(). Empty if there is no
    /// such port. The session node copies from these into its PipeWire
    /// buffers.
    [[nodiscard]] std::span<const float>
    outputBuffer(std::size_t node, std::uint32_t port, std::uint32_t frames) const noexcept RB_NONBLOCKING;

    /// Points an **unconnected** input port at memory the caller owns, which
    /// is how a PipeWire buffer reaches a source block, or at nothing for
    /// silence. Returns false for a port that a wire already feeds, or one
    /// that does not exist.
    ///
    /// Real-time: the session node calls it once per port per cycle.
    [[nodiscard]] bool bindExternalInput(std::size_t node, std::uint32_t port, float *buffer) noexcept RB_NONBLOCKING;

private:
    struct Plan;

    explicit CompiledGraph(std::unique_ptr<Plan> plan) noexcept;

    std::unique_ptr<Plan> m_plan;
};

} // namespace rb::graph
