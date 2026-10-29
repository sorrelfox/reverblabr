// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What an engine and a plugin host share (ADR-0014), with no host process
// and no PipeWire: the memory, the messages, one cycle of the host, and the
// built-in plugin the rest is measured with.

#include "allocationtrap.h"
#include "plugin/hostprocessor.h"
#include "plugin/messages.h"
#include "plugin/shared.h"
#include "plugin/testplugins.h"

#include <gtest/gtest.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

using namespace rb;
using namespace rb::plugin;
using rb::test::NoAllocations;

namespace
{

/// The engine's mapping and a second one through the same descriptor, the
/// way the host maps it: two addresses, one memory.
struct TwoMappings {
    SharedMemory engine;
    SharedMemory host;
};

TwoMappings mapTwice()
{
    std::expected<SharedMemory, SharedError> engine = SharedMemory::create();
    EXPECT_TRUE(engine.has_value());
    std::expected<SharedMemory, SharedError> host = SharedMemory::map(::dup(engine->fd()));
    EXPECT_TRUE(host.has_value());
    return TwoMappings{std::move(*engine), std::move(*host)};
}

/// One stereo cycle's buffers.
struct Buffers {
    explicit Buffers(std::uint32_t frames, float level = 0.0f)
        : inL(frames, level)
        , inR(frames, level)
        , outL(frames, -1.0f)
        , outR(frames, -1.0f)
    {
    }

    std::vector<float> inL, inR, outL, outR;
    std::array<float *, 2> in{inL.data(), inR.data()};
    std::array<float *, 2> out{outL.data(), outR.data()};
};

std::unique_ptr<dsp::Block> preparedGain(std::uint32_t latency = 0, double rate = 48000.0)
{
    std::unique_ptr<dsp::Block> block = makeBuiltinPlugin(TestGain::kTypeId, latency);
    dsp::PrepareSpec spec;
    spec.sampleRate = rate;
    EXPECT_TRUE(block->prepare(spec).has_value());
    return block;
}

} // namespace

TEST(PluginShared, TheHostsMappingIsTheEnginesMemory)
{
    TwoMappings shared = mapTwice();
    ASSERT_NE(&shared.engine.block(), &shared.host.block());

    ASSERT_TRUE(pushParam(shared.engine.block(), ParamChange{3, 0.25f}));
    shared.host.block().cycles.store(41);

    ParamChange change;
    ASSERT_TRUE(popParam(shared.host.block(), change));
    EXPECT_EQ(change.index, 3U);
    EXPECT_FLOAT_EQ(change.value, 0.25f);
    EXPECT_FALSE(popParam(shared.host.block(), change));
    EXPECT_EQ(shared.engine.block().cycles.load(), 41U);
}

TEST(PluginShared, AFullRingRefusesAndCountsWhatItRefused)
{
    TwoMappings shared = mapTwice();
    for (std::uint32_t index = 0; index < kParamSlots; ++index) {
        ASSERT_TRUE(pushParam(shared.engine.block(), ParamChange{index, 1.0f}));
    }
    EXPECT_FALSE(pushParam(shared.engine.block(), ParamChange{0, 2.0f}));
    EXPECT_EQ(shared.engine.block().paramsDropped.load(), 1U);

    // In order, and all of them, after which there is room again.
    ParamChange change;
    for (std::uint32_t index = 0; index < kParamSlots; ++index) {
        ASSERT_TRUE(popParam(shared.host.block(), change));
        ASSERT_EQ(change.index, index);
    }
    EXPECT_TRUE(pushParam(shared.engine.block(), ParamChange{7, 2.0f}));
}

TEST(PluginShared, MemoryThatIsNotABlockOfThisVersionIsRefused)
{
    std::expected<SharedMemory, SharedError> engine = SharedMemory::create();
    ASSERT_TRUE(engine.has_value());
    engine->block().version = kProtocolVersion + 1;
    const auto newer = SharedMemory::map(::dup(engine->fd()));
    ASSERT_FALSE(newer.has_value());
    EXPECT_EQ(newer.error(), SharedError::WrongLayout);

    const int small = ::memfd_create("rb-test-small", MFD_CLOEXEC);
    ASSERT_GE(small, 0);
    ASSERT_EQ(::ftruncate(small, 64), 0);
    const auto tooSmall = SharedMemory::map(small);
    ASSERT_FALSE(tooSmall.has_value());
    EXPECT_EQ(tooSmall.error(), SharedError::NotMapped);
}

TEST(PluginMessages, AMessageArrivesWholeAndAlone)
{
    int pair[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair), 0);
    // As the engine makes them: nobody reads until all three are sent.
    widenSocket(pair[0]);
    widenSocket(pair[1]);

    const std::string big(200000, 'x');
    ASSERT_TRUE(sendMessage(pair[0], Message{MessageType::Ready, "reverblabr.plugin.one\n128"}));
    ASSERT_TRUE(sendMessage(pair[0], Message{MessageType::State, big}));
    ASSERT_TRUE(sendMessage(pair[0], Message{MessageType::Quit, {}}));

    const auto ready = receiveMessage(pair[1], true);
    ASSERT_TRUE(ready.has_value());
    EXPECT_EQ(ready->type, MessageType::Ready);
    EXPECT_EQ(ready->payload, "reverblabr.plugin.one\n128");
    const auto state = receiveMessage(pair[1], true);
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->payload.size(), big.size());
    const auto quit = receiveMessage(pair[1], true);
    ASSERT_TRUE(quit.has_value());
    EXPECT_EQ(quit->type, MessageType::Quit);
    EXPECT_TRUE(quit->payload.empty());

    // Nothing waiting is not the same as nobody there.
    EXPECT_FALSE(receiveMessage(pair[1], false).has_value());
    EXPECT_TRUE(wouldBlock());
    ::close(pair[0]);
    EXPECT_FALSE(receiveMessage(pair[1], false).has_value());
    EXPECT_FALSE(wouldBlock());
    // And a host that has gone is an answer, not a SIGPIPE.
    EXPECT_FALSE(sendMessage(pair[1], Message{MessageType::Quit, {}}));
    ::close(pair[1]);
}

TEST(PluginMessages, AMessageMuchLargerThanASocketTakesArrivesWhole)
{
    // **Far past what a SOCK_SEQPACKET socket takes as one packet** (about
    // 208 KiB here), as a list of thousands of presets or a large state is:
    // sent while the other end reads, so the sender waits on a full socket
    // as well, and put together whole at the other end.
    int pair[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair), 0);
    std::string big(900 * 1024, '\0');
    for (std::size_t at = 0; at < big.size(); ++at) {
        big[at] = static_cast<char>('a' + at % 26);
    }
    bool sent = false;
    std::thread sender([&] {
        sent = sendMessage(pair[0], Message{MessageType::Presets, big});
        sent = sent && sendMessage(pair[0], Message{MessageType::Quit, {}});
    });
    const auto first = receiveMessage(pair[1], true);
    const auto second = receiveMessage(pair[1], true);
    sender.join();
    EXPECT_TRUE(sent);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->type, MessageType::Presets);
    EXPECT_EQ(first->payload.size(), big.size());
    EXPECT_TRUE(first->payload == big) << "not the message that was sent";
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->type, MessageType::Quit);

    // And one exactly at the limit, between widened sockets.
    widenSocket(pair[0]);
    widenSocket(pair[1]);
    std::thread atLimit(
        [&] { sent = sendMessage(pair[0], Message{MessageType::State, std::string(kMaxMessageBytes, 'x')}); });
    const auto limit = receiveMessage(pair[1], true);
    atLimit.join();
    EXPECT_TRUE(sent);
    ASSERT_TRUE(limit.has_value());
    EXPECT_EQ(limit->payload.size(), kMaxMessageBytes);
    ::close(pair[0]);
    ::close(pair[1]);
}

TEST(PluginMessages, APresetListKeepsItsFramingWhateverTheNames)
{
    const std::vector<PresetEntry> listed{{"a\tb", "Tabbed id"}, {"ok", "Two\nlines\tand a tab"}};
    EXPECT_EQ(parsePresets(presetsText(listed)), (std::vector<PresetEntry>{{"ok", "Two lines and a tab"}}));
}

TEST(PluginMessages, AMessageLargerThanTheLimitIsNotSent)
{
    int pair[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair), 0);
    EXPECT_FALSE(sendMessage(pair[0], Message{MessageType::SetState, std::string(kMaxMessageBytes + 1, 'x')}));
    ::close(pair[0]);
    ::close(pair[1]);
}

TEST(PluginMessages, WhatAPluginsWindowSaysComesBackAsItWasSaid)
{
    // **A value read back is the float written**, however many digits it
    // takes: a knob in the plugin's window and the panel's must agree to
    // the last bit, or each would tell the other it moved.
    for (const float value : {0.0f, 1.0f, -6.02f, 0.833007812f, 1.0e-7f, 123456.789f}) {
        const std::optional<float> back = parseValue(valueText(value));
        ASSERT_TRUE(back.has_value()) << valueText(value);
        EXPECT_EQ(*back, value);
    }
    EXPECT_FALSE(parseValue("loud").has_value());
    EXPECT_FALSE(parseValue("1.5x").has_value());

    const auto pair = parsePair(pairText("g_out", "begin"));
    ASSERT_TRUE(pair.has_value());
    EXPECT_EQ(pair->first, "g_out");
    EXPECT_EQ(pair->second, "begin");
    EXPECT_FALSE(parsePair("no line").has_value());

    const std::vector<NamedValue> values{{"g_out", 0.5f}, {"12", -3.25f}};
    const auto parsed = parseValues(valuesText("edit", values));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->first, "edit");
    EXPECT_EQ(parsed->second, values);
    EXPECT_FALSE(parseValues("follow\ng_out=loud\n").has_value());

    const std::vector<PresetEntry> presets{{"file:///home/x/.lv2/a.preset.lv2/a.ttl", "Gentle"}, {"7", "Hard knee"}};
    EXPECT_EQ(parsePresets(presetsText(presets)), presets);
    EXPECT_TRUE(parsePresets("").empty());
}

TEST(PluginHostProcessor, EveryCycleBeatsTheHeartEvenWithNoPlugin)
{
    TwoMappings shared = mapTwice();
    HostProcessor processor(shared.host.block());
    Buffers buffers(64, 0.5f);

    processor.process(buffers.in, buffers.out, 64, 48000);
    processor.process(buffers.in, buffers.out, 64, 48000);

    EXPECT_EQ(shared.engine.block().cycles.load(), 2U);
    EXPECT_EQ(buffers.outL[0], 0.0f) << "no plugin is silence";
}

TEST(PluginHostProcessor, AParameterIsHeardFromTheFirstSampleOfTheNextCycle)
{
    TwoMappings shared = mapTwice();
    HostProcessor processor(shared.host.block());
    std::unique_ptr<dsp::Block> gain = preparedGain();
    EXPECT_EQ(processor.exchange(gain.get(), 48000, gain->params().size()), nullptr);

    Buffers before(128, 0.5f);
    processor.process(before.in, before.out, 128, 48000);
    EXPECT_FLOAT_EQ(before.outL[127], 0.5f);

    ASSERT_TRUE(pushParam(shared.engine.block(), ParamChange{TestGain::Gain, 2.0f}));
    // One the plugin does not have: dropped, not applied somewhere else.
    ASSERT_TRUE(pushParam(shared.engine.block(), ParamChange{99, 3.0f}));
    Buffers after(128, 0.5f);
    processor.process(after.in, after.out, 128, 48000);
    EXPECT_FLOAT_EQ(after.outL[0], 1.0f);
    EXPECT_FLOAT_EQ(after.outR[127], 1.0f);
    EXPECT_EQ(shared.engine.block().paramRead.load(), 2U);
}

TEST(PluginHostProcessor, AChangeFromThePluginsWindowIsHeardAfterTheEnginesInTheSameCycle)
{
    TwoMappings shared = mapTwice();
    HostProcessor processor(shared.host.block());
    std::unique_ptr<dsp::Block> gain = preparedGain();
    (void)processor.exchange(gain.get(), 48000, gain->params().size());

    // Both arrived before the cycle: the window's is the later word.
    ASSERT_TRUE(pushParam(shared.engine.block(), ParamChange{TestGain::Gain, 3.0f}));
    ASSERT_TRUE(processor.pushFromHost(ParamChange{TestGain::Gain, 2.0f}));
    EXPECT_FALSE(processor.fromHostDrained());
    Buffers buffers(64, 0.5f);
    processor.process(buffers.in, buffers.out, 64, 48000);
    EXPECT_FLOAT_EQ(buffers.outL[0], 1.0f);
    EXPECT_TRUE(processor.fromHostDrained());
}

TEST(PluginHostProcessor, AHeldPluginIsNotRunAndItsCyclesAreSilent)
{
    TwoMappings shared = mapTwice();
    HostProcessor processor(shared.host.block());
    std::unique_ptr<dsp::Block> gain = preparedGain();
    (void)processor.exchange(gain.get(), 48000, gain->params().size());

    processor.hold();
    // A change waits for the plugin, rather than reaching it while held.
    ASSERT_TRUE(pushParam(shared.engine.block(), ParamChange{TestGain::Gain, 2.0f}));
    Buffers held(64, 0.5f);
    processor.process(held.in, held.out, 64, 48000);
    EXPECT_EQ(held.outL[0], 0.0f);
    EXPECT_EQ(shared.engine.block().paramRead.load(), 0U);
    EXPECT_EQ(shared.engine.block().cycles.load(), 1U) << "a held host is still alive";

    processor.release();
    Buffers after(64, 0.5f);
    processor.process(after.in, after.out, 64, 48000);
    EXPECT_FLOAT_EQ(after.outL[0], 1.0f);
}

TEST(PluginHostProcessor, AHoldWaitsForTheCycleInsideThePlugin)
{
    // A cycle is inside the plugin when the hold is asked for: hold() comes
    // back only once that cycle has left it.
    TwoMappings shared = mapTwice();
    HostProcessor processor(shared.host.block());
    struct Slow final : public dsp::Block {
        dsp::BlockInfo m_info;
        std::atomic<bool> inside{false};
        std::atomic<bool> letGo{false};
        [[nodiscard]] const dsp::BlockInfo &info() const noexcept override
        {
            return m_info;
        }
        [[nodiscard]] std::span<const dsp::ParamDescriptor> params() const noexcept override
        {
            return {};
        }
        [[nodiscard]] std::expected<void, dsp::BlockError> prepare(const dsp::PrepareSpec &) override
        {
            return {};
        }
        void process(dsp::ProcessContext &) noexcept RB_NONBLOCKING override
        {
            inside.store(true);
            while (!letGo.load()) { }
        }
        void reset() noexcept RB_NONBLOCKING override { }
        void applyParam(std::uint32_t, float) noexcept RB_NONBLOCKING override { }
        [[nodiscard]] std::expected<void, dsp::BlockError> setParam(std::string_view, const dsp::ParamValue &) override
        {
            return {};
        }
        [[nodiscard]] dsp::ParamValue param(std::string_view) const override
        {
            return {};
        }
        [[nodiscard]] dsp::ParamValueMap saveState() const override
        {
            return {};
        }
        [[nodiscard]] std::expected<void, dsp::BlockError> loadState(const dsp::ParamValueMap &) override
        {
            return {};
        }
    } slow;
    (void)processor.exchange(&slow, 48000, 0);
    std::thread audio([&processor] {
        Buffers buffers(64, 0.5f);
        processor.process(buffers.in, buffers.out, 64, 48000);
    });
    while (!slow.inside.load()) {
        std::this_thread::yield();
    }
    std::atomic<bool> held{false};
    std::thread holder([&processor, &held] {
        processor.hold();
        held.store(true);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds{30});
    EXPECT_FALSE(held.load()) << "the hold came back while a cycle was still in the plugin";
    slow.letGo.store(true);
    holder.join();
    audio.join();
    EXPECT_TRUE(held.load());
    processor.release();
}

TEST(PluginHostProcessor, ACycleAtAnotherRateIsSilentAndAsksForThatRate)
{
    TwoMappings shared = mapTwice();
    HostProcessor processor(shared.host.block());
    std::unique_ptr<dsp::Block> gain = preparedGain();
    (void)processor.exchange(gain.get(), 48000, gain->params().size());
    EXPECT_EQ(shared.engine.block().preparedRate.load(), 48000U);

    Buffers buffers(64, 0.5f);
    processor.process(buffers.in, buffers.out, 64, 44100);
    EXPECT_EQ(buffers.outL[0], 0.0f);
    EXPECT_EQ(shared.engine.block().rateWanted.load(), 44100U);

    std::unique_ptr<dsp::Block> again = preparedGain(0, 44100.0);
    EXPECT_EQ(processor.exchange(again.get(), 44100, again->params().size()), gain.get());
    EXPECT_EQ(shared.engine.block().rateWanted.load(), 0U);
    processor.process(buffers.in, buffers.out, 64, 44100);
    EXPECT_FLOAT_EQ(buffers.outL[0], 0.5f);
}

TEST(PluginHostProcessor, ACycleTouchesTheAllocatorNotAtAll)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    TwoMappings shared = mapTwice();
    HostProcessor processor(shared.host.block());
    std::unique_ptr<dsp::Block> gain = preparedGain(300);
    (void)processor.exchange(gain.get(), 48000, gain->params().size());
    Buffers buffers(256, 0.25f);

    std::size_t allocations = 1;
    {
        const NoAllocations guard(NoAllocations::OnAllocation::Count);
        for (int cycle = 0; cycle < 100; ++cycle) {
            (void)pushParam(shared.engine.block(), ParamChange{TestGain::Gain, 0.01f * static_cast<float>(cycle)});
            // And one from the plugin's window, through the host's own queue.
            (void)processor.pushFromHost(ParamChange{TestGain::Gain, 0.02f * static_cast<float>(cycle)});
            processor.process(buffers.in, buffers.out, 256, 48000);
            processor.process(buffers.in, buffers.out, 256, 44100);
        }
        allocations = guard.count();
    }
    EXPECT_EQ(allocations, 0U);
}

TEST(PluginTestGain, ItDelaysByTheLatencyItReports)
{
    constexpr std::uint32_t kLatency = 100;
    std::unique_ptr<dsp::Block> gain = preparedGain(kLatency);
    EXPECT_EQ(gain->info().latencySamples, kLatency);
    EXPECT_EQ(gain->info().typeId, TestGain::kTypeId);

    TwoMappings shared = mapTwice();
    HostProcessor processor(shared.host.block());
    (void)processor.exchange(gain.get(), 48000, gain->params().size());
    Buffers buffers(256);
    buffers.inL[0] = 1.0f; // an impulse
    processor.process(buffers.in, buffers.out, 256, 48000);
    for (std::uint32_t frame = 0; frame < 256; ++frame) {
        ASSERT_FLOAT_EQ(buffers.outL[frame], frame == kLatency ? 1.0f : 0.0f) << "at " << frame;
    }
}

TEST(PluginTestGain, OnlyWhatIsBuiltInIsMade)
{
    EXPECT_NE(makeBuiltinPlugin("test.gain", 0), nullptr);
    EXPECT_EQ(makeBuiltinPlugin("test.nothing", 0), nullptr);
}
