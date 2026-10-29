// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "graph/topology.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <numeric>
#include <random>
#include <vector>

using namespace rb::graph;

using Order = std::vector<std::size_t>;

TEST(Topology, EmptyGraph)
{
    const auto order = topologicalOrder(0, {});
    ASSERT_TRUE(order.has_value());
    EXPECT_TRUE(order->empty());
}

TEST(Topology, NodesWithoutEdgesKeepIndexOrder)
{
    EXPECT_EQ(topologicalOrder(3, {}), (Order{0, 1, 2}));
}

TEST(Topology, Chain)
{
    const std::vector<Edge> edges{{2, 1}, {1, 0}};
    EXPECT_EQ(topologicalOrder(3, edges), (Order{2, 1, 0}));
}

TEST(Topology, FanOutAndMerge)
{
    // 0 feeds 1 and 2, which both feed 3.
    const std::vector<Edge> edges{{0, 2}, {0, 1}, {2, 3}, {1, 3}};
    EXPECT_EQ(topologicalOrder(4, edges), (Order{0, 1, 2, 3}));
}

TEST(Topology, TiesGoToLowestIndex)
{
    const std::vector<Edge> edges{{3, 1}};
    EXPECT_EQ(topologicalOrder(4, edges), (Order{0, 2, 3, 1}));
}

TEST(Topology, CycleIsRejected)
{
    const std::vector<Edge> edges{{0, 1}, {1, 2}, {2, 0}};
    EXPECT_EQ(topologicalOrder(3, edges).error(), TopologyError::Cycle);
}

TEST(Topology, SelfLoopIsRejected)
{
    const std::vector<Edge> edges{{1, 1}};
    EXPECT_EQ(topologicalOrder(2, edges).error(), TopologyError::Cycle);
}

TEST(Topology, NodeOutOfRangeIsRejected)
{
    const std::vector<Edge> edges{{0, 5}};
    EXPECT_EQ(topologicalOrder(2, edges).error(), TopologyError::NodeOutOfRange);
}

TEST(Topology, RandomAcyclicGraphsAreOrdered)
{
    std::mt19937 random(20260919);
    for (int round = 0; round < 200; ++round) {
        const std::size_t nodeCount = std::uniform_int_distribution<std::size_t>(1, 40)(random);

        // Edges only go forward in a random ranking, so the graph is acyclic.
        std::vector<std::size_t> rank(nodeCount);
        std::iota(rank.begin(), rank.end(), std::size_t{0});
        std::shuffle(rank.begin(), rank.end(), random);
        std::vector<Edge> edges;
        std::bernoulli_distribution connect(0.15);
        for (std::size_t a = 0; a < nodeCount; ++a) {
            for (std::size_t b = a + 1; b < nodeCount; ++b) {
                if (connect(random)) {
                    edges.push_back({rank[a], rank[b]});
                }
            }
        }

        const auto order = topologicalOrder(nodeCount, edges);
        ASSERT_TRUE(order.has_value()) << "round " << round;
        ASSERT_EQ(order->size(), nodeCount);
        std::vector<std::size_t> position(nodeCount);
        for (std::size_t i = 0; i < nodeCount; ++i) {
            position[(*order)[i]] = i;
        }
        for (const Edge &edge : edges) {
            EXPECT_LT(position[edge.from], position[edge.to]) << "round " << round;
        }
    }
}
