// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "graph/compiledgraph.h"

#include "dsp/audioblock.h"
#include "dsp/blockinfo.h"
#include "dsp/delayline.h"
#include "graph/topology.h"
#include "rt/clock.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <memory>
#include <numbers>
#include <tuple>
#include <utility>

namespace rb::graph
{

namespace
{

/// What one step of the plan does.
enum class StepKind : std::uint8_t {
    Block, ///< Call the block's process().
    Sum, ///< Add the inputs into the one output: the hidden unity-gain sum.
    /// A disabled block in the middle of a lane: its input to its output,
    /// extra outputs silent, held back by as long as the block itself would
    /// have delayed it. **A disabled block at either end of a lane has no
    /// dry signal to pass through and comes out silent instead**; see
    /// Step::dryIsSilence.
    Passthrough,
    /// A block that has just been switched on or off: both what the block
    /// makes of the signal and the signal itself, mixed across a fade.
    Crossfade,
};

} // namespace

/// Everything a compiled graph holds. It is one object behind one pointer so
/// that moving a graph, and retiring one, is a pointer each.
struct CompiledGraph::Plan {
    struct Step {
        StepKind kind = StepKind::Block;
        dsp::Block *block = nullptr;
        std::uint32_t inputOffset = 0;
        std::uint32_t inputCount = 0;
        std::uint32_t outputOffset = 0;
        std::uint32_t outputCount = 0;

        // --- switching a block off, and back on ---
        /// Where this step's private copy of the dry signal lives, and of
        /// what the block made of it while they are being mixed. Both are
        /// buffers of their own: the dry one is delayed in place, and the
        /// wet one would otherwise be the step's output.
        std::uint32_t dryOffset = 0;
        std::uint32_t wetOffset = 0;
        /// Which delay line holds the dry signal back, or kNoDelay, and by
        /// how much.
        std::uint32_t delay = kNoDelay;
        std::uint32_t delaySamples = 0;
        /// Samples to wait before the fade starts, so that whichever side is
        /// about to be faded in has had time to fill. A block's first
        /// latencySamples of output are silence, and so is a delay line that
        /// has only just been made.
        std::uint32_t holdRemaining = 0;
        /// Samples of fade left, and the rotation that walks the equal-power
        /// coefficients round without asking libm on the audio thread.
        std::uint32_t fadeRemaining = 0;
        float wetGain = 1.0f;
        float dryGain = 0.0f;
        float rotationCos = 1.0f;
        float rotationSin = 0.0f;
        /// What the step becomes once the fade is over.
        StepKind settlesTo = StepKind::Block;
        /// Whether this step's dry signal is silence rather than what
        /// arrived at it.
        ///
        /// **What "off" means depends on what the block has on its edges**.
        /// A block with a connector on both is in the middle of a
        /// lane, and switching it off is the block not being there: the
        /// signal carries on exactly as it arrived. A block missing one has
        /// nothing to pass through — a source receives nothing from anybody,
        /// so a source that is off cannot be the source of a sound, and an
        /// output block has no downstream wire for a pass-through to feed,
        /// only the hardware. Off means silence for both.
        ///
        /// It is the *block* that says which it is
        /// (`BlockInfo::inputsFromAudioSystem`, `::outputsToAudioSystem`),
        /// not the category it is listed under.
        bool dryIsSilence = false;
        /// **The lanes a dry signal is folded from**. A block that
        /// takes more than one signal — a voice and the music it ducks — is
        /// switched off to what it would be without it: every signal heard
        /// as it arrived, summed, and not only the first. Each lane's ports
        /// are added into the first lane's, port for port. Nought for a
        /// block with one lane, which is nearly all of them.
        std::uint32_t dryLaneWidth = 0;

        /// **The inputs that are held back to line up with the others**:
        /// where they are in Plan::compensations, and how many.
        std::uint32_t compensationOffset = 0;
        std::uint32_t compensationCount = 0;
        /// The node's key (InternalNode::key), for the bypass line a later
        /// graph takes over.
        std::string key;
        /// The node's index in the spec, or -1 for a hidden sum: where a
        /// timed cycle puts the block's time.
        std::size_t specNode = static_cast<std::size_t>(-1);

        /// Where this step's outputs are measured, or null.
        rt::BlockMeter *meter = nullptr;
    };

    static constexpr std::uint32_t kNoDelay = static_cast<std::uint32_t>(-1);

    /// Puts a step's input into its own buffers and holds it back by as long
    /// as the block would have delayed it, so the two sides of a fade line
    /// up and a bypassed block keeps the graph's timing.
    void fillDry(const Step &step, std::uint32_t frames) noexcept RB_NONBLOCKING
    {
        for (std::uint32_t port = 0; port < step.inputCount; ++port) {
            float *const out = dryPointers[step.dryOffset + port];
            // A step at the end of a lane has nothing to fall back on, so
            // what it is switched off *to* is silence and not what happened
            // to arrive at its ports.
            const float *const in = step.dryIsSilence ? nullptr : inputPointers[step.inputOffset + port];
            for (std::uint32_t frame = 0; frame < frames; ++frame) {
                out[frame] = in == nullptr ? 0.0f : in[frame];
            }
        }
        // **Nothing to hold back.** Delaying silence gives silence, and a
        // line that is never read is a line that need not exist.
        if (step.delay != kNoDelay) {
            delays[step.delay].process(dsp::AudioBlock{dryPointers.data() + step.dryOffset, step.inputCount, frames});
        }
        // Every further lane into the first, after the delay, so each lane
        // was held back on its own and the sum lines up.
        if (step.dryLaneWidth > 0) {
            for (std::uint32_t port = step.dryLaneWidth; port < step.inputCount; ++port) {
                float *const into = dryPointers[step.dryOffset + port % step.dryLaneWidth];
                const float *const from = dryPointers[step.dryOffset + port];
                for (std::uint32_t frame = 0; frame < frames; ++frame) {
                    into[frame] += from[frame];
                }
            }
        }
    }

    /// Moves a step's fade on by a cycle, and settles it when it is over.
    static void advanceFade(Step &step, std::uint32_t frames) noexcept RB_NONBLOCKING
    {
        const std::uint32_t held = frames < step.holdRemaining ? frames : step.holdRemaining;
        step.holdRemaining -= held;
        std::uint32_t turning = frames - held;
        if (turning > step.fadeRemaining) {
            turning = step.fadeRemaining;
        }
        for (std::uint32_t turn = 0; turn < turning; ++turn) {
            const float wet = step.wetGain * step.rotationCos - step.dryGain * step.rotationSin;
            step.dryGain = step.wetGain * step.rotationSin + step.dryGain * step.rotationCos;
            step.wetGain = wet;
        }
        step.fadeRemaining -= turning;
        if (step.holdRemaining == 0 && step.fadeRemaining == 0) {
            // Land on the side it was going to rather than wherever the
            // rotation got to, and stop doing the work.
            step.wetGain = step.settlesTo == StepKind::Block ? 1.0f : 0.0f;
            step.dryGain = 1.0f - step.wetGain;
            step.kind = step.settlesTo;
        }
    }

    /// In the order they run.
    std::vector<Step> steps;
    /// Every step's input and output channel pointers, one after another, so
    /// that an AudioBlock is a pointer into these and a count.
    std::vector<float *> inputPointers;
    std::vector<float *> outputPointers;
    /// True where an input pointer may be pointed somewhere by the runtime,
    /// which is to say where no wire feeds it.
    std::vector<bool> inputIsExternal;
    /// bufferCount buffers of maxBlockFrames frames, one after another.
    std::vector<float> pool;
    /// The dry and wet channel pointers of the steps that need them.
    std::vector<float *> dryPointers;
    std::vector<float *> wetPointers;
    /// One line per step that holds a bypassed signal back. Prepared here,
    /// so a cycle never allocates. Each has the key a later graph finds it
    /// by (carryOverFrom()): the id of the node it belongs to.
    std::vector<dsp::DelayLine> delays;
    std::vector<std::string> delayKeys;

    /// **An input held back so that it arrives with the others**.
    /// What arrives at a merge point — a hidden sum, or a block with more
    /// than one input — along a path with less latency than the longest one
    /// is copied into a buffer of its own and delayed by the difference, and
    /// the step reads that buffer instead.
    struct Compensation {
        /// Which input pointer this is, and the buffer it reads from before
        /// being pointed at its own.
        std::uint32_t input = 0;
        const float *from = nullptr;
        float *into = nullptr;
        dsp::DelayLine line;
        /// The wire it holds back, as "<from node>:<port>><to node>:<port>",
        /// so a recompile that keeps the wire keeps what the line holds.
        std::string key;
    };
    std::vector<Compensation> compensations;
    /// The longest a compensation line holds anything back, and the total
    /// of them: what a test and the diagnostics look at.
    std::uint32_t longestCompensation = 0;
    std::size_t bufferCount = 0;
    std::uint32_t maxBlockFrames = dsp::kMaxBlockFrames;

    // Per node of the original spec.
    std::vector<std::string> ids;
    std::vector<PortCounts> portCounts;
    std::vector<std::uint32_t> latency;
    std::vector<std::uint32_t> inputOffsetOfNode;
    std::vector<std::uint32_t> outputOffsetOfNode;
    std::vector<std::size_t> scheduleOfNodes;

    std::uint32_t graphLatency = 0;
    std::size_t sumNodeCount = 0;
};

namespace
{

/// A node of the graph the compiler actually schedules: the spec's nodes plus
/// the hidden sums it had to add.
struct InternalNode {
    StepKind kind = StepKind::Block;
    bool dryIsSilence = false;
    dsp::Block *block = nullptr;
    std::uint32_t inputCount = 0;
    std::uint32_t outputCount = 0;
    std::uint32_t ownLatency = 0;
    /// Plan::Step::dryLaneWidth.
    std::uint32_t dryLaneWidth = 0;
    /// How long the fade between processed and dry is, in samples, and which
    /// way it goes.
    std::uint32_t crossfade = 0;
    bool fadeToDry = false;
    /// Which node of the spec this is, or npos for a hidden sum.
    std::size_t specNode = static_cast<std::size_t>(-1);
    /// What a later graph knows this node by: its id, or for a hidden sum
    /// the port it feeds.
    std::string key;
    /// Where this node's levels go, or null. A hidden sum has none: it is
    /// not something the session has a tile for.
    rt::BlockMeter *meter = nullptr;
};

/// One end of an internal edge.
struct InternalEdge {
    std::size_t fromNode = 0;
    std::uint32_t fromPort = 0;
    std::size_t toNode = 0;
    std::uint32_t toPort = 0;
};

/// The number of ports a block ends up with: as many as are used, but never
/// fewer than it insists on nor more than it has.
[[nodiscard]] std::uint32_t portCountFor(const dsp::PortSpec &spec, std::uint32_t highestUsedPlusOne)
{
    return std::clamp(highestUsedPlusOne, spec.minCount, spec.maxCount);
}

} // namespace

std::string_view graphErrorName(GraphError error) noexcept
{
    switch (error) {
    case GraphError::NoBlock:
        return "noBlock";
    case GraphError::NoSource:
        return "noSource";
    case GraphError::Cycle:
        return "cycle";
    case GraphError::NodeOutOfRange:
        return "nodeOutOfRange";
    case GraphError::InputPortOutOfRange:
        return "inputPortOutOfRange";
    case GraphError::OutputPortOutOfRange:
        return "outputPortOutOfRange";
    case GraphError::DuplicateEdge:
        return "duplicateEdge";
    case GraphError::InputFilledByAudioSystem:
        return "inputFilledByAudioSystem";
    case GraphError::OutputTakenByAudioSystem:
        return "outputTakenByAudioSystem";
    case GraphError::BlockSizeTooLarge:
        return "blockSizeTooLarge";
    }
    return "unknown";
}

std::expected<CompiledGraph, GraphError> CompiledGraph::compile(const GraphSpec &spec)
{
    const std::size_t specNodeCount = spec.nodes.size();
    if (spec.maxBlockFrames == 0 || spec.maxBlockFrames > dsp::kMaxBlockFrames) {
        return std::unexpected(GraphError::BlockSizeTooLarge);
    }
    for (const NodeSpec &node : spec.nodes) {
        if (node.block == nullptr) {
            return std::unexpected(GraphError::NoBlock);
        }
    }

    // --- validate the wires, and see how many ports each node needs ---
    std::vector<std::uint32_t> highestInput(specNodeCount, 0);
    std::vector<std::uint32_t> highestOutput(specNodeCount, 0);
    for (const EdgeSpec &edge : spec.edges) {
        if (edge.fromNode >= specNodeCount || edge.toNode >= specNodeCount) {
            return std::unexpected(GraphError::NodeOutOfRange);
        }
        const dsp::BlockInfo &from = spec.nodes[edge.fromNode].block->info();
        const dsp::BlockInfo &to = spec.nodes[edge.toNode].block->info();
        if (edge.fromPort >= from.outputs.maxCount) {
            return std::unexpected(GraphError::OutputPortOutOfRange);
        }
        if (edge.toPort >= to.inputs.maxCount) {
            return std::unexpected(GraphError::InputPortOutOfRange);
        }
        // **Ports the audio system fills are not wiring**. The
        // session's edit rules refuse such a wire when it is drawn, and the
        // canvas will not even offer the port; this is for a session that
        // did not come through either — a file written by hand, one from
        // another version, or a second client over the facade — where the
        // block would otherwise be fed from two places with nothing saying
        // so. It is refused before anything runs, like every other thing
        // wrong with a graph.
        if (to.inputsFromAudioSystem) {
            return std::unexpected(GraphError::InputFilledByAudioSystem);
        }
        if (from.outputsToAudioSystem) {
            return std::unexpected(GraphError::OutputTakenByAudioSystem);
        }
        highestOutput[edge.fromNode] = std::max(highestOutput[edge.fromNode], edge.fromPort + 1);
        highestInput[edge.toNode] = std::max(highestInput[edge.toNode], edge.toPort + 1);
    }

    {
        std::vector<std::tuple<std::size_t, std::uint32_t, std::size_t, std::uint32_t>> seen;
        seen.reserve(spec.edges.size());
        for (const EdgeSpec &edge : spec.edges) {
            seen.emplace_back(edge.fromNode, edge.fromPort, edge.toNode, edge.toPort);
        }
        std::ranges::sort(seen);
        if (std::ranges::adjacent_find(seen) != seen.end()) {
            return std::unexpected(GraphError::DuplicateEdge);
        }
    }

    // **Something has to make the sound**: a block the audio system fills,
    // or one that takes nothing in and makes its own (a generator).
    // Said by the block's ports and flags, not by the library category it is
    // listed under — a generator listed under Outputs is a source all the
    // same.
    const bool hasSource = std::ranges::any_of(spec.nodes, [](const NodeSpec &node) {
        const dsp::BlockInfo &info = node.block->info();
        return info.inputsFromAudioSystem || (info.inputs.maxCount == 0 && info.outputs.maxCount > 0);
    });
    if (!hasSource) {
        return std::unexpected(GraphError::NoSource);
    }

    // --- the nodes the compiler schedules ---
    std::vector<InternalNode> nodes;
    nodes.reserve(specNodeCount);
    for (std::size_t index = 0; index < specNodeCount; ++index) {
        const NodeSpec &node = spec.nodes[index];
        const dsp::BlockInfo &info = node.block->info();
        InternalNode internal;
        internal.crossfade = node.crossfadeSamples;
        internal.fadeToDry = !node.enabled;
        // **The block says which end of a lane it is at**, not the category
        // it is listed under.
        internal.dryIsSilence = info.inputsFromAudioSystem || info.outputsToAudioSystem;
        internal.kind = internal.crossfade > 0 ? StepKind::Crossfade
            : node.enabled                     ? StepKind::Block
                                               : StepKind::Passthrough;
        // A block being faded across goes on running for the length of the
        // fade, whichever way it is going, because the fade needs both what
        // it makes of the signal and the signal itself.
        internal.block = node.enabled || internal.crossfade > 0 ? node.block : nullptr;
        internal.inputCount = portCountFor(info.inputs, highestInput[index]);
        internal.outputCount = portCountFor(info.outputs, highestOutput[index]);
        // **A bypassed block still holds the signal back by as long as it
        // would have.** Otherwise switching a block off would shift it
        // against every path running beside it, and a merge point that was
        // lined up would no longer be.
        internal.ownLatency = info.latencySamples;
        if (info.inputs.laneWidth > 0 && internal.inputCount > info.inputs.laneWidth) {
            internal.dryLaneWidth = info.inputs.laneWidth;
        }
        internal.specNode = index;
        internal.key = node.id;
        internal.meter = node.meter;
        nodes.push_back(std::move(internal));
    }

    // Several wires into one input port need a hidden unity-gain sum, because
    // a port carries one signal. One wire goes straight in.
    std::map<std::pair<std::size_t, std::uint32_t>, std::vector<const EdgeSpec *>> byDestination;
    for (const EdgeSpec &edge : spec.edges) {
        byDestination[{edge.toNode, edge.toPort}].push_back(&edge);
    }

    std::vector<InternalEdge> edges;
    edges.reserve(spec.edges.size());
    std::size_t sumNodeCount = 0;
    for (const auto &[destination, arriving] : byDestination) {
        const auto &[toNode, toPort] = destination;
        if (arriving.size() == 1) {
            edges.push_back(InternalEdge{arriving.front()->fromNode, arriving.front()->fromPort, toNode, toPort});
            continue;
        }
        InternalNode sum;
        sum.kind = StepKind::Sum;
        sum.inputCount = static_cast<std::uint32_t>(arriving.size());
        sum.outputCount = 1;
        sum.key = "sum>" + spec.nodes[toNode].id + ":" + std::to_string(toPort);
        const std::size_t sumIndex = nodes.size();
        nodes.push_back(std::move(sum));
        ++sumNodeCount;

        for (std::uint32_t lane = 0; lane < arriving.size(); ++lane) {
            edges.push_back(InternalEdge{arriving[lane]->fromNode, arriving[lane]->fromPort, sumIndex, lane});
        }
        edges.push_back(InternalEdge{sumIndex, 0, toNode, toPort});
    }

    // --- the order they run in ---
    std::vector<Edge> topologyEdges;
    topologyEdges.reserve(edges.size());
    for (const InternalEdge &edge : edges) {
        topologyEdges.push_back(Edge{edge.fromNode, edge.toNode});
    }
    const std::expected<std::vector<std::size_t>, TopologyError> order = topologicalOrder(nodes.size(), topologyEdges);
    if (!order) {
        return std::unexpected(order.error() == TopologyError::Cycle ? GraphError::Cycle : GraphError::NodeOutOfRange);
    }

    // Which wire, if any, feeds each input port, and how many read each
    // output port. The second is what says when a buffer is free again.
    std::map<std::pair<std::size_t, std::uint32_t>, std::pair<std::size_t, std::uint32_t>> sourceOfInput;
    std::map<std::pair<std::size_t, std::uint32_t>, std::size_t> readersOfOutput;
    for (const InternalEdge &edge : edges) {
        sourceOfInput[{edge.toNode, edge.toPort}] = {edge.fromNode, edge.fromPort};
        ++readersOfOutput[{edge.fromNode, edge.fromPort}];
    }

    // --- buffers ---
    // Each output port gets a buffer for as long as anything still has to
    // read it. A buffer nobody reads is a graph output and is kept, so the
    // runtime can copy it out after the cycle.
    std::map<std::pair<std::size_t, std::uint32_t>, std::size_t> bufferOfOutput;
    std::map<std::pair<std::size_t, std::uint32_t>, std::size_t> readersLeft = readersOfOutput;
    std::vector<std::size_t> freeBuffers;
    std::size_t bufferCount = 0;

    auto plan = std::make_unique<Plan>();
    plan->steps.reserve(nodes.size());
    plan->ids.resize(specNodeCount);
    plan->portCounts.resize(specNodeCount);
    plan->latency.assign(specNodeCount, 0);
    plan->inputOffsetOfNode.assign(specNodeCount, 0);
    plan->outputOffsetOfNode.assign(specNodeCount, 0);
    plan->maxBlockFrames = spec.maxBlockFrames;
    plan->sumNodeCount = sumNodeCount;

    std::vector<std::uint32_t> latencyOfNode(nodes.size(), 0);
    // Which buffer each input pointer reads, worked out first and turned into
    // pointers once the pool's size is known.
    std::vector<std::size_t> inputBuffer;
    std::vector<std::size_t> outputBuffer;
    constexpr std::size_t kSilent = static_cast<std::size_t>(-1);

    // The inputs to hold back, found while scheduling and given their
    // buffers once the pool's size is known.
    struct PendingCompensation {
        std::uint32_t input = 0;
        std::size_t fromBuffer = 0;
        std::uint32_t samples = 0;
        std::string key;
    };
    std::vector<PendingCompensation> pending;

    for (const std::size_t index : *order) {
        const InternalNode &node = nodes[index];

        Plan::Step step;
        step.kind = node.kind;
        step.block = node.block;
        step.meter = node.meter;
        step.delaySamples = node.ownLatency;
        step.dryIsSilence = node.dryIsSilence;
        step.dryLaneWidth = node.dryLaneWidth;
        step.key = node.key;
        step.specNode = node.specNode;
        if (node.kind == StepKind::Passthrough || node.kind == StepKind::Crossfade) {
            step.settlesTo = node.fadeToDry ? StepKind::Passthrough : StepKind::Block;
        }
        if (node.kind == StepKind::Crossfade) {
            step.fadeRemaining = node.crossfade;
            // Whichever side is about to be faded in needs as long as the
            // block's own latency to have anything to say: a block's first
            // samples are silence, and so are a fresh delay line's.
            step.holdRemaining = node.ownLatency;
            const double angle = std::numbers::pi / 2.0 / static_cast<double>(node.crossfade);
            step.rotationCos = static_cast<float>(std::cos(angle));
            // The two sides are a unit vector rotated from one axis to the
            // other, so their powers add to one all the way across and
            // nothing has to ask libm during a cycle.
            step.rotationSin = static_cast<float>(node.fadeToDry ? std::sin(angle) : -std::sin(angle));
            step.wetGain = node.fadeToDry ? 1.0f : 0.0f;
            step.dryGain = node.fadeToDry ? 0.0f : 1.0f;
        }
        step.inputOffset = static_cast<std::uint32_t>(inputBuffer.size());
        step.inputCount = node.inputCount;
        step.outputOffset = static_cast<std::uint32_t>(outputBuffer.size());
        step.outputCount = node.outputCount;

        std::uint32_t arriving = 0;
        for (std::uint32_t port = 0; port < node.inputCount; ++port) {
            const auto found = sourceOfInput.find({index, port});
            if (found == sourceOfInput.end()) {
                inputBuffer.push_back(kSilent); // an unconnected port is silence
                continue;
            }
            inputBuffer.push_back(bufferOfOutput.at(found->second));
            arriving = std::max(arriving, latencyOfNode[found->second.first]);
        }
        latencyOfNode[index] = arriving + node.ownLatency;

        // **Every input arrives as late as the latest one**. Two paths
        // meeting here — at a hidden sum, or at a block with more than one
        // input, a Mixer's strips or a Ducking block's voice and music — have
        // picked up different latencies on the way, and adding them as they
        // are would add two moments of one sound together. The earlier ones
        // are held back by the difference. **Latency only**: a delay a block
        // adds on purpose, a Sync block's, is not in latencyOfNode and is
        // never lined up with, or compensating would undo it.
        step.compensationOffset = static_cast<std::uint32_t>(pending.size());
        for (std::uint32_t port = 0; port < node.inputCount; ++port) {
            const auto found = sourceOfInput.find({index, port});
            if (found == sourceOfInput.end()) {
                continue;
            }
            const std::uint32_t behind = arriving - latencyOfNode[found->second.first];
            if (behind == 0) {
                continue;
            }
            pending.push_back(PendingCompensation{
                step.inputOffset + port,
                bufferOfOutput.at(found->second),
                behind,
                nodes[found->second.first].key + ":" + std::to_string(found->second.second) + ">" + node.key + ":"
                    + std::to_string(port),
            });
        }
        step.compensationCount = static_cast<std::uint32_t>(pending.size()) - step.compensationOffset;

        // Taken before the inputs are let go, so that a node never writes
        // into a buffer it is still reading.
        for (std::uint32_t port = 0; port < node.outputCount; ++port) {
            std::size_t buffer = 0;
            if (freeBuffers.empty()) {
                buffer = bufferCount++;
            } else {
                buffer = freeBuffers.back();
                freeBuffers.pop_back();
            }
            bufferOfOutput[{index, port}] = buffer;
            outputBuffer.push_back(buffer);
        }

        for (std::uint32_t port = 0; port < node.inputCount; ++port) {
            const auto found = sourceOfInput.find({index, port});
            if (found == sourceOfInput.end()) {
                continue;
            }
            const auto left = readersLeft.find(found->second);
            if (left != readersLeft.end() && --left->second == 0) {
                freeBuffers.push_back(bufferOfOutput.at(found->second));
            }
        }

        if (node.specNode != static_cast<std::size_t>(-1)) {
            plan->scheduleOfNodes.push_back(node.specNode);
            plan->ids[node.specNode] = spec.nodes[node.specNode].id;
            plan->portCounts[node.specNode] = PortCounts{node.inputCount, node.outputCount};
            plan->latency[node.specNode] = latencyOfNode[index];
            plan->inputOffsetOfNode[node.specNode] = step.inputOffset;
            plan->outputOffsetOfNode[node.specNode] = step.outputOffset;
        }
        plan->graphLatency = std::max(plan->graphLatency, latencyOfNode[index]);
        plan->steps.push_back(step);
    }

    // **Private buffers for the steps that switch a block off.** They are
    // not shared: a dry signal is delayed in place, and a wet one is what
    // the block would have written straight out.
    std::vector<std::size_t> dryBuffer;
    std::vector<std::size_t> wetBuffer;
    for (Plan::Step &step : plan->steps) {
        if (step.kind != StepKind::Passthrough && step.kind != StepKind::Crossfade) {
            continue;
        }
        step.dryOffset = static_cast<std::uint32_t>(dryBuffer.size());
        for (std::uint32_t port = 0; port < step.inputCount; ++port) {
            dryBuffer.push_back(bufferCount++);
        }
        if (step.kind == StepKind::Crossfade) {
            step.wetOffset = static_cast<std::uint32_t>(wetBuffer.size());
            for (std::uint32_t port = 0; port < step.outputCount; ++port) {
                wetBuffer.push_back(bufferCount++);
            }
        }
    }

    // **A buffer of its own for each input held back.** The one it reads
    // from may be read by another step as it is, so the delay cannot be done
    // in place there.
    std::vector<std::size_t> compensationBuffer;
    compensationBuffer.reserve(pending.size());
    for (std::size_t index = 0; index < pending.size(); ++index) {
        compensationBuffer.push_back(bufferCount++);
    }

    plan->bufferCount = bufferCount;
    plan->pool.assign(bufferCount * spec.maxBlockFrames, 0.0f);
    plan->dryPointers.resize(dryBuffer.size(), nullptr);
    for (std::size_t index = 0; index < dryBuffer.size(); ++index) {
        plan->dryPointers[index] = plan->pool.data() + dryBuffer[index] * spec.maxBlockFrames;
    }
    plan->wetPointers.resize(wetBuffer.size(), nullptr);
    for (std::size_t index = 0; index < wetBuffer.size(); ++index) {
        plan->wetPointers[index] = plan->pool.data() + wetBuffer[index] * spec.maxBlockFrames;
    }

    // A line each for the steps whose dry signal has to be held back. This
    // is the only place they are made; a cycle only pushes samples through.
    for (Plan::Step &step : plan->steps) {
        const bool bypassing = step.kind == StepKind::Passthrough || step.kind == StepKind::Crossfade;
        if (!bypassing || step.delaySamples == 0 || step.inputCount == 0 || step.dryIsSilence) {
            continue;
        }
        step.delay = static_cast<std::uint32_t>(plan->delays.size());
        plan->delays.emplace_back();
        plan->delays.back().prepare(step.inputCount, step.delaySamples);
        plan->delays.back().setDelaySamples(step.delaySamples);
        plan->delayKeys.push_back(step.key);
    }
    plan->inputPointers.resize(inputBuffer.size(), nullptr);
    plan->inputIsExternal.assign(inputBuffer.size(), false);
    for (std::size_t index = 0; index < inputBuffer.size(); ++index) {
        if (inputBuffer[index] == kSilent) {
            plan->inputIsExternal[index] = true;
            continue;
        }
        plan->inputPointers[index] = plan->pool.data() + inputBuffer[index] * spec.maxBlockFrames;
    }
    plan->outputPointers.resize(outputBuffer.size(), nullptr);
    for (std::size_t index = 0; index < outputBuffer.size(); ++index) {
        plan->outputPointers[index] = plan->pool.data() + outputBuffer[index] * spec.maxBlockFrames;
    }

    plan->compensations.resize(pending.size());
    for (std::size_t index = 0; index < pending.size(); ++index) {
        Plan::Compensation &held = plan->compensations[index];
        held.input = pending[index].input;
        held.from = plan->pool.data() + pending[index].fromBuffer * spec.maxBlockFrames;
        held.into = plan->pool.data() + compensationBuffer[index] * spec.maxBlockFrames;
        held.line.prepare(1, pending[index].samples);
        held.line.setDelaySamples(pending[index].samples);
        held.key = std::move(pending[index].key);
        // The step reads the held-back copy, not the wire's own buffer.
        plan->inputPointers[held.input] = held.into;
        plan->longestCompensation = std::max(plan->longestCompensation, pending[index].samples);
    }

    return CompiledGraph(std::move(plan));
}

CompiledGraph::CompiledGraph(std::unique_ptr<Plan> plan) noexcept
    : m_plan(std::move(plan))
{
}

CompiledGraph::CompiledGraph(CompiledGraph &&) noexcept = default;
CompiledGraph &CompiledGraph::operator=(CompiledGraph &&) noexcept = default;
CompiledGraph::~CompiledGraph() = default;

void CompiledGraph::execute(std::uint32_t frames, std::uint32_t sourcePresent) noexcept RB_NONBLOCKING
{
    execute(frames, sourcePresent, {});
}

void CompiledGraph::execute(std::uint32_t frames,
                            std::uint32_t sourcePresent,
                            std::span<std::uint64_t> nanos) noexcept RB_NONBLOCKING
{
    const bool timed = !nanos.empty();
    if (frames > m_plan->maxBlockFrames) {
        frames = m_plan->maxBlockFrames; // never write past what was made
    }
    float *const *inputs = m_plan->inputPointers.data();
    float *const *outputs = m_plan->outputPointers.data();
    float *const *dry = m_plan->dryPointers.data();
    float *const *wet = m_plan->wetPointers.data();

    for (Plan::Step &step : m_plan->steps) {
        const std::uint64_t started = timed ? rt::monotonicNanos() : 0;
        for (std::uint32_t index = 0; index < step.compensationCount; ++index) {
            Plan::Compensation &held = m_plan->compensations[step.compensationOffset + index];
            for (std::uint32_t frame = 0; frame < frames; ++frame) {
                held.into[frame] = held.from[frame];
            }
            float *channel[1] = {held.into};
            held.line.process(dsp::AudioBlock{channel, 1, frames});
        }
        // **What goes into an analysed block, copied before it runs**:
        // the first two inputs, as its outputs are copied after, and after
        // the paths meeting it have been lined up, so that the two spectra
        // are of the same moment. A block may write over its input as it
        // goes, so this cannot wait until it has run. One flag read
        // otherwise.
        if (step.meter != nullptr && step.meter->isAnalysing() && step.inputCount > 0) {
            const float *const first = inputs[step.inputOffset];
            const float *const second = step.inputCount > 1 ? inputs[step.inputOffset + 1] : first;
            const std::array<const float *, rt::BlockMeter::kAnalysisChannels> pair{first, second};
            step.meter->analyseInput(pair, frames);
        }
        switch (step.kind) {
        case StepKind::Block: {
            dsp::ProcessContext context{
                dsp::AudioBlock{inputs + step.inputOffset, step.inputCount, frames},
                dsp::AudioBlock{outputs + step.outputOffset, step.outputCount, frames},
                frames,
                sourcePresent,
            };
            step.block->process(context);
            break;
        }
        case StepKind::Sum: {
            float *const out = outputs[step.outputOffset];
            for (std::uint32_t frame = 0; frame < frames; ++frame) {
                out[frame] = 0.0f;
            }
            for (std::uint32_t lane = 0; lane < step.inputCount; ++lane) {
                const float *const in = inputs[step.inputOffset + lane];
                if (in == nullptr) {
                    continue;
                }
                for (std::uint32_t frame = 0; frame < frames; ++frame) {
                    out[frame] += in[frame];
                }
            }
            break;
        }
        case StepKind::Passthrough: {
            // The signal itself, held back by as long as the block would
            // have delayed it, so switching a block off does not shift it
            // against the paths running beside it.
            m_plan->fillDry(step, frames);
            for (std::uint32_t port = 0; port < step.outputCount; ++port) {
                float *const out = outputs[step.outputOffset + port];
                const float *const from = port < step.inputCount ? dry[step.dryOffset + port] : nullptr;
                for (std::uint32_t frame = 0; frame < frames; ++frame) {
                    out[frame] = from == nullptr ? 0.0f : from[frame];
                }
            }
            break;
        }
        case StepKind::Crossfade: {
            m_plan->fillDry(step, frames);
            // The block goes on running for the length of the fade, whether
            // it is being switched off or on: the fade needs both sides.
            dsp::ProcessContext context{
                dsp::AudioBlock{inputs + step.inputOffset, step.inputCount, frames},
                dsp::AudioBlock{wet + step.wetOffset, step.outputCount, frames},
                frames,
                sourcePresent,
            };
            step.block->process(context);

            for (std::uint32_t port = 0; port < step.outputCount; ++port) {
                float *const out = outputs[step.outputOffset + port];
                const float *const wetIn = wet[step.wetOffset + port];
                const float *const dryIn = port < step.inputCount ? dry[step.dryOffset + port] : nullptr;
                float wetGain = step.wetGain;
                float dryGain = step.dryGain;
                std::uint32_t hold = step.holdRemaining;
                std::uint32_t left = step.fadeRemaining;
                for (std::uint32_t frame = 0; frame < frames; ++frame) {
                    out[frame] = wetIn[frame] * wetGain + (dryIn == nullptr ? 0.0f : dryIn[frame] * dryGain);
                    if (hold > 0) {
                        --hold;
                        continue;
                    }
                    if (left > 0) {
                        const float turned = wetGain * step.rotationCos - dryGain * step.rotationSin;
                        dryGain = wetGain * step.rotationSin + dryGain * step.rotationCos;
                        wetGain = turned;
                        --left;
                    }
                }
            }
            // The gains and the counters are the step's, not the port's, so
            // they move on once, after every port has used them.
            Plan::advanceFade(step, frames);
            break;
        }
        }
        if (timed && step.specNode < nanos.size()) {
            nanos[step.specNode] += rt::monotonicNanos() - started;
        }

        // **Measured here, while the output is still this block's own.** A
        // buffer is given back to the pool as soon as nothing has to read it
        // any more, so by the end of a cycle the buffer a block wrote may
        // hold what a later block wrote. A meter read after the cycle would
        // show one block's level against another block's tile.
        if (step.meter != nullptr) {
            for (std::uint32_t port = 0; port < step.outputCount; ++port) {
                if (const float *const out = outputs[step.outputOffset + port]; out != nullptr) {
                    step.meter->channel(port).measure(std::span<const float>(out, frames));
                }
            }
            // **And copied out for a spectrum, while somebody is looking**:
            // the first two outputs, which is what a stereo lane
            // is. One flag read otherwise.
            if (step.meter->isAnalysing() && step.outputCount > 0) {
                const float *const first = outputs[step.outputOffset];
                const float *const second = step.outputCount > 1 ? outputs[step.outputOffset + 1] : first;
                const std::array<const float *, rt::BlockMeter::kAnalysisChannels> pair{first, second};
                step.meter->analyse(pair, frames);
            }
        }
    }
}

std::size_t CompiledGraph::nodeCount() const noexcept RB_NONBLOCKING
{
    return m_plan->ids.size();
}

std::span<const std::size_t> CompiledGraph::scheduleOfNodes() const noexcept
{
    return m_plan->scheduleOfNodes;
}

CompiledGraph::PortCounts CompiledGraph::portCounts(std::size_t node) const noexcept
{
    return node < m_plan->portCounts.size() ? m_plan->portCounts[node] : PortCounts{};
}

std::string_view CompiledGraph::nodeId(std::size_t node) const noexcept RB_NONBLOCKING
{
    return node < m_plan->ids.size() ? std::string_view{m_plan->ids[node]} : std::string_view{};
}

std::uint32_t CompiledGraph::latencyAt(std::size_t node) const noexcept
{
    return node < m_plan->latency.size() ? m_plan->latency[node] : 0;
}

std::uint32_t CompiledGraph::latencySamples() const noexcept RB_NONBLOCKING
{
    return m_plan->graphLatency;
}

std::size_t CompiledGraph::compensationCount() const noexcept
{
    return m_plan->compensations.size();
}

std::uint32_t CompiledGraph::longestCompensation() const noexcept
{
    return m_plan->longestCompensation;
}

std::size_t CompiledGraph::carryOverFrom(const CompiledGraph &previous) noexcept RB_NONBLOCKING
{
    // Found by key, which is a few string comparisons a line: a graph has a
    // handful of these, and this runs once an edit, between two cycles.
    std::size_t carried = 0;
    for (Plan::Compensation &held : m_plan->compensations) {
        for (const Plan::Compensation &was : previous.m_plan->compensations) {
            if (was.key == held.key) {
                carried += held.line.takeStateFrom(was.line) ? 1 : 0;
                break;
            }
        }
    }
    for (std::size_t index = 0; index < m_plan->delays.size(); ++index) {
        for (std::size_t other = 0; other < previous.m_plan->delays.size(); ++other) {
            if (previous.m_plan->delayKeys[other] == m_plan->delayKeys[index]) {
                carried += m_plan->delays[index].takeStateFrom(previous.m_plan->delays[other]) ? 1 : 0;
                break;
            }
        }
    }
    return carried;
}

std::size_t CompiledGraph::sumNodeCount() const noexcept
{
    return m_plan->sumNodeCount;
}

std::size_t CompiledGraph::bufferCount() const noexcept
{
    return m_plan->bufferCount;
}

std::span<const float>
CompiledGraph::outputBuffer(std::size_t node, std::uint32_t port, std::uint32_t frames) const noexcept RB_NONBLOCKING
{
    if (node >= m_plan->portCounts.size() || port >= m_plan->portCounts[node].outputs) {
        return {};
    }
    const std::uint32_t index = m_plan->outputOffsetOfNode[node] + port;
    return {m_plan->outputPointers[index], std::min(frames, m_plan->maxBlockFrames)};
}

bool CompiledGraph::bindExternalInput(std::size_t node, std::uint32_t port, float *buffer) noexcept RB_NONBLOCKING
{
    if (node >= m_plan->portCounts.size() || port >= m_plan->portCounts[node].inputs) {
        return false;
    }
    const std::uint32_t index = m_plan->inputOffsetOfNode[node] + port;
    if (!m_plan->inputIsExternal[index]) {
        return false; // a wire already feeds it
    }
    m_plan->inputPointers[index] = buffer;
    return true;
}

} // namespace rb::graph
