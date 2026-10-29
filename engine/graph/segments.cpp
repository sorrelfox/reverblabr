// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "graph/segments.h"

#include "graph/topology.h"

#include <algorithm>
#include <string>
#include <utility>

namespace rb::graph
{

namespace
{

/// How many hosted nodes the sound has been through by the time it reaches
/// each node, along the path with the most of them, and the order the nodes
/// can run in.
struct Reach {
    std::vector<std::size_t> order;
    std::vector<std::uint32_t> hosts;
};

[[nodiscard]] bool isHosted(const std::vector<bool> &hosted, std::size_t node)
{
    return node < hosted.size() && hosted[node];
}

[[nodiscard]] std::expected<Reach, GraphError>
reach(std::size_t nodeCount, std::span<const EdgeSpec> edges, const std::vector<bool> &hosted)
{
    std::vector<Edge> order;
    order.reserve(edges.size());
    for (const EdgeSpec &edge : edges) {
        order.push_back(Edge{edge.fromNode, edge.toNode});
    }
    std::expected<std::vector<std::size_t>, TopologyError> sorted = topologicalOrder(nodeCount, order);
    if (!sorted) {
        return std::unexpected(sorted.error() == TopologyError::Cycle ? GraphError::Cycle : GraphError::NodeOutOfRange);
    }
    Reach result;
    result.order = std::move(*sorted);
    result.hosts.assign(nodeCount, 0);
    for (const std::size_t node : result.order) {
        for (const EdgeSpec &edge : edges) {
            if (edge.toNode == node) {
                const std::uint32_t through = isHosted(hosted, edge.fromNode) ? 1U : 0U;
                result.hosts[node] = std::max(result.hosts[node], result.hosts[edge.fromNode] + through);
            }
        }
    }
    return result;
}

} // namespace

std::expected<Partition, GraphError>
partition(std::size_t nodeCount, std::span<const EdgeSpec> edges, const std::vector<bool> &hosted)
{
    const std::expected<Reach, GraphError> reached = reach(nodeCount, edges, hosted);
    if (!reached) {
        return std::unexpected(reached.error());
    }
    Partition result;
    result.segmentOf.resize(nodeCount, 0);
    for (std::size_t node = 0; node < nodeCount; ++node) {
        // A hosted node's inputs are gathered in the segment its latest
        // one comes from, which may be a segment nothing else is in.
        result.segmentCount = std::max(result.segmentCount, reached->hosts[node] + 1);
        result.segmentOf[node] = isHosted(hosted, node) ? kHosted : reached->hosts[node];
    }
    return result;
}

dsp::ChannelPosition boundaryChannel(std::uint32_t port) noexcept
{
    if (port == 0) {
        return dsp::ChannelPosition::FrontLeft;
    }
    if (port == 1) {
        return dsp::ChannelPosition::FrontRight;
    }
    return dsp::auxChannel(port - 2);
}

std::expected<SegmentPlan, GraphError> splitIntoSegments(const GraphSpec &whole, const std::vector<bool> &hosted)
{
    const std::size_t count = whole.nodes.size();
    const std::expected<Reach, GraphError> reached = reach(count, whole.edges, hosted);
    if (!reached) {
        return std::unexpected(reached.error());
    }
    std::expected<Partition, GraphError> cut = partition(count, whole.edges, hosted);
    if (!cut) {
        return std::unexpected(cut.error());
    }

    SegmentPlan plan;
    plan.partition = std::move(*cut);
    const std::uint32_t segments = plan.partition.segmentCount;
    plan.segments.resize(segments);
    plan.origin.resize(segments);
    for (GraphSpec &segment : plan.segments) {
        segment.sampleRate = whole.sampleRate;
        segment.maxBlockFrames = whole.maxBlockFrames;
    }

    // **Nothing to cut: the one segment is the session as it was**, node
    // for node and wire for wire, so a session with no plugin in it runs
    // exactly as it did before there were segments.
    if (std::ranges::none_of(hosted, [](bool value) { return value; })) {
        plan.segments[0] = whole;
        for (std::size_t node = 0; node < count; ++node) {
            plan.origin[0].push_back(node);
        }
        return plan;
    }

    // How late the sound is by the time it leaves each node.
    std::vector<std::uint32_t> latency(count, 0);
    for (const std::size_t node : reached->order) {
        std::uint32_t arriving = 0;
        for (const EdgeSpec &edge : whole.edges) {
            if (edge.toNode == node) {
                arriving = std::max(arriving, latency[edge.fromNode]);
            }
        }
        latency[node] = arriving + whole.nodes[node].block->info().latencySamples;
    }

    std::vector<std::size_t> local(count, SegmentPlan::kBoundaryNode);
    for (std::size_t node = 0; node < count; ++node) {
        if (plan.partition.hosted(node)) {
            continue;
        }
        const std::uint32_t segment = plan.partition.segmentOf[node];
        local[node] = plan.segments[segment].nodes.size();
        plan.segments[segment].nodes.push_back(whole.nodes[node]);
        plan.origin[segment].push_back(node);
    }

    const auto addEdge = [&plan](std::uint32_t segment, EdgeSpec edge) {
        std::vector<EdgeSpec> &edges = plan.segments[segment].edges;
        if (std::ranges::find(edges, edge) == edges.end()) {
            edges.push_back(edge);
        }
    };
    const auto addLink = [&plan](SegmentPlan::PortLink link) {
        if (std::ranges::find(plan.links, link) == plan.links.end()) {
            plan.links.push_back(link);
        }
    };

    /// The boundary called \a id, made the first time it is asked for, and
    /// wide enough for \a port. Answers its index and its node in its
    /// segment's spec.
    struct Made {
        std::size_t boundary = 0;
        std::size_t node = 0;
    };
    const auto boundary = [&plan](const std::string &id,
                                  SegmentBoundary::Direction direction,
                                  std::uint32_t segment,
                                  std::size_t node,
                                  std::uint32_t port) {
        for (std::size_t index = 0; index < plan.boundaries.size(); ++index) {
            SegmentPlan::Boundary &existing = plan.boundaries[index];
            if (existing.id == id) {
                existing.channels = std::max(existing.channels, port + 1);
                const std::vector<NodeSpec> &nodes = plan.segments[segment].nodes;
                const auto found = std::ranges::find_if(nodes, [&id](const NodeSpec &spec) { return spec.id == id; });
                return Made{index, static_cast<std::size_t>(std::distance(nodes.begin(), found))};
            }
        }
        SegmentPlan::Boundary made;
        made.id = id;
        made.direction = direction;
        made.segment = segment;
        made.channels = port + 1;
        made.node = node;
        plan.boundaries.push_back(made);
        NodeSpec spec;
        spec.id = id;
        plan.segments[segment].nodes.push_back(spec);
        plan.origin[segment].push_back(SegmentPlan::kBoundaryNode);
        return Made{plan.boundaries.size() - 1, plan.segments[segment].nodes.size() - 1};
    };

    /// Where port \a port of node \a from can be read in segment \a segment:
    /// the node itself when it runs there, and otherwise a boundary that
    /// brings it in — from a host, or from the segment it runs in.
    const auto readable = [&](std::size_t from, std::uint32_t port, std::uint32_t segment) {
        if (!plan.partition.hosted(from) && plan.partition.segmentOf[from] == segment) {
            return std::pair{local[from], port};
        }
        const std::string &name = whole.nodes[from].id;
        const Made arriving = boundary(
            name + ".recv" + std::to_string(segment), SegmentBoundary::Direction::Arriving, segment, from, port);
        plan.boundaries[arriving.boundary].carriedLatency = latency[from];
        SegmentPlan::PortRef source;
        if (plan.partition.hosted(from)) {
            source = SegmentPlan::PortRef{true, from, port};
        } else {
            const std::uint32_t home = plan.partition.segmentOf[from];
            const Made leaving = boundary(name + ".send", SegmentBoundary::Direction::Leaving, home, from, port);
            addEdge(home, EdgeSpec{local[from], port, leaving.node, port});
            source = SegmentPlan::PortRef{false, leaving.boundary, port};
        }
        addLink(SegmentPlan::PortLink{source, SegmentPlan::PortRef{false, arriving.boundary, port}});
        return std::pair{arriving.node, port};
    };

    for (const EdgeSpec &edge : whole.edges) {
        if (!plan.partition.hosted(edge.toNode)) {
            const std::uint32_t segment = plan.partition.segmentOf[edge.toNode];
            const auto [node, port] = readable(edge.fromNode, edge.fromPort, segment);
            addEdge(segment, EdgeSpec{node, port, local[edge.toNode], edge.toPort});
            continue;
        }
        // Into a host: gathered in the segment its latest input comes from,
        // and sent to it from there.
        const std::uint32_t segment = reached->hosts[edge.toNode];
        const auto [node, port] = readable(edge.fromNode, edge.fromPort, segment);
        const Made feed = boundary(whole.nodes[edge.toNode].id + ".feed",
                                   SegmentBoundary::Direction::Leaving,
                                   segment,
                                   edge.toNode,
                                   edge.toPort);
        addEdge(segment, EdgeSpec{node, port, feed.node, edge.toPort});
        addLink(SegmentPlan::PortLink{SegmentPlan::PortRef{false, feed.boundary, edge.toPort},
                                      SegmentPlan::PortRef{true, edge.toNode, edge.toPort}});
    }
    return plan;
}

SegmentBoundary::SegmentBoundary(Direction direction, std::uint32_t channels)
    : m_direction(direction)
{
    m_info.typeId = std::string{direction == Direction::Arriving ? kArrivingTypeId : kLeavingTypeId};
    m_info.category = dsp::BlockCategory::Advanced;
    m_info.inputs = dsp::PortSpec::fixed(channels);
    m_info.outputs = dsp::PortSpec::fixed(channels);
    m_info.inputsFromAudioSystem = direction == Direction::Arriving;
    m_info.outputsToAudioSystem = direction == Direction::Leaving;
}

std::expected<void, dsp::BlockError> SegmentBoundary::prepare(const dsp::PrepareSpec &spec)
{
    if (spec.maxBlockFrames == 0 || spec.maxBlockFrames > dsp::kMaxBlockFrames) {
        return std::unexpected(dsp::BlockError::UnsupportedSpec);
    }
    return {};
}

void SegmentBoundary::process(dsp::ProcessContext &context) noexcept RB_NONBLOCKING
{
    for (std::uint32_t channel = 0; channel < context.output.channelCount(); ++channel) {
        const std::span<float> out = context.output.channel(channel);
        if (out.empty()) {
            continue;
        }
        const std::span<float> in =
            channel < context.input.channelCount() ? context.input.channel(channel) : std::span<float>{};
        if (in.empty()) {
            std::fill(out.begin(), out.end(), 0.0f);
        } else if (in.data() != out.data()) {
            std::copy_n(in.begin(), std::min(in.size(), out.size()), out.begin());
        }
    }
}

} // namespace rb::graph
