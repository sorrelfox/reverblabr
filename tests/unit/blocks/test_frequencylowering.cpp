// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Frequency Lowering as a block: its moves come from its settings,
// its ear is moved and the other passes untouched and lined up, the
// latency is reported and is what the sound shows, a range given upside
// down or a destination above its source is taken sensibly, a setting
// changed while it plays makes no click, and it runs the same at any cycle
// length and allocates nothing. The moving itself is dsp::FrequencyMover's,
// measured in tests/unit/dsp/test_frequencymove.cpp.

#include "allocationtrap.h"
#include "analysis.h"
#include "blocks/frequencylowering.h"
#include "blocksizeinvariance.h"
#include "dsp/decibel.h"
#include "dsp/fft.h"
#include "offlinerunner.h"
#include "rt/meter.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <span>
#include <vector>

using namespace rb;
using namespace rb::blocks;
using namespace rb::dsp;
using namespace rb::test;

namespace
{

constexpr std::size_t kSecond = 48000;

/// The last half second of \a signal.
[[nodiscard]] std::span<const float> settled(const Signal &signal)
{
    return std::span<const float>(signal).subspan(signal.size() - kSecond / 2);
}

/// The power of \a signal above \a fromHz, in dB against a full-scale
/// sine: Hann-windowed frames of 1024, half overlapping, averaged. A click
/// is broadband and shows here; a tone below it does not.
[[nodiscard]] float aboveDb(std::span<const float> signal, double fromHz)
{
    constexpr std::size_t size = 1024;
    RealFft fft;
    fft.prepare(size);
    std::vector<float> frame(size);
    std::vector<std::complex<float>> spectrum(fft.bins());
    double windowPower = 0.0;
    std::vector<float> window(size);
    for (std::size_t n = 0; n < size; ++n) {
        window[n] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(n) / size));
        windowPower += static_cast<double>(window[n]) * window[n];
    }
    const auto first = static_cast<std::size_t>(std::ceil(fromHz * size / kTestSampleRate));
    double power = 0.0;
    std::size_t frames = 0;
    for (std::size_t at = 0; at + size <= signal.size(); at += size / 2) {
        for (std::size_t n = 0; n < size; ++n) {
            frame[n] = window[n] * signal[at + n];
        }
        fft.forward(frame, spectrum);
        for (std::size_t bin = first; bin < fft.bins(); ++bin) {
            power += std::norm(spectrum[bin]);
        }
        ++frames;
    }
    const double mean = 4.0 * power / (static_cast<double>(frames) * windowPower * size);
    return static_cast<float>(10.0 * std::log10(std::max(mean, 1.0e-12)));
}

void set(FrequencyLowering &block, std::string_view id, const ParamValue &value)
{
    ASSERT_TRUE(block.setParam(id, value).has_value()) << id;
}

} // namespace

TEST(FrequencyLowering, ItIsAHearingBlockWithAnEarAndOneMoveInUse)
{
    FrequencyLowering block;
    EXPECT_EQ(block.info().typeId, "hearing.frequencylowering");
    EXPECT_EQ(block.info().category, BlockCategory::Hearing);
    EXPECT_EQ(block.param("ear"), ParamValue::ofEnum("left"));
    ASSERT_EQ(block.params().size(),
              FrequencyLowering::FirstMoveParam + FrequencyLowering::kMoveCount * FrequencyLowering::MoveParamCount);
    EXPECT_EQ(block.params()[FrequencyLowering::paramIndex(2, FrequencyLowering::SourceHigh)].id, "move3SourceHigh");

    const FrequencyMove first = block.moveSettings(0);
    EXPECT_TRUE(first.active);
    EXPECT_EQ(first.method, MoveMethod::Compress);
    EXPECT_EQ(first.sourceLowHz, 4000.0f);
    EXPECT_EQ(first.sourceHighHz, 8000.0f);
    EXPECT_EQ(first.destinationLowHz, 2500.0f);
    EXPECT_EQ(first.destinationHighHz, 4000.0f);
    EXPECT_EQ(first.source, MoveSource::Remove);
    for (std::size_t move = 1; move < FrequencyLowering::kMoveCount; ++move) {
        EXPECT_FALSE(block.moveSettings(move).active) << move;
    }
}

TEST(FrequencyLowering, TheEarItIsSetToIsMovedAndTheOtherPassesUntouchedAndLinedUp)
{
    for (const char *ear : {"left", "right"}) {
        FrequencyLowering block;
        set(block, "ear", ParamValue::ofEnum(ear));
        BlockRunner runner(block, 2, 2);
        ASSERT_TRUE(runner.prepare().has_value());
        const std::uint32_t latency = block.info().latencySamples;
        EXPECT_EQ(latency, 1024U) << "a frame of at least 20 ms at 48 kHz";

        const Signal tone = sine(2 * kSecond, 6000.0, kTestSampleRate, 0.5f);
        const MultiSignal output = runner.run({tone, tone}, CycleShape::fixed(256));
        const std::size_t moved = std::string_view(ear) == "left" ? 0 : 1;
        const std::size_t other = 1 - moved;

        const double landed = carriedTo(block.moveSettings(0), 6000.0);
        EXPECT_NEAR(gainToDb(amplitudeAt(settled(output[moved]), landed) / 0.5f), 0.0f, 1.5f) << ear;
        EXPECT_LT(gainToDb(amplitudeAt(settled(output[moved]), 6000.0) / 0.5f), -40.0f) << ear;

        // The other side is the tone, exactly, as late as the block says.
        const std::span<const float> late = std::span<const float>(output[other]).subspan(latency);
        const std::span<const float> early = std::span<const float>(tone).first(tone.size() - latency);
        EXPECT_LT(nullTestDb(late, early), -100.0f) << ear;
    }

    // An impulse on the side passed through comes out exactly as late as
    // the block says (a tone's lag is ambiguous by its period).
    FrequencyLowering block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const Signal click = impulse(4 * 1024, 100, 0.5f);
    const MultiSignal output = runner.run({silence(click.size()), click}, CycleShape::random(16, 512, 2));
    const std::optional<int> measured = latencySamples(click, output[1], 2048);
    ASSERT_TRUE(measured.has_value());
    EXPECT_EQ(*measured, static_cast<int>(block.info().latencySamples));
}

TEST(FrequencyLowering, NothingMovedTheEarComesOutAsItWentInAsLateAsTheOther)
{
    FrequencyLowering block;
    set(block, "move1Used", ParamValue::ofBool(false));
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const Signal left = pinkNoise(kSecond, 0.4f, 1);
    const Signal right = pinkNoise(kSecond, 0.4f, 2);
    const MultiSignal output = runner.run({left, right}, CycleShape::random(16, 1024, 3));
    const std::uint32_t latency = block.info().latencySamples;
    for (std::size_t channel = 0; channel < 2; ++channel) {
        const Signal &input = channel == 0 ? left : right;
        EXPECT_LT(nullTestDb(std::span<const float>(output[channel]).subspan(latency),
                             std::span<const float>(input).first(input.size() - latency)),
                  -90.0f)
            << channel;
    }
}

TEST(FrequencyLowering, EveryMovesSettingsReachTheMover)
{
    FrequencyLowering block;
    set(block, "move2Used", ParamValue::ofBool(true));
    set(block, "move2Method", ParamValue::ofEnum("transpose"));
    set(block, "move2SourceLow", ParamValue::ofFloat(3000.0f));
    set(block, "move2SourceHigh", ParamValue::ofFloat(5000.0f));
    set(block, "move2DestinationLow", ParamValue::ofFloat(1500.0f));
    set(block, "move2DestinationHigh", ParamValue::ofFloat(2500.0f));
    set(block, "move2Level", ParamValue::ofFloat(-6.0f));
    set(block, "move2Source", ParamValue::ofEnum("lower"));
    set(block, "move2When", ParamValue::ofEnum("needed"));
    const FrequencyMove move = block.moveSettings(1);
    EXPECT_TRUE(move.active);
    EXPECT_EQ(move.method, MoveMethod::Transpose);
    EXPECT_EQ(move.sourceLowHz, 3000.0f);
    EXPECT_EQ(move.sourceHighHz, 5000.0f);
    EXPECT_EQ(move.destinationLowHz, 1500.0f);
    EXPECT_EQ(move.destinationHighHz, 2500.0f);
    EXPECT_EQ(move.levelDb, -6.0f);
    EXPECT_EQ(move.source, MoveSource::Lower);
    EXPECT_EQ(move.when, MoveWhen::WhenNeeded);

    set(block, "move2On", ParamValue::ofBool(false));
    EXPECT_FALSE(block.moveSettings(1).active) << "switched off";
}

TEST(FrequencyLowering, ARangeUpsideDownIsTheSameRangeAndADestinationAboveItsSourceDoesNothing)
{
    FrequencyLowering block;
    set(block, "move1SourceLow", ParamValue::ofFloat(8000.0f));
    set(block, "move1SourceHigh", ParamValue::ofFloat(4000.0f));
    FrequencyMove move = block.moveSettings(0);
    EXPECT_TRUE(move.active);
    EXPECT_EQ(move.sourceLowHz, 4000.0f);
    EXPECT_EQ(move.sourceHighHz, 8000.0f);

    set(block, "move1DestinationLow", ParamValue::ofFloat(5000.0f));
    set(block, "move1DestinationHigh", ParamValue::ofFloat(9000.0f));
    move = block.moveSettings(0);
    EXPECT_FALSE(move.active) << "a destination above the source is not a lowering";
}

TEST(FrequencyLowering, ItSaysWhichMovesAreActing)
{
    FrequencyLowering block;
    set(block, "move2Used", ParamValue::ofBool(true));
    set(block, "move2When", ParamValue::ofEnum("needed"));
    rt::BlockMeter meter;
    BlockRunner runner(block, 2, 2);
    runner.setMeter(&meter);
    ASSERT_TRUE(runner.prepare().has_value());
    // A low tone: the first move always acts, the second ("when needed",
    // 2-4 kHz) has nothing in its range louder than what is below it.
    const Signal tone = sine(kSecond / 2, 500.0, kTestSampleRate, 0.5f);
    (void)runner.run({tone, tone});
    EXPECT_EQ(meter.value(FrequencyLowering::kActingValue), 1.0f);

    meter.setAnalysing(true);
    (void)runner.run({tone, tone});
    const rt::BandValues bands = meter.bands();
    ASSERT_EQ(bands.count, FrequencyLowering::kMoveCount);
    EXPECT_EQ(bands.values[0], 1.0f);
    EXPECT_EQ(bands.values[1], 0.0f);
    EXPECT_EQ(bands.values[2], 0.0f);
}

TEST(FrequencyLowering, SettingsChangedWhileItPlaysMakeNoClickOnEitherEar)
{
    // A 6 kHz tone on both ears; then, while it plays, the destination
    // moved, the move made a transposition keeping its source, switched
    // off, and the block moved to the other ear. Nothing heard is above
    // 6 kHz, so a click is whatever shows above 10 kHz round a change.
    FrequencyLowering block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const Signal tone = sine(kSecond, 6000.0, kTestSampleRate, 0.5f);
    Signal left;
    Signal right;
    const auto play = [&] {
        const MultiSignal output = runner.run({tone, tone}, CycleShape::fixed(256));
        left.insert(left.end(), output[0].begin(), output[0].end());
        right.insert(right.end(), output[1].begin(), output[1].end());
    };
    play();
    set(block, "move1DestinationLow", ParamValue::ofFloat(2000.0f));
    play();
    set(block, "move1Method", ParamValue::ofEnum("transpose"));
    set(block, "move1Source", ParamValue::ofEnum("keep"));
    play();
    set(block, "move1On", ParamValue::ofBool(false));
    play();
    set(block, "ear", ParamValue::ofEnum("right"));
    play();

    const std::uint32_t latency = block.info().latencySamples;
    for (std::size_t change = 1; change < 5; ++change) {
        const std::size_t from = change * kSecond - 2048;
        const float leftHigh = aboveDb(std::span<const float>(left).subspan(from, 4096 + latency), 10000.0);
        const float rightHigh = aboveDb(std::span<const float>(right).subspan(from, 4096 + latency), 10000.0);
        std::printf("[measure] change %zu while a 6 kHz tone plays: above 10 kHz %.1f dB left, %.1f dB right\n",
                    change,
                    leftHigh,
                    rightHigh);
        EXPECT_LT(leftHigh, -80.0f) << change;
        EXPECT_LT(rightHigh, -80.0f) << change;
    }
    // The same measure hears a click: the tone cut off dead.
    Signal cut = tone;
    std::fill(cut.begin() + kSecond / 2 + 3, cut.end(), 0.0f);
    const float cutHigh = aboveDb(std::span<const float>(cut).subspan(kSecond / 2 - 2048, 4096 + latency), 10000.0);
    std::printf("[measure] a tone cut off dead: above 10 kHz %.1f dB\n", cutHigh);
    EXPECT_GT(cutHigh, -80.0f);
}

TEST(FrequencyLowering, ItSoundsTheSameWhateverTheCycles)
{
    Signal hiss = whiteNoise(kSecond / 2, 0.3f, 4);
    const Signal tone = sine(kSecond / 2, 6000.0, kTestSampleRate, 0.3f);
    for (std::size_t n = 0; n < hiss.size(); ++n) {
        hiss[n] += tone[n];
    }
    InvarianceOptions options;
    options.inputChannels = 2;
    options.outputChannels = 2;
    for (const char *ear : {"left", "right"}) {
        const InvarianceReport report = checkBlockSizeInvariance(
            [ear] {
                auto block = std::make_unique<FrequencyLowering>();
                EXPECT_TRUE(block->setParam("ear", ParamValue::ofEnum(ear)).has_value());
                EXPECT_TRUE(block->setParam("move2Used", ParamValue::ofBool(true)).has_value());
                EXPECT_TRUE(block->setParam("move2Method", ParamValue::ofEnum("transpose")).has_value());
                EXPECT_TRUE(block->setParam("move2When", ParamValue::ofEnum("needed")).has_value());
                return std::unique_ptr<Block>(std::move(block));
            },
            {hiss, tone},
            options);
        EXPECT_TRUE(report.passed) << ear << ": " << report.summary;
    }
}

TEST(FrequencyLowering, NothingIsAllocatedOnTheAudioThreadEvenWhileItsSettingsChange)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    FrequencyLowering block;
    rt::BlockMeter meter;
    meter.setAnalysing(true);
    BlockRunner runner(block, 2, 2);
    runner.setMeter(&meter);
    ASSERT_TRUE(runner.prepare().has_value());
    const MultiSignal input{whiteNoise(8192, 0.5f, 3), whiteNoise(8192, 0.5f, 4)};
    std::size_t before = 0;
    (void)runner.runWatchingForAllocations(input, CycleShape::random(16, 1024, 5), before);
    set(block, "move1DestinationLow", ParamValue::ofFloat(2000.0f));
    set(block, "move2Used", ParamValue::ofBool(true));
    set(block, "ear", ParamValue::ofEnum("right"));
    std::size_t during = 0;
    (void)runner.runWatchingForAllocations(input, CycleShape::random(16, 1024, 6), during);
    EXPECT_EQ(before, 0U);
    EXPECT_EQ(during, 0U);
}

TEST(FrequencyLowering, ItsLatencyFollowsTheRate)
{
    for (const auto &[rate, expected] :
         {std::pair{44100.0, 1024U}, std::pair{48000.0, 1024U}, std::pair{96000.0, 2048U}}) {
        FrequencyLowering block;
        PrepareSpec spec;
        spec.sampleRate = rate;
        ASSERT_TRUE(block.prepare(spec).has_value());
        EXPECT_EQ(block.info().latencySamples, expected) << rate;
    }
}
