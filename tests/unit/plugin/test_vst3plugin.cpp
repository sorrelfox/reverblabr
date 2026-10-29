// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// A VST3 plugin as the host runs it, with no host process and no server:
// the tests' plugin (tests/vst3), described from its bundles; its sound,
// once a side for its mono one; both halves' states; its `.vstpreset`
// files; and its view, a stand-in that needs no display but uses the run
// loop the window offers as a real one does, put in a parent of the test's
// own, with the gestures round what it moves. The audio thread stays
// allocation-free with the view open.

#include "allocationtrap.h"
#include "dsp/audioblock.h"
#include "editor.h"
#include "vst3module.h"
#include "vst3plugin.h"

#include <gtest/gtest.h>
#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using namespace rb;
using namespace rb::pluginhost;
using rb::test::NoAllocations;
namespace fs = std::filesystem;

namespace
{

constexpr const char *kStereo = "5242565354334741494E53544552454F";
constexpr const char *kMono = "5242565354334741494E4D4F4E4F2020";

[[nodiscard]] std::string bundle(const std::string &name = "rb-test-vst3")
{
    return (fs::path{RB_TEST_VST3_DIR} / (name + ".vst3")).string();
}

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

/// A folder of the test's own: the plugin's notes, a home for its presets
/// and a state folder for where windows were.
class Folder
{
public:
    Folder()
        : m_dir(fs::temp_directory_path() / ("rb-vst3plugin-" + std::to_string(::getpid())))
        , m_log("RB_TEST_VST3_LOG", (m_dir / "view.log").string())
        , m_home("HOME", (m_dir / "home").string())
        , m_state("XDG_STATE_HOME", (m_dir / "state").string())
    {
        fs::create_directories(m_dir / "home");
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
        std::ifstream in(m_dir / "view.log");
        for (std::string said; std::getline(in, said);) {
            if (said == line) {
                return true;
            }
        }
        return false;
    }
    [[nodiscard]] int count(const std::string &line) const
    {
        int found = 0;
        std::ifstream in(m_dir / "view.log");
        for (std::string said; std::getline(in, said);) {
            found += said == line ? 1 : 0;
        }
        return found;
    }
    [[nodiscard]] fs::path path() const
    {
        return m_dir;
    }
    /// Where the user's presets of the stereo plugin are.
    [[nodiscard]] fs::path presets() const
    {
        return m_dir / "home" / ".vst3" / "presets" / "Reverblabr" / "Reverblabr test gain";
    }

private:
    fs::path m_dir;
    ScopedEnv m_log;
    ScopedEnv m_home;
    ScopedEnv m_state;
};

/// **A parent of the test's own**, which needs no display; a person
/// closes it through close().
struct StandIns {
    struct Parent final : public Vst3ViewParent {
        explicit Parent(StandIns &owner)
            : m_owner(owner)
        {
        }
        ~Parent() override
        {
            m_owner.open = nullptr;
        }
        [[nodiscard]] unsigned long id() const override
        {
            return 0x5a5a;
        }
        void resize(int width, int height) override
        {
            m_owner.size = {width, height};
        }
        void setResizable(bool resizable) override
        {
            m_owner.resizable = resizable;
        }
        void show() override
        {
            m_owner.shown = true;
        }
        StandIns &m_owner;
        std::function<void(bool)> closed;
    };

    StandIns()
    {
        Vst3Plugin::setViewParents([this](MainLoop &,
                                          const std::string &,
                                          const std::string &,
                                          int width,
                                          int height,
                                          std::function<void(bool)> closed,
                                          std::function<void(int, int)>) {
            auto parent = std::make_unique<Parent>(*this);
            parent->closed = std::move(closed);
            size = {width, height};
            open = parent.get();
            return parent;
        });
    }
    ~StandIns()
    {
        Vst3Plugin::setViewParents({});
    }
    StandIns(const StandIns &) = delete;
    StandIns &operator=(const StandIns &) = delete;

    /// A person closes the window.
    void close() const
    {
        if (open != nullptr) {
            const std::function<void(bool)> closed = open->closed;
            closed(false);
        }
    }

    Parent *open = nullptr;
    std::pair<int, int> size;
    bool resizable = false;
    bool shown = false;
};

struct Cycle {
    explicit Cycle(std::uint32_t frames, float left, float right)
        : inL(frames, left)
        , inR(frames, right)
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

/// What the plugin tells the host, and the host's queue to the audio
/// thread, which here is the same thread.
struct Told {
    std::vector<std::string> heard;
    std::vector<bool> values;
    std::vector<std::string> shown;
    std::vector<std::string> toAudio;
    int closed = 0;
    dsp::Block *block = nullptr;
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
        events.toAudio = [this](std::uint32_t index, float value) {
            toAudio.push_back(std::to_string(index) + "=" + plugin::valueText(value));
            if (block != nullptr) {
                block->applyParam(index, value);
            }
        };
        events.whileHeld = [](const std::function<void()> &change) { change(); };
    }
};

[[nodiscard]] std::unique_ptr<Vst3Plugin> preparedPlugin(const char *id = kStereo,
                                                         Vst3Plugin::Mono mono = Vst3Plugin::Mono::DualMono)
{
    const std::shared_ptr<Vst3Module> module = Vst3Module::open(bundle());
    if (!module) {
        return nullptr;
    }
    std::unique_ptr<Vst3Plugin> plugin = Vst3Plugin::make(module, id, mono);
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

/// A `.vstpreset` made by hand, byte by byte as the format lays one out,
/// rather than by the host's own writer: the component's state of the
/// tests' plugin, its gain at \a gain of 0 to 1.
[[nodiscard]] std::string handMadePreset(const std::string &classId, float gain)
{
    const auto little = [](std::string &out, std::uint64_t value, int bytes) {
        for (int at = 0; at < bytes; ++at) {
            out += static_cast<char>((value >> (8 * at)) & 0xff);
        }
    };
    std::string component(8, '\0');
    std::memcpy(component.data(), &gain, 4); // a float, then the colour
    std::string file = "VST3";
    little(file, 1, 4);
    file += classId;
    little(file, 48 + component.size(), 8); // the list, after the one chunk
    file += component;
    file += "List";
    little(file, 1, 4);
    file += "Comp";
    little(file, 48, 8);
    little(file, component.size(), 8);
    return file;
}

} // namespace

// --- describing ---

TEST(PluginVst3, ABundleIsDescribedByLoadingItOrFromWhatItSaysOfItself)
{
    std::vector<plugin::PluginDescription> found;
    const auto collect = [&found](const plugin::PluginDescription &description) { found.push_back(description); };

    // **No moduleinfo.json**: every plugin the factory makes, by loading it.
    ASSERT_TRUE(describeVst3(bundle(), collect));
    ASSERT_EQ(found.size(), 2U) << "the stereo and the mono plugin, and not their controller";
    EXPECT_EQ(found[0].id, kStereo);
    EXPECT_EQ(found[0].name, "Reverblabr test gain");
    EXPECT_EQ(found[0].vendor, "Reverblabr");
    EXPECT_EQ(found[0].category, "Tools");
    EXPECT_EQ(found[0].audioInputs, 2U) << "the main bus, not the side chain";
    EXPECT_EQ(found[0].audioOutputs, 2U);
    EXPECT_EQ(found[1].id, kMono);
    EXPECT_EQ(found[1].audioInputs, 1U);

    // **Its settings**: neither the hidden one nor the meter.
    ASSERT_EQ(found[0].params.size(), 2U);
    const plugin::PluginParam &gain = found[0].params[0];
    EXPECT_EQ(gain.id, "1");
    EXPECT_EQ(gain.name, "Gain");
    EXPECT_EQ(gain.kind, plugin::PluginParam::Kind::Float);
    EXPECT_FLOAT_EQ(gain.minimum, 0.0f);
    EXPECT_FLOAT_EQ(gain.maximum, 4.0f);
    EXPECT_FLOAT_EQ(gain.defaultValue, 1.0f);
    const plugin::PluginParam &colour = found[0].params[1];
    EXPECT_EQ(colour.kind, plugin::PluginParam::Kind::Choice);
    EXPECT_EQ(colour.choices,
              (std::vector<std::pair<float, std::string>>{{0.0f, "Clean"}, {1.0f, "Warm"}, {2.0f, "Hot"}}));

    // **With one**, which names only the stereo plugin: it is the list.
    found.clear();
    ASSERT_TRUE(describeVst3(bundle("rb-test-vst3-listed"), collect));
    ASSERT_EQ(found.size(), 1U);
    EXPECT_EQ(found[0].id, kStereo);
    EXPECT_EQ(found[0].params.size(), 2U) << "its settings still come from its code";

    // **One that names no plugin's sound is never loaded**: it has no
    // library to load.
    found.clear();
    EXPECT_TRUE(describeVst3(bundle("rb-test-vst3-empty"), collect));
    EXPECT_TRUE(found.empty());
    EXPECT_FALSE(describeVst3((fs::path{RB_TEST_VST3_DIR} / "nothing.vst3").string(), collect));
}

TEST(PluginVst3, ACurveFindsTheValueAndBackOnALineOnACurveAndInSteps)
{
    Vst3Curve line;
    for (int at = 0; at <= 4; ++at) {
        line.plain.push_back(10.0 * at);
    }
    EXPECT_DOUBLE_EQ(line.plainOf(0.5), 20.0);
    EXPECT_DOUBLE_EQ(line.normalizedOf(25.0), 0.625);
    EXPECT_DOUBLE_EQ(line.normalizedOf(-3.0), 0.0) << "below the range is its start";
    EXPECT_DOUBLE_EQ(line.normalizedOf(99.0), 1.0);

    Vst3Curve frequency;
    frequency.logarithmic = true;
    for (int at = 0; at <= 2; ++at) {
        frequency.plain.push_back(20.0 * std::pow(1000.0, at / 2.0));
    }
    EXPECT_NEAR(frequency.plainOf(0.25), 20.0 * std::pow(1000.0, 0.25), 1e-9);
    EXPECT_NEAR(frequency.normalizedOf(frequency.plainOf(0.8)), 0.8, 1e-12);

    Vst3Curve steps;
    steps.stepped = true;
    steps.plain = {0.0, 1.0, 2.0};
    EXPECT_DOUBLE_EQ(steps.plainOf(0.5), 1.0) << "the middle third is the middle step";
    EXPECT_DOUBLE_EQ(steps.plainOf(0.99), 2.0);
    EXPECT_DOUBLE_EQ(steps.normalizedOf(2.0), 1.0);
    EXPECT_DOUBLE_EQ(steps.normalizedOf(0.9), 0.5) << "the nearest step";
}

TEST(PluginVst3, APresetFileIsReadAsTheFormatLaysItOut)
{
    const std::optional<Vst3Preset> made = readVst3Preset(handMadePreset(kStereo, 0.5f));
    ASSERT_TRUE(made.has_value());
    EXPECT_EQ(made->classId, kStereo);
    EXPECT_EQ(made->component.size(), 8U);
    EXPECT_FALSE(made->controller.has_value());

    Vst3Preset both{kMono, "component bytes", std::string{"controller bytes"}};
    const std::optional<Vst3Preset> back = readVst3Preset(writeVst3Preset(both));
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(back->classId, kMono);
    EXPECT_EQ(back->component, "component bytes");
    EXPECT_EQ(back->controller, std::optional<std::string>{"controller bytes"});

    EXPECT_FALSE(readVst3Preset("VST3 and then nothing much").has_value());
    std::string cut = writeVst3Preset(both);
    cut.resize(cut.size() - 4);
    EXPECT_FALSE(readVst3Preset(cut).has_value()) << "a list that runs past the end";
}

// --- the sound ---

TEST(PluginVst3, ItsSoundFollowsTheEnginesSettings)
{
    const Folder folder;
    std::unique_ptr<Vst3Plugin> plugin = preparedPlugin();
    ASSERT_NE(plugin, nullptr) << "the tests' VST3 plugin is not in " << RB_TEST_VST3_DIR;
    EXPECT_EQ(plugin->instanceCount(), 1U);
    EXPECT_EQ(plugin->info().inputs.maxCount, 2U);
    ASSERT_EQ(plugin->params().size(), 2U);
    EXPECT_TRUE(folder.wasTold("hello 7")) << "the component's message, made by the host, never arrived";

    Cycle cycle(64, 1.0f, 0.5f);
    cycle.run(*plugin);
    EXPECT_FLOAT_EQ(cycle.outL[63], 1.0f) << "its own gain of 1";
    EXPECT_FLOAT_EQ(cycle.outR[63], 0.5f);

    plugin->applyParam(0, 2.0f);
    plugin->applyParam(1, 2.0f); // the third choice, by position
    cycle.run(*plugin);
    EXPECT_FLOAT_EQ(cycle.outL[63], 2.0f);
    EXPECT_FLOAT_EQ(static_cast<float>(*plugin->param("1").asNumber()), 2.0f);
    EXPECT_FLOAT_EQ(static_cast<float>(*plugin->param("2").asNumber()), 2.0f) << "a choice is kept as its value";
}

TEST(PluginVst3, AMonoPluginRunsOnceASideOrOnTheSidesMixed)
{
    const Folder folder;
    std::unique_ptr<Vst3Plugin> twice = preparedPlugin(kMono, Vst3Plugin::Mono::DualMono);
    ASSERT_NE(twice, nullptr);
    EXPECT_EQ(twice->instanceCount(), 2U);
    twice->applyParam(0, 2.0f);
    Cycle cycle(32, 1.0f, 0.25f);
    cycle.run(*twice);
    EXPECT_FLOAT_EQ(cycle.outL[31], 2.0f);
    EXPECT_FLOAT_EQ(cycle.outR[31], 0.5f) << "the second instance heard the change too";

    std::unique_ptr<Vst3Plugin> mixed = preparedPlugin(kMono, Vst3Plugin::Mono::Downmix);
    ASSERT_NE(mixed, nullptr);
    EXPECT_EQ(mixed->instanceCount(), 1U);
    cycle.run(*mixed);
    EXPECT_FLOAT_EQ(cycle.outL[31], 0.625f);
    EXPECT_FLOAT_EQ(cycle.outR[31], 0.625f) << "a mono output goes to both sides";
}

TEST(PluginVst3, BothHalvesStatesComeBackInAFreshPlugin)
{
    const Folder folder;
    const StandIns parents;
    HandLoop loop;
    Told told;
    std::unique_ptr<Vst3Plugin> plugin = preparedPlugin();
    ASSERT_NE(plugin, nullptr);
    plugin->attach(loop, told.events);
    plugin->applyParam(0, 3.0f);
    Cycle cycle(16, 1.0f, 1.0f);
    cycle.run(*plugin);
    // The view sets the controller's own zoom to 2.
    ASSERT_EQ(plugin->openUi(), Editable::Opened::Open);
    loop.tick();
    plugin->closeUi();
    const std::string state = plugin->stateText();
    ASSERT_FALSE(state.empty());

    std::unique_ptr<Vst3Plugin> fresh = preparedPlugin();
    ASSERT_NE(fresh, nullptr);
    ASSERT_TRUE(fresh->loadStateText(state));
    EXPECT_TRUE(folder.wasTold("zoom 2")) << "the controller's own state did not come back";
    EXPECT_FLOAT_EQ(static_cast<float>(*fresh->param("1").asNumber()), 3.0f) << "the gain the sound had";
    EXPECT_FALSE(fresh->loadStateText("not a state"));
    std::unique_ptr<Vst3Plugin> other = preparedPlugin(kMono);
    ASSERT_NE(other, nullptr);
    EXPECT_FALSE(other->loadStateText(state)) << "another plugin's state";
}

// --- the view ---

TEST(PluginVst3, WhatThePersonDoesInTheViewReachesTheEngineAndTheSoundWithItsGesture)
{
    const Folder folder;
    const StandIns parents;
    // The loop and what is told outlive the plugin, as the host's do.
    HandLoop loop;
    Told told;
    std::unique_ptr<Vst3Plugin> plugin = preparedPlugin();
    ASSERT_NE(plugin, nullptr);
    told.block = plugin.get();
    plugin->attach(loop, told.events);
    ASSERT_TRUE(plugin->hasUi());

    ASSERT_EQ(plugin->openUi(), Editable::Opened::Open);
    EXPECT_TRUE(folder.wasTold("attached"));
    EXPECT_TRUE(parents.shown);
    EXPECT_TRUE(parents.resizable);
    EXPECT_EQ(parents.size, (std::pair{300, 150})) << "the size the view asked for";
    // Its timer and its descriptor, through the window's run loop, besides
    // the host's own turn.
    EXPECT_EQ(loop.watches(), 3U);

    Cycle cycle(128, 1.0f, 1.0f);
    loop.tick(); // the view asks for room, takes hold of the gain and moves it
    EXPECT_EQ(parents.size, (std::pair{320, 200}));
    EXPECT_TRUE(folder.wasTold("size 320x200")) << "the view was not told it has the room";
    EXPECT_EQ(told.heard, (std::vector<std::string>{"0 begin", "0=0.25"}));
    EXPECT_EQ(told.toAudio, (std::vector<std::string>{"0=0.25"}));
    EXPECT_TRUE(folder.wasTold("fd")) << "the view's descriptor was never watched";
    cycle.run(*plugin);
    EXPECT_FLOAT_EQ(cycle.outL[127], 0.25f) << "the sound heard the view";

    // It rests, longer than a guessed gesture, and moves again and lets go.
    for (int tick = 0; tick < 80 && told.heard.size() < 4; ++tick) {
        loop.tick();
        cycle.run(*plugin);
    }
    EXPECT_EQ(told.heard, (std::vector<std::string>{"0 begin", "0=0.25", "0=" + plugin::valueText(0.3f), "0 end"}));
    cycle.run(*plugin);
    EXPECT_FLOAT_EQ(cycle.outL[127], 0.3f);
    EXPECT_TRUE(folder.wasTold("level")) << "the meter the sound gave out never reached the controller";

    // **The engine's change reaches the controller**, and so the view, and
    // does not come back as a person's.
    plugin->applyParam(0, 2.0f);
    cycle.run(*plugin);
    loop.tick();
    EXPECT_TRUE(folder.wasTold("gain 2.00"));
    EXPECT_EQ(told.heard.size(), 4U);

    // Closed by a person: said once, and what the view had on the loop
    // let go of.
    parents.close();
    EXPECT_EQ(told.closed, 1);
    EXPECT_FALSE(plugin->uiOpen());
    EXPECT_TRUE(folder.wasTold("removed"));
    EXPECT_EQ(loop.watches(), 1U) << "the view's timer and descriptor were let go";

    // Opened again, and closed by the engine this time.
    ASSERT_EQ(plugin->openUi(), Editable::Opened::Open);
    plugin->closeUi();
    EXPECT_EQ(told.closed, 1);
    EXPECT_EQ(loop.watches(), 1U);
}

TEST(PluginVst3, ACycleWithTheViewOpenTouchesTheAllocatorNotAtAll)
{
    if (!NoAllocations::isSupported()) {
        GTEST_SKIP() << "the allocator belongs to a sanitizer in this build";
    }
    const Folder folder;
    const StandIns parents;
    HandLoop loop;
    Told told;
    std::unique_ptr<Vst3Plugin> plugin = preparedPlugin();
    ASSERT_NE(plugin, nullptr);
    plugin->attach(loop, told.events);
    ASSERT_EQ(plugin->openUi(), Editable::Opened::Open);
    Cycle cycle(256, 0.5f, 0.5f);
    cycle.run(*plugin); // started, outside the trap

    std::size_t allocations = 1;
    {
        // The engine's changes, a choice among them, and what the sound
        // gives out: all of it on the audio thread with the view open.
        const NoAllocations guard(NoAllocations::OnAllocation::Count);
        for (int round = 0; round < 100; ++round) {
            plugin->applyParam(0, 0.01f * static_cast<float>(round));
            plugin->applyParam(1, static_cast<float>(round % 3));
            cycle.run(*plugin);
        }
        allocations = guard.count();
    }
    EXPECT_EQ(allocations, 0U);
    plugin->closeUi();
}

// --- presets ---

TEST(PluginVst3, ItsPresetsAreFilesWhereOtherHostsKeepThem)
{
    const Folder folder;
    fs::create_directories(folder.presets() / "Factory");
    std::ofstream(folder.presets() / "Factory" / "Loud.vstpreset", std::ios::binary) << handMadePreset(kStereo, 0.75f);
    std::ofstream(folder.presets() / "Not ours.vstpreset", std::ios::binary) << handMadePreset(kMono, 0.5f);
    std::ofstream(folder.presets() / "Broken.vstpreset", std::ios::binary) << "VST3";
    HandLoop loop;
    Told told;
    std::unique_ptr<Vst3Plugin> plugin = preparedPlugin();
    ASSERT_NE(plugin, nullptr);
    plugin->attach(loop, told.events);

    std::vector<plugin::PresetEntry> listed = plugin->presets();
    ASSERT_EQ(listed.size(), 1U) << "only this plugin's, and only ones that can be read";
    EXPECT_EQ(listed[0].name, "Loud");
    EXPECT_EQ(listed[0].id, (folder.presets() / "Factory" / "Loud.vstpreset").string());

    ASSERT_TRUE(plugin->loadPreset(listed[0].id));
    EXPECT_FLOAT_EQ(static_cast<float>(*plugin->param("1").asNumber()), 3.0f);
    EXPECT_EQ(told.values, (std::vector<bool>{true})) << "a preset a person chose is an edit";
    EXPECT_EQ(told.shown, (std::vector<std::string>{listed[0].id}));
    Cycle cycle(64, 1.0f, 1.0f);
    cycle.run(*plugin);
    EXPECT_FLOAT_EQ(cycle.outL[63], 3.0f);
    EXPECT_FALSE(plugin->loadPreset((folder.presets() / "Not ours.vstpreset").string()));

    // **One kept from the block** goes where the user's are, and is found.
    plugin->applyParam(0, 2.0f);
    cycle.run(*plugin);
    const std::optional<std::string> saved = plugin->savePreset("Mine/too");
    ASSERT_TRUE(saved.has_value());
    EXPECT_EQ(*saved, (folder.presets() / "Mine_too.vstpreset").string());
    listed = plugin->presets();
    ASSERT_EQ(listed.size(), 2U);
    EXPECT_EQ(listed[1].name, "Mine_too");
    plugin->applyParam(0, 1.0f);
    cycle.run(*plugin);
    ASSERT_TRUE(plugin->loadPreset(*saved));
    EXPECT_FLOAT_EQ(static_cast<float>(*plugin->param("1").asNumber()), 2.0f);
}

TEST(PluginVst3, ParameterChangesHaveOneQueueAParameterACycle)
{
    Vst3Changes changes(4);
    EXPECT_TRUE(changes.add(5, 0.1));
    EXPECT_TRUE(changes.add(7, 0.5));
    EXPECT_TRUE(changes.add(5, 0.2));
    ASSERT_EQ(changes.count(), 2U) << "a second value for parameter 5 is in its one queue";
    EXPECT_EQ(changes.idAt(0), 5U);
    EXPECT_EQ(changes.lastAt(0), std::optional<double>{0.2});
    Steinberg::int32 index = -1;
    EXPECT_EQ(changes.addParameterData(7, index), changes.getParameterData(1));
    EXPECT_EQ(index, 1);
    EXPECT_EQ(changes.getParameterCount(), 2);
}

TEST(PluginVst3, AClassIdIsThePluginsInEitherCase)
{
    const Folder folder;
    const std::shared_ptr<Vst3Module> module = Vst3Module::open(bundle());
    ASSERT_NE(module, nullptr);
    std::string lower = kStereo;
    std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::unique_ptr<Vst3Plugin> plugin = Vst3Plugin::make(module, lower, Vst3Plugin::Mono::DualMono);
    ASSERT_NE(plugin, nullptr);
    std::unique_ptr<Vst3Plugin> other = preparedPlugin();
    ASSERT_NE(other, nullptr);
    EXPECT_TRUE(plugin->loadStateText(other->stateText())) << "a state of the same plugin, written in capitals";
    EXPECT_EQ(vst3CanonicalId(lower), std::optional<std::string>{kStereo});
    EXPECT_FALSE(vst3CanonicalId("not a class id").has_value());
}

TEST(PluginVst3, ANewLatencyIsTakenOnByActivatingThePluginAgain)
{
    const Folder folder;
    HandLoop loop;
    Told told;
    std::unique_ptr<Vst3Plugin> plugin = preparedPlugin();
    ASSERT_NE(plugin, nullptr);
    plugin->attach(loop, told.events);
    EXPECT_EQ(plugin->info().latencySamples, 0U);
    // The colour changes, the controller hears it and asks for a restart
    // for its latency, and the host's turn takes it on.
    plugin->applyParam(1, 2.0f);
    Cycle cycle(16, 1.0f, 1.0f);
    cycle.run(*plugin);
    plugin->editorTick();
    EXPECT_FALSE(plugin->idle()) << "a new latency is not a plugin made again";
    EXPECT_EQ(plugin->info().latencySamples, 32U) << "read before the plugin was activated again";
    cycle.run(*plugin);
    EXPECT_FLOAT_EQ(cycle.outL[15], 1.0f) << "it still sounds after";
}

TEST(PluginVst3, BothInstancesOfAMonoPluginHearTheController)
{
    const Folder folder;
    HandLoop loop;
    Told told;
    std::unique_ptr<Vst3Plugin> twice = preparedPlugin(kMono, Vst3Plugin::Mono::DualMono);
    ASSERT_NE(twice, nullptr);
    ASSERT_EQ(twice->instanceCount(), 2U);
    twice->attach(loop, told.events);
    twice->applyParam(0, 2.0f);
    Cycle cycle(16, 1.0f, 1.0f);
    cycle.run(*twice);
    twice->editorTick(); // the controller is told of the gain, and says so
    EXPECT_EQ(folder.count("component heard gain"), 2) << "the second instance did not hear the controller";
}

TEST(PluginVst3, TheControllerNeverHearsTheComponentOnTheAudioThread)
{
#if defined(__has_feature)
#if __has_feature(realtime_sanitizer)
    GTEST_SKIP() << "the tests' plugin allocates to speak from its process call, which RealtimeSanitizer stops";
#endif
#endif
    const Folder folder;
    const ScopedEnv talks("RB_TEST_VST3_TALK", "1");
    // Its first cycle on a thread of its own, as the audio thread is: what
    // the component says there is not passed on.
    std::unique_ptr<Vst3Plugin> plugin = preparedPlugin();
    ASSERT_NE(plugin, nullptr);
    Cycle cycle(16, 1.0f, 1.0f);
    std::thread audio([&] { cycle.run(*plugin); });
    audio.join();
    EXPECT_FALSE(folder.wasTold("cycle")) << "the controller heard the component on the audio thread";
    EXPECT_FLOAT_EQ(cycle.outL[15], 1.0f) << "and the sound went on";

    // On the host's own thread, it is.
    std::unique_ptr<Vst3Plugin> here = preparedPlugin();
    ASSERT_NE(here, nullptr);
    cycle.run(*here);
    EXPECT_TRUE(folder.wasTold("cycle"));
}

TEST(PluginVst3, AnEmptyStateIsNeverGivenToEitherHalf)
{
    const Folder folder;
    const ScopedEnv empty("RB_TEST_VST3_EMPTY_STATE", "1");
    std::unique_ptr<Vst3Plugin> plugin = preparedPlugin();
    ASSERT_NE(plugin, nullptr);
    std::unique_ptr<Vst3Plugin> fresh = preparedPlugin(kMono);
    ASSERT_NE(fresh, nullptr);
    std::unique_ptr<Vst3Plugin> again = preparedPlugin();
    ASSERT_NE(again, nullptr);
    EXPECT_TRUE(again->loadStateText(plugin->stateText()));
    EXPECT_FALSE(folder.wasTold("given an empty state"));
}

TEST(PluginVst3, APluginThatWillNotStartLetsGoOfItsSecondInstance)
{
    const Folder folder;
    const ScopedEnv refuses("RB_TEST_VST3_REFUSE_SETUP", "1");
    const std::shared_ptr<Vst3Module> module = Vst3Module::open(bundle());
    ASSERT_NE(module, nullptr);
    std::unique_ptr<Vst3Plugin> plugin = Vst3Plugin::make(module, kMono, Vst3Plugin::Mono::DualMono);
    ASSERT_NE(plugin, nullptr);
    dsp::PrepareSpec spec;
    spec.sampleRate = 48000.0;
    spec.inputChannels = 2;
    spec.outputChannels = 2;
    EXPECT_FALSE(plugin->prepare(spec).has_value());
    EXPECT_EQ(plugin->instanceCount(), 1U);
}

TEST(PluginVst3, AViewThatWouldNotBeAttachedIsLetGoOfNotRemoved)
{
    const Folder folder;
    const StandIns parents;
    const ScopedEnv refuses("RB_TEST_VST3_REFUSE_ATTACH", "1");
    HandLoop loop;
    Told told;
    std::unique_ptr<Vst3Plugin> plugin = preparedPlugin();
    ASSERT_NE(plugin, nullptr);
    plugin->attach(loop, told.events);
    EXPECT_EQ(plugin->openUi(), Editable::Opened::Failed);
    EXPECT_TRUE(folder.wasTold("refused to attach"));
    EXPECT_FALSE(folder.wasTold("removed"));
    EXPECT_FALSE(plugin->uiOpen());
    EXPECT_EQ(parents.open, nullptr) << "the window it was to go in is gone";
    EXPECT_EQ(loop.watches(), 1U);
}

TEST(PluginVst3, APresetNeverGoesOverAnotherOrComesFromElsewhere)
{
    const Folder folder;
    fs::create_directories(folder.presets());
    std::ofstream(folder.presets() / "Other.vstpreset", std::ios::binary) << handMadePreset(kMono, 0.5f);
    HandLoop loop;
    Told told;
    std::unique_ptr<Vst3Plugin> plugin = preparedPlugin();
    ASSERT_NE(plugin, nullptr);
    plugin->attach(loop, told.events);

    EXPECT_FALSE(plugin->savePreset("Other").has_value()) << "another plugin's preset of that name";
    EXPECT_EQ(vst3PresetClassOf((folder.presets() / "Other.vstpreset").string()), std::optional<std::string>{kMono});
    // **Names that are not one file's** are made one.
    EXPECT_EQ(plugin->savePreset(".."), std::optional<std::string>{(folder.presets() / "_.vstpreset").string()});
    EXPECT_EQ(plugin->savePreset("Bell\x07"),
              std::optional<std::string>{(folder.presets() / "Bell_.vstpreset").string()});
    // The same name again replaces its own; another that made the same
    // file name does not.
    ASSERT_TRUE(plugin->savePreset("Mine_too").has_value());
    EXPECT_TRUE(plugin->savePreset("Mine_too").has_value());
    EXPECT_FALSE(plugin->savePreset("Mine/too").has_value());
    for (const fs::directory_entry &entry : fs::directory_iterator(folder.presets())) {
        EXPECT_FALSE(entry.path().filename().string().starts_with(".")) << "a part written was left behind";
    }

    // **A preset is only one where this plugin's are.**
    const fs::path elsewhere = folder.path() / "elsewhere.vstpreset";
    std::ofstream(elsewhere, std::ios::binary) << handMadePreset(kStereo, 0.75f);
    EXPECT_FALSE(plugin->loadPreset(elsewhere.string()));
    EXPECT_FALSE(
        plugin->loadPreset((folder.presets() / ".." / ".." / ".." / ".." / ".." / "elsewhere.vstpreset").string()));
    fs::create_symlink(elsewhere, folder.presets() / "Linked.vstpreset");
    EXPECT_FALSE(plugin->loadPreset((folder.presets() / "Linked.vstpreset").string())) << "a link out of the folder";
    EXPECT_TRUE(plugin->loadPreset((folder.presets() / "Mine_too.vstpreset").string()));
}
