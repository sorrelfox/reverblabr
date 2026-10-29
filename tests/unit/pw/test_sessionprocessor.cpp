// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What one audio cycle of a session node does, with no PipeWire anywhere
// near it. The node itself is tested against a real server in tests/pw; this
// is the part that has to obey the real-time rules, so it is also where the
// allocation trap and RealtimeSanitizer look.

#include "../dsp/gainblock.h"
#include "allocationtrap.h"
#include "blocks/recorder.h"
#include "graph/graphspec.h"
#include "pw/sessionprocessor.h"
#include "rt/audiotap.h"
#include "rt/meter.h"
#include "sessionblocks.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <memory>
#include <vector>

using namespace rb;
using namespace rb::pw;

namespace
{

constexpr std::uint32_t kFrames = 128;
constexpr std::uint32_t kRate = 48000;
constexpr std::uint32_t kChannels = 2;

/// A session of two blocks: a source that passes the node's input ports on,
/// and a gain whose outputs are the node's output ports. It is the smallest
/// graph shaped like a real one.
class SessionProcessorTest : public ::testing::Test
{
protected:
    test::PassThroughSource source{kChannels};
    test::GainBlock gain;
    std::array<std::vector<float>, kChannels> inputStorage;
    std::array<std::vector<float>, kChannels> outputStorage;
    /// One meter per block, as the runtime gives it: the source's and the
    /// gain's.
    std::array<rt::BlockMeter, 2> meters;
    SessionProcessor processor;

    void SetUp() override
    {
        for (std::uint32_t channel = 0; channel < kChannels; ++channel) {
            inputStorage[channel].assign(dsp::kMaxBlockFrames, 0.0f);
            outputStorage[channel].assign(dsp::kMaxBlockFrames, 0.0f);
        }
    }

    /// Both input lanes in one lane by default, which is how one source
    /// block's stereo pair arrives.
    /// \a effect stands in for the gain, when a test gives one.
    [[nodiscard]] std::unique_ptr<LiveGraph>
    makeLive(double sampleRate = kRate, bool separateLanes = false, bool metered = true, dsp::Block *effect = nullptr)
    {
        dsp::Block &second = effect != nullptr ? *effect : static_cast<dsp::Block &>(gain);
        graph::GraphSpec spec;
        spec.sampleRate = sampleRate;
        graph::NodeSpec sourceNode;
        sourceNode.id = "src";
        sourceNode.block = &source;
        sourceNode.meter = metered ? &meters[0] : nullptr;
        graph::NodeSpec gainNode;
        gainNode.id = "gain";
        gainNode.block = &second;
        gainNode.meter = metered ? &meters[1] : nullptr;
        for (rt::BlockMeter &meter : meters) {
            meter.setChannelCount(kChannels);
        }
        spec.nodes.push_back(sourceNode);
        spec.nodes.push_back(gainNode);
        for (std::uint32_t channel = 0; channel < kChannels; ++channel) {
            graph::EdgeSpec edge;
            edge.fromNode = 0;
            edge.fromPort = channel;
            edge.toNode = 1;
            edge.toPort = channel;
            spec.edges.push_back(edge);
        }

        dsp::PrepareSpec prepare;
        prepare.sampleRate = sampleRate;
        prepare.maxBlockFrames = dsp::kMaxBlockFrames;
        prepare.inputChannels = kChannels;
        prepare.outputChannels = kChannels;
        EXPECT_TRUE(source.prepare(prepare).has_value());
        // The effect is told where to publish, as the runtime tells it.
        dsp::PrepareSpec effectPrepare = prepare;
        effectPrepare.meter = metered ? &meters[1] : nullptr;
        EXPECT_TRUE(second.prepare(effectPrepare).has_value());

        std::expected<graph::CompiledGraph, graph::GraphError> compiled = graph::CompiledGraph::compile(spec);
        EXPECT_TRUE(compiled.has_value());
        if (!compiled) {
            return nullptr;
        }

        auto live = std::make_unique<LiveGraph>();
        live->graph = std::make_unique<graph::CompiledGraph>(std::move(*compiled));
        live->sampleRate = sampleRate;
        live->paramTargets = {&source, &second};
        live->blockNanos.assign(live->graph->nodeCount(), 0);
        live->laneCount = separateLanes ? kChannels : 1;
        for (std::uint32_t channel = 0; channel < kChannels; ++channel) {
            SessionPort in;
            in.graphNode = 0;
            in.graphPort = channel;
            in.lane = separateLanes ? channel : 0;
            in.buffer = inputStorage[channel].data();
            live->inputs.push_back(in);

            SessionPort out;
            out.graphNode = 1;
            out.graphPort = channel;
            out.lane = separateLanes ? channel : 0;
            out.buffer = outputStorage[channel].data();
            live->outputs.push_back(out);
        }
        return live;
    }

    void putLive(std::unique_ptr<LiveGraph> live)
    {
        const std::unique_ptr<LiveGraph> retired(processor.exchange(live.release()));
    }

    /// The cycle that follows the one before it: the position moved on by a
    /// cycle's frames and the graph's own cycle counter by one, which is what
    /// a server that is keeping up looks like. A test that wants a missed
    /// cycle takes this and moves one of them further.
    [[nodiscard]] CycleInfo nextCycle(std::uint32_t frames = kFrames, std::uint32_t rate = kRate) const
    {
        CycleInfo cycle;
        cycle.frames = frames;
        cycle.sampleRate = rate;
        cycle.position = m_position;
        cycle.clockId = m_clockId;
        cycle.graphCycle = m_graphCycle;
        return cycle;
    }

    /// Runs \a cycle and leaves the graph where that cycle left it, so the
    /// next one follows on from whatever the test did to this one.
    void runCycle(const CycleInfo &cycle)
    {
        processor.process(cycle);
        m_position = cycle.position + cycle.frames;
        m_clockId = cycle.clockId;
        m_graphCycle = cycle.graphCycle + 1;
    }

    /// One cycle that follows the one before it, so nothing looks like an xrun.
    void runCycle(std::uint32_t frames = kFrames, std::uint32_t rate = kRate)
    {
        runCycle(nextCycle(frames, rate));
    }

    void fillInputs(float value)
    {
        for (std::vector<float> &channel : inputStorage) {
            std::ranges::fill(channel, value);
        }
    }

    [[nodiscard]] float outputPeak(std::uint32_t frames = kFrames) const
    {
        float peak = 0.0f;
        for (const std::vector<float> &channel : outputStorage) {
            for (std::uint32_t frame = 0; frame < frames; ++frame) {
                peak = std::max(peak, std::abs(channel[frame]));
            }
        }
        return peak;
    }

private:
    std::uint64_t m_position = 0;
    std::uint32_t m_clockId = 0;
    std::uint32_t m_graphCycle = 0;
};

} // namespace

TEST_F(SessionProcessorTest, ACycleWithNoGraphIsStillACycle)
{
    runCycle();

    const SessionTelemetry telemetry = processor.telemetry();
    EXPECT_EQ(telemetry.cycles, 1U);
    EXPECT_EQ(telemetry.hasGraph, 0U);
    EXPECT_EQ(telemetry.xruns, 0U);
    EXPECT_EQ(telemetry.frames, kFrames);
}

TEST_F(SessionProcessorTest, AudioArrivesAtTheOutputPortsUnchanged)
{
    putLive(makeLive());
    for (std::uint32_t channel = 0; channel < kChannels; ++channel) {
        for (std::uint32_t frame = 0; frame < kFrames; ++frame) {
            inputStorage[channel][frame] = static_cast<float>(frame) / static_cast<float>(kFrames);
        }
    }

    runCycle();

    // The gain starts at its default of 0 dB and is not asked to move, so
    // the ramp has to come out sample for sample.
    for (std::uint32_t channel = 0; channel < kChannels; ++channel) {
        for (std::uint32_t frame = 0; frame < kFrames; ++frame) {
            ASSERT_FLOAT_EQ(outputStorage[channel][frame], inputStorage[channel][frame])
                << "channel " << channel << ", frame " << frame;
        }
    }
    EXPECT_EQ(processor.telemetry().hasGraph, 1U);
}

TEST_F(SessionProcessorTest, AnUnlinkedInputPortReadsAsSilence)
{
    std::unique_ptr<LiveGraph> live = makeLive();
    fillInputs(0.5f);
    // What PipeWire gives for a port nothing is linked to.
    for (SessionPort &port : live->inputs) {
        port.buffer = nullptr;
    }
    for (std::vector<float> &channel : outputStorage) {
        std::ranges::fill(channel, 7.0f); // something that is plainly not audio
    }
    putLive(std::move(live));

    runCycle();

    EXPECT_FLOAT_EQ(outputPeak(), 0.0f);
}

TEST_F(SessionProcessorTest, AnUnlinkedOutputPortIsLeftAlone)
{
    std::unique_ptr<LiveGraph> live = makeLive();
    live->outputs[1].buffer = nullptr;
    fillInputs(0.5f);
    std::ranges::fill(outputStorage[1], 7.0f);
    putLive(std::move(live));

    runCycle();

    EXPECT_FLOAT_EQ(outputStorage[0][0], 0.5f);
    EXPECT_FLOAT_EQ(outputStorage[1][0], 7.0f) << "a port with no buffer must not be written through";
}

TEST_F(SessionProcessorTest, AnOutputPortIsWrittenEvenWhenTheGraphHasNothingForIt)
{
    std::unique_ptr<LiveGraph> live = makeLive();
    // A port pointing at a block port that does not exist: the graph has
    // nothing to say through it, and it must still not carry what was in the
    // memory before.
    live->outputs[1].graphPort = 9;
    std::ranges::fill(outputStorage[1], 7.0f);
    fillInputs(0.5f);
    putLive(std::move(live));

    runCycle();

    EXPECT_FLOAT_EQ(outputStorage[1][0], 0.0f);
}

TEST_F(SessionProcessorTest, AGainChangeQueuedForTheAudioThreadIsHeard)
{
    putLive(makeLive());
    fillInputs(1.0f);

    ParamChange change;
    change.target = 1; // the gain block
    change.index = test::GainBlock::GainDb;
    change.value = -6.0f;
    EXPECT_TRUE(processor.queueParam(change));

    // The block smooths the change over 20 ms, so give it longer than that.
    for (int cycle = 0; cycle < 20; ++cycle) {
        runCycle();
    }

    EXPECT_NEAR(outputStorage[0][kFrames - 1], 0.5011872f, 1e-4f) << "-6 dB";
}

TEST_F(SessionProcessorTest, AParameterForABlockThisGraphDoesNotHaveIsDropped)
{
    putLive(makeLive());
    fillInputs(1.0f);

    ParamChange change;
    change.target = 17; // no such slot
    change.index = test::GainBlock::GainDb;
    change.value = -60.0f;
    EXPECT_TRUE(processor.queueParam(change));

    for (int cycle = 0; cycle < 20; ++cycle) {
        runCycle();
    }

    EXPECT_NEAR(outputStorage[0][kFrames - 1], 1.0f, 1e-5f);
}

TEST_F(SessionProcessorTest, ACycleAtARateTheGraphIsNotPreparedForIsSilent)
{
    putLive(makeLive(kRate));
    fillInputs(1.0f);

    runCycle(kFrames, 44100);

    EXPECT_FLOAT_EQ(outputPeak(), 0.0f);
    EXPECT_EQ(processor.rateChangeWanted(), 44100U);
    EXPECT_EQ(processor.telemetry().rateMismatches, 1U);

    // A graph prepared for the new rate ends it.
    putLive(makeLive(44100));
    runCycle(kFrames, 44100);
    EXPECT_EQ(processor.rateChangeWanted(), 0U);
    EXPECT_FLOAT_EQ(outputStorage[0][0], 1.0f);
}

TEST_F(SessionProcessorTest, ACycleThatDoesNotFollowTheLastOneIsAnXrun)
{
    putLive(makeLive());
    runCycle();
    runCycle();
    EXPECT_EQ(processor.telemetry().xruns, 0U);

    CycleInfo jumped = nextCycle();
    jumped.position = 4096; // the graph moved on without us
    runCycle(jumped);

    EXPECT_EQ(processor.telemetry().xruns, 1U);
}

TEST_F(SessionProcessorTest, TheGraphCountingACycleThisNodeDidNotRunIsAnXrun)
{
    // PipeWire counts the graph's cycles for us. Two steps of its counter
    // against one of ours is a cycle the graph ran while this node was
    // somewhere else, which is the plainest evidence of a missed cycle the
    // server offers — and the one that was thrown away by testing the
    // counter for a change rather than for its step.
    putLive(makeLive());
    runCycle();
    runCycle();

    CycleInfo skipped = nextCycle();
    ++skipped.graphCycle;
    skipped.position += kFrames;
    runCycle(skipped);

    EXPECT_EQ(processor.telemetry().xruns, 1U);
    EXPECT_EQ(processor.telemetry().lostCycles, 1U);
}

TEST_F(SessionProcessorTest, AStallIsOneOccasionAndAsManyCyclesAsItCost)
{
    // A node that stops for a while is late once and costs every cycle it
    // was away for, so the two figures answer two different questions.
    putLive(makeLive());
    runCycle();

    CycleInfo late = nextCycle();
    late.graphCycle += 7;
    late.position += 7 * kFrames;
    runCycle(late);

    EXPECT_EQ(processor.telemetry().xruns, 1U);
    EXPECT_EQ(processor.telemetry().lostCycles, 7U);
}

TEST_F(SessionProcessorTest, ANodeThatMissesOneCycleInEightSaysSo)
{
    // **The counter has to move when cycles are genuinely lost.** Nothing
    // noticed it sitting at zero through anything, which is why it sat there
    // for as long as it did: a node that skips a cycle in eight is
    // audibly ruined and was reporting a clean run.
    putLive(makeLive());
    constexpr int kCycles = 800;
    int skipped = 0;
    for (int cycle = 0; cycle < kCycles; ++cycle) {
        CycleInfo next = nextCycle();
        if (cycle % 8 == 7) {
            // The graph ran this one; this node did not see it.
            ++skipped;
            next.graphCycle += 1;
            next.position += kFrames;
        }
        runCycle(next);
    }

    EXPECT_EQ(skipped, kCycles / 8);
    EXPECT_EQ(processor.telemetry().xruns, static_cast<std::uint64_t>(skipped));
    EXPECT_EQ(processor.telemetry().lostCycles, static_cast<std::uint64_t>(skipped));
}

TEST_F(SessionProcessorTest, ASustainedHealthyRunCountsNothingAtAll)
{
    putLive(makeLive());
    for (int cycle = 0; cycle < 5000; ++cycle) {
        runCycle();
    }

    const SessionTelemetry telemetry = processor.telemetry();
    EXPECT_EQ(telemetry.cycles, 5000U);
    EXPECT_EQ(telemetry.xruns, 0U) << "a graph that kept up was counted as losing cycles";
    EXPECT_EQ(telemetry.lostCycles, 0U);
    EXPECT_EQ(telemetry.clockChanges, 0U) << "a counter going up once a cycle is not a change of clock";
}

// An output the node found still unread is counted, however many a
// cycle has, and a cycle with none adds nothing.
TEST_F(SessionProcessorTest, OutputsFoundStillUnreadAreCounted)
{
    putLive(makeLive());
    CycleInfo cycle = nextCycle();
    cycle.unreadOutputs = 2;
    runCycle(cycle);
    runCycle(nextCycle());
    cycle = nextCycle();
    cycle.unreadOutputs = 1;
    runCycle(cycle);
    EXPECT_EQ(processor.telemetry().unreadOutputs, 3U);
    EXPECT_EQ(processor.telemetry().xruns, 0U) << "an unread output is not a missed cycle";
}

TEST_F(SessionProcessorTest, TheGraphCycleCounterWrappingIsNotAnXrun)
{
    // It is 32 bits wide, so a session left running long enough goes past
    // the end of it. The step across that is still one cycle.
    putLive(makeLive());
    CycleInfo last = nextCycle();
    last.graphCycle = 0xFFFFFFFFU;
    runCycle(last);
    runCycle(); // the counter is back at 0
    runCycle();

    EXPECT_EQ(processor.telemetry().xruns, 0U);
    EXPECT_EQ(processor.telemetry().clockChanges, 0U);
}

TEST_F(SessionProcessorTest, PipeWiresOwnXrunFlagCountsToo)
{
    putLive(makeLive());
    runCycle();

    CycleInfo flagged = nextCycle();
    flagged.xrunFlag = true;
    runCycle(flagged);

    EXPECT_EQ(processor.telemetry().xruns, 1U);
}

TEST_F(SessionProcessorTest, BeingHandedToAnotherDriverIsNotAnXrun)
{
    // The position and the cycle counter are both the driver's, so a node
    // moved from one driver to another sees them both jump without anybody
    // having missed a cycle. The clock id is what says so.
    putLive(makeLive());
    runCycle();
    runCycle();

    CycleInfo moved = nextCycle();
    moved.position = 987654; // the new driver's idea of where the graph is
    moved.graphCycle = 500000; // and its own count of the cycles it has run
    moved.clockId = 7;
    runCycle(moved);

    EXPECT_EQ(processor.telemetry().xruns, 0U);
    EXPECT_EQ(processor.telemetry().lostCycles, 0U);
    EXPECT_EQ(processor.telemetry().clockChanges, 1U);
}

TEST_F(SessionProcessorTest, AGraphThatWasRestartedIsNotAnXrun)
{
    // A restarted graph counts its cycles from the beginning again, so its
    // counter goes backwards. Going backwards is how a restart is told from
    // a node that was late, which only ever shows as a step forward.
    putLive(makeLive());
    for (int cycle = 0; cycle < 100; ++cycle) {
        runCycle();
    }

    CycleInfo restarted = nextCycle();
    restarted.position = 0;
    restarted.graphCycle = 0;
    runCycle(restarted);

    EXPECT_EQ(processor.telemetry().xruns, 0U);
    EXPECT_EQ(processor.telemetry().lostCycles, 0U);
    EXPECT_EQ(processor.telemetry().clockChanges, 1U);
}

TEST_F(SessionProcessorTest, ADiscontinuityInTheDriversClockIsNotAnXrun)
{
    putLive(makeLive());
    runCycle();

    // A driver whose timestamps jumped says so, and says it is not an xrun.
    CycleInfo jumped = nextCycle();
    jumped.position = 500000;
    jumped.discontinuity = true;
    runCycle(jumped);

    EXPECT_EQ(processor.telemetry().xruns, 0U);
}

TEST_F(SessionProcessorTest, TheDriversOwnXrunEstimateCounts)
{
    putLive(makeLive());
    runCycle();

    // The position followed on, so only the driver's own figure says a cycle
    // was lost — which is the case the position alone cannot see.
    CycleInfo late = nextCycle();
    late.xrunDuration = 512;
    runCycle(late);

    EXPECT_EQ(processor.telemetry().xruns, 1U);

    // The figure is accumulated, so the same value again is the same xrun.
    CycleInfo after = nextCycle();
    after.xrunDuration = 512;
    runCycle(after);

    EXPECT_EQ(processor.telemetry().xruns, 1U);
}

TEST_F(SessionProcessorTest, AHandoverDoesNotHideAMissedCycle)
{
    // A new graph does not move the graph position: the position belongs to
    // the driver, and a swap that only exchanges pointers cannot disturb it.
    // So a jump across a handover is still a cycle somebody lost.
    putLive(makeLive());
    runCycle();
    runCycle();

    putLive(makeLive());
    CycleInfo jumped = nextCycle();
    jumped.position = 999999;
    runCycle(jumped);

    EXPECT_EQ(processor.telemetry().xruns, 1U);

    // And the cycles either side of it are counted as one run.
    EXPECT_EQ(processor.telemetry().cycles, 3U);
}

TEST_F(SessionProcessorTest, TheDspLoadIsAFractionOfTheCycleItHad)
{
    putLive(makeLive());
    fillInputs(0.25f);

    runCycle();

    const SessionTelemetry telemetry = processor.telemetry();
    EXPECT_GT(telemetry.lastLoad, 0.0f) << "a cycle that took no time at all was not measured";
    EXPECT_LT(telemetry.lastLoad, 1.0f) << "copying 256 samples took longer than 2.7 ms";
    EXPECT_GE(telemetry.peakLoad, telemetry.lastLoad);
}

TEST_F(SessionProcessorTest, ThePeakLoadStartsAgainWhenItIsRead)
{
    putLive(makeLive());
    for (int cycle = 0; cycle < 5; ++cycle) {
        runCycle();
    }
    const float peak = processor.telemetry().peakLoad;
    ASSERT_GT(peak, 0.0f);

    processor.resetPeakLoad();
    runCycle();

    EXPECT_EQ(processor.telemetry().peakLoad, processor.telemetry().lastLoad);
}

TEST_F(SessionProcessorTest, SourcePresentFollowsTheLanesThatHaveBuffers)
{
    std::unique_ptr<LiveGraph> live = makeLive(kRate, true);
    live->inputs[1].buffer = nullptr; // the second lane is not linked
    putLive(std::move(live));

    runCycle();

    EXPECT_EQ(processor.telemetry().sourcePresent, 0b01U);
}

TEST_F(SessionProcessorTest, AnExchangeHandsTheOldGraphBack)
{
    std::unique_ptr<LiveGraph> first = makeLive();
    LiveGraph *firstRaw = first.get();
    EXPECT_EQ(processor.exchange(first.release()), nullptr);

    std::unique_ptr<LiveGraph> second = makeLive();
    const std::unique_ptr<LiveGraph> retired(processor.exchange(second.release()));

    EXPECT_EQ(retired.get(), firstRaw) << "the retired graph is the caller's to free";
    EXPECT_NE(processor.live(), nullptr);
}

TEST_F(SessionProcessorTest, ACycleTakesNoMemory)
{
    if (!test::NoAllocations::isSupported()) {
        GTEST_SKIP() << "the sanitizers own the allocator in this build";
    }
    putLive(makeLive());
    fillInputs(0.5f);
    // Once through first: the first cycle of a graph is no different, but a
    // test that armed the trap before any code had run would be measuring
    // the first call of everything.
    runCycle();

    ParamChange change;
    change.target = 1;
    change.index = test::GainBlock::GainDb;
    change.value = -3.0f;
    EXPECT_TRUE(processor.queueParam(change));

    {
        const test::NoAllocations realTime;
        for (int cycle = 0; cycle < 100; ++cycle) {
            runCycle();
        }
    }
}

TEST_F(SessionProcessorTest, AnAnalysedBlocksOutputIsCopiedOutAndNoOtherIs)
{
    // The live spectrum's copy of a block's output. Only the block
    // somebody is looking at is copied, only while they are, and what is
    // copied is what the block put out, both channels, frame for frame.
    putLive(makeLive());
    for (std::uint32_t channel = 0; channel < kChannels; ++channel) {
        for (std::uint32_t frame = 0; frame < kFrames; ++frame) {
            inputStorage[channel][frame] = static_cast<float>(frame + channel * 1000) / 4096.0f;
        }
    }
    runCycle();
    EXPECT_EQ(meters[1].analysis(), nullptr) << "a ring was made before anybody asked for one";

    meters[1].setAnalysing(true);
    ASSERT_NE(meters[1].analysis(), nullptr);
    runCycle();
    runCycle();
    rt::AudioTap &tap = *meters[1].analysis();
    ASSERT_EQ(tap.readableFrames(), 2U * kFrames);
    std::vector<float> copied(2U * kFrames * kChannels);
    ASSERT_EQ(tap.read(copied, 2U * kFrames), 2U * kFrames);
    for (std::uint32_t frame = 0; frame < kFrames; ++frame) {
        for (std::uint32_t channel = 0; channel < kChannels; ++channel) {
            ASSERT_FLOAT_EQ(copied[frame * kChannels + channel], outputStorage[channel][frame]) << frame;
        }
    }
    EXPECT_EQ(meters[0].analysis(), nullptr) << "a block nobody is looking at was copied";

    // Switched off, nothing more arrives; switched on again, the same ring.
    rt::AudioTap *const ring = meters[1].analysis();
    meters[1].setAnalysing(false);
    runCycle();
    EXPECT_EQ(tap.readableFrames(), 0U);
    meters[1].setAnalysing(true);
    EXPECT_EQ(meters[1].analysis(), ring);
}

TEST_F(SessionProcessorTest, AnAnalysedBlocksInputIsCopiedOutBeforeItRuns)
{
    // The sound going into the block, beside the sound coming out of
    // it, both channels frame for frame, from the same cycles.
    test::FixedGainEffect half(0.5f, kChannels);
    putLive(makeLive(kRate, false, true, &half));
    for (std::uint32_t channel = 0; channel < kChannels; ++channel) {
        for (std::uint32_t frame = 0; frame < kFrames; ++frame) {
            inputStorage[channel][frame] = static_cast<float>(frame + channel * 1000) / 4096.0f;
        }
    }
    meters[1].setAnalysing(true);
    ASSERT_NE(meters[1].inputAnalysis(), nullptr);
    runCycle();
    rt::AudioTap &before = *meters[1].inputAnalysis();
    rt::AudioTap &after = *meters[1].analysis();
    ASSERT_EQ(before.readableFrames(), kFrames);
    ASSERT_EQ(after.readableFrames(), kFrames);
    std::vector<float> in(kFrames * kChannels);
    std::vector<float> out(kFrames * kChannels);
    ASSERT_EQ(before.read(in, kFrames), kFrames);
    ASSERT_EQ(after.read(out, kFrames), kFrames);
    for (std::uint32_t frame = 0; frame < kFrames; ++frame) {
        for (std::uint32_t channel = 0; channel < kChannels; ++channel) {
            ASSERT_FLOAT_EQ(in[frame * kChannels + channel], inputStorage[channel][frame]) << frame;
            ASSERT_FLOAT_EQ(out[frame * kChannels + channel], 0.5f * inputStorage[channel][frame]) << frame;
        }
    }

    // **Nothing before the block while nobody looks**, and nothing for the
    // source, which is not being looked at.
    meters[1].setAnalysing(false);
    runCycle();
    EXPECT_EQ(before.readableFrames(), 0U);
    EXPECT_EQ(meters[0].inputAnalysis(), nullptr);
}

TEST_F(SessionProcessorTest, ABlocksBandValuesAreTakenOnlyWhileItIsAnalysed)
{
    // A block that publishes 64 band values every cycle. Nothing is
    // stored while nobody looks; while somebody does, the whole cycle's.
    test::BandWriterEffect bands;
    putLive(makeLive(kRate, false, true, &bands));
    fillInputs(0.5f);
    runCycle();
    runCycle();
    EXPECT_EQ(meters[1].bandPublishCount(), 0U) << "band values were stored while nobody looked";

    meters[1].setAnalysing(true);
    runCycle();
    EXPECT_EQ(meters[1].bandPublishCount(), 1U);
    const rt::BandValues taken = meters[1].bands();
    ASSERT_EQ(taken.count, rt::kMaxBandValues);
    for (std::size_t band = 0; band < rt::kMaxBandValues; ++band) {
        EXPECT_FLOAT_EQ(taken.values[band], test::BandWriterEffect::bandValue(band)) << band;
    }

    meters[1].setAnalysing(false);
    runCycle();
    EXPECT_EQ(meters[1].bandPublishCount(), 1U) << "band values were stored after the look ended";
}

TEST_F(SessionProcessorTest, ACycleOfAnAnalysedBlockTakesNoMemory)
{
    if (!test::NoAllocations::isSupported()) {
        GTEST_SKIP() << "the sanitizers own the allocator in this build";
    }
    // Both sides copied out and 64 band values published, every cycle.
    test::BandWriterEffect bands;
    putLive(makeLive(kRate, false, true, &bands));
    fillInputs(0.5f);
    meters[1].setAnalysing(true);
    runCycle();
    {
        const test::NoAllocations realTime;
        for (int cycle = 0; cycle < 100; ++cycle) {
            runCycle();
        }
    }
    // A full ring drops cycles rather than waiting or growing.
    EXPECT_GT(meters[1].analysis()->readableFrames(), 0U);
    EXPECT_GT(meters[1].inputAnalysis()->readableFrames(), 0U);
    EXPECT_EQ(meters[1].bandPublishCount(), 101U);
}

TEST_F(SessionProcessorTest, AHandoverTakesNoMemory)
{
    if (!test::NoAllocations::isSupported()) {
        GTEST_SKIP() << "the sanitizers own the allocator in this build";
    }
    std::unique_ptr<LiveGraph> next = makeLive();
    LiveGraph *raw = next.release();

    LiveGraph *retired = nullptr;
    {
        // This is what runs on the data thread between two cycles: the whole
        // of the handover, and nothing else.
        const test::NoAllocations realTime;
        retired = processor.exchange(raw);
    }
    EXPECT_EQ(retired, nullptr);
}

TEST_F(SessionProcessorTest, EveryCycleLengthTheEngineAllowsRuns)
{
    putLive(makeLive());
    fillInputs(0.5f);

    for (const std::uint32_t frames : {dsp::kMinBlockFrames, 64U, 256U, 1024U, dsp::kMaxBlockFrames}) {
        runCycle(frames);
        EXPECT_FLOAT_EQ(outputStorage[0][frames - 1], 0.5f) << frames << " frames";
    }
    EXPECT_EQ(processor.telemetry().xruns, 0U) << "a change of quantum is not an xrun";
}

// --- fading a lane across an edit ---

TEST_F(SessionProcessorTest, AFadeOutRampsALaneDownAndSaysWhenItIsDone)
{
    putLive(makeLive());
    fillInputs(1.0f);
    runCycle();
    ASSERT_FLOAT_EQ(outputStorage[0][kFrames - 1], 1.0f);

    // Two cycles' worth, so the ramp is not over in one.
    const std::uint32_t fadeFrames = 2 * kFrames;
    const std::uint32_t sequence = processor.requestFade(0b1, 0.0f, fadeFrames);
    ASSERT_NE(sequence, 0U);

    runCycle();
    EXPECT_LT(outputStorage[0][kFrames - 1], 0.55f) << "the lane is halfway down after the first cycle";
    EXPECT_GT(outputStorage[0][0], 0.9f) << "and it started from where it was";
    EXPECT_NE(processor.telemetry().fadeDone, sequence) << "the ramp cannot be over yet";

    runCycle();
    EXPECT_EQ(processor.telemetry().fadeDone, sequence);

    // And it stays down until it is asked for again: this is the window the
    // graph is swapped in.
    runCycle();
    EXPECT_FLOAT_EQ(outputPeak(), 0.0f);
}

TEST_F(SessionProcessorTest, AFadeInStartsFromWhereTheGraphWasHandedOver)
{
    // A graph whose lane arrives at zero is one that is about to be faded
    // in, which is how the engine hands over an edited lane.
    std::unique_ptr<LiveGraph> live = makeLive();
    for (SessionPort &port : live->outputs) {
        port.gain = 0.0f;
    }
    putLive(std::move(live));
    fillInputs(1.0f);

    runCycle();
    EXPECT_FLOAT_EQ(outputPeak(), 0.0f) << "a lane handed over at zero is silent until it is asked for";

    const std::uint32_t fadeFrames = 2 * kFrames;
    const std::uint32_t sequence = processor.requestFade(0b1, 1.0f, fadeFrames);
    runCycle();
    EXPECT_GT(outputStorage[0][kFrames - 1], 0.45f);
    EXPECT_LT(outputStorage[0][0], 0.1f);
    runCycle();
    EXPECT_EQ(processor.telemetry().fadeDone, sequence);
    // The last sample of the cycle the ramp ends on is one step short of the
    // target; the target itself is where the next cycle starts.
    EXPECT_NEAR(outputStorage[0][kFrames - 1], 1.0f, 1.0f / static_cast<float>(kFrames));
    runCycle();
    EXPECT_FLOAT_EQ(outputStorage[0][0], 1.0f) << "and from there the lane is through at full level";
}

TEST_F(SessionProcessorTest, ALaneTheEditDoesNotTouchIsCopiedSampleForSample)
{
    // This is the whole point of fading by lane rather than by node: an edit
    // to one lane must leave the others bit-identical, not merely close.
    putLive(makeLive(kRate, true));
    for (std::uint32_t channel = 0; channel < kChannels; ++channel) {
        for (std::uint32_t frame = 0; frame < kFrames; ++frame) {
            inputStorage[channel][frame] = std::sin(0.031f * static_cast<float>(frame)) * 0.7f;
        }
    }

    (void)processor.requestFade(0b01, 0.0f, 2 * kFrames);
    runCycle();

    for (std::uint32_t frame = 0; frame < kFrames; ++frame) {
        ASSERT_EQ(outputStorage[1][frame], inputStorage[1][frame]) << "frame " << frame;
    }
    EXPECT_LT(outputStorage[0][kFrames - 1], 0.4f) << "while the faded lane did come down";
}

TEST_F(SessionProcessorTest, AFadeOfNoFramesTakesEffectAtOnce)
{
    putLive(makeLive());
    fillInputs(1.0f);

    const std::uint32_t sequence = processor.requestFade(0b1, 0.0f, 0);
    runCycle();

    EXPECT_EQ(processor.telemetry().fadeDone, sequence);
    EXPECT_FLOAT_EQ(outputPeak(), 0.0f);
}

TEST_F(SessionProcessorTest, AFadeFinishesEvenWhereNothingIsLinked)
{
    // The control thread waits for the ramp before it swaps the graph. If an
    // unlinked lane could stall it, an edit would hang until something was
    // plugged in.
    std::unique_ptr<LiveGraph> live = makeLive();
    for (SessionPort &port : live->outputs) {
        port.buffer = nullptr;
    }
    putLive(std::move(live));

    const std::uint32_t sequence = processor.requestFade(0b1, 0.0f, kFrames);
    runCycle();

    EXPECT_EQ(processor.telemetry().fadeDone, sequence);
}

TEST_F(SessionProcessorTest, TheRampNeverStepsFurtherThanTheFadeAllows)
{
    // What "click-free" means in numbers: over a 10 ms ramp at 48 kHz no two
    // neighbouring samples of a steady signal differ by more than the ramp's
    // own slope.
    putLive(makeLive());
    fillInputs(1.0f);
    runCycle();

    constexpr std::uint32_t kFadeFrames = 480; // 10 ms at 48 kHz
    (void)processor.requestFade(0b1, 0.0f, kFadeFrames);

    float previous = outputStorage[0][kFrames - 1];
    float largestStep = 0.0f;
    for (int cycle = 0; cycle < 8; ++cycle) {
        runCycle();
        for (std::uint32_t frame = 0; frame < kFrames; ++frame) {
            largestStep = std::max(largestStep, std::abs(outputStorage[0][frame] - previous));
            previous = outputStorage[0][frame];
        }
    }
    // One ramp step is 1/480; allow a little for the arithmetic.
    EXPECT_LT(largestStep, 1.5f / static_cast<float>(kFadeFrames));
    EXPECT_FLOAT_EQ(previous, 0.0f);
}

TEST_F(SessionProcessorTest, FadingTakesNoMemory)
{
    if (!test::NoAllocations::isSupported()) {
        GTEST_SKIP() << "the sanitizers own the allocator in this build";
    }
    putLive(makeLive());
    fillInputs(0.5f);
    runCycle();

    {
        const test::NoAllocations realTime;
        (void)processor.requestFade(0b1, 0.0f, 4 * kFrames);
        for (int cycle = 0; cycle < 8; ++cycle) {
            runCycle();
        }
    }
}

TEST_F(SessionProcessorTest, EveryClickIsShownAtEveryQuantum)
{
    // The thing a meter must not do is lose a single sample. A click of one
    // sample is the hardest case at both ends: at quantum 32 a cycle is
    // shorter than the screen's frame, and at 2048 a cycle is longer than
    // it, so a meter that published "the latest value" would miss the click
    // in one direction or the other. **This is the same arithmetic the
    // sampler does**, run against real cycles of the real processor, with no
    // audio server and no clock: the frames are what decides.
    constexpr std::uint32_t kSamplerFrames = kRate / 60; // sixty snapshots a second
    constexpr float kClick = 0.5f; // −6 dBFS

    for (const std::uint32_t quantum : {32U, 64U, 128U, 256U, 512U, 1024U, 2048U}) {
        putLive(makeLive());
        for (rt::BlockMeter &meter : meters) {
            meter.clear();
        }

        // Four clicks, half a second apart, each landing at a different
        // place inside a cycle — one of them deliberately on the last frame
        // of one.
        const std::vector<std::uint64_t> clicks{
            quantum * 3ULL,
            (kRate / 2ULL) + 1ULL,
            kRate - 1ULL,
            (kRate * 3ULL / 2ULL) + quantum - 1ULL,
        };
        const std::uint64_t total = 2ULL * kRate;

        std::uint64_t played = 0;
        std::uint64_t nextRead = kSamplerFrames;
        std::vector<float> seen;
        while (played < total) {
            fillInputs(0.0f);
            for (const std::uint64_t click : clicks) {
                if (click >= played && click < played + quantum) {
                    inputStorage[0][click - played] = kClick;
                }
            }
            runCycle(quantum);
            played += quantum;
            while (played >= nextRead) {
                const rt::MeterReading reading = meters[1].channel(0).take();
                if (reading.peak > 0.0f) {
                    seen.push_back(reading.peak);
                }
                nextRead += kSamplerFrames;
            }
        }
        // Whatever is left in the accumulator when the run ends counts too.
        if (const rt::MeterReading last = meters[1].channel(0).take(); last.peak > 0.0f) {
            seen.push_back(last.peak);
        }

        EXPECT_EQ(seen.size(), clicks.size()) << "at quantum " << quantum;
        for (const float peak : seen) {
            EXPECT_FLOAT_EQ(peak, kClick) << "at quantum " << quantum;
        }
    }
}

TEST_F(SessionProcessorTest, AMeterMeasuresWhatTheBlockPutOutNotWhatTheNodeSends)
{
    putLive(makeLive());
    fillInputs(0.25f);
    runCycle();

    // Both blocks are metered, not only the one whose ports the node
    // publishes: a tile in the middle of a chain has a meter too.
    const rt::MeterReading source = meters[0].channel(0).take();
    const rt::MeterReading gain = meters[1].channel(0).take();
    EXPECT_FLOAT_EQ(source.peak, 0.25f);
    EXPECT_FLOAT_EQ(gain.peak, 0.25f);
    EXPECT_NEAR(gain.rms, 0.25f, 1e-6f);
    EXPECT_EQ(gain.frames, kFrames);
    EXPECT_TRUE(gain.isActive());
}

TEST_F(SessionProcessorTest, AGraphThatIsNotMeteredIsStillRun)
{
    // A node's meter is allowed to be null: a session nobody is looking at
    // should not pay for meters.
    putLive(makeLive(kRate, false, false));

    fillInputs(0.5f);
    runCycle();
    EXPECT_FLOAT_EQ(outputPeak(), 0.5f);
    EXPECT_FLOAT_EQ(meters[1].channel(0).take().peak, 0.0f);
}

// **An application that stops playing, recorded**. Its lane has no
// buffer while nothing is linked to it; a Recorder whose tap reads its
// pauses from that lane writes nothing for those cycles, so the two parts
// join, and one that does not writes them as silence. Counted in cycles,
// so the length is exact rather than roughly right.
TEST_F(SessionProcessorTest, ARecorderLeavesOutTheCyclesItsApplicationWasAwayOrWritesThemAsSilence)
{
    for (const bool leaveOut : {true, false}) {
        SCOPED_TRACE(leaveOut ? "pauses left out" : "pauses kept as silence");
        blocks::Recorder recorder;
        rt::AudioTap tap(kChannels, 48000);
        tap.setGapLanes(leaveOut ? 1U : 0U);

        graph::GraphSpec spec;
        spec.sampleRate = kRate;
        graph::NodeSpec sourceNode;
        sourceNode.id = "app";
        sourceNode.block = &source;
        graph::NodeSpec recorderNode;
        recorderNode.id = "rec";
        recorderNode.block = &recorder;
        spec.nodes = {sourceNode, recorderNode};
        for (std::uint32_t channel = 0; channel < kChannels; ++channel) {
            spec.edges.push_back(graph::EdgeSpec{0, channel, 1, channel});
        }
        dsp::PrepareSpec prepare;
        prepare.sampleRate = kRate;
        prepare.recording = &tap;
        ASSERT_TRUE(source.prepare(prepare).has_value());
        ASSERT_TRUE(recorder.prepare(prepare).has_value());
        std::expected<graph::CompiledGraph, graph::GraphError> compiled = graph::CompiledGraph::compile(spec);
        ASSERT_TRUE(compiled.has_value());
        auto live = std::make_unique<LiveGraph>();
        live->graph = std::make_unique<graph::CompiledGraph>(std::move(*compiled));
        live->sampleRate = kRate;
        live->laneCount = 1;
        for (std::uint32_t channel = 0; channel < kChannels; ++channel) {
            SessionPort in;
            in.graphNode = 0;
            in.graphPort = channel;
            in.lane = 0;
            in.buffer = inputStorage[channel].data();
            live->inputs.push_back(in);
        }
        putLive(std::move(live));

        const auto linked = [this](bool present) {
            LiveGraph *graph = processor.live();
            for (std::uint32_t channel = 0; channel < kChannels; ++channel) {
                graph->inputs[channel].buffer = present ? inputStorage[channel].data() : nullptr;
            }
        };
        fillInputs(0.5f);
        for (int cycle = 0; cycle < 10; ++cycle) {
            runCycle();
        }
        linked(false); // the application has gone
        for (int cycle = 0; cycle < 7; ++cycle) {
            runCycle();
        }
        linked(true); // and is back
        for (int cycle = 0; cycle < 5; ++cycle) {
            runCycle();
        }

        std::vector<float> recorded(2 * 48000);
        const std::size_t frames = tap.read(recorded, 48000);
        if (leaveOut) {
            ASSERT_EQ(frames, 15U * kFrames);
            // The two parts join: nothing but the application's sound.
            for (std::size_t sample = 0; sample < frames * kChannels; ++sample) {
                ASSERT_EQ(recorded[sample], 0.5f) << "sample " << sample;
            }
        } else {
            ASSERT_EQ(frames, 22U * kFrames);
            // Seven cycles of silence where it was away, and only there.
            std::size_t silent = 0;
            for (std::size_t frame = 0; frame < frames; ++frame) {
                silent += recorded[frame * kChannels] == 0.0f ? 1 : 0;
            }
            EXPECT_EQ(silent, 7U * kFrames);
            EXPECT_EQ(recorded[10 * kFrames * kChannels], 0.0f);
            EXPECT_EQ(recorded[17 * kFrames * kChannels], 0.5f);
        }
        const std::unique_ptr<LiveGraph> retired(processor.exchange(nullptr));
    }
}

// --- Giving way before the kernel's limit ------------------------

namespace
{

/// What a test says the kernel says about the audio thread: each reading
/// adds cpuPerCycle of CPU, and counts a sleep or not.
struct FakeThread {
    static inline std::uint64_t cpu = 0;
    static inline std::int64_t sleeps = 0;
    static inline std::uint64_t cpuPerCycle = 0;
    static inline bool sleepsBetweenCycles = false;
    static inline bool realtime = true;
    static inline int asked = 0;

    static bool read(rt::ThreadRunReading &reading) noexcept RB_NONBLOCKING
    {
        cpu += cpuPerCycle;
        if (sleepsBetweenCycles) {
            ++sleeps;
        }
        reading.cpuNanos = cpu;
        reading.sleeps = sleeps;
        return true;
    }

    static bool isRealtime() noexcept RB_NONBLOCKING
    {
        ++asked;
        return realtime;
    }
};

constexpr std::uint64_t kMs = 1'000'000;

class SessionProcessorOverload : public SessionProcessorTest
{
protected:
    test::StallingEffect stall{kChannels};

    void SetUp() override
    {
        SessionProcessorTest::SetUp();
        FakeThread::cpu = 0;
        FakeThread::sleeps = 0;
        FakeThread::cpuPerCycle = 10 * kMs;
        FakeThread::sleepsBetweenCycles = false;
        FakeThread::realtime = true;
        FakeThread::asked = 0;
        processor.setThreadProbes(&FakeThread::read, &FakeThread::isRealtime);
    }

    /// Cycles until the node gives way, or \a most of them. How many it took.
    int cyclesUntilItGivesWay(int most = 100)
    {
        for (int cycle = 1; cycle <= most; ++cycle) {
            runCycle();
            if (processor.telemetry().shedding != 0) {
                return cycle;
            }
        }
        return -1;
    }
};

} // namespace

// A loop that never sleeps, with nothing heavy in this session: it still
// gives way once the loop has run so long that everybody must, which is
// well inside the kernel's 200 ms.
TEST_F(SessionProcessorOverload, ALoopThatNeverSleepsGivesWayWellBeforeTheKernelsLimit)
{
    putLive(makeLive());
    fillInputs(0.5f);
    runCycle();
    EXPECT_GT(outputPeak(), 0.4f);

    const int cycles = cyclesUntilItGivesWay();
    ASSERT_GT(cycles, 0) << "the session never gave way";
    const std::uint64_t ran = static_cast<std::uint64_t>(cycles + 1) * FakeThread::cpuPerCycle;
    EXPECT_GE(ran, static_cast<std::uint64_t>(kShedRegardless.count()));
    EXPECT_LT(ran, 150 * kMs) << "gave way only after " << ran / kMs << " ms";
    EXPECT_EQ(processor.telemetry().overloads, 1U);

    // Silent from then on, and the thread gets its sleep back.
    fillInputs(0.5f);
    for (int cycle = 0; cycle < 10; ++cycle) {
        runCycle();
        EXPECT_EQ(outputPeak(), 0.0f);
    }
    EXPECT_EQ(processor.telemetry().overloads, 1U) << "one occasion, not one a cycle";
}

// The session that is the reason gives way first, and names the block that
// cost the most.
TEST_F(SessionProcessorOverload, ASessionTakingARealPartOfTheCycleGivesWayFirstAndNamesItsHeaviestBlock)
{
    putLive(makeLive(kRate, false, true, &stall));
    // 1.5 ms of a 2.7 ms cycle.
    stall.holdEvery(1, 1500);
    fillInputs(0.5f);

    const int cycles = cyclesUntilItGivesWay();
    ASSERT_GT(cycles, 0);
    const std::uint64_t ran = static_cast<std::uint64_t>(cycles + 1) * FakeThread::cpuPerCycle;
    EXPECT_GE(ran, static_cast<std::uint64_t>(kShedAfter.count()));
    EXPECT_LT(ran, static_cast<std::uint64_t>(kShedRegardless.count())) << "it waited for everybody's limit";

    const SessionTelemetry telemetry = processor.telemetry();
    EXPECT_STREQ(telemetry.heaviest[0].data(), "gain") << "the stalling block sits where the gain does";
    EXPECT_GT(telemetry.heaviestLoad[0], 0.4f);
    EXPECT_LT(telemetry.heaviestLoad[1], telemetry.heaviestLoad[0]);
    EXPECT_STREQ(telemetry.heaviest[1].data(), "src");
    EXPECT_STREQ(telemetry.heaviest[2].data(), "") << "two blocks, so no third";
}

// A light session in a loop that is overrunning for somebody else's sake
// keeps playing until the heavy one has had its chance to give way.
TEST_F(SessionProcessorOverload, ALightSessionIsNotTheOneToGiveWay)
{
    putLive(makeLive());
    fillInputs(0.5f);
    FakeThread::cpuPerCycle = 5 * kMs;
    // 90 ms of run: past kShedAfter, short of kShedRegardless.
    for (int cycle = 0; cycle < 18; ++cycle) {
        runCycle();
    }
    EXPECT_EQ(processor.telemetry().shedding, 0U);
    EXPECT_GT(outputPeak(), 0.4f);
}

// **A long cycle is not an overrun.** At the largest quantum a cycle can
// take more CPU than kShedAfter and still have time to spare; what the
// kernel minds is the loop never sleeping, not how long one cycle is.
TEST_F(SessionProcessorOverload, ALongCycleThatIsFollowedByASleepNeverGivesWay)
{
    putLive(makeLive(kRate, false, true, &stall));
    stall.holdEvery(1, 1500);
    FakeThread::sleepsBetweenCycles = true;
    FakeThread::cpuPerCycle = 60 * kMs;
    EXPECT_EQ(cyclesUntilItGivesWay(50), -1);
    FakeThread::cpuPerCycle = 150 * kMs;
    EXPECT_EQ(cyclesUntilItGivesWay(50), -1);
    EXPECT_EQ(processor.telemetry().overloads, 0U);
}

TEST_F(SessionProcessorOverload, ALoopThatSleepsBetweenCyclesNeverGivesWay)
{
    putLive(makeLive(kRate, false, true, &stall));
    stall.holdEvery(1, 1500);
    FakeThread::sleepsBetweenCycles = true;
    EXPECT_EQ(cyclesUntilItGivesWay(200), -1);
    EXPECT_EQ(processor.telemetry().overloads, 0U);
}

// The kernel holds only a real-time thread to its limit, so an ordinary one
// has nothing to give way to. This is also what keeps offline processing
// in a tight loop, as the tests here do, from ever going silent.
TEST_F(SessionProcessorOverload, AThreadThatIsNotRealtimeNeverGivesWay)
{
    FakeThread::realtime = false;
    putLive(makeLive());
    EXPECT_EQ(cyclesUntilItGivesWay(), -1);
}

// **Why it did not give way is said**: rtkit can refuse real-time
// under its burst limit, and whoever expected the session to give way has
// to be able to tell that from the watch failing. One run that never
// sleeps is one occasion, and the thread is asked once for it, not once a
// cycle.
TEST_F(SessionProcessorOverload, ALongRunOnAThreadThatIsNotRealtimeIsCountedOnce)
{
    FakeThread::realtime = false;
    putLive(makeLive());
    ASSERT_EQ(cyclesUntilItGivesWay(100), -1);
    const SessionTelemetry telemetry = processor.telemetry();
    EXPECT_EQ(telemetry.realtime, 0U);
    EXPECT_EQ(telemetry.overloads, 0U);
    EXPECT_EQ(telemetry.longRunsNotRealtime, 1U);
    EXPECT_EQ(FakeThread::asked, 2) << "asked on the first cycle and once for the run, not every cycle";

    // A rest ends the occasion; the next long run is another.
    FakeThread::sleepsBetweenCycles = true;
    for (int cycle = 0; cycle < 3; ++cycle) {
        runCycle();
    }
    FakeThread::sleepsBetweenCycles = false;
    ASSERT_EQ(cyclesUntilItGivesWay(100), -1);
    EXPECT_EQ(processor.telemetry().longRunsNotRealtime, 2U);
}

// A reader knows from the first cycle whether this thread will ever be held
// to the kernel's limit.
TEST_F(SessionProcessorOverload, TheFirstCycleSaysWhetherTheThreadIsRealtime)
{
    FakeThread::sleepsBetweenCycles = true;
    putLive(makeLive());
    runCycle();
    EXPECT_EQ(processor.telemetry().realtime, 1U);
    EXPECT_EQ(processor.telemetry().longRunsNotRealtime, 0U);

    SessionProcessor other;
    FakeThread::realtime = false;
    other.setThreadProbes(&FakeThread::read, &FakeThread::isRealtime);
    other.process(nextCycle());
    EXPECT_EQ(other.telemetry().realtime, 0U);
}

TEST_F(SessionProcessorOverload, ANewGraphRunsTheBlocksAgain)
{
    putLive(makeLive());
    fillInputs(0.5f);
    ASSERT_GT(cyclesUntilItGivesWay(), 0);
    runCycle();
    EXPECT_EQ(outputPeak(), 0.0f);

    FakeThread::sleepsBetweenCycles = true;
    putLive(makeLive());
    fillInputs(0.5f);
    runCycle();
    EXPECT_EQ(processor.telemetry().shedding, 0U);
    EXPECT_GT(outputPeak(), 0.4f);
}

TEST_F(SessionProcessorOverload, AnotherQuantumRunsTheBlocksAgain)
{
    putLive(makeLive());
    ASSERT_GT(cyclesUntilItGivesWay(), 0);

    FakeThread::sleepsBetweenCycles = true;
    fillInputs(0.5f);
    runCycle(kFrames * 2);
    EXPECT_EQ(processor.telemetry().shedding, 0U);
    EXPECT_GT(outputPeak(kFrames * 2), 0.4f);
}

// An edit waits for its fade to come back, so a silent session still has
// to finish one.
TEST_F(SessionProcessorOverload, AFadeFinishesWhileTheSessionIsSilent)
{
    putLive(makeLive());
    ASSERT_GT(cyclesUntilItGivesWay(), 0);
    const std::uint32_t sequence = processor.requestFade(1, 0.0f, kFrames * 2);
    runCycle();
    runCycle();
    EXPECT_EQ(processor.telemetry().fadeDone, sequence);
}

TEST_F(SessionProcessorOverload, GivingWayAndBeingSilentTakeNoMemory)
{
    if (!test::NoAllocations::isSupported()) {
        GTEST_SKIP() << "the sanitizers own the allocator in this build";
    }
    putLive(makeLive(kRate, false, true, &stall));
    stall.holdEvery(1, 1500);
    runCycle();
    {
        const test::NoAllocations realTime;
        for (int cycle = 0; cycle < 20; ++cycle) {
            runCycle();
        }
    }
    EXPECT_EQ(processor.telemetry().shedding, 1U) << "the trap did not cover giving way";
}
