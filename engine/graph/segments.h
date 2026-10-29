// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "dsp/channelmap.h"
#include "graph/compiledgraph.h"
#include "graph/graphspec.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace rb::graph
{

/// What partition() says of a node that runs in a process of its own.
inline constexpr std::uint32_t kHosted = std::numeric_limits<std::uint32_t>::max();

/// **Where each block of a session runs, once it is cut around the blocks
/// that run somewhere else** (ADR-0014).
///
/// A plugin runs in a host process, as a PipeWire node of its own, so the
/// blocks either side of it cannot be in one compiled graph: the graph would
/// have to stop half-way through a cycle and wait for another process. So the
/// session is cut into **segments**, each a graph of its own in a node of its
/// own, and PipeWire runs segment, host and segment in one cycle in the order
/// their links give.
///
/// **A block's segment is the most hosted blocks on any path to it.** A
/// source is in segment 0; a block after one plugin is in segment 1, and
/// stays there however many plugin-free paths also reach it, because every
/// wire into it then comes from a segment at or before its own. That is what
/// makes the order of the segments the order PipeWire runs them in, and what
/// keeps a session with no plugin in it exactly one segment.
struct Partition {
    /// Per node of the spec: the segment it runs in, or kHosted.
    std::vector<std::uint32_t> segmentOf;
    /// How many segments there are, never fewer than one.
    std::uint32_t segmentCount = 1;

    [[nodiscard]] bool hosted(std::size_t node) const noexcept
    {
        return segmentOf[node] == kHosted;
    }
};

/// Cuts \a nodeCount nodes joined by \a edges around the ones \a hosted says
/// run elsewhere. The only error is a cycle, which the compiler would refuse
/// anyway.
[[nodiscard]] std::expected<Partition, GraphError>
partition(std::size_t nodeCount, std::span<const EdgeSpec> edges, const std::vector<bool> &hosted);

/// **Where a segment's audio leaves it, or arrives in it**, as a block the
/// segment's graph can hold.
///
/// Everything that crosses from one segment to another — to a plugin host,
/// from one, or straight to a later segment — goes through a node's ports.
/// The session node already gives a port to every block that says its audio
/// comes from, or goes to, the audio system; a boundary is a block that says
/// so, and passes what it is given straight through. So a segment is a graph
/// like any other, compiled like any other, and a session node needs to know
/// nothing about plugins.
///
/// **An arriving boundary carries the delay the sound has picked up
/// before it**, as its latencySamples, though it adds none itself. That is
/// how latency compensation, which works inside one graph, lines up
/// two paths that met after one of them went through a plugin: the graph of
/// the later segment sees one input arriving later than the other by
/// exactly what the plugin reported, and holds the other back by that much.
class SegmentBoundary final : public dsp::Block
{
public:
    enum class Direction : std::uint8_t {
        Arriving, ///< Its inputs are ports of the node, filled from elsewhere.
        Leaving, ///< Its outputs are ports of the node, read by elsewhere.
    };

    static constexpr std::string_view kArrivingTypeId = "segment.arriving";
    static constexpr std::string_view kLeavingTypeId = "segment.leaving";

    SegmentBoundary(Direction direction, std::uint32_t channels);

    [[nodiscard]] Direction direction() const noexcept
    {
        return m_direction;
    }

    [[nodiscard]] std::uint32_t channels() const noexcept
    {
        return m_info.inputs.maxCount;
    }

    /// The delay the sound has picked up before it arrives here. Read when
    /// a graph is compiled, never by a cycle.
    void setCarriedLatency(std::uint32_t samples) noexcept
    {
        m_info.latencySamples = samples;
    }

    [[nodiscard]] const dsp::BlockInfo &info() const noexcept override
    {
        return m_info;
    }

    [[nodiscard]] std::span<const dsp::ParamDescriptor> params() const noexcept override
    {
        return {};
    }

    [[nodiscard]] std::expected<void, dsp::BlockError> prepare(const dsp::PrepareSpec &spec) override;
    void process(dsp::ProcessContext &context) noexcept RB_NONBLOCKING override;

    void reset() noexcept RB_NONBLOCKING override { }

    void applyParam(std::uint32_t, float) noexcept RB_NONBLOCKING override { }

    [[nodiscard]] std::expected<void, dsp::BlockError> setParam(std::string_view, const dsp::ParamValue &) override
    {
        return std::unexpected(dsp::BlockError::UnknownParam);
    }

    [[nodiscard]] dsp::ParamValue param(std::string_view) const override
    {
        return {};
    }

    [[nodiscard]] dsp::ParamValueMap saveState() const override
    {
        return {};
    }

    [[nodiscard]] std::expected<void, dsp::BlockError> loadState(const dsp::ParamValueMap &) override
    {
        return {};
    }

private:
    Direction m_direction;
    dsp::BlockInfo m_info;
};

/// **A session cut into segments, with everything between them spelled
/// out** (ADR-0014): the graph each segment compiles, the boundaries its
/// audio crosses, and the PipeWire links that join boundaries and hosts.
///
/// **Every wire that crosses goes through a boundary**, never straight from
/// one host to another: all of a hosted node's inputs are gathered in one
/// segment — the latest any of them comes from — and leave it together.
/// That is what lets latency compensation line up two paths that meet at a
/// plugin's input, one of them through another plugin: they meet in a graph
/// first, which can hold one back, rather than in PipeWire, which sums them
/// as they come.
struct SegmentPlan {
    Partition partition;

    /// A boundary block a segment needs, which the caller makes.
    struct Boundary {
        /// Its id in the segment's spec, which is what the session node
        /// names its ports after: `<block>.send` for a block's audio
        /// leaving for later segments, `<block>.recv<N>` for a block's audio
        /// arriving in segment N, and `<block>.feed` for a hosted block's
        /// inputs leaving for it.
        std::string id;
        SegmentBoundary::Direction direction = SegmentBoundary::Direction::Arriving;
        std::uint32_t segment = 0;
        std::uint32_t channels = 0;
        /// For an arriving boundary: the delay the sound has picked up by
        /// the time it leaves the block it comes from.
        std::uint32_t carriedLatency = 0;
        /// The node of the whole spec whose audio it carries: the block it
        /// sends or receives, or the hosted block it feeds.
        std::size_t node = 0;
    };
    std::vector<Boundary> boundaries;

    /// One end of a link: a channel of a boundary, or a port of a hosted
    /// node.
    struct PortRef {
        bool hosted = false;
        /// The boundary's index, or the hosted node's index in the whole
        /// spec.
        std::size_t index = 0;
        std::uint32_t port = 0;

        [[nodiscard]] bool operator==(const PortRef &other) const = default;
    };
    struct PortLink {
        PortRef from; ///< an output
        PortRef to; ///< an input

        [[nodiscard]] bool operator==(const PortLink &other) const = default;
    };
    std::vector<PortLink> links;

    /// Per segment, what it compiles. A boundary's node has its id and no
    /// block yet: the caller puts its block there.
    std::vector<GraphSpec> segments;
    /// Per segment, per node of its spec: the whole spec's index of that
    /// node, or kBoundaryNode for a boundary.
    std::vector<std::vector<std::size_t>> origin;

    static constexpr std::size_t kBoundaryNode = std::numeric_limits<std::size_t>::max();
};

/// Cuts \a whole around the nodes \a hosted says run elsewhere. With none,
/// the one segment is \a whole exactly. **Latency is read from the blocks
/// now**, so a plugin's reported latency is carried into the segments after
/// it; a latency that changes means splitting again.
[[nodiscard]] std::expected<SegmentPlan, GraphError> splitIntoSegments(const GraphSpec &whole,
                                                                       const std::vector<bool> &hosted);

/// The channel a boundary's \a port is on, which names its PipeWire port:
/// front left and right for the first two, as the session's bus is, and
/// auxiliary channels after them.
[[nodiscard]] dsp::ChannelPosition boundaryChannel(std::uint32_t port) noexcept;

} // namespace rb::graph
