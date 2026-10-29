// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Join L/R: the left side of one lane and the right side of the
// other, and nothing else; and two ears' chains of different latencies,
// split from one signal and joined again, come out aligned.

#include "../dsp/earblock.h"
#include "allocationtrap.h"
#include "analysis.h"
#include "blocks/joinlr.h"
#include "blocksizeinvariance.h"
#include "graph/compiledgraph.h"
#include "graph/graphspec.h"
#include "offlinerunner.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <optional>

using namespace rb;
using namespace rb::blocks;
using namespace rb::dsp;
using namespace rb::test;

namespace
{

constexpr std::size_t kFrames = 8192;

/// Where the first sample above half of \a signal's peak is.
[[nodiscard]] std::optional<std::size_t> clickAt(const Signal &signal)
{
    const float threshold = 0.5f * peak(signal);
    for (std::size_t frame = 0; frame < signal.size(); ++frame) {
        if (threshold > 0.0f && std::abs(signal[frame]) >= threshold) {
            return frame;
        }
    }
    return std::nullopt;
}

/// Where the sound comes in, standing for an Input Device: a stereo pair
/// the audio system fills, passed on as it arrives. What makes the graph
/// one the compiler will run, and what one signal is fanned out from.
class StereoIn final : public Block
{
public:
    StereoIn()
    {
        m_info.typeId = "test.stereoIn";
        m_info.category = BlockCategory::Sources;
        m_info.inputs = PortSpec::fixed(2, {"in_FL", "in_FR"});
        m_info.outputs = PortSpec::fixed(2, {"out_FL", "out_FR"});
        m_info.inputsFromAudioSystem = true;
    }

    [[nodiscard]] const BlockInfo &info() const noexcept override
    {
        return m_info;
    }
    [[nodiscard]] std::span<const ParamDescriptor> params() const noexcept override
    {
        return {};
    }
    [[nodiscard]] std::expected<void, BlockError> prepare(const PrepareSpec & /*spec*/) override
    {
        return {};
    }
    void process(ProcessContext &context) noexcept RB_NONBLOCKING override
    {
        for (std::uint32_t side = 0; side < 2; ++side) {
            const std::span<float> in = context.input.channel(side);
            const std::span<float> out = context.output.channel(side);
            for (std::uint32_t frame = 0; frame < context.frames && frame < out.size(); ++frame) {
                out[frame] = frame < in.size() ? in[frame] : 0.0f;
            }
        }
    }
    void reset() noexcept RB_NONBLOCKING override { }
    void applyParam(std::uint32_t /*index*/, float /*value*/) noexcept RB_NONBLOCKING override { }
    [[nodiscard]] std::expected<void, BlockError> setParam(std::string_view /*id*/,
                                                           const ParamValue & /*value*/) override
    {
        return std::unexpected(BlockError::UnknownParam);
    }
    [[nodiscard]] ParamValue param(std::string_view /*id*/) const override
    {
        return {};
    }
    [[nodiscard]] ParamValueMap saveState() const override
    {
        return {};
    }
    [[nodiscard]] std::expected<void, BlockError> loadState(const ParamValueMap & /*state*/) override
    {
        return {};
    }

private:
    BlockInfo m_info;
};

} // namespace

TEST(JoinLRTest, IsTwoLanesInAndOneStereoPairOutUnderHearing)
{
    const JoinLR block;
    EXPECT_EQ(block.info().typeId, "hearing.joinlr");
    EXPECT_EQ(block.info().category, BlockCategory::Hearing);
    EXPECT_EQ(block.info().inputs.laneWidth, 2U);
    ASSERT_EQ(block.info().inputs.laneLabels.size(), 2U);
    EXPECT_EQ(block.info().inputs.laneLabels[0], "left");
    EXPECT_EQ(block.info().inputs.laneLabels[1], "right");
    EXPECT_EQ(block.info().outputs.maxCount, 2U);
    EXPECT_EQ(block.info().latencySamples, 0U) << "the compiler lines the two lanes up, not the block";
    EXPECT_TRUE(block.params().empty());
}

TEST(JoinLRTest, PassesTheLeftLanesLeftAndTheRightLanesRightAndNothingElse)
{
    // Four different signals on the four inputs, so that any of them turning
    // up where it should not is seen.
    const MultiSignal input{
        whiteNoise(kFrames, 0.5f, 1), // left lane, FL: kept
        whiteNoise(kFrames, 0.5f, 2), // left lane, FR: dropped
        whiteNoise(kFrames, 0.5f, 3), // right lane, FL: dropped
        whiteNoise(kFrames, 0.5f, 4), // right lane, FR: kept
    };
    JoinLR block;
    BlockRunner runner(block, 4, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const MultiSignal out = runner.run(input, CycleShape::random(32, 1024, 3));
    ASSERT_EQ(out.size(), 2U);
    EXPECT_EQ(out[0], input[0]) << "FL is not the left lane's FL";
    EXPECT_EQ(out[1], input[3]) << "FR is not the right lane's FR";
}

TEST(JoinLRTest, AllocatesNothingAndSoundsTheSameWhateverTheQuantum)
{
    const MultiSignal input{sine(kFrames, 997.0, kTestSampleRate, 0.4f),
                            sine(kFrames, 1499.0, kTestSampleRate, 0.4f),
                            sine(kFrames, 211.0, kTestSampleRate, 0.4f),
                            sine(kFrames, 3001.0, kTestSampleRate, 0.4f)};
    InvarianceOptions options;
    options.inputChannels = 4;
    options.outputChannels = 2;
    const InvarianceReport report = checkBlockSizeInvariance([] { return std::make_unique<JoinLR>(); }, input, options);
    EXPECT_TRUE(report.passed) << report.summary;

    if (NoAllocations::isSupported()) {
        JoinLR block;
        BlockRunner runner(block, 4, 2);
        ASSERT_TRUE(runner.prepare().has_value());
        std::size_t allocations = 0;
        (void)runner.runWatchingForAllocations(input, CycleShape::random(16, 1024, 5), allocations);
        EXPECT_EQ(allocations, 0U);
    }
}

TEST(JoinLRTest, TwoEarsOnLanesOfDifferentLatencyComeOutAligned)
{
    // The side-by-side layout: one stereo signal fanned out to two lanes, a
    // left-ear block 64 samples late on one and a right-ear block 300 late
    // on the other, joined again. **The compiler lines the two up**:
    // a click on both sides comes out of both at 100 + 300.
    StereoIn split;
    OneEarBlock leftEar(64);
    OneEarBlock rightEar(300);
    JoinLR join;
    ASSERT_TRUE(leftEar.setParam("ear", ParamValue::ofEnum("left")).has_value());
    ASSERT_TRUE(rightEar.setParam("ear", ParamValue::ofEnum("right")).has_value());

    graph::GraphSpec spec;
    spec.maxBlockFrames = 2048;
    spec.nodes.push_back(graph::NodeSpec{"split", &split, true});
    spec.nodes.push_back(graph::NodeSpec{"left", &leftEar, true});
    spec.nodes.push_back(graph::NodeSpec{"right", &rightEar, true});
    spec.nodes.push_back(graph::NodeSpec{"join", &join, true});
    for (std::uint32_t side = 0; side < 2; ++side) {
        spec.edges.push_back(graph::EdgeSpec{0, side, 1, side});
        spec.edges.push_back(graph::EdgeSpec{0, side, 2, side});
        spec.edges.push_back(graph::EdgeSpec{1, side, 3, side});
        spec.edges.push_back(graph::EdgeSpec{2, side, 3, 2 + side});
    }
    std::expected<graph::CompiledGraph, graph::GraphError> compiled = graph::CompiledGraph::compile(spec);
    ASSERT_TRUE(compiled.has_value()) << graph::graphErrorName(compiled.error());
    for (std::size_t node = 0; node < spec.nodes.size(); ++node) {
        PrepareSpec prepare;
        prepare.sampleRate = kTestSampleRate;
        prepare.maxBlockFrames = 2048;
        prepare.inputChannels = compiled->portCounts(node).inputs;
        prepare.outputChannels = compiled->portCounts(node).outputs;
        ASSERT_TRUE(spec.nodes[node].block->prepare(prepare).has_value()) << spec.nodes[node].id;
    }

    GraphRunner runner(*compiled);
    runner.feed(0, 0, impulse(kFrames, 100, 1.0f));
    runner.feed(0, 1, impulse(kFrames, 100, 1.0f));
    runner.capture(3, 0);
    runner.capture(3, 1);
    runner.run(kFrames, CycleShape::random(32, 512, 9));

    const std::optional<std::size_t> left = clickAt(runner.captured(3, 0));
    const std::optional<std::size_t> right = clickAt(runner.captured(3, 1));
    ASSERT_TRUE(left.has_value());
    ASSERT_TRUE(right.has_value());
    EXPECT_EQ(*right, 100U + 300U);
    EXPECT_EQ(*left, *right) << "the ears came out " << static_cast<long>(*right) - static_cast<long>(*left)
                             << " samples apart";
}
