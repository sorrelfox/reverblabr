// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Model block, with its real host and real ONNX files written by the
// test: what comes out, how late, and above all what does not. **Nothing
// but the model's answer ever leaves this block**, so every state a block
// can be in is run here with a tone going in, and the output held to exact
// zeros.

#include "allocationtrap.h"
#include "blocks/modelblock/modelblock.h"
#include "blocksizeinvariance.h"
#include "dsp/audiobuffer.h"
#include "graph/compiledgraph.h"
#include "model/recognise.h"
#include "model/voice.h"
#include "offlinerunner.h"
#include "onnxbytes.h"
#include "rt/meter.h"
#include "signals.h"
#include "testblocks.h"

#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(__has_feature)
#if __has_feature(realtime_sanitizer)
#include <sanitizer/rtsan_interface.h>
#define RB_TEST_UNDER_RTSAN 1
#endif
#endif

using namespace rb;
using namespace rb::blocks;
using namespace rb::dsp;
using namespace rb::test;
using namespace std::chrono_literals;

namespace
{

namespace fs = std::filesystem;

/// kill(2) for a host this test started, and nothing else. **A pid of 0 or -1
/// is not a process**: kill(0) signals the whole process group and kill(-1)
/// every process the user owns, which is the desktop session. hostPid() is -1
/// whenever no host is running, and a wait that fails inside a helper does not
/// end the test, so a host that never came up gets here. Anything but an
/// ordinary pid of another process is a failed test, never a signal.
[[nodiscard]] bool signalHost(int pid, int signal)
{
    if (pid <= 1 || pid == ::getpid()) {
        ADD_FAILURE() << "refusing to signal pid " << pid;
        return false;
    }
    return ::kill(pid, signal) == 0;
}

constexpr double kRate = 48000.0;
constexpr std::uint32_t kHop = 480;
/// The default spare time, which the lock-step below leans on.
constexpr std::uint32_t kSpare = 2400;
/// How long a test waits for what a loaded machine may take a while over.
constexpr auto kPatience = 30s;
/// Longer than any fade this block makes, at any cycle.
constexpr std::uint32_t kAfterFade = 4096;

/// A folder of the test's own, with a model in it.
class Folder
{
public:
    Folder()
    {
        std::string pattern = (fs::temp_directory_path() / "rb-model-block-XXXXXX").string();
        EXPECT_NE(::mkdtemp(pattern.data()), nullptr);
        m_path = pattern;
    }

    Folder(const Folder &) = delete;
    Folder &operator=(const Folder &) = delete;

    ~Folder()
    {
        std::error_code ignored;
        fs::remove_all(m_path, ignored);
    }

    [[nodiscard]] const std::string &path() const noexcept
    {
        return m_path;
    }

    void write(const std::string &name, const std::string &bytes) const
    {
        std::ofstream(fs::path(m_path) / name, std::ios::binary) << bytes;
    }

    /// \a model at \a rate, hopping \a hop, with \a extra in the file.
    void hold(const std::string &model,
              std::uint32_t rate = 48000,
              std::uint32_t hop = kHop,
              const std::string &extra = {},
              std::uint32_t channels = 1) const
    {
        write("model.onnx", model);
        write(std::string{model::kConfigurationName},
              std::format(R"({{"format": 1, "file": "model.onnx", "sampleRate": {}, "channels": {}, "frameSize": {},
                             "input": "audio", "output": "out"{}}})",
                          rate,
                          channels,
                          hop,
                          extra));
    }

private:
    std::string m_path;
};

/// A block, driven by hand: one cycle at a time, with what it wrote kept.
class Driven
{
public:
    explicit Driven(const std::string &folder, double rate = kRate)
    {
        if (!folder.empty()) {
            EXPECT_TRUE(block.setParam("folder", ParamValue::ofString(folder)).has_value());
        }
        spec.sampleRate = rate;
        spec.meter = &meter;
        EXPECT_TRUE(block.prepare(spec).has_value());
    }

    /// One cycle of \a left and \a right.
    void cycle(std::span<const float> left, std::span<const float> right)
    {
        const auto frames = static_cast<std::uint32_t>(left.size());
        m_in[0].assign(left.begin(), left.end());
        m_in[1].assign(right.begin(), right.end());
        m_out[0].assign(frames, 123.0f);
        m_out[1].assign(frames, 123.0f);
        std::array<float *, 2> in{m_in[0].data(), m_in[1].data()};
        std::array<float *, 2> out{m_out[0].data(), m_out[1].data()};
        ProcessContext context{AudioBlock{in.data(), 2, frames}, AudioBlock{out.data(), 2, frames}, frames};
        block.process(context);
        heard[0].insert(heard[0].end(), m_out[0].begin(), m_out[0].end());
        heard[1].insert(heard[1].end(), m_out[1].begin(), m_out[1].end());
    }

    /// \a signal in cycles of \a frames, each only once the host has done
    /// all it could with the one before, so a machine's load changes nothing.
    void lockStep(const Signal &signal, std::uint32_t frames)
    {
        for (std::size_t at = 0; at < signal.size(); at += frames) {
            const std::size_t count = std::min<std::size_t>(frames, signal.size() - at);
            const std::span<const float> part = std::span<const float>{signal}.subspan(at, count);
            cycle(part, part);
            waitForHost();
        }
    }

    void waitForHost()
    {
        const std::uint64_t written = block.framesWritten();
        const std::uint32_t behind = block.info().latencySamples - kSpare;
        const std::uint64_t wanted = written > behind ? written - behind : 0;
        const auto deadline = std::chrono::steady_clock::now() + kPatience;
        while (block.wetEnd() < wanted && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(100us);
        }
        ASSERT_GE(block.wetEnd(), wanted);
    }

    /// \a signal at the pace sound really goes.
    void paced(const Signal &signal, std::uint32_t frames)
    {
        const auto period = std::chrono::duration<double>(static_cast<double>(frames) / spec.sampleRate);
        auto next = std::chrono::steady_clock::now();
        for (std::size_t at = 0; at + frames <= signal.size(); at += frames) {
            const std::span<const float> part = std::span<const float>{signal}.subspan(at, frames);
            cycle(part, part);
            next += std::chrono::duration_cast<std::chrono::steady_clock::duration>(period);
            std::this_thread::sleep_until(next);
        }
    }

    /// Cycles of a tone, at the pace sound goes, until \a done says so.
    template <typename Done>
    [[nodiscard]] bool toneUntil(Done done, std::chrono::seconds limit = 20s, std::uint32_t frames = 256)
    {
        const Signal piece = sine(frames, 997.0, kRate, 0.5f);
        const auto deadline = std::chrono::steady_clock::now() + limit;
        while (std::chrono::steady_clock::now() < deadline) {
            cycle(piece, piece);
            if (done()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::duration<double>(frames / spec.sampleRate));
        }
        return false;
    }

    /// Whether the host's first run is over, cycles going by.
    void waitUntilRunning()
    {
        ASSERT_TRUE(toneUntil([this] { return block.state() == ModelBlock::Running; }))
            << "state " << static_cast<int>(block.state()) << " detail " << block.detail();
    }

    /// Stops listening: only what comes next counts.
    void forget()
    {
        heard[0].clear();
        heard[1].clear();
    }

    /// Whether everything kept since forget(), past \a from, is exact zeros.
    void expectSilence(std::size_t from = 0) const
    {
        ASSERT_GT(heard[0].size(), from);
        for (const Signal &channel : heard) {
            for (std::size_t index = from; index < channel.size(); ++index) {
                ASSERT_EQ(channel[index], 0.0f) << "sound at frame " << index;
            }
        }
    }

    rt::BlockMeter meter;
    PrepareSpec spec;
    ModelBlock block;
    std::array<Signal, 2> heard;

private:
    std::array<Signal, 2> m_in;
    std::array<Signal, 2> m_out;
};

/// The first frame that is not silence, or the length.
[[nodiscard]] std::size_t firstSound(const Signal &signal)
{
    const auto found = std::ranges::find_if(signal, [](float sample) { return std::abs(sample) > 1e-4f; });
    return static_cast<std::size_t>(found - signal.begin());
}

/// How far apart two neighbouring samples are, at the most.
[[nodiscard]] float largestStep(const Signal &signal)
{
    float largest = 0.0f;
    for (std::size_t index = 1; index < signal.size(); ++index) {
        largest = std::max(largest, std::abs(signal[index] - signal[index - 1]));
    }
    return largest;
}

/// A block whose host has its model loaded before any sound goes in, and
/// which waits for the host after every cycle, however it is cut.
class LockStepBlock final : public Block
{
public:
    explicit LockStepBlock(const std::string &folder)
    {
        (void)m_inner.setParam("folder", ParamValue::ofString(folder));
    }

    [[nodiscard]] const BlockInfo &info() const noexcept override
    {
        return m_inner.info();
    }

    [[nodiscard]] std::span<const ParamDescriptor> params() const noexcept override
    {
        return m_inner.params();
    }

    [[nodiscard]] std::expected<void, BlockError> prepare(const PrepareSpec &spec) override
    {
        const std::expected<void, BlockError> prepared = m_inner.prepare(spec);
        const auto deadline = std::chrono::steady_clock::now() + kPatience;
        while (!m_inner.hostReady() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(1ms);
        }
        return prepared;
    }

    // **Waits on purpose**, which a real block never may: these tests run
    // it offline, with no audio thread to keep in time, and the wait is
    // what makes the result the same however a cycle is cut. So
    // -Wfunction-effects is quietened for this function alone, and the
    // real-time sanitizer for the wait alone: the model block's own
    // process() before it is still checked.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfunction-effects"
#endif
    void process(ProcessContext &context) noexcept RB_NONBLOCKING override
    {
        m_inner.process(context);
        const std::uint64_t written = m_inner.framesWritten();
        const std::uint32_t behind = m_inner.info().latencySamples - kSpare;
        const std::uint64_t wanted = written > behind ? written - behind : 0;
#if defined(RB_TEST_UNDER_RTSAN)
        const __rtsan::ScopedDisabler waitingOnPurpose;
#endif
        const auto deadline = std::chrono::steady_clock::now() + kPatience;
        while (m_inner.wetEnd() < wanted && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(50us);
        }
    }
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

    void reset() noexcept RB_NONBLOCKING override
    {
        m_inner.reset();
    }

    void applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING override
    {
        m_inner.applyParam(index, value);
    }

    [[nodiscard]] std::expected<void, BlockError> setParam(std::string_view id, const ParamValue &value) override
    {
        return m_inner.setParam(id, value);
    }

    [[nodiscard]] ParamValue param(std::string_view id) const override
    {
        return m_inner.param(id);
    }

    [[nodiscard]] ParamValueMap saveState() const override
    {
        return m_inner.saveState();
    }

    [[nodiscard]] std::expected<void, BlockError> loadState(const ParamValueMap &state) override
    {
        return m_inner.loadState(state);
    }

private:
    ModelBlock m_inner;
};

class ModelBlockTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        ::setenv("REVERBLABR_MODEL_HOST", RB_TEST_MODEL_HOST, 1);
    }
};

} // namespace

TEST_F(ModelBlockTest, SaysWhatItIs)
{
    ModelBlock block;
    EXPECT_EQ(block.info().typeId, "effect.model");
    EXPECT_EQ(block.info().category, BlockCategory::Effects);
    EXPECT_TRUE(block.info().silentWhenOff) << "the graph must never pass the input round this block";
    EXPECT_EQ(block.info().latencySamples, 0U);
    EXPECT_EQ(block.preparedShape(), 0U);
}

TEST_F(ModelBlockTest, RunsAModel)
{
    Folder folder;
    folder.hold(gainModel(0.5f, kHop));
    Driven driven(folder.path());
    driven.waitUntilRunning();
    driven.forget();
    const Signal tone = sine(9600, 997.0, kRate, 0.5f);
    driven.lockStep(tone, 256);
    // Past the tone's way through: half of it, as it went in.
    const std::uint32_t latency = driven.block.info().latencySamples;
    EXPECT_EQ(latency, kHop + kSpare);
    const Signal &out = driven.heard[0];
    for (std::size_t index = latency; index < tone.size(); ++index) {
        ASSERT_FLOAT_EQ(out[index], tone[index - latency] * 0.5f) << index;
        ASSERT_FLOAT_EQ(driven.heard[1][index], tone[index - latency] * 0.5f) << index;
    }
    EXPECT_EQ(driven.block.state(), ModelBlock::Running);
    EXPECT_NEAR(driven.meter.value(ModelBlock::kStateValue), ModelBlock::Running, 0.0f);
}

TEST_F(ModelBlockTest, TheInputAndOutputCanBeOneBuffer)
{
    Folder folder;
    folder.hold(gainModel(0.5f, kHop));
    Driven driven("");
    Signal shared = sine(256, 997.0, kRate, 0.5f);
    std::array<float *, 2> both{shared.data(), shared.data()};
    ProcessContext context{AudioBlock{both.data(), 2, 256}, AudioBlock{both.data(), 2, 256}, 256};
    driven.block.process(context);
    EXPECT_EQ(*std::ranges::max_element(shared), 0.0f);
    EXPECT_EQ(*std::ranges::min_element(shared), 0.0f);
}

TEST_F(ModelBlockTest, ALanesTwoSidesStayTheirOwn)
{
    Folder folder;
    folder.hold(gainModel(1.0f, kHop));
    Driven driven(folder.path());
    driven.waitUntilRunning();
    driven.forget();
    const Signal left = sine(4864, 500.0, kRate, 0.5f);
    const Signal right = sine(4864, 1500.0, kRate, 0.25f);
    for (std::size_t at = 0; at + 256 <= left.size(); at += 256) {
        driven.cycle(std::span<const float>{left}.subspan(at, 256), std::span<const float>{right}.subspan(at, 256));
        driven.waitForHost();
    }
    const std::uint32_t latency = driven.block.info().latencySamples;
    for (std::size_t index = latency; index < left.size(); ++index) {
        ASSERT_FLOAT_EQ(driven.heard[0][index], left[index - latency]);
        ASSERT_FLOAT_EQ(driven.heard[1][index], right[index - latency]);
    }
}

TEST_F(ModelBlockTest, AStereoModelTakesBothSidesAtOnce)
{
    Folder folder;
    folder.hold(gainModel(0.5f, kHop, 2, 2), 48000, kHop, {}, 2);
    Driven driven(folder.path());
    driven.waitUntilRunning();
    driven.forget();
    const Signal tone = sine(4800, 997.0, kRate, 0.5f);
    driven.lockStep(tone, 256);
    const std::uint32_t latency = driven.block.info().latencySamples;
    ASSERT_FLOAT_EQ(driven.heard[1][4000], tone[4000 - latency] * 0.5f);
}

TEST_F(ModelBlockTest, AModelWithASpareDimensionRuns)
{
    Folder folder;
    folder.hold(gainModel(0.5f, kHop, 3));
    Driven driven(folder.path());
    driven.waitUntilRunning();
}

TEST_F(ModelBlockTest, DownmixIsOneRunOnBothSides)
{
    Folder folder;
    folder.hold(gainModel(1.0f, kHop));
    Driven driven("");
    ASSERT_TRUE(driven.block.setParam("folder", ParamValue::ofString(folder.path())).has_value());
    ASSERT_TRUE(driven.block.setParam("monoMode", ParamValue::ofEnum("downmix")).has_value());
    ASSERT_TRUE(driven.block.prepare(driven.spec).has_value());
    driven.waitUntilRunning();
    driven.forget();
    const Signal left = constant(9600, 0.5f);
    const Signal right = constant(9600, 0.0f);
    for (std::size_t at = 0; at + 256 <= left.size(); at += 256) {
        driven.cycle(std::span<const float>{left}.subspan(at, 256), std::span<const float>{right}.subspan(at, 256));
        driven.waitForHost();
    }
    EXPECT_FLOAT_EQ(driven.heard[0].back(), 0.25f);
    EXPECT_FLOAT_EQ(driven.heard[1].back(), 0.25f);
}

TEST_F(ModelBlockTest, SettingsThatMoveTheLatencyMoveTheShape)
{
    Folder folder;
    folder.hold(gainModel(1.0f, kHop));
    ModelBlock block;
    ASSERT_TRUE(block.setParam("folder", ParamValue::ofString(folder.path())).has_value());
    const std::uint32_t chosen = block.preparedShape();
    EXPECT_NE(chosen, 0U);
    ModelBlock same;
    ASSERT_TRUE(same.loadState(block.saveState()).has_value());
    EXPECT_EQ(same.preparedShape(), chosen) << "a fresh block with the same settings says the same";
    ASSERT_TRUE(block.setParam("spareTime", ParamValue::ofEnum("100ms")).has_value());
    EXPECT_NE(block.preparedShape(), chosen);
}

TEST_F(ModelBlockTest, TheSpareTimeIsPartOfTheLatency)
{
    Folder folder;
    folder.hold(gainModel(1.0f, kHop));
    ModelBlock block;
    ASSERT_TRUE(block.setParam("folder", ParamValue::ofString(folder.path())).has_value());
    ASSERT_TRUE(block.setParam("spareTime", ParamValue::ofEnum("20ms")).has_value());
    PrepareSpec spec;
    ASSERT_TRUE(block.prepare(spec).has_value());
    EXPECT_EQ(block.info().latencySamples, kHop + 960);
}

namespace
{

class ModelLatency : public ModelBlockTest, public ::testing::WithParamInterface<std::uint32_t>
{
};

/// Sends a click through and says how late it comes out.
[[nodiscard]] std::size_t measuredLatency(Driven &driven, std::uint32_t frames)
{
    driven.waitUntilRunning();
    driven.lockStep(silence(driven.block.info().latencySamples + 8192), frames);
    driven.forget();
    const std::size_t wanted = driven.block.info().latencySamples + std::size_t{8192};
    Signal click = impulse(wanted + frames, 0, 1.0f);
    driven.lockStep(click, frames);
    return firstSound(driven.heard[0]);
}

} // namespace

TEST_P(ModelLatency, ThePlainModelIsAsLateAsReported)
{
    Folder folder;
    folder.hold(gainModel(1.0f, kHop));
    Driven driven(folder.path());
    EXPECT_EQ(measuredLatency(driven, GetParam()), driven.block.info().latencySamples);
}

TEST_P(ModelLatency, AModelsOwnDelayIsTakenOutOfTheAnswer)
{
    // It answers a hop late and says so: frame n still answers frame n.
    Folder folder;
    folder.hold(
        oneHopLateModel(kHop),
        48000,
        kHop,
        std::format(R"(, "latencySamples": {}, "state": [{{"input": "state_in", "output": "state_out"}}])", kHop));
    Driven driven(folder.path());
    EXPECT_EQ(driven.block.info().latencySamples, 2 * kHop + kSpare);
    EXPECT_EQ(measuredLatency(driven, GetParam()), driven.block.info().latencySamples);
}

TEST_P(ModelLatency, AModelAtAnotherRateIsWithinASample)
{
    // A click through two resamplers comes out spread, and its peak is
    // where it was aimed.
    Folder folder;
    folder.hold(gainModel(1.0f, 160), 16000, 160);
    Driven driven(folder.path());
    driven.waitUntilRunning();
    const std::uint32_t frames = GetParam();
    const std::uint32_t latency = driven.block.info().latencySamples;
    driven.lockStep(silence(latency + 8192), frames);
    driven.forget();
    const Signal tone = sine(16384, 300.0, kRate, 0.5f);
    driven.lockStep(tone, frames);
    const Signal &out = driven.heard[0];
    // A tone's period is 160 samples: the best of the lags within half of it.
    int bestLag = 0;
    double best = -1.0;
    for (int lag = -79; lag <= 79; ++lag) {
        double crossed = 0.0;
        for (std::size_t index = latency + 2000; index + 100 < tone.size(); ++index) {
            crossed += static_cast<double>(out[static_cast<std::size_t>(static_cast<std::ptrdiff_t>(index) + lag)])
                * tone[index - latency];
        }
        if (crossed > best) {
            best = crossed;
            bestLag = lag;
        }
    }
    EXPECT_LE(std::abs(bestLag), 1) << "the tone is where the latency says";
    double wanted = 0.0;
    for (std::size_t index = latency + 2000; index + 100 < tone.size(); ++index) {
        wanted += static_cast<double>(tone[index - latency]) * tone[index - latency];
    }
    EXPECT_NEAR(best / wanted, 1.0, 0.02) << "and as loud as it went in";
}

INSTANTIATE_TEST_SUITE_P(Quanta, ModelLatency, ::testing::Values(64U, 128U, 256U, 512U, 1024U, 2048U));

TEST_F(ModelBlockTest, HowTheSoundIsCutChangesNothing)
{
    Folder folder;
    folder.hold(gainModel(0.5f, kHop));
    InvarianceOptions options;
    options.inputChannels = 2;
    options.outputChannels = 2;
    options.maxBlockFrames = 2048;
    const InvarianceReport report =
        checkBlockSizeInvariance([&folder] { return std::make_unique<LockStepBlock>(folder.path()); },
                                 spread(whiteNoise(48000, 0.25f), 2),
                                 options);
    EXPECT_TRUE(report.passed) << report.summary;
}

TEST_F(ModelBlockTest, ACycleTakesNoMemory)
{
    Folder folder;
    folder.hold(gainModel(0.5f, kHop));
    Driven driven(folder.path());
    driven.waitUntilRunning();
    Signal in = sine(256, 997.0, kRate, 0.5f);
    Signal out(256);
    Signal second(256);
    std::array<float *, 2> input{in.data(), in.data()};
    std::array<float *, 2> output{out.data(), second.data()};
    ProcessContext context{AudioBlock{input.data(), 2, 256}, AudioBlock{output.data(), 2, 256}, 256};
    for (int cycle = 0; cycle < 400; ++cycle) {
        const NoAllocations realTime;
        driven.block.process(context);
    }
    // And with the host gone, which is another way through.
    ASSERT_TRUE(signalHost(driven.block.hostPid(), SIGKILL));
    for (int cycle = 0; cycle < 400; ++cycle) {
        const NoAllocations realTime;
        driven.block.process(context);
    }
}

TEST_F(ModelBlockTest, ACycleWithNoModelTakesNoMemory)
{
    Driven driven("");
    Signal in = sine(256, 997.0, kRate, 0.5f);
    Signal out(256);
    std::array<float *, 2> input{in.data(), in.data()};
    std::array<float *, 2> output{out.data(), out.data()};
    ProcessContext context{AudioBlock{input.data(), 2, 256}, AudioBlock{output.data(), 2, 256}, 256};
    const NoAllocations realTime;
    driven.block.process(context);
}

// **Every state, a tone in and exact zeros out.**

TEST_F(ModelBlockTest, WithNoModelNothingLeaves)
{
    Driven driven("");
    driven.paced(sine(9600, 997.0, kRate, 0.5f), 256);
    driven.expectSilence();
    EXPECT_EQ(driven.block.state(), ModelBlock::NoModel);
    EXPECT_EQ(driven.block.info().latencySamples, 0U);
}

TEST_F(ModelBlockTest, WithAFolderItCannotUseNothingLeaves)
{
    Folder folder;
    folder.write("model.pth", "pickle");
    Driven driven(folder.path());
    driven.paced(sine(4800, 997.0, kRate, 0.5f), 256);
    driven.expectSilence();
    EXPECT_EQ(driven.block.state(), ModelBlock::Failed);
    EXPECT_EQ(driven.block.detail(), static_cast<std::uint32_t>(model::Refusal::CodeFiles));
    EXPECT_EQ(driven.meter.value(ModelBlock::kDetailValue), static_cast<float>(model::Refusal::CodeFiles));
}

TEST_F(ModelBlockTest, WithAFolderThatIsNotThereNothingLeaves)
{
    Driven driven("/nowhere/at/all");
    driven.paced(sine(4800, 997.0, kRate, 0.5f), 256);
    driven.expectSilence();
    EXPECT_EQ(driven.block.detail(), static_cast<std::uint32_t>(model::Refusal::NoFolder));
}

TEST_F(ModelBlockTest, AModelTheRuntimeRefusesLeavesNothing)
{
    // Its head reads as a model; the runtime will not have it.
    Folder folder;
    OnnxGraph graph;
    graph.inputs = {{"audio", {kHop}}};
    graph.outputs = {{"out", {kHop}}};
    graph.nodes = {{"NoSuchOperator", {"audio"}, {"out"}, {}}};
    folder.hold(onnxBytes(graph));
    Driven driven(folder.path());
    ASSERT_TRUE(driven.toneUntil([&driven] { return driven.block.state() == ModelBlock::Failed; }));
    EXPECT_EQ(driven.block.detail(), static_cast<std::uint32_t>(model::Refusal::RuntimeRefused));
    driven.expectSilence();
}

TEST_F(ModelBlockTest, AModelThatGivesTheWrongSizeLeavesNothing)
{
    Folder folder;
    // Says 480 in the file and 240 in the configuration.
    folder.hold(gainModel(1.0f, kHop), 48000, 240);
    Driven driven(folder.path());
    ASSERT_TRUE(driven.toneUntil([&driven] { return driven.block.state() == ModelBlock::Failed; }));
    EXPECT_EQ(driven.block.detail(), static_cast<std::uint32_t>(model::Refusal::WrongShape));
    driven.expectSilence();
}

TEST_F(ModelBlockTest, WhileItLoadsNothingLeaves)
{
    Folder folder;
    folder.hold(gainModel(0.5f, kHop));
    Driven driven(folder.path());
    // Hold the host where it is: a load that has not ended.
    ASSERT_TRUE(driven.toneUntil([&driven] { return driven.block.hostPid() > 0; }));
    ASSERT_TRUE(signalHost(driven.block.hostPid(), SIGSTOP));
    driven.paced(sine(9600, 997.0, kRate, 0.5f), 256);
    EXPECT_NE(driven.block.state(), ModelBlock::Running);
    // If the load got as far as the rings, this is a host that stopped, and
    // the fade is over by now; if not, there was never any sound.
    driven.forget();
    driven.paced(sine(9600, 997.0, kRate, 0.5f), 256);
    driven.expectSilence();
    // Unless it was ended for standing still: then there is none to wake.
    if (const int host = driven.block.hostPid(); host > 0) {
        (void)signalHost(host, SIGCONT);
    }
}

TEST_F(ModelBlockTest, AModelTooSlowLeavesNothingAndSaysSo)
{
    Folder folder;
    folder.hold(slowModel(kHop, 3000000));
    Driven driven(folder.path());
    ASSERT_TRUE(driven.toneUntil([&driven] { return driven.block.state() == ModelBlock::CannotKeepUp; }, 60s))
        << "state " << static_cast<int>(driven.block.state());
    driven.forget();
    ASSERT_TRUE(driven.toneUntil([&driven] { return driven.meter.value(ModelBlock::kLoadValue) > 100.0f; }, 60s))
        << "the load says why";
    driven.expectSilence();
    EXPECT_EQ(driven.block.state(), ModelBlock::CannotKeepUp);
}

TEST_F(ModelBlockTest, ACycleLongerThanTheSpareTimeIsSaid)
{
    Folder folder;
    folder.hold(gainModel(0.5f, kHop));
    Driven driven("");
    ASSERT_TRUE(driven.block.setParam("folder", ParamValue::ofString(folder.path())).has_value());
    ASSERT_TRUE(driven.block.setParam("spareTime", ParamValue::ofEnum("20ms")).has_value());
    ASSERT_TRUE(driven.block.prepare(driven.spec).has_value());
    driven.waitUntilRunning();
    ASSERT_TRUE(driven.toneUntil([&driven] { return driven.block.state() == ModelBlock::CannotKeepUp; }, 20s, 2048));
    EXPECT_EQ(driven.block.detail(), static_cast<std::uint32_t>(model::Refusal::CycleTooLong));
    driven.forget();
    driven.paced(sine(9600, 997.0, kRate, 0.5f), 2048);
    driven.expectSilence(kAfterFade);
}

TEST_F(ModelBlockTest, AMissedDeadlineIsSilenceAndNotTheInput)
{
    Folder folder;
    folder.hold(gainModel(0.5f, kHop));
    Driven driven(folder.path());
    driven.waitUntilRunning();
    const int host = driven.block.hostPid();
    ASSERT_TRUE(signalHost(host, SIGSTOP));
    driven.forget();
    const Signal tone = sine(9600, 997.0, kRate, 1.0f);
    driven.paced(tone, 256);
    EXPECT_EQ(driven.block.state(), ModelBlock::CannotKeepUp);
    driven.expectSilence(kAfterFade);
    // And the fade down was the model's sound, at its half, going down.
    for (const float sample : driven.heard[0]) {
        ASSERT_LE(std::abs(sample), 0.5001f);
    }
    (void)signalHost(host, SIGCONT);
    // It comes back, by itself.
    ASSERT_TRUE(driven.toneUntil([&driven] { return driven.block.state() == ModelBlock::Running; }));
}

TEST_F(ModelBlockTest, WhatTheModelDoesNotCallSoundIsSilence)
{
    Folder folder;
    folder.hold(notSoundModel(kHop));
    Driven driven(folder.path());
    ASSERT_TRUE(driven.toneUntil([&driven] { return driven.block.state() == ModelBlock::BadOutput; }));
    driven.forget();
    driven.paced(sine(9600, 997.0, kRate, 0.5f), 256);
    driven.expectSilence();
    EXPECT_EQ(driven.block.state(), ModelBlock::BadOutput);
}

TEST_F(ModelBlockTest, AHostThatIsKilledLeavesSilenceAndNoClick)
{
    Folder folder;
    folder.hold(gainModel(1.0f, kHop));
    Driven driven(folder.path());
    driven.waitUntilRunning();
    driven.forget();
    const Signal before = sine(4864, 200.0, kRate, 0.5f);
    driven.lockStep(before, 256);
    ASSERT_TRUE(signalHost(driven.block.hostPid(), SIGKILL));
    // The tone goes on going in, and on through the rest of what was made.
    const Signal after =
        sine(14400, 200.0, kRate, 0.5f, static_cast<float>(2.0 * std::numbers::pi * 200.0 * 4864.0 / kRate));
    driven.paced(after, 256);
    const Signal &out = driven.heard[0];
    ASSERT_EQ(out.size() % 256, 0U);
    // The fade is over well inside what follows.
    EXPECT_NE(driven.block.state(), ModelBlock::Running);
    driven.expectSilence(before.size() + 8192);
    const float toneStep = static_cast<float>(0.5 * 2.0 * std::numbers::pi * 200.0 / kRate);
    const float fadeStep = 0.5f / static_cast<float>(std::lround(ModelBlock::kGuardMs * kRate / 1000.0));
    // Past where the tone before this one, which ended where it ended, comes
    // out of the block.
    const Signal down(out.begin() + driven.block.info().latencySamples + 1, out.end());
    EXPECT_LT(largestStep(down), (toneStep + fadeStep) * 1.5f) << "the way down clicked";
}

TEST_F(ModelBlockTest, AHostThatIsKilledIsStartedAgain)
{
    Folder folder;
    folder.hold(gainModel(0.5f, kHop));
    Driven driven(folder.path());
    driven.waitUntilRunning();
    const int first = driven.block.hostPid();
    ASSERT_TRUE(signalHost(first, SIGKILL));
    ASSERT_TRUE(driven.toneUntil([&driven] { return driven.block.state() == ModelBlock::Restarting; }));
    // Silent through the restart, once the last of the sound has gone.
    driven.forget();
    ASSERT_TRUE(
        driven.toneUntil([&driven, first] { return driven.block.hostPid() != first && driven.block.hostPid() > 0; }));
    driven.expectSilence(kAfterFade);
    ASSERT_TRUE(driven.toneUntil([&driven] { return driven.block.state() == ModelBlock::Running; }));
    driven.forget();
    const Signal tone = sine(9600, 997.0, kRate, 0.5f);
    driven.lockStep(tone, 256);
    const std::uint32_t latency = driven.block.info().latencySamples;
    EXPECT_NEAR(driven.heard[0][latency + 4000], tone[4000] * 0.5f, 1e-5f);
}

TEST_F(ModelBlockTest, AHostThatKeepsCrashingIsLeftAloneAndSilentUntilAskedToTryAgain)
{
    Folder folder;
    folder.hold(gainModel(0.5f, kHop));
    Driven driven(folder.path());
    for (int crash = 0; crash < 3; ++crash) {
        ASSERT_TRUE(driven.toneUntil([&driven] { return driven.block.state() == ModelBlock::Running; }));
        ASSERT_TRUE(signalHost(driven.block.hostPid(), SIGKILL));
        ASSERT_TRUE(driven.toneUntil([&driven] {
            return driven.block.state() == ModelBlock::Restarting || driven.block.state() == ModelBlock::Failed;
        }));
    }
    ASSERT_TRUE(driven.toneUntil([&driven] { return driven.block.state() == ModelBlock::Failed; }));
    EXPECT_EQ(driven.block.detail(), static_cast<std::uint32_t>(model::Refusal::KeptCrashing));
    driven.forget();
    driven.paced(sine(9600, 997.0, kRate, 0.5f), 256);
    driven.expectSilence(kAfterFade);

    const auto retry = static_cast<std::uint32_t>(ModelBlock::Retry);
    driven.block.applyParam(retry, 1.0f);
    ASSERT_TRUE(driven.toneUntil([&driven] { return driven.block.state() == ModelBlock::Running; }));
}

TEST_F(ModelBlockTest, AWildModelIsHeldToALimit)
{
    Folder folder;
    folder.hold(gainModel(1000.0f, kHop));
    Driven driven(folder.path());
    driven.waitUntilRunning();
    for (const float sample : driven.heard[0]) {
        ASSERT_LE(std::abs(sample), ModelBlock::kHighestSample);
    }
}

TEST_F(ModelBlockTest, ASessionStoppingGoesOnWithoutAnyWait)
{
    Folder folder;
    folder.hold(slowModel(kHop, 3000000));
    auto driven = std::make_unique<Driven>(folder.path());
    ASSERT_TRUE(driven->toneUntil([&driven] { return driven->block.hostPid() > 0; }));
    const auto before = std::chrono::steady_clock::now();
    driven.reset();
    EXPECT_LT(std::chrono::steady_clock::now() - before, 3s) << "a block goes whatever its model is doing";
}

// **Through the graph**, where bypass, fades and a following block are.

namespace
{

/// A constant going into a Model block going into a gain, as sessions run.
struct Chain {
    explicit Chain(const std::string &folder)
        : driven(folder)
    {
    }

    [[nodiscard]] graph::GraphSpec spec(bool enabled, std::uint32_t fade = 0)
    {
        graph::NodeSpec first;
        first.id = "src";
        first.block = &source;
        graph::NodeSpec model;
        model.id = "model";
        model.block = &driven.block;
        model.enabled = enabled;
        model.crossfadeSamples = fade;
        graph::NodeSpec last;
        last.id = "after";
        last.block = &after;
        graph::GraphSpec spec;
        spec.sampleRate = kRate;
        spec.nodes = {first, model, last};
        spec.edges = {
            {0, 0, 1, 0},
            {0, 1, 1, 1},
            {1, 0, 2, 0},
            {1, 1, 2, 1},
        };
        return spec;
    }

    [[nodiscard]] graph::CompiledGraph compile(bool enabled, std::uint32_t fade = 0)
    {
        std::expected<graph::CompiledGraph, graph::GraphError> compiled =
            graph::CompiledGraph::compile(spec(enabled, fade));
        EXPECT_TRUE(compiled.has_value());
        return std::move(*compiled);
    }

    /// Cycles of the graph, waiting for the host; what left the last block.
    [[nodiscard]] Signal run(graph::CompiledGraph &graph, std::uint32_t cycles)
    {
        constexpr std::uint32_t kFrames = 64;
        Signal heard;
        for (std::uint32_t cycle = 0; cycle < cycles; ++cycle) {
            graph.execute(kFrames);
            const std::span<const float> out = graph.outputBuffer(2, 0, kFrames);
            heard.insert(heard.end(), out.begin(), out.end());
            driven.waitForHost();
        }
        return heard;
    }

    Driven driven;
    test::ConstantSource source{1.0f, 2};
    test::GainStage after{0.5f, 2};
};

} // namespace

TEST_F(ModelBlockTest, SwitchedOffWithNothingLoadedNothingReachesTheNextBlock)
{
    Chain chain("");
    PrepareSpec prepare;
    ASSERT_TRUE(chain.source.prepare(prepare).has_value());
    ASSERT_TRUE(chain.after.prepare(prepare).has_value());
    graph::CompiledGraph graph = chain.compile(false);
    for (const float sample : chain.run(graph, 16)) {
        ASSERT_EQ(sample, 0.0f);
    }
}

TEST_F(ModelBlockTest, WithNoModelNothingReachesTheNextBlock)
{
    Chain chain("");
    PrepareSpec prepare;
    ASSERT_TRUE(chain.source.prepare(prepare).has_value());
    ASSERT_TRUE(chain.after.prepare(prepare).has_value());
    graph::CompiledGraph graph = chain.compile(true);
    for (const float sample : chain.run(graph, 16)) {
        ASSERT_EQ(sample, 0.0f);
    }
}

TEST_F(ModelBlockTest, TheSwitchRunsToSilenceAndBackToTheModel)
{
    Folder folder;
    folder.hold(gainModel(0.5f, kHop));
    Chain chain(folder.path());
    PrepareSpec prepare;
    ASSERT_TRUE(chain.source.prepare(prepare).has_value());
    ASSERT_TRUE(chain.after.prepare(prepare).has_value());
    chain.driven.waitUntilRunning();

    graph::CompiledGraph running = chain.compile(true);
    const Signal on = chain.run(running, 400);
    EXPECT_FLOAT_EQ(on.back(), 0.25f) << "the model's half, and the next block's";

    constexpr std::uint32_t kFade = 480;
    graph::CompiledGraph fadingOff = chain.compile(false, kFade);
    const Signal off = chain.run(fadingOff, 400);
    // Down from the model's sound, and at no moment louder than it.
    for (const float sample : off) {
        ASSERT_LE(std::abs(sample), 0.2501f) << "the input came through the fade";
    }
    for (std::size_t index = 8192; index < off.size(); ++index) {
        ASSERT_EQ(off[index], 0.0f) << index;
    }

    graph::CompiledGraph stillOff = chain.compile(false);
    for (const float sample : chain.run(stillOff, 64)) {
        ASSERT_EQ(sample, 0.0f);
    }

    graph::CompiledGraph fadingOn = chain.compile(true, kFade);
    const Signal back = chain.run(fadingOn, 800);
    for (const float sample : back) {
        ASSERT_LE(std::abs(sample), 0.2501f) << "the input came through the fade";
    }
    EXPECT_FLOAT_EQ(back.back(), 0.25f);
}

TEST_F(ModelBlockTest, AHostKilledUnderTheGraphLeavesTheNextBlockSilent)
{
    Folder folder;
    folder.hold(gainModel(0.5f, kHop));
    Chain chain(folder.path());
    PrepareSpec prepare;
    ASSERT_TRUE(chain.source.prepare(prepare).has_value());
    ASSERT_TRUE(chain.after.prepare(prepare).has_value());
    chain.driven.waitUntilRunning();
    graph::CompiledGraph running = chain.compile(true);
    (void)chain.run(running, 200);
    const int host = chain.driven.block.hostPid();
    ASSERT_TRUE(signalHost(host, SIGKILL));
    Signal heard;
    // Up to the moment another host is started, when sound may come back.
    while (heard.size() < 38400 && (chain.driven.block.hostPid() == host || chain.driven.block.hostPid() == -1)) {
        running.execute(64);
        const std::span<const float> out = running.outputBuffer(2, 0, 64);
        heard.insert(heard.end(), out.begin(), out.end());
        std::this_thread::sleep_for(1300us);
    }
    ASSERT_GT(heard.size(), 8192U);
    for (std::size_t index = 8192; index < heard.size(); ++index) {
        ASSERT_EQ(heard[index], 0.0f) << index;
    }
}

// **A real model, where one is at hand.** Not in the repository, as no model
// ever is: set the variable to a folder to run it.

TEST_F(ModelBlockTest, ACleanupFamilyRunsOnARealModel)
{
    const char *const folder = std::getenv("REVERBLABR_TEST_CLEANUP_MODEL");
    if (folder == nullptr) {
        GTEST_SKIP() << "no model at hand";
    }
    Driven driven(folder);
    ASSERT_EQ(driven.block.state(), ModelBlock::Loading) << driven.block.detail();
    driven.waitUntilRunning();
    driven.forget();

    // Something with the rhythm of a voice: harmonics on a pitch that
    // never stands still, so that only one lag lines the two up.
    constexpr std::size_t kFrames = 240000;
    Signal voice(kFrames);
    double phase = 0.0;
    for (std::size_t n = 0; n < kFrames; ++n) {
        const double t = static_cast<double>(n) / kRate;
        const double envelope = std::pow(std::sin(2.0 * std::numbers::pi * 3.0 * t), 2.0);
        phase += 2.0 * std::numbers::pi * (130.0 + 40.0 * std::sin(2.0 * std::numbers::pi * 1.7 * t)) / kRate;
        double sum = 0.0;
        for (int harmonic = 1; harmonic <= 20; ++harmonic) {
            sum += std::sin(phase * harmonic) / harmonic;
        }
        voice[n] = static_cast<float>(0.2 * envelope * sum);
    }
    driven.lockStep(voice, 256);

    const std::uint32_t latency = driven.block.info().latencySamples;
    const Signal &out = driven.heard[0];
    std::size_t bestLag = 0;
    double best = -1.0;
    for (std::size_t lag = 0; lag < 9600; lag += 1) {
        double sum = 0.0;
        for (std::size_t n = 48000; n < 96000; n += 2) {
            sum += static_cast<double>(out[n + lag]) * voice[n];
        }
        if (sum > best) {
            best = sum;
            bestLag = lag;
        }
    }
    std::cout << "reported latency " << latency << ", the answer lines up at " << bestLag << ", load "
              << driven.meter.value(ModelBlock::kLoadValue) << " %\n";
    EXPECT_EQ(bestLag, latency);

    driven.forget();
    const Signal noise = whiteNoise(96000, 0.1f);
    driven.lockStep(noise, 256);
    double in = 0.0;
    double through = 0.0;
    for (std::size_t n = 48000; n < 96000; ++n) {
        in += static_cast<double>(noise[n]) * noise[n];
        through += static_cast<double>(driven.heard[0][n]) * driven.heard[0][n];
    }
    const double reduced = 10.0 * std::log10(through / in);
    std::cout << "noise alone comes out " << reduced << " dB\n";
    EXPECT_LT(reduced, -10.0);
}

// **Voice conversion**: three graphs for one sound. Written by hand and tiny,
// with the inputs and outputs of the real ones, so the whole path runs here
// and every way one of the three can let the others down is tried. The rule
// is the block's and holds for them all: nothing but the voice's answer.

namespace
{

/// A folder that holds all a voice needs.
class VoiceFolder : public Folder
{
public:
    explicit VoiceFolder(const TinyVoice &voice = {},
                         TinyFault content = TinyFault::None,
                         TinyFault pitch = TinyFault::None,
                         std::int64_t likeliest = kLikeliest)
    {
        write("voice.onnx", tinyVoice(voice));
        write("encoder.onnx", tinyContentEncoder(content));
        write("estimator.onnx", tinyPitchEstimator(likeliest, pitch));
    }

    /// The pitch estimator's answer, in Hz.
    static constexpr std::int64_t kLikeliest = 150;
    static constexpr double kPitchHz = 179.3;
};

/// A sound slow enough to come through a voice whose frames are 20 ms long.
[[nodiscard]] Signal slowWave(std::size_t frames)
{
    return sine(frames, 7.0, kRate, 0.5f);
}

/// The lag at which \a out best matches \a in, near \a around.
[[nodiscard]] std::ptrdiff_t bestLagNear(const Signal &out, const Signal &in, std::size_t around, std::size_t from)
{
    std::ptrdiff_t bestLag = 0;
    double least = std::numeric_limits<double>::max();
    const auto reach = static_cast<std::ptrdiff_t>(2400);
    for (std::ptrdiff_t lag = -reach; lag <= reach; lag += 8) {
        double difference = 0.0;
        for (std::size_t n = from; n + around + 2 * 2400 < out.size() && n < in.size(); n += 16) {
            const auto at = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(n + around) + lag);
            const double gap = out[at] - in[n];
            difference += gap * gap;
        }
        if (difference < least) {
            least = difference;
            bestLag = lag;
        }
    }
    return bestLag;
}

/// The mean of \a signal over its last \a frames.
[[nodiscard]] double lastMean(const Signal &signal, std::size_t frames)
{
    double sum = 0.0;
    for (std::size_t index = signal.size() - frames; index < signal.size(); ++index) {
        sum += signal[index];
    }
    return sum / static_cast<double>(frames);
}

/// What the kernel has counted of \a pid's time, in seconds.
[[nodiscard]] double cpuSeconds(int pid)
{
    std::ifstream file(std::format("/proc/{}/stat", pid));
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::istringstream fields(text.substr(text.rfind(')') + 2));
    std::string field;
    long user = 0;
    long system = 0;
    for (int index = 3; index <= 15 && fields >> field; ++index) {
        if (index == 14) {
            user = std::stol(field);
        } else if (index == 15) {
            system = std::stol(field);
        }
    }
    return static_cast<double>(user + system) / static_cast<double>(::sysconf(_SC_CLK_TCK));
}

class VoiceLatency : public ModelBlockTest, public ::testing::WithParamInterface<std::uint32_t>
{
};

class OneOfTheThree : public ModelBlockTest, public ::testing::WithParamInterface<int>
{
protected:
    /// A folder with \a fault in the graph this run is about.
    [[nodiscard]] static std::unique_ptr<VoiceFolder> folderWith(TinyFault fault)
    {
        TinyVoice voice;
        voice.fault = GetParam() == 0 ? fault : TinyFault::None;
        return std::make_unique<VoiceFolder>(
            voice, GetParam() == 1 ? fault : TinyFault::None, GetParam() == 2 ? fault : TinyFault::None);
    }
};

} // namespace

TEST_F(ModelBlockTest, AVoiceRunsAndBothSidesPlayIt)
{
    VoiceFolder folder;
    Driven driven(folder.path());
    EXPECT_EQ(driven.block.state(), ModelBlock::Loading) << driven.block.detail();
    driven.waitUntilRunning();
    EXPECT_EQ(driven.block.detail(), static_cast<std::uint32_t>(model::Device::Cpu));
    driven.forget();
    const Signal left = slowWave(48000);
    const Signal right(48000, 0.0f);
    for (std::size_t at = 0; at + 256 <= left.size(); at += 256) {
        driven.cycle(std::span<const float>{left}.subspan(at, 256), std::span<const float>{right}.subspan(at, 256));
        driven.waitForHost();
    }
    ASSERT_EQ(driven.heard[0].size(), driven.heard[1].size());
    EXPECT_EQ(driven.heard[0], driven.heard[1]) << "one voice, on both sides";
    const float loudest = std::ranges::max(driven.heard[0], {}, [](float sample) { return std::abs(sample); });
    EXPECT_NEAR(std::abs(loudest), 0.25f, 0.03f) << "the two sides mixed";
}

TEST_F(ModelBlockTest, AVoiceRunsInASessionAtAnotherRate)
{
    VoiceFolder folder;
    Driven driven(folder.path(), 44100.0);
    EXPECT_EQ(driven.block.state(), ModelBlock::Loading) << driven.block.detail();
    driven.waitUntilRunning();
    driven.forget();
    driven.lockStep(slowWave(44100), 256);
    const float loudest = std::ranges::max(driven.heard[0], {}, [](float sample) { return std::abs(sample); });
    EXPECT_NEAR(std::abs(loudest), 0.5f, 0.05f) << "down to the voice's rate and back, unchanged";
}

TEST_F(ModelBlockTest, AVoicesDelayHoldsARunAsWellAsItsOwnPath)
{
    VoiceFolder folder;
    Driven driven(folder.path());
    const model::Description found = model::recognise(folder.path());
    const std::optional<model::Timing> timing = model::timingFor(found, 48000, kSpare);
    ASSERT_TRUE(timing.has_value());
    EXPECT_EQ(driven.block.info().latencySamples, timing->latency);
    EXPECT_GT(timing->latency, timing->hopFrames + timing->pathDelay + kSpare) << "a run has time of its own";
}

TEST_P(VoiceLatency, TheVoiceIsAsLateAsReported)
{
    VoiceFolder folder;
    Driven driven(folder.path());
    driven.waitUntilRunning();
    const std::uint32_t latency = driven.block.info().latencySamples;
    driven.lockStep(silence(latency + 8192), GetParam());
    driven.forget();
    const Signal wave = slowWave(96000);
    driven.lockStep(wave, GetParam());
    // The two sides are the same here, so the mix is the wave. The voice
    // holds a value for 20 ms, which is half that late against a smooth wave.
    const std::ptrdiff_t lag = bestLagNear(driven.heard[0], wave, latency, 4800);
    EXPECT_LE(std::abs(lag), 480) << "off by " << lag;
}

INSTANTIATE_TEST_SUITE_P(Quanta, VoiceLatency, ::testing::Values(64U, 256U, 2048U));

TEST_F(ModelBlockTest, HowTheSoundIsCutChangesNothingInAVoice)
{
    VoiceFolder folder;
    InvarianceOptions options;
    options.inputChannels = 2;
    options.outputChannels = 2;
    options.maxBlockFrames = 2048;
    const InvarianceReport report = checkBlockSizeInvariance(
        [&folder] { return std::make_unique<LockStepBlock>(folder.path()); }, spread(slowWave(96000), 2), options);
    EXPECT_TRUE(report.passed) << report.summary;
}

TEST_F(ModelBlockTest, APitchShiftMovesTheVoiceWhileItRuns)
{
    TinyVoice voice;
    voice.sayThePitch = true;
    VoiceFolder folder(voice);
    Driven driven(folder.path());
    driven.waitUntilRunning();
    const int host = driven.block.hostPid();
    const std::uint32_t shape = driven.block.preparedShape();
    const Signal tone = slowWave(48000);

    driven.lockStep(tone, 256);
    EXPECT_NEAR(lastMean(driven.heard[0], 1024), VoiceFolder::kPitchHz / 1000.0, 0.01);

    ASSERT_TRUE(driven.block.setParam("semitones", ParamValue::ofInt(12)).has_value());
    driven.block.applyParam(ModelBlock::Semitones, 12.0f);
    driven.forget();
    driven.lockStep(tone, 256);
    EXPECT_NEAR(lastMean(driven.heard[0], 1024), 2.0 * VoiceFolder::kPitchHz / 1000.0, 0.02);

    driven.block.applyParam(ModelBlock::Semitones, -12.0f);
    driven.forget();
    driven.lockStep(tone, 256);
    EXPECT_NEAR(lastMean(driven.heard[0], 1024), 0.5 * VoiceFolder::kPitchHz / 1000.0, 0.01);

    EXPECT_EQ(driven.block.hostPid(), host) << "no new process for a new pitch";
    EXPECT_EQ(driven.block.preparedShape(), shape) << "and no new graph";
}

TEST_F(ModelBlockTest, TheOtherExportRunsAsWell)
{
    TinyVoice voice;
    voice.otherNames = true;
    Folder folder;
    folder.write("voice.onnx", tinyVoice(voice));
    folder.write("encoder.onnx", tinyMaskedContentEncoder());
    folder.write("estimator.onnx", tinyPitchEstimator(VoiceFolder::kLikeliest));
    Driven driven(folder.path());
    driven.waitUntilRunning();
    driven.forget();
    driven.lockStep(slowWave(48000), 256);
    EXPECT_GT(std::ranges::max(driven.heard[0]), 0.1f);
}

TEST_F(ModelBlockTest, AVoiceInAFolderChosenUnderTheHomeRuns)
{
    // A folder chooser gives a path, and the session keeps it as ~/...: the
    // words the block, the panel and the host each have to turn back.
    VoiceFolder folder;
    const fs::path path{folder.path()};
    const char *const before = std::getenv("HOME");
    const std::string kept = before == nullptr ? std::string{} : before;
    ::setenv("HOME", path.parent_path().c_str(), 1);
    {
        Driven driven("~/" + path.filename().string());
        EXPECT_EQ(driven.block.state(), ModelBlock::Loading) << driven.block.detail();
        driven.waitUntilRunning();
        EXPECT_GT(driven.block.info().latencySamples, 0U);
        driven.forget();
        driven.lockStep(slowWave(48000), 256);
        EXPECT_GT(std::ranges::max(driven.heard[0]), 0.1f);
    }
    ::setenv("HOME", kept.c_str(), 1);
}

TEST_F(ModelBlockTest, ASessionFromBeforeVoicesLoadsAsItWas)
{
    Folder folder;
    folder.hold(gainModel(0.5f, kHop));
    ModelBlock block;
    ParamValueMap saved;
    saved.emplace("folder", ParamValue::ofString(folder.path()));
    saved.emplace("spareTime", ParamValue::ofEnum("100ms"));
    saved.emplace("monoMode", ParamValue::ofEnum("downmix"));
    ASSERT_TRUE(block.loadState(saved).has_value());
    EXPECT_EQ(block.param("semitones").asInt(), std::int64_t{0});
    EXPECT_EQ(block.param("spareTime").text(), "100ms");
    EXPECT_EQ(block.param("monoMode").text(), "downmix");
    EXPECT_EQ(block.param("folder").text(), folder.path());
}

TEST_F(ModelBlockTest, TheSemitonesAreSavedAndHeldToTheirRange)
{
    VoiceFolder folder;
    ModelBlock block;
    ASSERT_TRUE(block.setParam("folder", ParamValue::ofString(folder.path())).has_value());
    ASSERT_TRUE(block.setParam("semitones", ParamValue::ofInt(-7)).has_value());
    ModelBlock again;
    ASSERT_TRUE(again.loadState(block.saveState()).has_value());
    EXPECT_EQ(again.param("semitones").asInt(), std::int64_t{-7});
    EXPECT_FALSE(block.setParam("semitones", ParamValue::ofInt(ModelBlock::kMostSemitones + 1)).has_value());
}

TEST_F(ModelBlockTest, AVoiceWithAFileMissingLeavesNothingAndSaysWhich)
{
    struct Case {
        std::string gone;
        model::Refusal said;
    };
    for (const Case &missing : {Case{"encoder.onnx", model::Refusal::NoContentModel},
                                Case{"estimator.onnx", model::Refusal::NoPitchModel},
                                Case{"voice.onnx", model::Refusal::NoVoiceModel}}) {
        VoiceFolder folder;
        fs::remove(fs::path(folder.path()) / missing.gone);
        Driven driven(folder.path());
        EXPECT_EQ(driven.block.state(), ModelBlock::Failed) << missing.gone;
        EXPECT_EQ(driven.block.detail(), static_cast<std::uint32_t>(missing.said)) << missing.gone;
        EXPECT_EQ(driven.block.info().latencySamples, 0U);
        const Signal tone = sine(4800, 997.0, kRate, 0.5f);
        driven.paced(tone, 256);
        driven.expectSilence();
    }
}

TEST_F(ModelBlockTest, AFileThatGoesAfterTheFolderWasReadLeavesNothing)
{
    VoiceFolder folder;
    Driven driven("");
    ASSERT_TRUE(driven.block.setParam("folder", ParamValue::ofString(folder.path())).has_value());
    fs::remove(fs::path(folder.path()) / "estimator.onnx");
    ASSERT_TRUE(driven.block.prepare(driven.spec).has_value());
    ASSERT_TRUE(driven.toneUntil([&driven] { return driven.block.state() == ModelBlock::Failed; }));
    EXPECT_EQ(driven.block.detail(), static_cast<std::uint32_t>(model::Refusal::NoPitchModel));
    driven.expectSilence();
}

TEST_P(OneOfTheThree, WhatItGivesThatIsNotANumberIsSilence)
{
    const std::unique_ptr<VoiceFolder> folder = folderWith(TinyFault::NotSound);
    Driven driven(folder->path());
    ASSERT_TRUE(driven.toneUntil([&driven] { return driven.block.state() == ModelBlock::BadOutput; }))
        << "state " << static_cast<int>(driven.block.state()) << " detail " << driven.block.detail();
    driven.forget();
    driven.paced(sine(9600, 997.0, kRate, 0.5f), 256);
    driven.expectSilence();
    EXPECT_EQ(driven.block.state(), ModelBlock::BadOutput);
}

TEST_P(OneOfTheThree, GivingTooLittleIsARefusalAndSilence)
{
    const std::unique_ptr<VoiceFolder> folder = folderWith(TinyFault::TooLittle);
    Driven driven(folder->path());
    ASSERT_TRUE(driven.toneUntil([&driven] { return driven.block.state() == ModelBlock::Failed; }))
        << "state " << static_cast<int>(driven.block.state());
    EXPECT_EQ(driven.block.detail(), static_cast<std::uint32_t>(model::Refusal::WrongShape));
    driven.expectSilence();
}

TEST_P(OneOfTheThree, TakingTooLongIsSilenceAndSaysSo)
{
    const std::unique_ptr<VoiceFolder> folder = folderWith(TinyFault::Slow);
    Driven driven(folder->path());
    ASSERT_TRUE(driven.toneUntil([&driven] { return driven.block.state() == ModelBlock::CannotKeepUp; }, 60s))
        << "state " << static_cast<int>(driven.block.state());
    driven.forget();
    ASSERT_TRUE(driven.toneUntil([&driven] { return driven.meter.value(ModelBlock::kLoadValue) > 100.0f; }, 60s))
        << "the load says why";
    driven.expectSilence();
    EXPECT_EQ(driven.block.state(), ModelBlock::CannotKeepUp);
}

INSTANTIATE_TEST_SUITE_P(TheVoiceTheEncoderAndTheEstimator, OneOfTheThree, ::testing::Values(0, 1, 2));

TEST_F(ModelBlockTest, AVoiceHostThatIsKilledLeavesSilenceAndComesBack)
{
    VoiceFolder folder;
    Driven driven(folder.path());
    driven.waitUntilRunning();
    driven.lockStep(slowWave(24000), 256);
    const int host = driven.block.hostPid();
    ASSERT_GT(host, 0);
    ASSERT_TRUE(signalHost(host, SIGKILL));
    driven.forget();
    const Signal tone = sine(19200, 997.0, kRate, 1.0f);
    driven.paced(tone, 256);
    driven.expectSilence(kAfterFade);
    ASSERT_TRUE(driven.toneUntil([&driven] { return driven.block.state() == ModelBlock::Running; }));
    EXPECT_NE(driven.block.hostPid(), host);
}

TEST_F(ModelBlockTest, AVoiceRunsOnARealModel)
{
    const char *const folder = std::getenv("REVERBLABR_TEST_VOICE_MODEL");
    if (folder == nullptr) {
        GTEST_SKIP() << "no model at hand";
    }
    Driven driven(folder);
    ASSERT_EQ(driven.block.state(), ModelBlock::Loading) << driven.block.detail();
    driven.waitUntilRunning();
    const int host = driven.block.hostPid();
    driven.forget();

    // Harmonics on a pitch that never stands still, in syllables.
    constexpr std::size_t kFrames = 480000;
    Signal voice(kFrames);
    double phase = 0.0;
    for (std::size_t n = 0; n < kFrames; ++n) {
        const double t = static_cast<double>(n) / kRate;
        const double envelope = std::pow(std::sin(2.0 * std::numbers::pi * 1.5 * t), 2.0);
        phase += 2.0 * std::numbers::pi * (160.0 + 40.0 * std::sin(2.0 * std::numbers::pi * 1.7 * t)) / kRate;
        double sum = 0.0;
        for (int harmonic = 1; harmonic <= 20; ++harmonic) {
            sum += std::sin(phase * harmonic) / harmonic;
        }
        voice[n] = static_cast<float>(0.15 * envelope * sum);
    }

    const double before = cpuSeconds(host);
    driven.paced(voice, 256);
    const double used = cpuSeconds(host) - before;
    const double seconds = static_cast<double>(kFrames) / kRate;
    const std::uint32_t latency = driven.block.info().latencySamples;
    EXPECT_EQ(driven.block.state(), ModelBlock::Running);

    // How late the syllables are: the voice's waves are its own, and only
    // the loudness can be lined up with what went in.
    const auto envelopeOf = [](const Signal &signal) {
        Signal envelope;
        for (std::size_t at = 0; at + 480 <= signal.size(); at += 480) {
            double sum = 0.0;
            for (std::size_t n = at; n < at + 480; ++n) {
                sum += static_cast<double>(signal[n]) * signal[n];
            }
            envelope.push_back(static_cast<float>(std::sqrt(sum / 480.0)));
        }
        return envelope;
    };
    const Signal in = envelopeOf(voice);
    const Signal out = envelopeOf(driven.heard[0]);
    int bestLag = 0;
    double best = -1.0;
    for (int lag = 0; lag < 80; ++lag) {
        double sum = 0.0;
        for (std::size_t n = 100; n + 200 < in.size(); ++n) {
            sum += static_cast<double>(out[n + static_cast<std::size_t>(lag)]) * in[n];
        }
        if (sum > best) {
            best = sum;
            bestLag = lag;
        }
    }
    const double lateMs = bestLag * 10.0;
    const double reportedMs = latency * 1000.0 / kRate;
    std::cout << "reported delay " << reportedMs << " ms, the syllables line up at " << lateMs << " ms; time used "
              << driven.meter.value(ModelBlock::kLoadValue) << " %, " << used / seconds << " of a core\n";
    EXPECT_NEAR(lateMs, reportedMs, 30.0);
}
