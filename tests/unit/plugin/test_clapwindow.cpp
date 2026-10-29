// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// A CLAP plugin's own window and its presets, as the host runs them, with
// no host process and no server: the tests' plugin (tests/clap), whose
// window is a stand-in that needs no display but asks for the host's timers
// and a descriptor as a real one does; the parameters it moves, with the
// gestures round them; the audio thread allocation-free with the window
// open; its presets through preset discovery and preset-load.

#include "allocationtrap.h"
#include "clapplugin.h"
#include "dsp/audioblock.h"
#include "editor.h"

#include <gtest/gtest.h>
#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <array>
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

constexpr const char *kTestClap = "org.reverblabr.test.gain";

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

/// A main loop that runs only when a test says: tick() calls every timer
/// and every descriptor that has something to read.
class HandLoop final : public MainLoop
{
public:
    std::optional<Id> addTimer(std::chrono::milliseconds, std::function<void()> tick) override
    {
        m_timers.emplace(++m_next, std::move(tick));
        return m_next;
    }
    std::optional<Id> addFd(int fd, std::uint32_t, std::function<void(std::uint32_t)> ready) override
    {
        m_fds.emplace(++m_next, std::pair{fd, std::move(ready)});
        return m_next;
    }
    bool changeFd(Id id, std::uint32_t) override
    {
        return m_fds.contains(id);
    }
    void remove(Id id) override
    {
        m_timers.erase(id);
        m_fds.erase(id);
    }
    void tick()
    {
        const auto timers = m_timers;
        for (const auto &[id, timer] : timers) {
            if (m_timers.contains(id)) {
                timer();
            }
        }
        const auto fds = m_fds;
        for (const auto &[id, watched] : fds) {
            pollfd readable{watched.first, POLLIN, 0};
            if (m_fds.contains(id) && ::poll(&readable, 1, 0) > 0 && (readable.revents & POLLIN) != 0) {
                watched.second(Readable);
            }
        }
    }
    [[nodiscard]] std::size_t watches() const
    {
        return m_timers.size() + m_fds.size();
    }

private:
    std::map<Id, std::function<void()>> m_timers;
    std::map<Id, std::pair<int, std::function<void(std::uint32_t)>>> m_fds;
    Id m_next = 0;
};

/// A folder of the test's own: the plugin's notes, and its preset files.
class Folder
{
public:
    Folder()
        : m_dir(fs::temp_directory_path() / ("rb-clapwindow-" + std::to_string(::getpid())))
        , m_log("RB_TEST_CLAP_LOG", (m_dir / "window.log").string())
        , m_presets("RB_TEST_CLAP_PRESETS", (m_dir / "presets").string())
    {
        fs::create_directories(m_dir / "presets");
    }
    ~Folder()
    {
        std::error_code ignored;
        fs::remove_all(m_dir, ignored);
    }
    Folder(const Folder &) = delete;
    Folder &operator=(const Folder &) = delete;

    [[nodiscard]] bool wasTold(const std::string &line) const
    {
        std::ifstream in(m_dir / "window.log");
        for (std::string said; std::getline(in, said);) {
            if (said == line) {
                return true;
            }
        }
        return false;
    }
    [[nodiscard]] fs::path path() const
    {
        return m_dir;
    }

private:
    fs::path m_dir;
    ScopedEnv m_log;
    ScopedEnv m_presets;
};

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

struct Told {
    std::vector<std::string> heard;
    std::vector<bool> values;
    std::vector<std::string> shown;
    int closed = 0;
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
        events.whileHeld = [](const std::function<void()> &change) { change(); };
    }
};

[[nodiscard]] std::unique_ptr<ClapPlugin> preparedTestPlugin()
{
    const std::shared_ptr<ClapLibrary> library =
        ClapLibrary::open((fs::path{RB_TEST_CLAP_DIR} / "rb-test-clap.clap").string());
    if (!library) {
        return nullptr;
    }
    std::unique_ptr<ClapPlugin> plugin = ClapPlugin::make(library, kTestClap, ClapPlugin::Mono::DualMono);
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

TEST(PluginClapWindow, WhatThePersonDoesInTheWindowReachesTheEngineWithItsGesture)
{
    const Folder folder;
    // The loop and what is told outlive the plugin, as the host's do.
    HandLoop loop;
    Told told;
    std::unique_ptr<ClapPlugin> plugin = preparedTestPlugin();
    ASSERT_NE(plugin, nullptr) << "the tests' CLAP plugin is not in " << RB_TEST_CLAP_DIR;
    plugin->attach(loop, told.events);
    ASSERT_TRUE(plugin->hasUi()) << "a floating window needs no window of the host's";

    ASSERT_EQ(plugin->openUi(), Editable::Opened::Open);
    EXPECT_TRUE(folder.wasTold("shown"));
    // Its timer and its descriptor, through the host's timer-support and
    // posix-fd-support, besides the host's own turn.
    EXPECT_EQ(loop.watches(), 3U);

    Cycle cycle(128, 1.0f);
    loop.tick(); // the window takes hold of the gain
    cycle.run(*plugin); // and says so in the next process call
    loop.tick();
    EXPECT_EQ(told.heard, (std::vector<std::string>{"0 begin", "0=0.25"}));
    EXPECT_TRUE(folder.wasTold("fd")) << "the plugin's descriptor was never watched";
    EXPECT_FLOAT_EQ(static_cast<float>(*plugin->param("1").asNumber()), 0.25f);

    // It rests, longer than a guessed gesture, and moves again and lets go.
    for (int tick = 0; tick < 80 && told.heard.size() < 4; ++tick) {
        loop.tick();
        cycle.run(*plugin);
    }
    loop.tick();
    EXPECT_EQ(told.heard, (std::vector<std::string>{"0 begin", "0=0.25", "0=" + plugin::valueText(0.3f), "0 end"}));
    EXPECT_FLOAT_EQ(cycle.outL[127], 0.3f);

    // **The engine's change reaches the plugin**, and so its window, and
    // does not come back as a person's.
    plugin->applyParam(0, 2.0f);
    cycle.run(*plugin);
    loop.tick();
    EXPECT_TRUE(folder.wasTold("gain 2"));
    EXPECT_EQ(told.heard.size(), 4U);

    // Closed by a person: said once, and taken down on the host's turn.
    std::ofstream(folder.path() / "window.log.close").put('\n');
    loop.tick();
    loop.tick();
    EXPECT_EQ(told.closed, 1);
    EXPECT_FALSE(plugin->uiOpen());
    EXPECT_TRUE(folder.wasTold("hidden"));
    EXPECT_TRUE(folder.wasTold("destroyed"));
    EXPECT_EQ(loop.watches(), 1U) << "the window's timer and descriptor were let go";
}

TEST(PluginClapWindow, ACycleWithTheWindowOpenTouchesTheAllocatorNotAtAll)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    const Folder folder;
    // The loop and what is told outlive the plugin, as the host's do.
    HandLoop loop;
    Told told;
    std::unique_ptr<ClapPlugin> plugin = preparedTestPlugin();
    ASSERT_NE(plugin, nullptr);
    plugin->attach(loop, told.events);
    ASSERT_EQ(plugin->openUi(), Editable::Opened::Open);
    Cycle cycle(256, 0.5f);
    cycle.run(*plugin); // started, outside the trap

    std::size_t allocations = 1;
    {
        // The window's gestures and values, the engine's changes: all of
        // it on the audio thread with the window listening.
        const NoAllocations guard(NoAllocations::OnAllocation::Count);
        for (int round = 0; round < 100; ++round) {
            plugin->applyParam(0, 0.01f * static_cast<float>(round));
            cycle.run(*plugin);
        }
        allocations = guard.count();
    }
    EXPECT_EQ(allocations, 0U);
    plugin->closeUi();
}

TEST(PluginClapWindow, ItsOwnPresetsAreFoundAndPutInAndTheValuesFollow)
{
    const Folder folder;
    std::ofstream(folder.path() / "presets" / "Loud.rbgain") << "3\n";
    std::ofstream(folder.path() / "presets" / "ignored.txt") << "2\n";
    // The loop and what is told outlive the plugin, as the host's do.
    HandLoop loop;
    Told told;
    std::unique_ptr<ClapPlugin> plugin = preparedTestPlugin();
    ASSERT_NE(plugin, nullptr);
    plugin->attach(loop, told.events);

    const std::vector<plugin::PresetEntry> listed = plugin->presets();
    ASSERT_EQ(listed.size(), 2U);
    EXPECT_EQ(listed[0].name, "Half");
    EXPECT_EQ(listed[0].id, "plugin:#half");
    EXPECT_EQ(listed[1].name, "Loud");
    EXPECT_EQ(listed[1].id, (folder.path() / "presets" / "Loud.rbgain").string());

    ASSERT_TRUE(plugin->loadPreset(listed[1].id));
    loop.tick();
    EXPECT_FLOAT_EQ(static_cast<float>(*plugin->param("1").asNumber()), 3.0f);
    EXPECT_EQ(told.values, (std::vector<bool>{true})) << "a preset a person chose is an edit";
    EXPECT_EQ(told.shown, (std::vector<std::string>{listed[1].id}));
    Cycle cycle(64, 1.0f);
    cycle.run(*plugin);
    EXPECT_FLOAT_EQ(cycle.outL[63], 3.0f);

    ASSERT_TRUE(plugin->loadPreset("plugin:#half"));
    loop.tick();
    EXPECT_FLOAT_EQ(static_cast<float>(*plugin->param("1").asNumber()), 0.5f);
    EXPECT_EQ(told.shown.back(), "plugin:#half");
    EXPECT_FALSE(plugin->loadPreset("plugin:#nothing"));
}
