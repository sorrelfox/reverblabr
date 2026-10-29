// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "dsp/block.h"
#include "graph/compiledgraph.h"
#include "graph/graphspec.h"
#include "rt/clock.h"
#include "testblocks.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <span>
#include <string>
#include <vector>

using namespace rb::graph;
using namespace rb::test;

namespace
{

constexpr std::uint32_t kMaxFrames = 1024;

/// Builds a spec out of blocks the test owns, and prepares each block for the
/// ports the compiler gave it.
///
/// Preparing is the runtime's job, not the compiler's — re-preparing on every
/// compile would reset the very state a recompile is meant to keep — so the
/// test does what the runtime will do: compile first, then prepare each block
/// for the port counts the plan settled on.
class Fixture
{
public:
    /// Adds a block and returns its node index.
    std::size_t add(std::unique_ptr<rb::dsp::Block> block, const std::string &id, bool enabled = true)
    {
        m_owned.push_back(std::move(block));
        m_spec.nodes.push_back(NodeSpec{id, m_owned.back().get(), enabled});
        return m_spec.nodes.size() - 1;
    }

    void wire(std::size_t fromNode, std::uint32_t fromPort, std::size_t toNode, std::uint32_t toPort)
    {
        m_spec.edges.push_back(EdgeSpec{fromNode, fromPort, toNode, toPort});
    }

    [[nodiscard]] GraphSpec &spec()
    {
        return m_spec;
    }

    [[nodiscard]] std::expected<CompiledGraph, GraphError> compile()
    {
        m_spec.maxBlockFrames = kMaxFrames;
        return CompiledGraph::compile(m_spec);
    }

    /// Compiles and prepares, the way the runtime will.
    [[nodiscard]] CompiledGraph compileAndPrepare()
    {
        std::expected<CompiledGraph, GraphError> graph = compile();
        EXPECT_TRUE(graph.has_value()) << (graph ? "" : graphErrorName(graph.error()));
        prepare(*graph);
        return std::move(*graph);
    }

    void prepare(const CompiledGraph &graph)
    {
        for (std::size_t node = 0; node < m_spec.nodes.size(); ++node) {
            const CompiledGraph::PortCounts counts = graph.portCounts(node);
            rb::dsp::PrepareSpec spec;
            spec.sampleRate = m_spec.sampleRate;
            spec.maxBlockFrames = kMaxFrames;
            spec.inputChannels = counts.inputs;
            spec.outputChannels = counts.outputs;
            EXPECT_TRUE(m_spec.nodes[node].block->prepare(spec).has_value());
        }
    }

private:
    std::vector<std::unique_ptr<rb::dsp::Block>> m_owned;
    GraphSpec m_spec;
};

/// Passes its input through and takes at least \a microseconds doing it,
/// which is what a block that costs something looks like to a clock.
class SlowStage final : public SimpleBlock
{
public:
    explicit SlowStage(std::uint64_t microseconds)
        : SimpleBlock("test.slowStage", rb::dsp::BlockCategory::Effects, 1, 1)
        , m_nanos(microseconds * 1000)
    {
    }

    void process(rb::dsp::ProcessContext &context) noexcept RB_NONBLOCKING override
    {
        const std::uint64_t started = rb::rt::monotonicNanos();
        const std::span<float> out = context.output.channel(0);
        const std::span<float> in = context.input.channel(0);
        for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
            out[frame] = in.empty() ? 0.0f : in[frame];
        }
        while (rb::rt::monotonicNanos() - started < m_nanos) { }
    }

private:
    std::uint64_t m_nanos;
};

} // namespace

TEST(CompiledGraph, RunsAChainInOrder)
{
    Fixture fixture;
    auto *source = new ConstantSource(0.5f);
    auto *gain = new GainStage(2.0f);
    auto *sink = new Sink();
    const std::size_t s = fixture.add(std::unique_ptr<rb::dsp::Block>(source), "b1");
    const std::size_t g = fixture.add(std::unique_ptr<rb::dsp::Block>(gain), "b2");
    const std::size_t k = fixture.add(std::unique_ptr<rb::dsp::Block>(sink), "b3");
    fixture.wire(s, 0, g, 0);
    fixture.wire(g, 0, k, 0);

    CompiledGraph graph = fixture.compileAndPrepare();
    SimpleBlock::s_tick = 0;
    graph.execute(64);

    EXPECT_LT(source->lastRunAt(), gain->lastRunAt());
    EXPECT_LT(gain->lastRunAt(), sink->lastRunAt());
    ASSERT_EQ(sink->framesHeard(), 64u);
    for (const float sample : sink->heard(0)) {
        EXPECT_FLOAT_EQ(sample, 1.0f) << "0.5 through a gain of two";
    }
    EXPECT_EQ(graph.scheduleOfNodes().size(), 3u);
    EXPECT_EQ(graph.nodeId(0), "b1");
    EXPECT_EQ(graph.sumNodeCount(), 0u);
}

TEST(CompiledGraph, FansOutWithoutCopying)
{
    // One output feeding two inputs has to reach both of them, and has to do
    // it out of the one buffer it was written into.
    Fixture fixture;
    auto *source = new ConstantSource(0.25f);
    auto *left = new Sink();
    auto *right = new Sink();
    const std::size_t s = fixture.add(std::unique_ptr<rb::dsp::Block>(source), "b1");
    const std::size_t l = fixture.add(std::unique_ptr<rb::dsp::Block>(left), "b2");
    const std::size_t r = fixture.add(std::unique_ptr<rb::dsp::Block>(right), "b3");
    fixture.wire(s, 0, l, 0);
    fixture.wire(s, 0, r, 0);

    CompiledGraph graph = fixture.compileAndPrepare();
    graph.execute(32);

    EXPECT_EQ(graph.sumNodeCount(), 0u) << "fan-out is not a sum";
    ASSERT_EQ(left->heard(0).size(), 32u);
    ASSERT_EQ(right->heard(0).size(), 32u);
    EXPECT_FLOAT_EQ(left->heard(0)[0], 0.25f);
    EXPECT_FLOAT_EQ(right->heard(0)[0], 0.25f);
    EXPECT_EQ(graph.bufferCount(), 1u) << "one signal, one buffer";
}

TEST(CompiledGraph, ThreeWiresIntoOnePortAddUpToTheAnalyticSum)
{
    Fixture fixture;
    auto *first = new ConstantSource(0.125f);
    auto *second = new ConstantSource(0.25f);
    auto *third = new ConstantSource(-0.5f);
    auto *sink = new Sink();
    const std::size_t a = fixture.add(std::unique_ptr<rb::dsp::Block>(first), "b1");
    const std::size_t b = fixture.add(std::unique_ptr<rb::dsp::Block>(second), "b2");
    const std::size_t c = fixture.add(std::unique_ptr<rb::dsp::Block>(third), "b3");
    const std::size_t k = fixture.add(std::unique_ptr<rb::dsp::Block>(sink), "b4");
    fixture.wire(a, 0, k, 0);
    fixture.wire(b, 0, k, 0);
    fixture.wire(c, 0, k, 0);

    CompiledGraph graph = fixture.compileAndPrepare();
    graph.execute(16);

    EXPECT_EQ(graph.sumNodeCount(), 1u) << "one hidden sum, not three wires into one port";
    ASSERT_EQ(sink->heard(0).size(), 16u);
    for (const float sample : sink->heard(0)) {
        EXPECT_FLOAT_EQ(sample, 0.125f + 0.25f - 0.5f);
    }
}

TEST(CompiledGraph, ACycleIsRejected)
{
    Fixture fixture;
    const std::size_t s = fixture.add(std::make_unique<ConstantSource>(), "b1");
    const std::size_t first = fixture.add(std::make_unique<GainStage>(), "b2");
    const std::size_t second = fixture.add(std::make_unique<GainStage>(), "b3");
    fixture.wire(s, 0, first, 0);
    fixture.wire(first, 0, second, 0);
    fixture.wire(second, 0, first, 0); // back round

    const std::expected<CompiledGraph, GraphError> graph = fixture.compile();
    ASSERT_FALSE(graph.has_value());
    EXPECT_EQ(graph.error(), GraphError::Cycle);
}

TEST(CompiledGraph, AGraphWithNothingToProduceAudioIsRejected)
{
    Fixture fixture;
    const std::size_t gain = fixture.add(std::make_unique<GainStage>(), "b1");
    const std::size_t sink = fixture.add(std::make_unique<Sink>(), "b2");
    fixture.wire(gain, 0, sink, 0);

    EXPECT_EQ(fixture.compile().error(), GraphError::NoSource);
    EXPECT_EQ(CompiledGraph::compile(GraphSpec{}).error(), GraphError::NoSource) << "and an empty graph too";
}

TEST(CompiledGraph, BadWiresAreRejected)
{
    Fixture fixture;
    const std::size_t s = fixture.add(std::make_unique<ConstantSource>(), "b1");
    const std::size_t k = fixture.add(std::make_unique<Sink>(), "b2");

    fixture.wire(s, 0, 99, 0);
    EXPECT_EQ(fixture.compile().error(), GraphError::NodeOutOfRange);

    fixture.spec().edges.clear();
    fixture.wire(s, 7, k, 0);
    EXPECT_EQ(fixture.compile().error(), GraphError::OutputPortOutOfRange);

    fixture.spec().edges.clear();
    fixture.wire(s, 0, k, 7);
    EXPECT_EQ(fixture.compile().error(), GraphError::InputPortOutOfRange);

    fixture.spec().edges.clear();
    fixture.wire(s, 0, k, 0);
    fixture.wire(s, 0, k, 0);
    EXPECT_EQ(fixture.compile().error(), GraphError::DuplicateEdge);
}

TEST(CompiledGraph, AWireToAPortTheAudioSystemOwnsIsRejected)
{
    // A capture block's inputs are filled by the application or the
    // device it follows, and an output block's outputs belong to the device.
    // A wire to either is two things writing one signal. The session refuses
    // such an edit and the canvas never offers the port, so what reaches
    // here is a session that came through neither — a file written by hand,
    // one from another version, or a second client — and it is refused
    // before anything runs rather than heard as a mess.
    //
    // **The category is not what says so.** These stand-ins are ordinary
    // effects that happen to have said which of their ports somebody else
    // owns, which is the whole point of asking the block rather than the
    // sidebar.
    Fixture fixture;
    const std::size_t s = fixture.add(std::make_unique<ConstantSource>(1.0f, 2), "b1");
    const std::size_t plain = fixture.add(std::make_unique<GainStage>(1.0f, 2), "b2");
    auto capture = std::make_unique<GainStage>(1.0f, 2);
    capture->setInputsFromAudioSystem(true);
    const std::size_t c = fixture.add(std::move(capture), "b3");
    auto device = std::make_unique<GainStage>(1.0f, 2);
    device->setOutputsToAudioSystem(true);
    const std::size_t d = fixture.add(std::move(device), "b4");

    fixture.wire(s, 0, c, 0);
    EXPECT_EQ(fixture.compile().error(), GraphError::InputFilledByAudioSystem);

    fixture.spec().edges.clear();
    fixture.wire(d, 0, plain, 0);
    EXPECT_EQ(fixture.compile().error(), GraphError::OutputTakenByAudioSystem);

    // **And the way round a session is really built is untouched**: out of
    // the captured block, through an effect, into the device.
    fixture.spec().edges.clear();
    fixture.wire(c, 0, plain, 0);
    fixture.wire(s, 0, plain, 1);
    fixture.wire(plain, 0, d, 0);
    const std::expected<CompiledGraph, GraphError> compiled = fixture.compile();
    EXPECT_TRUE(compiled.has_value()) << (compiled ? "" : graphErrorName(compiled.error()));
}

TEST(CompiledGraph, ANodeWithNoBlockIsRejected)
{
    GraphSpec spec;
    spec.nodes.push_back(NodeSpec{"b1", nullptr, true});
    EXPECT_EQ(CompiledGraph::compile(spec).error(), GraphError::NoBlock);
}

TEST(CompiledGraph, ACycleLongerThanTheBuffersIsRejected)
{
    Fixture fixture;
    fixture.add(std::make_unique<ConstantSource>(), "b1");
    fixture.spec().maxBlockFrames = rb::dsp::kMaxBlockFrames + 1;
    EXPECT_EQ(CompiledGraph::compile(fixture.spec()).error(), GraphError::BlockSizeTooLarge);
    fixture.spec().maxBlockFrames = 0;
    EXPECT_EQ(CompiledGraph::compile(fixture.spec()).error(), GraphError::BlockSizeTooLarge);
}

TEST(CompiledGraph, ADisabledBlockPassesAudioThrough)
{
    Fixture fixture;
    auto *source = new ConstantSource(0.75f);
    auto *gain = new GainStage(10.0f);
    auto *sink = new Sink();
    const std::size_t s = fixture.add(std::unique_ptr<rb::dsp::Block>(source), "b1");
    const std::size_t g = fixture.add(std::unique_ptr<rb::dsp::Block>(gain), "b2", /*enabled=*/false);
    const std::size_t k = fixture.add(std::unique_ptr<rb::dsp::Block>(sink), "b3");
    fixture.wire(s, 0, g, 0);
    fixture.wire(g, 0, k, 0);

    CompiledGraph graph = fixture.compileAndPrepare();
    graph.execute(8);

    EXPECT_EQ(gain->cycleCount(), 0u) << "a disabled block does not run";
    ASSERT_EQ(sink->heard(0).size(), 8u);
    EXPECT_FLOAT_EQ(sink->heard(0)[0], 0.75f) << "the signal came through untouched";
}

TEST(CompiledGraph, AnUnconnectedInputReadsAsSilence)
{
    Fixture fixture;
    fixture.add(std::make_unique<ConstantSource>(), "b1");
    auto *sink = new Sink();
    fixture.add(std::unique_ptr<rb::dsp::Block>(sink), "b2"); // nothing wired to it

    CompiledGraph graph = fixture.compileAndPrepare();
    graph.execute(8);
    ASSERT_EQ(sink->heard(0).size(), 8u);
    for (const float sample : sink->heard(0)) {
        EXPECT_EQ(sample, 0.0f);
    }
}

TEST(CompiledGraph, AnUnconnectedInputCanBePointedAtTheCallersMemory)
{
    // This is how a PipeWire buffer reaches a source block.
    Fixture fixture;
    fixture.add(std::make_unique<ConstantSource>(), "b1");
    auto *sink = new Sink();
    const std::size_t k = fixture.add(std::unique_ptr<rb::dsp::Block>(sink), "b2");

    CompiledGraph graph = fixture.compileAndPrepare();
    std::vector<float> external(kMaxFrames, 0.375f);
    EXPECT_TRUE(graph.bindExternalInput(k, 0, external.data()));
    graph.execute(4);
    ASSERT_EQ(sink->heard(0).size(), 4u);
    EXPECT_FLOAT_EQ(sink->heard(0)[0], 0.375f);

    EXPECT_TRUE(graph.bindExternalInput(k, 0, nullptr)) << "and back to silence";
    graph.execute(4);
    EXPECT_EQ(sink->heard(0)[0], 0.0f);

    EXPECT_FALSE(graph.bindExternalInput(k, 9, external.data())) << "no such port";
    EXPECT_FALSE(graph.bindExternalInput(99, 0, external.data())) << "no such node";
}

TEST(CompiledGraph, APortAWireAlreadyFeedsCannotBeRebound)
{
    Fixture fixture;
    const std::size_t s = fixture.add(std::make_unique<ConstantSource>(), "b1");
    auto *sink = new Sink();
    const std::size_t k = fixture.add(std::unique_ptr<rb::dsp::Block>(sink), "b2");
    fixture.wire(s, 0, k, 0);

    CompiledGraph graph = fixture.compileAndPrepare();
    std::vector<float> external(kMaxFrames, 1.0f);
    EXPECT_FALSE(graph.bindExternalInput(k, 0, external.data()));
}

TEST(CompiledGraph, EveryCycleLengthUpToTheLimitWorksWithNoRecompile)
{
    // PipeWire's quantum changes while a session runs. The same graph has to
    // take every length without being built again.
    Fixture fixture;
    auto *source = new ConstantSource(0.5f);
    auto *sink = new Sink();
    const std::size_t s = fixture.add(std::unique_ptr<rb::dsp::Block>(source), "b1");
    const std::size_t k = fixture.add(std::unique_ptr<rb::dsp::Block>(sink), "b2");
    fixture.wire(s, 0, k, 0);

    CompiledGraph graph = fixture.compileAndPrepare();
    for (const std::uint32_t frames : {1u, 2u, 63u, 256u, kMaxFrames}) {
        graph.execute(frames);
        ASSERT_EQ(sink->framesHeard(), frames);
        for (const float sample : sink->heard(0)) {
            EXPECT_FLOAT_EQ(sample, 0.5f) << "in cycles of " << frames;
        }
    }
}

TEST(CompiledGraph, BlockStateSurvivesARecompile)
{
    // The point of blocks living outside the plan: an edit rebuilds the plan,
    // and the envelopes carry on where they were.
    auto source = std::make_unique<ConstantSource>(1.0f);
    auto total = std::make_unique<RunningTotal>();
    auto sink = std::make_unique<Sink>();
    RunningTotal *watched = total.get();

    GraphSpec spec;
    spec.maxBlockFrames = kMaxFrames;
    spec.nodes.push_back(NodeSpec{"b1", source.get(), true});
    spec.nodes.push_back(NodeSpec{"b2", total.get(), true});
    spec.nodes.push_back(NodeSpec{"b3", sink.get(), true});
    spec.edges.push_back(EdgeSpec{0, 0, 1, 0});
    spec.edges.push_back(EdgeSpec{1, 0, 2, 0});

    std::expected<CompiledGraph, GraphError> first = CompiledGraph::compile(spec);
    ASSERT_TRUE(first.has_value());
    for (std::size_t node = 0; node < spec.nodes.size(); ++node) {
        rb::dsp::PrepareSpec prepare;
        prepare.maxBlockFrames = kMaxFrames;
        ASSERT_TRUE(spec.nodes[node].block->prepare(prepare).has_value());
    }
    first->execute(100);
    EXPECT_DOUBLE_EQ(watched->total(), 100.0);

    // The session is edited: another block appears, and the plan is rebuilt.
    auto extra = std::make_unique<GainStage>(1.0f);
    spec.nodes.push_back(NodeSpec{"b4", extra.get(), true});
    spec.edges.push_back(EdgeSpec{0, 0, 3, 0});

    std::expected<CompiledGraph, GraphError> second = CompiledGraph::compile(spec);
    ASSERT_TRUE(second.has_value());
    EXPECT_DOUBLE_EQ(watched->total(), 100.0) << "compiling must not touch a block";

    second->execute(50);
    EXPECT_DOUBLE_EQ(watched->total(), 150.0) << "it carried on rather than starting again";
}

TEST(CompiledGraph, LatencyAddsUpAlongTheLongestPath)
{
    Fixture fixture;
    auto *source = new ConstantSource();
    auto *slow = new GainStage();
    auto *slower = new GainStage();
    auto *sink = new Sink();
    slow->setLatencySamples(32);
    slower->setLatencySamples(64);

    const std::size_t s = fixture.add(std::unique_ptr<rb::dsp::Block>(source), "b1");
    const std::size_t a = fixture.add(std::unique_ptr<rb::dsp::Block>(slow), "b2");
    const std::size_t b = fixture.add(std::unique_ptr<rb::dsp::Block>(slower), "b3");
    const std::size_t k = fixture.add(std::unique_ptr<rb::dsp::Block>(sink), "b4");
    fixture.wire(s, 0, a, 0);
    fixture.wire(a, 0, b, 0);
    fixture.wire(a, 0, k, 0); // the short way round

    const CompiledGraph graph = fixture.compileAndPrepare();
    EXPECT_EQ(graph.latencyAt(s), 0u);
    EXPECT_EQ(graph.latencyAt(a), 32u);
    EXPECT_EQ(graph.latencyAt(b), 96u);
    EXPECT_EQ(graph.latencyAt(k), 32u) << "the sink is on the short path";
    EXPECT_EQ(graph.latencySamples(), 96u) << "the graph's latency is the longest of them";
}

TEST(CompiledGraph, ADisabledBlockStillDelaysTheSignalAsMuchAsItDid)
{
    // It is not processing, but it is still in the path, and a path that
    // changed length when a block was switched off would no longer line up
    // with the paths running beside it. So the bypass holds the signal back
    // by as much as the block would have.
    Fixture fixture;
    auto *source = new ConstantSource();
    auto *slow = new GainStage();
    slow->setLatencySamples(128);
    const std::size_t s = fixture.add(std::unique_ptr<rb::dsp::Block>(source), "b1");
    const std::size_t a = fixture.add(std::unique_ptr<rb::dsp::Block>(slow), "b2", /*enabled=*/false);
    fixture.wire(s, 0, a, 0);

    const CompiledGraph graph = fixture.compileAndPrepare();
    EXPECT_EQ(graph.latencySamples(), 128u);
    EXPECT_EQ(graph.latencyAt(a), 128u);
}

TEST(CompiledGraph, BuffersAreReusedOnceNothingStillReadsThem)
{
    // A chain of twenty blocks needs a handful of buffers, not twenty: that
    // is what the liveness pass is for.
    Fixture fixture;
    std::size_t previous = fixture.add(std::make_unique<ConstantSource>(), "b0");
    for (int stage = 1; stage <= 20; ++stage) {
        const std::size_t next = fixture.add(std::make_unique<GainStage>(1.0f), "b" + std::to_string(stage));
        fixture.wire(previous, 0, next, 0);
        previous = next;
    }

    const CompiledGraph graph = fixture.compileAndPrepare();
    EXPECT_LE(graph.bufferCount(), 3u) << "a straight chain needs no more than a couple of buffers";
}

TEST(CompiledGraph, TheOutputOfAnyNodeCanBeReadAfterACycle)
{
    Fixture fixture;
    auto *source = new ConstantSource(0.6f);
    const std::size_t s = fixture.add(std::unique_ptr<rb::dsp::Block>(source), "b1");

    CompiledGraph graph = fixture.compileAndPrepare();
    graph.execute(4);

    const std::span<const float> out = graph.outputBuffer(s, 0, 4);
    ASSERT_EQ(out.size(), 4u);
    for (const float sample : out) {
        EXPECT_FLOAT_EQ(sample, 0.6f);
    }
    EXPECT_TRUE(graph.outputBuffer(s, 9, 4).empty());
    EXPECT_TRUE(graph.outputBuffer(99, 0, 4).empty());
}

TEST(CompiledGraph, EqualInputsGiveTheSameScheduleEveryTime)
{
    const auto build = [] {
        Fixture fixture;
        const std::size_t s = fixture.add(std::make_unique<ConstantSource>(), "b1");
        const std::size_t a = fixture.add(std::make_unique<GainStage>(), "b2");
        const std::size_t b = fixture.add(std::make_unique<GainStage>(), "b3");
        const std::size_t k = fixture.add(std::make_unique<Sink>(), "b4");
        fixture.wire(s, 0, a, 0);
        fixture.wire(s, 0, b, 0);
        fixture.wire(a, 0, k, 0);
        fixture.wire(b, 0, k, 0);
        const CompiledGraph graph = fixture.compileAndPrepare();
        return std::vector<std::size_t>(graph.scheduleOfNodes().begin(), graph.scheduleOfNodes().end());
    };
    EXPECT_EQ(build(), build());
}

TEST(CompiledGraph, EveryRandomAcyclicGraphCompilesAndRunsInDependencyOrder)
{
    // A thousand graphs built at random, each acyclic by construction: a wire
    // only ever goes from a lower node to a higher one. Every one has to
    // compile, and every block has to run after the blocks that feed it.
    std::mt19937 random(20260920); // fixed, so a failure can be reproduced
    constexpr int kGraphs = 1000;

    for (int attempt = 0; attempt < kGraphs; ++attempt) {
        std::uniform_int_distribution<int> sizeOf(2, 12);
        const int nodeCount = sizeOf(random);

        Fixture fixture;
        std::vector<SimpleBlock *> blocks;
        for (int index = 0; index < nodeCount; ++index) {
            // The first is always a source, so the graph can be run at all.
            std::unique_ptr<rb::dsp::Block> block;
            SimpleBlock *raw = nullptr;
            if (index == 0) {
                auto source = std::make_unique<ConstantSource>(1.0f);
                raw = source.get();
                block = std::move(source);
            } else {
                auto gain = std::make_unique<GainStage>(1.0f);
                raw = gain.get();
                block = std::move(gain);
            }
            blocks.push_back(raw);
            fixture.add(std::move(block), "b" + std::to_string(index));
        }

        std::vector<std::pair<std::size_t, std::size_t>> wires;
        std::uniform_int_distribution<int> edgeCount(0, nodeCount * 2);
        const int wanted = edgeCount(random);
        for (int edge = 0; edge < wanted; ++edge) {
            std::uniform_int_distribution<int> toIndex(1, nodeCount - 1);
            const int to = toIndex(random);
            std::uniform_int_distribution<int> fromIndex(0, to - 1);
            const int from = fromIndex(random);
            const auto pair = std::pair<std::size_t, std::size_t>{from, to};
            if (std::ranges::find(wires, pair) != wires.end()) {
                continue; // the compiler refuses the same wire twice, rightly
            }
            wires.push_back(pair);
            fixture.wire(static_cast<std::size_t>(from), 0, static_cast<std::size_t>(to), 0);
        }

        std::expected<CompiledGraph, GraphError> graph = fixture.compile();
        ASSERT_TRUE(graph.has_value()) << "attempt " << attempt << ": " << graphErrorName(graph.error());
        fixture.prepare(*graph);

        SimpleBlock::s_tick = 0;
        graph->execute(16);

        for (const auto &[from, to] : wires) {
            ASSERT_GE(blocks[from]->lastRunAt(), 0) << "attempt " << attempt;
            ASSERT_LT(blocks[from]->lastRunAt(), blocks[to]->lastRunAt())
                << "attempt " << attempt << ": " << from << " has to run before " << to;
        }
    }
}

TEST(CompiledGraph, CompilingASixtyBlockGraphIsQuick)
{
    Fixture fixture;
    std::size_t previous = fixture.add(std::make_unique<ConstantSource>(), "b0");
    for (int stage = 1; stage < 60; ++stage) {
        const std::size_t next = fixture.add(std::make_unique<GainStage>(1.0f), "b" + std::to_string(stage));
        fixture.wire(previous, 0, next, 0);
        previous = next;
    }
    fixture.spec().maxBlockFrames = rb::dsp::kMaxBlockFrames;

    // Timed over several runs, because one of them is mostly measurement.
    constexpr int kRuns = 20;
    const auto started = std::chrono::steady_clock::now();
    for (int run = 0; run < kRuns; ++run) {
        const std::expected<CompiledGraph, GraphError> graph = CompiledGraph::compile(fixture.spec());
        ASSERT_TRUE(graph.has_value());
    }
    const auto elapsed = std::chrono::steady_clock::now() - started;
    const double millisecondsEach =
        std::chrono::duration<double, std::milli>(elapsed).count() / static_cast<double>(kRuns);
    EXPECT_LT(millisecondsEach, 2.0) << "compiling took " << millisecondsEach << " ms";
}

// The time a session node asks for while a cycle is running long:
// each block's, at its own place in the spec, and nobody else's.
TEST(CompiledGraph, ATimedCycleSaysWhatEachBlockTook)
{
    Fixture fixture;
    const std::size_t source = fixture.add(std::make_unique<ConstantSource>(0.5f), "source");
    const std::size_t light = fixture.add(std::make_unique<SlowStage>(200), "light");
    const std::size_t heavy = fixture.add(std::make_unique<SlowStage>(3000), "heavy");
    const std::size_t sink = fixture.add(std::make_unique<Sink>(), "sink");
    fixture.wire(source, 0, light, 0);
    fixture.wire(light, 0, heavy, 0);
    fixture.wire(heavy, 0, sink, 0);
    CompiledGraph graph = fixture.compileAndPrepare();

    std::vector<std::uint64_t> nanos(4, 0);
    graph.execute(64, rb::dsp::kEveryLanePresent, nanos);
    EXPECT_GE(nanos[light], 200'000U);
    EXPECT_LT(nanos[light], 2'000'000U) << "the light block was given the heavy one's time";
    EXPECT_GE(nanos[heavy], 3'000'000U);
    EXPECT_LT(nanos[source], 1'000'000U);

    // Added to, not written over, so a few cycles can be summed.
    graph.execute(64, rb::dsp::kEveryLanePresent, nanos);
    EXPECT_GE(nanos[heavy], 6'000'000U);

    // A span too short times the nodes it has room for, and no others.
    std::vector<std::uint64_t> two(2, 0);
    graph.execute(64, rb::dsp::kEveryLanePresent, two);
    EXPECT_GE(two[light], 200'000U);
}
