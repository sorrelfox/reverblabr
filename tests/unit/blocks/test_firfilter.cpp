// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// FIR Filter: the same as a convolution done the slow way to within
// -90 dB, a response changed while running without a click, the latency
// reported, and a 2 s stereo response cheap enough for quantum 256.

#include "allocationtrap.h"
#include "analysis.h"
#include "blocks/firfilter.h"
#include "blocksizeinvariance.h"
#include "dsp/decibel.h"
#include "offlinerunner.h"
#include "rt/meter.h"
#include "signals.h"

#include <gtest/gtest.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <memory>
#include <numbers>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace rb;
using namespace rb::blocks;
using namespace rb::dsp;
using namespace rb::test;

namespace
{

constexpr std::size_t kSecond = 48000;
constexpr std::size_t kLatency = FirFilter::kPartition;

/// A folder of its own for a test's files, gone afterwards.
class ScratchFolder
{
public:
    ScratchFolder()
    {
        static std::atomic<int> s_count{0};
        const char *cache = std::getenv("XDG_CACHE_HOME");
        const std::filesystem::path base =
            cache != nullptr && *cache != '\0' ? std::filesystem::path{cache} : std::filesystem::temp_directory_path();
        m_path = base / std::format("rb-fir-test-{}-{}", ::getpid(), s_count.fetch_add(1));
        std::filesystem::remove_all(m_path);
        std::filesystem::create_directories(m_path);
    }

    ~ScratchFolder()
    {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }

    ScratchFolder(const ScratchFolder &) = delete;
    ScratchFolder &operator=(const ScratchFolder &) = delete;

    [[nodiscard]] std::string file(const std::string &name) const
    {
        return (m_path / name).string();
    }

private:
    std::filesystem::path m_path;
};

void put16(std::ofstream &out, std::uint16_t value)
{
    out.put(static_cast<char>(value & 0xff));
    out.put(static_cast<char>(value >> 8));
}

void put32(std::ofstream &out, std::uint32_t value)
{
    for (int shift = 0; shift < 32; shift += 8) {
        out.put(static_cast<char>((value >> shift) & 0xff));
    }
}

/// \a channels as a 32-bit floating-point WAV at \a rate, written by hand
/// rather than through the code under test, every sample exactly as given.
void writeWav(const std::string &path, std::uint32_t rate, const std::vector<std::vector<float>> &channels)
{
    const auto count = static_cast<std::uint16_t>(channels.size());
    const auto frames = static_cast<std::uint32_t>(channels.front().size());
    const std::uint32_t bytes = frames * count * 4;
    std::ofstream out(path, std::ios::binary);
    out.write("RIFF", 4);
    put32(out, 36 + bytes);
    out.write("WAVEfmt ", 8);
    put32(out, 16);
    put16(out, 3); // IEEE floating point
    put16(out, count);
    put32(out, rate);
    put32(out, rate * count * 4);
    put16(out, static_cast<std::uint16_t>(count * 4));
    put16(out, 32);
    out.write("data", 4);
    put32(out, bytes);
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        for (const std::vector<float> &channel : channels) {
            out.write(reinterpret_cast<const char *>(&channel[frame]), 4);
        }
    }
}

/// A response like a small room's: noise dying away by 60 dB over its
/// length, a different noise for every channel.
[[nodiscard]] std::vector<std::vector<float>> roomResponse(std::size_t channels, std::size_t taps, std::uint32_t seed)
{
    std::vector<std::vector<float>> response;
    for (std::size_t channel = 0; channel < channels; ++channel) {
        Signal noise = whiteNoise(taps, 0.5f, seed + static_cast<std::uint32_t>(channel));
        for (std::size_t n = 0; n < taps; ++n) {
            noise[n] *= static_cast<float>(std::pow(10.0, -3.0 * static_cast<double>(n) / static_cast<double>(taps)));
        }
        response.push_back(std::move(noise));
    }
    return response;
}

/// A click \a at samples in, \a length long, in \a channels channels.
[[nodiscard]] std::vector<std::vector<float>>
click(std::size_t length, std::size_t at = 0, float amplitude = 1.0f, std::size_t channels = 1)
{
    return std::vector<std::vector<float>>(channels, impulse(length, at, amplitude));
}

/// \a input through \a response the slow way, in doubles: every output
/// sample the sum of every tap times the input that far back.
[[nodiscard]] MultiSignal directConvolution(const MultiSignal &input, const std::vector<std::vector<float>> &response)
{
    const std::size_t frames = input[0].size();
    // Which input through which response reaches each output, as the block
    // does it: one response for both, one a side, or true stereo.
    auto through = [&](std::size_t output) -> std::vector<std::pair<std::size_t, std::size_t>> {
        if (response.size() == 1) {
            return {{output, 0}};
        }
        if (response.size() == 2) {
            return {{output, output}};
        }
        return {{0, output}, {1, 2 + output}};
    };
    MultiSignal out(2, Signal(frames, 0.0f));
    for (std::size_t output = 0; output < 2; ++output) {
        for (std::size_t n = 0; n < frames; ++n) {
            double sum = 0.0;
            for (const auto &[channel, path] : through(output)) {
                const std::vector<float> &h = response[path];
                const std::size_t reach = std::min(h.size(), n + 1);
                for (std::size_t k = 0; k < reach; ++k) {
                    sum += static_cast<double>(h[k]) * static_cast<double>(input[channel][n - k]);
                }
            }
            out[output][n] = static_cast<float>(sum);
        }
    }
    return out;
}

/// How far \a actual, taken \a delay samples later, is from \a expected at
/// worst, in dB under \a expected's peak.
[[nodiscard]] double worstErrorDb(const Signal &actual, const Signal &expected, std::size_t delay)
{
    double worst = 0.0;
    double loudest = 0.0;
    for (std::size_t n = 0; n + delay < actual.size() && n < expected.size(); ++n) {
        worst = std::max(worst, std::abs(static_cast<double>(actual[n + delay]) - expected[n]));
        loudest = std::max(loudest, std::abs(static_cast<double>(expected[n])));
    }
    return 20.0 * std::log10(std::max(worst, 1e-30) / loudest);
}

/// Where the loudest sample of \a signal is.
[[nodiscard]] std::size_t loudestAt(const Signal &signal)
{
    std::size_t at = 0;
    for (std::size_t n = 1; n < signal.size(); ++n) {
        if (std::abs(signal[n]) > std::abs(signal[at])) {
            at = n;
        }
    }
    return at;
}

void setFile(FirFilter &block, const std::string &path)
{
    ASSERT_TRUE(block.setParam("file", ParamValue::ofString(path)).has_value());
}

void setFlag(FirFilter &block, const char *id, bool on)
{
    ASSERT_TRUE(block.setParam(id, ParamValue::ofBool(on)).has_value());
}

/// Runs \a signal through \a response and checks it against the slow way.
void expectMatchesDirectConvolution(const std::vector<std::vector<float>> &response, const MultiSignal &signal)
{
    ScratchFolder folder;
    const std::string path = folder.file("response.wav");
    writeWav(path, 48000, response);

    FirFilter block;
    setFile(block, path);
    setFlag(block, "normalise", false);
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    ASSERT_EQ(block.taps(), response.front().size());

    MultiSignal input = signal;
    for (Signal &channel : input) {
        channel.resize(channel.size() + kLatency, 0.0f);
    }
    const MultiSignal out = runner.run(input, CycleShape::random(1, 1024, 84));
    const MultiSignal expected = directConvolution(signal, response);
    for (std::size_t channel = 0; channel < 2; ++channel) {
        const double error = worstErrorDb(out[channel], expected[channel], kLatency);
        std::cout << response.size() << " channel(s), " << response.front().size() << " taps, output " << channel
                  << ": worst error " << error << " dB\n";
        EXPECT_LT(error, -90.0);
    }
}

} // namespace

TEST(FirFilterTest, ItIsWhatConvolvingSampleBySampleGivesWithin90Db)
{
    // The criterion. A quarter of a second of stereo room, 47 partitions
    // and a part one, against the sum over every tap done in doubles.
    const Signal left = whiteNoise(kSecond / 2, 0.5f, 1);
    const Signal right = whiteNoise(kSecond / 2, 0.5f, 2);
    expectMatchesDirectConvolution(roomResponse(2, kSecond / 4, 10), MultiSignal{left, right});
}

TEST(FirFilterTest, TrueStereoAndMonoResponsesAreConvolvedAsTheyShouldBe)
{
    // True stereo: each side of the input through its own response to each
    // side of the output. Mono: the one response on both sides.
    const Signal left = whiteNoise(kSecond / 4, 0.5f, 3);
    const Signal right = sine(kSecond / 4, 440.0, kTestSampleRate, 0.5f);
    expectMatchesDirectConvolution(roomResponse(4, 3000, 20), MultiSignal{left, right});
    expectMatchesDirectConvolution(roomResponse(1, 700, 30), MultiSignal{left, right});
}

TEST(FirFilterTest, WithoutAResponseItIsTheInputAPartitionLate)
{
    FirFilter block;
    EXPECT_EQ(block.info().latencySamples, kLatency);
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    EXPECT_EQ(block.shape(), FirFilter::kNoFile);
    const Signal noise = whiteNoise(4096, 0.5f, 4);
    const MultiSignal out = runner.run(MultiSignal{noise, noise});
    // Through a transform and back: the same to the last bit or two.
    for (std::size_t n = 0; n + kLatency < noise.size(); ++n) {
        ASSERT_NEAR(out[0][n + kLatency], noise[n], 1e-6f) << "at " << n;
        ASSERT_NEAR(out[1][n + kLatency], noise[n], 1e-6f) << "at " << n;
    }
    for (std::size_t n = 0; n < kLatency; ++n) {
        ASSERT_EQ(out[0][n], 0.0f);
    }
}

TEST(FirFilterTest, AFileItCannotUseIsSaidAndTheSoundPassesThrough)
{
    ScratchFolder folder;
    const std::string text = folder.file("notes.wav");
    std::ofstream(text) << "not audio";
    const std::string threeChannels = folder.file("three.wav");
    writeWav(threeChannels, 48000, click(100, 0, 1.0f, 3));

    for (const std::string &path : {text, folder.file("missing.wav"), threeChannels}) {
        FirFilter block;
        setFile(block, path);
        BlockRunner runner(block, 2, 2);
        ASSERT_TRUE(runner.prepare().has_value());
        EXPECT_EQ(block.shape(), FirFilter::kUnusable) << path;
        const Signal noise = whiteNoise(2048, 0.5f, 5);
        const MultiSignal out = runner.run(MultiSignal{noise, noise});
        EXPECT_NEAR(out[0][kLatency + 100], noise[100], 1e-6f) << path;
        // Still saved: the file may be back next time.
        EXPECT_EQ(block.saveState().at("file").text(), path);
    }
}

TEST(FirFilterTest, ItSoundsTheSameWhateverTheQuantum)
{
    ScratchFolder folder;
    const std::string path = folder.file("room.wav");
    writeWav(path, 48000, roomResponse(4, 5000, 40));
    InvarianceOptions options;
    options.inputChannels = 2;
    options.outputChannels = 2;
    const Signal noise = whiteNoise(16384, 0.3f, 6);
    const InvarianceReport report = checkBlockSizeInvariance(
        [&path] {
            auto block = std::make_unique<FirFilter>();
            EXPECT_TRUE(block->setParam("file", ParamValue::ofString(path)).has_value());
            return std::unique_ptr<Block>(std::move(block));
        },
        MultiSignal{noise, whiteNoise(16384, 0.3f, 7)},
        options);
    EXPECT_TRUE(report.passed) << report.summary;
}

TEST(FirFilterTest, ChangingTheResponseWhileRunningIsClickFree)
{
    // The criterion. A 1 kHz tone through a click, then through a click
    // half a period later — the tone turned upside down — and two seconds
    // long, so that the history grows as well. Swapped outright, the
    // output would jump by the tone's whole height, twice; crossed over,
    // no step is bigger than the tone's own.
    ScratchFolder folder;
    const std::string first = folder.file("first.wav");
    const std::string second = folder.file("second.wav");
    writeWav(first, 48000, click(64, 0, 1.0f, 2));
    writeWav(second, 48000, click(2 * kSecond, 24, 1.0f, 2));

    FirFilter block;
    setFile(block, first);
    setFlag(block, "normalise", false);
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());

    // A cosine: partitions start at multiples of 256 samples, which are
    // whole periods of the tone plus a third, and every one of them falls
    // on a peak of this one, where turning it over is the biggest step.
    const Signal tone = sine(kSecond / 2, 1000.0, kTestSampleRate, 0.5f, std::numbers::pi / 2.0);
    // The tone's own steps, once it has come through: before, the output
    // is the silence of the latency and then the tone at its peak.
    const Signal before = runner.run(MultiSignal{tone, tone})[0];
    const float ownStep = maxSampleStep(std::span<const float>{before}.subspan(kLatency));
    setFile(block, second);
    EXPECT_TRUE(block.changing());
    // Five hundred whole periods on: the tone goes on where it was.
    const Signal &more = tone;
    const MultiSignal out = runner.run(MultiSignal{more, more});
    EXPECT_FALSE(block.changing());
    EXPECT_EQ(block.taps(), 2 * kSecond);

    const float step = std::max(maxSampleStep(out[0]), maxSampleStep(out[1]));
    std::cout << "the tone's own largest step " << ownStep << ", across the change " << step << "\n";
    EXPECT_LT(step, ownStep * 1.05f);
    // And the new response is the one heard afterwards.
    for (std::size_t n = out[0].size() - 1000; n < out[0].size(); ++n) {
        ASSERT_NEAR(out[0][n], more[n - kLatency - 24], 1e-5f) << "at " << n;
    }
}

TEST(FirFilterTest, ALongerResponseHearsWhatCameBeforeItAtOnce)
{
    // A click 700 samples in reaches three partitions back, further than
    // the first response needed: the history the audio thread takes with
    // the longer response has to bring the recent past with it, or the new
    // response is heard coming in partition by partition during the fade.
    ScratchFolder folder;
    const std::string first = folder.file("first.wav");
    const std::string second = folder.file("second.wav");
    writeWav(first, 48000, click(1000, 0, 1.0f, 2));
    writeWav(second, 48000, click(2 * kSecond, 700, 1.0f, 2));
    constexpr std::size_t kDelay = 700;

    FirFilter block;
    setFile(block, first);
    setFlag(block, "normalise", false);
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());

    // Whole partitions, so that the response is taken at the end of the
    // first partition of the second run and heard from the second.
    constexpr std::size_t kBefore = 40 * kLatency;
    const Signal noise = whiteNoise(kBefore + 12 * kLatency, 0.5f, 14);
    const Signal head(noise.begin(), noise.begin() + kBefore);
    const Signal tail(noise.begin() + kBefore, noise.end());
    (void)runner.run(MultiSignal{head, head});
    setFile(block, second);
    const Signal out = runner.run(MultiSignal{tail, tail})[0];

    double worst = 0.0;
    for (std::size_t n = kLatency; n < tail.size(); ++n) {
        const std::size_t at = kBefore + n;
        const double into = std::min(1.0, static_cast<double>(n - kLatency + 1) / FirFilter::kFadeFrames);
        const double expected = (1.0 - into) * noise[at - kLatency] + into * noise[at - kLatency - kDelay];
        worst = std::max(worst, std::abs(out[n] - expected));
    }
    std::cout << "worst difference from the crossfade of the two: " << worst << "\n";
    EXPECT_LT(worst, 1e-5);
}

TEST(FirFilterTest, ResponsesSentWhileItRunsAreTakenWholeAndFreedElsewhere)
{
    // The engine thread sends response after response — longer, shorter,
    // none — while another thread runs the block the whole time, as the
    // audio thread does. Every one the audio thread takes it hands back,
    // and the last one sent is the one heard. Under tsan this is also the
    // race check of the handover.
    ScratchFolder folder;
    const std::string shortOne = folder.file("short.wav");
    const std::string longOne = folder.file("long.wav");
    writeWav(shortOne, 48000, roomResponse(2, 1000, 110));
    writeWav(longOne, 48000, roomResponse(4, 20000, 120));

    FirFilter block;
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    std::atomic<bool> done{false};
    std::atomic<int> cycles{0};
    std::thread audio([&] {
        const Signal noise = whiteNoise(1024, 0.3f, 15);
        while (!done.load()) {
            (void)runner.run(MultiSignal{noise, noise}, CycleShape::fixed(128));
            cycles.fetch_add(1);
        }
    });
    const std::array<std::string, 3> files{longOne, shortOne, std::string{}};
    for (int round = 0; round < 30; ++round) {
        setFile(block, files[static_cast<std::size_t>(round) % files.size()]);
        const int seen = cycles.load();
        while (cycles.load() < seen + 2) {
            std::this_thread::yield();
        }
    }
    setFile(block, longOne);
    const int seen = cycles.load();
    while (cycles.load() < seen + 4) {
        std::this_thread::yield();
    }
    done.store(true);
    audio.join();
    EXPECT_FALSE(block.changing());
    EXPECT_EQ(block.shape(), 4.0f);
    EXPECT_EQ(block.taps(), 20000U);
}

TEST(FirFilterTest, ARunTouchesTheAllocatorNotAtAll)
{
    // Taking a new response and a longer history, crossing over and
    // retiring the old ones, all under the trap.
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    ScratchFolder folder;
    const std::string first = folder.file("first.wav");
    const std::string second = folder.file("second.wav");
    writeWav(first, 48000, roomResponse(2, 3000, 50));
    writeWav(second, 48000, roomResponse(4, 30000, 60));

    FirFilter block;
    setFile(block, first);
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const Signal noise = whiteNoise(kSecond, 0.3f, 8);
    (void)runner.run(MultiSignal{noise, noise});
    setFile(block, second);
    std::size_t allocations = 1;
    const MultiSignal out =
        runner.runWatchingForAllocations(MultiSignal{noise, noise}, CycleShape::random(1, 512, 9), allocations);
    EXPECT_EQ(allocations, 0U);
    EXPECT_FALSE(block.changing());
    EXPECT_EQ(block.shape(), 4.0f);
    EXPECT_EQ(out[0].size(), noise.size());
}

TEST(FirFilterTest, NormalisingMakesNoiseComeOutAsLoudAsItWentIn)
{
    ScratchFolder folder;
    const std::string quiet = folder.file("quiet.wav");
    writeWav(quiet, 48000, click(100, 0, 0.25f));
    const std::string room = folder.file("room.wav");
    writeWav(room, 48000, roomResponse(2, 9600, 70));

    const Signal noise = whiteNoise(kSecond, 0.3f, 10);
    auto level = [&](const std::string &path, bool normalise) {
        FirFilter block;
        setFile(block, path);
        setFlag(block, "normalise", normalise);
        BlockRunner runner(block, 2, 2);
        EXPECT_TRUE(runner.prepare().has_value());
        const MultiSignal out = runner.run(MultiSignal{noise, noise});
        // After the response has filled up, against the same stretch of
        // the input.
        const std::span<const float> left{out[0].begin() + kSecond / 4, out[0].end()};
        const std::span<const float> right{out[1].begin() + kSecond / 4, out[1].end()};
        const std::span<const float> in{noise.begin() + kSecond / 4 - kLatency, noise.end() - kLatency};
        return std::max(rmsDb(left), rmsDb(right)) - rmsDb(in);
    };
    EXPECT_NEAR(level(quiet, false), -12.04f, 0.01f);
    EXPECT_NEAR(level(quiet, true), 0.0f, 0.01f);
    // A room: noise through it is as loud as noise, at the louder side.
    EXPECT_NEAR(level(room, true), 0.0f, 0.3f);
}

TEST(FirFilterTest, TrimmingTakesTheSilenceOffBothEnds)
{
    ScratchFolder folder;
    const std::string path = folder.file("padded.wav");
    // Half a partition of nothing, the response, and three thousand
    // samples of nothing.
    std::vector<std::vector<float>> response = click(128 + 3000, 128, 1.0f, 2);
    response[1][129] = 0.5f;
    writeWav(path, 48000, response);

    const Signal pulse = impulse(4096, 0);
    for (const bool trim : {false, true}) {
        FirFilter block;
        setFile(block, path);
        setFlag(block, "normalise", false);
        setFlag(block, "trim", trim);
        BlockRunner runner(block, 2, 2);
        ASSERT_TRUE(runner.prepare().has_value());
        const MultiSignal out = runner.run(MultiSignal{pulse, pulse});
        EXPECT_EQ(block.taps(), trim ? 2U : 3128U);
        EXPECT_EQ(loudestAt(out[0]), kLatency + (trim ? 0 : 128));
        EXPECT_FLOAT_EQ(out[1][kLatency + (trim ? 1 : 129)], 0.5f);
    }
}

TEST(FirFilterTest, AResponseAtAnotherRateIsHeardAtTheSameTimes)
{
    // A click 10 ms in, recorded at 44.1 kHz, is 10 ms in at 48.
    ScratchFolder folder;
    const std::string path = folder.file("cd.wav");
    writeWav(path, 44100, click(4410, 441));

    FirFilter block;
    setFile(block, path);
    setFlag(block, "normalise", false);
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    EXPECT_EQ(block.taps(), 4800U);
    const MultiSignal out = runner.run(MultiSignal{impulse(8192, 0), impulse(8192, 0)});
    EXPECT_EQ(loudestAt(out[0]), kLatency + 480);
}

TEST(FirFilterTest, ALongerResponseIsCutAtTenSeconds)
{
    ScratchFolder folder;
    const std::string path = folder.file("hall.wav");
    writeWav(path, 48000, std::vector<std::vector<float>>{whiteNoise(12 * kSecond, 0.1f, 11)});
    FirFilter block;
    setFile(block, path);
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    EXPECT_EQ(block.shape(), 1.0f);
    EXPECT_EQ(block.taps(), 10 * kSecond);
}

TEST(FirFilterTest, ThePreGainTurnsTheInputDown)
{
    FirFilter block;
    ASSERT_TRUE(block.setParam("preGainDb", ParamValue::ofFloat(-6.0f)).has_value());
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const Signal noise = whiteNoise(4096, 0.5f, 12);
    const MultiSignal out = runner.run(MultiSignal{noise, noise});
    EXPECT_NEAR(out[0][kLatency + 1000] / noise[1000], dbToGain(-6.0f), 1e-6f);
}

TEST(FirFilterTest, TheFileAndTheSettingsAreSavedAndPutBack)
{
    ScratchFolder folder;
    const std::string path = folder.file("room.wav");
    writeWav(path, 48000, roomResponse(2, 2000, 80));

    FirFilter block;
    setFile(block, path);
    setFlag(block, "trim", true);
    ASSERT_TRUE(block.setParam("preGainDb", ParamValue::ofFloat(-3.0f)).has_value());
    const ParamValueMap saved = block.saveState();
    EXPECT_EQ(saved.at("file").text(), path);

    FirFilter again;
    ASSERT_TRUE(again.loadState(saved).has_value());
    BlockRunner runner(again, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    EXPECT_EQ(again.shape(), 2.0f);
    EXPECT_EQ(again.param("trim").asNumber(), 1.0);
    EXPECT_EQ(again.param("preGainDb").asNumber(), -3.0);
}

TEST(FirFilterTest, ItPublishesWhatItIsUsing)
{
    ScratchFolder folder;
    const std::string path = folder.file("room.wav");
    writeWav(path, 48000, roomResponse(4, kSecond, 90));

    FirFilter block;
    rt::BlockMeter meter;
    BlockRunner runner(block, 2, 2);
    runner.setMeter(&meter);
    ASSERT_TRUE(runner.prepare().has_value());
    (void)runner.run(MultiSignal{Signal(512, 0.0f), Signal(512, 0.0f)});
    EXPECT_EQ(meter.value(FirFilter::kShapeValue), FirFilter::kNoFile);
    EXPECT_EQ(meter.value(FirFilter::kTapsValue), 0.0f);
    EXPECT_NEAR(meter.value(FirFilter::kDelayMsValue), 5.333f, 0.001f);

    setFile(block, path);
    (void)runner.run(MultiSignal{Signal(2048, 0.0f), Signal(2048, 0.0f)});
    EXPECT_EQ(meter.value(FirFilter::kShapeValue), 4.0f);
    EXPECT_EQ(meter.value(FirFilter::kTapsValue), static_cast<float>(kSecond));
    EXPECT_NEAR(meter.value(FirFilter::kSecondsValue), 1.0f, 1e-6f);
}

TEST(FirFilterTest, TwoSecondsOfStereoResponseFitQuantum256)
{
    // The criterion's arithmetic: 375 partitions a side, one partition every
    // 256 samples, so at quantum 256 every cycle does all of the work once.
    // Measured over twenty seconds.
    ScratchFolder folder;
    const std::string path = folder.file("hall.wav");
    writeWav(path, 48000, roomResponse(2, 2 * kSecond, 100));
    FirFilter block;
    setFile(block, path);
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    ASSERT_EQ(block.taps(), 2 * kSecond);

    constexpr std::size_t kSeconds = 20;
    const Signal noise = whiteNoise(kSecond * kSeconds, 0.3f, 13);
    const auto started = std::chrono::steady_clock::now();
    (void)runner.run(MultiSignal{noise, noise}, CycleShape::fixed(256));
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const double share = took / static_cast<double>(kSeconds);
    std::cerr << "fir: 2 s stereo response, " << kSeconds << " s of stereo in " << took << " s, " << share * 100.0
              << " % of a core, " << share * 256.0 / 48.0 << " ms a 5.33 ms cycle\n";
#ifdef NDEBUG
    EXPECT_LT(share, 0.05) << kSeconds << " s took " << took << " s";
#else
    GTEST_SKIP() << "the figure above is from a debug build";
#endif
}
