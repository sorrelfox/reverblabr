// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "allocationtrap.h"
#include "analysis.h"
#include "blocksizeinvariance.h"
#include "dsp/audiobuffer.h"
#include "dsp/decibel.h"
#include "gainblock.h"
#include "graph/compiledgraph.h"
#include "graph/graphspec.h"
#include "kitblocks.h"
#include "offlinerunner.h"
#include "signals.h"
#include "wavfile.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <vector>

using namespace rb::test;

// --- the signal generators ---

TEST(Signals, MakeWhatTheySay)
{
    EXPECT_EQ(silence(10).size(), 10u);
    EXPECT_EQ(peak(silence(10)), 0.0f);
    EXPECT_FLOAT_EQ(peak(constant(10, 0.3f)), 0.3f);

    const Signal tone = sine(4800, 1000.0);
    EXPECT_NEAR(peak(tone), 1.0f, 1.0e-5f);
    EXPECT_NEAR(rms(tone), 1.0f / std::numbers::sqrt2_v<float>, 1.0e-3f);

    const Signal click = impulse(100, 7, 0.5f);
    EXPECT_FLOAT_EQ(click[7], 0.5f);
    EXPECT_EQ(click[6], 0.0f);
    EXPECT_EQ(click[8], 0.0f);

    const Signal edge = step(100, 40, 0.25f);
    EXPECT_EQ(edge[39], 0.0f);
    EXPECT_FLOAT_EQ(edge[40], 0.25f);
    EXPECT_FLOAT_EQ(edge.back(), 0.25f);
}

TEST(Signals, NoiseIsTheSameEveryRunAndDifferentBetweenSeeds)
{
    // A test that cannot be reproduced is not a test.
    EXPECT_EQ(whiteNoise(1000, 1.0f, 7), whiteNoise(1000, 1.0f, 7));
    EXPECT_NE(whiteNoise(1000, 1.0f, 7), whiteNoise(1000, 1.0f, 8));
    EXPECT_LE(peak(whiteNoise(10000, 0.5f, 1)), 0.5f);

    EXPECT_EQ(pinkNoise(1000, 1.0f, 7), pinkNoise(1000, 1.0f, 7));
    EXPECT_NEAR(peak(pinkNoise(10000, 0.5f, 1)), 0.5f, 1.0e-6f);
}

TEST(Signals, PinkNoiseHasMoreLowEndThanWhite)
{
    // What makes it pink: its power falls with frequency.
    constexpr std::size_t kFrames = 48000;
    const Signal pink = pinkNoise(kFrames, 1.0f, 3);
    const float low = amplitudeAt(pink, 100.0);
    const float high = amplitudeAt(pink, 10000.0);
    EXPECT_GT(low, high * 2.0f) << "low " << low << ", high " << high;

    const Signal white = whiteNoise(kFrames, 1.0f, 3);
    EXPECT_LT(amplitudeAt(white, 100.0), low);
}

TEST(Signals, ASweepVisitsBothEndsOfItsRange)
{
    constexpr double kSampleRate = 48000.0;
    const Signal rising = sweep(48000, 100.0, 10000.0, kSampleRate);
    EXPECT_NEAR(peak(rising), 1.0f, 0.05f);
    // The first tenth of it is near the bottom of the range, the last near
    // the top.
    const std::span<const float> start(rising.data(), 4800);
    const std::span<const float> end(rising.data() + 43200, 4800);
    EXPECT_GT(amplitudeAt(start, 150.0, kSampleRate), amplitudeAt(start, 8000.0, kSampleRate));
    EXPECT_GT(amplitudeAt(end, 8000.0, kSampleRate), amplitudeAt(end, 150.0, kSampleRate));
}

TEST(Signals, ABurstIsToneThenNothing)
{
    const Signal bursts = burst(1000, 1000.0, 100, 100);
    EXPECT_GT(peak(std::span<const float>(bursts.data(), 100)), 0.5f);
    for (std::size_t frame = 100; frame < 200; ++frame) {
        EXPECT_EQ(bursts[frame], 0.0f) << "at frame " << frame;
    }
}

// --- the analysis helpers ---

TEST(Analysis, PeakAndRmsAgreeWithTheAnalyticValues)
{
    const Signal tone = sine(48000, 1000.0, kTestSampleRate, 0.5f);
    EXPECT_NEAR(peak(tone), 0.5f, 1.0e-5f);
    EXPECT_NEAR(rms(tone), 0.5f / std::numbers::sqrt2_v<float>, 1.0e-3f);
    EXPECT_NEAR(peakDb(tone), rb::dsp::gainToDb(0.5f), 0.01f);
    EXPECT_NEAR(rmsDb(tone), peakDb(tone) - 3.01f, 0.05f) << "a sine's RMS is 3 dB below its peak";

    EXPECT_EQ(peak(Signal{}), 0.0f);
    EXPECT_EQ(rms(Signal{}), 0.0f);
    EXPECT_EQ(rmsDb(silence(10)), rb::dsp::kSilenceDb);
}

TEST(Analysis, TheNullTestSaysHowFarApartTwoSignalsAre)
{
    const Signal tone = sine(1000, 440.0);
    EXPECT_EQ(nullTestDb(tone, tone), rb::dsp::kSilenceDb);
    EXPECT_TRUE(nulls(tone, tone));

    Signal nudged = tone;
    nudged[500] += 0.001f;
    EXPECT_NEAR(nullTestDb(tone, nudged), rb::dsp::gainToDb(0.001f), 0.01f);
    EXPECT_FALSE(nulls(tone, nudged)) << "a thousandth is well above the ninety-decibel floor";
    EXPECT_TRUE(nulls(tone, nudged, -50.0f)) << "but not above a looser one";

    EXPECT_FALSE(nulls(tone, sine(999, 440.0))) << "different lengths are different signals";
}

TEST(Analysis, TheClickDetectorFindsAStepThatShouldNotBeThere)
{
    const Signal smooth = sine(1000, 100.0);
    const float smoothStep = maxSampleStep(smooth);

    Signal clicking = smooth;
    clicking[500] = 1.0f;
    clicking[501] = -1.0f;
    EXPECT_GT(maxSampleStep(clicking), smoothStep * 10.0f);
    EXPECT_EQ(maxSampleStep(Signal{}), 0.0f);
    EXPECT_EQ(maxSampleStep(Signal{1.0f}), 0.0f);
}

TEST(Analysis, LatencyIsFoundByCorrelatingAnImpulse)
{
    const Signal sent = impulse(1000, 10);
    const Signal came = impulse(1000, 10 + 37);
    const std::optional<int> lag = latencySamples(sent, came, 200);
    ASSERT_TRUE(lag.has_value());
    EXPECT_EQ(*lag, 37);

    EXPECT_EQ(latencySamples(sent, sent, 200), 0);
    EXPECT_FALSE(latencySamples(silence(100), silence(100), 10).has_value());
    EXPECT_FALSE(latencySamples(Signal{}, sent, 10).has_value());
}

TEST(Analysis, DistortionIsNearZeroForASineAndNotForABentOne)
{
    constexpr double kFrequency = 1000.0;
    const Signal clean = sine(48000, kFrequency, kTestSampleRate, 0.5f);
    EXPECT_LT(totalHarmonicDistortion(clean, kFrequency), 0.001f);

    // The same sine with its tips squared off.
    Signal squashed = clean;
    for (float &sample : squashed) {
        sample = std::clamp(sample, -0.3f, 0.3f);
    }
    EXPECT_GT(totalHarmonicDistortion(squashed, kFrequency), 0.05f);
}

TEST(Analysis, TheAmplitudeAtAFrequencyIsTheOneThatWasPutThere)
{
    const Signal tone = sine(48000, 1000.0, kTestSampleRate, 0.4f);
    EXPECT_NEAR(amplitudeAt(tone, 1000.0), 0.4f, 0.01f);
    EXPECT_LT(amplitudeAt(tone, 3000.0), 0.01f);
    EXPECT_EQ(amplitudeAt(Signal{}, 1000.0), 0.0f);
}

// --- the offline runner ---

TEST(BlockRunner, RunsTheTestGainBlock)
{
    GainBlock block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    ASSERT_TRUE(block.setParam("gainDb", rb::dsp::ParamValue::ofFloat(-6.0f)).has_value());
    block.reset();

    const MultiSignal input = spread(sine(9600, 1000.0, kTestSampleRate, 0.5f), 2);
    const MultiSignal output = runner.run(input);

    ASSERT_EQ(output.size(), 2u);
    ASSERT_EQ(output[0].size(), input[0].size());
    EXPECT_NEAR(peak(output[0]), 0.5f * rb::dsp::dbToGain(-6.0f), 0.01f);
    EXPECT_NEAR(peak(output[1]), peak(output[0]), 1.0e-6f);
}

TEST(BlockRunner, TheCycleLengthDoesNotChangeAWellBehavedBlock)
{
    const MultiSignal input = spread(whiteNoise(20000, 0.5f, 5), 1);

    FixedGain steady(0.75f);
    BlockRunner steadyRunner(steady, 1, 1);
    ASSERT_TRUE(steadyRunner.prepare().has_value());
    const MultiSignal fixedCycles = steadyRunner.run(input, CycleShape::fixed(256));

    FixedGain varied(0.75f);
    BlockRunner variedRunner(varied, 1, 1);
    ASSERT_TRUE(variedRunner.prepare().has_value());
    const MultiSignal randomCycles = variedRunner.run(input, CycleShape::random(1, 2048, 4));

    ASSERT_EQ(fixedCycles[0].size(), randomCycles[0].size());
    EXPECT_TRUE(nulls(fixedCycles[0], randomCycles[0]));
}

TEST(BlockRunner, MeasuresABlocksLatencyBackOutOfIt)
{
    DelayByOneHundred block;
    BlockRunner runner(block, 1, 1);
    ASSERT_TRUE(runner.prepare().has_value());

    const MultiSignal input{impulse(2000, 50)};
    const MultiSignal output = runner.run(input, CycleShape::fixed(64));

    const std::optional<int> lag = latencySamples(input[0], output[0], 500);
    ASSERT_TRUE(lag.has_value());
    EXPECT_EQ(static_cast<std::uint32_t>(*lag), DelayByOneHundred::kDelaySamples);
    EXPECT_EQ(block.info().latencySamples, DelayByOneHundred::kDelaySamples) << "and it said so as well";
}

TEST(BlockRunner, FindsTheDistortionASoftClipperAdds)
{
    SoftClipper block;
    BlockRunner runner(block, 1, 1);
    ASSERT_TRUE(runner.prepare().has_value());

    const MultiSignal input{sine(48000, 1000.0, kTestSampleRate, 0.8f)};
    const MultiSignal output = runner.run(input);
    EXPECT_GT(totalHarmonicDistortion(output[0], 1000.0), 0.02f);
    EXPECT_LT(totalHarmonicDistortion(input[0], 1000.0), 0.001f) << "and it was clean going in";
}

TEST(GraphRunner, RunsAThreeBlockGraph)
{
    auto source = std::make_unique<SineSource>(1000.0, 0.5f);
    auto gain = std::make_unique<FixedGain>(0.5f);
    auto tail = std::make_unique<FixedGain>(0.5f);

    rb::graph::GraphSpec spec;
    spec.maxBlockFrames = 2048;
    spec.nodes.push_back(rb::graph::NodeSpec{"b1", source.get(), true});
    spec.nodes.push_back(rb::graph::NodeSpec{"b2", gain.get(), true});
    spec.nodes.push_back(rb::graph::NodeSpec{"b3", tail.get(), true});
    spec.edges.push_back(rb::graph::EdgeSpec{0, 0, 1, 0});
    spec.edges.push_back(rb::graph::EdgeSpec{1, 0, 2, 0});

    std::expected<rb::graph::CompiledGraph, rb::graph::GraphError> graph = rb::graph::CompiledGraph::compile(spec);
    ASSERT_TRUE(graph.has_value());
    for (const rb::graph::NodeSpec &node : spec.nodes) {
        rb::dsp::PrepareSpec prepare;
        prepare.maxBlockFrames = 2048;
        ASSERT_TRUE(node.block->prepare(prepare).has_value());
    }

    GraphRunner runner(*graph);
    runner.capture(2, 0);
    runner.run(48000, CycleShape::random(1, 1024, 11));

    const Signal &heard = runner.captured(2, 0);
    ASSERT_EQ(heard.size(), 48000u);
    EXPECT_NEAR(peak(heard), 0.5f * 0.5f * 0.5f, 0.01f) << "the source through two halvings";
    EXPECT_LT(totalHarmonicDistortion(heard, 1000.0), 0.01f) << "and still a clean sine at every cycle length";
    EXPECT_TRUE(runner.captured(9, 0).empty()) << "a port nobody captured has nothing";
}

TEST(GraphRunner, FeedsAPortNoWireReaches)
{
    auto source = std::make_unique<SineSource>();
    auto gain = std::make_unique<FixedGain>(2.0f);

    rb::graph::GraphSpec spec;
    spec.maxBlockFrames = 2048;
    spec.nodes.push_back(rb::graph::NodeSpec{"b1", source.get(), true});
    spec.nodes.push_back(rb::graph::NodeSpec{"b2", gain.get(), true});

    std::expected<rb::graph::CompiledGraph, rb::graph::GraphError> graph = rb::graph::CompiledGraph::compile(spec);
    ASSERT_TRUE(graph.has_value());
    for (const rb::graph::NodeSpec &node : spec.nodes) {
        rb::dsp::PrepareSpec prepare;
        prepare.maxBlockFrames = 2048;
        ASSERT_TRUE(node.block->prepare(prepare).has_value());
    }

    GraphRunner runner(*graph);
    runner.feed(1, 0, constant(1000, 0.25f));
    runner.capture(1, 0);
    runner.run(1000, CycleShape::fixed(128));

    const Signal &heard = runner.captured(1, 0);
    ASSERT_EQ(heard.size(), 1000u);
    for (const float sample : heard) {
        EXPECT_FLOAT_EQ(sample, 0.5f) << "what was fed in, doubled";
    }
}

// --- the block-size-invariance check ---

TEST(BlockSizeInvariance, PassesABlockThatWorksSampleBySample)
{
    const MultiSignal input = spread(whiteNoise(30000, 0.5f, 9), 1);
    const InvarianceReport report = checkBlockSizeInvariance([] { return std::make_unique<FixedGain>(0.75f); }, input);
    EXPECT_TRUE(report.passed) << report.summary;
}

TEST(BlockSizeInvariance, CatchesABlockThatDependsOnTheChunkSize)
{
    // The check has to fail the block that was written wrong on purpose,
    // otherwise it proves nothing about the blocks that were not.
    const MultiSignal input = spread(whiteNoise(30000, 0.5f, 9), 1);
    const InvarianceReport report = checkBlockSizeInvariance([] { return std::make_unique<ChunkDependent>(); }, input);
    EXPECT_FALSE(report.passed) << report.summary;
    EXPECT_GT(report.worstNullDb, -90.0f);
    EXPECT_NE(report.summary.find("differs by"), std::string::npos) << report.summary;
}

TEST(BlockSizeInvariance, PassesTheTestGainBlock)
{
    const MultiSignal input = spread(sine(30000, 440.0, kTestSampleRate, 0.5f), 2);
    InvarianceOptions options;
    options.inputChannels = 2;
    options.outputChannels = 2;
    const InvarianceReport report =
        checkBlockSizeInvariance([] { return std::make_unique<GainBlock>(); }, input, options);
    EXPECT_TRUE(report.passed) << report.summary;
}

TEST(BlockSizeInvariance, PassesTheDelayLineBlock)
{
    const MultiSignal input = spread(whiteNoise(30000, 0.5f, 12), 1);
    const InvarianceReport report =
        checkBlockSizeInvariance([] { return std::make_unique<DelayByOneHundred>(); }, input);
    EXPECT_TRUE(report.passed) << report.summary;
}

// --- the allocation trap ---

namespace
{

/// Hands back a pointer the optimiser can no longer trace to where it came
/// from.
///
/// [expr.new] lets an implementation leave out an allocation whose result it
/// can account for, and at `-O2` GCC took that permission: the `new` and the
/// `delete` below both disappeared, the trap counted nothing, and the test
/// failed for days in the one build that ships. The empty `asm volatile`
/// stops the pairing rather than the elision — once the pointer has been
/// through a construct the optimiser may not look into, it cannot show the
/// `delete` frees what the `new` took, so neither may go. Skipping the test
/// in an optimised build was the alternative and is the worse one: this trap
/// is how the project proves the audio thread does not allocate, and a build
/// where it is never armed is a build where nobody would notice it had
/// stopped working.
[[nodiscard]] int *opaque(int *pointer) noexcept
{
    __asm__ __volatile__("" : "+r"(pointer));
    return pointer;
}

} // namespace

TEST(AllocationTrap, CountsWhatHappensWhileItIsArmed)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    EXPECT_FALSE(NoAllocations::isArmed());
    {
        const NoAllocations guard(NoAllocations::OnAllocation::Count);
        EXPECT_TRUE(NoAllocations::isArmed());
        int *taken = opaque(new int(3));
        delete taken;
        EXPECT_GE(guard.count(), 2u) << "one for the new and one for the delete";
    }
    EXPECT_FALSE(NoAllocations::isArmed());
}

TEST(AllocationTrap, SeesNothingWhenNothingHappens)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    std::vector<float> ready(1000, 0.0f); // taken before the guard is armed
    const NoAllocations guard(NoAllocations::OnAllocation::Count);
    for (float &sample : ready) {
        sample += 1.0f;
    }
    EXPECT_EQ(guard.count(), 0u);
}

namespace
{

/// Runs a block that allocates while the trap is armed. A function of its
/// own because EXPECT_DEATH takes its statement as one macro argument, and
/// this one is full of commas.
void allocateOnARealTimePath()
{
    AllocatesWhileProcessing block;
    rb::dsp::AudioBuffer input(1, 64);
    rb::dsp::AudioBuffer output(1, 64);
    rb::dsp::ProcessContext context{input.block(64), output.block(64), 64};

    const NoAllocations realTime;
    block.process(context);
}

} // namespace

TEST(AllocationTrap, AbortsOnAnAllocationInsideProcess)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    // A death test, because that is what the trap does: it stops the binary
    // rather than letting a real-time path get away with it.
    EXPECT_DEATH(allocateOnARealTimePath(), "must not touch the allocator");
}

TEST(AllocationTrap, AWellBehavedBlockAllocatesNothingWhileItRuns)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    GainBlock block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());

    std::size_t allocations = 1;
    const MultiSignal output =
        runner.runWatchingForAllocations(spread(sine(4800, 1000.0), 2), CycleShape::random(1, 512, 2), allocations);
    EXPECT_EQ(allocations, 0u);
    EXPECT_EQ(output[0].size(), 4800u);
}

// --- audio files ---

TEST(AudioFile, WritesAndReadsBackWhatWasWritten)
{
    if (!audioFilesSupported()) {
        GTEST_SKIP() << "this build has no libsndfile";
    }
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "reverblabr-kit-roundtrip.wav";
    const MultiSignal written{sine(4800, 1000.0, kTestSampleRate, 0.5f), sine(4800, 2000.0, kTestSampleRate, 0.25f)};

    ASSERT_TRUE(writeWavFile(path.string(), written).has_value());
    const std::expected<AudioFile, AudioFileError> read = readAudioFile(path.string());
    ASSERT_TRUE(read.has_value()) << audioFileErrorName(read.error());

    EXPECT_EQ(read->sampleRate, kTestSampleRate);
    ASSERT_EQ(read->channels.size(), 2u);
    // Written as 32-bit floats, so it comes back sample for sample. Exact
    // equality rather than a null test, because a null test cannot say
    // anything below the silence floor.
    EXPECT_EQ(read->channels[0], written[0]);
    EXPECT_EQ(read->channels[1], written[1]);
    std::filesystem::remove(path);
}

TEST(AudioFile, SaysSoWhenThereIsNoSuchFile)
{
    if (!audioFilesSupported()) {
        GTEST_SKIP() << "this build has no libsndfile";
    }
    const std::expected<AudioFile, AudioFileError> read = readAudioFile("/nowhere/at/all.wav");
    ASSERT_FALSE(read.has_value());
    EXPECT_EQ(read.error(), AudioFileError::NotReadable);
}
