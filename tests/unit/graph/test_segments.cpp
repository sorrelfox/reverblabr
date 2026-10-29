// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// A session cut around the blocks that run in plugin hosts (ADR-0014):
// which segment each block lands in, what crosses between them, and that
// latency compensation still lines up paths that went through a plugin.

#include "graph/compiledgraph.h"
#include "graph/segments.h"
#include "testblocks.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace rb;
using namespace rb::graph;

namespace
{

[[nodiscard]] NodeSpec node(const std::string &id, dsp::Block *block)
{
    NodeSpec spec;
    spec.id = id;
    spec.block = block;
    return spec;
}

[[nodiscard]] EdgeSpec edge(std::size_t from, std::size_t to, std::uint32_t toPort = 0)
{
    return EdgeSpec{from, 0, to, toPort};
}

/// The ids of a segment's nodes, in its spec's order.
[[nodiscard]] std::vector<std::string> idsOf(const GraphSpec &spec)
{
    std::vector<std::string> ids;
    for (const NodeSpec &entry : spec.nodes) {
        ids.push_back(entry.id);
    }
    return ids;
}

[[nodiscard]] const SegmentPlan::Boundary *boundaryCalled(const SegmentPlan &plan, const std::string &id)
{
    for (const SegmentPlan::Boundary &boundary : plan.boundaries) {
        if (boundary.id == id) {
            return &boundary;
        }
    }
    return nullptr;
}

/// A link written out: "P1.feed:0 -> host 1:0".
[[nodiscard]] std::string describe(const SegmentPlan &plan, const SegmentPlan::PortLink &link)
{
    const auto end = [&plan](const SegmentPlan::PortRef &ref) {
        const std::string name = ref.hosted ? "host " + std::to_string(ref.index) : plan.boundaries[ref.index].id;
        return name + ":" + std::to_string(ref.port);
    };
    return end(link.from) + " -> " + end(link.to);
}

[[nodiscard]] std::vector<std::string> linksOf(const SegmentPlan &plan)
{
    std::vector<std::string> links;
    for (const SegmentPlan::PortLink &link : plan.links) {
        links.push_back(describe(plan, link));
    }
    return links;
}

/// Puts a boundary block of the right shape wherever the plan left a gap,
/// and prepares every block of the segment.
void fillBoundaries(SegmentPlan &plan, std::uint32_t segment, std::vector<std::unique_ptr<SegmentBoundary>> &owned)
{
    GraphSpec &spec = plan.segments[segment];
    for (NodeSpec &entry : spec.nodes) {
        if (entry.block != nullptr) {
            continue;
        }
        const SegmentPlan::Boundary *boundary = boundaryCalled(plan, entry.id);
        ASSERT_NE(boundary, nullptr) << entry.id;
        owned.push_back(std::make_unique<SegmentBoundary>(boundary->direction, boundary->channels));
        owned.back()->setCarriedLatency(boundary->carriedLatency);
        entry.block = owned.back().get();
    }
    dsp::PrepareSpec prepare;
    prepare.inputChannels = 1;
    prepare.outputChannels = 1;
    for (const NodeSpec &entry : spec.nodes) {
        ASSERT_TRUE(entry.block->prepare(prepare).has_value()) << entry.id;
    }
}

} // namespace

TEST(Segments, ASessionWithNoPluginIsOneSegmentExactlyAsItWas)
{
    test::ConstantSource source;
    test::GainStage gain;
    GraphSpec whole;
    whole.nodes = {node("src", &source), node("gain", &gain)};
    whole.edges = {edge(0, 1)};

    const std::expected<SegmentPlan, GraphError> plan = splitIntoSegments(whole, {false, false});
    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ(plan->partition.segmentCount, 1U);
    ASSERT_EQ(plan->segments.size(), 1U);
    EXPECT_EQ(idsOf(plan->segments[0]), idsOf(whole));
    EXPECT_EQ(plan->segments[0].edges, whole.edges);
    EXPECT_TRUE(plan->boundaries.empty());
    EXPECT_TRUE(plan->links.empty());
    EXPECT_EQ(plan->origin[0], (std::vector<std::size_t>{0, 1}));
}

TEST(Segments, TwoPluginsInSeriesMakeThreeSegments)
{
    // src -> P1 -> mid -> P2 -> out
    test::ConstantSource source;
    test::GainStage first;
    test::GainStage middle;
    test::GainStage second;
    test::GainStage out;
    first.setLatencySamples(30);
    second.setLatencySamples(70);
    GraphSpec whole;
    whole.nodes = {
        node("src", &source), node("P1", &first), node("mid", &middle), node("P2", &second), node("out", &out)};
    whole.edges = {edge(0, 1), edge(1, 2), edge(2, 3), edge(3, 4)};

    const std::expected<SegmentPlan, GraphError> plan = splitIntoSegments(whole, {false, true, false, true, false});
    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ(plan->partition.segmentCount, 3U);
    EXPECT_EQ(plan->partition.segmentOf, (std::vector<std::uint32_t>{0, kHosted, 1, kHosted, 2}));
    EXPECT_EQ(idsOf(plan->segments[0]), (std::vector<std::string>{"src", "P1.feed"}));
    EXPECT_EQ(idsOf(plan->segments[1]), (std::vector<std::string>{"mid", "P1.recv1", "P2.feed"}));
    EXPECT_EQ(idsOf(plan->segments[2]), (std::vector<std::string>{"out", "P2.recv2"}));
    EXPECT_EQ(
        linksOf(*plan),
        (std::vector<std::string>{
            "P1.feed:0 -> host 1:0", "host 1:0 -> P1.recv1:0", "P2.feed:0 -> host 3:0", "host 3:0 -> P2.recv2:0"}));
    // What arrives from each plugin has picked up every latency before it.
    EXPECT_EQ(boundaryCalled(*plan, "P1.recv1")->carriedLatency, 30U);
    EXPECT_EQ(boundaryCalled(*plan, "P2.recv2")->carriedLatency, 100U);
}

TEST(Segments, TwoPluginsInParallelMeetInOneSegmentLinedUp)
{
    // src -> P1 (100 samples) -> mix:0, src -> P2 (none) -> mix:1, mix -> out
    test::ConstantSource source;
    test::GainStage slow;
    test::GainStage fast;
    test::GainStage mix(1.0f, 2);
    test::GainStage out;
    slow.setLatencySamples(100);
    GraphSpec whole;
    whole.nodes = {node("src", &source), node("P1", &slow), node("P2", &fast), node("mix", &mix), node("out", &out)};
    whole.edges = {edge(0, 1), edge(0, 2), edge(1, 3, 0), edge(2, 3, 1), edge(3, 4)};

    std::expected<SegmentPlan, GraphError> plan = splitIntoSegments(whole, {false, true, true, false, false});
    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ(plan->partition.segmentCount, 2U);
    EXPECT_EQ(idsOf(plan->segments[0]), (std::vector<std::string>{"src", "P1.feed", "P2.feed"}));
    EXPECT_EQ(idsOf(plan->segments[1]), (std::vector<std::string>{"mix", "out", "P1.recv1", "P2.recv1"}));
    EXPECT_EQ(boundaryCalled(*plan, "P1.recv1")->carriedLatency, 100U);
    EXPECT_EQ(boundaryCalled(*plan, "P2.recv1")->carriedLatency, 0U);

    // The later segment's graph holds the faster path back by the slower
    // one's latency, exactly as it would inside one graph.
    std::vector<std::unique_ptr<SegmentBoundary>> owned;
    fillBoundaries(*plan, 1, owned);
    const std::expected<CompiledGraph, GraphError> compiled = CompiledGraph::compile(plan->segments[1]);
    ASSERT_TRUE(compiled.has_value()) << graphErrorName(compiled.error());
    EXPECT_EQ(compiled->compensationCount(), 1U);
    EXPECT_EQ(compiled->longestCompensation(), 100U);
    EXPECT_EQ(compiled->latencySamples(), 100U);
}

TEST(Segments, APathAroundAPluginCrossesToTheSegmentAfterIt)
{
    // src -> P -> mix:0, and src -> mix:1 straight.
    test::ConstantSource source;
    test::GainStage plugin;
    test::GainStage mix(1.0f, 2);
    plugin.setLatencySamples(64);
    GraphSpec whole;
    whole.nodes = {node("src", &source), node("P", &plugin), node("mix", &mix)};
    whole.edges = {edge(0, 1), edge(1, 2, 0), edge(0, 2, 1)};

    std::expected<SegmentPlan, GraphError> plan = splitIntoSegments(whole, {false, true, false});
    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ(idsOf(plan->segments[0]), (std::vector<std::string>{"src", "P.feed", "src.send"}));
    EXPECT_EQ(idsOf(plan->segments[1]), (std::vector<std::string>{"mix", "P.recv1", "src.recv1"}));
    EXPECT_EQ(linksOf(*plan),
              (std::vector<std::string>{"P.feed:0 -> host 1:0", "host 1:0 -> P.recv1:0", "src.send:0 -> src.recv1:0"}));

    std::vector<std::unique_ptr<SegmentBoundary>> owned;
    fillBoundaries(*plan, 1, owned);
    const std::expected<CompiledGraph, GraphError> compiled = CompiledGraph::compile(plan->segments[1]);
    ASSERT_TRUE(compiled.has_value());
    EXPECT_EQ(compiled->longestCompensation(), 64U);
}

TEST(Segments, OnePluginIntoAnotherMeetsTheOtherInputsInASegmentFirst)
{
    // src -> P1 (50) -> P2:0, src -> P2:1, P2 -> out. P2's two inputs are
    // gathered in segment 1, where the straight one can be held back.
    test::ConstantSource source;
    test::GainStage first;
    test::GainStage second(1.0f, 2);
    test::GainStage out;
    first.setLatencySamples(50);
    GraphSpec whole;
    whole.nodes = {node("src", &source), node("P1", &first), node("P2", &second), node("out", &out)};
    whole.edges = {edge(0, 1), edge(1, 2, 0), edge(0, 2, 1), edge(2, 3)};

    std::expected<SegmentPlan, GraphError> plan = splitIntoSegments(whole, {false, true, true, false});
    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ(plan->partition.segmentCount, 3U);
    EXPECT_EQ(idsOf(plan->segments[1]), (std::vector<std::string>{"P1.recv1", "P2.feed", "src.recv1"}));
    EXPECT_EQ(idsOf(plan->segments[2]), (std::vector<std::string>{"out", "P2.recv2"}));
    for (const std::string &link : linksOf(*plan)) {
        EXPECT_EQ(link.find("host 1:0 -> host"), std::string::npos) << "a host straight into a host: " << link;
    }

    std::vector<std::unique_ptr<SegmentBoundary>> owned;
    fillBoundaries(*plan, 1, owned);
    const std::expected<CompiledGraph, GraphError> compiled = CompiledGraph::compile(plan->segments[1]);
    ASSERT_TRUE(compiled.has_value());
    EXPECT_EQ(compiled->longestCompensation(), 50U);
}

TEST(Segments, AFeedbackLoopIsRefused)
{
    test::GainStage one;
    test::GainStage two;
    GraphSpec whole;
    whole.nodes = {node("a", &one), node("b", &two)};
    whole.edges = {edge(0, 1), edge(1, 0)};
    const std::expected<SegmentPlan, GraphError> plan = splitIntoSegments(whole, {false, true});
    ASSERT_FALSE(plan.has_value());
    EXPECT_EQ(plan.error(), GraphError::Cycle);
}

TEST(Segments, ABoundaryPassesWhatItIsGiven)
{
    SegmentBoundary arriving(SegmentBoundary::Direction::Arriving, 2);
    EXPECT_TRUE(arriving.info().inputsFromAudioSystem);
    EXPECT_FALSE(arriving.info().outputsToAudioSystem);
    arriving.setCarriedLatency(123);
    EXPECT_EQ(arriving.info().latencySamples, 123U);

    std::vector<float> left(16, 0.5f);
    std::vector<float> outLeft(16, -1.0f);
    std::vector<float> outRight(16, -1.0f);
    float *in[] = {left.data(), nullptr};
    float *out[] = {outLeft.data(), outRight.data()};
    dsp::ProcessContext context{dsp::AudioBlock{in, 2, 16}, dsp::AudioBlock{out, 2, 16}, 16};
    arriving.process(context);
    EXPECT_FLOAT_EQ(outLeft[15], 0.5f);
    EXPECT_FLOAT_EQ(outRight[0], 0.0f) << "a channel with nothing arriving is silence";
}

TEST(Segments, TheChannelsOfABoundaryAreTheBussesThenAuxiliaries)
{
    EXPECT_EQ(boundaryChannel(0), dsp::ChannelPosition::FrontLeft);
    EXPECT_EQ(boundaryChannel(1), dsp::ChannelPosition::FrontRight);
    EXPECT_EQ(boundaryChannel(2), dsp::ChannelPosition::Aux0);
}
