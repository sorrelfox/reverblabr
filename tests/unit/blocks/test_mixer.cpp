// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Mixer block: five strips summed exactly, mute and solo that
// mean what they say, and fades and scene recalls that take the time they
// are asked to take without a step anywhere.
//
// **A change is sent the way the runtime sends it**: written to the block
// with setParam() on the engine's side, and then the same numbers, in the
// same order, through applyParam() as the audio thread's queue delivers
// them. A fade is the ramp, the volume and the ramp put back.

#include "allocationtrap.h"
#include "analysis.h"
#include "blocks/mixer.h"
#include "blocksizeinvariance.h"
#include "offlinerunner.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace rb;
using namespace rb::blocks;
using namespace rb::dsp;
using namespace rb::test;

namespace
{

constexpr double kRate = kTestSampleRate;
constexpr std::uint32_t kCycle = 64;

/// A distinct steady level on each strip, a power of two apiece, so what
/// comes out says exactly which strips were heard.
constexpr std::array<float, Mixer::kStrips> kLevels{0.01f, 0.02f, 0.04f, 0.08f, 0.16f};

[[nodiscard]] MultiSignal steadyStrips(std::size_t frames)
{
    MultiSignal input;
    for (const float level : kLevels) {
        input.push_back(constant(frames, level));
        input.push_back(constant(frames, level));
    }
    return input;
}

/// The runtime's way: the value into the block on the engine's side, then
/// the same through the queue.
void send(Mixer &block, const std::vector<std::pair<std::string, float>> &changes)
{
    for (const auto &[id, value] : changes) {
        const std::size_t index = static_cast<std::size_t>(std::ranges::find(block.params(), id, &ParamDescriptor::id)
                                                           - block.params().begin());
        const ParamDescriptor &descriptor = block.params()[index];
        const ParamValue as =
            descriptor.type == ParamType::Bool ? ParamValue::ofBool(value != 0.0f) : ParamValue::ofFloat(value);
        EXPECT_TRUE(block.setParam(id, as).has_value()) << id;
    }
    for (const auto &[id, value] : changes) {
        const auto index = static_cast<std::uint32_t>(std::ranges::find(block.params(), id, &ParamDescriptor::id)
                                                      - block.params().begin());
        block.applyParam(index, value);
    }
}

/// A fade of strip \a strip (1 to 5), as the interface sends one.
void fade(Mixer &block, std::uint32_t strip, float to, float ms)
{
    const std::string n = std::to_string(strip);
    send(block, {{"ramp" + n, ms}, {"volume" + n, to}, {"ramp" + n, Mixer::kDefaultRampMs}});
}

/// Runs \a cycles cycles of the steady strips and keeps the left output.
[[nodiscard]] Signal runFor(BlockRunner &runner, std::size_t frames)
{
    Signal out;
    const MultiSignal input = steadyStrips(kCycle);
    for (std::size_t done = 0; done < frames; done += kCycle) {
        const MultiSignal cycle = runner.run(input, CycleShape::fixed(kCycle));
        out.insert(out.end(), cycle[0].begin(), cycle[0].end());
    }
    return out;
}

[[nodiscard]] std::size_t samplesOf(double ms)
{
    return static_cast<std::size_t>(std::lround(ms * 1.0e-3 * kRate));
}

} // namespace

TEST(MixerTest, ItTakesFiveStereoLanesAndPutsOutOne)
{
    const Mixer block;
    const PortSpec &inputs = block.info().inputs;
    EXPECT_EQ(inputs.minCount, 10U);
    EXPECT_EQ(inputs.maxCount, 10U) << "five lanes is the limit, and the compiler refuses a port past it";
    EXPECT_EQ(inputs.laneWidth, 2U);
    EXPECT_EQ(inputs.laneLabels, (std::vector<std::string>{"input1", "input2", "input3", "input4", "input5"}));
    EXPECT_EQ(block.info().outputs.maxCount, 2U);
    EXPECT_EQ(block.info().category, BlockCategory::Advanced);
    EXPECT_EQ(block.info().face, FaceKind::Value);

    // What a panel shows is the five strips and the master; the ramps and
    // the scenes are how the interface talks to the block, not settings.
    std::vector<std::string> shown;
    for (const ParamDescriptor &descriptor : block.params()) {
        if (!descriptor.flags.hidden) {
            shown.push_back(descriptor.id);
        }
    }
    EXPECT_EQ(shown.size(), 5U * 4U + 1U);
    EXPECT_EQ(shown.back(), "master");
    EXPECT_FLOAT_EQ(*block.param("volume3").asFloat(), 1.0f);
    EXPECT_FLOAT_EQ(*block.param("ramp3").asFloat(), Mixer::kDefaultRampMs);
    EXPECT_FALSE(*block.param("sceneCSaved").asBool());
    EXPECT_EQ(
        Mixer::indexOf(2, Mixer::Balance),
        static_cast<std::uint32_t>(std::ranges::find(block.params(), std::string{"balance3"}, &ParamDescriptor::id)
                                   - block.params().begin()));
    EXPECT_EQ(block.params()[Mixer::kMaster].id, "master");
    EXPECT_EQ(block.params()[Mixer::kFirstScene].id, "sceneA1");
}

TEST(MixerTest, TheSumIsExact)
{
    // The criterion: with every strip at 100 %, centred and heard, what
    // comes out is the five inputs added up, bit for bit.
    Mixer block;
    BlockRunner runner(block, 10, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    MultiSignal input;
    for (std::uint32_t port = 0; port < 10; ++port) {
        input.push_back(whiteNoise(4096, 0.3f, port + 1));
    }
    const MultiSignal out = runner.run(input, CycleShape::fixed(256));
    for (std::size_t frame = 0; frame < 4096; ++frame) {
        float left = 0.0f;
        float right = 0.0f;
        for (std::uint32_t strip = 0; strip < Mixer::kStrips; ++strip) {
            left += input[strip * 2][frame];
            right += input[strip * 2 + 1][frame];
        }
        ASSERT_EQ(out[0][frame], left) << "frame " << frame;
        ASSERT_EQ(out[1][frame], right) << "frame " << frame;
    }

    // And each strip at its own volume is that strip times its volume.
    send(block, {{"volume1", 2.0f}, {"volume2", 0.5f}, {"volume4", 0.0f}});
    const MultiSignal steady = steadyStrips(4096);
    const MultiSignal scaled = runner.run(steady, CycleShape::fixed(256));
    const float expected = kLevels[0] * 2.0f + kLevels[1] * 0.5f + kLevels[2] + kLevels[4];
    EXPECT_NEAR(scaled[0].back(), expected, 1e-7f);
    EXPECT_NEAR(scaled[1].back(), expected, 1e-7f);
}

TEST(MixerTest, MuteAndSoloMeanWhatTheySay)
{
    struct Case {
        const char *what;
        std::vector<std::pair<std::string, float>> set;
        std::array<bool, Mixer::kStrips> heard;
    };
    const std::vector<Case> cases{
        {"nothing set", {}, {true, true, true, true, true}},
        {"strip 2 muted", {{"mute2", 1}}, {true, false, true, true, true}},
        {"strip 3 soloed", {{"solo3", 1}}, {false, false, true, false, false}},
        {"solo is additive", {{"solo3", 1}, {"solo5", 1}}, {false, false, true, false, true}},
        {"a muted solo is silent", {{"solo3", 1}, {"mute3", 1}}, {false, false, false, false, false}},
        {"mute beside a solo", {{"solo1", 1}, {"solo2", 1}, {"mute2", 1}}, {true, false, false, false, false}},
        {"solo taken off again", {{"solo3", 1}, {"solo3", 0}}, {true, true, true, true, true}},
    };
    for (const Case &test : cases) {
        Mixer block;
        BlockRunner runner(block, 10, 2);
        ASSERT_TRUE(runner.prepare().has_value());
        send(block, test.set);
        const Signal out = runFor(runner, samplesOf(40.0));
        float expected = 0.0f;
        for (std::uint32_t strip = 0; strip < Mixer::kStrips; ++strip) {
            expected += test.heard[strip] ? kLevels[strip] : 0.0f;
        }
        EXPECT_NEAR(out.back(), expected, 1e-6f) << test.what;
    }
}

TEST(MixerTest, ABalanceTurnsOneSideOfItsStripDown)
{
    Mixer block;
    BlockRunner runner(block, 10, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    send(block, {{"balance1", -1.0f}, {"balance2", 0.5f}});
    const MultiSignal out = runner.run(steadyStrips(4096), CycleShape::fixed(256));
    const float rest = kLevels[2] + kLevels[3] + kLevels[4];
    // Fully left: strip 1's right is gone. Half right: strip 2's left is half.
    EXPECT_NEAR(out[0].back(), kLevels[0] + kLevels[1] * 0.5f + rest, 1e-7f);
    EXPECT_NEAR(out[1].back(), kLevels[1] + rest, 1e-7f);
}

TEST(MixerTest, AFadeTakesTheTimeItIsAskedForAndNeverSteps)
{
    // The criterion: 0.5 s, 2 s with Shift, and a scene recalled over 1 s,
    // each within 5 % — measured from the audio, on strip 5 alone.
    for (const float ms : {Mixer::kFadeMs, Mixer::kSlowFadeMs, Mixer::kSceneRecallMs}) {
        Mixer block;
        BlockRunner runner(block, 10, 2);
        ASSERT_TRUE(runner.prepare().has_value());
        send(block, {{"solo5", 1}});
        (void)runFor(runner, samplesOf(40.0));

        fade(block, 5, 0.0f, ms);
        const Signal out = runFor(runner, samplesOf(ms * 1.5));
        const auto silentAt = static_cast<std::size_t>(
            std::ranges::find_if(out, [](float sample) { return sample <= 0.0f; }) - out.begin());
        const double took = 1000.0 * static_cast<double>(silentAt + 1) / kRate;
        EXPECT_NEAR(took, ms, ms * 0.05) << "a fade of " << ms << " ms";
        // A straight line: never more than the whole distance over the
        // whole time in one sample.
        EXPECT_LE(maxSampleStep(out), kLevels[4] / static_cast<float>(samplesOf(ms)) * 1.01f) << ms;

        // And back up to 100 % over the same time.
        fade(block, 5, 1.0f, ms);
        const Signal up = runFor(runner, samplesOf(ms * 1.5));
        const auto fullAt = static_cast<std::size_t>(
            std::ranges::find_if(up, [](float sample) { return sample >= kLevels[4]; }) - up.begin());
        EXPECT_NEAR(1000.0 * static_cast<double>(fullAt + 1) / kRate, ms, ms * 0.05) << "back up over " << ms;
    }
}

TEST(MixerTest, TheRampIsTheOneSentWithTheVolumeNotTheOneLeftInTheStore)
{
    // All three values arrive before the next cycle starts, so by the top
    // of it the store has the ramp put back to 20 ms. The fade has to take
    // the time it was sent with all the same.
    Mixer block;
    BlockRunner runner(block, 10, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    send(block, {{"solo1", 1}});
    (void)runFor(runner, samplesOf(40.0));
    fade(block, 1, 0.0f, Mixer::kFadeMs);
    const Signal out = runFor(runner, samplesOf(100.0));
    EXPECT_GT(out.back(), kLevels[0] * 0.7f) << "the fade took the 20 ms left in the store";
}

TEST(MixerTest, ASliderDraggedMovesQuicklyAndAFadeCanBeCaughtMidway)
{
    Mixer block;
    BlockRunner runner(block, 10, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    send(block, {{"solo1", 1}});
    (void)runFor(runner, samplesOf(40.0));

    // A drag is a volume on its own, over the default 20 ms.
    send(block, {{"volume1", 0.5f}});
    const Signal dragged = runFor(runner, samplesOf(25.0));
    EXPECT_NEAR(dragged.back(), kLevels[0] * 0.5f, 1e-7f);

    // A fade under way, and then a drag: the drag takes over from wherever
    // the fade had got to, with no step.
    fade(block, 1, 0.0f, Mixer::kSlowFadeMs);
    Signal both = runFor(runner, samplesOf(500.0));
    send(block, {{"volume1", 1.0f}});
    const Signal after = runFor(runner, samplesOf(40.0));
    both.insert(both.end(), after.begin(), after.end());
    EXPECT_NEAR(both.back(), kLevels[0], 1e-7f);
    EXPECT_LT(maxSampleStep(both), kLevels[0] / static_cast<float>(samplesOf(Mixer::kDefaultRampMs)) * 1.01f);
}

TEST(MixerTest, TheMasterIsOverEveryStrip)
{
    Mixer block;
    BlockRunner runner(block, 10, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    send(block, {{"master", 0.25f}});
    const Signal out = runFor(runner, samplesOf(40.0));
    float sum = 0.0f;
    for (const float level : kLevels) {
        sum += level;
    }
    EXPECT_NEAR(out.back(), sum * 0.25f, 1e-7f);
}

TEST(MixerTest, ScenesAreKeptWithTheBlock)
{
    Mixer block;
    ASSERT_TRUE(block.setParam("sceneB2", ParamValue::ofFloat(0.3f)).has_value());
    ASSERT_TRUE(block.setParam("sceneBSaved", ParamValue::ofBool(true)).has_value());
    ASSERT_TRUE(block.setParam("volume4", ParamValue::ofFloat(1.8f)).has_value());
    ASSERT_TRUE(block.setParam("solo2", ParamValue::ofBool(true)).has_value());

    Mixer opened;
    ASSERT_TRUE(opened.loadState(block.saveState()).has_value());
    EXPECT_FLOAT_EQ(*opened.param("sceneB2").asFloat(), 0.3f);
    EXPECT_TRUE(*opened.param("sceneBSaved").asBool());
    EXPECT_FLOAT_EQ(*opened.param("volume4").asFloat(), 1.8f);
    EXPECT_TRUE(*opened.param("solo2").asBool());
    EXPECT_FALSE(block.setParam("volume1", ParamValue::ofFloat(2.5f)).has_value()) << "past 200 %";
}

TEST(MixerTest, ItSoundsTheSameWhateverTheQuantum)
{
    MultiSignal input;
    for (std::uint32_t port = 0; port < 10; ++port) {
        input.push_back(sine(16384, 200.0 + 150.0 * port, kRate, 0.2f));
    }
    InvarianceOptions options;
    options.inputChannels = 10;
    options.outputChannels = 2;
    const InvarianceReport report = checkBlockSizeInvariance(
        [] {
            auto block = std::make_unique<Mixer>();
            EXPECT_TRUE(block->setParam("volume2", ParamValue::ofFloat(1.5f)).has_value());
            EXPECT_TRUE(block->setParam("balance3", ParamValue::ofFloat(-0.4f)).has_value());
            EXPECT_TRUE(block->setParam("mute4", ParamValue::ofBool(true)).has_value());
            EXPECT_TRUE(block->setParam("master", ParamValue::ofFloat(0.7f)).has_value());
            return std::unique_ptr<Block>(std::move(block));
        },
        input,
        options);
    EXPECT_TRUE(report.passed) << report.summary;
}

TEST(MixerTest, ARunTouchesTheAllocatorNotAtAll)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    Mixer block;
    BlockRunner runner(block, 10, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    fade(block, 2, 0.0f, Mixer::kFadeMs);
    std::size_t allocations = 1;
    const MultiSignal out =
        runner.runWatchingForAllocations(steadyStrips(9600), CycleShape::random(1, 512, 3), allocations);
    EXPECT_EQ(allocations, 0U);
    EXPECT_EQ(out[0].size(), 9600U);
}
