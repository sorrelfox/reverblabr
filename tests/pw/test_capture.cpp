// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Capturing one application against a real PipeWire: the tone arrives,
// the application never notices, and it is captured again every time it comes
// back. What the block does with the channels once they arrive is measured
// offline in tests/unit/blocks/test_applicationsource.cpp.

#include "blocks/applicationsource.h"
#include "blocks/builtinblocks.h"
#include "core/sessionplan.h"
#include "core/sessionruntime.h"
#include "dsp/blockregistry.h"
#include "sessionharness.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QSignalSpy>
#include <gtest/gtest.h>

#include <algorithm>
#include <iostream>
#include <map>
#include <string>
#include <thread>

using namespace rb;
using namespace rb::core;
using namespace rb::pwtest;
using namespace Qt::StringLiterals;

namespace
{

constexpr double kOtherToneHz = 1500.0;

[[nodiscard]] dsp::BlockRegistry captureRegistry()
{
    dsp::BlockRegistry registry;
    EXPECT_TRUE(blocks::registerBuiltinBlocks(registry));
    // The other end of the session is a stand-in until the Output Device
    // block exists.
    EXPECT_TRUE(registry.add(std::string{test::PassThroughOutput::kTypeId}, &test::PassThroughOutput::make));
    return registry;
}

[[nodiscard]] BlockInstance block(const QString &id, std::string_view type)
{
    BlockInstance made;
    made.id = id;
    made.type = QString::fromUtf8(type.data(), static_cast<qsizetype>(type.size()));
    return made;
}

[[nodiscard]] Wire wire(const QString &from, const QString &to, std::uint32_t channel)
{
    Wire made;
    made.fromBlock = from;
    made.fromPort = channel;
    made.toBlock = to;
    made.toPort = channel;
    return made;
}

/// A session of one application source going straight out. An empty \a mode
/// leaves the block's own default, which is "what it sends to the default
/// output".
[[nodiscard]] Session captureSession(const QString &binary, const QString &mode = {}, const QStringList &outputs = {})
{
    Session session;
    session.id = QUuid::createUuid();
    session.name = u"Capture"_s;
    BlockInstance source = block(u"src"_s, blocks::ApplicationSource::kTypeId);
    source.params = QJsonObject{{"binary"_L1, binary}};
    if (!mode.isEmpty()) {
        source.params.insert("outputMode"_L1, mode);
    }
    if (!outputs.isEmpty()) {
        QJsonArray names;
        for (const QString &name : outputs) {
            names.append(name);
        }
        source.params.insert("outputs"_L1, names);
    }
    session.blocks = {source, block(u"out"_s, test::PassThroughOutput::kTypeId)};
    session.wires = {wire(u"src"_s, u"out"_s, 0), wire(u"src"_s, u"out"_s, 1)};
    return session;
}

[[nodiscard]] FakeAppOptions toneApp(const std::string &name, const std::string &binary, double hz = 1000.0)
{
    FakeAppOptions options = appOptions(name, binary);
    options.toneHz = hz;
    return options;
}

/// Links a capture filter to the session node's outputs and gives it back.
[[nodiscard]] std::unique_ptr<CaptureNode> listenTo(const Graph::Node &ours, const std::string &name, double sineHz)
{
    auto capture = std::make_unique<CaptureNode>(captureOptions(name, sineHz));
    const std::optional<Graph::Node> node = capture->waitUntilReady();
    EXPECT_TRUE(node.has_value());
    if (node) {
        EXPECT_EQ(linkByChannel(ours.id, node->id), 2U);
    }
    return capture;
}

/// The channels of a node's ports in one direction, monitors left out.
[[nodiscard]] QStringList channelsOf(Pump &engine, std::uint32_t node, pw::PortDirection direction)
{
    QStringList channels;
    for (const pw::PwPort &port : engine.mirror().portsOf(node, direction)) {
        if (!port.isMonitor) {
            channels.append(port.channel);
        }
    }
    channels.sort();
    return channels;
}

/// Whether \a stream plays on \a sink with the sink's layout: a link up from
/// each of its channels to the same channel of the sink, and no other.
[[nodiscard]] bool playsWithItsLayout(Pump &engine, std::uint32_t stream, std::uint32_t sink)
{
    if (channelsOf(engine, stream, pw::PortDirection::Output) != channelsOf(engine, sink, pw::PortDirection::Input)) {
        return false;
    }
    QStringList joined;
    for (const pw::PwLink &link : engine.mirror().linksOf(stream)) {
        if (link.outputNode != stream || link.inputNode != sink) {
            continue;
        }
        const std::optional<pw::PwPort> from = engine.mirror().port(link.outputPort);
        const std::optional<pw::PwPort> to = engine.mirror().port(link.inputPort);
        if (!link.isUp() || !from || !to || from->channel != to->channel) {
            return false;
        }
        joined.append(from->channel);
    }
    joined.sort();
    return joined == channelsOf(engine, sink, pw::PortDirection::Input);
}

/// Asks the policy to move \a stream to \a sink, the way a volume applet
/// does, and waits until the move is over.
///
/// **Over means the stream plays there with that sink's layout**: its
/// output ports carry the sink's channels — one MONO port on the
/// mono sink, FL and FR on the stereo one — and every link from it to the
/// sink is up, its format and buffers agreed. The policy links a stream
/// before it gives it the new layout, and that link goes active from the old
/// ports first, so a link merely being there is not the end of a move: judged
/// by that, the test asked for the next move 8 to 30 ms after the last, while
/// the policy was still in the middle of the last one, and it never saw the
/// stream come back to stereo still mono.
///
/// A move the policy still drops is asked for again: taking the
/// target away and setting it anew makes the policy look at the stream again.
[[nodiscard]] bool movedByThePolicy(Pump &engine, std::uint32_t stream, const char *sink)
{
    const auto arrived = [&] {
        const std::optional<pw::PwNode> node = engine.mirror().nodeNamed(QString::fromLatin1(sink));
        return node && playsWithItsLayout(engine, stream, node->object.id);
    };
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (attempt > 0) {
            std::cerr << "the policy dropped the move to " << sink << "; asking again\n";
            EXPECT_EQ(
                runCommand({"pw-metadata", "-n", "default", "-d", std::to_string(stream), "target.object"}).status, 0);
        }
        moveStream(stream, sink);
        if (engine.pumpUntil(arrived, std::chrono::seconds{5})) {
            return true;
        }
    }
    return false;
}

} // namespace

using Capture = SessionTest;

TEST_F(Capture, AnApplicationIsHeardInTheSessionAndOnItsOwnDeviceAtOnce)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilReady());
    const dsp::BlockRegistry registry = captureRegistry();
    SessionRuntime runtime(engine.connection(), engine.mirror(), registry);

    FakeApp app(toneApp("CaptureApp", "captureapp"));
    const std::optional<Graph::Node> stream = engine.awaitMatch(playbackStreamOf("CaptureApp"));
    ASSERT_TRUE(stream.has_value());
    ASSERT_TRUE(engine.awaitPorts(stream->id, "output", 2));

    const std::expected<void, RuntimeError> started = runtime.start(captureSession(u"captureapp"_s));
    ASSERT_TRUE(started.has_value()) << runtimeErrorName(started.error()).toStdString();
    const std::optional<Graph::Node> ours = engine.awaitNode(runtime.node()->nodeName());
    ASSERT_TRUE(ours.has_value());
    ASSERT_TRUE(engine.awaitPorts(ours->id, "input", 2));

    // The links the engine wanted are made, and they are ours.
    ASSERT_TRUE(engine.pumpUntil([&] { return Graph::capture().linked(stream->id, ours->id); }))
        << "the application's stream was never linked to the session";

    const std::unique_ptr<CaptureNode> capture = listenTo(*ours, "rbtest.capture.app", 1000.0);
    engine.pump(std::chrono::milliseconds{400});
    const std::map<std::string, SineCheck> heard = capture->measureSine(300ms);
    ASSERT_TRUE(heard.contains("FL"));
    EXPECT_NEAR(heard.at("FL").peakDb, kTonePeakDb, 0.5) << "the tone did not arrive at the level it was played at";
    EXPECT_LT(heard.at("FL").residualDb, -55.0) << "what arrived was not a clean tone";

    // **And the application never noticed.** Capturing adds links; it does
    // not move the stream, so it is still playing to the sink it chose.
    const std::optional<Graph::Node> sink = engine.awaitNode(QString::fromLatin1(kStereoSink));
    ASSERT_TRUE(sink.has_value());
    EXPECT_TRUE(Graph::capture().linked(stream->id, sink->id)) << "capturing took the application off its own device";

    runtime.stop();
}

TEST_F(Capture, AnApplicationThatComesBackIsCapturedAgainWithin250ms)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilReady());
    const dsp::BlockRegistry registry = captureRegistry();
    SessionRuntime runtime(engine.connection(), engine.mirror(), registry);
    ASSERT_TRUE(runtime.start(captureSession(u"restartapp"_s)).has_value());
    const std::optional<Graph::Node> ours = engine.awaitNode(runtime.node()->nodeName());
    ASSERT_TRUE(ours.has_value());
    ASSERT_TRUE(engine.awaitPorts(ours->id, "input", 2));
    const std::uint32_t ourId = ours->id;

    // **Both ends are timed inside this process.** A pw-dump costs tens of
    // milliseconds, and charging that to the budget is how a test starts
    // failing on a slower machine for something that is not its fault.
    QElapsedTimer sincePorts;
    bool waitingForPorts = false;
    std::uint32_t streamNode = 0;
    qint64 worst = -1;

    // **The clock starts when the stream can be told apart and has ports,
    // whichever of the two is seen second.** Its binary is not in what the
    // registry announces; it arrives with the node's info, which on a busy
    // machine comes after both of its ports. Watching the ports alone
    // then never recognised the stream at all, while the session had
    // already captured it. Neither is the session's to wait for, so the
    // quarter second is measured from the later of them.
    const auto streamReady = [&](std::uint32_t nodeId) {
        if (!waitingForPorts) {
            return;
        }
        const std::optional<pw::PwNode> owner = engine.mirror().node(nodeId);
        if (owner && owner->applicationBinary == u"restartapp"_s
            && !engine.mirror().portsOf(nodeId, pw::PortDirection::Output).isEmpty()) {
            streamNode = nodeId;
            sincePorts.start();
            waitingForPorts = false;
        }
    };
    QObject::connect(&engine.mirror(), &pw::RegistryMirror::portAdded, &runtime, [&](const pw::PwPort &port) {
        streamReady(port.nodeId);
    });
    QObject::connect(&engine.mirror(), &pw::RegistryMirror::nodeChanged, &runtime, [&](const pw::PwNode &node) {
        streamReady(node.object.id);
    });

    FakeApp app(toneApp("RestartApp", "restartapp"));
    for (int run = 0; run < 20; ++run) {
        waitingForPorts = true;
        streamNode = 0;
        if (run > 0) {
            app.start();
        }
        ASSERT_TRUE(engine.pumpUntil([&] { return streamNode != 0; })) << "run " << run << ": no ports ever appeared";
        ASSERT_TRUE(engine.pumpUntil([&] { return engine.mirror().isLinked(streamNode, ourId); }))
            << "run " << run << ": the application was never captured again";
        worst = std::max(worst, sincePorts.elapsed());

        app.stop();
        ASSERT_TRUE(engine.pumpUntil([&] { return !engine.mirror().node(streamNode).has_value(); }))
            << "run " << run << ": the stream never went away";
    }

    // Printed, because a number that is only checked is a number nobody
    // looks at until it fails.
    std::cerr << "capture after restart, twenty runs: worst " << worst << " ms of the 250 allowed\n";
    EXPECT_LE(worst, 250) << "the slowest of twenty restarts took " << worst << " ms to be captured again";
    EXPECT_GE(worst, 0) << "the clock never started, so nothing was measured";
    runtime.stop();
}

TEST_F(Capture, TwoStreamsOfOneApplicationAreSummedAndNeitherIsChanged)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilReady());
    const dsp::BlockRegistry registry = captureRegistry();
    SessionRuntime runtime(engine.connection(), engine.mirror(), registry);
    ASSERT_TRUE(runtime.start(captureSession(u"sumapp"_s)).has_value());
    const std::optional<Graph::Node> ours = engine.awaitNode(runtime.node()->nodeName());
    ASSERT_TRUE(ours.has_value());
    ASSERT_TRUE(engine.awaitPorts(ours->id, "input", 2));
    const std::unique_ptr<CaptureNode> capture = listenTo(*ours, "rbtest.capture.sum", 1000.0);

    // **Two tones, not two of the same tone.** Two independent players of one
    // frequency arrive at whatever phase they like, and their sum is anywhere
    // between silence and double; two frequencies can each be measured on
    // their own, which is what makes "summed exactly" something a test can say.
    FakeApp first(toneApp("SumApp", "sumapp"));
    ASSERT_TRUE(engine.awaitMatch(playbackStreamOf("SumApp")).has_value());
    ASSERT_TRUE(
        engine.pumpUntil([&] { return engine.mirror().nodesOfKind(pw::NodeKind::PlaybackStream).size() >= 1; }));
    engine.pump(std::chrono::milliseconds{500});
    const std::map<std::string, double> alone = capture->measureToneSettled(1000.0, 400ms);
    ASSERT_TRUE(alone.contains("FL"));
    EXPECT_NEAR(alone.at("FL"), kTonePeakDb, 0.5);

    FakeApp second(toneApp("SumApp", "sumapp", kOtherToneHz));
    ASSERT_TRUE(engine.pumpUntil([&] {
        int streams = 0;
        for (const pw::PwNode &node : engine.mirror().nodesOfKind(pw::NodeKind::PlaybackStream)) {
            streams += node.applicationBinary == u"sumapp"_s ? 1 : 0;
        }
        return streams == 2;
    })) << "the second stream never arrived";
    engine.pump(std::chrono::milliseconds{600});

    const std::map<std::string, double> together = capture->measureToneSettled(1000.0, 400ms);
    const std::map<std::string, double> other = capture->measureToneSettled(kOtherToneHz, 400ms);
    ASSERT_TRUE(together.contains("FL"));
    ASSERT_TRUE(other.contains("FL"));
    // Each tone arrives at the level it was played at, and neither is
    // touched by the other being there: that is what summing at unity means.
    EXPECT_NEAR(together.at("FL"), alone.at("FL"), 0.05) << "the first stream changed when the second arrived";
    EXPECT_NEAR(other.at("FL"), kTonePeakDb, 0.5) << "the second stream did not arrive at its own level";

    runtime.stop();
}

TEST_F(Capture, AStreamMovedToAnotherSinkIsStillCaptured)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilReady());
    const dsp::BlockRegistry registry = captureRegistry();
    SessionRuntime runtime(engine.connection(), engine.mirror(), registry);
    // "Any output", because this test is about the capture following the
    // stream; which outputs count is what the modes are for.
    ASSERT_TRUE(runtime.start(captureSession(u"moveapp"_s, u"any"_s)).has_value());
    const std::optional<Graph::Node> ours = engine.awaitNode(runtime.node()->nodeName());
    ASSERT_TRUE(ours.has_value());
    ASSERT_TRUE(engine.awaitPorts(ours->id, "input", 2));
    const std::unique_ptr<CaptureNode> capture = listenTo(*ours, "rbtest.capture.move", 1000.0);

    // **No target.** A stream that asked for a particular sink is not one
    // the policy will move, and being moved is the whole point here.
    FakeAppOptions options = toneApp("MoveApp", "moveapp");
    options.target.clear();
    FakeApp app(options);
    const std::optional<Graph::Node> stream = engine.awaitMatch(playbackStreamOf("MoveApp"));
    ASSERT_TRUE(stream.has_value());
    ASSERT_TRUE(engine.pumpUntil([&] { return engine.mirror().isLinked(stream->id, ours->id); }));
    engine.pump(std::chrono::milliseconds{400});
    EXPECT_NEAR(capture->measureSine(300ms).at("FL").peakDb, kTonePeakDb, 0.5);

    // The user sends the application to another device, from a volume applet
    // or a settings page. Capturing follows it: what is wanted is a match,
    // not a node, so the links are made again wherever it ends up.
    moveStream(stream->id, kMonoSink);
    ASSERT_TRUE(engine.pumpUntil([&] {
        const std::optional<pw::PwNode> sink = engine.mirror().nodeNamed(QString::fromLatin1(kMonoSink));
        return sink && engine.mirror().isLinked(stream->id, sink->object.id);
    })) << "the application never reached the other sink";
    // The stream is mono now, so the block's ports are too: a link only ever
    // joins two ports of the same channel, and there is no FL to link to any
    // more. The node grew the port it needs without the session being edited.
    ASSERT_TRUE(engine.pumpUntil([&] { return engine.mirror().isLinked(stream->id, ours->id); }))
        << "the move left the application uncaptured";
    ASSERT_TRUE(engine.awaitPorts(ours->id, "input", 1));
    EXPECT_EQ(Graph::capture().portsOf(ours->id, "input").at(0).prop("port.name"), "src:in_MONO");

    engine.pump(std::chrono::milliseconds{700});
    const std::map<std::string, SineCheck> heard = capture->measureSine(400ms);
    ASSERT_TRUE(heard.contains("FL"));
    // A mono stream is heard on both sides at unity, which is the mapping
    // policy and not a coincidence.
    EXPECT_NEAR(heard.at("FL").peakDb, kTonePeakDb, 1.0) << "the tone was lost or changed level on the way";

    runtime.stop();
}

TEST_F(Capture, ASurroundStreamGetsAPortPerChannelAndIsStillHeard)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilReady());
    const dsp::BlockRegistry registry = captureRegistry();
    SessionRuntime runtime(engine.connection(), engine.mirror(), registry);
    // The surround sink is not the default, so this one takes any output.
    ASSERT_TRUE(runtime.start(captureSession(u"surroundapp"_s, u"any"_s)).has_value());
    const std::optional<Graph::Node> ours = engine.awaitNode(runtime.node()->nodeName());
    ASSERT_TRUE(ours.has_value());
    const std::unique_ptr<CaptureNode> capture = listenTo(*ours, "rbtest.capture.surround", 1000.0);

    // An application playing to a 5.1 sink is remixed by PipeWire before it
    // reaches us, so its stream carries six channels. Every one of them needs
    // a port of its own, because a link only ever joins two ports of the same
    // channel and the folding is the block's ([ADR-0007]); the coefficients
    // themselves are measured offline.
    FakeAppOptions options = toneApp("SurroundApp", "surroundapp");
    options.target = kSurroundSink;
    FakeApp app(options);
    const std::optional<Graph::Node> stream = engine.awaitMatch(playbackStreamOf("SurroundApp"));
    ASSERT_TRUE(stream.has_value());
    ASSERT_TRUE(engine.awaitPorts(stream->id, "output", 6));

    ASSERT_TRUE(engine.awaitPorts(ours->id, "input", 6)) << "the session node did not grow ports for the surround "
                                                            "channels";
    const Graph graph = Graph::capture();
    std::vector<std::string> names;
    for (const Graph::Port &port : graph.portsOf(ours->id, "input")) {
        names.push_back(port.prop("port.name"));
    }
    for (const char *const wanted : {"src:in_FL", "src:in_FR", "src:in_FC", "src:in_LFE", "src:in_RL", "src:in_RR"}) {
        EXPECT_NE(std::ranges::find(names, std::string(wanted)), names.end()) << wanted << " is missing";
    }

    ASSERT_TRUE(engine.pumpUntil([&] { return engine.mirror().isLinked(stream->id, ours->id); }));
    engine.pump(std::chrono::milliseconds{700});
    const std::map<std::string, SineCheck> heard = capture->measureSine(400ms);
    ASSERT_TRUE(heard.contains("FL"));
    // The tone was played as a stereo pair, so the front channels carry it
    // and the rest are silence: it comes back at the level it went in.
    EXPECT_NEAR(heard.at("FL").peakDb, kTonePeakDb, 1.0);

    runtime.stop();
}

TEST_F(Capture, TheDefaultModeFollowsTheStreamOnAndOffTheDefaultOutput)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilReady());
    const dsp::BlockRegistry registry = captureRegistry();
    SessionRuntime runtime(engine.connection(), engine.mirror(), registry);
    // The block's own default: only what the application sends to the
    // default output.
    ASSERT_TRUE(runtime.start(captureSession(u"modeapp"_s)).has_value());
    const std::optional<Graph::Node> ours = engine.awaitNode(runtime.node()->nodeName());
    ASSERT_TRUE(ours.has_value());
    ASSERT_TRUE(engine.awaitPorts(ours->id, "input", 2));
    const std::uint32_t ourId = ours->id;

    FakeAppOptions options = toneApp("ModeApp", "modeapp");
    options.target.clear(); // so the policy will move it
    FakeApp app(options);
    const std::optional<Graph::Node> stream = engine.awaitMatch(playbackStreamOf("ModeApp"));
    ASSERT_TRUE(stream.has_value());
    ASSERT_TRUE(engine.pumpUntil([&] { return engine.mirror().isLinked(stream->id, ourId); }))
        << "an application playing to the default output was not captured";

    // Moved off the default output, it stops being ours — within the quarter
    // second the interface is allowed, measured from the move being seen.
    // Timed to the moment our last link from it goes, not to when the test
    // looks: the move itself is waited for until the policy is done with it.
    // The session lets go of a stream as soon as it is asked to move,
    // before the move is seen, which counts as no time at all.
    QElapsedTimer sinceMoved;
    bool asked = false;
    bool moved = false;
    qint64 letGoAfter = -1;
    QObject::connect(&engine.mirror(), &pw::RegistryMirror::linkAdded, &runtime, [&](const pw::PwLink &link) {
        const std::optional<pw::PwNode> target = engine.mirror().node(link.inputNode);
        if (!moved && link.outputNode == stream->id && target && target->name == QString::fromLatin1(kMonoSink)) {
            sinceMoved.start();
            moved = true;
        }
    });
    QObject::connect(&engine.mirror(), &pw::RegistryMirror::linkRemoved, &runtime, [&] {
        if (asked && letGoAfter < 0 && !engine.mirror().isLinked(stream->id, ourId)) {
            letGoAfter = moved ? sinceMoved.elapsed() : 0;
        }
    });
    asked = true;
    ASSERT_TRUE(movedByThePolicy(engine, stream->id, kMonoSink)) << "the stream never reached the other sink";
    ASSERT_TRUE(moved);
    ASSERT_TRUE(engine.pumpUntil([&] { return !engine.mirror().isLinked(stream->id, ourId); }))
        << "the stream went on being captured after it left the default output";
    ASSERT_GE(letGoAfter, 0);
    EXPECT_LE(letGoAfter, 250) << "letting go took " << letGoAfter << " ms";
    // **And it plays there**: a move that remade its ports under our
    // links left it suspended on the other sink, which is silence.
    EXPECT_TRUE(engine.pumpUntil(
        [&] {
            const std::optional<pw::PwNode> playing = engine.mirror().node(stream->id);
            return playing && playing->state == pw::NodeState::Running;
        },
        std::chrono::seconds{3}))
        << "the stream was left suspended on the other sink: silent there";

    // And back again. **The policy's move is waited for on its own**, so that
    // a machine too busy to move the stream is not reported as a session
    // that failed to follow it. **Back means with its stereo layout**:
    // left suspended on the mono sink, it came back still mono,
    // linked to the stereo sink's FL only — heard on the left alone — and
    // movedByThePolicy() never saw it arrive.
    ASSERT_TRUE(movedByThePolicy(engine, stream->id, kStereoSink))
        << "the stream never came back to the default output with its layout";
    ASSERT_TRUE(engine.pumpUntil([&] { return engine.mirror().isLinked(stream->id, ourId); }))
        << "coming back to the default output did not start the capture again";

    runtime.stop();
}

TEST_F(Capture, AMoveCostsOneLinkPerChannelAndNoMore)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilReady());
    const dsp::BlockRegistry registry = captureRegistry();
    SessionRuntime runtime(engine.connection(), engine.mirror(), registry);
    ASSERT_TRUE(runtime.start(captureSession(u"churnapp"_s, u"any"_s)).has_value());
    const std::optional<Graph::Node> ours = engine.awaitNode(runtime.node()->nodeName());
    ASSERT_TRUE(ours.has_value());
    ASSERT_TRUE(engine.awaitPorts(ours->id, "input", 2));
    const std::uint32_t ourId = ours->id;

    FakeAppOptions options = toneApp("ChurnApp", "churnapp");
    options.target.clear();
    FakeApp app(options);
    const std::optional<Graph::Node> stream = engine.awaitMatch(playbackStreamOf("ChurnApp"));
    ASSERT_TRUE(stream.has_value());
    ASSERT_TRUE(engine.pumpUntil([&] { return engine.mirror().isLinked(stream->id, ourId); }));
    engine.pump(std::chrono::milliseconds{400}); // let the first pass settle

    // **Churn is the thing to be afraid of**: a reconciler that answers each
    // event on its own makes a link and unmakes it again, and a session full
    // of applications would spend its life relinking.
    int made = 0;
    int lost = 0;
    QObject::connect(&engine.mirror(), &pw::RegistryMirror::linkAdded, &runtime, [&](const pw::PwLink &link) {
        made += link.inputNode == ourId ? 1 : 0;
    });
    QObject::connect(
        &engine.mirror(), &pw::RegistryMirror::linkRemoved, &runtime, [&](std::uint32_t, std::uint64_t) { ++lost; });

    moveStream(stream->id, kMonoSink);
    ASSERT_TRUE(engine.pumpUntil([&] {
        const std::optional<pw::PwNode> sink = engine.mirror().nodeNamed(QString::fromLatin1(kMonoSink));
        return sink && engine.mirror().isLinked(stream->id, sink->object.id);
    }));
    ASSERT_TRUE(engine.pumpUntil([&] { return engine.mirror().isLinked(stream->id, ourId); }));
    engine.pump(std::chrono::milliseconds{700});

    // The stream came back as mono, so one link is all it needs now, and the
    // two it had are gone: one pass, not a series of them.
    EXPECT_LE(made, 2) << "the move made " << made << " links into the session";
    EXPECT_LE(lost, 4) << "the move unmade " << lost << " links";
    EXPECT_EQ(Graph::capture().portsOf(ourId, "input").size(), 1U);

    runtime.stop();
}

TEST_F(Capture, ChangingTheDefaultOutputChangesWhatTheDefaultModeTakes)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilReady());
    const dsp::BlockRegistry registry = captureRegistry();
    SessionRuntime runtime(engine.connection(), engine.mirror(), registry);
    ASSERT_TRUE(runtime.start(captureSession(u"defaultapp"_s)).has_value());
    const std::optional<Graph::Node> ours = engine.awaitNode(runtime.node()->nodeName());
    ASSERT_TRUE(ours.has_value());
    const std::uint32_t ourId = ours->id;

    // This one asked for the stereo sink and stays there whatever happens;
    // what changes is which sink is the default.
    FakeApp app(toneApp("DefaultApp", "defaultapp"));
    const std::optional<Graph::Node> stream = engine.awaitMatch(playbackStreamOf("DefaultApp"));
    ASSERT_TRUE(stream.has_value());
    ASSERT_TRUE(engine.pumpUntil([&] { return engine.mirror().isLinked(stream->id, ourId); }));

    const std::optional<pw::PwNode> mono = engine.mirror().nodeNamed(QString::fromLatin1(kMonoSink));
    const std::optional<pw::PwNode> stereo = engine.mirror().nodeNamed(QString::fromLatin1(kStereoSink));
    ASSERT_TRUE(mono.has_value() && stereo.has_value());

    (void)runCommand({"wpctl", "set-default", std::to_string(mono->object.id)});
    ASSERT_TRUE(engine.pumpUntil([&] { return !engine.mirror().isLinked(stream->id, ourId); }))
        << "the application went on being captured after the default output moved away from it";

    (void)runCommand({"wpctl", "set-default", std::to_string(stereo->object.id)});
    ASSERT_TRUE(engine.pumpUntil([&] { return engine.mirror().isLinked(stream->id, ourId); }))
        << "the default output came back and the application did not";

    runtime.stop();
}

TEST_F(Capture, TheModeAndTheOutputsItNamesSurviveSaveAndRestore)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilConnected());
    const dsp::BlockRegistry registry = captureRegistry();
    SessionRuntime runtime(engine.connection(), engine.mirror(), registry);

    const QStringList chosen{QString::fromLatin1(kMonoSink), QString::fromLatin1(kSurroundSink)};
    ASSERT_TRUE(runtime.start(captureSession(u"saveapp"_s, u"chosen"_s, chosen)).has_value());

    // **What the block gives back is what gets saved.** A parameter the
    // block does not know is a parameter the next save drops, so a list that
    // survived being loaded has to survive being asked for again.
    const QHash<QString, QJsonObject> state = runtime.blockState();
    ASSERT_TRUE(state.contains(u"src"_s));
    const QJsonObject saved = state.value(u"src"_s);
    EXPECT_EQ(saved.value("outputMode"_L1).toString(), u"chosen"_s);
    const QJsonArray outputs = saved.value("outputs"_L1).toArray();
    ASSERT_EQ(outputs.size(), 2);
    EXPECT_EQ(outputs.at(0).toString(), QString::fromLatin1(kMonoSink));
    EXPECT_EQ(outputs.at(1).toString(), QString::fromLatin1(kSurroundSink));
    EXPECT_EQ(saved.value("binary"_L1).toString(), u"saveapp"_s);

    runtime.stop();
}

// --- the other side of the call ---

namespace
{

/// A session capturing one application and the microphone that application
/// is itself recording from.
[[nodiscard]] Session callSession(const QString &binary, bool split)
{
    Session session = captureSession(binary, u"any"_s);
    QJsonObject params = session.blocks.front().params;
    params.insert("includeInput"_L1, true);
    params.insert("splitChannels"_L1, split);
    session.blocks.front().params = params;
    return session;
}

/// Feeds a tone into a virtual source, which is how a test gets sound out of
/// a microphone that does not exist. **The policy will not route a playback
/// stream into a source**, whatever it is really made of, so the link is
/// made by hand; that is what feeding a virtual microphone looks like from
/// the outside and it is the harness's business, not the engine's.
[[nodiscard]] bool feedTheMicrophone(Pump &engine, const FakeApp &feeder, const char *sourceName)
{
    const std::optional<Graph::Node> stream = engine.awaitMatch(playbackStreamOf(feeder.options().applicationName));
    const std::optional<Graph::Node> virtualSource = engine.awaitNode(QString::fromLatin1(sourceName));
    if (!stream || !virtualSource || !engine.awaitPorts(stream->id, "output", 2)) {
        return false;
    }
    return linkByChannel(stream->id, virtualSource->id) == 2U;
}

/// Every link into a session's input ports, described as it appears and
/// timed as it appears and goes, against marks the test sets. A
/// session node is one named "reverblabr.…"; the link's source is named by
/// node, binary and class, since a port that came and went cannot be asked
/// about afterwards.
class SessionLinks
{
public:
    struct Link {
        std::uint32_t id = 0;
        QString fromName;
        QString fromBinary;
        QString fromClass;
        QString fromPort;
        QString into;
        qint64 addedMs = 0;
        qint64 removedMs = -1;

        [[nodiscard]] QString describe() const
        {
            const QString gone = removedMs < 0 ? u"still there"_s : u"gone at %1 ms"_s.arg(removedMs);
            return u"%1:%2 (%3, %4) into %5, made at %6 ms, %7"_s.arg(fromName,
                                                                      fromPort,
                                                                      fromBinary.isEmpty() ? u"no binary"_s
                                                                                           : fromBinary,
                                                                      fromClass,
                                                                      into,
                                                                      QString::number(addedMs),
                                                                      gone);
        }
    };

    SessionLinks(const pw::RegistryMirror &mirror, const QElapsedTimer &clock)
        : m_mirror(mirror)
        , m_clock(clock)
    {
        QObject::connect(
            &mirror, &pw::RegistryMirror::linkAdded, &m_watching, [this](const pw::PwLink &link) { added(link); });
        QObject::connect(
            &mirror, &pw::RegistryMirror::linkRemoved, &m_watching, [this](std::uint32_t id, std::uint64_t) {
                for (Link &link : m_links) {
                    if (link.id == id && link.removedMs < 0) {
                        link.removedMs = m_clock.elapsed();
                    }
                }
            });
    }

    void mark(const QString &what)
    {
        m_marks.append(u"%1 %2 ms"_s.arg(what).arg(m_clock.elapsed()));
    }

    [[nodiscard]] const QList<Link> &all() const
    {
        return m_links;
    }

    [[nodiscard]] QString story() const
    {
        QStringList lines;
        for (const Link &link : m_links) {
            lines.append(link.describe());
        }
        return lines.join(u"; "_s) + u" [" + m_marks.join(u", "_s) + u"]";
    }

private:
    void added(const pw::PwLink &link)
    {
        const std::optional<pw::PwNode> into = m_mirror.node(link.inputNode);
        if (!into || !into->name.startsWith(u"reverblabr."_s)) {
            return;
        }
        const std::optional<pw::PwNode> from = m_mirror.node(link.outputNode);
        const std::optional<pw::PwPort> fromPort = m_mirror.port(link.outputPort);
        const std::optional<pw::PwPort> intoPort = m_mirror.port(link.inputPort);
        Link seen;
        seen.id = link.object.id;
        seen.fromName = from ? from->name : u"node %1"_s.arg(link.outputNode);
        seen.fromBinary = from ? from->applicationBinary : QString{};
        seen.fromClass = from ? from->mediaClass : QString{};
        seen.fromPort = fromPort ? fromPort->name : u"port %1"_s.arg(link.outputPort);
        seen.into = intoPort ? intoPort->name : u"port %1 of node %2"_s.arg(link.inputPort).arg(link.inputNode);
        seen.addedMs = m_clock.elapsed();
        m_links.append(seen);
    }

    const pw::RegistryMirror &m_mirror;
    const QElapsedTimer &m_clock;
    QObject m_watching;
    QList<Link> m_links;
    QStringList m_marks;
};

/// Waits until the session's node is linked to a node of that name.
[[nodiscard]] bool awaitLinkedFrom(Pump &engine, const QString &nodeName, std::uint32_t ours)
{
    return engine.pumpUntil([&] {
        const std::optional<pw::PwNode> node = engine.mirror().nodeNamed(nodeName);
        return node && engine.mirror().isLinked(node->object.id, ours);
    });
}

} // namespace

TEST_F(Capture, BothSidesOfACallAreCapturedFromOneBlock)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilReady());
    const dsp::BlockRegistry registry = captureRegistry();
    SessionRuntime runtime(engine.connection(), engine.mirror(), registry);

    // The far side of the call: another tone, played into the virtual
    // microphone our application is going to be recording from.
    FakeAppOptions feederOptions = toneApp("MicFeeder", "micfeeder", kOtherToneHz);
    feederOptions.target = kStereoSource;
    FakeApp feeder(feederOptions);
    ASSERT_TRUE(feedTheMicrophone(engine, feeder, kStereoSource));

    // The application: playing its own tone and recording from that
    // microphone, which is what a voice-call client looks like.
    FakeAppOptions callOptions = toneApp("CallApp", "callapp", 1000.0);
    callOptions.records = true;
    callOptions.recordFrom = kStereoSource;
    FakeApp app(callOptions);
    ASSERT_TRUE(engine.awaitMatch(playbackStreamOf("CallApp")).has_value());

    ASSERT_TRUE(runtime.start(callSession(u"callapp"_s, false)).has_value());
    const std::optional<Graph::Node> ours = engine.awaitNode(runtime.node()->nodeName());
    ASSERT_TRUE(ours.has_value());
    // Four ports: the application's two and the microphone's two, which
    // have to be ports of their own or PipeWire would sum the front lefts.
    ASSERT_TRUE(engine.awaitPorts(ours->id, "input", 4));
    ASSERT_TRUE(awaitLinkedFrom(engine, QString::fromLatin1(kStereoSource), ours->id))
        << "the microphone the application is recording from was never linked to the session";

    const std::unique_ptr<CaptureNode> capture = listenTo(*ours, "rbtest.capture.call", 0.0);
    engine.pump(std::chrono::milliseconds{500});
    const std::map<std::string, double> near = capture->measureTone(1000.0, 400ms);
    const std::map<std::string, double> far = capture->measureTone(kOtherToneHz, 400ms);
    ASSERT_TRUE(near.contains("FL") && far.contains("FL"));
    EXPECT_NEAR(near.at("FL"), kTonePeakDb, 1.5) << "the application was not heard";
    EXPECT_NEAR(far.at("FL"), kTonePeakDb, 1.5) << "the other side of the call was not heard";

    runtime.stop();
}

TEST_F(Capture, TheSplitPutsTheMicrophoneOnTheLeftAndTheApplicationOnTheRight)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilReady());
    const dsp::BlockRegistry registry = captureRegistry();
    SessionRuntime runtime(engine.connection(), engine.mirror(), registry);

    FakeAppOptions feederOptions = toneApp("SplitFeeder", "splitfeeder", kOtherToneHz);
    feederOptions.target = kStereoSource;
    FakeApp feeder(feederOptions);
    ASSERT_TRUE(feedTheMicrophone(engine, feeder, kStereoSource));

    FakeAppOptions callOptions = toneApp("SplitApp", "splitapp", 1000.0);
    callOptions.records = true;
    callOptions.recordFrom = kStereoSource;
    FakeApp app(callOptions);
    ASSERT_TRUE(engine.awaitMatch(playbackStreamOf("SplitApp")).has_value());

    // **Every link into the session, as it is made and as it goes**.
    // A leak measured in the sound could be a lost cycle smearing one
    // tone across the spectrum, or something linked into the wrong side for
    // a moment; the sound cannot tell those apart, and the graph can. Each
    // link is described when it appears, because a port that came and went
    // is gone by the time anyone asks, and every node is watched, not only
    // the microphone: the feeder plays the microphone's tone too.
    QElapsedTimer clock;
    clock.start();
    SessionLinks links(engine.mirror(), clock);

    ASSERT_TRUE(runtime.start(callSession(u"splitapp"_s, true)).has_value());
    const std::optional<Graph::Node> ours = engine.awaitNode(runtime.node()->nodeName());
    ASSERT_TRUE(ours.has_value());
    ASSERT_TRUE(engine.awaitPorts(ours->id, "input", 4));
    ASSERT_TRUE(awaitLinkedFrom(engine, QString::fromLatin1(kStereoSource), ours->id));

    const std::unique_ptr<CaptureNode> capture = listenTo(*ours, "rbtest.capture.split", 0.0);
    // **Two more listeners, measured in the same window**, so that a
    // leak says where the microphone's tone joined: a second listener on the
    // session's output hears what the first one does unless the first one's
    // delivery is to blame, and one on the application's own stream hears
    // what arrives at the application's side of the session.
    const std::unique_ptr<CaptureNode> second = listenTo(*ours, "rbtest.capture.second", 0.0);
    const std::optional<Graph::Node> applicationStream = engine.awaitMatch(playbackStreamOf("SplitApp"));
    ASSERT_TRUE(applicationStream.has_value());
    const std::unique_ptr<CaptureNode> atTheApplication = listenTo(*applicationStream, "rbtest.capture.apptap", 0.0);
    engine.pump(std::chrono::milliseconds{500});
    const std::uint64_t xrunsBefore = runtime.node()->telemetry().xruns;
    links.mark(u"measuring from"_s);
    const std::map<std::string, double> application = capture->measureTone(1000.0, 400ms);
    std::map<std::string, double> microphoneAgain;
    std::map<std::string, double> microphoneAtTheApplication;
    std::thread againThread([&] { microphoneAgain = second->measureTone(kOtherToneHz, 400ms); });
    std::thread applicationThread(
        [&] { microphoneAtTheApplication = atTheApplication->measureTone(kOtherToneHz, 400ms); });
    const std::map<std::string, double> microphone = capture->measureTone(kOtherToneHz, 400ms);
    againThread.join();
    applicationThread.join();
    links.mark(u"measured to"_s);
    const std::uint64_t lostCycles = runtime.node()->telemetry().xruns - xrunsBefore;

    // **Each side is fed only by what belongs on it**, from the first link
    // on: a session port is "<block>:in_<channel>", fed by the application's
    // playback stream, and the microphone's are "<block>:in_mic_<channel>",
    // fed by the microphone the application records from.
    for (const SessionLinks::Link &link : links.all()) {
        if (link.into.contains(u":in_mic_"_s)) {
            EXPECT_EQ(link.fromName, QString::fromLatin1(kStereoSource))
                << "the microphone's side was fed by " << link.describe().toStdString();
        } else if (link.into.contains(u":in_"_s)) {
            EXPECT_TRUE(link.fromBinary == u"splitapp"_s && link.fromClass == u"Stream/Output/Audio"_s)
                << "the application's side was fed by " << link.describe().toStdString();
        }
    }
    const auto rightOf = [](const std::map<std::string, double> &levels) {
        return levels.contains("FR") ? std::to_string(levels.at("FR")) : std::string{"none"};
    };
    const std::string story = " (" + std::to_string(lostCycles)
        + " cycles lost while measuring; the microphone's tone on the right, dBFS: " + rightOf(microphone) + " here, "
        + rightOf(microphoneAgain) + " at a second listener on the session, " + rightOf(microphoneAtTheApplication)
        + " in the application's own stream; links into the session: " + links.story().toStdString() + ")";
    ASSERT_TRUE(application.contains("FL") && application.contains("FR"));
    ASSERT_TRUE(microphone.contains("FL") && microphone.contains("FR"));

    // **At its own level**, because each side is folded to mono here: a
    // stereo pair going to one channel is half of each, and both halves are
    // the same tone.
    EXPECT_NEAR(microphone.at("FL"), kTonePeakDb, 1.5) << "the microphone should be on the left";
    EXPECT_NEAR(application.at("FR"), kTonePeakDb, 1.5) << "the application should be on the right";
    // And nothing of either on the other side.
    std::cout << "the split:" << story << "\n";
    EXPECT_LT(application.at("FL"), kTonePeakDb - 30.0) << "the application leaked onto the left" << story;
    EXPECT_LT(microphone.at("FR"), kTonePeakDb - 30.0) << "the microphone leaked onto the right" << story;

    runtime.stop();
}

TEST_F(Capture, TheApplicationSwitchingMicrophonesIsFollowedWithin500ms)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilReady());
    const dsp::BlockRegistry registry = captureRegistry();
    SessionRuntime runtime(engine.connection(), engine.mirror(), registry);

    FakeAppOptions callOptions = toneApp("SwitchApp", "switchapp", 1000.0);
    callOptions.records = true;
    callOptions.recordFrom = kStereoSource;
    FakeApp app(callOptions);
    ASSERT_TRUE(engine.awaitMatch(playbackStreamOf("SwitchApp")).has_value());

    ASSERT_TRUE(runtime.start(callSession(u"switchapp"_s, false)).has_value());
    const std::optional<Graph::Node> ours = engine.awaitNode(runtime.node()->nodeName());
    ASSERT_TRUE(ours.has_value());
    ASSERT_TRUE(awaitLinkedFrom(engine, QString::fromLatin1(kStereoSource), ours->id));

    // **Nothing is stored and nothing is edited.** Somebody changes the
    // microphone in the application's own settings; the plan is worked out
    // again from the graph, and the graph is where the answer lives.
    QElapsedTimer since;
    since.start();
    app.recordFrom(kMonoSource);
    ASSERT_TRUE(awaitLinkedFrom(engine, QString::fromLatin1(kMonoSource), ours->id))
        << "the session never followed the application onto the other microphone";
    const qint64 took = since.elapsed();
    EXPECT_LT(took, 500) << "following the switch took " << took << " ms";
    std::cout << "the microphone switch was followed in " << took << " ms\n";

    // And it let go of the one that was: a session recording from a
    // microphone nobody is using any more is a microphone light nobody can
    // explain.
    EXPECT_TRUE(engine.pumpUntil([&] {
        const std::optional<pw::PwNode> old = engine.mirror().nodeNamed(QString::fromLatin1(kStereoSource));
        return old && !engine.mirror().isLinked(old->object.id, ours->id);
    })) << "the session stayed linked to the microphone the application had left";

    runtime.stop();
}
