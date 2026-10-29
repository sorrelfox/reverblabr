// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Switching a block off and on again: it happens across a fade, it
// leaves the graph's timing alone, and it costs no memory. All of it runs
// offline, because none of it needs an audio server to be true.

#include "allocationtrap.h"
#include "graph/compiledgraph.h"
#include "testblocks.h"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>
#include <vector>

using namespace rb;
using namespace rb::graph;

namespace
{

constexpr double kRate = 48000.0;
constexpr std::uint32_t kFrames = 64;
/// 10 ms, which is what a bypass toggle asks for.
constexpr std::uint32_t kFade = 480;

[[nodiscard]] NodeSpec node(const std::string &id, dsp::Block *block, bool enabled = true, std::uint32_t fade = 0)
{
    NodeSpec spec;
    spec.id = id;
    spec.block = block;
    spec.enabled = enabled;
    spec.crossfadeSamples = fade;
    return spec;
}

[[nodiscard]] EdgeSpec edge(std::size_t from, std::size_t to)
{
    EdgeSpec spec;
    spec.fromNode = from;
    spec.fromPort = 0;
    spec.toNode = to;
    spec.toPort = 0;
    return spec;
}

/// Compiles a spec, preparing its blocks only if asked.
///
/// **A recompile does not prepare a block again.** Preparing clears what the
/// block remembers, which is the very thing a recompile exists to keep, so
/// the second graph of a test has to be compiled with this off — exactly as
/// the runtime does it.
[[nodiscard]] CompiledGraph compile(const GraphSpec &spec, bool prepareBlocks = true)
{
    dsp::PrepareSpec prepare;
    prepare.sampleRate = spec.sampleRate;
    prepare.maxBlockFrames = spec.maxBlockFrames;
    prepare.inputChannels = 1;
    prepare.outputChannels = 1;
    for (const NodeSpec &entry : spec.nodes) {
        if (prepareBlocks) {
            EXPECT_TRUE(entry.block->prepare(prepare).has_value()) << entry.id;
        }
    }
    std::expected<CompiledGraph, GraphError> compiled = CompiledGraph::compile(spec);
    EXPECT_TRUE(compiled.has_value());
    return std::move(*compiled);
}

/// Runs a graph and keeps what left one of its nodes.
[[nodiscard]] std::vector<float>
run(CompiledGraph &graph, std::size_t node, std::uint32_t cycles, std::uint32_t frames = kFrames)
{
    std::vector<float> heard;
    heard.reserve(cycles * frames);
    for (std::uint32_t cycle = 0; cycle < cycles; ++cycle) {
        graph.execute(frames);
        const std::span<const float> out = graph.outputBuffer(node, 0, frames);
        heard.insert(heard.end(), out.begin(), out.end());
    }
    return heard;
}

/// The largest step from one sample to the next.
[[nodiscard]] float largestStep(const std::vector<float> &samples, std::size_t from = 1)
{
    float largest = 0.0f;
    for (std::size_t index = std::max<std::size_t>(from, 1); index < samples.size(); ++index) {
        largest = std::max(largest, std::abs(samples[index] - samples[index - 1]));
    }
    return largest;
}

} // namespace

TEST(Bypass, ABypassedBlockHoldsTheSignalBackJustAsItDid)
{
    // The acceptance criterion: an impulse through a bypassed block has to
    // stay level with one through a path of the same length beside it.
    constexpr std::uint32_t kLatency = 16;
    test::ConstantSource source(0.0f, 1);
    test::LatentGain processed(0.5f, kLatency, 1);
    test::LatentGain parallel(0.5f, kLatency, 1);

    for (const bool enabled : {true, false}) {
        GraphSpec spec;
        spec.sampleRate = kRate;
        spec.nodes = {node("src", &source), node("fx", &processed, enabled), node("dry", &parallel)};
        spec.edges = {edge(0, 1), edge(0, 2)};
        CompiledGraph graph = compile(spec);

        // One sample of signal, then silence, and see when each path hands
        // it on. Both are read from the same cycles, or they would be
        // compared against different moments.
        source.setValue(1.0f);
        graph.execute(1);
        source.setValue(0.0f);

        std::vector<float> through;
        std::vector<float> beside;
        for (std::uint32_t cycle = 0; cycle < 2 * kLatency; ++cycle) {
            graph.execute(1);
            through.push_back(graph.outputBuffer(1, 0, 1).front());
            beside.push_back(graph.outputBuffer(2, 0, 1).front());
        }
        const auto peakAt = [](const std::vector<float> &samples) {
            std::size_t at = 0;
            for (std::size_t index = 0; index < samples.size(); ++index) {
                if (std::abs(samples[index]) > std::abs(samples[at])) {
                    at = index;
                }
            }
            return at;
        };
        EXPECT_EQ(peakAt(through), peakAt(beside))
            << "the impulse arrived at a different time with the block " << (enabled ? "on" : "off");
    }
}

TEST(Bypass, ABypassedBlockStillReportsItsLatencyToTheGraph)
{
    test::ConstantSource source(1.0f, 1);
    test::LatentGain processed(0.5f, 32, 1);

    GraphSpec spec;
    spec.sampleRate = kRate;
    spec.nodes = {node("src", &source), node("fx", &processed, false)};
    spec.edges = {edge(0, 1)};
    const CompiledGraph graph = compile(spec);

    EXPECT_EQ(graph.latencyAt(1), 32U) << "switching a block off must not change what the graph owes";
    EXPECT_EQ(graph.latencySamples(), 32U);
}

TEST(Bypass, ABypassedBlockIsNotRun)
{
    test::ConstantSource source(1.0f, 1);
    test::GainStage gain(0.5f, 1);

    GraphSpec spec;
    spec.sampleRate = kRate;
    spec.nodes = {node("src", &source), node("fx", &gain, false)};
    spec.edges = {edge(0, 1)};
    CompiledGraph graph = compile(spec);

    const std::uint64_t before = gain.cycleCount();
    (void)run(graph, 1, 4);
    EXPECT_EQ(gain.cycleCount(), before) << "a block that is off should cost nothing";

    // And what comes out is the signal itself.
    const std::vector<float> heard = run(graph, 1, 1);
    EXPECT_FLOAT_EQ(heard.front(), 1.0f);
}

TEST(Bypass, SwitchingABlockOffAcrossAFadeLeavesNoClick)
{
    // The acceptance criterion: toggling on a running sine leaves no step
    // from one sample to the next bigger than the sine already has.
    test::ConstantSource source(1.0f, 1);
    test::GainStage gain(0.25f, 1);

    GraphSpec active;
    active.sampleRate = kRate;
    active.nodes = {node("src", &source), node("fx", &gain)};
    active.edges = {edge(0, 1)};
    CompiledGraph running = compile(active);
    const std::vector<float> settled = run(running, 1, 4);
    ASSERT_FLOAT_EQ(settled.back(), 0.25f);

    // Now the same graph with the block switched off, faded across.
    GraphSpec bypassed = active;
    bypassed.nodes[1] = node("fx", &gain, false, kFade);
    CompiledGraph fading = compile(bypassed, false);
    const std::vector<float> across = run(fading, 1, 16);

    // It starts where the processed signal was and ends at the signal
    // itself, and never lurches on the way.
    EXPECT_NEAR(across.front(), 0.25f, 0.01f);
    EXPECT_FLOAT_EQ(across.back(), 1.0f);
    // A constant signal has no step of its own, so every step here is the
    // fade's. Equal power over 480 samples moves fastest at the ends, by
    // about (pi/2)/480 of the difference.
    const float allowed = 0.75f * 2.0f * std::numbers::pi_v<float> / 2.0f / static_cast<float>(kFade);
    EXPECT_LT(largestStep(across), allowed) << "the fade lurched somewhere";
}

TEST(Bypass, SwitchingABlockBackOnFadesTheOtherWay)
{
    test::ConstantSource source(1.0f, 1);
    test::GainStage gain(0.25f, 1);

    GraphSpec bypassed;
    bypassed.sampleRate = kRate;
    bypassed.nodes = {node("src", &source), node("fx", &gain, false)};
    bypassed.edges = {edge(0, 1)};
    CompiledGraph off = compile(bypassed);
    ASSERT_FLOAT_EQ(run(off, 1, 2).back(), 1.0f);

    GraphSpec active = bypassed;
    active.nodes[1] = node("fx", &gain, true, kFade);
    CompiledGraph fading = compile(active, false);
    const std::vector<float> across = run(fading, 1, 16);

    EXPECT_NEAR(across.front(), 1.0f, 0.01f);
    EXPECT_FLOAT_EQ(across.back(), 0.25f);
    const float allowed = 0.75f * 2.0f * std::numbers::pi_v<float> / 2.0f / static_cast<float>(kFade);
    EXPECT_LT(largestStep(across), allowed);
}

TEST(Bypass, AFadeLandsExactlyOnTheSideItWasGoingTo)
{
    test::ConstantSource source(1.0f, 1);
    test::GainStage gain(0.25f, 1);

    GraphSpec spec;
    spec.sampleRate = kRate;
    spec.nodes = {node("src", &source), node("fx", &gain, false, kFade)};
    spec.edges = {edge(0, 1)};
    CompiledGraph graph = compile(spec);

    // Long enough that the fade is well over.
    const std::vector<float> heard = run(graph, 1, 32);
    EXPECT_FLOAT_EQ(heard.back(), 1.0f);
    // And once it is over the block is not run any more.
    const std::uint64_t ran = gain.cycleCount();
    (void)run(graph, 1, 4);
    EXPECT_EQ(gain.cycleCount(), ran);
}

TEST(Bypass, AFadeOnALatencyReportingBlockWaitsForBothSidesToFill)
{
    // A block's first samples are silence, and so are a fresh delay line's.
    // Fading straight away would fade into that silence, which is a dip
    // where there should be none.
    constexpr std::uint32_t kLatency = 64;
    test::ConstantSource source(1.0f, 1);
    test::LatentGain processed(0.5f, kLatency, 1);

    GraphSpec active;
    active.sampleRate = kRate;
    active.nodes = {node("src", &source), node("fx", &processed)};
    active.edges = {edge(0, 1)};
    CompiledGraph running = compile(active);
    (void)run(running, 1, 8); // let the block's own delay fill

    GraphSpec bypassed = active;
    bypassed.nodes[1] = node("fx", &processed, false, kFade);
    CompiledGraph fading = compile(bypassed, false);
    const std::vector<float> across = run(fading, 1, 24);

    EXPECT_NEAR(across.front(), 0.5f, 0.01f) << "it should still be hearing the block";
    EXPECT_FLOAT_EQ(across.back(), 1.0f);
    const float allowed = 0.5f * 2.0f * std::numbers::pi_v<float> / 2.0f / static_cast<float>(kFade);
    EXPECT_LT(largestStep(across), allowed) << "the fade dipped through the silence of a line not yet full";
}

TEST(Bypass, AThousandTogglesTakeNoMemoryAndStayClean)
{
    test::ConstantSource source(1.0f, 1);
    test::GainStage gain(0.25f, 1);

    GraphSpec spec;
    spec.sampleRate = kRate;
    spec.nodes = {node("src", &source), node("fx", &gain, false, kFade)};
    spec.edges = {edge(0, 1)};
    CompiledGraph graph = compile(spec);
    (void)run(graph, 1, 1);

    if (!test::NoAllocations::isSupported()) {
        GTEST_SKIP() << "the sanitizers own the allocator in this build";
    }
    {
        const test::NoAllocations realTime;
        for (int cycle = 0; cycle < 1000; ++cycle) {
            graph.execute(kFrames);
        }
    }
}

TEST(Bypass, SwitchingAMeterOffOnlyStopsItMeasuring)
{
    // A meter is a pass-through in the first place, so switching it off must
    // not change a sample of what goes by; it only stops it looking.
    test::ConstantSource source(0.75f, 1);
    test::RunningTotal meter;

    GraphSpec watching;
    watching.sampleRate = kRate;
    watching.nodes = {node("src", &source), node("meter", &meter)};
    watching.edges = {edge(0, 1)};
    CompiledGraph on = compile(watching);
    const std::vector<float> measured = run(on, 1, 4);
    const double total = meter.total();
    EXPECT_GT(total, 0.0);

    GraphSpec ignored = watching;
    ignored.nodes[1] = node("meter", &meter, false);
    CompiledGraph off = compile(ignored, false);
    const std::vector<float> unmeasured = run(off, 1, 4);

    ASSERT_EQ(unmeasured.size(), measured.size());
    for (std::size_t index = 0; index < measured.size(); ++index) {
        ASSERT_EQ(unmeasured[index], measured[index]) << "sample " << index;
    }
    EXPECT_EQ(meter.total(), total) << "a meter that is off should have stopped measuring";
}

// **What "off" means depends on what the block has on its edges**.
//
// A block with a connector on both is in the middle of a lane, and switching
// it off is the block not being there: the signal carries on exactly as it
// arrived. A block missing one has nothing to pass through. A capture block
// receives nothing from anybody — what reaches its ports comes from the
// audio system — so a source that is off cannot be the source of a sound;
// and an output block has no downstream wire for a pass-through to feed,
// only the hardware. Off means silence for both.
//
// The bug this was: a bypassed capture block copied the capture straight to
// its output, so switching an Application source off left the sound playing
// and the wires still animating, because audio really was flowing.

TEST(Bypass, ASourceThatIsOffIsSilentRatherThanPassingItsCaptureThrough)
{
    test::AudioSystemEnd capture(true);
    test::GainStage tail(1.0f);

    // The audio system's own buffer, which is what a capture block's ports
    // are filled from. Nothing in the session put it there.
    std::vector<float> captured(kFrames, 0.5f);

    GraphSpec running;
    running.sampleRate = kRate;
    running.nodes = {node("src", &capture), node("fx", &tail)};
    running.edges = {edge(0, 1)};
    CompiledGraph on = compile(running);
    ASSERT_TRUE(on.bindExternalInput(0, 0, captured.data()));
    for (const float sample : run(on, 1, 2)) {
        ASSERT_FLOAT_EQ(sample, 0.5f) << "the capture never reached the lane";
    }

    GraphSpec silenced = running;
    silenced.nodes[0] = node("src", &capture, false);
    CompiledGraph off = compile(silenced, false);
    ASSERT_TRUE(off.bindExternalInput(0, 0, captured.data()));
    for (const float sample : run(off, 1, 2)) {
        ASSERT_FLOAT_EQ(sample, 0.0f) << "a source that is off was still heard";
    }
}

TEST(Bypass, AnOutputThatIsOffSendsSilenceToItsDevice)
{
    test::ConstantSource source(0.6f, 1);
    test::AudioSystemEnd device(false);

    GraphSpec playing;
    playing.sampleRate = kRate;
    playing.nodes = {node("src", &source), node("out", &device)};
    playing.edges = {edge(0, 1)};
    CompiledGraph on = compile(playing);
    for (const float sample : run(on, 1, 2)) {
        ASSERT_FLOAT_EQ(sample, 0.6f);
    }

    GraphSpec muted = playing;
    muted.nodes[1] = node("out", &device, false);
    CompiledGraph off = compile(muted, false);
    for (const float sample : run(off, 1, 2)) {
        ASSERT_FLOAT_EQ(sample, 0.0f) << "an output that is off still played to its device";
    }
}

TEST(Bypass, AGeneratorThatIsOffIsSilent)
{
    // A block with no inputs at all makes its own sound, as Wave does.
    // It has nothing to pass through, so off is silence: its pass-through
    // has no input to copy, and its outputs are left quiet. Held here, since
    // a generator is the one kind of block that relies on it.
    test::ConstantSource generator(0.6f, 1);
    test::GainStage tail(1.0f);

    GraphSpec playing;
    playing.sampleRate = kRate;
    playing.nodes = {node("gen", &generator), node("fx", &tail)};
    playing.edges = {edge(0, 1)};
    CompiledGraph on = compile(playing);
    for (const float sample : run(on, 1, 2)) {
        ASSERT_FLOAT_EQ(sample, 0.6f);
    }

    GraphSpec quiet = playing;
    quiet.nodes[0] = node("gen", &generator, false);
    CompiledGraph off = compile(quiet, false);
    for (const float sample : run(off, 1, 2)) {
        ASSERT_FLOAT_EQ(sample, 0.0f) << "a generator that is off was still heard";
    }
}

TEST(Bypass, AnEffectInTheMiddleOfALaneStillPassesItsInputThrough)
{
    // The other half of the rule, beside the two above: the block that has
    // a connector on both edges is bypassed and not silenced, which is what
    // switching an effect off has always meant.
    test::ConstantSource source(0.6f, 1);
    test::GainStage effect(0.25f);

    GraphSpec spec;
    spec.sampleRate = kRate;
    spec.nodes = {node("src", &source), node("fx", &effect, false)};
    spec.edges = {edge(0, 1)};
    CompiledGraph off = compile(spec);
    for (const float sample : run(off, 1, 2)) {
        ASSERT_FLOAT_EQ(sample, 0.6f) << "a bypassed effect changed the signal";
    }
}

TEST(Bypass, SwitchingASourceOffFadesItToSilenceWithoutAClick)
{
    // The cross-fade runs between what the block makes of the signal and
    // what it is being switched to — which, for a block at the end of a
    // lane, is silence. Fading to the capture instead would have been a
    // fade to the very thing that should have stopped.
    test::AudioSystemEnd capture(true);
    std::vector<float> captured(kFrames, 0.5f);

    GraphSpec running;
    running.sampleRate = kRate;
    running.nodes = {node("src", &capture)};
    CompiledGraph on = compile(running);
    ASSERT_TRUE(on.bindExternalInput(0, 0, captured.data()));
    (void)run(on, 0, 2);

    GraphSpec fading = running;
    fading.nodes[0] = node("src", &capture, false, kFade);
    CompiledGraph off = compile(fading, false);
    ASSERT_TRUE(off.bindExternalInput(0, 0, captured.data()));
    const std::vector<float> heard = run(off, 0, 16);

    EXPECT_NEAR(heard.front(), 0.5f, 0.05f) << "the fade started somewhere other than where the sound was";
    EXPECT_NEAR(heard.back(), 0.0f, 1.0e-6f) << "the fade did not end in silence";
    // A tenth of the step a switch with no fade would make: the point of
    // the fade is that nothing clicks on the way down.
    EXPECT_LT(largestStep(heard), 0.05f) << "switching the source off clicked";
}

namespace
{

/// Two lanes of one port each in, one port out, and only the first lane
/// heard while it runs: a ducker whose background is turned all the way
/// down, as far as the compiler can tell.
class TwoLanes final : public test::SimpleBlock
{
public:
    TwoLanes()
        : SimpleBlock("test.twolanes", dsp::BlockCategory::Advanced, 2, 1)
    {
        m_info.inputs = dsp::PortSpec::lanes(1, {"main", "background"});
    }

    void process(dsp::ProcessContext &context) noexcept RB_NONBLOCKING override
    {
        const std::span<float> out = context.output.channel(0);
        const std::span<float> main = context.input.channel(0);
        for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
            out[frame] = main[frame];
        }
    }
};

} // namespace

TEST(Bypass, ABlockWithTwoLanesIsSwitchedOffToBothOfThem)
{
    // A block that takes a voice and the music it ducks is switched
    // off to what the session would be without it — both of them, summed —
    // and not to the first lane alone, which would silence the music the
    // moment the ducker was turned off.
    test::ConstantSource voice(0.25f, 1);
    test::ConstantSource music(0.5f, 1);
    TwoLanes ducker;

    GraphSpec spec;
    spec.sampleRate = kRate;
    spec.nodes = {node("voice", &voice), node("music", &music), node("duck", &ducker, false)};
    EdgeSpec intoBackground = edge(1, 2);
    intoBackground.toPort = 1;
    spec.edges = {edge(0, 2), intoBackground};
    CompiledGraph off = compile(spec);
    for (const float sample : run(off, 2, 2)) {
        ASSERT_FLOAT_EQ(sample, 0.75f) << "a ducker that is off did not pass both of its lanes";
    }

    // And across the fade back on, the dry side it fades from is the sum
    // too, so nothing jumps at the start of it.
    spec.nodes[2] = node("duck", &ducker, true, kFade);
    CompiledGraph fading = compile(spec, false);
    const std::vector<float> heard = run(fading, 2, 2 * kFade / kFrames);
    EXPECT_NEAR(heard.front(), 0.75f, 1e-3f) << "the fade did not start from both lanes";
    EXPECT_NEAR(heard.back(), 0.25f, 1e-6f) << "the fade did not land on the block";
    EXPECT_LT(largestStep(heard), 0.01f);
}
