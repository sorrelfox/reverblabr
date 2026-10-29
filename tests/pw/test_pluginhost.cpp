// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// A plugin in a process of its own (ADR-0014), in a real graph: a node
// in this process feeds the host's node, which feeds another node in this
// process, and the three are judged from both ends.
//
// **What the first node plays says which cycle it is**: every sample of a
// cycle is that cycle's number, scaled small. The last node knows which
// cycle it is running in, so it can tell whether what reached it went
// through the host in the same cycle, or a cycle late, or not at all.

#include "pw/pluginhostprocess.h"
#include "pwtest.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <gtest/gtest.h>
#include <pipewire/filter.h>
#include <pipewire/pipewire.h>

#include <array>
#include <atomic>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

using namespace rb::pw;
using namespace rb::pwtest;
using namespace std::chrono_literals;

namespace
{

constexpr std::uint64_t kCodes = 4096;
constexpr float kCodeScale = 8192.0f;

[[nodiscard]] std::uint64_t cycleOf(const spa_io_position *position)
{
    return position->clock.position / std::max<std::uint64_t>(position->clock.duration, 1);
}

struct Port {
};

/// The node before the host and the node after it, on a thread loop of
/// their own, as the engine's session nodes are.
class Chain
{
public:
    Chain()
    {
        pw_init(nullptr, nullptr);
        m_loop = pw_thread_loop_new("rbtest-plugin-chain", nullptr);
        pw_thread_loop_lock(m_loop);
        m_source = make("rbtest.plugin.before", &Chain::onSource, false);
        m_sink = make("rbtest.plugin.after", &Chain::onSink, true);
        pw_thread_loop_start(m_loop);
        pw_thread_loop_unlock(m_loop);
    }

    ~Chain()
    {
        pw_thread_loop_lock(m_loop);
        pw_filter_destroy(m_source.filter);
        pw_filter_destroy(m_sink.filter);
        pw_thread_loop_unlock(m_loop);
        pw_thread_loop_stop(m_loop);
        pw_thread_loop_destroy(m_loop);
    }

    Chain(const Chain &) = delete;
    Chain &operator=(const Chain &) = delete;

    /// How many cycles the node before the host has run: what the watchdog
    /// compares the host's heartbeat with.
    [[nodiscard]] std::uint64_t sourceCycles() const
    {
        return m_sourceCycles.load();
    }

    /// **What the last node heard, counted since it started.** The counts
    /// only ever go up, and only the audio thread moves them, so a window
    /// is one snapshot taken from another (listen()): nothing is reset
    /// under a cycle that is counting, and what was heard in a window is
    /// the sum of what it was heard as, read once each.
    struct Counts {
        std::uint64_t same = 0; ///< heard in the cycle it was played
        std::uint64_t late = 0; ///< heard one cycle later
        std::uint64_t other = 0; ///< anything else
        std::uint64_t silent = 0;
        /// Cycles not judged, just after the last node saw one go missing:
        /// a cycle the machine did not run is not the code's doing, and the
        /// two after it carry what was in the lines when it went.
        std::uint64_t skipped = 0;

        [[nodiscard]] std::uint64_t heard() const
        {
            return same + late + other;
        }

        [[nodiscard]] Counts operator-(const Counts &before) const
        {
            return Counts{same - before.same,
                          late - before.late,
                          other - before.other,
                          silent - before.silent,
                          skipped - before.skipped};
        }
    };

    [[nodiscard]] Counts counts() const
    {
        return Counts{m_same.load(), m_late.load(), m_other.load(), m_silent.load(), m_skipped.load()};
    }

    /// The gain the host is expected to apply, to read the code back.
    void expectGain(float gain)
    {
        m_expectedGain = gain;
    }

    /// What reached the last node, over what the first played, both last
    /// cycle: the host's gain as heard.
    std::atomic<float> m_ratio{0.0f};

private:
    struct Node {
        pw_filter *filter = nullptr;
        Port *left = nullptr;
        Port *right = nullptr;
        Chain *chain = nullptr;
        void (Chain::*process)(Node &, spa_io_position *) = nullptr;
    };

    static void onProcess(void *data, spa_io_position *position)
    {
        auto *node = static_cast<Node *>(data);
        if (position != nullptr) {
            (node->chain->*node->process)(*node, position);
        }
    }

    static pw_filter_events makeEvents()
    {
        pw_filter_events events{};
        events.version = PW_VERSION_FILTER_EVENTS;
        events.process = &Chain::onProcess;
        return events;
    }

    Node make(const char *name, void (Chain::*process)(Node &, spa_io_position *), bool input)
    {
        static const pw_filter_events events = makeEvents();
        auto node = std::make_unique<Node>();
        node->chain = this;
        node->process = process;
        node->filter = pw_filter_new_simple(pw_thread_loop_get_loop(m_loop),
                                            name,
                                            pw_properties_new(PW_KEY_MEDIA_TYPE,
                                                              "Audio",
                                                              PW_KEY_MEDIA_CATEGORY,
                                                              "Filter",
                                                              PW_KEY_NODE_NAME,
                                                              name,
                                                              PW_KEY_NODE_AUTOCONNECT,
                                                              "false",
                                                              "node.always-process",
                                                              "true",
                                                              nullptr),
                                            &events,
                                            node.get());
        const pw_direction direction = input ? PW_DIRECTION_INPUT : PW_DIRECTION_OUTPUT;
        const char *prefix = input ? "in_" : "out_";
        for (const char *channel : {"FL", "FR"}) {
            const std::string portName = std::string(prefix) + channel;
            auto *port = static_cast<Port *>(pw_filter_add_port(
                node->filter,
                direction,
                PW_FILTER_PORT_FLAG_MAP_BUFFERS,
                sizeof(Port),
                pw_properties_new(
                    PW_KEY_FORMAT_DSP, "32 bit float mono audio", PW_KEY_PORT_NAME, portName.c_str(), nullptr),
                nullptr,
                0));
            (std::string_view(channel) == "FL" ? node->left : node->right) = port;
        }
        pw_filter_connect(node->filter, PW_FILTER_FLAG_RT_PROCESS, nullptr, 0);
        m_nodes.push_back(std::move(node));
        return *m_nodes.back();
    }

    void onSource(Node &node, spa_io_position *position)
    {
        const std::uint32_t frames = position->clock.duration;
        const float value = static_cast<float>(cycleOf(position) % kCodes + 1) / kCodeScale;
        m_played.store(value);
        for (Port *port : {node.left, node.right}) {
            if (auto *out = static_cast<float *>(pw_filter_get_dsp_buffer(port, frames)); out != nullptr) {
                std::fill_n(out, frames, value);
            }
        }
        ++m_sourceCycles;
    }

    void onSink(Node &node, spa_io_position *position)
    {
        const std::uint32_t frames = position->clock.duration;
        const auto *left = static_cast<const float *>(pw_filter_get_dsp_buffer(node.left, frames));
        const auto *right = static_cast<const float *>(pw_filter_get_dsp_buffer(node.right, frames));
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
        if (right[0] != left[frames - 1]) {
            ++m_other; // the two channels must carry the same
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
    Node m_source;
    Node m_sink;
    std::atomic<std::uint64_t> m_sourceCycles{0};
    std::atomic<float> m_played{1.0f};
    std::atomic<float> m_expectedGain{1.0f};
    std::atomic<std::uint64_t> m_same{0};
    std::atomic<std::uint64_t> m_late{0};
    std::atomic<std::uint64_t> m_other{0};
    std::atomic<std::uint64_t> m_silent{0};
    std::atomic<std::uint64_t> m_skipped{0};
    /// The audio thread's own, for noticing a cycle that went missing.
    std::uint64_t m_lastCycle = 0;
    int m_settling = 0;
};

/// Runs the event loop until \a done, or for \a timeout.
bool spinUntil(const std::function<bool()> &done, std::chrono::milliseconds timeout = kDefaultTimeout)
{
    QElapsedTimer timer;
    timer.start();
    while (!done()) {
        if (timer.elapsed() > timeout.count()) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
    }
    return true;
}

void spinFor(std::chrono::milliseconds duration)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < duration.count()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
}

bool pwLink(const std::string &from, const std::string &to, bool unlink = false)
{
    std::vector<std::string> argv{"pw-link"};
    if (unlink) {
        argv.emplace_back("-d");
    }
    argv.push_back(from);
    argv.push_back(to);
    return runCommand(argv).status == 0;
}

/// Before, host, after: each channel from one to the next, or around the
/// host when \a around.
/// **How the host names its ports**: the built-in plugins after their
/// channels, an LV2 plugin's by number.
std::array<std::string, 2> g_hostPorts{"FL", "FR"};

bool linkChain(const QString &host, bool around = false, bool unlink = false)
{
    bool ok = true;
    for (std::size_t side = 0; side < 2; ++side) {
        const std::string channel = side == 0 ? "FL" : "FR";
        const std::string port = g_hostPorts[side];
        if (around) {
            ok = pwLink("rbtest.plugin.before:out_" + channel, "rbtest.plugin.after:in_" + channel, unlink) && ok;
        } else {
            const std::string node = host.toStdString();
            ok = pwLink("rbtest.plugin.before:out_" + channel, node + ":in_" + port, unlink) && ok;
            ok = pwLink(node + ":out_" + port, "rbtest.plugin.after:in_" + channel, unlink) && ok;
        }
    }
    return ok;
}

void forceQuantum(int quantum)
{
    (void)runCommand({"pw-metadata", "-n", "settings", "0", "clock.force-quantum", std::to_string(quantum)});
}

int g_argc = 1;
char g_programName[] = "reverblabr-pw-pluginhost-test";
char *g_argv[] = {g_programName, nullptr};

} // namespace

class PluginHosts : public PipeWireTest
{
protected:
    void SetUp() override
    {
        PipeWireTest::SetUp();
        m_app = std::make_unique<QCoreApplication>(g_argc, g_argv);
        m_chain = std::make_unique<Chain>();
        qputenv("REVERBLABR_PLUGIN_HOST", RB_PLUGIN_HOST);
    }

    void TearDown() override
    {
        forceQuantum(0);
        m_chain.reset();
        m_app.reset();
        PipeWireTest::TearDown();
    }

    /// A host with the test gain in it, started and in the graph.
    void startHost(PluginHostProcess &host,
                   std::uint32_t latency = 0,
                   const QString &plugin = QStringLiteral("test.gain"),
                   const QString &format = QStringLiteral("builtin"),
                   const QString &mono = QStringLiteral("dualMono"))
    {
        PluginHostSpec spec;
        spec.plugin = plugin;
        spec.format = format;
        spec.monoMode = mono;
        g_hostPorts = format == QStringLiteral("builtin") ? std::array<std::string, 2>{"FL", "FR"}
                                                          : std::array<std::string, 2>{"1", "2"};
        // Another name for every host, so that one test's host on its way
        // out is never taken for the next one's.
        static int s_instances = 0;
        spec.instance = QStringLiteral("t%1").arg(++s_instances);
        spec.latencySamples = latency;
        EXPECT_EQ(spec.program, QString::fromUtf8(RB_PLUGIN_HOST));
        QObject::connect(&host, &PluginHostProcess::ready, &host, [this](const QString &, quint32 latency) {
            m_readyLatency = latency;
        });
        QObject::connect(&host, &PluginHostProcess::exited, &host, [this](int code, bool crashed) {
            m_exitCode = code;
            m_crashed = crashed;
            m_exitAt = m_clock.nsecsElapsed();
        });
        QObject::connect(&host, &PluginHostProcess::failed, &host, [this](const QString &code) { m_failure = code; });
        QObject::connect(&host, &PluginHostProcess::hung, &host, [this] { m_hungAt = m_clock.nsecsElapsed(); });
        m_clock.start();
        ASSERT_TRUE(host.start(spec));
    }

    /// Starts the host, waits for it to be ready, and links the chain.
    void startChain(PluginHostProcess &host,
                    std::uint32_t latency = 0,
                    const QString &plugin = QStringLiteral("test.gain"),
                    const QString &format = QStringLiteral("builtin"),
                    const QString &mono = QStringLiteral("dualMono"))
    {
        startHost(host, latency, plugin, format, mono);
        ASSERT_TRUE(spinUntil([&] { return host.isReady(); })) << "the host never said it was ready";
        EXPECT_TRUE(host.nodeName().startsWith(QStringLiteral("reverblabr.plugin.t"))) << host.nodeName().toStdString();
        ASSERT_TRUE(waitForNode(nodeNamed(host.nodeName().toStdString())).has_value());
        ASSERT_TRUE(linkChain(host.nodeName()));
        ASSERT_TRUE(spinUntil([&] { return m_chain->counts().heard() > 10; })) << "nothing came through the host";
    }

    std::unique_ptr<Chain> m_chain;

    /// What the last node heard over \a duration, from here on.
    [[nodiscard]] Chain::Counts listen(std::chrono::milliseconds duration)
    {
        const Chain::Counts before = m_chain->counts();
        spinFor(duration);
        return m_chain->counts() - before;
    }
    QElapsedTimer m_clock;
    std::optional<quint32> m_readyLatency;
    std::optional<int> m_exitCode;
    bool m_crashed = false;
    qint64 m_exitAt = -1;
    qint64 m_hungAt = -1;
    QString m_failure;

private:
    std::unique_ptr<QCoreApplication> m_app;
};

TEST_F(PluginHosts, TheHostRunsInTheSameCycleAsTheNodesAroundItAtEveryQuantum)
{
    PluginHostProcess host;
    startChain(host);

    for (const int quantum : {64, 256, 1024}) {
        forceQuantum(quantum);
        spinFor(500ms);
        const Chain::Counts heard = listen(1s);
        std::printf(
            "[measure] quantum %d: %llu cycles heard after the host, %llu in the same cycle, %llu one late, "
            "%llu otherwise, %llu silent, %llu not judged after a missed cycle\n",
            quantum,
            static_cast<unsigned long long>(heard.heard()),
            static_cast<unsigned long long>(heard.same),
            static_cast<unsigned long long>(heard.late),
            static_cast<unsigned long long>(heard.other),
            static_cast<unsigned long long>(heard.silent),
            static_cast<unsigned long long>(heard.skipped));
        EXPECT_GT(heard.heard(), 48000U / static_cast<unsigned>(quantum) / 2) << "at quantum " << quantum;
        EXPECT_EQ(heard.same, heard.heard()) << "at quantum " << quantum;
        EXPECT_EQ(heard.silent, 0U) << "at quantum " << quantum;
    }
}

TEST_F(PluginHosts, AParameterSentThroughTheRingIsHeardWithinACycleOrTwo)
{
    PluginHostProcess host;
    startChain(host);
    forceQuantum(256);
    spinFor(300ms);
    EXPECT_NEAR(m_chain->m_ratio.load(), 1.0f, 1e-4f);

    const std::uint64_t cyclesBefore = host.cycles();
    QElapsedTimer sent;
    sent.start();
    ASSERT_TRUE(host.setParam(0, 0.5f));
    ASSERT_TRUE(spinUntil([&] { return std::abs(m_chain->m_ratio.load() - 0.5f) < 1e-4f; }, 1s));
    const std::uint64_t cyclesTaken = host.cycles() - cyclesBefore;
    std::printf("[measure] gain 0.5 heard after %.1f ms, %llu host cycles\n",
                static_cast<double>(sent.nsecsElapsed()) / 1e6,
                static_cast<unsigned long long>(cyclesTaken));
    EXPECT_LE(cyclesTaken, 3U);
    EXPECT_EQ(host.droppedParams(), 0U);

    // And from then on, every cycle, still in the same cycle.
    m_chain->expectGain(0.5f);
    spinFor(100ms);
    const Chain::Counts heard = listen(500ms);
    EXPECT_GT(heard.heard(), 50U);
    EXPECT_EQ(heard.same, heard.heard());
}

TEST_F(PluginHosts, TheLatencyThePluginReportsIsTheDelayThatComesThrough)
{
    // A plugin with a cycle's worth of latency hears every cycle one late:
    // the engine will compensate the rest of the graph by what it reports,
    // so what it reports had better be what it does.
    PluginHostProcess host;
    startChain(host, 256);
    ASSERT_TRUE(m_readyLatency.has_value());
    EXPECT_EQ(*m_readyLatency, 256U);
    EXPECT_EQ(host.latencySamples(), 256U);

    forceQuantum(256);
    spinFor(500ms);
    const Chain::Counts heard = listen(1s);
    std::printf(
        "[measure] 256 samples of latency at quantum 256: %llu heard, %llu one cycle late, %llu same, "
        "%llu otherwise, %llu not judged after a missed cycle\n",
        static_cast<unsigned long long>(heard.heard()),
        static_cast<unsigned long long>(heard.late),
        static_cast<unsigned long long>(heard.same),
        static_cast<unsigned long long>(heard.other),
        static_cast<unsigned long long>(heard.skipped));
    EXPECT_GT(heard.heard(), 90U);
    EXPECT_EQ(heard.late, heard.heard());
}

TEST_F(PluginHosts, AHostThatDiesIsNoticedAtOnceAndLinkedAroundWithin100Ms)
{
    PluginHostProcess host;
    startChain(host);
    const auto node = waitForNode(nodeNamed(host.nodeName().toStdString()));
    ASSERT_TRUE(node.has_value());

    forceQuantum(256);
    spinFor(300ms);
    const qint64 killedAt = m_clock.nsecsElapsed();
    ASSERT_TRUE(signalProcess(host.pid(), SIGKILL));
    ASSERT_TRUE(spinUntil([&] { return m_exitCode.has_value(); }, 1s));
    const double noticed = static_cast<double>(m_exitAt - killedAt) / 1e6;
    EXPECT_LT(noticed, 100.0);
    EXPECT_TRUE(m_crashed);

    // The bypass as the session will make it: a link around the host, which
    // is all that is left to do once it has gone.
    const std::uint64_t sameBefore = m_chain->counts().same;
    ASSERT_TRUE(linkChain(host.nodeName(), true));
    ASSERT_TRUE(spinUntil([&] { return m_chain->counts().same > sameBefore; }, 1s));
    const double bypassed = static_cast<double>(m_clock.nsecsElapsed() - killedAt) / 1e6;
    std::printf(
        "[measure] killed host noticed after %.2f ms; heard around it %.1f ms after the kill "
        "(the pw-link calls included)\n",
        noticed,
        bypassed);
    EXPECT_LT(bypassed, 100.0);
    EXPECT_FALSE(host.isRunning());
    EXPECT_TRUE(waitForNodeGone(*node));
    // The nodes around it are still running.
    const std::uint64_t before = m_chain->sourceCycles();
    spinFor(100ms);
    EXPECT_GT(m_chain->sourceCycles(), before);
}

TEST_F(PluginHosts, AHostThatStopsRunningIsNoticedWithinAFewCyclesAndCanBeLinkedAround)
{
    PluginHostProcess host;
    startChain(host);
    forceQuantum(256);
    spinFor(300ms);
    host.watch([this] { return m_chain->sourceCycles(); });
    spinFor(200ms);
    ASSERT_EQ(m_hungAt, -1) << "a host that runs is not hung";

    const qint64 stoppedAt = m_clock.nsecsElapsed();
    ASSERT_TRUE(signalProcess(host.pid(), SIGSTOP));
    ASSERT_TRUE(spinUntil([&] { return m_hungAt >= 0; }, 1s)) << "the watchdog never said";
    const double noticed = static_cast<double>(m_hungAt - stoppedAt) / 1e6;
    std::printf("[measure] stopped host noticed after %.2f ms\n", noticed);
    EXPECT_LT(noticed, 100.0);

    // The bypass: the host's links go, one around it comes.
    QElapsedTimer relink;
    relink.start();
    const std::uint64_t sameBefore = m_chain->counts().same;
    ASSERT_TRUE(linkChain(host.nodeName(), false, true));
    ASSERT_TRUE(linkChain(host.nodeName(), true));
    ASSERT_TRUE(spinUntil([&] { return m_chain->counts().same > sameBefore; }, 1s));
    std::printf("[measure] around the stopped host, heard again after %.1f ms (the pw-link calls included)\n",
                static_cast<double>(relink.nsecsElapsed()) / 1e6);

    EXPECT_TRUE(signalProcess(host.pid(), SIGCONT));
}

TEST_F(PluginHosts, AHostAskedToFinishDoesSoCleanly)
{
    PluginHostProcess host;
    startChain(host);
    const auto node = waitForNode(nodeNamed(host.nodeName().toStdString()));
    ASSERT_TRUE(node.has_value());

    host.stop();
    ASSERT_TRUE(spinUntil([&] { return m_exitCode.has_value(); }, 2s));
    EXPECT_EQ(*m_exitCode, 0);
    EXPECT_FALSE(m_crashed);
    EXPECT_TRUE(waitForNodeGone(*node));
}

TEST_F(PluginHosts, TheStateComesBackAndGoesBackIn)
{
    PluginHostProcess host;
    startChain(host);
    QString state;
    QObject::connect(&host, &PluginHostProcess::stateReceived, &host, [&](const QString &text) { state = text; });

    ASSERT_TRUE(host.setParam(0, 2.0f));
    ASSERT_TRUE(spinUntil([&] { return std::abs(m_chain->m_ratio.load() - 2.0f) < 1e-3f; }, 1s));
    host.requestState();
    ASSERT_TRUE(spinUntil([&] { return !state.isEmpty(); }, 1s));
    EXPECT_TRUE(state.startsWith(QStringLiteral("gain=2"))) << state.toStdString();

    host.setState(QStringLiteral("gain=0.25\n"));
    ASSERT_TRUE(spinUntil([&] { return std::abs(m_chain->m_ratio.load() - 0.25f) < 1e-3f; }, 1s));
}

TEST_F(PluginHosts, APluginWithNoWindowOrPresetsSaysSoAndRunsOn)
{
    // The built-in plugin has neither, and the answers say so, where a
    // plugin of a format that has them would show its window: the
    // messages, with no window anywhere.
    PluginHostProcess host;
    startChain(host);
    std::optional<QString> closed;
    bool opened = false;
    std::optional<QList<QPair<QString, QString>>> presets;
    std::optional<QString> shown;
    std::optional<QList<QPair<QString, float>>> values;
    QObject::connect(&host, &PluginHostProcess::uiOpened, &host, [&] { opened = true; });
    QObject::connect(&host, &PluginHostProcess::uiClosed, &host, [&](const QString &why) { closed = why; });
    QObject::connect(&host, &PluginHostProcess::presetsListed, &host, [&](const auto &list) { presets = list; });
    QObject::connect(&host, &PluginHostProcess::presetShown, &host, [&](const QString &id) { shown = id; });
    QObject::connect(&host, &PluginHostProcess::valuesChanged, &host, [&](bool, const auto &list) { values = list; });

    host.openUi();
    ASSERT_TRUE(spinUntil([&] { return closed.has_value(); }, 1s));
    EXPECT_EQ(*closed, QStringLiteral("unavailable"));
    EXPECT_FALSE(opened);
    host.listPresets();
    ASSERT_TRUE(spinUntil([&] { return presets.has_value(); }, 1s));
    EXPECT_TRUE(presets->isEmpty());
    host.loadPreset(QStringLiteral("nothing"));
    ASSERT_TRUE(spinUntil([&] { return shown.has_value(); }, 1s));
    EXPECT_TRUE(shown->isEmpty());

    // **A state put back says what it holds**, for the engine's panel.
    host.setState(QStringLiteral("gain=0.5\n"));
    ASSERT_TRUE(spinUntil([&] { return values.has_value(); }, 1s));
    ASSERT_EQ(values->size(), 1);
    EXPECT_EQ(values->front().first, QStringLiteral("gain"));
    EXPECT_FLOAT_EQ(values->front().second, 0.5f);
    ASSERT_TRUE(spinUntil([&] { return std::abs(m_chain->m_ratio.load() - 0.5f) < 1e-3f; }, 1s));
}

TEST_F(PluginHosts, APluginTheHostDoesNotHaveIsAFailureItSays)
{
    PluginHostProcess host;
    startHost(host, 0, QStringLiteral("test.nothing"));
    ASSERT_TRUE(spinUntil([&] { return m_exitCode.has_value(); }, 2s));
    EXPECT_EQ(m_failure, QStringLiteral("UnknownPlugin"));
    EXPECT_EQ(*m_exitCode, 3);
    EXPECT_TRUE(m_crashed);
    EXPECT_FALSE(host.isReady());
}

TEST_F(PluginHosts, AChangeMadeWhileAStateLoadsIsNotLost)
{
    // A plugin that takes a fifth of a second to load a state: a knob turned
    // meanwhile would reach the plugin being replaced, and be lost with it,
    // if it were not held back until the new one runs.
    PluginHostProcess host;
    startChain(host, 0, QStringLiteral("test.slowgain"));
    std::optional<bool> loaded;
    QObject::connect(&host, &PluginHostProcess::stateLoaded, &host, [&](bool ok) { loaded = ok; });

    host.setState(QStringLiteral("gain=0.25\n"));
    ASSERT_TRUE(host.setParam(0, 3.0f));
    ASSERT_TRUE(spinUntil([&] { return loaded.has_value(); }, 2s));
    EXPECT_TRUE(*loaded);
    ASSERT_TRUE(spinUntil([&] { return std::abs(m_chain->m_ratio.load() - 3.0f) < 1e-3f; }, 1s))
        << "heard at " << m_chain->m_ratio.load() << ": the change made while the state loaded was lost";
    spinFor(100ms);
    EXPECT_NEAR(m_chain->m_ratio.load(), 3.0f, 1e-3f);
}

TEST_F(PluginHosts, AHostThatHoldsTheWholeGraphStillIsHungAfterTheLongestStillness)
{
    // A host stopped while PipeWire changes the graph can hold everything
    // still, the audio feeding it too: the count of cycles before it
    // then never moves, and the heartbeat standing still for long enough is
    // all there is to go on.
    PluginHostProcess host;
    startChain(host);
    forceQuantum(256);
    spinFor(300ms);
    host.watch([] { return std::uint64_t{0}; }, 3, 40ms, 200ms);
    spinFor(300ms);
    ASSERT_EQ(m_hungAt, -1) << "a host that runs is not hung";

    const qint64 stoppedAt = m_clock.nsecsElapsed();
    ASSERT_TRUE(signalProcess(host.pid(), SIGSTOP));
    ASSERT_TRUE(spinUntil([&] { return m_hungAt >= 0; }, 2s)) << "the watchdog never said";
    const double noticed = static_cast<double>(m_hungAt - stoppedAt) / 1e6;
    std::printf("[measure] stopped host with nothing moving before it noticed after %.1f ms\n", noticed);
    EXPECT_GE(noticed, 190.0);
    EXPECT_LT(noticed, 400.0);
    EXPECT_TRUE(signalProcess(host.pid(), SIGKILL));
}

namespace
{

constexpr const char *kLspEqualizer = "http://lsp-plug.in/plugins/lv2/para_equalizer_x8_stereo";
constexpr const char *kLspMonoEqualizer = "http://lsp-plug.in/plugins/lv2/para_equalizer_x8_mono";

[[nodiscard]] bool lspInstalled()
{
    return std::filesystem::exists("/usr/lib64/lv2/lsp-plugins.lv2/manifest.ttl");
}

} // namespace

TEST_F(PluginHosts, AnLv2PluginRunsInTheHostAndHearsItsSettings)
{
    // LSP's eight-band equaliser with every band off is the sound as it
    // came; its output gain is its third setting.
    if (!lspInstalled()) {
        GTEST_SKIP() << "LSP's LV2 plugins are not installed (lsp-plugins-lv2)";
    }
    PluginHostProcess host;
    startChain(host, 0, QString::fromLatin1(kLspEqualizer), QStringLiteral("lv2"));
    forceQuantum(256);
    spinFor(500ms);
    const Chain::Counts flat = listen(500ms);
    std::printf("[measure] LSP Parametric Equalizer x8 Stereo, every band off: %llu heard, %llu in the same cycle\n",
                static_cast<unsigned long long>(flat.heard()),
                static_cast<unsigned long long>(flat.same));
    EXPECT_GT(flat.heard(), 50U);
    EXPECT_EQ(flat.same, flat.heard()) << "not the sound as it came, in the same cycle";

    ASSERT_TRUE(host.setParam(2, 0.5f)); // g_out
    ASSERT_TRUE(spinUntil([&] { return std::abs(m_chain->m_ratio.load() - 0.5f) < 1e-3f; }, 1s))
        << "heard at " << m_chain->m_ratio.load();

    // Its state is LV2's, as Turtle, with the setting in it.
    const std::optional<QString> state = host.stateNow();
    ASSERT_TRUE(state.has_value());
    EXPECT_TRUE(state->contains(QStringLiteral("g_out"))) << state->left(400).toStdString();
    EXPECT_TRUE(state->contains(QStringLiteral("0.5"))) << state->left(400).toStdString();

    // And put back into a plugin that had it at unity.
    ASSERT_TRUE(host.setParam(2, 1.0f));
    ASSERT_TRUE(spinUntil([&] { return std::abs(m_chain->m_ratio.load() - 1.0f) < 1e-3f; }, 1s));
    host.setState(*state);
    EXPECT_TRUE(spinUntil([&] { return std::abs(m_chain->m_ratio.load() - 0.5f) < 1e-3f; }, 2s))
        << "heard at " << m_chain->m_ratio.load();
}

TEST_F(PluginHosts, AMonoLv2PluginRunsOnceASideOrOnTheSidesMixed)
{
    if (!lspInstalled()) {
        GTEST_SKIP() << "LSP's LV2 plugins are not installed (lsp-plugins-lv2)";
    }
    {
        // Once a side: each side comes through as itself.
        PluginHostProcess host;
        startChain(host, 0, QString::fromLatin1(kLspMonoEqualizer), QStringLiteral("lv2"), QStringLiteral("dualMono"));
        forceQuantum(256);
        spinFor(500ms);
        const Chain::Counts heard = listen(500ms);
        EXPECT_GT(heard.heard(), 50U);
        EXPECT_EQ(heard.same, heard.heard()) << "both sides, each as it came";
        host.kill();
    }
    {
        // On the sides mixed: the same, since both sides play the same.
        PluginHostProcess host;
        startChain(host, 0, QString::fromLatin1(kLspMonoEqualizer), QStringLiteral("lv2"), QStringLiteral("downmix"));
        spinFor(500ms);
        const Chain::Counts heard = listen(500ms);
        EXPECT_GT(heard.heard(), 50U);
        EXPECT_EQ(heard.same, heard.heard());
    }
}

namespace
{

/// LADSPA_PATH at the tests' own plugins, for as long as a test
/// runs: the host a test starts inherits it.
class TestLadspaPath
{
public:
    TestLadspaPath()
    {
        if (const char *before = std::getenv("LADSPA_PATH"); before != nullptr) {
            m_before = before;
        }
        ::setenv("LADSPA_PATH", RB_TEST_LADSPA_DIR, 1);
    }
    ~TestLadspaPath()
    {
        if (m_before) {
            ::setenv("LADSPA_PATH", m_before->c_str(), 1);
        } else {
            ::unsetenv("LADSPA_PATH");
        }
    }
    TestLadspaPath(const TestLadspaPath &) = delete;
    TestLadspaPath &operator=(const TestLadspaPath &) = delete;

private:
    std::optional<std::string> m_before;
};

} // namespace

TEST_F(PluginHosts, ALadspaPluginRunsInTheHostAndHearsItsSettings)
{
    // The tests' own stereo gain, found by its library and label.
    const TestLadspaPath path;
    PluginHostProcess host;
    startChain(host, 0, QStringLiteral("rb-test-ladspa.so:rb_stereo_gain"), QStringLiteral("ladspa"));
    forceQuantum(256);
    spinFor(500ms);
    const Chain::Counts unity = listen(500ms);
    std::printf("[measure] LADSPA stereo gain at 1: %llu heard, %llu in the same cycle\n",
                static_cast<unsigned long long>(unity.heard()),
                static_cast<unsigned long long>(unity.same));
    EXPECT_GT(unity.heard(), 50U);
    EXPECT_EQ(unity.same, unity.heard()) << "not the sound as it came, in the same cycle";

    ASSERT_TRUE(host.setParam(0, 0.5f)); // gain
    ASSERT_TRUE(spinUntil([&] { return std::abs(m_chain->m_ratio.load() - 0.5f) < 1e-3f; }, 1s))
        << "heard at " << m_chain->m_ratio.load();

    // Its state is its control values, by the ids the scan gave them.
    const std::optional<QString> state = host.stateNow();
    ASSERT_TRUE(state.has_value());
    EXPECT_TRUE(state->startsWith(QStringLiteral("gain=0.5"))) << state->toStdString();

    // And put back into a plugin that had it at unity.
    ASSERT_TRUE(host.setParam(0, 1.0f));
    ASSERT_TRUE(spinUntil([&] { return std::abs(m_chain->m_ratio.load() - 1.0f) < 1e-3f; }, 1s));
    host.setState(*state);
    EXPECT_TRUE(spinUntil([&] { return std::abs(m_chain->m_ratio.load() - 0.5f) < 1e-3f; }, 2s))
        << "heard at " << m_chain->m_ratio.load();
}

TEST_F(PluginHosts, AMonoLadspaPluginRunsOnceASide)
{
    const TestLadspaPath path;
    PluginHostProcess host;
    startChain(host, 0, QStringLiteral("rb-test-ladspa.so:rb_mono_gain"), QStringLiteral("ladspa"));
    forceQuantum(256);
    spinFor(500ms);
    const Chain::Counts heard = listen(500ms);
    EXPECT_GT(heard.heard(), 50U);
    EXPECT_EQ(heard.same, heard.heard()) << "both sides, each as it came";
    ASSERT_TRUE(host.setParam(0, 0.25f));
    EXPECT_TRUE(spinUntil([&] { return std::abs(m_chain->m_ratio.load() - 0.25f) < 1e-3f; }, 1s))
        << "heard at " << m_chain->m_ratio.load();
}
