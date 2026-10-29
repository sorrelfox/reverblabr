// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Frequency Raising as a block: it starts copying 250-500 Hz two
// octaves up and keeping it; a low tone is copied up on its ear at its
// level and the other ear passes untouched and lined up; Harmonics gives a
// low tone its harmonics; with Frequency Lowering in series, the two halves
// of a dip in the middle each land where they are sent; a destination
// below its source does nothing; a setting changed while it plays makes no
// click; it runs the same at any cycle length, allocates nothing, and a
// minute of four moves costs little. The moving itself is
// dsp::FrequencyMover's, measured in tests/unit/dsp/test_frequencymove.cpp.

#include "allocationtrap.h"
#include "analysis.h"
#include "blocks/frequencylowering.h"
#include "blocks/frequencyraising.h"
#include "blocksizeinvariance.h"
#include "dsp/decibel.h"
#include "dsp/fft.h"
#include "offlinerunner.h"
#include "rt/meter.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <iostream>
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

void set(FrequencyMoveBlock &block, std::string_view id, const ParamValue &value)
{
    ASSERT_TRUE(block.setParam(id, value).has_value()) << id;
}

/// \a input on both ears through \a block, at cycles of 256.
[[nodiscard]] MultiSignal through(FrequencyMoveBlock &block, const Signal &input)
{
    BlockRunner runner(block, 2, 2);
    EXPECT_TRUE(runner.prepare().has_value());
    return runner.run({input, input}, CycleShape::fixed(256));
}

} // namespace

TEST(FrequencyRaising, ItIsAHearingBlockThatStartsByCopyingTheLowsTwoOctavesUp)
{
    FrequencyRaising block;
    EXPECT_EQ(block.info().typeId, "hearing.frequencyraising");
    EXPECT_EQ(block.info().category, BlockCategory::Hearing);
    EXPECT_EQ(block.param("ear"), ParamValue::ofEnum("left"));
    EXPECT_EQ(block.param("move1Method"), ParamValue::ofEnum("transpose"));
    EXPECT_EQ(block.param("move1Source"), ParamValue::ofEnum("keep"));

    const FrequencyMove first = block.moveSettings(0);
    EXPECT_TRUE(first.active);
    EXPECT_EQ(first.method, MoveMethod::Transpose);
    EXPECT_EQ(first.sourceLowHz, 250.0f);
    EXPECT_EQ(first.sourceHighHz, 500.0f);
    EXPECT_EQ(first.destinationLowHz, 1000.0f);
    EXPECT_EQ(first.destinationHighHz, 2000.0f);
    EXPECT_EQ(first.source, MoveSource::Keep);
    EXPECT_TRUE(first.raises());
    for (std::size_t move = 1; move < FrequencyRaising::kMoveCount; ++move) {
        EXPECT_FALSE(block.moveSettings(move).active) << move;
        EXPECT_TRUE(block.moveSettings(move).raises()) << "its defaults go up: " << move;
    }
    // Its ranges reach lower than Lowering's.
    const ParamDescriptor &low = block.params()[FrequencyRaising::paramIndex(0, FrequencyRaising::SourceLow)];
    EXPECT_EQ(low.minimum, ParamValue::ofFloat(60.0f));
}

TEST(FrequencyRaising, ALowToneIsCopiedTwoOctavesUpOnItsEarAndTheOtherPassesUntouchedAndLinedUp)
{
    for (const char *ear : {"left", "right"}) {
        for (const float levelDb : {0.0f, -6.0f}) {
            FrequencyRaising block;
            set(block, "ear", ParamValue::ofEnum(ear));
            set(block, "move1Level", ParamValue::ofFloat(levelDb));
            const Signal tone = sine(3 * kSecond, 300.0, kTestSampleRate, 0.5f);
            const MultiSignal output = through(block, tone);
            const std::uint32_t latency = block.info().latencySamples;
            EXPECT_EQ(latency, 4096U) << "a frame of at least 80 ms at 48 kHz";
            const std::size_t moved = std::string_view(ear) == "left" ? 0 : 1;
            const std::size_t other = 1 - moved;

            const float copy = gainToDb(amplitudeAt(settled(output[moved]), 1200.0) / 0.5f);
            const float original = gainToDb(amplitudeAt(settled(output[moved]), 300.0) / 0.5f);
            std::printf("[measure] %s ear, 300 Hz two octaves up at %+.0f dB: %+.2f dB at 1.2 kHz, %+.2f at 300 Hz\n",
                        ear,
                        levelDb,
                        copy,
                        original);
            EXPECT_NEAR(copy, levelDb, 1.0f) << ear;
            EXPECT_NEAR(original, 0.0f, 0.5f) << ear;

            const std::span<const float> late = std::span<const float>(output[other]).subspan(latency);
            const std::span<const float> early = std::span<const float>(tone).first(tone.size() - latency);
            EXPECT_LT(nullTestDb(late, early), -100.0f) << ear;
        }
    }

    // An impulse on the side passed through comes out as late as the block
    // says.
    FrequencyRaising block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const Signal click = impulse(4 * 4096, 100, 0.5f);
    const MultiSignal output = runner.run({silence(click.size()), click}, CycleShape::random(16, 512, 2));
    const std::optional<int> measured = latencySamples(click, output[1], 8192);
    ASSERT_TRUE(measured.has_value());
    EXPECT_EQ(*measured, static_cast<int>(block.info().latencySamples));
}

TEST(FrequencyRaising, HarmonicsGiveALowToneItsSecondToFourthInTheDestinationAndNothingBelowIt)
{
    FrequencyRaising block;
    set(block, "move1Method", ParamValue::ofEnum("harmonics"));
    set(block, "move1SourceLow", ParamValue::ofFloat(80.0f));
    set(block, "move1SourceHigh", ParamValue::ofFloat(160.0f));
    set(block, "move1DestinationLow", ParamValue::ofFloat(150.0f));
    set(block, "move1DestinationHigh", ParamValue::ofFloat(450.0f));
    set(block, "move1Level", ParamValue::ofFloat(-6.0f));
    set(block, "movedOnly", ParamValue::ofBool(true));
    EXPECT_EQ(block.moveSettings(0).method, MoveMethod::Harmonics);

    const Signal tone = sine(3 * kSecond, 100.0, kTestSampleRate, 0.5f);
    const MultiSignal output = through(block, tone);
    for (const double hz : {200.0, 300.0, 400.0}) {
        const float at = gainToDb(amplitudeAt(settled(output[0]), hz) / 0.5f);
        std::printf("[measure] through the block, 100 Hz's harmonic at %.0f Hz set to -6 dB: %+.2f dB\n", hz, at);
        EXPECT_NEAR(at, -6.0f, 1.0f) << hz << " Hz";
    }
    // The fundamental itself is not added to: moved sound only, and none
    // of it at 100 Hz.
    EXPECT_LT(gainToDb(amplitudeAt(settled(output[0]), 100.0) / 0.5f), -60.0f);
    EXPECT_LT(gainToDb(amplitudeAt(settled(output[0]), 500.0) / 0.5f), -60.0f);
}

TEST(FrequencyRaising, WithFrequencyLoweringInSeriesBothHalvesOfADipLandWhereTheyAreSent)
{
    // A dip in the middle, 1-4 kHz: Lowering squeezes its lower half,
    // 1-2 kHz, down into 500-1000 Hz; Raising squeezes its upper half,
    // 2-4 kHz, up into 4-8 kHz. Both empty what they move.
    FrequencyLowering lowering;
    set(lowering, "move1SourceLow", ParamValue::ofFloat(1000.0f));
    set(lowering, "move1SourceHigh", ParamValue::ofFloat(2000.0f));
    set(lowering, "move1DestinationLow", ParamValue::ofFloat(500.0f));
    set(lowering, "move1DestinationHigh", ParamValue::ofFloat(1000.0f));
    FrequencyRaising raising;
    set(raising, "move1Method", ParamValue::ofEnum("compress"));
    set(raising, "move1Source", ParamValue::ofEnum("remove"));
    set(raising, "move1SourceLow", ParamValue::ofFloat(2000.0f));
    set(raising, "move1SourceHigh", ParamValue::ofFloat(4000.0f));
    set(raising, "move1DestinationLow", ParamValue::ofFloat(4000.0f));
    set(raising, "move1DestinationHigh", ParamValue::ofFloat(8000.0f));

    Signal both = sine(3 * kSecond, 1500.0, kTestSampleRate, 0.25f);
    const Signal upper = sine(3 * kSecond, 3000.0, kTestSampleRate, 0.25f);
    for (std::size_t n = 0; n < both.size(); ++n) {
        both[n] += upper[n];
    }
    const MultiSignal lowered = through(lowering, both);
    BlockRunner second(raising, 2, 2);
    ASSERT_TRUE(second.prepare().has_value());
    const MultiSignal output = second.run(lowered, CycleShape::fixed(256));

    const double down = carriedTo(lowering.moveSettings(0), 1500.0);
    const double up = carriedTo(raising.moveSettings(0), 3000.0);
    const float downDb = gainToDb(amplitudeAt(settled(output[0]), down) / 0.25f);
    const float upDb = gainToDb(amplitudeAt(settled(output[0]), up) / 0.25f);
    const float leftAt1500 = gainToDb(amplitudeAt(settled(output[0]), 1500.0) / 0.25f);
    const float leftAt3000 = gainToDb(amplitudeAt(settled(output[0]), 3000.0) / 0.25f);
    std::printf(
        "[measure] a dip's two halves: 1.5 kHz to %.0f Hz at %+.2f dB, 3 kHz to %.0f Hz at %+.2f dB; left "
        "behind %.1f and %.1f dB\n",
        down,
        downDb,
        up,
        upDb,
        leftAt1500,
        leftAt3000);
    // Evenly in octaves: 1.5 kHz is as far up its octave as 750 Hz and
    // 6 kHz are up theirs.
    EXPECT_NEAR(down, 750.0, 1.0);
    EXPECT_NEAR(up, 6000.0, 1.0);
    EXPECT_NEAR(downDb, 0.0f, 1.5f);
    EXPECT_NEAR(upDb, 0.0f, 1.5f);
    EXPECT_LT(leftAt1500, -40.0f);
    EXPECT_LT(leftAt3000, -40.0f);
}

TEST(FrequencyRaising, ARangeUpsideDownIsTheSameRangeAndADestinationBelowItsSourceDoesNothing)
{
    FrequencyRaising block;
    set(block, "move1SourceLow", ParamValue::ofFloat(500.0f));
    set(block, "move1SourceHigh", ParamValue::ofFloat(250.0f));
    FrequencyMove move = block.moveSettings(0);
    EXPECT_TRUE(move.active);
    EXPECT_EQ(move.sourceLowHz, 250.0f);
    EXPECT_EQ(move.sourceHighHz, 500.0f);

    set(block, "move1DestinationLow", ParamValue::ofFloat(100.0f));
    set(block, "move1DestinationHigh", ParamValue::ofFloat(200.0f));
    move = block.moveSettings(0);
    EXPECT_FALSE(move.active) << "a destination below the source is not a raising";
}

TEST(FrequencyRaising, EveryMovesSettingsReachTheMover)
{
    FrequencyRaising block;
    set(block, "move3Used", ParamValue::ofBool(true));
    for (const auto &[id, method] : {std::pair{"compress", MoveMethod::Compress},
                                     std::pair{"transpose", MoveMethod::Transpose},
                                     std::pair{"harmonics", MoveMethod::Harmonics}}) {
        set(block, "move3Method", ParamValue::ofEnum(id));
        EXPECT_EQ(block.moveSettings(2).method, method) << id;
    }
    set(block, "move3SourceLow", ParamValue::ofFloat(100.0f));
    set(block, "move3SourceHigh", ParamValue::ofFloat(200.0f));
    set(block, "move3DestinationLow", ParamValue::ofFloat(300.0f));
    set(block, "move3DestinationHigh", ParamValue::ofFloat(900.0f));
    set(block, "move3Level", ParamValue::ofFloat(-3.0f));
    set(block, "move3Source", ParamValue::ofEnum("lower"));
    set(block, "move3When", ParamValue::ofEnum("needed"));
    const FrequencyMove move = block.moveSettings(2);
    EXPECT_TRUE(move.active);
    EXPECT_EQ(move.sourceLowHz, 100.0f);
    EXPECT_EQ(move.sourceHighHz, 200.0f);
    EXPECT_EQ(move.destinationLowHz, 300.0f);
    EXPECT_EQ(move.destinationHighHz, 900.0f);
    EXPECT_EQ(move.levelDb, -3.0f);
    EXPECT_EQ(move.source, MoveSource::Lower);
    EXPECT_EQ(move.when, MoveWhen::WhenNeeded);

    set(block, "move3On", ParamValue::ofBool(false));
    EXPECT_FALSE(block.moveSettings(2).active) << "switched off";
}

TEST(FrequencyRaising, ItSaysWhichMovesAreActing)
{
    FrequencyRaising block;
    set(block, "move2Used", ParamValue::ofBool(true));
    set(block, "move2When", ParamValue::ofEnum("needed"));
    rt::BlockMeter meter;
    BlockRunner runner(block, 2, 2);
    runner.setMeter(&meter);
    ASSERT_TRUE(runner.prepare().has_value());
    // A 3 kHz tone: the first move always acts; the second ("when
    // needed", 500-1000 Hz) has nothing in its range louder than what is
    // above it.
    const Signal tone = sine(kSecond, 3000.0, kTestSampleRate, 0.5f);
    (void)runner.run({tone, tone});
    EXPECT_EQ(meter.value(FrequencyRaising::kActingValue), 1.0f);

    meter.setAnalysing(true);
    (void)runner.run({tone, tone});
    const rt::BandValues bands = meter.bands();
    ASSERT_EQ(bands.count, FrequencyRaising::kMoveCount);
    EXPECT_EQ(bands.values[0], 1.0f);
    EXPECT_EQ(bands.values[1], 0.0f);
}

TEST(FrequencyRaising, SettingsChangedWhileItPlaysMakeNoClickOnEitherEar)
{
    // A 300 Hz tone on both ears; then, while it plays, the destination
    // moved, the move made Harmonics, switched off, and the block moved to
    // the other ear. Nothing it makes reaches 10 kHz, so a click is
    // whatever shows there round a change.
    FrequencyRaising block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const Signal tone = sine(kSecond, 300.0, kTestSampleRate, 0.5f);
    Signal left;
    Signal right;
    const auto play = [&] {
        const MultiSignal output = runner.run({tone, tone}, CycleShape::fixed(256));
        left.insert(left.end(), output[0].begin(), output[0].end());
        right.insert(right.end(), output[1].begin(), output[1].end());
    };
    play();
    set(block, "move1DestinationLow", ParamValue::ofFloat(800.0f));
    play();
    set(block, "move1Method", ParamValue::ofEnum("harmonics"));
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
        std::printf("[measure] change %zu while a 300 Hz tone plays: above 10 kHz %.1f dB left, %.1f dB right\n",
                    change,
                    leftHigh,
                    rightHigh);
        EXPECT_LT(leftHigh, -80.0f) << change;
        EXPECT_LT(rightHigh, -80.0f) << change;
    }
}

TEST(FrequencyRaising, ItSoundsTheSameWhateverTheCycles)
{
    Signal hum = pinkNoise(kSecond, 0.2f, 4);
    const Signal tone = sine(kSecond, 120.0, kTestSampleRate, 0.3f);
    for (std::size_t n = 0; n < hum.size(); ++n) {
        hum[n] += tone[n];
    }
    InvarianceOptions options;
    options.inputChannels = 2;
    options.outputChannels = 2;
    for (const char *ear : {"left", "right"}) {
        const InvarianceReport report = checkBlockSizeInvariance(
            [ear] {
                auto block = std::make_unique<FrequencyRaising>();
                EXPECT_TRUE(block->setParam("ear", ParamValue::ofEnum(ear)).has_value());
                EXPECT_TRUE(block->setParam("move3Used", ParamValue::ofBool(true)).has_value());
                EXPECT_TRUE(block->setParam("move3Method", ParamValue::ofEnum("harmonics")).has_value());
                EXPECT_TRUE(block->setParam("move2Used", ParamValue::ofBool(true)).has_value());
                EXPECT_TRUE(block->setParam("move2When", ParamValue::ofEnum("needed")).has_value());
                return std::unique_ptr<Block>(std::move(block));
            },
            {hum, tone},
            options);
        EXPECT_TRUE(report.passed) << ear << ": " << report.summary;
    }
}

TEST(FrequencyRaising, NothingIsAllocatedOnTheAudioThreadEvenWhileItsSettingsChange)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    FrequencyRaising block;
    rt::BlockMeter meter;
    meter.setAnalysing(true);
    BlockRunner runner(block, 2, 2);
    runner.setMeter(&meter);
    ASSERT_TRUE(runner.prepare().has_value());
    const MultiSignal input{whiteNoise(16384, 0.5f, 3), whiteNoise(16384, 0.5f, 4)};
    std::size_t before = 0;
    (void)runner.runWatchingForAllocations(input, CycleShape::random(16, 1024, 5), before);
    set(block, "move1Method", ParamValue::ofEnum("harmonics"));
    set(block, "move2Used", ParamValue::ofBool(true));
    set(block, "ear", ParamValue::ofEnum("right"));
    std::size_t during = 0;
    (void)runner.runWatchingForAllocations(input, CycleShape::random(16, 1024, 6), during);
    EXPECT_EQ(before, 0U);
    EXPECT_EQ(during, 0U);
}

TEST(FrequencyRaising, ItsLatencyFollowsTheRate)
{
    for (const auto &[rate, expected] :
         {std::pair{44100.0, 4096U}, std::pair{48000.0, 4096U}, std::pair{96000.0, 8192U}}) {
        FrequencyRaising block;
        PrepareSpec spec;
        spec.sampleRate = rate;
        ASSERT_TRUE(block.prepare(spec).has_value());
        EXPECT_EQ(block.info().latencySamples, expected) << rate;
    }
}

TEST(FrequencyRaising, AMinuteOfFourMovesCostsLessThanThreePerCentOfACore)
{
    // Every move in use, one of each method and a "when needed", on speech-
    // like sound: a buzz with its harmonics, under a little hiss.
    FrequencyRaising block;
    for (const char *used : {"move2Used", "move3Used", "move4Used"}) {
        set(block, used, ParamValue::ofBool(true));
    }
    set(block, "move2Method", ParamValue::ofEnum("compress"));
    set(block, "move3Method", ParamValue::ofEnum("harmonics"));
    set(block, "move4When", ParamValue::ofEnum("needed"));
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    Signal sound = whiteNoise(kSecond * 60, 0.02f, 8);
    for (std::size_t n = 0; n < sound.size(); ++n) {
        const double t = static_cast<double>(n) / kTestSampleRate;
        double buzz = 0.0;
        for (int harmonic = 1; harmonic * 120.0 < 4000.0; ++harmonic) {
            buzz += std::sin(2.0 * std::numbers::pi * harmonic * 120.0 * t) / harmonic;
        }
        sound[n] += static_cast<float>(0.1 * buzz);
    }
    const auto started = std::chrono::steady_clock::now();
    (void)runner.run(MultiSignal{sound, sound}, CycleShape::fixed(256));
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const double share = took / 60.0;
    std::cerr << "frequency raising: a minute of four moves in " << took << " s, " << share * 100.0 << " % of a core\n";
#ifdef NDEBUG
    EXPECT_LT(share, 0.03) << "a minute took " << took << " s";
#else
    GTEST_SKIP() << "the figure above is from a debug build";
#endif
}
