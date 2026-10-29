// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <cstddef>
#include <expected>
#include <span>
#include <vector>

namespace rb::graph
{

/// A directed edge between two nodes of a graph, identified by index.
struct Edge {
    std::size_t from;
    std::size_t to;
};

enum class TopologyError {
    Cycle, ///< The edges contain a cycle, so no order exists.
    NodeOutOfRange, ///< An edge names a node index that is not below the node count.
};

/// Orders the nodes 0 .. nodeCount-1 so that every edge points from an earlier
/// node to a later one. When several nodes are ready at the same time, the one
/// with the lowest index comes first, so equal inputs always give the same order.
[[nodiscard]] std::expected<std::vector<std::size_t>, TopologyError> topologicalOrder(std::size_t nodeCount,
                                                                                      std::span<const Edge> edges);

} // namespace rb::graph
