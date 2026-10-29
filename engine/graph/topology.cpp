// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "graph/topology.h"

#include <functional>
#include <queue>

namespace rb::graph
{

std::expected<std::vector<std::size_t>, TopologyError> topologicalOrder(std::size_t nodeCount,
                                                                        std::span<const Edge> edges)
{
    std::vector<std::size_t> pendingInputs(nodeCount, 0);
    std::vector<std::vector<std::size_t>> successors(nodeCount);
    for (const Edge &edge : edges) {
        if (edge.from >= nodeCount || edge.to >= nodeCount) {
            return std::unexpected(TopologyError::NodeOutOfRange);
        }
        successors[edge.from].push_back(edge.to);
        ++pendingInputs[edge.to];
    }

    // Kahn's algorithm with a min-heap, so ties go to the lowest index.
    std::priority_queue<std::size_t, std::vector<std::size_t>, std::greater<>> ready;
    for (std::size_t node = 0; node < nodeCount; ++node) {
        if (pendingInputs[node] == 0) {
            ready.push(node);
        }
    }

    std::vector<std::size_t> order;
    order.reserve(nodeCount);
    while (!ready.empty()) {
        const std::size_t node = ready.top();
        ready.pop();
        order.push_back(node);
        for (const std::size_t next : successors[node]) {
            if (--pendingInputs[next] == 0) {
                ready.push(next);
            }
        }
    }

    // Nodes on a cycle never reach zero pending inputs.
    if (order.size() != nodeCount) {
        return std::unexpected(TopologyError::Cycle);
    }
    return order;
}

} // namespace rb::graph
