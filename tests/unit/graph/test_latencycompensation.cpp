// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Latency compensation: paths with different latencies meet with
// their impulses in the same sample — at a hidden sum and at a block with
// more than one input — and stay lined up across a recompile, without a gap
// where the new graph's delay lines would have started empty.

#include "dsp/delayline.h"
#include "graph/compiledgraph.h"
#include "testblocks.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace rb;
using namespace rb::graph;

namespace
{

constexpr double kRate = 48000.0;
constexpr std::uint32_t kFrames = 32;

[[nodiscard]] NodeSpec node(const std::string &id, dsp::Block *block, bool enabled = true)
{
    NodeSpec spec;
    spec.id = id;
    spec.block = block;
    spec.enabled = enabled;
    return spec;
}

[[nodiscard]] EdgeSpec edge(std::size_t from, std::size_t to, std::uint32_t toPort = 0)
{
    EdgeSpec spec;
    spec.fromNode = from;
    spec.fromPort = 0;
    spec.toNode = to;
    spec.toPort = toPort;
    return spec;
}

void prepare(const GraphSpec &spec)
{
    dsp::PrepareSpec prepare;
    prepare.sampleRate = spec.sampleRate;
    prepare.maxBlockFrames = spec.maxBlockFrames;
    prepare.inputChannels = 1;
    prepare.outputChannels = 1;
    for (const NodeSpec &entry : spec.nodes) {
        ASSERT_TRUE(entry.block->prepare(prepare).has_value()) << entry.id;
    }
}

[[nodiscard]] CompiledGraph compile(const GraphSpec &spec)
{
    std::expected<CompiledGraph, GraphError> compiled = CompiledGraph::compile(spec);
    EXPECT_TRUE(compiled.has_value());
    return std::move(*compiled);
}

/// One sample of 1.0 from \a source, then silence, and what \a node puts out
/// over \a cycles cycles.
[[nodiscard]] std::vector<float>
impulseThrough(CompiledGraph &graph, test::ConstantSource &source, std::size_t node, std::uint32_t cycles)
{
    std::vector<float> heard;
    source.setValue(1.0f);
    graph.execute(1);
    const std::span<const float> first = graph.outputBuffer(node, 0, 1);
    heard.push_back(first.front());
    source.setValue(0.0f);
    for (std::uint32_t cycle = 0; cycle < cycles; ++cycle) {
        graph.execute(kFrames);
        const std::span<const float> out = graph.outputBuffer(node, 0, kFrames);
        heard.insert(heard.end(), out.begin(), out.end());
    }
    return heard;
}

/// Where each non-zero sample is.
[[nodiscard]] std::vector<std::size_t> impulsesIn(const std::vector<float> &samples)
{
    std::vector<std::size_t> at;
    for (std::size_t index = 0; index < samples.size(); ++index) {
        if (std::abs(samples[index]) > 1e-6f) {
            at.push_back(index);
        }
    }
    return at;
}

/// Two inputs added into one output: the stand-in for a Mixer, a Ducking
/// block or an Input Switch, as far as the compiler is concerned.
class TwoInputs final : public test::SimpleBlock
{
public:
    TwoInputs()
        : SimpleBlock("test.twoInputs", dsp::BlockCategory::Advanced, 2, 1)
    {
        m_info.inputs = dsp::PortSpec::lanes(1, {"first", "second"});
    }

    void process(dsp::ProcessContext &context) noexcept RB_NONBLOCKING override
    {
        const std::span<float> out = context.output.channel(0);
        const std::span<float> first = context.input.channel(0);
        const std::span<float> second = context.input.channel(1);
        for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
            out[frame] = (first.empty() ? 0.0f : first[frame]) + (second.empty() ? 0.0f : second[frame]);
        }
    }
};

/// Holds its signal back on purpose, as a Sync block does: a delay
/// that is its job, reported as intendedDelaySamples() and not as latency.
class OnPurpose final : public test::SimpleBlock
{
public:
    explicit OnPurpose(std::uint32_t samples)
        : SimpleBlock("test.onPurpose", dsp::BlockCategory::Advanced, 1, 1)
        , m_samples(samples)
    {
    }

    [[nodiscard]] std::expected<void, dsp::BlockError> prepare(const dsp::PrepareSpec &spec) override
    {
        if (const std::expected<void, dsp::BlockError> ready = SimpleBlock::prepare(spec); !ready) {
            return ready;
        }
        m_delay.prepare(1, m_samples);
        m_delay.setDelaySamples(m_samples);
        return {};
    }

    void process(dsp::ProcessContext &context) noexcept RB_NONBLOCKING override
    {
        const std::span<float> out = context.output.channel(0);
        const std::span<float> in = context.input.channel(0);
        for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
            out[frame] = in.empty() ? 0.0f : in[frame];
        }
        m_delay.process(context.output);
    }

    [[nodiscard]] std::uint32_t intendedDelaySamples() const noexcept RB_NONBLOCKING override
    {
        return m_samples;
    }

private:
    dsp::DelayLine m_delay;
    std::uint32_t m_samples = 0;
};

} // namespace

TEST(LatencyCompensation, ADelayAddedOnPurposeIsNeverLinedUpWith)
{
    // A Sync block holding one path back 40 samples, beside a path straight
    // through. Lining the other path up with it would undo it, which is the
    // whole of what somebody put it there for: the impulse comes out twice,
    // at 0 and at 40, and nothing is held back to meet it.
    test::ConstantSource source(0.0f, 1);
    OnPurpose sync(40);
    test::GainStage fast(1.0f, 1);
    test::GainStage out(1.0f, 1);
    GraphSpec spec;
    spec.sampleRate = kRate;
    spec.nodes = {node("src", &source), node("sync", &sync), node("fast", &fast), node("out", &out)};
    spec.edges = {edge(0, 1), edge(0, 2), edge(1, 3), edge(2, 3)};
    prepare(spec);
    CompiledGraph graph = compile(spec);
    EXPECT_EQ(graph.compensationCount(), 0U);
    EXPECT_EQ(graph.latencySamples(), 0U) << "a delay on purpose was counted as latency";
    const std::vector<float> heard = impulseThrough(graph, source, 3, 4);
    EXPECT_EQ(impulsesIn(heard), (std::vector<std::size_t>{0, 40}));
}

TEST(LatencyCompensation, TwoPathsMeetingAtASumArriveInTheSameSample)
{
    // The criterion: one path through a block with 16 samples of latency,
    // one straight through, into the same port. The impulse comes out once,
    // at 16, twice as tall — not at 0 and again at 16.
    test::ConstantSource source(0.0f, 1);
    test::LatentGain slow(1.0f, 16, 1);
    test::GainStage fast(1.0f, 1);
    test::GainStage out(1.0f, 1);

    GraphSpec spec;
    spec.sampleRate = kRate;
    spec.nodes = {node("src", &source), node("slow", &slow), node("fast", &fast), node("out", &out)};
    spec.edges = {edge(0, 1), edge(0, 2), edge(1, 3), edge(2, 3)};
    prepare(spec);
    CompiledGraph graph = compile(spec);

    EXPECT_EQ(graph.sumNodeCount(), 1U);
    EXPECT_EQ(graph.compensationCount(), 1U) << "the fast path was not held back";
    EXPECT_EQ(graph.longestCompensation(), 16U);
    EXPECT_EQ(graph.latencyAt(3), 16U);
    EXPECT_EQ(graph.latencySamples(), 16U);

    const std::vector<float> heard = impulseThrough(graph, source, 3, 4);
    EXPECT_EQ(impulsesIn(heard), (std::vector<std::size_t>{16})) << "the two paths arrived apart";
    EXPECT_FLOAT_EQ(heard[16], 2.0f);
}

TEST(LatencyCompensation, TwoLanesIntoOneBlockArriveInTheSameSample)
{
    // A merge point that is a block, not a sum: its two inputs, one behind a
    // latency of 24 and one of 5, lined up at 24.
    test::ConstantSource source(0.0f, 1);
    test::LatentGain longer(1.0f, 24, 1);
    test::LatentGain shorter(1.0f, 5, 1);
    TwoInputs merge;

    GraphSpec spec;
    spec.sampleRate = kRate;
    spec.nodes = {node("src", &source), node("long", &longer), node("short", &shorter), node("merge", &merge)};
    spec.edges = {edge(0, 1), edge(0, 2), edge(1, 3, 0), edge(2, 3, 1)};
    prepare(spec);
    CompiledGraph graph = compile(spec);
    EXPECT_EQ(graph.compensationCount(), 1U);
    EXPECT_EQ(graph.longestCompensation(), 19U);

    const std::vector<float> heard = impulseThrough(graph, source, 3, 4);
    EXPECT_EQ(impulsesIn(heard), (std::vector<std::size_t>{24}));
    EXPECT_FLOAT_EQ(heard[24], 2.0f);
}

TEST(LatencyCompensation, PathsOfEqualLatencyAreLeftAlone)
{
    // A stereo pair from one source, or two paths through the same kind of
    // block, needs nothing, and gets no delay line.
    test::ConstantSource source(0.0f, 1);
    test::LatentGain left(1.0f, 8, 1);
    test::LatentGain right(1.0f, 8, 1);
    TwoInputs merge;
    GraphSpec spec;
    spec.sampleRate = kRate;
    spec.nodes = {node("src", &source), node("l", &left), node("r", &right), node("merge", &merge)};
    spec.edges = {edge(0, 1), edge(0, 2), edge(1, 3, 0), edge(2, 3, 1)};
    prepare(spec);
    const CompiledGraph graph = compile(spec);
    EXPECT_EQ(graph.compensationCount(), 0U);
}

TEST(LatencyCompensation, ARecompileKeepsThePathsLinedUpAsBlocksComeAndGo)
{
    // The criterion: a block added to the short path, then the long path's
    // block taken out, and the impulse still arrives once each time.
    test::ConstantSource source(0.0f, 1);
    test::LatentGain slow(1.0f, 16, 1);
    test::LatentGain added(1.0f, 6, 1);
    test::GainStage fast(1.0f, 1);
    test::GainStage out(1.0f, 1);

    GraphSpec spec;
    spec.sampleRate = kRate;
    spec.nodes = {
        node("src", &source), node("slow", &slow), node("fast", &fast), node("out", &out), node("added", &added)};
    spec.edges = {edge(0, 1), edge(0, 2), edge(1, 3), edge(2, 4), edge(4, 3)};
    prepare(spec);
    CompiledGraph withAdded = compile(spec);
    EXPECT_EQ(withAdded.longestCompensation(), 10U);
    const std::vector<float> first = impulseThrough(withAdded, source, 3, 4);
    EXPECT_EQ(impulsesIn(first), (std::vector<std::size_t>{16}));

    // The slow block goes: now the path with the added block is the long one.
    spec.nodes = {node("src", &source), node("fast", &fast), node("out", &out), node("added", &added)};
    spec.edges = {edge(0, 1), edge(0, 3), edge(1, 2), edge(3, 2)};
    CompiledGraph withoutSlow = compile(spec);
    withoutSlow.carryOverFrom(withAdded);
    EXPECT_EQ(withoutSlow.longestCompensation(), 6U);
    const std::vector<float> second = impulseThrough(withoutSlow, source, 2, 4);
    EXPECT_EQ(impulsesIn(second), (std::vector<std::size_t>{6}));
}

TEST(LatencyCompensation, ABypassedBlockKeepsItsPlaceInTheLineUp)
{
    // Switched off, a latent block still holds its path back by as long as
    // it would have, so the paths meeting after it stay lined up.
    test::ConstantSource source(0.0f, 1);
    test::LatentGain slow(1.0f, 16, 1);
    test::GainStage fast(1.0f, 1);
    test::GainStage out(1.0f, 1);
    GraphSpec spec;
    spec.sampleRate = kRate;
    spec.nodes = {node("src", &source), node("slow", &slow, false), node("fast", &fast), node("out", &out)};
    spec.edges = {edge(0, 1), edge(0, 2), edge(1, 3), edge(2, 3)};
    prepare(spec);
    CompiledGraph graph = compile(spec);
    const std::vector<float> heard = impulseThrough(graph, source, 3, 4);
    EXPECT_EQ(impulsesIn(heard), (std::vector<std::size_t>{16}));
}

TEST(LatencyCompensation, ARecompileDoesNotPutAGapIntoAHeldBackPath)
{
    // A steady level on both paths, and a recompile that changes nothing
    // about them — an edit elsewhere. The new graph's delay lines take over
    // what the old ones held, so the level carries on without the 16
    // samples of silence a fresh line would have put in.
    test::ConstantSource source(0.25f, 1);
    test::LatentGain slow(1.0f, 16, 1);
    test::GainStage fast(1.0f, 1);
    test::GainStage out(1.0f, 1);
    test::ConstantSource elsewhere(0.0f, 1);
    GraphSpec spec;
    spec.sampleRate = kRate;
    spec.nodes = {node("src", &source), node("slow", &slow, false), node("fast", &fast), node("out", &out)};
    spec.edges = {edge(0, 1), edge(0, 2), edge(1, 3), edge(2, 3)};
    prepare(spec);
    CompiledGraph before = compile(spec);
    for (int cycle = 0; cycle < 4; ++cycle) {
        before.execute(kFrames);
    }

    spec.nodes.push_back(node("elsewhere", &elsewhere));
    CompiledGraph after = compile(spec);
    EXPECT_EQ(after.carryOverFrom(before), 2U) << "the compensation line and the bypass line";
    std::vector<float> heard;
    for (int cycle = 0; cycle < 2; ++cycle) {
        after.execute(kFrames);
        const std::span<const float> samples = after.outputBuffer(3, 0, kFrames);
        heard.insert(heard.end(), samples.begin(), samples.end());
    }
    for (std::size_t index = 0; index < heard.size(); ++index) {
        ASSERT_FLOAT_EQ(heard[index], 0.5f) << "a gap at sample " << index << " after the recompile";
    }
}
