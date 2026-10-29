// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The session processing node, run inside the private PipeWire
// instance. What the node puts out is measured by a capture filter that
// shares no code with the engine, and the graph itself is judged by pw-dump.

#include "sessionharness.h"

#include <QSignalSpy>
#include <gtest/gtest.h>
#include <sys/resource.h>

#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace rb;
using namespace rb::pw;
using namespace rb::pwtest;
using namespace Qt::StringLiterals;

using SessionNodes = SessionTest;

TEST_F(SessionNodes, TheNodeCarriesItsNameItsPortsAndNoMediaClass)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilConnected());

    TestSession session;
    SessionNode node(engine.connection());
    ASSERT_TRUE(node.start(TestSession::spec(u"11111111-2222"_s, u"Podcast"_s)).has_value());
    EXPECT_EQ(node.nodeName(), u"reverblabr.11111111-2222.seg0"_s);

    const std::optional<Graph::Node> dumped = engine.awaitNode(u"reverblabr.11111111-2222.seg0"_s);
    ASSERT_TRUE(dumped.has_value());
    ASSERT_TRUE(engine.awaitPorts(dumped->id, "input", 2));
    ASSERT_TRUE(engine.awaitPorts(dumped->id, "output", 2));

    EXPECT_EQ(dumped->prop("node.description"), "Reverblabr — Podcast");
    EXPECT_EQ(dumped->prop("application.name"), "Reverblabr");
    EXPECT_EQ(dumped->prop("node.autoconnect"), "false");
    EXPECT_EQ(dumped->prop("node.always-process"), "true");
    // The policy must leave the node alone: it is neither a device nor an
    // application stream ([ADR-0007]).
    EXPECT_EQ(dumped->prop("media.class"), "");
    EXPECT_EQ(dumped->prop("media.role"), "");

    // By name, not by position: ports come back in port-id order and the
    // server does not promise to number them in the order they were made.
    const Graph graph = Graph::capture();
    const std::vector<Graph::Port> inputs = graph.portsOf(dumped->id, "input");
    const std::vector<Graph::Port> outputs = graph.portsOf(dumped->id, "output");
    ASSERT_EQ(inputs.size(), 2U);
    ASSERT_EQ(outputs.size(), 2U);
    const auto named = [](const std::vector<Graph::Port> &ports, const std::string &name) {
        return std::ranges::find_if(ports, [&name](const Graph::Port &port) { return port.prop("port.name") == name; });
    };
    for (const auto &[name, channel] :
         std::vector<std::pair<std::string, std::string>>{{"src:in_FL", "FL"}, {"src:in_FR", "FR"}}) {
        const auto port = named(inputs, name);
        ASSERT_NE(port, inputs.end()) << name;
        EXPECT_EQ(port->channel, channel);
    }
    for (const std::string &name : std::vector<std::string>{"gain:out_FL", "gain:out_FR"}) {
        const auto port = named(outputs, name);
        ASSERT_NE(port, outputs.end()) << name;
        EXPECT_EQ(port->prop("format.dsp"), "32 bit float mono audio");
    }

    // The server gives the node its id a round trip after the filter is
    // connected, so it is not there the moment start() returns.
    ASSERT_TRUE(engine.pumpUntil([&] { return node.nodeId() != 0; })) << "the node never learned its own id";
    EXPECT_EQ(node.nodeId(), dumped->id);
}

TEST_F(SessionNodes, TheProcessCallbackRunsWithNothingLinkedToIt)
{
    // node.always-process: meters and generators have to keep running with
    // nothing linked, or a session would look dead until it was routed.
    Pump engine;
    ASSERT_TRUE(engine.waitUntilConnected());

    TestSession session;
    SessionNode node(engine.connection());
    ASSERT_TRUE(node.start(TestSession::spec(u"22222222-3333"_s, u"Idle"_s)).has_value());
    ASSERT_TRUE(node.setGraph(session.compile()).has_value());

    ASSERT_TRUE(engine.pumpUntil([&] { return node.telemetry().cycles > 0; }));
    const std::uint64_t first = node.telemetry().cycles;
    // The first window of the two is the machine's reading and the second
    // is the one judged, so the lost cycles below are the node's and not
    // the load's.
    const MachineCost machine(engine, node, std::chrono::milliseconds{300});
    const std::uint64_t xrunsBefore = node.telemetry().xruns;
    engine.pump(std::chrono::milliseconds{300});
    const SessionTelemetry telemetry = node.telemetry();

    EXPECT_GT(telemetry.cycles, first) << "the node stopped running when nothing was linked to it";
    EXPECT_EQ(engine.connection().reconnectCount(), 0U)
        << "the connection was lost under the node, which takes the node with it";
    EXPECT_EQ(telemetry.sourcePresent, 0U) << "an unlinked lane must not say a source is present";
    EXPECT_EQ(telemetry.hasGraph, 1U);
    EXPECT_LE(telemetry.xruns - xrunsBefore, machine.allowanceFor(std::chrono::milliseconds{300}))
        << "a node with nothing linked to it lost cycles the machine was not losing anyway";
}

TEST_F(SessionNodes, AnUnlinkedInputReadsAsSilenceAtTheOutput)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilConnected());

    TestSession session;
    SessionNode node(engine.connection());
    ASSERT_TRUE(node.start(TestSession::spec(u"33333333-4444"_s, u"Silent"_s)).has_value());
    ASSERT_TRUE(node.setGraph(session.compile()).has_value());

    const std::optional<Graph::Node> ours = engine.awaitNode(node.nodeName());
    ASSERT_TRUE(ours.has_value());
    ASSERT_TRUE(engine.awaitPorts(ours->id, "output", 2));

    CaptureNode capture(captureOptions("rbtest.capture.silence"));
    const std::optional<Graph::Node> captureNode = capture.waitUntilReady();
    ASSERT_TRUE(captureNode.has_value());
    ASSERT_EQ(linkByChannel(ours->id, captureNode->id), 2U);

    engine.pump(std::chrono::milliseconds{400});
    const std::map<std::string, double> levels = capture.measure(300ms);

    ASSERT_TRUE(levels.contains("FL"));
    EXPECT_LT(levels.at("FL"), -150.0) << "an unconnected input port has to read as silence";
    EXPECT_LT(levels.at("FR"), -150.0);
}

TEST_F(SessionNodes, ATestToneArrivesAtTheGainItWasGiven)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilConnected());

    TestSession session;
    SessionNode node(engine.connection());
    ASSERT_TRUE(node.start(TestSession::spec(u"44444444-5555"_s, u"Tone"_s)).has_value());
    ASSERT_TRUE(node.setGraph(session.compile()).has_value());

    const std::optional<Graph::Node> ours = engine.awaitNode(node.nodeName());
    ASSERT_TRUE(ours.has_value());
    ASSERT_TRUE(engine.awaitPorts(ours->id, "input", 2));
    ASSERT_TRUE(engine.awaitPorts(ours->id, "output", 2));

    FakeApp app(appOptions("ToneApp", "toneapp"));
    const std::optional<Graph::Node> stream = engine.awaitMatch(playbackStreamOf("ToneApp"));
    ASSERT_TRUE(stream.has_value());
    ASSERT_TRUE(engine.awaitPorts(stream->id, "output", 2));

    CaptureNode capture(captureOptions("rbtest.capture.tone"));
    const std::optional<Graph::Node> captureNode = capture.waitUntilReady();
    ASSERT_TRUE(captureNode.has_value());

    ASSERT_EQ(linkByChannel(stream->id, ours->id), 2U);
    ASSERT_EQ(linkByChannel(ours->id, captureNode->id), 2U);
    engine.pump(std::chrono::milliseconds{300});

    const std::map<std::string, double> unity = capture.measureSettled();
    ASSERT_TRUE(unity.contains("FL"));
    EXPECT_NEAR(unity.at("FL"), kTonePeakDb, 0.1) << "the tone did not come through at the level it went in at";
    EXPECT_NEAR(unity.at("FR"), kTonePeakDb, 0.1);
    EXPECT_GT(node.telemetry().sourcePresent, 0U) << "the linked lane has to say its source is there";

    // The same tone through a gain of -6 dB. The absolute level depends on
    // the tone file; what the block did is the difference between the two
    // measurements, and that is what has to be right to 0.05 dB.
    ParamChange change;
    change.target = TestSession::kGainSlot;
    change.index = test::GainBlock::GainDb;
    change.value = -6.0f;
    ASSERT_TRUE(node.queueParam(change));
    engine.pump(std::chrono::milliseconds{200});

    const std::map<std::string, double> quieter = capture.measureSettled();
    ASSERT_TRUE(quieter.contains("FL"));
    EXPECT_NEAR(unity.at("FL") - quieter.at("FL"), 6.0, 0.05);
    EXPECT_NEAR(unity.at("FR") - quieter.at("FR"), 6.0, 0.05);
    EXPECT_NEAR(quieter.at("FL"), kTonePeakDb - 6.0, 0.1);
}

TEST_F(SessionNodes, StopTakesTheNodeItsPortsAndItsLinksOutOfTheGraph)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilConnected());

    TestSession session;
    auto node = std::make_unique<SessionNode>(engine.connection());
    ASSERT_TRUE(node->start(TestSession::spec(u"55555555-6666"_s, u"Stopping"_s)).has_value());
    ASSERT_TRUE(node->setGraph(session.compile()).has_value());

    const std::optional<Graph::Node> ours = engine.awaitNode(node->nodeName());
    ASSERT_TRUE(ours.has_value());
    ASSERT_TRUE(engine.awaitPorts(ours->id, "output", 2));

    CaptureNode capture(captureOptions("rbtest.capture.stopping"));
    const std::optional<Graph::Node> captureNode = capture.waitUntilReady();
    ASSERT_TRUE(captureNode.has_value());
    ASSERT_EQ(linkByChannel(ours->id, captureNode->id), 2U);
    ASSERT_TRUE(engine.awaitLink(ours->id, captureNode->id));

    const std::uint32_t nodeId = ours->id;

    // **What is being timed is the graph, not the measuring.** Asking
    // pw-dump costs a process, tens of milliseconds here and a fifth of a
    // second on a loaded machine, which would go on the bill. The registry
    // announces the removals as events instead, so the clock stops when the
    // server says our node, its ports and its links are gone.
    const auto nothingOfOursLeft = [&] {
        return !engine.mirror().node(nodeId).has_value()
            && engine.mirror().portsOf(nodeId, PortDirection::Input).isEmpty()
            && engine.mirror().portsOf(nodeId, PortDirection::Output).isEmpty()
            && engine.mirror().linksOf(nodeId).isEmpty();
    };
    ASSERT_FALSE(nothingOfOursLeft()) << "the graph was already empty of us before Stop";

    QElapsedTimer timer;
    timer.start();
    node->stop();

    ASSERT_TRUE(engine.pumpUntil(nothingOfOursLeft, std::chrono::seconds{2}))
        << "something of ours was still in the graph two seconds after Stop";
    const qint64 elapsed = timer.elapsed();
    std::cerr << "the graph was clean " << elapsed << " ms after Stop\n";
    EXPECT_LT(elapsed, 100) << "the graph took longer than 100 ms to come clean";

    // And pw-dump, which watches the same graph through none of our code,
    // has the last word on whether it really is clean. It is not on the
    // clock: how long it takes to ask says nothing about the graph.
    const Graph after = Graph::capture();
    EXPECT_EQ(after.node(nodeId), nullptr);
    EXPECT_TRUE(after.portsOf(nodeId, "input").empty());
    EXPECT_TRUE(after.portsOf(nodeId, "output").empty());
    EXPECT_FALSE(after.linked(nodeId, captureNode->id));

    EXPECT_FALSE(node->isRunning());
    node.reset();
}

TEST_F(SessionNodes, AGraphHandedOverWhileTheNodeRunsDoesNotBreakTheSound)
{
    // ADR-0008: the swap is a blocking invoke on the data loop that only
    // exchanges pointers. What matters here is that the sound carries on
    // through it, and that the blocks keep their state.
    Pump engine;
    ASSERT_TRUE(engine.waitUntilConnected());

    TestSession session;
    SessionNode node(engine.connection());
    ASSERT_TRUE(node.start(TestSession::spec(u"66666666-7777"_s, u"Handover"_s)).has_value());
    ASSERT_TRUE(node.setGraph(session.compile()).has_value());

    const std::optional<Graph::Node> ours = engine.awaitNode(node.nodeName());
    ASSERT_TRUE(ours.has_value());
    ASSERT_TRUE(engine.awaitPorts(ours->id, "input", 2));
    ASSERT_TRUE(engine.awaitPorts(ours->id, "output", 2));

    FakeApp app(appOptions("HandoverApp", "handoverapp"));
    const std::optional<Graph::Node> stream = engine.awaitMatch(playbackStreamOf("HandoverApp"));
    ASSERT_TRUE(stream.has_value());
    ASSERT_TRUE(engine.awaitPorts(stream->id, "output", 2));

    CaptureNode capture(captureOptions("rbtest.capture.handover", 1000.0));
    const std::optional<Graph::Node> captureNode = capture.waitUntilReady();
    ASSERT_TRUE(captureNode.has_value());
    ASSERT_EQ(linkByChannel(stream->id, ours->id), 2U);
    ASSERT_EQ(linkByChannel(ours->id, captureNode->id), 2U);
    engine.pump(std::chrono::milliseconds{500});

    const std::map<std::string, SineCheck> before = capture.measureSine(300ms);
    ASSERT_TRUE(before.contains("FL"));
    ASSERT_LT(before.at("FL").residualDb, -80.0) << "the tone was not clean before the swaps began";

    const std::uint64_t cyclesBefore = node.telemetry().cycles;
    for (int swap = 0; swap < 200; ++swap) {
        ASSERT_TRUE(node.setGraph(session.compile()).has_value()) << "swap " << swap;
        engine.pump(std::chrono::milliseconds{2});
    }
    EXPECT_GT(node.telemetry().cycles, cyclesBefore);

    const std::map<std::string, SineCheck> after = capture.measureSine(300ms);
    ASSERT_TRUE(after.contains("FL"));
    EXPECT_LT(after.at("FL").residualDb, -80.0) << "the swaps left a click in the sound";
    EXPECT_EQ(after.at("FL").nullBuffers, 0U);
    EXPECT_NEAR(after.at("FL").peakDb, before.at("FL").peakDb, 0.1);
}

TEST_F(SessionNodes, TheLoadAndTheXrunCounterAreReported)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilConnected());

    ForcedClock clock;
    clock.setQuantum(256);

    TestSession session;
    SessionNode node(engine.connection());
    ASSERT_TRUE(node.start(TestSession::spec(u"77777777-8888"_s, u"Soak"_s)).has_value());
    ASSERT_TRUE(node.setGraph(session.compile()).has_value());

    const std::optional<Graph::Node> ours = engine.awaitNode(node.nodeName());
    ASSERT_TRUE(ours.has_value());
    ASSERT_TRUE(engine.awaitPorts(ours->id, "input", 2));
    ASSERT_TRUE(engine.awaitPorts(ours->id, "output", 2));

    FakeApp app(appOptions("SoakApp", "soakapp"));
    const std::optional<Graph::Node> stream = engine.awaitMatch(playbackStreamOf("SoakApp"));
    ASSERT_TRUE(stream.has_value());
    ASSERT_TRUE(engine.awaitPorts(stream->id, "output", 2));

    CaptureNode capture(captureOptions("rbtest.capture.soak"));
    const std::optional<Graph::Node> captureNode = capture.waitUntilReady();
    ASSERT_TRUE(captureNode.has_value());
    ASSERT_EQ(linkByChannel(stream->id, ours->id), 2U);
    ASSERT_EQ(linkByChannel(ours->id, captureNode->id), 2U);

    ASSERT_TRUE(engine.pumpUntil([&] { return node.telemetry().frames == 256; }))
        << "the quantum never settled at 256 frames";
    ASSERT_TRUE(engine.pumpUntil([&] { return node.telemetry().sourcePresent > 0; }))
        << "the lane the tone arrives on never said its source was there";
    // What the machine costs this session before the soak begins, at the
    // quantum the soak runs at. Both figures the soak ends on are judged
    // against it rather than against nothing at all: under
    // `ctest -j8`, or beside a second pipeline on a CI runner, a cycle is
    // lost to the scheduler and no code is at fault. It leaves the
    // peak-load figure reset, which is what the soak needs anyway.
    const MachineCost machine(engine, node);
    const std::uint64_t xrunsBefore = node.telemetry().xruns;
    const std::uint64_t cyclesBefore = node.telemetry().cycles;

    // The fake application plays a minute of tone and stops, so a longer soak
    // starts it again and links it up afresh. That the sound keeps arriving
    // is the point: a soak through silence would prove much less.
    const auto playAgain = [&] {
        app.restart();
        const std::optional<Graph::Node> fresh = engine.awaitMatch(playbackStreamOf("SoakApp"));
        return fresh.has_value() && engine.awaitPorts(fresh->id, "output", 2)
            && linkByChannel(fresh->id, ours->id) == 2U;
    };

    const int seconds = soakSeconds();
    int restarts = 0;
    QElapsedTimer soak;
    soak.start();
    while (soak.elapsed() < static_cast<qint64>(seconds) * 1000) {
        engine.pump(std::chrono::milliseconds{500});
        // The lane going quiet is the sign, not the process: pw-cat can be
        // done with the file while its process is still winding down.
        if (node.telemetry().sourcePresent == 0) {
            ASSERT_TRUE(playAgain()) << "the tone could not be started again after restart " << restarts;
            ++restarts;
        }
    }

    const qint64 took = soak.elapsed();
    const SessionTelemetry telemetry = node.telemetry();
    EXPECT_EQ(telemetry.frames, 256U);
    EXPECT_GT(telemetry.cycles - cyclesBefore, static_cast<std::uint64_t>(seconds) * 100)
        << "the node did not run for the whole of the soak";
    // A node that loses a cycle whenever something happens loses one at
    // every restart of the tone, and one that has stopped keeping up loses
    // them by the thousand; neither fits inside the machine's own rate.
    const std::uint64_t lost = telemetry.xruns - xrunsBefore;
    const std::uint64_t allowed = machine.allowanceFor(std::chrono::milliseconds{took});
    EXPECT_LE(lost, allowed) << lost << " cycles were lost in " << seconds << " s at quantum 256, where " << allowed
                             << " were allowed (the machine lost " << machine.lost() << " in the second before)";
    EXPECT_EQ(engine.connection().reconnectCount(), 0U) << "the connection was lost during the soak";
    EXPECT_GT(telemetry.peakLoad, 0.0f) << "the load was never measured";
    // **A cycle may not take longer than the time it had** — unless the
    // machine was already that late with nothing being asked of it, when
    // the figure is the scheduler's and not the graph's. That is the same
    // reading the lost cycles are judged against.
    EXPECT_LT(telemetry.peakLoad, std::max(1.0f, machine.peakLoad()))
        << "a cycle took longer than the time it had, and the machine was not already doing that";
    std::cerr << "DSP load over " << seconds << " s at quantum 256: peak " << telemetry.peakLoad * 100.0f << " %, last "
              << telemetry.lastLoad * 100.0f << " %, " << telemetry.cycles - cyclesBefore << " cycles, "
              << telemetry.xruns - xrunsBefore << " xruns, " << restarts << " restarts of the tone\n";
}

namespace
{

/// The test session with a block in its middle that can be told to hold the
/// audio thread. Both ends are the blocks TestSession::spec names, so the
/// node, its lanes and its ports are the ordinary ones and only the middle
/// is unusual.
class StallingSession
{
public:
    test::PassThroughSource source{2};
    test::StallingEffect stall{2};
    test::GainBlock gain;

    [[nodiscard]] SessionGraph compile(double sampleRate = kRate)
    {
        graph::GraphSpec spec;
        spec.sampleRate = sampleRate;
        graph::NodeSpec sourceNode;
        sourceNode.id = "src";
        sourceNode.block = &source;
        graph::NodeSpec stallNode;
        stallNode.id = "stall";
        stallNode.block = &stall;
        graph::NodeSpec gainNode;
        gainNode.id = "gain";
        gainNode.block = &gain;
        spec.nodes.push_back(sourceNode);
        spec.nodes.push_back(stallNode);
        spec.nodes.push_back(gainNode);
        for (std::size_t step = 0; step + 1 < spec.nodes.size(); ++step) {
            for (std::uint32_t channel = 0; channel < 2; ++channel) {
                graph::EdgeSpec edge;
                edge.fromNode = step;
                edge.fromPort = channel;
                edge.toNode = step + 1;
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
        EXPECT_TRUE(stall.prepare(prepare).has_value());
        EXPECT_TRUE(gain.prepare(prepare).has_value());

        std::expected<graph::CompiledGraph, graph::GraphError> compiled = graph::CompiledGraph::compile(spec);
        EXPECT_TRUE(compiled.has_value());
        SessionGraph result;
        if (compiled) {
            result.graph = std::make_unique<graph::CompiledGraph>(std::move(*compiled));
        }
        result.sampleRate = sampleRate;
        result.paramTargets = {&source, &stall, &gain};
        return result;
    }
};

} // namespace

TEST_F(SessionNodes, ANodeThatReallyMissesCyclesSaysHowMany)
{
    // **The counter has to move when cycles are genuinely lost.** It sat at
    // approximately zero whatever happened for as long as it did because
    // nothing here ever lost a cycle on purpose and watched it: a
    // node that skips one cycle in eight destroys the sound and was
    // reporting a clean run. So one is skipped here, against a real server.
    Pump engine;
    ASSERT_TRUE(engine.waitUntilConnected());

    // At the quantum the server picks for an idle graph a cycle is 21 ms
    // long, and a node would have to be held for longer than that to be
    // late at all. Held at 256 frames, a cycle is 5.3 ms and the stall
    // below is plainly over the deadline.
    ForcedClock clock;
    clock.setQuantum(256);

    StallingSession session;
    SessionNode node(engine.connection());
    ASSERT_TRUE(node.start(TestSession::spec(u"77777777-8888"_s, u"Stalling"_s)).has_value());
    ASSERT_TRUE(node.setGraph(session.compile()).has_value());
    ASSERT_TRUE(engine.pumpUntil([&] { return node.telemetry().frames == 256; }))
        << "the quantum never settled at 256 frames";

    // What the machine is costing the session while nothing is wrong with
    // it, which is what the injected defect has to stand out against.
    const MachineCost machine(engine, node, std::chrono::milliseconds{500});
    const SessionTelemetry before = node.telemetry();

    // One cycle in eight held for 20 ms, which is several cycles' worth at
    // any quantum this server runs: the graph goes on without the node.
    session.stall.holdEvery(8, 20000);
    engine.pump(std::chrono::milliseconds{2000});
    const SessionTelemetry during = node.telemetry();
    session.stall.holdEvery(0, 0);

    ASSERT_GT(session.stall.heldCycles(), 10U) << "the defect was never injected: the block was not in the graph";
    const std::uint64_t counted = during.xruns - before.xruns;
    const std::uint64_t lost = during.lostCycles - before.lostCycles;
    EXPECT_GT(counted, machine.allowanceFor(std::chrono::milliseconds{2000}))
        << "a node held for 20 ms in every eighth cycle reported " << counted << " missed cycles, where the machine"
        << " was costing it " << machine.lost() << " in the half second before";
    EXPECT_GT(lost, 0U) << "the graph ran cycles this node did not, and the count of them stayed at zero";
    std::cerr << "a stall of 20 ms in every eighth cycle: " << counted << " occasions, " << lost << " cycles lost over "
              << during.cycles - before.cycles << " cycles run, " << session.stall.heldCycles() << " of them held\n";

    // And the figure is the defect's, not the test's: with the block idle
    // again the session goes back to losing nothing.
    engine.pump(std::chrono::milliseconds{500});
    const SessionTelemetry settled = node.telemetry();
    engine.pump(std::chrono::milliseconds{1000});
    EXPECT_LE(node.telemetry().xruns - settled.xruns, machine.allowanceFor(std::chrono::milliseconds{1000}))
        << "the session went on losing cycles after the stall was taken out of it";
}

// **A session that overruns every cycle gives way, and the process lives**.
// rtkit holds the data loop to RLIMIT_RTTIME, 200 ms of CPU without
// a sleep, and a loop whose every cycle overruns never sleeps: a block held
// 6 ms in every 5.3 ms cycle had the kernel kill the process four times out
// of four. Held for three seconds here, the session gives way, says
// which block cost it, and the test is still running to check.
//
// **Only a real-time thread gives way**: rtkit refuses real-time once
// a user has asked too often too quickly (its burst limit, which a busy gate
// reaches), and the thread it refused is held to no limit at all. Then the
// session rightly goes on, and says it ran long without being real-time.
// The test asks the node which it was, and says so. pw.give-way-no-realtime
// runs it with RB_PW_NO_REALTIME, so the second case is always tried.
TEST_F(SessionNodes, ASessionOverrunningEveryCycleGivesWayAndTheProcessLives)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilConnected());
    ForcedClock clock;
    clock.setQuantum(256);
    StallingSession session;
    SessionNode node(engine.connection());
    ASSERT_TRUE(node.start(TestSession::spec(u"77777777-9999"_s, u"Overrunning"_s)).has_value());
    ASSERT_TRUE(node.setGraph(session.compile()).has_value());
    ASSERT_TRUE(engine.pumpUntil([&] { return node.telemetry().frames == 256; }))
        << "the quantum never settled at 256 frames";

    rlimit limit{};
    ASSERT_EQ(getrlimit(RLIMIT_RTTIME, &limit), 0);

    session.stall.holdEvery(1, 6000);
    engine.pump(std::chrono::milliseconds{3000});
    const SessionTelemetry during = node.telemetry();
    const std::uint64_t held = session.stall.heldCycles();
    session.stall.holdEvery(0, 0);

    ASSERT_GT(held, 0U) << "the defect was never injected";
    if (during.realtime == 0) {
        // Nothing holds this thread to a limit, so there is nothing to give
        // way to: it went on, alive, and said why.
        std::cerr << "the data loop is not real-time (rtkit refused, or RB_PW_NO_REALTIME): held " << held
                  << " cycles of 6 ms and went on; RLIMIT_RTTIME " << limit.rlim_cur << " / " << limit.rlim_max
                  << " us\n";
        EXPECT_EQ(during.overloads, 0U) << "a thread no limit holds gave way";
        EXPECT_EQ(during.shedding, 0U);
        EXPECT_GE(during.longRunsNotRealtime, 1U) << "it never said it ran long without being real-time";
        return;
    }
    EXPECT_EQ(during.overloads, 1U) << "it gave way " << during.overloads << " times";
    EXPECT_EQ(during.shedding, 1U) << "it is not silent after giving way";
    EXPECT_STREQ(during.heaviest[0].data(), "stall");
    EXPECT_GT(during.heaviestLoad[0], 1.0f) << "the stalling block took more than a cycle";
    std::cerr << "the data loop is real-time: held " << held
              << " cycles of 6 ms at quantum 256 before giving way; the heaviest block, " << during.heaviest[0].data()
              << ", took " << during.heaviestLoad[0] * 100.0f << " % of a cycle; RLIMIT_RTTIME " << limit.rlim_cur
              << " / " << limit.rlim_max << " us\n";

    // An edit is a new chance: the same graph, with the block no longer
    // holding anything, plays again and stays playing.
    ASSERT_TRUE(node.setGraph(session.compile()).has_value());
    ASSERT_TRUE(engine.pumpUntil([&] { return node.telemetry().shedding == 0; }));
    engine.pump(std::chrono::milliseconds{500});
    EXPECT_EQ(node.telemetry().overloads, 1U) << "it gave way again with nothing heavy in it";
    EXPECT_EQ(node.telemetry().shedding, 0U);
}

TEST_F(SessionNodes, BeingHandedToAnotherDriverIsNotALostCycle)
{
    // With nothing linked to it the node runs on the dummy driver; linking
    // it to a sink hands it to the sink's clock. **The position and the
    // graph's cycle counter both belong to that clock**, so both jump at the
    // handover, and neither jump lost anybody a cycle.
    Pump engine;
    ASSERT_TRUE(engine.waitUntilConnected());

    TestSession session;
    SessionNode node(engine.connection());
    ASSERT_TRUE(node.start(TestSession::spec(u"88888888-9999"_s, u"Handover"_s)).has_value());
    ASSERT_TRUE(node.setGraph(session.compile()).has_value());

    const std::optional<Graph::Node> ours = engine.awaitNode(node.nodeName());
    ASSERT_TRUE(ours.has_value());
    ASSERT_TRUE(engine.awaitPorts(ours->id, "output", 2));
    const std::optional<Graph::Node> sink = engine.awaitNode(QString::fromUtf8(kStereoSink));
    ASSERT_TRUE(sink.has_value());
    ASSERT_TRUE(engine.pumpUntil([&] { return node.telemetry().cycles > 0; }));

    const MachineCost machine(engine, node, std::chrono::milliseconds{500});
    const SessionTelemetry before = node.telemetry();

    ASSERT_EQ(linkByChannel(ours->id, sink->id), 2U);
    ASSERT_TRUE(engine.pumpUntil([&] { return node.telemetry().clockChanges > before.clockChanges; }))
        << "the node was never handed from the dummy driver to the sink, so there was no handover to judge";
    engine.pump(std::chrono::milliseconds{500});

    const SessionTelemetry after = node.telemetry();
    EXPECT_LE(after.xruns - before.xruns, machine.allowanceFor(std::chrono::milliseconds{1000}))
        << "a change of driver was counted as " << after.xruns - before.xruns << " missed cycles";
    EXPECT_EQ(after.lostCycles - before.lostCycles, 0U)
        << "the cycle counter of the driver the node arrived at was read as cycles it had lost";
    EXPECT_GT(after.cycles, before.cycles) << "the node stopped running when it changed driver";
}

TEST_F(SessionNodes, ARateChangeIsAskedAboutRatherThanHandledOnTheAudioThread)
{
    // ADR-0008: re-preparing a block allocates, so the audio thread never
    // does it. It goes silent and asks for a graph made for the new rate.
    Pump engine;
    ASSERT_TRUE(engine.waitUntilConnected());

    ForcedClock clock;
    clock.setRate(48000);

    TestSession session;
    SessionNode node(engine.connection());
    node.setRateCheckInterval(std::chrono::milliseconds{5});
    ASSERT_TRUE(node.start(TestSession::spec(u"88888888-9999"_s, u"Rates"_s)).has_value());
    ASSERT_TRUE(node.setGraph(session.compile(48000)).has_value());

    QSignalSpy rateSpy(&node, &SessionNode::rateChangeNeeded);
    ASSERT_TRUE(engine.pumpUntil([&] { return node.telemetry().sampleRate == 48000; }));

    clock.setRate(44100);
    ASSERT_TRUE(engine.pumpUntil([&] { return rateSpy.count() > 0; }))
        << "the node never said the rate had changed under it";
    EXPECT_EQ(rateSpy.at(0).at(0).toUInt(), 44100U);
    EXPECT_GT(node.telemetry().rateMismatches, 0U);

    // A graph prepared for the new rate ends it, and nothing was prepared on
    // the audio thread to get there.
    ASSERT_TRUE(node.setGraph(session.compile(44100)).has_value());
    ASSERT_TRUE(engine.pumpUntil([&] { return node.telemetry().sampleRate == 44100; }));
    const std::uint64_t mismatches = node.telemetry().rateMismatches;
    engine.pump(std::chrono::milliseconds{300});
    EXPECT_EQ(node.telemetry().rateMismatches, mismatches) << "it is still putting out silence";
}

TEST_F(SessionNodes, StartingTwiceIsRefusedAndStoppingTwiceIsHarmless)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilConnected());

    TestSession session;
    SessionNode node(engine.connection());
    const SessionNodeSpec spec = TestSession::spec(u"99999999-aaaa"_s, u"Twice"_s);
    ASSERT_TRUE(node.start(spec).has_value());

    const std::expected<void, SessionNodeError> again = node.start(spec);
    ASSERT_FALSE(again.has_value());
    EXPECT_EQ(again.error(), SessionNodeError::AlreadyRunning);

    node.stop();
    node.stop();
    EXPECT_FALSE(node.isRunning());

    // A graph cannot be handed to a node that is not running.
    const std::expected<void, SessionNodeError> handed = node.setGraph(session.compile());
    ASSERT_FALSE(handed.has_value());
    EXPECT_EQ(handed.error(), SessionNodeError::NotRunning);
}

TEST_F(SessionNodes, AGraphWithoutTheBlocksThePortsNameIsRefused)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilConnected());

    TestSession session;
    SessionNode node(engine.connection());
    SessionNodeSpec spec = TestSession::spec(u"bbbbbbbb-cccc"_s, u"Mismatch"_s);
    spec.outputs[0].blockId = u"nowhere"_s;
    ASSERT_TRUE(node.start(spec).has_value());

    const std::expected<void, SessionNodeError> handed = node.setGraph(session.compile());
    ASSERT_FALSE(handed.has_value());
    EXPECT_EQ(handed.error(), SessionNodeError::NoSuchBlock);
    EXPECT_FALSE(node.hasGraph()) << "a graph that was refused must not have gone live";
}

TEST_F(SessionNodes, TheLinkManagerLinksToTheLaneTheNodeNames)
{
    // A session node has one port per channel per block, so "FL" does not
    // say which port. SessionNode::endpoint() names the port, and the
    // manager has to honour it — and to know the node is ours on purpose.
    Pump engine;
    ASSERT_TRUE(engine.waitUntilReady());
    TestSession session;
    SessionNode node(engine.connection());
    ASSERT_TRUE(node.start(TestSession::specWithTwoSources(u"cccccccc-dddd"_s, u"Two lanes"_s)).has_value());
    ASSERT_TRUE(node.setGraph(session.compileWithTwoSources()).has_value());

    const std::optional<Graph::Node> ours = engine.awaitNode(node.nodeName());
    ASSERT_TRUE(ours.has_value());
    ASSERT_TRUE(engine.awaitPorts(ours->id, "input", 4))
        << "running=" << node.isRunning() << " connected=" << engine.connection().isConnected()
        << " reconnects=" << engine.connection().reconnectCount()
        << " lastError=" << engine.connection().lastError().toStdString() << " nodeId=" << node.nodeId()
        << " cycles=" << node.telemetry().cycles;
    ASSERT_TRUE(engine.awaitPorts(ours->id, "output", 2));

    FakeApp app(appOptions("TwoLaneApp", "twolaneapp"));
    const std::optional<Graph::Node> stream = engine.awaitMatch(playbackStreamOf("TwoLaneApp"));
    ASSERT_TRUE(stream.has_value());
    ASSERT_TRUE(engine.awaitPorts(stream->id, "output", 2));

    EndpointMatch fromApp;
    fromApp.applicationBinary = u"twolaneapp"_s;
    QList<DesiredLink> wanted;
    for (const dsp::ChannelPosition channel : {dsp::ChannelPosition::FrontLeft, dsp::ChannelPosition::FrontRight}) {
        DesiredLink link;
        link.from.node = fromApp;
        link.from.channel = channel == dsp::ChannelPosition::FrontLeft ? u"FL"_s : u"FR"_s;
        link.to = node.endpoint(u"src2"_s, channel, PortDirection::Input);
        wanted.append(link);
    }
    engine.manager().setDesiredLinks(wanted);

    ASSERT_TRUE(engine.pumpUntil([&] { return engine.manager().ownedLinkCount() == 2; }))
        << "the manager never made the two links";

    // pw-dump decides which ports they landed on, once the requests the
    // manager counts have reached the server.
    std::set<std::string> linkedPorts;
    ASSERT_TRUE(engine.awaitGraph([&](const Graph &graph) {
        linkedPorts.clear();
        for (const Graph::Link &link : graph.links) {
            if (link.inputNode != ours->id) {
                continue;
            }
            for (const Graph::Port &port : graph.ports) {
                if (port.id == link.inputPort) {
                    linkedPorts.insert(port.prop("port.name"));
                }
            }
        }
        return linkedPorts.size() >= 2;
    })) << "pw-dump never showed the two links the manager made";
    EXPECT_EQ(linkedPorts, (std::set<std::string>{"src2:in_FL", "src2:in_FR"}))
        << "the links went to a port the endpoint did not name";

    CaptureNode capture(captureOptions("rbtest.capture.twolane"));
    const std::optional<Graph::Node> captureNode = capture.waitUntilReady();
    ASSERT_TRUE(captureNode.has_value());
    ASSERT_EQ(linkByChannel(ours->id, captureNode->id), 2U);
    engine.pump(std::chrono::milliseconds{300});

    const std::map<std::string, double> levels = capture.measureSettled();
    ASSERT_TRUE(levels.contains("FL"));
    EXPECT_NEAR(levels.at("FL"), kTonePeakDb, 0.1) << "the lane that was linked did not carry the tone";

    engine.manager().setDesiredLinks({});
    ASSERT_TRUE(engine.pumpUntil([&] { return engine.manager().ownedLinkCount() == 0; }));
}
