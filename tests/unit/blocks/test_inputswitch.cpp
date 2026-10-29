// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Input Switch block: one of two inputs passed, and an
// equal-power crossfade between them that takes the time it was sent with
// and never clicks.
//
// **A switch is sent the way the runtime sends it**: the values written
// into the block on the engine's side, then the same through applyParam()
// in the order the queue delivers them — the time, the input, the time put
// back.

#include "allocationtrap.h"
#include "analysis.h"
#include "blocks/inputswitch.h"
#include "blocksizeinvariance.h"
#include "dsp/decibel.h"
#include "offlinerunner.h"
#include "rt/meter.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace rb;
using namespace rb::blocks;
using namespace rb::dsp;
using namespace rb::test;

namespace
{

constexpr double kRate = kTestSampleRate;
constexpr std::uint32_t kCycle = 128;

[[nodiscard]] std::size_t samplesOf(double ms)
{
    return static_cast<std::size_t>(std::lround(ms * 1.0e-3 * kRate));
}

/// A switch to \a input over \a ms, as a click or a Shift-click sends one.
void flip(InputSwitch &block, const std::string &input, float ms)
{
    EXPECT_TRUE(block.setParam("crossfade", ParamValue::ofFloat(ms)).has_value());
    EXPECT_TRUE(block.setParam("input", ParamValue::ofEnum(input)).has_value());
    EXPECT_TRUE(block.setParam("crossfade", ParamValue::ofFloat(InputSwitch::kFadeMs)).has_value());
    block.applyParam(InputSwitch::Crossfade, ms);
    block.applyParam(InputSwitch::Input, input == "b" ? 1.0f : 0.0f);
    block.applyParam(InputSwitch::Crossfade, InputSwitch::kFadeMs);
}

/// Runs \a input through in cycles, from \a from, and keeps the left side.
[[nodiscard]] Signal runFrom(BlockRunner &runner, const MultiSignal &input, std::size_t from, std::size_t frames)
{
    Signal out;
    for (std::size_t at = from; at < from + frames; at += kCycle) {
        MultiSignal cycle;
        for (const Signal &channel : input) {
            cycle.emplace_back(channel.begin() + static_cast<std::ptrdiff_t>(at),
                               channel.begin() + static_cast<std::ptrdiff_t>(at + kCycle));
        }
        const MultiSignal done = runner.run(cycle, CycleShape::fixed(kCycle));
        out.insert(out.end(), done[0].begin(), done[0].end());
    }
    return out;
}

/// A on both sides of the first lane, B on both of the second.
[[nodiscard]] MultiSignal lanes(const Signal &a, const Signal &b)
{
    return {a, a, b, b};
}

} // namespace

TEST(InputSwitchTest, ItTakesInputsAAndBAndPassesA)
{
    const InputSwitch block;
    EXPECT_EQ(block.info().inputs.maxCount, 4U);
    EXPECT_EQ(block.info().inputs.laneWidth, 2U);
    EXPECT_EQ(block.info().inputs.laneLabels, (std::vector<std::string>{"a", "b"}));
    EXPECT_EQ(block.info().outputs.maxCount, 2U);
    EXPECT_EQ(block.info().face, FaceKind::Text);
    EXPECT_EQ(block.param("input").text(), "a");
    EXPECT_FLOAT_EQ(*block.param("crossfade").asFloat(), InputSwitch::kFadeMs);
    EXPECT_TRUE(block.params()[InputSwitch::Crossfade].flags.hidden);
    EXPECT_FALSE(block.params()[InputSwitch::Input].flags.hidden) << "a command line has to be able to switch it";

    // Settled, the input passes bit for bit and the other is not heard.
    InputSwitch passing;
    BlockRunner runner(passing, 4, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const Signal a = whiteNoise(4096, 0.5f, 1);
    const Signal b = whiteNoise(4096, 0.5f, 2);
    const MultiSignal out = runner.run(lanes(a, b), CycleShape::fixed(256));
    EXPECT_EQ(out[0], a);
    EXPECT_EQ(out[1], a);
}

TEST(InputSwitchTest, TheCrossfadeKeepsThePowerAndTakesItsTime)
{
    // The criterion: two unrelated signals of the same loudness, switched
    // from one to the other, come out at that loudness all the way across
    // (±0.5 dB), and the fade is over in the time it was sent with.
    for (const float ms : {InputSwitch::kFadeMs, InputSwitch::kSlowFadeMs}) {
        const std::size_t total = samplesOf(ms) + samplesOf(600.0);
        const Signal a = whiteNoise(total, 0.3f, 7);
        const Signal b = whiteNoise(total, 0.3f, 8);
        InputSwitch block;
        BlockRunner runner(block, 4, 2);
        ASSERT_TRUE(runner.prepare().has_value());
        const std::size_t before = samplesOf(100.0) / kCycle * kCycle;
        (void)runFrom(runner, lanes(a, b), 0, before);
        flip(block, "b", ms);
        const Signal out = runFrom(runner, lanes(a, b), before, (total - before) / kCycle * kCycle);

        const double reference = rmsDb(Signal(a.begin(), a.begin() + static_cast<std::ptrdiff_t>(samplesOf(ms))));
        // Long enough that the noise itself holds still within a tenth of
        // a decibel from one window to the next.
        const std::size_t window = samplesOf(50.0);
        double lowest = 1e9;
        double highest = -1e9;
        for (std::size_t at = 0; at + window <= samplesOf(ms); at += window) {
            const double level = rmsDb(Signal(out.begin() + static_cast<std::ptrdiff_t>(at),
                                              out.begin() + static_cast<std::ptrdiff_t>(at + window)));
            lowest = std::min(lowest, level);
            highest = std::max(highest, level);
        }
        EXPECT_GT(lowest, reference - 0.5) << "the fade of " << ms << " ms dipped";
        EXPECT_LT(highest, reference + 0.5) << "the fade of " << ms << " ms swelled";

        // Over when it said: from then on, B alone and exactly.
        const std::size_t done = samplesOf(ms);
        std::size_t firstExact = out.size();
        for (std::size_t at = 0; at < out.size(); ++at) {
            if (out[at] == b[before + at]) {
                bool rest = true;
                for (std::size_t later = at; later < std::min(out.size(), at + 256); ++later) {
                    rest = rest && out[later] == b[before + later];
                }
                if (rest) {
                    firstExact = at;
                    break;
                }
            }
        }
        const double took = 1000.0 * static_cast<double>(firstExact) / kRate;
        EXPECT_NEAR(took, ms, ms * 0.05) << "a crossfade sent as " << ms << " ms";
        EXPECT_NEAR(static_cast<double>(firstExact), static_cast<double>(done), 2.0);
    }
}

TEST(InputSwitchTest, SwitchingDuringSilenceOrAHeavySignalDoesNotClick)
{
    // Heavy: A at +0.9 and B at −0.9, the widest step there is. The output
    // walks from one to the other along the fade and never jumps.
    const std::size_t total = samplesOf(1200.0) / kCycle * kCycle;
    InputSwitch block;
    BlockRunner runner(block, 4, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const MultiSignal heavy = lanes(constant(total, 0.9f), constant(total, -0.9f));
    (void)runFrom(runner, heavy, 0, kCycle * 4);
    flip(block, "b", InputSwitch::kFadeMs);
    Signal out = runFrom(runner, heavy, kCycle * 4, kCycle * 100);
    // And switched back half way through, which turns round where it is.
    flip(block, "a", InputSwitch::kFadeMs);
    const Signal back = runFrom(runner, heavy, kCycle * 104, total - kCycle * 104);
    out.insert(out.end(), back.begin(), back.end());

    // The steepest a quarter turn over half a second takes a ±0.9 pair.
    const float steepest =
        0.9f * std::sqrt(2.0f) * std::numbers::pi_v<float> / 2.0f / static_cast<float>(samplesOf(InputSwitch::kFadeMs));
    EXPECT_LT(maxSampleStep(out), steepest * 1.01f) << "the switch stepped";
    EXPECT_FLOAT_EQ(out.back(), 0.9f) << "it did not come back to A";

    // Silence: nothing in, nothing out, switched or not.
    InputSwitch quiet;
    BlockRunner silent(quiet, 4, 2);
    ASSERT_TRUE(silent.prepare().has_value());
    flip(quiet, "b", InputSwitch::kFadeMs);
    const MultiSignal nothing = silent.run(lanes(silence(9600), silence(9600)), CycleShape::fixed(kCycle));
    EXPECT_FLOAT_EQ(peak(nothing[0]), 0.0f);
}

TEST(InputSwitchTest, TheTimeIsTheOneSentWithTheSwitch)
{
    // By the top of the next cycle the store has the time put back to half
    // a second; a two-second switch has to take its two seconds all the
    // same.
    InputSwitch block;
    BlockRunner runner(block, 4, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    flip(block, "b", InputSwitch::kSlowFadeMs);
    (void)runner.run(lanes(silence(samplesOf(1000.0)), silence(samplesOf(1000.0))), CycleShape::fixed(kCycle));
    EXPECT_NEAR(block.position(), 0.5f, 0.01f) << "the switch did not take the time it was sent with";
}

TEST(InputSwitchTest, ItSaysWhereTheCrossfadeIs)
{
    InputSwitch block;
    rt::BlockMeter meter;
    BlockRunner runner(block, 4, 2);
    runner.setMeter(&meter);
    ASSERT_TRUE(runner.prepare().has_value());
    (void)runner.run(lanes(silence(256), silence(256)), CycleShape::fixed(256));
    EXPECT_FLOAT_EQ(meter.value(InputSwitch::kPositionValue), 0.0f);
    flip(block, "b", 0.0f);
    (void)runner.run(lanes(silence(256), silence(256)), CycleShape::fixed(256));
    EXPECT_FLOAT_EQ(meter.value(InputSwitch::kPositionValue), 1.0f);
}

TEST(InputSwitchTest, ItStartsWhereTheSessionLeftIt)
{
    InputSwitch block;
    ASSERT_TRUE(block.setParam("input", ParamValue::ofEnum("b")).has_value());
    InputSwitch opened;
    ASSERT_TRUE(opened.loadState(block.saveState()).has_value());
    BlockRunner runner(opened, 4, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const Signal b = whiteNoise(1024, 0.5f, 3);
    const MultiSignal out = runner.run(lanes(whiteNoise(1024, 0.5f, 4), b), CycleShape::fixed(256));
    EXPECT_EQ(out[0], b) << "a session saved on B opened on A, or faded to it";
}

TEST(InputSwitchTest, ItSoundsTheSameWhateverTheQuantum)
{
    MultiSignal input{sine(16384, 440.0, kRate, 0.5f),
                      sine(16384, 550.0, kRate, 0.5f),
                      sine(16384, 1300.0, kRate, 0.4f),
                      sine(16384, 1700.0, kRate, 0.4f)};
    InvarianceOptions options;
    options.inputChannels = 4;
    options.outputChannels = 2;
    const InvarianceReport report = checkBlockSizeInvariance(
        [] {
            // On B, where a session saved on B opens. The fade itself is
            // per sample and is checked across cycle joins above.
            auto block = std::make_unique<InputSwitch>();
            EXPECT_TRUE(block->setParam("input", ParamValue::ofEnum("b")).has_value());
            return std::unique_ptr<Block>(std::move(block));
        },
        input,
        options);
    EXPECT_TRUE(report.passed) << report.summary;
}

TEST(InputSwitchTest, ARunTouchesTheAllocatorNotAtAll)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    InputSwitch block;
    BlockRunner runner(block, 4, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    flip(block, "b", 50.0f);
    std::size_t allocations = 1;
    const MultiSignal out =
        runner.runWatchingForAllocations(lanes(sine(9600, 440.0, kRate, 0.5f), sine(9600, 660.0, kRate, 0.5f)),
                                         CycleShape::random(1, 512, 3),
                                         allocations);
    EXPECT_EQ(allocations, 0U);
    EXPECT_EQ(out[0].size(), 9600U);
}
