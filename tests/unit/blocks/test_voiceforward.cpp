// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Voice Forward: at 0 dB and switched off the sound is the input to the bit,
// even a film that peaks at full scale; whatever the settings do nothing passes
// the ceiling and nothing rises faster than the limit; the cases a stereo sound
// can be in end in the right cut and the right word; at every quantum and rate.

#include "allocationtrap.h"
#include "blocks/voiceforward.h"
#include "blocksizeinvariance.h"
#include "dsp/decibel.h"
#include "dsp/paramvalue.h"
#include "graph/compiledgraph.h"
#include "offlinerunner.h"
#include "rt/meter.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

using namespace rb;
using namespace rb::blocks;
using namespace rb::dsp;
using namespace rb::test;

namespace
{

constexpr std::size_t kSecond = 48000;
constexpr std::size_t kDelay = 1096;
constexpr std::array<std::uint32_t, 5> kQuanta{1, 64, 256, 1024, 8192};
constexpr float kCeiling = 0.89125f;
constexpr float kEnough = 1.0e-4f;

void set(VoiceForward &block, std::string_view id, float value)
{
    ASSERT_TRUE(block.setParam(id, ParamValue::ofFloat(value)).has_value()) << id;
}

void hold(VoiceForward &block, bool on)
{
    ASSERT_TRUE(block.setParam("holdLevel", ParamValue::ofBool(on)).has_value());
}

struct Rig {
    VoiceForward block;
    rt::BlockMeter meter;
    std::unique_ptr<BlockRunner> runner;

    /// A single cut, with no lift: what most of these are about.
    explicit Rig(float background = 0.0f, double rate = kTestSampleRate)
    {
        set(block, "background", background);
        set(block, "betweenVoices", background);
        hold(block, false);
        start(rate);
    }

    struct TwoPart {
    };

    Rig(TwoPart, float background, float between, bool holdLevel = false)
    {
        set(block, "background", background);
        set(block, "betweenVoices", between);
        hold(block, holdLevel);
        start(kTestSampleRate);
    }

    void start(double rate)
    {
        runner = std::make_unique<BlockRunner>(block, 2, 2, rate, 8192);
        runner->setMeter(&meter);
        EXPECT_TRUE(runner->prepare().has_value());
    }

    MultiSignal feed(const MultiSignal &signal, std::uint32_t quantum = 256)
    {
        return runner->run(signal, CycleShape::fixed(quantum));
    }
};

/// Hands on what the audio system gives it: what a source does in a session.
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
            for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
                out[frame] = in[frame];
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

/// A band of noise a film could hold: next to nothing under 100 Hz or over
/// 12 kHz, which the mask leaves out.
[[nodiscard]] Signal bandNoise(std::size_t frames, float amplitude, std::uint32_t seed)
{
    Signal noise = whiteNoise(frames, 1.0f, seed);
    constexpr float kLow = 0.04f;
    constexpr float kHigh = 0.3f;
    std::array<float, 4> fast{};
    std::array<float, 2> slow{};
    for (float &sample : noise) {
        float value = sample;
        for (float &pole : fast) {
            pole += kHigh * (value - pole);
            value = pole;
        }
        for (float &pole : slow) {
            pole += kLow * (value - pole);
            value -= pole;
        }
        sample = value;
    }
    float peak = 0.0f;
    for (std::size_t n = 2048; n < noise.size(); ++n) {
        peak = std::max(peak, std::fabs(noise[n]));
    }
    for (float &sample : noise) {
        sample *= amplitude / peak;
    }
    return noise;
}

/// Speech-like bursts in the centre over wide noise, swelling, that reach full
/// scale: the kind of thing a film hands the block.
[[nodiscard]] MultiSignal filmLike(std::size_t frames, std::uint32_t seed)
{
    const Signal centre = bandNoise(frames, 1.0f, seed);
    const Signal left = pinkNoise(frames, 1.0f, seed + 1);
    const Signal right = pinkNoise(frames, 1.0f, seed + 2);
    MultiSignal out{Signal(frames), Signal(frames)};
    float peak = 0.0f;
    for (std::size_t n = 0; n < frames; ++n) {
        const double seconds = static_cast<double>(n) / kTestSampleRate;
        const auto talk = static_cast<float>(0.5 + 0.5 * std::sin(2.0 * std::numbers::pi * 0.7 * seconds));
        const auto swell = static_cast<float>(0.6 + 0.4 * std::sin(2.0 * std::numbers::pi * 0.11 * seconds));
        out[0][n] = talk * centre[n] + swell * left[n];
        out[1][n] = talk * centre[n] + swell * right[n];
        peak = std::max({peak, std::fabs(out[0][n]), std::fabs(out[1][n])});
    }
    for (Signal &side : out) {
        for (float &sample : side) {
            sample /= peak;
        }
    }
    return out;
}

void expectDelayedExactly(const Signal &out, const Signal &in, std::size_t delay = kDelay, std::size_t from = 0)
{
    ASSERT_EQ(out.size(), in.size());
    for (std::size_t n = std::max(delay, from); n < out.size(); ++n) {
        ASSERT_EQ(out[n], in[n - delay]) << "at " << n;
    }
}

[[nodiscard]] float levelDb(const Signal &signal, std::size_t from, std::size_t to)
{
    double sum = 0.0;
    for (std::size_t n = from; n < to; ++n) {
        sum += static_cast<double>(signal[n]) * signal[n];
    }
    return static_cast<float>(10.0 * std::log10(std::max(sum / static_cast<double>(to - from), 1.0e-20)));
}

/// How much lower the output is than the input, over the last \a seconds.
[[nodiscard]] float changeDb(const Signal &out, const Signal &in, std::size_t seconds = 1)
{
    const std::size_t from = out.size() - seconds * kSecond;
    return levelDb(out, from, out.size()) - levelDb(in, from - kDelay, in.size() - kDelay);
}

} // namespace

TEST(VoiceForward, DelaysByAFrameAndTheLimiterAndDeclaresThatItAdds)
{
    Rig rig;
    EXPECT_EQ(rig.block.info().latencySamples, kDelay);
    EXPECT_EQ(rig.block.info().gainReach, GainReach::Adds);
    EXPECT_EQ(rig.block.info().category, BlockCategory::Effects);
}

TEST(VoiceForward, TheDefaultsChangeNothing)
{
    VoiceForward block;
    EXPECT_EQ(block.param("background").asFloat(), 0.0f);
    EXPECT_EQ(block.param("betweenVoices").asFloat(), VoiceForward::kDefaultBetweenDb);
    EXPECT_EQ(block.param("holdLevel").asBool(), true);
}

TEST(VoiceForward, NullAtNothingOnASignalThatPeaksAtFullScale)
{
    const MultiSignal film = filmLike(kSecond * 4, 3);
    for (const std::uint32_t quantum : kQuanta) {
        for (const bool holdLevel : {false, true}) {
            Rig rig(Rig::TwoPart{}, 0.0f, VoiceForward::kDefaultBetweenDb, holdLevel);
            const MultiSignal out = rig.feed(film, quantum);
            expectDelayedExactly(out[0], film[0]);
            expectDelayedExactly(out[1], film[1]);
            EXPECT_TRUE(rig.block.neutral());
            EXPECT_EQ(rig.block.liftDb(), 0.0f);
        }
    }
}

TEST(VoiceForward, NullSwitchedOffWhateverItIsSetTo)
{
    // Switched off and held off are one state in the graph: the runtime takes
    // "enabled and not held" for a node's enabled.
    const MultiSignal film = filmLike(kSecond, 4);
    VoiceForward block;
    set(block, "background", -24.0f);
    PrepareSpec prepare;
    prepare.maxBlockFrames = 256;
    ASSERT_TRUE(block.prepare(prepare).has_value());
    graph::NodeSpec node;
    node.id = "forward";
    node.block = &block;
    node.enabled = false;
    StereoIn source;
    graph::GraphSpec spec;
    spec.maxBlockFrames = 256;
    spec.nodes.push_back(graph::NodeSpec{"source", &source, true});
    spec.nodes.push_back(node);
    for (std::uint32_t side = 0; side < 2; ++side) {
        spec.edges.push_back(graph::EdgeSpec{0, side, 1, side});
    }
    auto compiled = graph::CompiledGraph::compile(spec);
    ASSERT_TRUE(compiled.has_value()) << graph::graphErrorName(compiled.error());
    GraphRunner runner(*compiled);
    runner.feed(0, 0, film[0]);
    runner.feed(0, 1, film[1]);
    runner.capture(1, 0);
    runner.capture(1, 1);
    runner.run(film[0].size());
    expectDelayedExactly(runner.captured(1, 0), film[0]);
    expectDelayedExactly(runner.captured(1, 1), film[1]);
}

TEST(VoiceForward, SoundsTheSameAtAnyQuantum)
{
    const MultiSignal film = filmLike(kSecond * 2, 5);
    InvarianceOptions options;
    options.inputChannels = 2;
    options.outputChannels = 2;
    options.maxBlockFrames = 8192;
    const InvarianceReport report = checkBlockSizeInvariance(
        [] {
            auto block = std::make_unique<VoiceForward>();
            EXPECT_TRUE(block->setParam("background", ParamValue::ofFloat(-18.0f)).has_value());
            return block;
        },
        film,
        options);
    EXPECT_TRUE(report.passed) << report.summary;

    Rig one(Rig::TwoPart{}, -18.0f, -3.0f, true);
    const MultiSignal first = one.feed(film, 1);
    for (const std::uint32_t quantum : kQuanta) {
        Rig other(Rig::TwoPart{}, -18.0f, -3.0f, true);
        const MultiSignal out = other.feed(film, quantum);
        for (std::size_t n = 0; n < out[0].size(); ++n) {
            ASSERT_EQ(out[0][n], first[0][n]) << quantum << " at " << n;
        }
    }
}

TEST(VoiceForward, TheCeilingMovesAtTheSameSampleAtAnyQuantum)
{
    // The ceiling moves only when the block leaves nothing or comes back to
    // it, so this has to cross that, on a sound over the ceiling.
    const MultiSignal film{whiteNoise(kSecond * 2, 1.0f, 43), whiteNoise(kSecond * 2, 1.0f, 44)};
    const auto half = static_cast<std::ptrdiff_t>(kSecond);
    const MultiSignal first{Signal(film[0].begin(), film[0].begin() + half),
                            Signal(film[1].begin(), film[1].begin() + half)};
    const MultiSignal second{Signal(film[0].begin() + half, film[0].end()),
                             Signal(film[1].begin() + half, film[1].end())};
    const auto runAt = [&](std::uint32_t quantum) {
        Rig rig(0.0f);
        (void)rig.feed(first, quantum);
        rig.block.applyParam(VoiceForward::Background, -18.0f);
        rig.block.applyParam(VoiceForward::BetweenVoices, -18.0f);
        return rig.feed(second, quantum);
    };
    // Whole hops, so the change arrives at the same sample in every run.
    const MultiSignal one = runAt(256);
    for (const std::uint32_t quantum : {64U, 128U, 2048U}) {
        const MultiSignal out = runAt(quantum);
        for (std::size_t n = 0; n < out[0].size(); ++n) {
            ASSERT_EQ(out[0][n], one[0][n]) << quantum << " at " << n;
        }
    }
}

TEST(VoiceForward, ARunTouchesTheAllocatorNotAtAll)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    VoiceForward block;
    set(block, "background", -18.0f);
    BlockRunner runner(block, 2, 2, kTestSampleRate, 2048);
    ASSERT_TRUE(runner.prepare().has_value());
    const MultiSignal film = filmLike(kSecond * 2, 6);
    std::size_t allocations = 1;
    (void)runner.runWatchingForAllocations(film, CycleShape::random(1, 2048, 3), allocations);
    EXPECT_EQ(allocations, 0U);
    EXPECT_LT(block.cutDb(), -1.0f) << "the cut never moved, so this tested little";
}

TEST(VoiceForward, ItReadsBothSidesBeforeWritingEither)
{
    VoiceForward shared;
    VoiceForward apart;
    set(shared, "background", -12.0f);
    set(apart, "background", -12.0f);
    set(shared, "betweenVoices", -12.0f);
    set(apart, "betweenVoices", -12.0f);
    PrepareSpec spec;
    ASSERT_TRUE(shared.prepare(spec).has_value());
    ASSERT_TRUE(apart.prepare(spec).has_value());
    constexpr std::uint32_t kFrames = 4096;
    for (int cycle = 0; cycle < 12; ++cycle) {
        Signal left = whiteNoise(kFrames, 0.5f, 40 + static_cast<std::uint32_t>(cycle));
        Signal right = whiteNoise(kFrames, 0.5f, 60 + static_cast<std::uint32_t>(cycle));
        Signal apartLeft = left;
        Signal apartRight = right;
        std::vector<float> outLeft(kFrames);
        std::vector<float> outRight(kFrames);
        std::array<float *, 2> in{apartLeft.data(), apartRight.data()};
        std::array<float *, 2> out{outLeft.data(), outRight.data()};
        ProcessContext separate{AudioBlock{in.data(), 2, kFrames}, AudioBlock{out.data(), 2, kFrames}, kFrames};
        apart.process(separate);
        std::array<float *, 2> both{left.data(), right.data()};
        ProcessContext together{AudioBlock{both.data(), 2, kFrames}, AudioBlock{both.data(), 2, kFrames}, kFrames};
        shared.process(together);
        for (std::size_t n = 0; n < kFrames; ++n) {
            ASSERT_EQ(left[n], outLeft[n]);
            ASSERT_EQ(right[n], outRight[n]);
        }
    }
    EXPECT_LT(shared.cutDb(), -11.0f);
}

TEST(VoiceForward, TheCutsAreClampedByEveryRoute)
{
    VoiceForward block;
    EXPECT_FALSE(block.setParam("background", ParamValue::ofFloat(-25.0f)).has_value());
    EXPECT_FALSE(block.setParam("background", ParamValue::ofFloat(1.0f)).has_value());
    EXPECT_FALSE(block.setParam("betweenVoices", ParamValue::ofFloat(-25.0f)).has_value());

    Rig rig(-12.0f);
    rig.block.applyParam(VoiceForward::Background, -200.0f);
    rig.block.applyParam(VoiceForward::BetweenVoices, -200.0f);
    const Signal noise = whiteNoise(kSecond, 0.3f, 8);
    (void)rig.feed(MultiSignal{noise, whiteNoise(kSecond, 0.3f, 9)});
    EXPECT_EQ(rig.block.cutDb(), VoiceForward::kLowestCutDb);
}

TEST(VoiceForward, BetweenVoicesStopsAtBackground)
{
    const MultiSignal wide{whiteNoise(kSecond * 2, 0.3f, 10), whiteNoise(kSecond * 2, 0.3f, 11)};
    Rig rig(Rig::TwoPart{}, -6.0f, -18.0f);
    (void)rig.feed(wide);
    EXPECT_EQ(rig.block.cutDb(), -6.0f);
}

TEST(VoiceForward, BothSidesAlikeLeavesNothingToMoveInTheBand)
{
    // The mask stops at 100 Hz and 12 kHz: only what lies outside that band
    // can move.
    const Signal mono = bandNoise(kSecond * 7, 0.5f, 12);
    Rig rig(-24.0f);
    const MultiSignal out = rig.feed(MultiSignal{mono, mono});
    EXPECT_EQ(rig.block.found(), VoiceForward::Alike);
    EXPECT_NEAR(changeDb(out[0], mono), 0.0f, 0.6f);
    EXPECT_EQ(rig.meter.value(VoiceForward::kFoundValue), static_cast<float>(VoiceForward::Alike));

    const Signal low = sine(kSecond * 7, 40.0, kTestSampleRate, 0.5f);
    Rig under(-24.0f);
    const MultiSignal lowOut = under.feed(MultiSignal{low, low});
    EXPECT_NEAR(changeDb(lowOut[0], low), -24.0f, 1.0f) << "under the band a mono sound still moves";
}

TEST(VoiceForward, ADryLineAloneIsNotBothSidesAlike)
{
    const Signal mono = bandNoise(kSecond * 4, 0.5f, 13);
    Rig rig(-12.0f);
    (void)rig.feed(MultiSignal{mono, mono});
    EXPECT_EQ(rig.block.found(), VoiceForward::Centre);
}

TEST(VoiceForward, OneSideSilentIsWideAndFallsByBetweenVoices)
{
    const Signal left = bandNoise(kSecond * 3, 0.5f, 14);
    const Signal right = silence(kSecond * 3);
    Rig rig(Rig::TwoPart{}, -12.0f, -3.0f);
    const MultiSignal out = rig.feed(MultiSignal{left, right});
    EXPECT_EQ(rig.block.found(), VoiceForward::Wide);
    EXPECT_EQ(rig.block.cutDb(), -3.0f);
    EXPECT_NEAR(changeDb(out[0], left), -3.0f, 0.1f);
}

TEST(VoiceForward, ReversedPolarityIsWideAndFalls)
{
    const Signal left = bandNoise(kSecond * 3, 0.5f, 15);
    Signal right = left;
    for (float &sample : right) {
        sample = -sample;
    }
    Rig rig(Rig::TwoPart{}, -12.0f, -3.0f);
    const MultiSignal out = rig.feed(MultiSignal{left, right});
    EXPECT_EQ(rig.block.found(), VoiceForward::Wide);
    EXPECT_NEAR(changeDb(out[0], left), -3.0f, 0.1f);
    EXPECT_NEAR(changeDb(out[1], right), -3.0f, 0.1f);
}

TEST(VoiceForward, APannedSourceFoldsTowardsTheMiddle)
{
    const Signal left = bandNoise(kSecond * 3, 0.5f, 16);
    for (const auto &[pan, wantLeft, wantRight] : {std::tuple{1.0f, 0.0f, 0.0f},
                                                   std::tuple{0.7f, -1.0f, 1.3f},
                                                   std::tuple{0.5f, -1.8f, 2.8f},
                                                   std::tuple{0.25f, -2.9f, 6.5f}}) {
        Signal right = left;
        for (float &sample : right) {
            sample *= pan;
        }
        Rig rig(-12.0f);
        const MultiSignal out = rig.feed(MultiSignal{left, right});
        EXPECT_NEAR(changeDb(out[0], left), wantLeft, 0.3f) << pan;
        EXPECT_NEAR(changeDb(out[1], right), wantRight, 0.3f) << pan;
    }
}

TEST(VoiceForward, UnrelatedSidesTakeTheWholeCut)
{
    const Signal left = bandNoise(kSecond * 3, 0.5f, 17);
    const Signal right = bandNoise(kSecond * 3, 0.5f, 18);
    Rig rig(-12.0f);
    const MultiSignal out = rig.feed(MultiSignal{left, right});
    EXPECT_NEAR(changeDb(out[0], left), -12.0f, 1.0f);
    EXPECT_NEAR(rig.block.takenOutDb(), -12.0f, 1.0f);
}

TEST(VoiceForward, TheCentreTestOpensHoldsAndCloses)
{
    // Two seconds of wide sound, of centre, and of wide again.
    constexpr std::size_t kPart = kSecond * 2;
    const Signal centre = bandNoise(kPart * 3, 0.5f, 19);
    const Signal left = bandNoise(kPart * 3, 0.5f, 20);
    const Signal right = bandNoise(kPart * 3, 0.5f, 21);
    MultiSignal film{Signal(kPart * 3), Signal(kPart * 3)};
    for (std::size_t n = 0; n < kPart * 3; ++n) {
        const bool talking = n >= kPart && n < kPart * 2;
        film[0][n] = (talking ? centre[n] : left[n]);
        film[1][n] = (talking ? centre[n] : right[n]);
    }
    Rig rig(Rig::TwoPart{}, -12.0f, -3.0f);
    (void)rig.feed(MultiSignal{Signal(film[0].begin(), film[0].begin() + kPart),
                               Signal(film[1].begin(), film[1].begin() + kPart)});
    EXPECT_EQ(rig.block.found(), VoiceForward::Wide);
    EXPECT_EQ(rig.block.cutDb(), -3.0f);

    (void)rig.feed(MultiSignal{Signal(film[0].begin() + kPart, film[0].begin() + kPart + kPart),
                               Signal(film[1].begin() + kPart, film[1].begin() + kPart + kPart)});
    EXPECT_EQ(rig.block.found(), VoiceForward::Centre);
    EXPECT_EQ(rig.block.cutDb(), -12.0f);

    // The share takes about 100 ms to fall, then the 300 ms hold.
    const std::size_t soon = kSecond / 4;
    (void)rig.feed(MultiSignal{Signal(film[0].begin() + 2 * kPart, film[0].begin() + 2 * kPart + soon),
                               Signal(film[1].begin() + 2 * kPart, film[1].begin() + 2 * kPart + soon)});
    EXPECT_EQ(rig.block.found(), VoiceForward::Centre) << "a short gap let the larger cut go";

    (void)rig.feed(MultiSignal{Signal(film[0].begin() + 2 * kPart + soon, film[0].end()),
                               Signal(film[1].begin() + 2 * kPart + soon, film[1].end())});
    EXPECT_EQ(rig.block.found(), VoiceForward::Wide);
    EXPECT_EQ(rig.block.cutDb(), -3.0f);
}

TEST(VoiceForward, QuietKeepsTheLastChoiceOfCut)
{
    const Signal mono = bandNoise(kSecond * 2, 0.5f, 22);
    Rig rig(Rig::TwoPart{}, -12.0f, -3.0f);
    (void)rig.feed(MultiSignal{mono, mono});
    EXPECT_EQ(rig.block.cutDb(), -12.0f);

    const Signal hush = bandNoise(kSecond * 2, 0.0001f, 23);
    const Signal other = bandNoise(kSecond * 2, 0.0001f, 24);
    (void)rig.feed(MultiSignal{hush, other});
    EXPECT_EQ(rig.block.found(), VoiceForward::Quiet);
    EXPECT_EQ(rig.block.cutDb(), -12.0f) << "a pause changed the cut";

    (void)rig.feed(MultiSignal{silence(kSecond), silence(kSecond)});
    EXPECT_EQ(rig.block.found(), VoiceForward::Quiet);
    EXPECT_EQ(rig.block.cutDb(), -12.0f);
}

TEST(VoiceForward, TheCutFallsAndRisesAtItsRates)
{
    const MultiSignal wide{whiteNoise(kSecond * 4, 0.1f, 25), whiteNoise(kSecond * 4, 0.1f, 26)};
    Rig rig(0.0f);
    (void)rig.feed(MultiSignal{Signal(wide[0].begin(), wide[0].begin() + kSecond),
                               Signal(wide[1].begin(), wide[1].begin() + kSecond)});
    rig.block.applyParam(VoiceForward::Background, -12.0f);
    rig.block.applyParam(VoiceForward::BetweenVoices, -12.0f);

    // 100 ms on, past the ceiling's 20 ms and the hop: 60 dB/s for the rest.
    const std::size_t tenth = kSecond / 10;
    const auto part = [&](std::size_t from, std::size_t count) {
        return MultiSignal{Signal(wide[0].begin() + static_cast<std::ptrdiff_t>(from),
                                  wide[0].begin() + static_cast<std::ptrdiff_t>(from + count)),
                           Signal(wide[1].begin() + static_cast<std::ptrdiff_t>(from),
                                  wide[1].begin() + static_cast<std::ptrdiff_t>(from + count))};
    };
    (void)rig.feed(part(kSecond, tenth));
    EXPECT_NEAR(rig.block.cutDb(), -60.0f * (0.1f - 0.02f), 0.5f);
    (void)rig.feed(part(kSecond + tenth, tenth * 3));
    EXPECT_EQ(rig.block.cutDb(), -12.0f);

    rig.block.applyParam(VoiceForward::Background, 0.0f);
    rig.block.applyParam(VoiceForward::BetweenVoices, 0.0f);
    (void)rig.feed(part(kSecond + tenth * 4, kSecond / 2));
    EXPECT_NEAR(rig.block.cutDb(), -12.0f + 6.0f, 0.2f);
    (void)rig.feed(part(kSecond * 2, kSecond));
    EXPECT_EQ(rig.block.cutDb(), 0.0f);
}

TEST(VoiceForward, HoldLevelWaitsForItsReadingsAndStopsAtItsCap)
{
    const MultiSignal wide{whiteNoise(kSecond * 2, 0.2f, 27), whiteNoise(kSecond * 2, 0.2f, 28)};
    Rig rig(Rig::TwoPart{}, -24.0f, -24.0f, true);
    (void)rig.feed(wide);
    EXPECT_EQ(rig.block.liftDb(), 0.0f) << "before the readings had filled";
    float highest = 0.0f;
    for (int second = 0; second < 16; ++second) {
        const MultiSignal next{whiteNoise(kSecond, 0.2f, 29 + static_cast<std::uint32_t>(second)),
                               whiteNoise(kSecond, 0.2f, 69 + static_cast<std::uint32_t>(second))};
        (void)rig.feed(next);
        highest = std::max(highest, rig.block.liftDb());
    }
    EXPECT_EQ(highest, VoiceForward::kMostLiftDb);
    EXPECT_EQ(rig.meter.value(VoiceForward::kLiftValue), VoiceForward::kMostLiftDb);
}

TEST(VoiceForward, HoldLevelOffLiftsNothing)
{
    const MultiSignal wide{whiteNoise(kSecond * 8, 0.2f, 31), whiteNoise(kSecond * 8, 0.2f, 32)};
    Rig rig(-24.0f);
    (void)rig.feed(wide);
    EXPECT_EQ(rig.block.liftDb(), 0.0f);
}

TEST(VoiceForward, TheWorstCaseKeepsTheCeilingAndTheRateThenComesBackExactly)
{
    // Full scale, with the lift at its cap and the rest of the sound 24 dB
    // down, and then the cut let go at once: the sound asks to come back 30 dB
    // louder than it is.
    constexpr std::size_t kSeconds = 44;
    const std::size_t frames = kSeconds * kSecond;
    const MultiSignal film = filmLike(frames, 33);
    Rig rig(Rig::TwoPart{}, -24.0f, -24.0f, true);

    Signal left(frames);
    Signal right(frames);
    const std::array<std::uint32_t, 4> quanta{64, 1024, 256, 8192};
    std::size_t neutralAt = 0;
    std::size_t at = 0;
    bool letGo = false;
    const auto block = kSecond / 10;
    while (at < frames) {
        const std::size_t count = std::min<std::size_t>(block, frames - at);
        if (!letGo && at >= kSecond * 14) {
            EXPECT_EQ(rig.block.liftDb(), VoiceForward::kMostLiftDb);
            rig.block.applyParam(VoiceForward::Background, 0.0f);
            rig.block.applyParam(VoiceForward::BetweenVoices, 0.0f);
            letGo = true;
        }
        const auto begin = static_cast<std::ptrdiff_t>(at);
        const auto end = static_cast<std::ptrdiff_t>(at + count);
        const MultiSignal part{Signal(film[0].begin() + begin, film[0].begin() + end),
                               Signal(film[1].begin() + begin, film[1].begin() + end)};
        const MultiSignal out = rig.feed(part, quanta[(at / block) % quanta.size()]);
        std::copy(out[0].begin(), out[0].end(), left.begin() + begin);
        std::copy(out[1].begin(), out[1].end(), right.begin() + begin);
        at += count;
        if (letGo && neutralAt == 0 && rig.block.neutral()) {
            neutralAt = at;
        }
    }
    ASSERT_GT(neutralAt, 0U) << "it never came back to nothing";
    ASSERT_LT(neutralAt, frames - 4 * kSecond);

    for (std::size_t n = 0; n + block < neutralAt; ++n) {
        ASSERT_LE(std::fabs(left[n]), kCeiling + kEnough) << "at " << n;
        ASSERT_LE(std::fabs(right[n]), kCeiling + kEnough) << "at " << n;
    }

    expectDelayedExactly(left, film[0], kDelay, neutralAt + 4 * kSecond);
    expectDelayedExactly(right, film[1], kDelay, neutralAt + 4 * kSecond);
}

TEST(VoiceForward, LettingGoRisesNoFasterThanTheRate)
{
    // A steady wide sound at full scale, so that what moves is the block: with
    // nothing in the centre the output is the input times the cut and the lift.
    constexpr std::size_t kSeconds = 20;
    const MultiSignal film{whiteNoise(kSeconds * kSecond, 1.0f, 41), whiteNoise(kSeconds * kSecond, 1.0f, 42)};
    Rig rig(Rig::TwoPart{}, -24.0f, -24.0f, true);
    const auto part = [&film](std::size_t from, std::size_t to) {
        const auto begin = static_cast<std::ptrdiff_t>(from);
        const auto end = static_cast<std::ptrdiff_t>(to);
        return MultiSignal{Signal(film[0].begin() + begin, film[0].begin() + end),
                           Signal(film[1].begin() + begin, film[1].begin() + end)};
    };
    (void)rig.feed(part(0, kSecond * 14));
    ASSERT_EQ(rig.block.liftDb(), VoiceForward::kMostLiftDb);
    rig.block.applyParam(VoiceForward::Background, 0.0f);
    rig.block.applyParam(VoiceForward::BetweenVoices, 0.0f);
    const MultiSignal out = rig.feed(part(kSecond * 14, kSeconds * kSecond));

    // In 100 ms the sound may rise 1.2 dB; steady noise wanders about 0.3.
    constexpr std::size_t kWindow = kSecond / 10;
    float last = levelDb(out[0], 0, kWindow);
    float highest = last;
    for (std::size_t n = kWindow; n + kWindow <= out[0].size(); n += kWindow) {
        const float now = levelDb(out[0], n, n + kWindow);
        EXPECT_LE(now - last, 1.2f + 0.3f) << "at " << n;
        last = now;
        highest = std::max(highest, now);
    }
    EXPECT_GT(highest - levelDb(out[0], 0, kWindow), 6.0f) << "it never rose, so this tested little";
}

TEST(VoiceForward, WhatAPeakLeavesBehindComesBackAtTheRate)
{
    // A steady sound with one short shot well over the ceiling: the limiter
    // takes it, and its own release must keep to the rate like everything else.
    constexpr std::size_t kFrames = kSecond * 3;
    Signal tone = sine(kFrames, 1000.0, kTestSampleRate, 0.2f);
    for (std::size_t n = kSecond * 2; n < kSecond * 2 + 240; ++n) {
        tone[n] = 3.0f;
    }
    Rig rig(-6.0f);
    const MultiSignal out = rig.feed(MultiSignal{tone, tone});
    constexpr std::size_t kWindow = kSecond / 100;
    const std::size_t from = kSecond * 2 + kDelay + 240;
    float last = levelDb(out[0], from, from + kWindow);
    bool dipped = false;
    for (std::size_t n = from + kWindow; n + kWindow < kFrames; n += kWindow) {
        const float now = levelDb(out[0], n, n + kWindow);
        EXPECT_LE(now - last, 0.12f + 0.05f) << "at " << n;
        dipped = dipped || now - last > 0.05f;
        last = now;
    }
    EXPECT_TRUE(dipped) << "nothing came back, so this tested little";
}

TEST(VoiceForward, TheCutWaitsWhileTheLimiterComesBack)
{
    // Both rising together would be twice the rate. Two tones have nothing
    // alike, so the output is the cut and the limiter alone.
    constexpr std::size_t kShotAt = kSecond * 2;
    constexpr std::size_t kFrames = kSecond * 4;
    Signal left = sine(kFrames, 800.0, kTestSampleRate, 0.5f);
    Signal right = sine(kFrames, 1300.0, kTestSampleRate, 0.5f);
    for (std::size_t n = kShotAt; n < kShotAt + 240; ++n) {
        left[n] = 12.0f;
        right[n] = 12.0f;
    }
    Rig rig(-24.0f);
    const auto cutAt = static_cast<std::ptrdiff_t>(kShotAt + 2048);
    (void)rig.feed(
        MultiSignal{Signal(left.begin(), left.begin() + cutAt), Signal(right.begin(), right.begin() + cutAt)});
    rig.block.applyParam(VoiceForward::Background, 0.0f);
    rig.block.applyParam(VoiceForward::BetweenVoices, 0.0f);
    const MultiSignal out =
        rig.feed(MultiSignal{Signal(left.begin() + cutAt, left.end()), Signal(right.begin() + cutAt, right.end())});
    constexpr std::size_t kWindow = kSecond / 100;
    float last = levelDb(out[0], 0, kWindow);
    for (std::size_t n = kWindow; n + kWindow < out[0].size(); n += kWindow) {
        const float now = levelDb(out[0], n, n + kWindow);
        EXPECT_LE(now - last, 0.12f + 0.08f) << "at " << n;
        last = now;
    }
}

TEST(VoiceForward, NoMovedSampleMeetsACeilingStillOnItsWayDown)
{
    // From nothing to the full cut at once, on a sound over the ceiling: by
    // the time the cut moves the limit has to be there.
    const MultiSignal film = filmLike(kSecond * 2, 34);
    Rig rig(0.0f);
    const auto half = static_cast<std::ptrdiff_t>(kSecond);
    (void)rig.feed(
        MultiSignal{Signal(film[0].begin(), film[0].begin() + half), Signal(film[1].begin(), film[1].begin() + half)});
    EXPECT_TRUE(rig.block.neutral());
    rig.block.applyParam(VoiceForward::Background, -24.0f);
    rig.block.applyParam(VoiceForward::BetweenVoices, -24.0f);
    constexpr std::uint32_t kQuantum = 64;
    bool moved = false;
    for (std::size_t at = kSecond; at + kQuantum < film[0].size(); at += kQuantum) {
        const auto begin = static_cast<std::ptrdiff_t>(at);
        const MultiSignal part{Signal(film[0].begin() + begin, film[0].begin() + begin + kQuantum),
                               Signal(film[1].begin() + begin, film[1].begin() + begin + kQuantum)};
        const MultiSignal out = rig.feed(part, kQuantum);
        const bool wasMoved = moved;
        moved = rig.block.cutDb() < 0.0f;
        if (wasMoved) {
            // The limiter's own delay lets the last untouched samples out.
            if (at > kSecond + 2 * kDelay) {
                ASSERT_LE(*std::ranges::max_element(out[0], {}, [](float x) { return std::fabs(x); }),
                          kCeiling + kEnough)
                    << at;
            }
        } else {
            EXPECT_FALSE(moved && at < kSecond + kSecond / 50) << "the cut moved before the ceiling arrived, at " << at;
        }
    }
    EXPECT_TRUE(moved);
}

TEST(VoiceForward, ItsDelayIsTrueAtEveryRate)
{
    for (const auto &[rate, delay] : {std::pair{44100.0, std::size_t{1024 + 66}},
                                      std::pair{48000.0, std::size_t{1024 + 72}},
                                      std::pair{96000.0, std::size_t{2048 + 144}},
                                      std::pair{192000.0, std::size_t{4096 + 288}}}) {
        Rig rig(0.0f, rate);
        EXPECT_EQ(rig.block.info().latencySamples, delay) << rate;
        const std::size_t frames = static_cast<std::size_t>(rate);
        const Signal left = whiteNoise(frames, 1.0f, 35);
        const Signal right = whiteNoise(frames, 1.0f, 36);
        const MultiSignal out = rig.feed(MultiSignal{left, right});
        expectDelayedExactly(out[0], left, delay);
        expectDelayedExactly(out[1], right, delay);
    }
}

TEST(VoiceForward, ItDoesTheSameAtEveryRate)
{
    for (const double rate : {44100.0, 48000.0, 96000.0, 192000.0}) {
        const auto frames = static_cast<std::size_t>(rate * 3.0);
        const Signal tone = sine(frames, 1000.0, rate, 0.4f);
        const Signal left = whiteNoise(frames, 0.3f, 37);
        const Signal right = whiteNoise(frames, 0.3f, 38);

        Rig centred(-24.0f, rate);
        (void)centred.feed(MultiSignal{tone, tone});
        EXPECT_EQ(centred.block.found(), VoiceForward::Centre) << rate;
        EXPECT_NEAR(centred.block.takenOutDb(), 0.0f, 0.3f) << rate;

        Rig unrelated(-24.0f, rate);
        (void)unrelated.feed(MultiSignal{left, right});
        EXPECT_EQ(unrelated.block.found(), VoiceForward::Wide) << rate;
        // White noise: above 12 kHz nothing is centre either way.
        EXPECT_LT(unrelated.block.takenOutDb(), -15.0f) << rate;

        // 60 dB/s falls the same in time.
        Rig timed(0.0f, rate);
        (void)timed.feed(MultiSignal{Signal(static_cast<std::size_t>(rate * 0.1), 0.0f),
                                     Signal(static_cast<std::size_t>(rate * 0.1), 0.0f)});
        timed.block.applyParam(VoiceForward::Background, -12.0f);
        timed.block.applyParam(VoiceForward::BetweenVoices, -12.0f);
        (void)timed.feed(MultiSignal{Signal(left.begin(), left.begin() + static_cast<std::ptrdiff_t>(rate * 0.12)),
                                     Signal(right.begin(), right.begin() + static_cast<std::ptrdiff_t>(rate * 0.12))});
        EXPECT_NEAR(timed.block.cutDb(), -60.0f * 0.1f, 0.6f) << rate;
    }
}

TEST(VoiceForward, TheMeterSaysWhatIsHappening)
{
    const Signal left = bandNoise(kSecond * 2, 0.3f, 39);
    const Signal right = bandNoise(kSecond * 2, 0.3f, 40);
    Rig rig(-12.0f);
    (void)rig.feed(MultiSignal{left, right});
    EXPECT_EQ(rig.meter.value(VoiceForward::kFoundValue), static_cast<float>(VoiceForward::Wide));
    EXPECT_NEAR(rig.meter.value(VoiceForward::kTakenOutValue), -12.0f, 1.0f);
    EXPECT_EQ(rig.meter.value(VoiceForward::kLiftValue), 0.0f);
    EXPECT_EQ(rig.meter.value(VoiceForward::kLimitValue), 0.0f);
}

TEST(VoiceForward, CostsLittle)
{
    VoiceForward block;
    set(block, "background", -12.0f);
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const MultiSignal input{whiteNoise(kSecond * 30, 0.1f, 1), whiteNoise(kSecond * 30, 0.1f, 2)};
    const auto started = std::chrono::steady_clock::now();
    (void)runner.run(input, CycleShape::fixed(256));
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const double share = took / 30.0;
    std::cerr << "voice forward: " << share * 100.0 << " % of a core\n";
#ifdef NDEBUG
    EXPECT_LT(share, 0.02);
#else
    (void)share;
#endif
}
