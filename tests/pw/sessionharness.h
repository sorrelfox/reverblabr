// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

// What a test needs in order to run a session node against the private
// PipeWire instance: an engine on a Qt event loop, a session of a few
// blocks, and a way to hold the server's clock still. Shared by the tests of
// the node itself and of editing it while it runs.

#include "../unit/dsp/gainblock.h"
#include "../unit/pw/sessionblocks.h"
#include "capturenode.h"
#include "dsp/channelmap.h"
#include "graph/graphdiff.h"
#include "graph/graphspec.h"
#include "pw/connection.h"
#include "pw/linkmanager.h"
#include "pw/registrymirror.h"
#include "pw/sessionnode.h"
#include "pwtest.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QString>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rb::pwtest
{

using namespace rb::pw;
using namespace Qt::StringLiterals;

constexpr std::uint32_t kRate = 48000;

/// How long any of these tests waits for the graph to catch up. Longer than
/// the harness's own default, because several of these run at once and each
/// pw-dump is a process.
constexpr std::chrono::milliseconds kWait{20000};

/// Turns the Qt event loop, and waits on the graph while it does.
///
/// **PipeWire's main loop runs on this thread's Qt event loop**, so a test
/// that waits without turning that loop starves the very round trips it is
/// waiting for: a `pw-dump` takes tens of milliseconds, and nothing moves
/// while it runs. Everything here pumps first and looks second.
///
/// It owns **no connection of its own**, which matters: entering two
/// PipeWire loops on one thread makes their locks invert, and
/// ThreadSanitizer rightly calls that a potential deadlock. A test that has
/// a connection already — through the engine, say — uses this and not Pump.
class Waiter
{
public:
    void pump(std::chrono::milliseconds duration)
    {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < duration.count()) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        }
    }

    [[nodiscard]] bool pumpUntil(const std::function<bool()> &condition, std::chrono::milliseconds timeout = kWait)
    {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeout.count()) {
            if (condition()) {
                return true;
            }
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        }
        return condition();
    }

    [[nodiscard]] std::optional<Graph::Node> awaitNode(const QString &name, std::chrono::milliseconds timeout = kWait)
    {
        return awaitMatch(nodeNamed(name.toStdString()), timeout);
    }

    [[nodiscard]] std::optional<Graph::Node> awaitMatch(const NodeMatch &match,
                                                        std::chrono::milliseconds timeout = kWait)
    {
        QElapsedTimer timer;
        timer.start();
        for (;;) {
            pump(std::chrono::milliseconds{50});
            const Graph graph = Graph::capture();
            if (const Graph::Node *node = graph.findNode(match); node != nullptr) {
                return *node;
            }
            if (timer.elapsed() >= timeout.count()) {
                return std::nullopt;
            }
        }
    }

    [[nodiscard]] bool awaitPorts(std::uint32_t nodeId,
                                  const std::string &direction,
                                  std::size_t count,
                                  std::chrono::milliseconds timeout = kWait)
    {
        QElapsedTimer timer;
        timer.start();
        for (;;) {
            pump(std::chrono::milliseconds{50});
            if (Graph::capture().portsOf(nodeId, direction).size() >= count) {
                return true;
            }
            if (timer.elapsed() >= timeout.count()) {
                return false;
            }
        }
    }

    [[nodiscard]] bool
    awaitLink(std::uint32_t outputNode, std::uint32_t inputNode, std::chrono::milliseconds timeout = kWait)
    {
        QElapsedTimer timer;
        timer.start();
        for (;;) {
            pump(std::chrono::milliseconds{50});
            if (Graph::capture().linked(outputNode, inputNode)) {
                return true;
            }
            if (timer.elapsed() >= timeout.count()) {
                return false;
            }
        }
    }

    /// Turns the loop until pw-dump shows what the condition asks for.
    ///
    /// **A link the manager has made is a request, not yet a link.**
    /// `LinkManager::ownedLinkCount()` counts it once the request is
    /// written, and the request reaches the server only when this thread's
    /// loop turns again. A `pumpUntil()` on that count can return before it
    /// has — whenever a slow reconcile uses up the pump's turn — and a
    /// pw-dump read then finds no link at all.
    [[nodiscard]] bool awaitGraph(const std::function<bool(const Graph &)> &condition,
                                  std::chrono::milliseconds timeout = kWait)
    {
        QElapsedTimer timer;
        timer.start();
        for (;;) {
            pump(std::chrono::milliseconds{50});
            if (condition(Graph::capture())) {
                return true;
            }
            if (timer.elapsed() >= timeout.count()) {
                return false;
            }
        }
    }
};

/// A Waiter with a connection of its own, and a mirror and link manager on
/// it — what a test of the PipeWire layer needs.
class Pump : public Waiter
{
public:
    Pump()
    {
        m_connection.setRetryDelays(std::chrono::milliseconds{50}, std::chrono::milliseconds{200});
        m_manager.setSettleTime(std::chrono::milliseconds{10});
        m_manager.setRetryDelays(std::chrono::milliseconds{50}, std::chrono::milliseconds{200});
        m_connection.start();
    }

    Connection &connection()
    {
        return m_connection;
    }

    LinkManager &manager()
    {
        return m_manager;
    }

    RegistryMirror &mirror()
    {
        return m_mirror;
    }

    [[nodiscard]] bool waitUntilConnected()
    {
        return pumpUntil([this] { return m_connection.isConnected(); });
    }

    [[nodiscard]] bool waitUntilReady()
    {
        return waitUntilConnected() && pumpUntil([this] { return m_mirror.isReady(); });
    }

private:
    Connection m_connection;
    // The engine has a registry mirror and a link manager on the same
    // connection as its session nodes, so the tests do too: that is the
    // arrangement a session node actually runs in.
    RegistryMirror m_mirror{m_connection};
    LinkManager m_manager{m_connection, m_mirror};
};

/// What the machine is costing a session that nothing is being asked of.
///
/// **A test that expects no lost cycles at all is measuring the machine
/// rather than the code.** `pw.session-node` failed that way on a CI runner
/// whose cores were shared with a second pipeline, and again on a developer
/// machine under `ctest -j8`: one cycle lost out of thousands, with nothing
/// wrong anywhere. So a test that cares about lost cycles reads the
/// session's own rate first, while nothing is happening to it, and judges
/// the window it is interested in against that reading — the ratio the CPU
/// budget in `DynamicsTest.TenMinutesOfSilenceCostNothingAndProduceNothing`
/// uses, counted in cycles rather than seconds.
///
/// Take the reading **at the quantum the test will judge at**: half as many
/// cycles, each twice as long, is not the same number of chances to be
/// late.
///
/// It resets the node's peak-load figure at both ends of its window, so the
/// reading belongs to that window and whatever the caller measures next
/// starts clean.
class MachineCost
{
public:
    MachineCost(Waiter &engine, SessionNode &node, std::chrono::milliseconds window = std::chrono::milliseconds{1000})
        : m_window(window)
    {
        node.resetPeakLoad();
        const std::uint64_t before = node.telemetry().xruns;
        engine.pump(window);
        const SessionTelemetry telemetry = node.telemetry();
        m_lost = telemetry.xruns - before;
        m_peakLoad = telemetry.peakLoad;
        node.resetPeakLoad();
    }

    /// What the same machine may cost the session over \a over without the
    /// session being at fault: the rate just measured, for that long, and
    /// one cycle more.
    ///
    /// **The one more is what makes the reading usable**, and it hides no
    /// defect: a session that really is losing cycles loses one every cycle,
    /// or one every time anything happens, which at quantum 64 is hundreds
    /// in a second and not one.
    [[nodiscard]] std::uint64_t allowanceFor(std::chrono::milliseconds over) const
    {
        return measuredFor(over) + 1;
    }

    /// The same, for a window the session ran \a cycles cycles in, **when the
    /// defect being looked for costs a steady share of them**: one per edit,
    /// one in every eight after a change, one in every cycle after it.
    ///
    /// **A short reading cannot see a busy machine's bursts.** What a gate
    /// costs a session beside other workers' builds is not a steady trickle
    /// but a stall now and then that takes several cycles with it at once:
    /// a thousand edits under asan lost 2, 6 and 8 in three runs of twelve,
    /// in a second and a half, where the half-second reading before them had
    /// lost none and so allowed one. So the allowance is never less
    /// than one cycle in kCyclesPerBusyMiss of those run — well under what
    /// any such defect costs, and well over what those bursts did.
    ///
    /// Not for a test whose defect is a single event, such as a change of
    /// driver counted as a lost cycle, or one every restart of a tone: a
    /// share of the cycles would hide those. They keep allowanceFor(over).
    [[nodiscard]] std::uint64_t allowanceFor(std::chrono::milliseconds over, std::uint64_t cycles) const
    {
        return std::max(measuredFor(over), cycles / kCyclesPerBusyMiss) + 1;
    }

    /// One cycle in this many may be lost to the machine in a window judged
    /// by share: 2 %. The worst burst measured was 8 in 1057 cycles (0.8 %);
    /// the least a steady defect costs is one in eight (12.5 %).
    static constexpr std::uint64_t kCyclesPerBusyMiss = 50;

    /// The cycles the reading itself cost, for a failure message to carry.
    [[nodiscard]] std::uint64_t lost() const
    {
        return m_lost;
    }

    /// The worst cycle of the reading, as a fraction of its own period.
    [[nodiscard]] float peakLoad() const
    {
        return m_peakLoad;
    }

private:
    /// The rate just measured, over \a over.
    [[nodiscard]] std::uint64_t measuredFor(std::chrono::milliseconds over) const
    {
        const double rate = static_cast<double>(m_lost) / static_cast<double>(m_window.count());
        return static_cast<std::uint64_t>(std::ceil(rate * static_cast<double>(over.count())));
    }

    std::chrono::milliseconds m_window;
    std::uint64_t m_lost = 0;
    float m_peakLoad = 0.0f;
};

/// The session the tests run: a source block that carries the node's input
/// ports, and a gain block whose outputs are the node's output ports.
///
/// The blocks live here rather than in the graph, which is what lets a
/// handover build a new plan around the same state.
class TestSession
{
public:
    test::PassThroughSource source{2};
    test::PassThroughSource second{2};
    test::GainBlock gain;

    [[nodiscard]] static SessionNodeSpec spec(const QString &id, const QString &name)
    {
        SessionNodeSpec result;
        result.sessionId = id;
        result.sessionName = name;
        result.sampleRate = kRate;
        SessionLane input;
        input.blockId = u"src"_s;
        input.layout = dsp::stereoLayout();
        SessionLane output;
        output.blockId = u"gain"_s;
        output.layout = dsp::stereoLayout();
        result.inputs.append(input);
        result.outputs.append(output);
        return result;
    }

    /// A graph of the two blocks, prepared for \a sampleRate.
    [[nodiscard]] SessionGraph compile(double sampleRate = kRate)
    {
        graph::GraphSpec spec;
        spec.sampleRate = sampleRate;
        graph::NodeSpec sourceNode;
        sourceNode.id = "src";
        sourceNode.block = &source;
        graph::NodeSpec gainNode;
        gainNode.id = "gain";
        gainNode.block = &gain;
        spec.nodes.push_back(sourceNode);
        spec.nodes.push_back(gainNode);
        for (std::uint32_t channel = 0; channel < 2; ++channel) {
            graph::EdgeSpec edge;
            edge.fromNode = 0;
            edge.fromPort = channel;
            edge.toNode = 1;
            edge.toPort = channel;
            spec.edges.push_back(edge);
        }

        dsp::PrepareSpec prepare;
        prepare.sampleRate = sampleRate;
        prepare.maxBlockFrames = dsp::kMaxBlockFrames;
        prepare.inputChannels = 2;
        prepare.outputChannels = 2;
        EXPECT_TRUE(source.prepare(prepare).has_value());
        EXPECT_TRUE(gain.prepare(prepare).has_value());

        std::expected<graph::CompiledGraph, graph::GraphError> compiled = graph::CompiledGraph::compile(spec);
        EXPECT_TRUE(compiled.has_value());

        SessionGraph result;
        if (compiled) {
            result.graph = std::make_unique<graph::CompiledGraph>(std::move(*compiled));
        }
        result.sampleRate = sampleRate;
        result.paramTargets = {&source, &gain};
        return result;
    }

    /// A session with a second source, so that two blocks have a port on
    /// the same channel and the channel alone no longer says which one.
    [[nodiscard]] static SessionNodeSpec specWithTwoSources(const QString &id, const QString &name)
    {
        SessionNodeSpec result = spec(id, name);
        SessionLane extra;
        extra.blockId = u"src2"_s;
        extra.layout = dsp::stereoLayout();
        result.inputs.append(extra);
        return result;
    }

    /// Both sources into the same gain, which is where the compiler puts a
    /// hidden sum.
    [[nodiscard]] SessionGraph compileWithTwoSources(double sampleRate = kRate)
    {
        graph::GraphSpec spec;
        spec.sampleRate = sampleRate;
        graph::NodeSpec first;
        first.id = "src";
        first.block = &source;
        graph::NodeSpec extra;
        extra.id = "src2";
        extra.block = &second;
        graph::NodeSpec gainNode;
        gainNode.id = "gain";
        gainNode.block = &gain;
        spec.nodes.push_back(first);
        spec.nodes.push_back(extra);
        spec.nodes.push_back(gainNode);
        for (std::size_t from = 0; from < 2; ++from) {
            for (std::uint32_t channel = 0; channel < 2; ++channel) {
                graph::EdgeSpec edge;
                edge.fromNode = from;
                edge.fromPort = channel;
                edge.toNode = 2;
                edge.toPort = channel;
                spec.edges.push_back(edge);
            }
        }

        dsp::PrepareSpec prepare;
        prepare.sampleRate = sampleRate;
        prepare.maxBlockFrames = dsp::kMaxBlockFrames;
        prepare.inputChannels = 2;
        prepare.outputChannels = 2;
        EXPECT_TRUE(source.prepare(prepare).has_value());
        EXPECT_TRUE(second.prepare(prepare).has_value());
        EXPECT_TRUE(gain.prepare(prepare).has_value());

        std::expected<graph::CompiledGraph, graph::GraphError> compiled = graph::CompiledGraph::compile(spec);
        EXPECT_TRUE(compiled.has_value());

        SessionGraph result;
        if (compiled) {
            result.graph = std::make_unique<graph::CompiledGraph>(std::move(*compiled));
        }
        result.sampleRate = sampleRate;
        result.paramTargets = {&source, &second, &gain};
        return result;
    }

    /// Where the gain block sits in paramTargets.
    static constexpr std::uint32_t kGainSlot = 1;
};

/// Asks the server to hold the quantum or the rate still, the way a user who
/// has set them in the settings would. Puts them back on the way out.
class ForcedClock
{
public:
    void setQuantum(int frames)
    {
        m_quantum = frames;
        (void)runCommand({"pw-metadata", "-n", "settings", "0", "clock.force-quantum", std::to_string(frames)});
    }

    void setRate(int rate)
    {
        m_rate = rate;
        (void)runCommand({"pw-metadata", "-n", "settings", "0", "clock.force-rate", std::to_string(rate)});
    }

    ~ForcedClock()
    {
        if (m_quantum != 0) {
            (void)runCommand({"pw-metadata", "-n", "settings", "0", "clock.force-quantum", "0"});
        }
        if (m_rate != 0) {
            (void)runCommand({"pw-metadata", "-n", "settings", "0", "clock.force-rate", "0"});
        }
    }

    ForcedClock() = default;
    ForcedClock(const ForcedClock &) = delete;
    ForcedClock &operator=(const ForcedClock &) = delete;

private:
    int m_quantum = 0;
    int m_rate = 0;
};

/// A capture node of two channels, filled in plainly: designated
/// initialisers on a struct with more fields than the test sets are a
/// warning, and the warnings are errors.
[[nodiscard]] inline CaptureNodeOptions captureOptions(const std::string &name, double sineHz = 0.0)
{
    CaptureNodeOptions options;
    options.name = name;
    options.channels = {"FL", "FR"};
    options.sineHz = sineHz;
    return options;
}

[[nodiscard]] inline FakeAppOptions appOptions(const std::string &name, const std::string &binary)
{
    FakeAppOptions options;
    options.applicationName = name;
    options.binary = binary;
    options.target = kStereoSink;
    return options;
}

/// A session of two independent lanes that a test can edit while it runs.
///
/// Each lane is a source whose ports the node publishes, a chain of effects
/// that a test can lengthen and shorten, and a last block whose outputs the
/// node publishes. **The two ends stay put**, so the ports of a lane, and
/// the links that end on them, survive any number of edits to its middle —
/// which is what lets a test listen to a lane while it is being edited. A
/// whole lane can be switched off instead, and then its ports do come and
/// go.
///
/// Two lanes is the smallest arrangement that can show an edit to one
/// leaving the other alone, which is what fading by lane is for.
class EditableSession
{
public:
    static constexpr int kLanes = 2;
    /// The most effects a lane will hold, so the blocks can all be made up
    /// front and an edit never has to build one.
    static constexpr int kEffectPool = 6;

    EditableSession()
    {
        for (int lane = 0; lane < kLanes; ++lane) {
            m_sources.push_back(std::make_unique<test::PassThroughSource>(2));
            // Unity, so a lane with nothing in its middle passes the sound
            // through at the level it arrived.
            m_tails.push_back(std::make_unique<test::FixedGainEffect>(1.0f, 2));
            for (int effect = 0; effect < kEffectPool; ++effect) {
                // Each one is plainly audible, so an edit can be heard.
                m_effects[lane].push_back(std::make_unique<test::FixedGainEffect>(0.5f, 2));
            }
            m_chain[lane] = 0;
            m_active[lane] = true;
        }
    }

    EditableSession(const EditableSession &) = delete;
    EditableSession &operator=(const EditableSession &) = delete;

    [[nodiscard]] static QString sourceId(int lane)
    {
        return u"src%1"_s.arg(lane);
    }

    [[nodiscard]] QString effectId(int lane, int index) const
    {
        return u"fx%1_%2"_s.arg(lane).arg(index);
    }

    /// The block whose outputs the node publishes for this lane, whatever
    /// has been done to the middle of it.
    [[nodiscard]] static QString outputId(int lane)
    {
        return u"out%1"_s.arg(lane);
    }

    [[nodiscard]] int effectCount(int lane) const
    {
        return m_chain[lane];
    }

    [[nodiscard]] bool isActive(int lane) const
    {
        return m_active[lane];
    }

    /// Switches a whole lane on or off. Its ports go with it, which is the
    /// case a port has to be added to, or taken out of, a running node.
    void setLaneActive(int lane, bool active)
    {
        m_active[lane] = active;
    }

    /// Adds an effect to the end of a lane, or takes the last one off.
    /// Returns false when the lane is already as long, or as short, as it
    /// goes.
    bool addEffect(int lane)
    {
        if (!m_active[lane] || m_chain[lane] >= kEffectPool) {
            return false;
        }
        ++m_chain[lane];
        return true;
    }

    bool removeEffect(int lane)
    {
        if (!m_active[lane] || m_chain[lane] == 0) {
            return false;
        }
        --m_chain[lane];
        return true;
    }

    [[nodiscard]] QList<SessionLane> inputLanes() const
    {
        QList<SessionLane> lanes;
        for (int lane = 0; lane < kLanes; ++lane) {
            if (!m_active[lane]) {
                continue;
            }
            SessionLane entry;
            entry.blockId = sourceId(lane);
            entry.layout = dsp::stereoLayout();
            lanes.append(entry);
        }
        return lanes;
    }

    [[nodiscard]] QList<SessionLane> outputLanes() const
    {
        QList<SessionLane> lanes;
        for (int lane = 0; lane < kLanes; ++lane) {
            if (!m_active[lane]) {
                continue;
            }
            SessionLane entry;
            entry.blockId = outputId(lane);
            entry.layout = dsp::stereoLayout();
            lanes.append(entry);
        }
        return lanes;
    }

    /// What the lanes add up to, as the graph compiler wants it.
    [[nodiscard]] graph::GraphSpec spec(double sampleRate = kRate) const
    {
        graph::GraphSpec result;
        result.sampleRate = sampleRate;
        for (int lane = 0; lane < kLanes; ++lane) {
            if (!m_active[lane]) {
                continue;
            }
            // source, then the middle, then the block the lane ends at.
            const std::size_t first = result.nodes.size();
            graph::NodeSpec source;
            source.id = sourceId(lane).toStdString();
            source.block = m_sources[static_cast<std::size_t>(lane)].get();
            result.nodes.push_back(source);
            for (int effect = 0; effect < m_chain[lane]; ++effect) {
                graph::NodeSpec node;
                node.id = effectId(lane, effect).toStdString();
                node.block = m_effects[lane][static_cast<std::size_t>(effect)].get();
                result.nodes.push_back(node);
            }
            graph::NodeSpec tail;
            tail.id = outputId(lane).toStdString();
            tail.block = m_tails[static_cast<std::size_t>(lane)].get();
            result.nodes.push_back(tail);

            for (std::size_t step = 0; step + 1 < result.nodes.size() - first; ++step) {
                for (std::uint32_t channel = 0; channel < 2; ++channel) {
                    graph::EdgeSpec edge;
                    edge.fromNode = first + step;
                    edge.fromPort = channel;
                    edge.toNode = first + step + 1;
                    edge.toPort = channel;
                    result.edges.push_back(edge);
                }
            }
        }
        return result;
    }

    /// Prepares every block that is in the graph and compiles it.
    [[nodiscard]] SessionGraph compile(double sampleRate = kRate)
    {
        const graph::GraphSpec wanted = spec(sampleRate);

        dsp::PrepareSpec prepare;
        prepare.sampleRate = sampleRate;
        prepare.maxBlockFrames = dsp::kMaxBlockFrames;
        prepare.inputChannels = 2;
        prepare.outputChannels = 2;
        SessionGraph result;
        for (const graph::NodeSpec &node : wanted.nodes) {
            if (sampleRate != m_preparedAt) {
                EXPECT_TRUE(node.block->prepare(prepare).has_value()) << node.id;
            }
            result.paramTargets.push_back(node.block);
        }
        m_preparedAt = sampleRate;

        std::expected<graph::CompiledGraph, graph::GraphError> compiled = graph::CompiledGraph::compile(wanted);
        EXPECT_TRUE(compiled.has_value());
        if (compiled) {
            result.graph = std::make_unique<graph::CompiledGraph>(std::move(*compiled));
        }
        result.sampleRate = sampleRate;
        return result;
    }

    /// Everything a block was prepared for is thrown away, so the next
    /// compile prepares it again. That is what a change of rate needs.
    void forgetPreparation()
    {
        m_preparedAt = 0.0;
    }

private:
    std::vector<std::unique_ptr<test::PassThroughSource>> m_sources;
    std::vector<std::unique_ptr<test::FixedGainEffect>> m_tails;
    std::array<std::vector<std::unique_ptr<test::FixedGainEffect>>, kLanes> m_effects;
    std::array<int, kLanes> m_chain{};
    std::array<bool, kLanes> m_active{};
    double m_preparedAt = 0.0;
};

/// The fixture: a Qt event loop for the engine, and the harness's own
/// checks around every test.
class SessionTest : public PipeWireTest
{
protected:
    void SetUp() override;
    void TearDown() override;

private:
    std::unique_ptr<QCoreApplication> m_app;
};

} // namespace rb::pwtest
