// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "graph/graphdiff.h"

#include <algorithm>
#include <map>
#include <set>
#include <tuple>

namespace rb::graph
{

namespace
{

/// A node's identity for this purpose: the block behind it and whether it is
/// doing anything. Two specs agree about a node when these agree.
struct NodeIdentity {
    const dsp::Block *block = nullptr;
    bool enabled = true;

    [[nodiscard]] bool operator==(const NodeIdentity &other) const = default;
};

/// One wire, in the terms the two specs can be compared in: node **ids**,
/// because indices mean different things in different specs.
using NamedEdge = std::tuple<std::string, std::uint32_t, std::string, std::uint32_t>;

[[nodiscard]] std::map<std::string, NodeIdentity> identities(const GraphSpec &spec)
{
    std::map<std::string, NodeIdentity> result;
    for (const NodeSpec &node : spec.nodes) {
        result.emplace(node.id, NodeIdentity{node.block, node.enabled});
    }
    return result;
}

[[nodiscard]] std::set<NamedEdge> namedEdges(const GraphSpec &spec)
{
    std::set<NamedEdge> result;
    for (const EdgeSpec &edge : spec.edges) {
        if (edge.fromNode >= spec.nodes.size() || edge.toNode >= spec.nodes.size()) {
            continue; // a wire that names no node changes nothing that can be heard
        }
        result.emplace(spec.nodes[edge.fromNode].id, edge.fromPort, spec.nodes[edge.toNode].id, edge.toPort);
    }
    return result;
}

} // namespace

std::vector<std::string> changedBlocks(const GraphSpec &before, const GraphSpec &after)
{
    const std::map<std::string, NodeIdentity> was = identities(before);
    const std::map<std::string, NodeIdentity> now = identities(after);
    const std::set<NamedEdge> wiredBefore = namedEdges(before);
    const std::set<NamedEdge> wiredAfter = namedEdges(after);

    // A block is touched when it is not the same block doing the same thing,
    // or when what arrives at it comes from somewhere else.
    std::set<std::string> touched;
    for (const auto &[id, identity] : now) {
        const auto found = was.find(id);
        if (found == was.end() || !(found->second == identity)) {
            touched.insert(id);
        }
    }
    for (const auto &[id, identity] : was) {
        if (!now.contains(id)) {
            touched.insert(id);
        }
    }
    const auto edgeTouches = [&touched](const std::set<NamedEdge> &one, const std::set<NamedEdge> &other) {
        for (const NamedEdge &edge : one) {
            if (!other.contains(edge)) {
                // Only the end the wire arrives at. What leaves a block
                // depends on what arrives at it and on its own state, not on
                // who is listening, so a block whose output has been wired
                // somewhere else still puts out exactly what it did before.
                touched.insert(std::get<2>(edge));
            }
        }
    };
    edgeTouches(wiredAfter, wiredBefore);
    edgeTouches(wiredBefore, wiredAfter);

    // What a touched block feeds hears the difference too, and so on down.
    std::map<std::string, std::vector<std::string>> downstream;
    for (const NamedEdge &edge : wiredAfter) {
        downstream[std::get<0>(edge)].push_back(std::get<2>(edge));
    }
    std::vector<std::string> pending(touched.begin(), touched.end());
    while (!pending.empty()) {
        const std::string from = std::move(pending.back());
        pending.pop_back();
        const auto found = downstream.find(from);
        if (found == downstream.end()) {
            continue;
        }
        for (const std::string &to : found->second) {
            if (touched.insert(to).second) {
                pending.push_back(to);
            }
        }
    }

    // In the order the new graph names them, so a caller can walk its own
    // lanes alongside; then whatever only the old graph had.
    std::vector<std::string> result;
    result.reserve(touched.size());
    for (const NodeSpec &node : after.nodes) {
        if (touched.contains(node.id)) {
            result.push_back(node.id);
        }
    }
    for (const NodeSpec &node : before.nodes) {
        if (touched.contains(node.id) && !now.contains(node.id) && std::ranges::find(result, node.id) == result.end()) {
            result.push_back(node.id);
        }
    }
    return result;
}

} // namespace rb::graph
