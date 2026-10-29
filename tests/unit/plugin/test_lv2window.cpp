// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// An LV2 plugin's own window and its presets, as the host runs them, with
// no host process and no server: the tests' plugin (tests/lv2), whose
// window shows itself and needs no display, its atom ports both ways, the
// audio thread allocation-free with the window open, and presets saved in a
// home of the test's own and put back.

#include "allocationtrap.h"
#include "atomring.h"
#include "dsp/audioblock.h"
#include "editor.h"
#include "lv2plugin.h"

#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

using namespace rb;
using namespace rb::pluginhost;
using rb::test::NoAllocations;
namespace fs = std::filesystem;

namespace
{

constexpr const char *kTestLv2 = "urn:reverblabr:test:gain";

/// An environment variable, for as long as a test runs.
class ScopedEnv
{
public:
    ScopedEnv(const char *name, const std::string &value)
        : m_name(name)
    {
        if (const char *before = std::getenv(name); before != nullptr) {
            m_before = before;
        }
        ::setenv(name, value.c_str(), 1);
    }
    ~ScopedEnv()
    {
        if (m_before) {
            ::setenv(m_name.c_str(), m_before->c_str(), 1);
        } else {
            ::unsetenv(m_name.c_str());
        }
    }
    ScopedEnv(const ScopedEnv &) = delete;
    ScopedEnv &operator=(const ScopedEnv &) = delete;

private:
    std::string m_name;
    std::optional<std::string> m_before;
};

/// A main loop that runs only when a test says: its timers are called by
/// tick(), as often as the test likes.
class HandLoop final : public MainLoop
{
public:
    std::optional<Id> addTimer(std::chrono::milliseconds, std::function<void()> tick) override
    {
        m_timers.emplace(++m_next, std::move(tick));
        return m_next;
    }
    std::optional<Id> addFd(int, std::uint32_t, std::function<void(std::uint32_t)>) override
    {
        return std::nullopt;
    }
    bool changeFd(Id, std::uint32_t) override
    {
        return false;
    }
    void remove(Id id) override
    {
        m_timers.erase(id);
    }
    void tick()
    {
        // A copy: a timer may go from inside its own call.
        const std::map<Id, std::function<void()>> timers = m_timers;
        for (const auto &[id, timer] : timers) {
            if (m_timers.contains(id)) {
                timer();
            }
        }
    }
    [[nodiscard]] std::size_t timers() const
    {
        return m_timers.size();
    }

private:
    std::map<Id, std::function<void()>> m_timers;
    Id m_next = 0;
};

/// A home of the test's own, with LV2_PATH at the tests' plugin, that
/// home's ~/.lv2 and the system's (LV2's own descriptions are there), and
/// the window's notes in it.
class Home
{
public:
    Home()
        : m_dir(fs::temp_directory_path() / ("rb-lv2window-" + std::to_string(::getpid())))
        , m_home("HOME", m_dir.string())
        , m_path("LV2_PATH",
                 std::string{RB_TEST_LV2_DIR} + ":" + (m_dir / ".lv2").string() + ":/usr/lib64/lv2:/usr/lib/lv2")
        , m_log("RB_TEST_LV2_UI_LOG", (m_dir / "window.log").string())
    {
        fs::create_directories(m_dir);
    }
    ~Home()
    {
        std::error_code ignored;
        fs::remove_all(m_dir, ignored);
    }
    Home(const Home &) = delete;
    Home &operator=(const Home &) = delete;

    [[nodiscard]] std::vector<std::string> told() const
    {
        std::vector<std::string> lines;
        std::ifstream in(m_dir / "window.log");
        for (std::string line; std::getline(in, line);) {
            lines.push_back(line);
        }
        return lines;
    }
    [[nodiscard]] bool wasTold(const std::string &line) const
    {
        const std::vector<std::string> lines = told();
        return std::ranges::find(lines, line) != lines.end();
    }
    [[nodiscard]] fs::path path() const
    {
        return m_dir;
    }

private:
    fs::path m_dir;
    ScopedEnv m_home;
    ScopedEnv m_path;
    ScopedEnv m_log;
};

/// One stereo cycle of the plugin, as the host's audio thread runs it.
struct Cycle {
    explicit Cycle(std::uint32_t frames, float level)
        : inL(frames, level)
        , inR(frames, level)
        , outL(frames, 0.0f)
        , outR(frames, 0.0f)
        , frames(frames)
    {
    }

    void run(dsp::Block &block)
    {
        dsp::ProcessContext context{
            dsp::AudioBlock{in.data(), 2, frames}, dsp::AudioBlock{out.data(), 2, frames}, frames};
        block.process(context);
    }

    std::vector<float> inL, inR, outL, outR;
    std::array<float *, 2> in{inL.data(), inR.data()};
    std::array<float *, 2> out{outL.data(), outR.data()};
    std::uint32_t frames;
};

/// What the plugin's window and presets told the host.
struct Told {
    std::vector<std::string> heard;
    std::vector<std::pair<std::uint32_t, float>> toAudio;
    std::vector<bool> values;
    std::vector<std::string> shown;
    int closed = 0;
    int held = 0;
    EditorEvents events;

    Told()
    {
        events.changed = [this](std::uint32_t index, float value) {
            heard.push_back(std::to_string(index) + "=" + plugin::valueText(value));
        };
        events.gesture = [this](std::uint32_t index, bool begin) {
            heard.push_back(std::to_string(index) + (begin ? " begin" : " end"));
        };
        events.valuesChanged = [this](bool edit) { values.push_back(edit); };
        events.presetShown = [this](const std::string &id) { shown.push_back(id); };
        events.closed = [this](const char *) { ++closed; };
        events.toAudio = [this](std::uint32_t index, float value) { toAudio.emplace_back(index, value); };
        events.whileHeld = [this](const std::function<void()> &change) {
            ++held;
            change();
        };
    }
};

[[nodiscard]] std::unique_ptr<Lv2Plugin> preparedTestPlugin()
{
    auto world = std::make_shared<Lv2World>();
    std::unique_ptr<Lv2Plugin> plugin = Lv2Plugin::make(world, kTestLv2, Lv2Plugin::Mono::DualMono);
    if (!plugin) {
        return nullptr;
    }
    dsp::PrepareSpec spec;
    spec.sampleRate = 48000.0;
    spec.inputChannels = 2;
    spec.outputChannels = 2;
    if (!plugin->prepare(spec)) {
        return nullptr;
    }
    return plugin;
}

} // namespace

TEST(PluginAtomRing, MessagesComeOutWholeInOrderAcrossTheEnd)
{
    AtomRing ring(256);
    std::array<std::uint8_t, 64> body{};
    std::array<std::uint8_t, 64> back{};
    // Enough round the ring that a message is cut by its end more than once.
    for (std::uint32_t round = 0; round < 100; ++round) {
        for (std::size_t at = 0; at < body.size(); ++at) {
            body[at] = static_cast<std::uint8_t>(round + at);
        }
        const auto size = static_cast<std::uint32_t>(1 + round % 60);
        ASSERT_TRUE(ring.write(round % 7, round, size, body.data()));
        AtomRing::Header header;
        ASSERT_TRUE(ring.read(header, back.data(), back.size()));
        EXPECT_EQ(header.port, round % 7);
        EXPECT_EQ(header.type, round);
        ASSERT_EQ(header.size, size);
        EXPECT_TRUE(std::equal(body.begin(), body.begin() + size, back.begin())) << "round " << round;
    }
    AtomRing::Header header;
    EXPECT_FALSE(ring.read(header, back.data(), back.size()));
}

TEST(PluginAtomRing, AFullRingRefusesAndOneTooLargeToReadIsLetGo)
{
    AtomRing ring(64);
    const std::array<std::uint8_t, 40> body{};
    ASSERT_TRUE(ring.write(1, 1, 40, body.data()));
    EXPECT_FALSE(ring.write(2, 2, 40, body.data())) << "no room, and refused rather than waited for";

    std::array<std::uint8_t, 8> small{};
    AtomRing::Header header;
    EXPECT_FALSE(ring.read(header, small.data(), small.size())) << "too large for the room given";
    ASSERT_TRUE(ring.write(3, 3, 4, body.data()));
    ASSERT_TRUE(ring.read(header, small.data(), small.size()));
    EXPECT_EQ(header.port, 3U);
}

TEST(PluginAtomRing, WritingAndReadingTouchTheAllocatorNotAtAll)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    AtomRing ring(4096);
    std::array<std::uint8_t, 100> body{};
    std::array<std::uint8_t, 100> back{};
    std::size_t allocations = 1;
    {
        const NoAllocations guard(NoAllocations::OnAllocation::Count);
        for (int round = 0; round < 1000; ++round) {
            (void)ring.write(0, 0, 100, body.data());
            AtomRing::Header header;
            (void)ring.read(header, back.data(), back.size());
        }
        allocations = guard.count();
    }
    EXPECT_EQ(allocations, 0U);
}

TEST(PluginLv2Window, WhatThePersonDoesReachesThePluginAndTheEngineAndBack)
{
    const Home home;
    // The loop and what is told outlive the plugin, as the host's do.
    HandLoop loop;
    Told told;
    std::unique_ptr<Lv2Plugin> plugin = preparedTestPlugin();
    ASSERT_NE(plugin, nullptr) << "the tests' LV2 plugin is not in " << RB_TEST_LV2_DIR;
    plugin->attach(loop, told.events);
    ASSERT_TRUE(plugin->hasUi()) << "a window that shows itself needs no display";

    ASSERT_EQ(plugin->openUi(), Editable::Opened::Open);
    EXPECT_TRUE(plugin->uiOpen());
    EXPECT_EQ(loop.timers(), 1U) << "the window's turn, on the host's main loop";
    EXPECT_TRUE(home.wasTold("port 0 1")) << "the window is told what everything is before it is shown";
    EXPECT_TRUE(home.wasTold("shown"));

    // The window's first turn: it takes hold of the gain, moves it to a
    // quarter, and sends the plugin 42.
    loop.tick();
    EXPECT_EQ(told.heard, (std::vector<std::string>{"0 begin", "0=0.25"}));
    ASSERT_EQ(told.toAudio.size(), 1U) << "the value goes to the audio thread, through the host";
    EXPECT_FLOAT_EQ(told.toAudio.front().second, 0.25f);
    EXPECT_FLOAT_EQ(static_cast<float>(*plugin->param("gain").asNumber()), 0.25f);

    // The host's audio thread takes it, and the plugin answers 42 with 43.
    plugin->applyParam(0, 0.25f);
    Cycle cycle(128, 1.0f);
    cycle.run(*plugin);
    EXPECT_FLOAT_EQ(cycle.outL[0], 0.25f);
    EXPECT_FLOAT_EQ(cycle.outR[127], 0.25f);
    loop.tick();
    EXPECT_TRUE(home.wasTold("atom 43")) << "what the plugin said back never reached the window";
    EXPECT_TRUE(home.wasTold("port 7 0.25")) << "the plugin's control output";
    EXPECT_FALSE(home.wasTold("port 0 0.25")) << "the window was told of its own value";

    // **The engine's change reaches the window.**
    plugin->applyParam(0, 2.0f);
    cycle.run(*plugin);
    loop.tick();
    EXPECT_TRUE(home.wasTold("port 0 2"));
    EXPECT_EQ(told.heard.size(), 2U) << "an engine's change came back as the person's";

    // Closed by the person, in the window's own way.
    std::ofstream(home.path() / "window.log.close").put('\n');
    loop.tick();
    EXPECT_EQ(told.closed, 1);
    EXPECT_FALSE(plugin->uiOpen());
    EXPECT_EQ(loop.timers(), 0U);
    EXPECT_TRUE(home.wasTold("hidden"));
    EXPECT_TRUE(home.wasTold("gone"));
}

TEST(PluginLv2Window, ACycleWithTheWindowOpenTouchesTheAllocatorNotAtAll)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    const Home home;
    // The loop and what is told outlive the plugin, as the host's do.
    HandLoop loop;
    Told told;
    std::unique_ptr<Lv2Plugin> plugin = preparedTestPlugin();
    ASSERT_NE(plugin, nullptr);
    plugin->attach(loop, told.events);
    ASSERT_EQ(plugin->openUi(), Editable::Opened::Open);
    loop.tick(); // the window's 42, waiting for the plugin

    Cycle cycle(256, 0.5f);
    std::size_t allocations = 1;
    {
        // Atoms in and out, a control output, changes from the engine:
        // all of it on the audio thread with the window listening.
        const NoAllocations guard(NoAllocations::OnAllocation::Count);
        for (int round = 0; round < 50; ++round) {
            plugin->applyParam(0, 0.01f * static_cast<float>(round));
            cycle.run(*plugin);
        }
        allocations = guard.count();
    }
    EXPECT_EQ(allocations, 0U);
    loop.tick();
    EXPECT_TRUE(home.wasTold("atom 43"));
    plugin->closeUi();
}

TEST(PluginLv2Window, ABurstLargerThanACycleHoldsWaitsForTheNextCycles)
{
    // 3000 numbers at once, 72 KiB of events, where a cycle's sequence
    // holds 64 KiB and takes half of that at most: none is lost, each
    // reaches the plugin a cycle or two later, and its answer comes back.
    constexpr int kBurst = 3000;
    const Home home;
    const ScopedEnv burst("RB_TEST_LV2_UI_BURST", std::to_string(kBurst));
    HandLoop loop;
    Told told;
    std::unique_ptr<Lv2Plugin> plugin = preparedTestPlugin();
    ASSERT_NE(plugin, nullptr);
    plugin->attach(loop, told.events);
    ASSERT_EQ(plugin->openUi(), Editable::Opened::Open);
    loop.tick(); // the window's 42, and the burst
    Cycle cycle(64, 1.0f);
    for (int round = 0; round < 5; ++round) {
        cycle.run(*plugin);
        loop.tick();
    }
    const std::vector<std::string> lines = home.told();
    for (int at = 0; at < kBurst; ++at) {
        ASSERT_NE(std::ranges::find(lines, "atom " + std::to_string(1001 + at)), lines.end())
            << "the answer to " << 1000 + at << " never came";
    }
    plugin->closeUi();
}

TEST(PluginLv2Window, APresetIsSavedWhereOtherHostsLookListedAndPutBackWithThePluginHeld)
{
    const Home home;
    // The loop and what is told outlive the plugin, as the host's do.
    HandLoop loop;
    Told told;
    std::unique_ptr<Lv2Plugin> plugin = preparedTestPlugin();
    ASSERT_NE(plugin, nullptr);
    plugin->attach(loop, told.events);
    EXPECT_TRUE(plugin->presets().empty());

    plugin->applyParam(0, 0.75f);
    const std::optional<std::string> saved = plugin->savePreset("Three quarters");
    ASSERT_TRUE(saved.has_value());
    const fs::path bundle = home.path() / ".lv2" / "Reverblabr_test_gain_Three_quarters.preset.lv2";
    EXPECT_TRUE(fs::exists(bundle / "manifest.ttl")) << *saved;
    EXPECT_TRUE(fs::exists(bundle / "Three_quarters.ttl")) << *saved;

    const std::vector<plugin::PresetEntry> listed = plugin->presets();
    ASSERT_EQ(listed.size(), 1U);
    EXPECT_EQ(listed.front().id, *saved);
    EXPECT_EQ(listed.front().name, "Three quarters");

    plugin->applyParam(0, 2.0f);
    ASSERT_TRUE(plugin->loadPreset(*saved));
    EXPECT_FLOAT_EQ(static_cast<float>(*plugin->param("gain").asNumber()), 0.75f);
    EXPECT_EQ(told.held, 1) << "a state put in while the audio thread could be in the plugin";
    EXPECT_EQ(told.values, (std::vector<bool>{true})) << "a preset a person chose is an edit";
    EXPECT_EQ(told.shown, (std::vector<std::string>{*saved}));
    Cycle cycle(64, 1.0f);
    cycle.run(*plugin);
    EXPECT_FLOAT_EQ(cycle.outL[63], 0.75f);

    EXPECT_FALSE(plugin->loadPreset("urn:reverblabr:test:no-such-preset"));
}

TEST(PluginLv2Window, PresetsWhoseNamesMakeOneFileNameAreKeptApart)
{
    // Names in another script are files of their own; two names that still
    // make one file name (spaces and underscores) do not overwrite each
    // other; and saving onto a name replaces that one.
    const Home home;
    HandLoop loop;
    Told told;
    std::unique_ptr<Lv2Plugin> plugin = preparedTestPlugin();
    ASSERT_NE(plugin, nullptr);
    plugin->attach(loop, told.events);

    const auto save = [&](const std::string &name, float gain) {
        plugin->applyParam(0, gain);
        return plugin->savePreset(name);
    };
    const std::optional<std::string> warm = save("Тёплый", 0.5f);
    const std::optional<std::string> soft = save("Мягкий", 0.25f);
    const std::optional<std::string> spaced = save("a b", 1.5f);
    const std::optional<std::string> joined = save("a_b", 2.0f);
    ASSERT_TRUE(warm && soft && spaced && joined);
    EXPECT_NE(*warm, *soft);
    EXPECT_NE(*spaced, *joined);
    std::vector<std::string> names;
    for (const plugin::PresetEntry &preset : plugin->presets()) {
        names.push_back(preset.name);
    }
    std::ranges::sort(names);
    EXPECT_EQ(names, (std::vector<std::string>{"a b", "a_b", "Мягкий", "Тёплый"}));

    // The same name again replaces it, rather than adding a fifth.
    const std::optional<std::string> again = save("Тёплый", 0.75f);
    ASSERT_TRUE(again);
    EXPECT_EQ(*again, *warm);
    EXPECT_EQ(plugin->presets().size(), 4U);
    ASSERT_TRUE(plugin->loadPreset(*spaced));
    EXPECT_FLOAT_EQ(static_cast<float>(*plugin->param("gain").asNumber()), 1.5f);
}
