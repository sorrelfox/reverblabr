// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Sessions with plugins in them (ADR-0014): cut into segments around
// each plugin's host, and judged from outside by two devices of the test's
// own — one the session records from, one it plays to.
//
// **What the input device plays says which cycle it is**: every sample of a
// cycle is that cycle's number, scaled small. The output device knows which
// cycle it is running in, so it can tell whether what reached it went
// through the session's segments and hosts in the same cycle, a cycle late,
// or not at all, and at what gain.

#include "blocks/builtinblocks.h"
#include "blocks/inputdevice.h"
#include "blocks/outputdevice.h"
#include "blocks/pluginblock.h"
#include "core/pluginscanner.h"
#include "core/sessionruntime.h"
#include "core/sessionstore.h"
#include "dsp/blockregistry.h"
#include "plugin/formats.h"
#include "pw/pluginhostprocess.h"
#include "sessionharness.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <gtest/gtest.h>
#include <pipewire/filter.h>
#include <pipewire/pipewire.h>

#include <atomic>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace rb;
using namespace rb::core;
using namespace rb::pwtest;
using namespace Qt::StringLiterals;
using namespace std::chrono_literals;

namespace
{

constexpr std::uint64_t kCodes = 4096;
constexpr float kCodeScale = 8192.0f;
constexpr const char *kInput = "rbtest.plugin.input";
constexpr const char *kOutput = "rbtest.plugin.output";

[[nodiscard]] std::uint64_t cycleOf(const spa_io_position *position)
{
    return position->clock.position / std::max<std::uint64_t>(position->clock.duration, 1);
}

/// What a test device keeps with each port: the port's own end of its
/// buffers, as the filter hands it over, so the source can see whether what
/// it wrote last cycle has been passed on.
struct Port {
    std::atomic<spa_io_buffers *> io{nullptr};
};

/// An input device that plays each cycle's number, and an output device
/// that reads it back — on a thread loop of their own, in a client of their
/// own, so the session takes them for anybody's devices.
class Devices
{
public:
    Devices()
    {
        pw_init(nullptr, nullptr);
        m_loop = pw_thread_loop_new("rbtest-plugin-devices", nullptr);
        pw_thread_loop_lock(m_loop);
        make(kInput, "Audio/Source", false);
        make(kOutput, "Audio/Sink", true);
        pw_thread_loop_start(m_loop);
        pw_thread_loop_unlock(m_loop);
    }

    ~Devices()
    {
        pw_thread_loop_lock(m_loop);
        for (const std::unique_ptr<Node> &node : m_nodes) {
            pw_filter_destroy(node->filter);
        }
        pw_thread_loop_unlock(m_loop);
        pw_thread_loop_stop(m_loop);
        pw_thread_loop_destroy(m_loop);
    }

    Devices(const Devices &) = delete;
    Devices &operator=(const Devices &) = delete;

    /// How many times the input device found one of its outputs still
    /// holding what it wrote the cycle before.
    [[nodiscard]] std::uint64_t sourceUnread() const
    {
        return m_sourceUnread.load();
    }

    /// The gain the session is expected to apply, to read the code back.
    void expectGain(float gain)
    {
        m_expectedGain = gain;
    }

    /// **What the output device heard, counted since it started.** The
    /// counts only go up and only the audio thread moves them, so a window
    /// is one snapshot taken from another, and what was heard in it is the
    /// sum of what it was heard as, each read once: nothing is reset under
    /// a cycle that is counting.
    struct Heard {
        std::uint64_t same = 0;
        std::uint64_t late = 0;
        std::uint64_t other = 0;
        std::uint64_t silent = 0;
        /// Cycles not judged, just after one went missing (see hear()).
        std::uint64_t skipped = 0;

        [[nodiscard]] std::uint64_t heard() const
        {
            return same + late + other;
        }

        [[nodiscard]] Heard operator-(const Heard &before) const
        {
            return Heard{same - before.same,
                         late - before.late,
                         other - before.other,
                         silent - before.silent,
                         skipped - before.skipped};
        }
    };

    [[nodiscard]] Heard counts() const
    {
        return Heard{m_same.load(), m_late.load(), m_other.load(), m_silent.load(), m_skipped.load()};
    }

    /// Listens for \a duration and says what was heard in it.
    [[nodiscard]] Heard listen(Waiter &waiter, std::chrono::milliseconds duration)
    {
        const Heard before = counts();
        waiter.pump(duration);
        return counts() - before;
    }

    [[nodiscard]] float ratio() const
    {
        return m_ratio.load();
    }

private:
    struct Node {
        pw_filter *filter = nullptr;
        Port *left = nullptr;
        Port *right = nullptr;
        Devices *devices = nullptr;
        bool input = false;
    };

    static void onProcess(void *data, spa_io_position *position)
    {
        auto *node = static_cast<Node *>(data);
        if (position == nullptr) {
            return;
        }
        if (node->input) {
            node->devices->hear(*node, position);
        } else {
            node->devices->play(*node, position);
        }
    }

    static void onIoChanged(void *, void *portData, std::uint32_t id, void *area, std::uint32_t size)
    {
        if (portData != nullptr && id == SPA_IO_Buffers) {
            static_cast<Port *>(portData)->io.store(
                area != nullptr && size >= sizeof(spa_io_buffers) ? static_cast<spa_io_buffers *>(area) : nullptr);
        }
    }

    static pw_filter_events makeEvents()
    {
        pw_filter_events events{};
        events.version = PW_VERSION_FILTER_EVENTS;
        events.process = &Devices::onProcess;
        events.io_changed = &Devices::onIoChanged;
        return events;
    }

    void make(const char *name, const char *mediaClass, bool input)
    {
        static const pw_filter_events events = makeEvents();
        auto node = std::make_unique<Node>();
        node->devices = this;
        node->input = input;
        node->filter = pw_filter_new_simple(pw_thread_loop_get_loop(m_loop),
                                            name,
                                            pw_properties_new(PW_KEY_MEDIA_TYPE,
                                                              "Audio",
                                                              PW_KEY_MEDIA_CATEGORY,
                                                              input ? "Playback" : "Capture",
                                                              PW_KEY_MEDIA_CLASS,
                                                              mediaClass,
                                                              PW_KEY_NODE_NAME,
                                                              name,
                                                              PW_KEY_NODE_DESCRIPTION,
                                                              name,
                                                              PW_KEY_NODE_AUTOCONNECT,
                                                              "false",
                                                              "node.always-process",
                                                              "true",
                                                              nullptr),
                                            &events,
                                            node.get());
        const pw_direction direction = input ? PW_DIRECTION_INPUT : PW_DIRECTION_OUTPUT;
        for (const char *channel : {"FL", "FR"}) {
            const std::string portName = std::string(input ? "playback_" : "capture_") + channel;
            auto *port = static_cast<Port *>(pw_filter_add_port(node->filter,
                                                                direction,
                                                                PW_FILTER_PORT_FLAG_MAP_BUFFERS,
                                                                sizeof(Port),
                                                                pw_properties_new(PW_KEY_FORMAT_DSP,
                                                                                  "32 bit float mono audio",
                                                                                  PW_KEY_PORT_NAME,
                                                                                  portName.c_str(),
                                                                                  PW_KEY_AUDIO_CHANNEL,
                                                                                  channel,
                                                                                  nullptr),
                                                                nullptr,
                                                                0));
            // The filter hands over zeroed memory of that size, not a Port.
            new (port) Port{};
            (std::string_view(channel) == "FL" ? node->left : node->right) = port;
        }
        pw_filter_connect(node->filter, PW_FILTER_FLAG_RT_PROCESS, nullptr, 0);
        m_nodes.push_back(std::move(node));
    }

    void play(Node &node, spa_io_position *position)
    {
        const std::uint32_t frames = position->clock.duration;
        const float value = static_cast<float>(cycleOf(position) % kCodes + 1) / kCodeScale;
        m_played.store(value);
        for (Port *port : {node.left, node.right}) {
            // **The same rule as the session's nodes**: a port still
            // holding last cycle's buffer gets no other, or this device would
            // put one side a cycle behind the other before the session saw it.
            if (const spa_io_buffers *io = port->io.load(); io != nullptr && io->status == SPA_STATUS_HAVE_DATA) {
                m_sourceUnread.fetch_add(1);
                continue;
            }
            if (auto *out = static_cast<float *>(pw_filter_get_dsp_buffer(port, frames)); out != nullptr) {
                std::fill_n(out, frames, value);
            }
        }
    }

    void hear(Node &node, spa_io_position *position)
    {
        const std::uint32_t frames = position->clock.duration;
        const auto *left = static_cast<const float *>(pw_filter_get_dsp_buffer(node.left, frames));
        const auto *right = static_cast<const float *>(pw_filter_get_dsp_buffer(node.right, frames));
        // **A cycle the machine did not run is not the session's doing**,
        // and the two after it carry what was in the delay lines when it
        // went, so they are not judged either.
        const std::uint64_t now = cycleOf(position);
        if (m_lastCycle != 0 && now != m_lastCycle + 1) {
            m_settling = 3;
        }
        m_lastCycle = now;
        if (left == nullptr || right == nullptr || left[frames - 1] == 0.0f) {
            ++m_silent;
            return;
        }
        m_ratio.store(left[frames - 1] / m_played.load());
        if (m_settling > 0) {
            --m_settling;
            ++m_skipped;
            return;
        }
        const float gain = m_expectedGain.load();
        const auto code = static_cast<std::int64_t>(std::lround(left[frames - 1] / gain * kCodeScale)) - 1;
        const auto cycle = static_cast<std::int64_t>(cycleOf(position) % kCodes);
        const std::int64_t behind = (cycle - code + static_cast<std::int64_t>(kCodes)) % kCodes;
        // The whole cycle, both sides, one value: anything else is two
        // cycles mixed, which is what a path held back by the wrong amount
        // sounds like.
        const bool whole =
            std::abs(right[0] - left[frames - 1]) < 1e-6f && std::abs(left[0] - left[frames - 1]) < 1e-6f;
        const float expected = static_cast<float>(code + 1) / kCodeScale * gain;
        if (!whole || std::abs(left[frames - 1] - expected) > 1e-5f) {
            ++m_other;
        } else if (behind == 0) {
            ++m_same;
        } else if (behind == 1) {
            ++m_late;
        } else {
            ++m_other;
        }
    }

    pw_thread_loop *m_loop = nullptr;
    std::vector<std::unique_ptr<Node>> m_nodes;
    std::atomic<float> m_played{1.0f};
    std::atomic<float> m_expectedGain{1.0f};
    std::atomic<std::uint64_t> m_same{0};
    std::atomic<std::uint64_t> m_late{0};
    std::atomic<std::uint64_t> m_other{0};
    std::atomic<std::uint64_t> m_silent{0};
    std::atomic<std::uint64_t> m_skipped{0};
    std::atomic<float> m_ratio{0.0f};
    std::atomic<std::uint64_t> m_sourceUnread{0};
    /// The audio thread's own, for noticing a cycle that went missing.
    std::uint64_t m_lastCycle = 0;
    int m_settling = 0;
};

[[nodiscard]] dsp::BlockRegistry pluginRegistry()
{
    dsp::BlockRegistry registry;
    EXPECT_TRUE(blocks::registerBuiltinBlocks(registry));
    return registry;
}

[[nodiscard]] QString typeOf(std::string_view type)
{
    return QString::fromUtf8(type.data(), static_cast<qsizetype>(type.size()));
}

/// A session of an input device, an output device and whatever the test
/// puts between them.
class SessionBuilder
{
public:
    SessionBuilder()
    {
        m_session.id = QUuid::createUuid();
        m_session.name = u"Plugins"_s;
        BlockInstance input;
        input.id = u"in"_s;
        input.type = typeOf(blocks::InputDevice::kTypeId);
        input.params = QJsonObject{{"device"_L1, QString::fromLatin1(kInput)}};
        BlockInstance output;
        output.id = u"out"_s;
        output.type = typeOf(blocks::OutputDevice::kTypeId);
        output.params = QJsonObject{{"device"_L1, QString::fromLatin1(kOutput)}};
        m_session.blocks = {input, output};
    }

    /// An LV2 plugin block, set to \a uri with \a params.
    SessionBuilder &lv2(const QString &id, const QString &uri, QJsonObject params = {})
    {
        BlockInstance block;
        block.id = id;
        block.type = typeOf(blocks::PluginBlock::kLv2TypeId);
        params.insert("plugin"_L1, uri);
        block.params = params;
        m_session.blocks.append(block);
        return *this;
    }

    SessionBuilder &ladspa(const QString &id, const QString &plugin, QJsonObject params = {})
    {
        BlockInstance block;
        block.id = id;
        block.type = typeOf(blocks::PluginBlock::kLadspaTypeId);
        params.insert("plugin"_L1, plugin);
        block.params = params;
        m_session.blocks.append(block);
        return *this;
    }

    SessionBuilder &clap(const QString &id, const QString &pluginId, QJsonObject params = {})
    {
        BlockInstance block;
        block.id = id;
        block.type = typeOf(blocks::PluginBlock::kClapTypeId);
        params.insert("plugin"_L1, pluginId);
        block.params = params;
        m_session.blocks.append(block);
        return *this;
    }

    SessionBuilder &plugin(const QString &id, double gain, int latency = 0)
    {
        BlockInstance block;
        block.id = id;
        block.type = typeOf(blocks::PluginBlock::kTypeId);
        block.params = QJsonObject{{"gain"_L1, gain}, {"builtinLatency"_L1, latency}};
        m_session.blocks.append(block);
        return *this;
    }

    /// A stereo wire from one block to another.
    SessionBuilder &wire(const QString &from, const QString &to)
    {
        for (std::uint32_t channel = 0; channel < 2; ++channel) {
            Wire made;
            made.fromBlock = from;
            made.fromPort = channel;
            made.toBlock = to;
            made.toPort = channel;
            m_session.wires.append(made);
        }
        return *this;
    }

    [[nodiscard]] const Session &session() const
    {
        return m_session;
    }

private:
    Session m_session;
};

/// The latency the runtime says a block has.
[[nodiscard]] std::optional<std::uint32_t> latencyOf(const SessionRuntime &runtime, const QString &blockId)
{
    for (const SessionRuntime::MeteredBlock &metered : runtime.meters()) {
        if (metered.id == blockId) {
            return metered.latencySamples;
        }
    }
    return std::nullopt;
}

class PluginSessions : public SessionTest
{
protected:
    void SetUp() override
    {
        SessionTest::SetUp();
        qputenv("REVERBLABR_PLUGIN_HOST", RB_PLUGIN_HOST);
        // After the application the fixture makes: a connection lives on
        // its event loop.
        m_pump = std::make_unique<Pump>();
        m_sound = std::make_unique<Devices>();
    }

    void TearDown() override
    {
        forceQuantum(0);
        m_sound.reset();
        m_pump.reset();
        SessionTest::TearDown();
    }

    static void forceQuantum(int quantum)
    {
        (void)runCommand({"pw-metadata", "-n", "settings", "0", "clock.force-quantum", std::to_string(quantum)});
    }

    /// Starts \a session and waits until sound comes through it.
    void run(SessionRuntime &runtime, const Session &session)
    {
        ASSERT_TRUE(m_pump->waitUntilReady());
        ASSERT_TRUE(m_pump->awaitNode(QString::fromLatin1(kInput)).has_value());
        ASSERT_TRUE(m_pump->awaitNode(QString::fromLatin1(kOutput)).has_value());
        const Devices::Heard before = m_sound->counts();
        ASSERT_TRUE(runtime.start(session).has_value());
        ASSERT_TRUE(m_pump->pumpUntil([&] { return (m_sound->counts() - before).heard() > 20; }))
            << "nothing came through the session\n"
            << dumpGraph();
    }

    std::unique_ptr<Pump> m_pump;
    std::unique_ptr<Devices> m_sound;
    dsp::BlockRegistry m_registry = pluginRegistry();
};

} // namespace

TEST_F(PluginSessions, APluginBetweenTwoDevicesIsHeardInTheSameCycleAtEveryQuantum)
{
    SessionRuntime runtime(m_pump->connection(), m_pump->mirror(), m_registry);
    const Session session = SessionBuilder().plugin(u"p"_s, 0.5).wire(u"in"_s, u"p"_s).wire(u"p"_s, u"out"_s).session();
    run(runtime, session);
    EXPECT_EQ(runtime.nodes().size(), 2U) << "one segment either side of the plugin";
    ASSERT_NE(runtime.pluginHost(u"p"_s), nullptr);

    m_sound->expectGain(0.5f);
    for (const int quantum : {64, 256, 1024}) {
        forceQuantum(quantum);
        m_pump->pump(500ms);
        const Devices::Heard heard = m_sound->listen(*m_pump, 1s);
        std::printf(
            "[measure] input -> segment -> plugin host -> segment -> output at quantum %d: %llu heard, "
            "%llu in the same cycle at gain 0.5, %llu a cycle late, %llu otherwise, %llu silent\n",
            quantum,
            static_cast<unsigned long long>(heard.heard()),
            static_cast<unsigned long long>(heard.same),
            static_cast<unsigned long long>(heard.late),
            static_cast<unsigned long long>(heard.other),
            static_cast<unsigned long long>(heard.silent));
        EXPECT_GT(heard.heard(), 48000U / static_cast<unsigned>(quantum) / 2) << "at quantum " << quantum;
        EXPECT_EQ(heard.same, heard.heard()) << "at quantum " << quantum;
        EXPECT_EQ(heard.silent, 0U) << "at quantum " << quantum;
    }

    // A knob on the plugin block goes to the host, and is heard.
    ASSERT_TRUE(runtime.setParam(u"p"_s, u"gain"_s, QJsonValue(2.0)).has_value());
    EXPECT_TRUE(m_pump->pumpUntil([this] { return std::abs(m_sound->ratio() - 2.0f) < 1e-4f; }, 2s));
    runtime.stop();
}

// **Both channels in the same cycle, through quantum change after quantum
// change**. Each change makes PipeWire give every port its buffers
// again, and in that there can be a cycle in which a port writes and the
// server does not pass the buffer on. A node that queued its next buffer
// behind the one still held put that channel a cycle behind the other for as
// long as the link lasted; soaked, that was about one change in twenty-five.
// Every window after every change has to be heard whole: both sides one
// value, in the same cycle.
TEST_F(PluginSessions, NeitherSideFallsACycleBehindTheOtherThroughQuantumChanges)
{
    SessionRuntime runtime(m_pump->connection(), m_pump->mirror(), m_registry);
    const Session session = SessionBuilder().plugin(u"p"_s, 0.5).wire(u"in"_s, u"p"_s).wire(u"p"_s, u"out"_s).session();
    run(runtime, session);
    m_sound->expectGain(0.5f);

    constexpr int kChanges = 10;
    std::uint64_t heard = 0;
    std::uint64_t whole = 0;
    for (int change = 0; change < kChanges; ++change) {
        const int quantum = change % 2 == 0 ? 1024 : 256;
        forceQuantum(quantum);
        m_pump->pump(250ms);
        const Devices::Heard window = m_sound->listen(*m_pump, 300ms);
        heard += window.heard();
        whole += window.same;
        EXPECT_EQ(window.same, window.heard()) << "change " << change << " to quantum " << quantum << ": "
                                               << window.late << " a cycle late, " << window.other << " not whole";
    }
    std::uint64_t unread = 0;
    for (const pw::SessionNode *node : runtime.nodes()) {
        unread += node->telemetry().unreadOutputs;
    }
    std::printf(
        "[measure] %d quantum changes with a plugin between two devices: %llu heard, %llu whole and in the "
        "same cycle; the segments met an output still unread %llu times, the input device %llu times\n",
        kChanges,
        static_cast<unsigned long long>(heard),
        static_cast<unsigned long long>(whole),
        static_cast<unsigned long long>(unread),
        static_cast<unsigned long long>(m_sound->sourceUnread()));
    EXPECT_GT(heard, 200U);
    runtime.stop();
}

TEST_F(PluginSessions, TwoPluginsInSeriesAreThreeSegmentsAndOneCycle)
{
    SessionRuntime runtime(m_pump->connection(), m_pump->mirror(), m_registry);
    const Session session = SessionBuilder()
                                .plugin(u"p1"_s, 0.5)
                                .plugin(u"p2"_s, 0.5)
                                .wire(u"in"_s, u"p1"_s)
                                .wire(u"p1"_s, u"p2"_s)
                                .wire(u"p2"_s, u"out"_s)
                                .session();
    run(runtime, session);
    EXPECT_EQ(runtime.nodes().size(), 3U);

    forceQuantum(256);
    m_sound->expectGain(0.25f);
    m_pump->pump(500ms);
    const Devices::Heard heard = m_sound->listen(*m_pump, 1s);
    std::printf("[measure] two plugins in series at quantum 256: %llu heard, %llu in the same cycle at gain 0.25\n",
                static_cast<unsigned long long>(heard.heard()),
                static_cast<unsigned long long>(heard.same));
    EXPECT_GT(heard.heard(), 90U);
    EXPECT_EQ(heard.same, heard.heard());
    runtime.stop();
}

TEST_F(PluginSessions, TwoPluginsSideBySideAreSummedInTheSameCycle)
{
    SessionRuntime runtime(m_pump->connection(), m_pump->mirror(), m_registry);
    const Session session = SessionBuilder()
                                .plugin(u"p1"_s, 0.25)
                                .plugin(u"p2"_s, 0.5)
                                .wire(u"in"_s, u"p1"_s)
                                .wire(u"in"_s, u"p2"_s)
                                .wire(u"p1"_s, u"out"_s)
                                .wire(u"p2"_s, u"out"_s)
                                .session();
    run(runtime, session);
    EXPECT_EQ(runtime.nodes().size(), 2U);

    forceQuantum(256);
    m_sound->expectGain(0.75f);
    m_pump->pump(500ms);
    const Devices::Heard heard = m_sound->listen(*m_pump, 1s);
    std::printf("[measure] two plugins side by side at quantum 256: %llu heard, %llu in the same cycle at gain 0.75\n",
                static_cast<unsigned long long>(heard.heard()),
                static_cast<unsigned long long>(heard.same));
    EXPECT_GT(heard.heard(), 90U);
    EXPECT_EQ(heard.same, heard.heard());
    runtime.stop();
}

TEST_F(PluginSessions, APluginsLatencyHoldsThePathAroundItBackByAsMuch)
{
    // in -> p (gain 1, 256 samples late) -> out, and in -> out straight.
    // Lined up, the two are one cycle late and twice as loud; not lined up,
    // every cycle is two cycles' numbers added.
    SessionRuntime runtime(m_pump->connection(), m_pump->mirror(), m_registry);
    const Session session = SessionBuilder()
                                .plugin(u"p"_s, 1.0, 256)
                                .wire(u"in"_s, u"p"_s)
                                .wire(u"p"_s, u"out"_s)
                                .wire(u"in"_s, u"out"_s)
                                .session();
    run(runtime, session);
    ASSERT_TRUE(m_pump->pumpUntil([&] { return latencyOf(runtime, u"p"_s) == 256U; }))
        << "the plugin's latency never reached its block";
    EXPECT_TRUE(m_pump->pumpUntil([&] { return runtime.latencySamples() == 256U; }))
        << "the session says it is " << runtime.latencySamples() << " samples late";

    forceQuantum(256);
    m_sound->expectGain(2.0f);
    m_pump->pump(500ms);
    const Devices::Heard heard = m_sound->listen(*m_pump, 1s);
    std::printf(
        "[measure] a plugin 256 samples late beside a path around it, at quantum 256: %llu heard, %llu one "
        "cycle late at gain 2, %llu otherwise\n",
        static_cast<unsigned long long>(heard.heard()),
        static_cast<unsigned long long>(heard.late),
        static_cast<unsigned long long>(heard.other));
    EXPECT_GT(heard.heard(), 90U);
    EXPECT_EQ(heard.late, heard.heard());
    runtime.stop();
}

TEST_F(PluginSessions, APluginsStateIsSavedAndPutBack)
{
    const dsp::BlockRegistry &registry = m_registry;
    Session session = SessionBuilder().plugin(u"p"_s, 1.0).wire(u"in"_s, u"p"_s).wire(u"p"_s, u"out"_s).session();
    {
        SessionRuntime runtime(m_pump->connection(), m_pump->mirror(), registry);
        run(runtime, session);
        ASSERT_TRUE(runtime.setParam(u"p"_s, u"gain"_s, QJsonValue(0.25)).has_value());
        ASSERT_TRUE(m_pump->pumpUntil([this] { return std::abs(m_sound->ratio() - 0.25f) < 1e-4f; }, 2s));

        // What saving does: the blocks' state into the session, and the
        // session into its file's JSON.
        const QHash<QString, QJsonObject> state = runtime.blockState();
        ASSERT_TRUE(state.contains(u"p"_s));
        EXPECT_TRUE(state.value(u"p"_s).value("state"_L1).toString().startsWith(u"gain=0.25"_s))
            << "the host's state was not asked for: " << state.value(u"p"_s).value("state"_L1).toString().toStdString();
        for (BlockInstance &block : session.blocks) {
            block.params = state.value(block.id, block.params);
        }
        runtime.stop();
    }

    // The state, and not the knob, is what goes back in: the knob is set
    // back to 1 in the file, and the plugin still plays at a quarter.
    std::expected<Session, SessionError> opened = sessionFromJson(sessionToJson(session));
    ASSERT_TRUE(opened.has_value());
    for (BlockInstance &block : opened->blocks) {
        if (block.id == u"p"_s) {
            block.params.insert("gain"_L1, 1.0);
        }
    }
    SessionRuntime reopened(m_pump->connection(), m_pump->mirror(), registry);
    run(reopened, *opened);
    EXPECT_TRUE(m_pump->pumpUntil([this] { return std::abs(m_sound->ratio() - 0.25f) < 1e-4f; }, 2s))
        << "heard at " << m_sound->ratio();
    reopened.stop();
}

TEST_F(PluginSessions, APluginSwitchedOffIsNotHostedAndPassesTheSoundThrough)
{
    SessionRuntime runtime(m_pump->connection(), m_pump->mirror(), m_registry);
    Session session = SessionBuilder().plugin(u"p"_s, 0.5).wire(u"in"_s, u"p"_s).wire(u"p"_s, u"out"_s).session();
    run(runtime, session);
    ASSERT_NE(runtime.pluginHost(u"p"_s), nullptr);
    EXPECT_TRUE(m_pump->pumpUntil([this] { return std::abs(m_sound->ratio() - 0.5f) < 1e-4f; }, 2s));

    for (BlockInstance &block : session.blocks) {
        if (block.id == u"p"_s) {
            block.enabled = false;
        }
    }
    ASSERT_TRUE(runtime.update(session).has_value());
    EXPECT_EQ(runtime.pluginHost(u"p"_s), nullptr);
    EXPECT_EQ(runtime.nodes().size(), 1U);
    EXPECT_TRUE(m_pump->pumpUntil([this] { return std::abs(m_sound->ratio() - 1.0f) < 1e-4f; }, 2s))
        << "heard at " << m_sound->ratio();
    runtime.stop();
}

namespace
{

/// How long after \a from the output device first hears the input at \a
/// gain, in milliseconds, or a negative number if it has not within a
/// second.
[[nodiscard]] double heardAtGainAfter(Waiter &waiter, const Devices &sound, float gain, const QElapsedTimer &from)
{
    if (!waiter.pumpUntil([&] { return std::abs(sound.ratio() - gain) < 1e-4f; }, 1s)) {
        return -1.0;
    }
    return static_cast<double>(from.nsecsElapsed()) / 1e6;
}

[[nodiscard]] bool processGone(int pid)
{
    return pid > 0 && ::kill(pid, 0) != 0;
}

} // namespace

TEST_F(PluginSessions, AKilledHostIsLinkedAroundWithin100MsAndTheSessionRunsOn)
{
    SessionRuntime runtime(m_pump->connection(), m_pump->mirror(), m_registry);
    QStringList failures;
    QObject::connect(&runtime, &SessionRuntime::pluginFailed, [&](const QString &blockId, const QString &reason) {
        failures.append(blockId + u':' + reason);
    });
    const Session session = SessionBuilder().plugin(u"p"_s, 0.5).wire(u"in"_s, u"p"_s).wire(u"p"_s, u"out"_s).session();
    run(runtime, session);
    forceQuantum(256);
    // Let the new quantum settle: a host stopped while PipeWire changes it
    // can hold the whole graph still (see the watchdog's longest stillness).
    m_pump->pump(300ms);
    ASSERT_TRUE(m_pump->pumpUntil([this] { return std::abs(m_sound->ratio() - 0.5f) < 1e-4f; }, 2s));
    EXPECT_EQ(runtime.pluginHealth(u"p"_s), PluginHealth::Running);

    QElapsedTimer killed;
    killed.start();
    ASSERT_TRUE(signalProcess(runtime.pluginHost(u"p"_s)->pid(), SIGKILL));
    // Linked around: the input at its own level, the plugin's gain gone.
    const double bypassed = heardAtGainAfter(*m_pump, *m_sound, 1.0f, killed);
    std::printf("[measure] killed plugin host linked around, heard again %.1f ms after the kill\n", bypassed);
    EXPECT_GE(bypassed, 0.0) << "never heard again: " << failures.join(u',').toStdString() << " health "
                             << static_cast<int>(runtime.pluginHealth(u"p"_s)) << "\n"
                             << dumpGraph();
    EXPECT_LT(bypassed, 100.0);
    EXPECT_EQ(failures, QStringList{u"p:crashed"_s});
    EXPECT_EQ(runtime.pluginHealth(u"p"_s), PluginHealth::Crashed);

    // The session runs on, whole, and the host is not started again by
    // itself: it had not been running for the ten seconds that takes.
    m_sound->expectGain(1.0f);
    m_pump->pump(300ms);
    const Devices::Heard heard = m_sound->listen(*m_pump, 1s);
    EXPECT_GT(heard.heard(), 90U);
    EXPECT_EQ(heard.same, heard.heard());
    EXPECT_EQ(runtime.pluginHealth(u"p"_s), PluginHealth::Crashed);
    runtime.stop();
}

TEST_F(PluginSessions, AStoppedHostIsLinkedAroundWithin100MsAndKilled)
{
    SessionRuntime runtime(m_pump->connection(), m_pump->mirror(), m_registry);
    const Session session = SessionBuilder().plugin(u"p"_s, 0.5).wire(u"in"_s, u"p"_s).wire(u"p"_s, u"out"_s).session();
    run(runtime, session);
    forceQuantum(256);
    // Let the new quantum settle: a host stopped while PipeWire changes it
    // can hold the whole graph still (see the watchdog's longest stillness).
    m_pump->pump(300ms);
    ASSERT_TRUE(m_pump->pumpUntil([this] { return std::abs(m_sound->ratio() - 0.5f) < 1e-4f; }, 2s));
    const int pid = runtime.pluginHost(u"p"_s)->pid();

    QElapsedTimer stopped;
    stopped.start();
    ASSERT_TRUE(signalProcess(pid, SIGSTOP));
    const double bypassed = heardAtGainAfter(*m_pump, *m_sound, 1.0f, stopped);
    std::printf("[measure] stopped plugin host linked around, heard again %.1f ms after it stopped\n", bypassed);
    EXPECT_GE(bypassed, 0.0) << "never heard again";
    EXPECT_LT(bypassed, 100.0);
    EXPECT_EQ(runtime.pluginHealth(u"p"_s), PluginHealth::Hung);
    EXPECT_TRUE(m_pump->pumpUntil([pid] { return processGone(pid); }, 2s)) << "the stopped host was not killed";
    runtime.stop();
}

TEST_F(PluginSessions, RestartBringsThePluginBackWithItsStateAndWhatWasSetSince)
{
    SessionRuntime runtime(m_pump->connection(), m_pump->mirror(), m_registry);
    const Session session = SessionBuilder().plugin(u"p"_s, 1.0).wire(u"in"_s, u"p"_s).wire(u"p"_s, u"out"_s).session();
    run(runtime, session);
    forceQuantum(256);

    // A state taken at a quarter, then the knob turned to a half after it.
    ASSERT_TRUE(runtime.setParam(u"p"_s, u"gain"_s, QJsonValue(0.25)).has_value());
    ASSERT_TRUE(m_pump->pumpUntil([this] { return std::abs(m_sound->ratio() - 0.25f) < 1e-4f; }, 2s));
    ASSERT_TRUE(runtime.blockState().value(u"p"_s).value("state"_L1).toString().startsWith(u"gain=0.25"_s));
    ASSERT_TRUE(runtime.setParam(u"p"_s, u"gain"_s, QJsonValue(0.5)).has_value());
    ASSERT_TRUE(m_pump->pumpUntil([this] { return std::abs(m_sound->ratio() - 0.5f) < 1e-4f; }, 2s));

    ASSERT_TRUE(signalProcess(runtime.pluginHost(u"p"_s)->pid(), SIGKILL));
    ASSERT_TRUE(m_pump->pumpUntil([this] { return std::abs(m_sound->ratio() - 1.0f) < 1e-4f; }, 1s));
    ASSERT_EQ(runtime.pluginHealth(u"p"_s), PluginHealth::Crashed);

    ASSERT_TRUE(runtime.restartPlugin(u"p"_s).has_value());
    EXPECT_TRUE(m_pump->pumpUntil([&] { return runtime.pluginHealth(u"p"_s) == PluginHealth::Running; }, 5s));
    // The state went in, and the half set after it went after it.
    EXPECT_TRUE(m_pump->pumpUntil([this] { return std::abs(m_sound->ratio() - 0.5f) < 1e-4f; }, 2s))
        << "heard at " << m_sound->ratio();
    // **The gain to judge by, some cycles before the window opens**: the
    // output device reads it at the start of a cycle and counts at the end,
    // so a cycle it was judging when the gain changed is judged by the old
    // one, and counted in a window that opened meanwhile.
    m_sound->expectGain(0.5f);
    m_pump->pump(50ms);
    const Devices::Heard heard = m_sound->listen(*m_pump, 500ms);
    EXPECT_EQ(heard.same, heard.heard()) << "back in, in the same cycle";

    EXPECT_EQ(runtime.restartPlugin(u"in"_s).error(), u"notAHostedPlugin"_s);
    EXPECT_EQ(runtime.restartPlugin(u"nothing"_s).error(), u"blockNotFound"_s);
    runtime.stop();
    EXPECT_EQ(runtime.restartPlugin(u"p"_s).error(), u"NotRunning"_s);
}

TEST_F(PluginSessions, AHostThatHadRunLongEnoughIsStartedAgainByItself)
{
    SessionRuntime runtime(m_pump->connection(), m_pump->mirror(), m_registry);
    runtime.setPluginRestartWindow(0ms);
    const Session session = SessionBuilder().plugin(u"p"_s, 0.5).wire(u"in"_s, u"p"_s).wire(u"p"_s, u"out"_s).session();
    run(runtime, session);
    ASSERT_TRUE(m_pump->pumpUntil([this] { return std::abs(m_sound->ratio() - 0.5f) < 1e-4f; }, 2s));
    const int pid = runtime.pluginHost(u"p"_s)->pid();

    ASSERT_TRUE(signalProcess(pid, SIGKILL));
    EXPECT_TRUE(m_pump->pumpUntil(
        [&] {
            return runtime.pluginHealth(u"p"_s) == PluginHealth::Running && runtime.pluginHost(u"p"_s)->pid() != pid;
        },
        5s));
    EXPECT_TRUE(m_pump->pumpUntil([this] { return std::abs(m_sound->ratio() - 0.5f) < 1e-4f; }, 2s))
        << "heard at " << m_sound->ratio();
    runtime.stop();
}

namespace
{

constexpr const char *kLspBundles = "/usr/lib64/lv2";
const QString kLspEqualizer = u"http://lsp-plug.in/plugins/lv2/para_equalizer_x8_stereo"_s;
const QString kLspCompressor = u"http://lsp-plug.in/plugins/lv2/compressor_stereo"_s;

constexpr const char *kLspClapFolder = "/usr/lib64/clap";
const QString kLspClapEqualizer = u"in.lsp-plug.para_equalizer_x8_stereo"_s;
const QString kLspClapCompressor = u"in.lsp-plug.compressor_stereo"_s;
/// LSP's CLAP settings, by the plugin's numbers for them.
const QString kClapOutputGain = u"3630838676"_s; // dB, -80 to 20
const QString kClapRatio = u"1503151474"_s; // 0 is 1:1, 1 is 100:1
const QString kClapAttackThreshold = u"1494456044"_s; // dB, -60 to 0
const QString kClapKnee = u"1536920686"_s; // dB, -24 to 0
const QString kClapSidechainMode = u"3810080779"_s; // Peak, RMS, LPF, SMA
const QString kClapStereoSplit = u"1384492962"_s;

/// **What a plugin block asks about its plugin, answered as the engine
/// answers it**: from a scan of the plugins \a variable finds in \a path,
/// into a cache of the test's own.
class PluginCatalogue
{
public:
    PluginCatalogue(const char *variable, const QByteArray &path)
        : m_variable(variable)
        , m_before(qEnvironmentVariableIsSet(variable) ? std::optional<QByteArray>(qgetenv(variable)) : std::nullopt)
        , m_clapPathBefore(qEnvironmentVariableIsSet("CLAP_PATH") ? std::optional<QByteArray>(qgetenv("CLAP_PATH"))
                                                                  : std::nullopt)
    {
        EXPECT_TRUE(m_folder.isValid());
        // **CLAP's own folders are searched whatever CLAP_PATH says**:
        // a catalogue of another format keeps them out, or it would
        // describe the machine's CLAP plugins as well.
        qputenv("CLAP_PATH", "");
        qputenv(plugin::kClapDefaultPathVariable, QDir(m_folder.path()).filePath(u"none"_s).toUtf8());
        qputenv(m_variable, path);
        m_scanner = std::make_unique<PluginScanner>(QDir(m_folder.path()).filePath(u"plugins.sqlite"_s));
        m_scanner->setHostProgram(QString::fromUtf8(RB_PLUGIN_HOST));
        m_scanner->setWatchingFolders(false);
        m_scanner->scan();
        QElapsedTimer waited;
        waited.start();
        while (m_scanner->isScanning() && waited.elapsed() < 30000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        plugin::setPluginInfoSource([this](std::string_view format, std::string_view id) {
            return m_scanner->describe(QString::fromUtf8(format.data(), static_cast<qsizetype>(format.size())),
                                       QString::fromUtf8(id.data(), static_cast<qsizetype>(id.size())));
        });
    }

    ~PluginCatalogue()
    {
        plugin::setPluginInfoSource({});
        // As it was: a folder the run was started with stays for the next.
        if (m_before) {
            qputenv(m_variable, *m_before);
        } else {
            qunsetenv(m_variable);
        }
        if (m_clapPathBefore) {
            qputenv("CLAP_PATH", *m_clapPathBefore);
        } else {
            qunsetenv("CLAP_PATH");
        }
        qunsetenv(plugin::kClapDefaultPathVariable);
    }

    PluginCatalogue(const PluginCatalogue &) = delete;
    PluginCatalogue &operator=(const PluginCatalogue &) = delete;

private:
    const char *m_variable;
    std::optional<QByteArray> m_before;
    std::optional<QByteArray> m_clapPathBefore;
    QTemporaryDir m_folder;
    std::unique_ptr<PluginScanner> m_scanner;
};

/// LSP's LV2 bundle, scanned.
class LspCatalogue : public PluginCatalogue
{
public:
    LspCatalogue()
        : PluginCatalogue("LV2_PATH", kLspBundles)
    {
    }

    [[nodiscard]] static bool installed()
    {
        return QFileInfo::exists(QString::fromLatin1(kLspBundles) + u"/lsp-plugins.lv2/manifest.ttl"_s);
    }
};

/// LSP's CLAP file, scanned: the folder it is in instead of CLAP's
/// own folders.
class LspClapCatalogue : public PluginCatalogue
{
public:
    LspClapCatalogue()
        : PluginCatalogue(plugin::kClapDefaultPathVariable, kLspClapFolder)
    {
    }

    /// Whether LSP's CLAP plugins are here, and this build can host them.
    [[nodiscard]] static bool installed()
    {
#ifdef RB_HAVE_CLAP
        return QFileInfo::exists(QString::fromLatin1(kLspClapFolder) + u"/lsp-plugins.clap"_s);
#else
        return false;
#endif
    }
};

} // namespace

TEST_F(PluginSessions, AnLv2EqualizerPlaysInASessionAndItsStateIsSavedAndPutBack)
{
    if (!LspCatalogue::installed()) {
        GTEST_SKIP() << "LSP's LV2 plugins are not installed (lsp-plugins-lv2)";
    }
    LspCatalogue catalogue;
    Session session =
        SessionBuilder().lv2(u"eq"_s, kLspEqualizer).wire(u"in"_s, u"eq"_s).wire(u"eq"_s, u"out"_s).session();
    {
        SessionRuntime runtime(m_pump->connection(), m_pump->mirror(), m_registry);
        run(runtime, session);
        forceQuantum(256);
        m_pump->pump(500ms);
        const Devices::Heard flat = m_sound->listen(*m_pump, 1s);
        std::printf(
            "[measure] LSP Parametric Equalizer x8 Stereo in a session, every band off: %llu heard, %llu in "
            "the same cycle\n",
            static_cast<unsigned long long>(flat.heard()),
            static_cast<unsigned long long>(flat.same));
        EXPECT_GT(flat.heard(), 90U);
        EXPECT_EQ(flat.same, flat.heard());

        // A setting by the plugin's own name for it, heard.
        ASSERT_TRUE(runtime.setParam(u"eq"_s, u"g_out"_s, QJsonValue(0.5)).has_value());
        ASSERT_TRUE(m_pump->pumpUntil([this] { return std::abs(m_sound->ratio() - 0.5f) < 1e-3f; }, 2s))
            << "heard at " << m_sound->ratio();

        // Saved: LV2's state, from the host, with the setting in it.
        const QHash<QString, QJsonObject> state = runtime.blockState();
        const QString saved = state.value(u"eq"_s).value("state"_L1).toString();
        EXPECT_TRUE(saved.contains(u"g_out"_s)) << saved.left(300).toStdString();
        EXPECT_EQ(state.value(u"eq"_s).value("plugin"_L1).toString(), kLspEqualizer);
        for (BlockInstance &block : session.blocks) {
            block.params = state.value(block.id, block.params);
        }
        runtime.stop();
    }
    std::expected<Session, SessionError> opened = sessionFromJson(sessionToJson(session));
    ASSERT_TRUE(opened.has_value());
    SessionRuntime reopened(m_pump->connection(), m_pump->mirror(), m_registry);
    run(reopened, *opened);
    EXPECT_TRUE(m_pump->pumpUntil([this] { return std::abs(m_sound->ratio() - 0.5f) < 1e-3f; }, 3s))
        << "heard at " << m_sound->ratio();
    reopened.stop();
}

TEST_F(PluginSessions, AnLv2CompressorCompressesWhenItIsAskedTo)
{
    if (!LspCatalogue::installed()) {
        GTEST_SKIP() << "LSP's LV2 plugins are not installed (lsp-plugins-lv2)";
    }
    LspCatalogue catalogue;
    // A ratio of one with the threshold at the top: nothing to do.
    const Session session =
        SessionBuilder()
            .lv2(u"comp"_s,
                 kLspCompressor,
                 QJsonObject{{"cr"_L1, 1.0}, {"al"_L1, 1.0}, {"kn"_L1, 1.0}, {"scm"_L1, u"0"_s}, {"ssplit"_L1, true}})
            .wire(u"in"_s, u"comp"_s)
            .wire(u"comp"_s, u"out"_s)
            .session();
    SessionRuntime runtime(m_pump->connection(), m_pump->mirror(), m_registry);
    run(runtime, session);
    forceQuantum(256);
    m_pump->pump(500ms);
    // Every kind of setting the file holds reached the block: a choice (Peak,
    // not the plugin's RMS) and a toggle as well as the numbers.
    const QJsonObject settings = runtime.blockState().value(u"comp"_s);
    EXPECT_EQ(settings.value("scm"_L1).toString(), u"0"_s);
    EXPECT_EQ(settings.value("ssplit"_L1).toBool(), true);
    EXPECT_DOUBLE_EQ(settings.value("cr"_L1).toDouble(), 1.0);
    const Devices::Heard untouched = m_sound->listen(*m_pump, 1s);
    EXPECT_GT(untouched.heard(), 90U);
    EXPECT_EQ(untouched.same, untouched.heard()) << "a ratio of one changed the sound";

    // Then a hundred to one from sixty decibels down: squashed.
    ASSERT_TRUE(runtime.setParam(u"comp"_s, u"al"_s, QJsonValue(0.001)).has_value());
    ASSERT_TRUE(runtime.setParam(u"comp"_s, u"cr"_s, QJsonValue(100.0)).has_value());
    EXPECT_TRUE(m_pump->pumpUntil([this] { return m_sound->ratio() < 0.2f; }, 3s)) << "heard at " << m_sound->ratio();
    std::printf("[measure] LSP Compressor Stereo at 100:1 from -60 dB: output over input %.4f\n",
                static_cast<double>(m_sound->ratio()));
    runtime.stop();
}

namespace
{

/// The value an LV2 state in Turtle gives the port \a symbol, or nothing.
[[nodiscard]] std::optional<double> portValue(const QString &state, const QString &symbol)
{
    const QRegularExpression pattern(u"lv2:symbol \"%1\"\\s*;\\s*pset:value\\s+([-+0-9.eE]+)"_s.arg(symbol));
    const QRegularExpressionMatch match = pattern.match(state);
    if (!match.hasMatch()) {
        return std::nullopt;
    }
    return match.captured(1).toDouble();
}

} // namespace

// **A choice in the file reaches a new host** as a switch and a number do:
// the host is given every setting the file has when there is no state, and
// a choice is a position, not a number the value is. The choices used to be
// left out, and the plugin ran on its own default.
TEST_F(PluginSessions, APluginsChoiceInTheFileReachesANewHost)
{
    if (!LspCatalogue::installed()) {
        GTEST_SKIP() << "LSP's LV2 plugins are not installed (lsp-plugins-lv2)";
    }
    LspCatalogue catalogue;
    // Peak (0) where the plugin's own default is RMS (1); a split where it
    // has none.
    const Session session = SessionBuilder()
                                .lv2(u"comp"_s, kLspCompressor, QJsonObject{{"scm"_L1, u"0"_s}, {"ssplit"_L1, true}})
                                .wire(u"in"_s, u"comp"_s)
                                .wire(u"comp"_s, u"out"_s)
                                .session();
    SessionRuntime runtime(m_pump->connection(), m_pump->mirror(), m_registry);
    run(runtime, session);
    m_pump->pump(500ms);
    // What the plugin has, from its host: LV2's state, with every port.
    const QString state = runtime.blockState().value(u"comp"_s).value("state"_L1).toString();
    ASSERT_TRUE(portValue(state, u"scm"_s).has_value()) << state.left(400).toStdString();
    EXPECT_EQ(*portValue(state, u"scm"_s), 0.0) << "the plugin runs on its own default choice";
    EXPECT_EQ(portValue(state, u"ssplit"_s).value_or(-1.0), 1.0);
    runtime.stop();
}

TEST_F(PluginSessions, AKilledLv2HostIsLinkedAroundAsAnyHostIs)
{
    if (!LspCatalogue::installed()) {
        GTEST_SKIP() << "LSP's LV2 plugins are not installed (lsp-plugins-lv2)";
    }
    LspCatalogue catalogue;
    const Session session = SessionBuilder()
                                .lv2(u"eq"_s, kLspEqualizer, QJsonObject{{"g_out"_L1, 0.5}})
                                .wire(u"in"_s, u"eq"_s)
                                .wire(u"eq"_s, u"out"_s)
                                .session();
    SessionRuntime runtime(m_pump->connection(), m_pump->mirror(), m_registry);
    run(runtime, session);
    forceQuantum(256);
    m_pump->pump(300ms);
    ASSERT_TRUE(m_pump->pumpUntil([this] { return std::abs(m_sound->ratio() - 0.5f) < 1e-3f; }, 3s))
        << "heard at " << m_sound->ratio();

    QElapsedTimer killed;
    killed.start();
    ASSERT_TRUE(signalProcess(runtime.pluginHost(u"eq"_s)->pid(), SIGKILL));
    ASSERT_TRUE(m_pump->pumpUntil([this] { return std::abs(m_sound->ratio() - 1.0f) < 1e-4f; }, 1s));
    const double bypassed = static_cast<double>(killed.nsecsElapsed()) / 1e6;
    std::printf("[measure] killed LV2 host linked around, heard again %.1f ms after the kill\n", bypassed);
    EXPECT_LT(bypassed, 100.0);
    EXPECT_EQ(runtime.pluginHealth(u"eq"_s), PluginHealth::Crashed);
    runtime.stop();
}

namespace
{

const QString kTestStereoGain = u"rb-test-ladspa.so:rb_stereo_gain"_s;
const QString kTestMonoGain = u"rb-test-ladspa.so:rb_mono_gain"_s;

/// The LADSPA plugins tests/ladspa builds, scanned.
class TestLadspaCatalogue : public PluginCatalogue
{
public:
    TestLadspaCatalogue()
        : PluginCatalogue("LADSPA_PATH", RB_TEST_LADSPA_DIR)
    {
    }
};

/// Where the LADSPA SDK's example amplifiers are, as the format looks for
/// them (LADSPA_PATH as the test was started with, or where distributions
/// put them), or nothing.
[[nodiscard]] std::optional<QByteArray> sdkFolder()
{
    for (const std::filesystem::path &folder : plugin::formatNamed("ladspa")->searchPaths()) {
        if (std::filesystem::exists(folder / "amp.so")) {
            return QByteArray::fromStdString(folder.string());
        }
    }
    return std::nullopt;
}

} // namespace

TEST_F(PluginSessions, ALadspaGainPlaysInASessionAndItsSettingIsSavedAndPutBack)
{
    TestLadspaCatalogue catalogue;
    Session session = SessionBuilder()
                          .ladspa(u"gain"_s, kTestStereoGain)
                          .wire(u"in"_s, u"gain"_s)
                          .wire(u"gain"_s, u"out"_s)
                          .session();
    {
        SessionRuntime runtime(m_pump->connection(), m_pump->mirror(), m_registry);
        run(runtime, session);
        forceQuantum(256);
        m_pump->pump(500ms);
        const Devices::Heard unity = m_sound->listen(*m_pump, 1s);
        EXPECT_GT(unity.heard(), 90U);
        EXPECT_EQ(unity.same, unity.heard()) << "not the sound as it came, at the default gain of 1";

        // A setting by the id the scan gave the plugin's port, heard.
        ASSERT_TRUE(runtime.setParam(u"gain"_s, u"gain"_s, QJsonValue(0.5)).has_value());
        ASSERT_TRUE(m_pump->pumpUntil([this] { return std::abs(m_sound->ratio() - 0.5f) < 1e-3f; }, 2s))
            << "heard at " << m_sound->ratio();

        // Saved: the plugin by its library and label, and its control
        // values, which are all a LADSPA plugin has.
        const QHash<QString, QJsonObject> state = runtime.blockState();
        EXPECT_EQ(state.value(u"gain"_s).value("plugin"_L1).toString(), kTestStereoGain);
        EXPECT_DOUBLE_EQ(state.value(u"gain"_s).value("gain"_L1).toDouble(), 0.5);
        const QString saved = state.value(u"gain"_s).value("state"_L1).toString();
        EXPECT_TRUE(saved.startsWith(u"gain=0.5"_s)) << saved.toStdString();
        for (BlockInstance &block : session.blocks) {
            block.params = state.value(block.id, block.params);
        }
        runtime.stop();
    }
    std::expected<Session, SessionError> opened = sessionFromJson(sessionToJson(session));
    ASSERT_TRUE(opened.has_value());
    SessionRuntime reopened(m_pump->connection(), m_pump->mirror(), m_registry);
    run(reopened, *opened);
    EXPECT_TRUE(m_pump->pumpUntil([this] { return std::abs(m_sound->ratio() - 0.5f) < 1e-3f; }, 3s))
        << "heard at " << m_sound->ratio();
    reopened.stop();
}

TEST_F(PluginSessions, AMonoLadspaPluginOnAStereoLaneRunsOnceASide)
{
    TestLadspaCatalogue catalogue;
    const Session session = SessionBuilder()
                                .ladspa(u"mono"_s, kTestMonoGain, QJsonObject{{"gain"_L1, 0.25}})
                                .wire(u"in"_s, u"mono"_s)
                                .wire(u"mono"_s, u"out"_s)
                                .session();
    SessionRuntime runtime(m_pump->connection(), m_pump->mirror(), m_registry);
    run(runtime, session);
    forceQuantum(256);
    EXPECT_TRUE(m_pump->pumpUntil([this] { return std::abs(m_sound->ratio() - 0.25f) < 1e-3f; }, 3s))
        << "heard at " << m_sound->ratio();
    EXPECT_EQ(runtime.blockState().value(u"mono"_s).value("monoMode"_L1).toString(), u"dualMono"_s);
    runtime.stop();
}

TEST_F(PluginSessions, AKilledLadspaHostIsLinkedAroundAsAnyHostIs)
{
    TestLadspaCatalogue catalogue;
    const Session session = SessionBuilder()
                                .ladspa(u"gain"_s, kTestStereoGain, QJsonObject{{"gain"_L1, 0.5}})
                                .wire(u"in"_s, u"gain"_s)
                                .wire(u"gain"_s, u"out"_s)
                                .session();
    SessionRuntime runtime(m_pump->connection(), m_pump->mirror(), m_registry);
    run(runtime, session);
    forceQuantum(256);
    m_pump->pump(300ms);
    ASSERT_TRUE(m_pump->pumpUntil([this] { return std::abs(m_sound->ratio() - 0.5f) < 1e-3f; }, 3s))
        << "heard at " << m_sound->ratio();

    QElapsedTimer killed;
    killed.start();
    ASSERT_TRUE(signalProcess(runtime.pluginHost(u"gain"_s)->pid(), SIGKILL));
    ASSERT_TRUE(m_pump->pumpUntil([this] { return std::abs(m_sound->ratio() - 1.0f) < 1e-4f; }, 1s));
    const double bypassed = static_cast<double>(killed.nsecsElapsed()) / 1e6;
    std::printf("[measure] killed LADSPA host linked around, heard again %.1f ms after the kill\n", bypassed);
    EXPECT_LT(bypassed, 100.0);
    EXPECT_EQ(runtime.pluginHealth(u"gain"_s), PluginHealth::Crashed);
    runtime.stop();
}

TEST_F(PluginSessions, TheLadspaSdksAmplifierPlaysInASession)
{
    // **A common LADSPA plugin**: the SDK's stereo amplifier, where the
    // machine has it (the "ladspa" package), gain "0 to ..., 1".
    const std::optional<QByteArray> folder = sdkFolder();
    if (!folder) {
        GTEST_SKIP() << "the LADSPA SDK's plugins are not installed (ladspa)";
    }
    PluginCatalogue catalogue("LADSPA_PATH", *folder);
    const Session session = SessionBuilder()
                                .ladspa(u"amp"_s, u"amp.so:amp_stereo"_s, QJsonObject{{"gain"_L1, 0.5}})
                                .wire(u"in"_s, u"amp"_s)
                                .wire(u"amp"_s, u"out"_s)
                                .session();
    SessionRuntime runtime(m_pump->connection(), m_pump->mirror(), m_registry);
    run(runtime, session);
    forceQuantum(256);
    EXPECT_TRUE(m_pump->pumpUntil([this] { return std::abs(m_sound->ratio() - 0.5f) < 1e-3f; }, 3s))
        << "heard at " << m_sound->ratio();
    ASSERT_TRUE(runtime.setParam(u"amp"_s, u"gain"_s, QJsonValue(2.0)).has_value());
    EXPECT_TRUE(m_pump->pumpUntil([this] { return std::abs(m_sound->ratio() - 2.0f) < 1e-3f; }, 2s))
        << "heard at " << m_sound->ratio();
    runtime.stop();
}

// --- CLAP ---

TEST_F(PluginSessions, AClapEqualizerPlaysInASessionAndItsStateIsSavedAndPutBack)
{
    if (!LspClapCatalogue::installed()) {
        GTEST_SKIP() << "LSP's CLAP plugins are not installed (lsp-plugins-clap), or this build has no CLAP SDK";
    }
    LspClapCatalogue catalogue;
    Session session =
        SessionBuilder().clap(u"eq"_s, kLspClapEqualizer).wire(u"in"_s, u"eq"_s).wire(u"eq"_s, u"out"_s).session();
    QString before;
    {
        SessionRuntime runtime(m_pump->connection(), m_pump->mirror(), m_registry);
        run(runtime, session);
        forceQuantum(256);
        m_pump->pump(500ms);
        const Devices::Heard flat = m_sound->listen(*m_pump, 1s);
        std::printf(
            "[measure] LSP Parametric Equalizer x8 Stereo (CLAP) in a session, every band off: %llu heard, %llu "
            "in the same cycle\n",
            static_cast<unsigned long long>(flat.heard()),
            static_cast<unsigned long long>(flat.same));
        EXPECT_GT(flat.heard(), 90U);
        EXPECT_EQ(flat.same, flat.heard());
        before = runtime.blockState().value(u"eq"_s).value("state"_L1).toString();

        // A setting by the plugin's own number for it, heard: half, in dB.
        ASSERT_TRUE(runtime.setParam(u"eq"_s, kClapOutputGain, QJsonValue(-6.0206)).has_value());
        ASSERT_TRUE(m_pump->pumpUntil([this] { return std::abs(m_sound->ratio() - 0.5f) < 1e-3f; }, 2s))
            << "heard at " << m_sound->ratio();

        // Saved: the plugin's own state, from the host, changed by it.
        const QHash<QString, QJsonObject> state = runtime.blockState();
        const QString saved = state.value(u"eq"_s).value("state"_L1).toString();
        EXPECT_FALSE(saved.isEmpty());
        EXPECT_NE(saved, before) << "the state did not change with the setting";
        EXPECT_EQ(state.value(u"eq"_s).value("plugin"_L1).toString(), kLspClapEqualizer);
        for (BlockInstance &block : session.blocks) {
            block.params = state.value(block.id, block.params);
        }
        runtime.stop();
    }
    // Put back from the file: the plugin's state alone makes it half.
    std::expected<Session, SessionError> opened = sessionFromJson(sessionToJson(session));
    ASSERT_TRUE(opened.has_value());
    SessionRuntime reopened(m_pump->connection(), m_pump->mirror(), m_registry);
    run(reopened, *opened);
    EXPECT_TRUE(m_pump->pumpUntil([this] { return std::abs(m_sound->ratio() - 0.5f) < 1e-3f; }, 3s))
        << "heard at " << m_sound->ratio();
    reopened.stop();
}

TEST_F(PluginSessions, AClapCompressorCompressesWhenItIsAskedTo)
{
    if (!LspClapCatalogue::installed()) {
        GTEST_SKIP() << "LSP's CLAP plugins are not installed (lsp-plugins-clap), or this build has no CLAP SDK";
    }
    LspClapCatalogue catalogue;
    // A ratio of one with the threshold at the top and a hard knee: nothing
    // to do.
    const Session session = SessionBuilder()
                                .clap(u"comp"_s,
                                      kLspClapCompressor,
                                      QJsonObject{{kClapRatio, 0.0},
                                                  {kClapAttackThreshold, 0.0},
                                                  {kClapKnee, 0.0},
                                                  {kClapSidechainMode, u"0"_s},
                                                  {kClapStereoSplit, true}})
                                .wire(u"in"_s, u"comp"_s)
                                .wire(u"comp"_s, u"out"_s)
                                .session();
    SessionRuntime runtime(m_pump->connection(), m_pump->mirror(), m_registry);
    run(runtime, session);
    forceQuantum(256);
    m_pump->pump(500ms);
    // Every kind of setting the file holds reached the block: a choice (Peak,
    // not the plugin's RMS) and a switch as well as the numbers.
    const QJsonObject settings = runtime.blockState().value(u"comp"_s);
    EXPECT_EQ(settings.value(kClapSidechainMode).toString(), u"0"_s);
    EXPECT_EQ(settings.value(kClapStereoSplit).toBool(), true);
    EXPECT_DOUBLE_EQ(settings.value(kClapRatio).toDouble(), 0.0);
    const Devices::Heard untouched = m_sound->listen(*m_pump, 1s);
    EXPECT_GT(untouched.heard(), 90U);
    EXPECT_EQ(untouched.same, untouched.heard()) << "a ratio of one changed the sound";

    // Then a hundred to one from sixty decibels down: squashed.
    ASSERT_TRUE(runtime.setParam(u"comp"_s, kClapAttackThreshold, QJsonValue(-60.0)).has_value());
    ASSERT_TRUE(runtime.setParam(u"comp"_s, kClapRatio, QJsonValue(1.0)).has_value());
    EXPECT_TRUE(m_pump->pumpUntil([this] { return m_sound->ratio() < 0.2f; }, 3s)) << "heard at " << m_sound->ratio();
    std::printf("[measure] LSP Compressor Stereo (CLAP) at 100:1 from -60 dB: output over input %.4f\n",
                static_cast<double>(m_sound->ratio()));
    runtime.stop();
}

TEST_F(PluginSessions, AKilledClapHostIsLinkedAroundAsAnyHostIs)
{
    if (!LspClapCatalogue::installed()) {
        GTEST_SKIP() << "LSP's CLAP plugins are not installed (lsp-plugins-clap), or this build has no CLAP SDK";
    }
    LspClapCatalogue catalogue;
    const Session session = SessionBuilder()
                                .clap(u"eq"_s, kLspClapEqualizer, QJsonObject{{kClapOutputGain, -6.0206}})
                                .wire(u"in"_s, u"eq"_s)
                                .wire(u"eq"_s, u"out"_s)
                                .session();
    SessionRuntime runtime(m_pump->connection(), m_pump->mirror(), m_registry);
    run(runtime, session);
    forceQuantum(256);
    m_pump->pump(300ms);
    ASSERT_TRUE(m_pump->pumpUntil([this] { return std::abs(m_sound->ratio() - 0.5f) < 1e-3f; }, 3s))
        << "heard at " << m_sound->ratio();

    QElapsedTimer killed;
    killed.start();
    ASSERT_TRUE(signalProcess(runtime.pluginHost(u"eq"_s)->pid(), SIGKILL));
    ASSERT_TRUE(m_pump->pumpUntil([this] { return std::abs(m_sound->ratio() - 1.0f) < 1e-4f; }, 1s));
    const double bypassed = static_cast<double>(killed.nsecsElapsed()) / 1e6;
    std::printf("[measure] killed CLAP host linked around, heard again %.1f ms after the kill\n", bypassed);
    EXPECT_LT(bypassed, 100.0);
    EXPECT_EQ(runtime.pluginHealth(u"eq"_s), PluginHealth::Crashed);
    runtime.stop();
}
