// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The engine through its own interface and nothing else: make a
// session, build a chain in it, start it, hear it, stop it. The same flow
// the mock walks, against a real PipeWire.

#include "core/audiosystem.h"
#include "core/editcommand.h"
#include "core/engine.h"
#include "core/session.h"
#include "core/sessionruntime.h"
#include "core/telemetry.h"
#include "dsp/blockregistry.h"
#include "dsp/decibel.h"
#include "pw/connection.h"
#include "pw/sessionnode.h"
#include "pw/virtualdeviceconf.h"
#include "pw/virtualdevicemanager.h"
#include "sessionharness.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <algorithm>
#include <iterator>
#include <map>
#include <memory>
#include <string>

using namespace rb;
using namespace rb::core;
using namespace rb::pwtest;
using namespace Qt::StringLiterals;

namespace
{

[[nodiscard]] dsp::BlockRegistry testRegistry()
{
    dsp::BlockRegistry registry;
    EXPECT_TRUE(registry.add(std::string{test::PassThroughSource::kTypeId}, &test::PassThroughSource::make));
    EXPECT_TRUE(registry.add(std::string{test::FixedGainEffect::kTypeId}, &test::FixedGainEffect::make));
    EXPECT_TRUE(registry.add(std::string{test::BandWriterEffect::kTypeId}, &test::BandWriterEffect::make));
    EXPECT_TRUE(registry.add(std::string{test::PassThroughOutput::kTypeId}, &test::PassThroughOutput::make));
    // One block with real parameters, for the tests that turn a knob.
    EXPECT_TRUE(registry.add(std::string{test::GainBlock::kTypeId}, &test::GainBlock::make));
    return registry;
}

[[nodiscard]] QString asJson(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

[[nodiscard]] QString addBlockEdit(const QString &id, std::string_view type)
{
    BlockInstance block;
    block.id = id;
    block.type = QString::fromUtf8(type.data(), static_cast<qsizetype>(type.size()));
    AddBlock command;
    command.block = block;
    return asJson(editCommandToJson(command));
}

[[nodiscard]] QString connectEdit(const QString &from, std::uint32_t fromPort, const QString &to, std::uint32_t toPort)
{
    Wire wire;
    wire.fromBlock = from;
    wire.fromPort = fromPort;
    wire.toBlock = to;
    wire.toPort = toPort;
    return asJson(editCommandToJson(Connect{wire, -1}));
}

[[nodiscard]] QString paramEdit(const QString &blockId, const QString &key, double value)
{
    return asJson(editCommandToJson(SetParam{blockId, key, value}));
}

[[nodiscard]] QString setEnabledEdit(const QString &blockId, bool enabled)
{
    return asJson(editCommandToJson(SetEnabled{blockId, enabled}));
}

} // namespace

using EngineThroughTheFacade = SessionTest;

TEST_F(EngineThroughTheFacade, MakesASessionRunsItAndTheSoundComesOut)
{
    QTemporaryDir sessions;
    ASSERT_TRUE(sessions.isValid());
    const dsp::BlockRegistry registry = testRegistry();
    Engine engine(registry, SessionStore{sessions.path()});

    QSignalSpy opened(&engine, &EngineFacade::sessionOpened);
    QSignalSpy snapshots(&engine, &EngineFacade::sessionSnapshot);
    QSignalSpy patches(&engine, &EngineFacade::sessionPatch);
    QSignalSpy states(&engine, &EngineFacade::stateChanged);
    QSignalSpy errors(&engine, &EngineFacade::error);

    engine.connectToPipeWire();
    Waiter pump;
    ASSERT_TRUE(pump.pumpUntil([&] { return engine.isConnected(); }));

    // Everything from here goes through the interface and nothing else.
    engine.createSession(u"Facade"_s);
    ASSERT_EQ(opened.count(), 1);
    const QString id = opened.back().at(0).toString();
    ASSERT_EQ(snapshots.count(), 1);

    engine.applyEdit(id, addBlockEdit(u"src"_s, test::PassThroughSource::kTypeId));
    engine.applyEdit(id, addBlockEdit(u"fx"_s, test::FixedGainEffect::kTypeId));
    engine.applyEdit(id, addBlockEdit(u"out"_s, test::PassThroughOutput::kTypeId));
    for (std::uint32_t channel = 0; channel < 2; ++channel) {
        engine.applyEdit(id, connectEdit(u"src"_s, channel, u"fx"_s, channel));
        engine.applyEdit(id, connectEdit(u"fx"_s, channel, u"out"_s, channel));
    }
    ASSERT_TRUE(errors.isEmpty()) << errors.back().at(0).toString().toStdString();
    ASSERT_EQ(patches.count(), 7);

    engine.start(id);
    ASSERT_TRUE(errors.isEmpty()) << errors.back().at(0).toString().toStdString();
    ASSERT_GE(states.count(), 2);
    ASSERT_EQ(states.back().at(1).toString(), u"running"_s);

    // And now it is a node in the graph, with a tone going through it.
    ASSERT_NE(engine.runtimeOf(id), nullptr);
    const QString nodeName = engine.runtimeOf(id)->node()->nodeName();
    const std::optional<Graph::Node> ours = pump.awaitNode(nodeName);
    ASSERT_TRUE(ours.has_value());
    ASSERT_TRUE(pump.awaitPorts(ours->id, "input", 2));
    ASSERT_TRUE(pump.awaitPorts(ours->id, "output", 2));

    FakeApp app(appOptions("FacadeApp", "facadeapp"));
    const std::optional<Graph::Node> stream = pump.awaitMatch(playbackStreamOf("FacadeApp"));
    ASSERT_TRUE(stream.has_value());
    ASSERT_TRUE(pump.awaitPorts(stream->id, "output", 2));
    ASSERT_EQ(linkByChannel(stream->id, ours->id), 2U);

    CaptureNode capture(captureOptions("rbtest.capture.facade", 1000.0));
    const std::optional<Graph::Node> captureNode = capture.waitUntilReady();
    ASSERT_TRUE(captureNode.has_value());
    ASSERT_EQ(linkByChannel(ours->id, captureNode->id), 2U);
    pump.pump(std::chrono::milliseconds{500});

    const std::map<std::string, SineCheck> heard = capture.measureSine(300ms);
    ASSERT_TRUE(heard.contains("FL"));
    EXPECT_NEAR(heard.at("FL").peakDb, kTonePeakDb - 6.02, 0.3) << "the session's gain block was not heard";
    EXPECT_LT(heard.at("FL").residualDb, -55.0);

    engine.stop(id);
    EXPECT_EQ(states.back().at(1).toString(), u"idle"_s);
    ASSERT_TRUE(pump.pumpUntil([&] {
        const Graph graph = Graph::capture();
        return graph.node(ours->id) == nullptr;
    })) << "Stop left the node in the graph";
}

namespace
{

/// A session of a source, a gain and an output, made through the facade.
[[nodiscard]] QString makeGainSession(Engine &engine, QSignalSpy &opened)
{
    engine.createSession(u"Comes back"_s);
    const QString id = opened.back().at(0).toString();
    engine.applyEdit(id, addBlockEdit(u"src"_s, test::PassThroughSource::kTypeId));
    engine.applyEdit(id, addBlockEdit(u"fx"_s, test::FixedGainEffect::kTypeId));
    engine.applyEdit(id, addBlockEdit(u"out"_s, test::PassThroughOutput::kTypeId));
    for (std::uint32_t channel = 0; channel < 2; ++channel) {
        engine.applyEdit(id, connectEdit(u"src"_s, channel, u"fx"_s, channel));
        engine.applyEdit(id, connectEdit(u"fx"_s, channel, u"out"_s, channel));
    }
    return id;
}

} // namespace

// **PipeWire going away does not end a running session** (PipeWire or
// WirePlumber restarting on the desktop): the session says it is starting
// while there is no PipeWire, and runs again by itself once there is, with
// its node back in the graph and the sound going through it. Stopping and
// starting the connection stands in for the restart, as in pw.registry.
TEST_F(EngineThroughTheFacade, ARunningSessionRunsAgainWhenPipeWireComesBack)
{
    QTemporaryDir sessions;
    ASSERT_TRUE(sessions.isValid());
    const dsp::BlockRegistry registry = testRegistry();
    Engine engine(registry, SessionStore{sessions.path()});
    QSignalSpy opened(&engine, &EngineFacade::sessionOpened);
    QSignalSpy states(&engine, &EngineFacade::stateChanged);
    QSignalSpy errors(&engine, &EngineFacade::error);

    engine.connectToPipeWire();
    Waiter pump;
    ASSERT_TRUE(pump.pumpUntil([&] { return engine.isConnected(); }));
    const QString id = makeGainSession(engine, opened);
    engine.start(id);
    ASSERT_TRUE(errors.isEmpty()) << errors.back().at(0).toString().toStdString();
    ASSERT_EQ(states.back().at(1).toString(), u"running"_s);
    const QString nodeName = engine.runtimeOf(id)->node()->nodeName();
    const std::optional<Graph::Node> before = pump.awaitNode(nodeName);
    ASSERT_TRUE(before.has_value());

    engine.connection().stop();
    ASSERT_TRUE(pump.pumpUntil([&] { return states.back().at(1).toString() == u"starting"_s; }))
        << "the session did not say it was waiting; it says " << states.back().at(1).toString().toStdString();
    EXPECT_FALSE(engine.runtimeOf(id)->isRunning());

    engine.connection().start();
    ASSERT_TRUE(pump.pumpUntil([&] { return states.back().at(1).toString() == u"running"_s; }))
        << "the session did not run again; it says " << states.back().at(1).toString().toStdString();
    EXPECT_TRUE(errors.isEmpty()) << errors.back().at(0).toString().toStdString();

    // A node of the same name, new in the graph, and the sound through it.
    const std::optional<Graph::Node> after = pump.awaitNode(nodeName);
    ASSERT_TRUE(after.has_value()) << "no node in the graph after PipeWire came back";
    ASSERT_TRUE(pump.awaitPorts(after->id, "input", 2));
    ASSERT_TRUE(pump.awaitPorts(after->id, "output", 2));
    FakeApp app(appOptions("BackApp", "backapp"));
    const std::optional<Graph::Node> stream = pump.awaitMatch(playbackStreamOf("BackApp"));
    ASSERT_TRUE(stream.has_value());
    ASSERT_TRUE(pump.awaitPorts(stream->id, "output", 2));
    ASSERT_EQ(linkByChannel(stream->id, after->id), 2U);
    CaptureNode capture(captureOptions("rbtest.capture.back", 1000.0));
    const std::optional<Graph::Node> captureNode = capture.waitUntilReady();
    ASSERT_TRUE(captureNode.has_value());
    ASSERT_EQ(linkByChannel(after->id, captureNode->id), 2U);
    pump.pump(std::chrono::milliseconds{500});
    const std::map<std::string, SineCheck> heard = capture.measureSine(300ms);
    ASSERT_TRUE(heard.contains("FL"));
    EXPECT_NEAR(heard.at("FL").peakDb, kTonePeakDb - 6.02, 0.3) << "the session's gain was not heard after the restart";

    engine.stop(id);
    EXPECT_EQ(states.back().at(1).toString(), u"idle"_s);
}

// **A session stopped while it waits stays stopped**: Stop is what the
// person asked for last, and PipeWire coming back must not undo it.
TEST_F(EngineThroughTheFacade, ASessionStoppedWhilePipeWireIsAwayStaysStopped)
{
    QTemporaryDir sessions;
    ASSERT_TRUE(sessions.isValid());
    const dsp::BlockRegistry registry = testRegistry();
    Engine engine(registry, SessionStore{sessions.path()});
    QSignalSpy opened(&engine, &EngineFacade::sessionOpened);
    QSignalSpy states(&engine, &EngineFacade::stateChanged);

    engine.connectToPipeWire();
    Waiter pump;
    ASSERT_TRUE(pump.pumpUntil([&] { return engine.isConnected(); }));
    const QString id = makeGainSession(engine, opened);
    engine.start(id);
    ASSERT_EQ(states.back().at(1).toString(), u"running"_s);

    engine.connection().stop();
    ASSERT_TRUE(pump.pumpUntil([&] { return states.back().at(1).toString() == u"starting"_s; }));
    engine.stop(id);
    ASSERT_EQ(states.back().at(1).toString(), u"idle"_s);

    engine.connection().start();
    ASSERT_TRUE(pump.pumpUntil([&] { return engine.isConnected(); }));
    pump.pump(std::chrono::milliseconds{500});
    EXPECT_EQ(states.back().at(1).toString(), u"idle"_s) << "PipeWire coming back started a stopped session";
    EXPECT_FALSE(engine.runtimeOf(id)->isRunning());
}

TEST_F(EngineThroughTheFacade, ASessionSurvivesBeingSavedClosedAndOpenedAgain)
{
    QTemporaryDir sessions;
    ASSERT_TRUE(sessions.isValid());
    const dsp::BlockRegistry registry = testRegistry();
    Engine engine(registry, SessionStore{sessions.path()});

    QSignalSpy opened(&engine, &EngineFacade::sessionOpened);
    QSignalSpy snapshots(&engine, &EngineFacade::sessionSnapshot);
    QSignalSpy listed(&engine, &EngineFacade::sessionsListed);
    QSignalSpy errors(&engine, &EngineFacade::error);

    engine.createSession(u"Kept"_s);
    const QString id = opened.back().at(0).toString();
    engine.applyEdit(id, addBlockEdit(u"src"_s, test::PassThroughSource::kTypeId));
    engine.applyEdit(id, addBlockEdit(u"fx"_s, test::FixedGainEffect::kTypeId));
    engine.saveSession(id);
    engine.closeSession(id);
    ASSERT_TRUE(errors.isEmpty()) << errors.back().at(0).toString().toStdString();

    engine.listSessions();
    ASSERT_EQ(listed.count(), 1);
    const QJsonDocument sessionList = QJsonDocument::fromJson(listed.back().at(0).toString().toUtf8());
    ASSERT_EQ(sessionList.array().size(), 1);
    EXPECT_EQ(sessionList.array().at(0).toObject().value(u"name"_s).toString(), u"Kept"_s);

    engine.openSession(id);
    ASSERT_TRUE(errors.isEmpty()) << errors.back().at(0).toString().toStdString();
    const QJsonObject session = QJsonDocument::fromJson(snapshots.back().at(2).toString().toUtf8()).object();
    EXPECT_EQ(session.value(u"name"_s).toString(), u"Kept"_s);
    EXPECT_EQ(session.value(u"blocks"_s).toArray().size(), 2);
}

TEST_F(EngineThroughTheFacade, SaysWhyASessionWillNotStart)
{
    QTemporaryDir sessions;
    ASSERT_TRUE(sessions.isValid());
    const dsp::BlockRegistry registry = testRegistry();
    Engine engine(registry, SessionStore{sessions.path()});

    QSignalSpy opened(&engine, &EngineFacade::sessionOpened);
    QSignalSpy states(&engine, &EngineFacade::stateChanged);
    QSignalSpy errors(&engine, &EngineFacade::error);

    engine.connectToPipeWire();
    Waiter pump;
    ASSERT_TRUE(pump.pumpUntil([&] { return engine.isConnected(); }));

    engine.createSession(u"Nothing"_s);
    const QString id = opened.back().at(0).toString();
    // A session with no source at all cannot be compiled into anything.
    engine.applyEdit(id, addBlockEdit(u"fx"_s, test::FixedGainEffect::kTypeId));
    engine.start(id);

    ASSERT_FALSE(errors.isEmpty());
    EXPECT_EQ(errors.back().at(0).toString(), u"session.cannotRun"_s);
    const QJsonObject args = QJsonDocument::fromJson(errors.back().at(1).toString().toUtf8()).object();
    EXPECT_EQ(args.value(u"session"_s).toString(), u"Nothing"_s);
    EXPECT_FALSE(args.value(u"reason"_s).toString().isEmpty());
    EXPECT_EQ(states.back().at(1).toString(), u"error"_s);
}

TEST_F(EngineThroughTheFacade, UndoIsHeardWhileTheSessionIsRunning)
{
    QTemporaryDir sessions;
    ASSERT_TRUE(sessions.isValid());
    const dsp::BlockRegistry registry = testRegistry();
    Engine engine(registry, SessionStore{sessions.path()});

    QSignalSpy opened(&engine, &EngineFacade::sessionOpened);
    QSignalSpy patches(&engine, &EngineFacade::sessionPatch);
    QSignalSpy undoState(&engine, &EngineFacade::undoStateChanged);
    QSignalSpy errors(&engine, &EngineFacade::error);

    engine.connectToPipeWire();
    Waiter pump;
    ASSERT_TRUE(pump.pumpUntil([&] { return engine.isConnected(); }));

    engine.createSession(u"Undo"_s);
    const QString id = opened.back().at(0).toString();
    engine.applyEdit(id, addBlockEdit(u"src"_s, test::PassThroughSource::kTypeId));
    engine.applyEdit(id, addBlockEdit(u"fx"_s, test::FixedGainEffect::kTypeId));
    engine.applyEdit(id, addBlockEdit(u"knob"_s, test::GainBlock::kTypeId));
    engine.applyEdit(id, addBlockEdit(u"out"_s, test::PassThroughOutput::kTypeId));
    for (std::uint32_t channel = 0; channel < 2; ++channel) {
        engine.applyEdit(id, connectEdit(u"src"_s, channel, u"fx"_s, channel));
        engine.applyEdit(id, connectEdit(u"fx"_s, channel, u"knob"_s, channel));
        engine.applyEdit(id, connectEdit(u"knob"_s, channel, u"out"_s, channel));
    }
    engine.start(id);
    ASSERT_TRUE(errors.isEmpty()) << errors.back().at(0).toString().toStdString();

    ASSERT_NE(engine.runtimeOf(id), nullptr);
    const std::optional<Graph::Node> ours = pump.awaitNode(engine.runtimeOf(id)->node()->nodeName());
    ASSERT_TRUE(ours.has_value());
    ASSERT_TRUE(pump.awaitPorts(ours->id, "input", 2));
    ASSERT_TRUE(pump.awaitPorts(ours->id, "output", 2));

    FakeApp app(appOptions("UndoApp", "undoapp"));
    const std::optional<Graph::Node> stream = pump.awaitMatch(playbackStreamOf("UndoApp"));
    ASSERT_TRUE(stream.has_value());
    ASSERT_TRUE(pump.awaitPorts(stream->id, "output", 2));
    ASSERT_EQ(linkByChannel(stream->id, ours->id), 2U);

    CaptureNode capture(captureOptions("rbtest.capture.undo", 1000.0));
    const std::optional<Graph::Node> captureNode = capture.waitUntilReady();
    ASSERT_TRUE(captureNode.has_value());
    ASSERT_EQ(linkByChannel(ours->id, captureNode->id), 2U);
    pump.pump(std::chrono::milliseconds{500});

    // The effect halves the tone, so what it does is plainly audible.
    const std::map<std::string, SineCheck> through = capture.measureSine(300ms);
    ASSERT_TRUE(through.contains("FL"));
    EXPECT_NEAR(through.at("FL").peakDb, kTonePeakDb - 6.02, 0.3);

    // A knob turned down while it runs, and taken back. **A parameter goes
    // to the block itself, not through a recompile**, so this is the path
    // that would quietly not happen: a block that is already running keeps
    // the state it has whatever the session says.
    engine.applyEdit(id, paramEdit(u"knob"_s, u"gainDb"_s, -6.0));
    ASSERT_TRUE(errors.isEmpty()) << errors.back().at(0).toString().toStdString() << " "
                                  << errors.back().at(1).toString().toStdString();
    pump.pump(std::chrono::milliseconds{300}); // the block smooths over 20 ms
    const std::map<std::string, SineCheck> turnedDown = capture.measureSine(300ms);
    ASSERT_TRUE(turnedDown.contains("FL"));
    EXPECT_NEAR(turnedDown.at("FL").peakDb, kTonePeakDb - 12.04, 0.3) << "the knob was not heard";

    engine.undo(id);
    pump.pump(std::chrono::milliseconds{300});
    const std::map<std::string, SineCheck> turnedBack = capture.measureSine(300ms);
    ASSERT_TRUE(turnedBack.contains("FL"));
    EXPECT_NEAR(turnedBack.at("FL").peakDb, kTonePeakDb - 6.02, 0.3)
        << "undoing a knob put it back in the session but not in the sound";

    // Switch it off while the session runs, and the tone comes through whole.
    engine.applyEdit(id, setEnabledEdit(u"fx"_s, false));
    ASSERT_TRUE(errors.isEmpty()) << errors.back().at(0).toString().toStdString();
    pump.pump(std::chrono::milliseconds{500}); // the bypass crossfades in
    const std::map<std::string, SineCheck> bypassed = capture.measureSine(300ms);
    ASSERT_TRUE(bypassed.contains("FL"));
    EXPECT_NEAR(bypassed.at("FL").peakDb, kTonePeakDb, 0.3) << "switching the effect off was not heard";

    // **This is the point of the test**: undo has to go the same way an edit
    // does — through the live-update path and into the audio — and not just
    // put the model back.
    const int patchesBefore = static_cast<int>(patches.count());
    engine.undo(id);
    ASSERT_TRUE(errors.isEmpty()) << errors.back().at(0).toString().toStdString();
    EXPECT_EQ(patches.count(), patchesBefore + 1) << "the undo was not sent to the clients";
    EXPECT_FALSE(undoState.isEmpty());
    pump.pump(std::chrono::milliseconds{500});

    const std::map<std::string, SineCheck> again = capture.measureSine(300ms);
    ASSERT_TRUE(again.contains("FL"));
    EXPECT_NEAR(again.at("FL").peakDb, kTonePeakDb - 6.02, 0.3) << "undo did not reach the running graph";
    EXPECT_LT(again.at("FL").residualDb, -55.0) << "the tone came back distorted";

    // And redo takes it out again, still while it runs.
    engine.redo(id);
    pump.pump(std::chrono::milliseconds{500});
    const std::map<std::string, SineCheck> redone = capture.measureSine(300ms);
    ASSERT_TRUE(redone.contains("FL"));
    EXPECT_NEAR(redone.at("FL").peakDb, kTonePeakDb, 0.3) << "redo did not reach the running graph";

    engine.stop(id);
}

namespace
{

/// The levels a snapshot holds for one block, or nothing when it says
/// nothing about it.
[[nodiscard]] std::optional<core::BlockTelemetry> blockOf(const core::TelemetrySnapshot &snapshot, const char *id)
{
    for (std::uint32_t index = 0; index < snapshot.blockCount; ++index) {
        if (std::string_view(snapshot.blocks[index].id) == id) {
            return snapshot.blocks[index];
        }
    }
    return std::nullopt;
}

} // namespace

// **A tick that measured nothing is not silence**. The audio thread
// accumulates and the sampler empties, and the two run at rates that have
// nothing to do with each other: the sampler every 16 ms, a cycle every 21.3
// ms at a quantum of 1024. So about one tick in four used to take an empty
// accumulator and publish it as a level, and the whole canvas went dark and
// light again sixteen times a second — the flicker the report showed, in
// which the state dots and the meters changed on every single frame.
//
// A steady tone is the case: while it plays, no frame the sampler publishes
// may say the block is quiet.
TEST_F(EngineThroughTheFacade, TheMetersDoNotBlinkBetweenAudioCycles)
{
    QTemporaryDir sessions;
    ASSERT_TRUE(sessions.isValid());
    const dsp::BlockRegistry registry = testRegistry();
    Engine engine(registry, SessionStore{sessions.path()});

    QSignalSpy opened(&engine, &EngineFacade::sessionOpened);
    QSignalSpy errors(&engine, &EngineFacade::error);

    engine.connectToPipeWire();
    Waiter pump;
    ASSERT_TRUE(pump.pumpUntil([&] { return engine.isConnected(); }));

    engine.createSession(u"Steady"_s);
    const QString id = opened.back().at(0).toString();
    engine.applyEdit(id, addBlockEdit(u"src"_s, test::PassThroughSource::kTypeId));
    engine.applyEdit(id, addBlockEdit(u"fx"_s, test::FixedGainEffect::kTypeId));
    engine.applyEdit(id, addBlockEdit(u"out"_s, test::PassThroughOutput::kTypeId));
    for (std::uint32_t channel = 0; channel < 2; ++channel) {
        engine.applyEdit(id, connectEdit(u"src"_s, channel, u"fx"_s, channel));
        engine.applyEdit(id, connectEdit(u"fx"_s, channel, u"out"_s, channel));
    }

    const core::TelemetrySlot *slot = engine.telemetryOf(id);
    ASSERT_NE(slot, nullptr);
    engine.start(id);
    ASSERT_TRUE(errors.isEmpty()) << errors.back().at(0).toString().toStdString();
    const std::optional<Graph::Node> ours = pump.awaitNode(engine.runtimeOf(id)->node()->nodeName());
    ASSERT_TRUE(ours.has_value());
    ASSERT_TRUE(pump.awaitPorts(ours->id, "input", 2));

    FakeApp app(appOptions("SteadyApp", "steadyapp"));
    const std::optional<Graph::Node> stream = pump.awaitMatch(playbackStreamOf("SteadyApp"));
    ASSERT_TRUE(stream.has_value());
    ASSERT_TRUE(pump.awaitPorts(stream->id, "output", 2));
    ASSERT_EQ(linkByChannel(stream->id, ours->id), 2U);

    ASSERT_TRUE(pump.pumpUntil([&] {
        const std::optional<core::BlockTelemetry> source = blockOf(slot->load(), "src");
        return source.has_value() && source->active;
    })) << "the source block never showed a signal";

    // Every frame from here on, for about a second: long enough that a
    // sampler beating against the quantum would have taken an empty
    // accumulator many times over.
    constexpr int kFramesWanted = 60;
    int seen = 0;
    int quiet = 0;
    double lowestDb = 0.0;
    std::uint64_t lastFrame = 0;
    ASSERT_TRUE(pump.pumpUntil([&] {
        const core::TelemetrySnapshot snapshot = slot->load();
        if (snapshot.frame == lastFrame) {
            return false;
        }
        lastFrame = snapshot.frame;
        const std::optional<core::BlockTelemetry> source = blockOf(snapshot, "src");
        if (!source.has_value()) {
            return false;
        }
        ++seen;
        if (!source->active) {
            ++quiet;
        }
        lowestDb = std::min(lowestDb, static_cast<double>(dsp::gainToDb(source->peak[0])));
        return seen >= kFramesWanted;
    })) << "the sampler did not publish "
        << kFramesWanted << " frames";

    EXPECT_EQ(quiet, 0) << quiet << " of " << seen
                        << " frames said the block was quiet while a steady tone was playing";
    // And the level itself is steady, not merely non-zero: a frame that
    // reported a fraction of the tone would flicker just as visibly.
    EXPECT_NEAR(lowestDb, kTonePeakDb, 1.0) << "the level dipped between cycles";

    engine.stop(id);
}

TEST_F(EngineThroughTheFacade, TheMetersFollowTheSoundWhileTheSessionRuns)
{
    QTemporaryDir sessions;
    ASSERT_TRUE(sessions.isValid());
    const dsp::BlockRegistry registry = testRegistry();
    Engine engine(registry, SessionStore{sessions.path()});

    QSignalSpy opened(&engine, &EngineFacade::sessionOpened);
    QSignalSpy errors(&engine, &EngineFacade::error);

    engine.connectToPipeWire();
    Waiter pump;
    ASSERT_TRUE(pump.pumpUntil([&] { return engine.isConnected(); }));

    engine.createSession(u"Meters"_s);
    const QString id = opened.back().at(0).toString();
    engine.applyEdit(id, addBlockEdit(u"src"_s, test::PassThroughSource::kTypeId));
    engine.applyEdit(id, addBlockEdit(u"fx"_s, test::FixedGainEffect::kTypeId));
    engine.applyEdit(id, addBlockEdit(u"out"_s, test::PassThroughOutput::kTypeId));
    for (std::uint32_t channel = 0; channel < 2; ++channel) {
        engine.applyEdit(id, connectEdit(u"src"_s, channel, u"fx"_s, channel));
        engine.applyEdit(id, connectEdit(u"fx"_s, channel, u"out"_s, channel));
    }

    // Before it runs there is a slot to read, and it says nothing is
    // happening. An interface can hold this pointer from the moment the
    // session opens.
    const core::TelemetrySlot *slot = engine.telemetryOf(id);
    ASSERT_NE(slot, nullptr);
    EXPECT_FALSE(slot->load().running);

    engine.start(id);
    ASSERT_TRUE(errors.isEmpty()) << errors.back().at(0).toString().toStdString();
    ASSERT_NE(engine.runtimeOf(id), nullptr);
    const std::optional<Graph::Node> ours = pump.awaitNode(engine.runtimeOf(id)->node()->nodeName());
    ASSERT_TRUE(ours.has_value());
    ASSERT_TRUE(pump.awaitPorts(ours->id, "input", 2));

    FakeApp app(appOptions("MeterApp", "meterapp"));
    const std::optional<Graph::Node> stream = pump.awaitMatch(playbackStreamOf("MeterApp"));
    ASSERT_TRUE(stream.has_value());
    ASSERT_TRUE(pump.awaitPorts(stream->id, "output", 2));
    ASSERT_EQ(linkByChannel(stream->id, ours->id), 2U);

    // **The activity flag is what the wires and the state dots go by**, so
    // what matters is how soon after the sound starts it says so.
    //
    // **Both blocks, not the first**: the sampler drains one block's
    // meter after another, so a cycle that lands between the two gives a
    // snapshot in which the source has heard the tone and the effect has
    // not yet. The next snapshot has both.
    ASSERT_TRUE(pump.pumpUntil([&] {
        const core::TelemetrySnapshot snapshot = slot->load();
        const std::optional<core::BlockTelemetry> source = blockOf(snapshot, "src");
        const std::optional<core::BlockTelemetry> effect = blockOf(snapshot, "fx");
        return source.has_value() && source->active && effect.has_value() && effect->active;
    })) << "the source and the effect never both showed a signal; the last snapshot was frame "
        << slot->load().frame << " with " << slot->load().blockCount << " blocks";

    const core::TelemetrySnapshot playing = slot->load();
    EXPECT_TRUE(playing.running);
    EXPECT_EQ(playing.sampleRate, 48000U);
    EXPECT_GT(playing.frame, 1U) << "the sampler published only once";

    const std::optional<core::BlockTelemetry> source = blockOf(playing, "src");
    const std::optional<core::BlockTelemetry> effect = blockOf(playing, "fx");
    ASSERT_TRUE(source.has_value());
    ASSERT_TRUE(effect.has_value());
    EXPECT_EQ(source->channelCount, 2U);
    // Every block has a meter, not only the ones with a port on the node:
    // the tone arrives at −12 dBFS and the effect halves it.
    EXPECT_NEAR(dsp::gainToDb(source->peak[0]), kTonePeakDb, 1.0);
    EXPECT_NEAR(dsp::gainToDb(effect->peak[0]), kTonePeakDb - 6.02, 1.0);
    EXPECT_FALSE(source->clipped);
    EXPECT_TRUE(effect->enabled);

    // And when the sound stops, the flag follows within the hundred
    // milliseconds the interface is allowed. The gap between the last
    // snapshot that heard something and the first that did not is the
    // longest the flag can have been wrong for.
    app.stop();
    qint64 lastHeard = 0;
    QElapsedTimer sinceStop;
    sinceStop.start();
    ASSERT_TRUE(pump.pumpUntil([&] {
        const std::optional<core::BlockTelemetry> now = blockOf(slot->load(), "src");
        if (now.has_value() && now->active) {
            lastHeard = sinceStop.elapsed();
            return false;
        }
        return true;
    })) << "the source block went on showing a signal after the application stopped";
    EXPECT_LE(sinceStop.elapsed() - lastHeard, 100) << "the activity flag took too long to fall";

    engine.stop(id);
    // A session that has stopped publishes silence rather than leaving the
    // meters where the music did.
    const core::TelemetrySnapshot stopped = slot->load();
    EXPECT_FALSE(stopped.running);
    EXPECT_EQ(stopped.blockCount, 0U);
}

// **A virtual device asked for through the interface is made in the real
// graph**: the id comes back first, then the list that shows it, and
// the list follows the node into the graph. A refusal changes nothing and
// says why in the engine's words.
// **The live spectrum of one block, taken only while asked for**:
// a 1 kHz tone through a running session shows in the band at 1 kHz, for
// the block asked about and no other, and asking for nobody stops the copy.
TEST_F(EngineThroughTheFacade, OneBlocksSpectrumIsTakenWhileItIsAskedFor)
{
    QTemporaryDir sessions;
    ASSERT_TRUE(sessions.isValid());
    const dsp::BlockRegistry registry = testRegistry();
    Engine engine(registry, SessionStore{sessions.path()});

    QSignalSpy opened(&engine, &EngineFacade::sessionOpened);
    QSignalSpy errors(&engine, &EngineFacade::error);

    engine.connectToPipeWire();
    Waiter pump;
    ASSERT_TRUE(pump.pumpUntil([&] { return engine.isConnected(); }));

    engine.createSession(u"Spectrum"_s);
    const QString id = opened.back().at(0).toString();
    engine.applyEdit(id, addBlockEdit(u"src"_s, test::PassThroughSource::kTypeId));
    engine.applyEdit(id, addBlockEdit(u"fx"_s, test::FixedGainEffect::kTypeId));
    engine.applyEdit(id, addBlockEdit(u"out"_s, test::PassThroughOutput::kTypeId));
    for (std::uint32_t channel = 0; channel < 2; ++channel) {
        engine.applyEdit(id, connectEdit(u"src"_s, channel, u"fx"_s, channel));
        engine.applyEdit(id, connectEdit(u"fx"_s, channel, u"out"_s, channel));
    }
    const core::TelemetrySlot *slot = engine.telemetryOf(id);
    ASSERT_NE(slot, nullptr);

    engine.start(id);
    ASSERT_TRUE(errors.isEmpty()) << errors.back().at(0).toString().toStdString();
    const std::optional<Graph::Node> ours = pump.awaitNode(engine.runtimeOf(id)->node()->nodeName());
    ASSERT_TRUE(ours.has_value());
    ASSERT_TRUE(pump.awaitPorts(ours->id, "input", 2));
    FakeApp app(appOptions("SpectrumApp", "spectrumapp"));
    const std::optional<Graph::Node> stream = pump.awaitMatch(playbackStreamOf("SpectrumApp"));
    ASSERT_TRUE(stream.has_value());
    ASSERT_TRUE(pump.awaitPorts(stream->id, "output", 2));
    ASSERT_EQ(linkByChannel(stream->id, ours->id), 2U);
    ASSERT_TRUE(pump.pumpUntil([&] {
        const std::optional<core::BlockTelemetry> effect = blockOf(slot->load(), "fx");
        return effect.has_value() && effect->active;
    }));

    const auto analysing = [&engine, &id](const QString &block) {
        for (const SessionRuntime::MeteredBlock &metered : engine.runtimeOf(id)->meters()) {
            if (metered.id == block) {
                return metered.meter->isAnalysing();
            }
        }
        return false;
    };

    // **Nobody has asked: nothing is copied and nothing is published.**
    pump.pump(std::chrono::milliseconds{100});
    EXPECT_EQ(slot->load().spectrumBands, 0U);
    EXPECT_FALSE(analysing(u"src"_s) || analysing(u"fx"_s)) << "a block was copied out before anybody asked";

    engine.analyse(id, u"fx"_s, core::kBothChannels);
    // The tone's band is the loudest once a few frames have been averaged.
    std::size_t toneBand = 0;
    for (std::size_t band = 0; band < core::kSpectrumBands; ++band) {
        if (core::spectrumBandCentreHz(band) < 1000.0) {
            toneBand = band + 1;
        }
    }
    const auto loudest = [](const core::TelemetrySnapshot &snapshot) {
        return static_cast<std::size_t>(
            std::distance(std::begin(snapshot.spectrum),
                          std::max_element(std::begin(snapshot.spectrum), std::end(snapshot.spectrum))));
    };
    ASSERT_TRUE(pump.pumpUntil([&] {
        const core::TelemetrySnapshot snapshot = slot->load();
        return snapshot.spectrumBands == core::kSpectrumBands && std::string(snapshot.analysedId) == "fx"
            && snapshot.spectrum[loudest(snapshot)] > -40.0f;
    })) << "the effect's spectrum never arrived";
    pump.pump(std::chrono::milliseconds{500});
    const core::TelemetrySnapshot looked = slot->load();
    const std::size_t peak = loudest(looked);
    EXPECT_LE(std::abs(static_cast<long>(peak) - static_cast<long>(toneBand)), 1L)
        << "the 1 kHz tone showed in the band at " << core::spectrumBandCentreHz(peak) << " Hz";
    // The effect halves the -12 dBFS tone, and a band that holds the whole
    // of a sine reads its level against full scale.
    EXPECT_NEAR(looked.spectrum[peak], kTonePeakDb - 6.02, 3.0);
    EXPECT_TRUE(analysing(u"fx"_s));
    EXPECT_FALSE(analysing(u"src"_s)) << "a block nobody asked about was copied out";

    // Another block asked for instead: the first stops being copied.
    engine.analyse(id, u"src"_s, core::kBothChannels);
    ASSERT_TRUE(pump.pumpUntil([&] { return std::string(slot->load().analysedId) == "src"; }));
    EXPECT_FALSE(analysing(u"fx"_s));
    EXPECT_TRUE(analysing(u"src"_s));

    // Nobody: nothing copied, nothing published.
    engine.analyse(id, QString{}, core::kBothChannels);
    ASSERT_TRUE(pump.pumpUntil([&] { return slot->load().spectrumBands == 0U; }));
    EXPECT_FALSE(analysing(u"src"_s) || analysing(u"fx"_s));

    engine.stop(id);
}

// **A block's band values and the sound before it, through the real engine**:
// a block publishing 64 band values in a running session, analysed,
// has every one in the snapshot; the spectrum before it holds the tone at
// the tone's level, and the one after it the same tone half as loud, which
// is all this block does to it.
TEST_F(EngineThroughTheFacade, AnAnalysedBlocksBandValuesAndTheSoundBeforeItReachTheSnapshot)
{
    QTemporaryDir sessions;
    ASSERT_TRUE(sessions.isValid());
    const dsp::BlockRegistry registry = testRegistry();
    Engine engine(registry, SessionStore{sessions.path()});

    QSignalSpy opened(&engine, &EngineFacade::sessionOpened);
    QSignalSpy errors(&engine, &EngineFacade::error);

    engine.connectToPipeWire();
    Waiter pump;
    ASSERT_TRUE(pump.pumpUntil([&] { return engine.isConnected(); }));

    engine.createSession(u"Bands"_s);
    const QString id = opened.back().at(0).toString();
    engine.applyEdit(id, addBlockEdit(u"src"_s, test::PassThroughSource::kTypeId));
    engine.applyEdit(id, addBlockEdit(u"bands"_s, test::BandWriterEffect::kTypeId));
    engine.applyEdit(id, addBlockEdit(u"out"_s, test::PassThroughOutput::kTypeId));
    for (std::uint32_t channel = 0; channel < 2; ++channel) {
        engine.applyEdit(id, connectEdit(u"src"_s, channel, u"bands"_s, channel));
        engine.applyEdit(id, connectEdit(u"bands"_s, channel, u"out"_s, channel));
    }
    const core::TelemetrySlot *slot = engine.telemetryOf(id);
    ASSERT_NE(slot, nullptr);

    engine.start(id);
    ASSERT_TRUE(errors.isEmpty()) << errors.back().at(0).toString().toStdString();
    const std::optional<Graph::Node> ours = pump.awaitNode(engine.runtimeOf(id)->node()->nodeName());
    ASSERT_TRUE(ours.has_value());
    ASSERT_TRUE(pump.awaitPorts(ours->id, "input", 2));
    FakeApp app(appOptions("BandsApp", "bandsapp"));
    const std::optional<Graph::Node> stream = pump.awaitMatch(playbackStreamOf("BandsApp"));
    ASSERT_TRUE(stream.has_value());
    ASSERT_TRUE(pump.awaitPorts(stream->id, "output", 2));
    ASSERT_EQ(linkByChannel(stream->id, ours->id), 2U);
    ASSERT_TRUE(pump.pumpUntil([&] {
        const std::optional<core::BlockTelemetry> block = blockOf(slot->load(), "bands");
        return block.has_value() && block->active;
    }));

    // Nobody has asked: no band values and no spectrum before anything.
    pump.pump(std::chrono::milliseconds{100});
    EXPECT_EQ(slot->load().bandValueCount, 0U);
    EXPECT_EQ(slot->load().inputSpectrumBands, 0U);

    engine.analyse(id, u"bands"_s, core::kBothChannels);
    std::size_t toneBand = 0;
    for (std::size_t band = 0; band < core::kSpectrumBands; ++band) {
        if (core::spectrumBandCentreHz(band) < 1000.0) {
            toneBand = band + 1;
        }
    }
    ASSERT_TRUE(pump.pumpUntil([&] {
        const core::TelemetrySnapshot snapshot = slot->load();
        return snapshot.bandValueCount == rt::kMaxBandValues && snapshot.inputSpectrumBands == core::kSpectrumBands
            && snapshot.inputSpectrum[toneBand] > -40.0f;
    })) << "the band values or the spectrum before the block never arrived";
    pump.pump(std::chrono::milliseconds{500});
    const core::TelemetrySnapshot looked = slot->load();
    for (std::size_t band = 0; band < rt::kMaxBandValues; ++band) {
        EXPECT_FLOAT_EQ(looked.bandValues[band], test::BandWriterEffect::bandValue(band)) << band;
    }
    const auto loudest = [](const float (&bands)[core::kSpectrumBands]) {
        return static_cast<std::size_t>(
            std::distance(std::begin(bands), std::max_element(std::begin(bands), std::end(bands))));
    };
    EXPECT_LE(std::abs(static_cast<long>(loudest(looked.inputSpectrum)) - static_cast<long>(toneBand)), 1L);
    EXPECT_NEAR(looked.inputSpectrum[toneBand], kTonePeakDb, 3.0);
    const float halved = looked.spectrum[toneBand] - looked.inputSpectrum[toneBand];
    std::printf("[measure] the tone after a block that halves it, less before it: %.2f dB\n",
                static_cast<double>(halved));
    EXPECT_NEAR(halved, -6.02f, 1.0f) << "the two spectra are not of the same sound";

    // One side only: said in the snapshot, and still the tone, which is on
    // both.
    engine.analyse(id, u"bands"_s, 0);
    ASSERT_TRUE(pump.pumpUntil([&] { return slot->load().analysedChannel == 0; }));

    // Nobody: none of it.
    engine.analyse(id, QString{}, core::kBothChannels);
    ASSERT_TRUE(pump.pumpUntil([&] {
        const core::TelemetrySnapshot snapshot = slot->load();
        return snapshot.bandValueCount == 0 && snapshot.inputSpectrumBands == 0;
    }));

    engine.stop(id);
}

TEST_F(EngineThroughTheFacade, AVirtualDeviceAskedForIsListedAndMadeInTheGraph)
{
    QTemporaryDir sessions;
    QTemporaryDir files;
    ASSERT_TRUE(sessions.isValid());
    ASSERT_TRUE(files.isValid());
    const dsp::BlockRegistry registry = testRegistry();
    Engine engine(registry, SessionStore{sessions.path()});
    // Never the user's own files, nor /etc's.
    engine.virtualDevices().setStores(
        pw::VirtualDeviceStore(pw::VirtualDeviceScope::User, files.filePath(u"user.conf"_s)),
        pw::VirtualDeviceStore(pw::VirtualDeviceScope::System, files.filePath(u"system.conf"_s)));

    QStringList heard;
    QString lastList;
    QObject::connect(
        &engine, &EngineFacade::virtualDeviceCreated, [&](const QString &id) { heard.append(u"created "_s + id); });
    QObject::connect(&engine, &EngineFacade::virtualDevicesChanged, [&](const QString &json) {
        heard.append(u"list"_s);
        lastList = json;
    });
    QSignalSpy errors(&engine, &EngineFacade::error);
    const auto listed = [&lastList](const QString &id) -> QJsonObject {
        for (const QJsonValue &device :
             QJsonDocument::fromJson(lastList.toUtf8()).object().value("devices"_L1).toArray()) {
            if (device.toObject().value("id"_L1).toString() == id) {
                return device.toObject();
            }
        }
        return {};
    };

    engine.connectToPipeWire();
    Waiter pump;
    ASSERT_TRUE(pump.pumpUntil([&] { return engine.isConnected(); }));

    heard.clear();
    engine.createVirtualDevice(u"{\"name\": \"Facade output\", \"direction\": \"output\"}"_s);
    ASSERT_EQ(errors.count(), 0) << errors.value(0).value(0).toString().toStdString();
    ASSERT_GE(heard.size(), 2);
    ASSERT_TRUE(heard.at(0).startsWith(u"created "_s)) << heard.join(u", "_s).toStdString();
    EXPECT_EQ(heard.at(1), u"list"_s) << "the list answers the call, after the id";
    const QString id = heard.at(0).mid(8);
    EXPECT_EQ(listed(id).value("name"_L1).toString(), u"Facade output"_s);
    EXPECT_EQ(listed(id).value("channels"_L1).toString(), u"stereo"_s) << "stereo when nothing was said";

    // The list follows the node into the graph by itself.
    ASSERT_TRUE(pump.pumpUntil([&] { return listed(id).value("status"_L1).toString() == "present"_L1; }))
        << lastList.toStdString();

    // A refusal says why, and nothing changes.
    engine.renameVirtualDevice(u"00000000"_s, u"Nobody"_s);
    ASSERT_EQ(errors.count(), 1);
    EXPECT_EQ(errors.back().at(0).toString(), u"device.virtualRefused"_s);
    EXPECT_EQ(QJsonDocument::fromJson(errors.back().at(1).toString().toUtf8()).object().value("reason"_L1),
              u"NotFound"_s);

    // Deleting it is answered too, and takes it out of the graph.
    heard.clear();
    engine.deleteVirtualDevice(id);
    EXPECT_EQ(heard, QStringList{u"list"_s}) << "one call, one answer";
    EXPECT_TRUE(listed(id).isEmpty());
    ASSERT_TRUE(pump.pumpUntil([&] { return !engine.virtualDevices().device(id).has_value(); }));
}

TEST_F(EngineThroughTheFacade, SaysTheGraphsClockAndDriverWithNothingRunning)
{
    // With no session running, AudioSystemChanged names the driver and
    // carries the graph's rate and quantum, and a quantum forced from
    // outside reaches the interface within a second.
    QTemporaryDir sessions;
    ASSERT_TRUE(sessions.isValid());
    const dsp::BlockRegistry registry = testRegistry();
    Engine engine(registry, SessionStore{sessions.path()});
    QSignalSpy said(&engine, &EngineFacade::audioSystemChanged);
    engine.connectToPipeWire();
    Waiter pump;
    ASSERT_TRUE(pump.pumpUntil([&] { return engine.isConnected(); }));

    const auto last = [&said] {
        return said.isEmpty() ? AudioSystemInfo{} : audioSystemFromJson(said.back().at(0).toString());
    };
    ASSERT_TRUE(pump.pumpUntil([&] { return last().quantum != 0 && !last().driver.isEmpty(); }))
        << "the engine never said the graph's clock and driver";
    EXPECT_EQ(last().rate, 48000);
    EXPECT_EQ(last().quantum, 1024);
    EXPECT_EQ(last().driver, u"Test sink (stereo)"_s) << "the driver is named by its description";

    CommandResult forced = runCommand({"pw-metadata", "-n", "settings", "0", "clock.force-quantum", "256"});
    ASSERT_EQ(forced.status, 0) << forced.output;
    EXPECT_TRUE(pump.pumpUntil([&] { return last().quantum == 256; }, std::chrono::milliseconds{1000}))
        << "the forced quantum did not reach the interface within a second";
    forced = runCommand({"pw-metadata", "-n", "settings", "0", "clock.force-quantum", "0"});
    ASSERT_EQ(forced.status, 0) << forced.output;
    EXPECT_TRUE(pump.pumpUntil([&] { return last().quantum == 1024; }, std::chrono::milliseconds{1000}));
}

TEST_F(EngineThroughTheFacade, DiagnosticsDescribeOurNodeItsLinksAndHowTheSessionKeepsUp)
{
    QTemporaryDir sessions;
    ASSERT_TRUE(sessions.isValid());
    const dsp::BlockRegistry registry = testRegistry();
    Engine engine(registry, SessionStore{sessions.path()});
    QSignalSpy opened(&engine, &EngineFacade::sessionOpened);
    QSignalSpy errors(&engine, &EngineFacade::error);
    QSignalSpy answered(&engine, &EngineFacade::diagnostics);

    engine.connectToPipeWire();
    Waiter pump;
    ASSERT_TRUE(pump.pumpUntil([&] { return engine.isConnected(); }));

    engine.createSession(u"Diagnosed"_s);
    ASSERT_EQ(opened.count(), 1);
    const QString id = opened.back().at(0).toString();
    engine.applyEdit(id, addBlockEdit(u"src"_s, test::PassThroughSource::kTypeId));
    engine.applyEdit(id, addBlockEdit(u"fx"_s, test::FixedGainEffect::kTypeId));
    engine.applyEdit(id, addBlockEdit(u"out"_s, test::PassThroughOutput::kTypeId));
    for (std::uint32_t channel = 0; channel < 2; ++channel) {
        engine.applyEdit(id, connectEdit(u"src"_s, channel, u"fx"_s, channel));
        engine.applyEdit(id, connectEdit(u"fx"_s, channel, u"out"_s, channel));
    }
    engine.start(id);
    ASSERT_TRUE(errors.isEmpty()) << errors.back().at(0).toString().toStdString();
    ASSERT_NE(engine.runtimeOf(id), nullptr);
    const QString nodeName = engine.runtimeOf(id)->node()->nodeName();
    const std::optional<Graph::Node> ours = pump.awaitNode(nodeName);
    ASSERT_TRUE(ours.has_value());
    ASSERT_TRUE(pump.awaitPorts(ours->id, "input", 2));

    FakeApp app(appOptions("DiagnosedApp", "diagnosedapp"));
    const std::optional<Graph::Node> stream = pump.awaitMatch(playbackStreamOf("DiagnosedApp"));
    ASSERT_TRUE(stream.has_value());
    ASSERT_TRUE(pump.awaitPorts(stream->id, "output", 2));
    ASSERT_EQ(linkByChannel(stream->id, ours->id), 2U);
    const QString appNode = QString::fromStdString(stream->prop("node.name"));
    // The application's links into the session, as the answer names them.
    const auto linksIntoUs = [&](const QJsonObject &answer) {
        int count = 0;
        for (const QJsonValue &value : answer.value("graph"_L1).toObject().value("links"_L1).toArray()) {
            const QJsonObject link = value.toObject();
            if (link.value("from"_L1).toString().startsWith(appNode + u':')
                && link.value("to"_L1).toString().startsWith(nodeName + u':')) {
                ++count;
            }
        }
        return count;
    };
    // **Asked until the engine has seen the links** made just now, and the
    // node has run a cycle — until then it cannot say whether it is
    // real-time. Each question is answered at once.
    QJsonObject answer;
    const bool seen = pump.pumpUntil([&] {
        const qsizetype before = answered.count();
        engine.getDiagnostics();
        if (answered.count() != before + 1) {
            return false;
        }
        answer = QJsonDocument::fromJson(answered.back().at(0).toString().toUtf8()).object();
        const QJsonObject first = answer.value("sessions"_L1).toArray().at(0).toObject();
        return linksIntoUs(answer) == 2 && first.value("cycles"_L1).toInteger() > 0;
    });
    ASSERT_TRUE(seen) << "the answer never showed the application's two links into the session: "
                      << QJsonDocument(answer.value("graph"_L1).toObject()).toJson().toStdString();

    const QJsonObject about = answer.value("engine"_L1).toObject();
    EXPECT_EQ(about.value("pid"_L1).toInteger(), QCoreApplication::applicationPid());
    EXPECT_FALSE(about.value("pipeWireLibrary"_L1).toString().isEmpty());
    EXPECT_FALSE(answer.value("audioSystem"_L1).toObject().value("serverVersion"_L1).toString().isEmpty());

    const QJsonArray sessionsJson = answer.value("sessions"_L1).toArray();
    ASSERT_EQ(sessionsJson.size(), 1);
    const QJsonObject session = sessionsJson.at(0).toObject();
    EXPECT_EQ(session.value("name"_L1).toString(), u"Diagnosed"_s);
    EXPECT_TRUE(session.value("running"_L1).toBool());
    EXPECT_GT(session.value("cycles"_L1).toInteger(), 0);
    EXPECT_GT(session.value("rate"_L1).toInteger(), 0);
    EXPECT_GT(session.value("quantum"_L1).toInteger(), 0);
    // **Known, either way**: rtkit may have refused this run.
    EXPECT_TRUE(session.value("realtime"_L1).isBool()) << "a node that has run a cycle knows";
    EXPECT_TRUE(session.value("xrunHistory"_L1).isArray());

    const QJsonObject graph = answer.value("graph"_L1).toObject();
    QStringList nodeNames;
    for (const QJsonValue &node : graph.value("nodes"_L1).toArray()) {
        nodeNames.append(node.toObject().value("name"_L1).toString());
    }
    EXPECT_TRUE(nodeNames.contains(nodeName)) << nodeNames.join(u", "_s).toStdString();
    EXPECT_FALSE(nodeNames.contains(appNode)) << "only our nodes are listed";
    EXPECT_EQ(linksIntoUs(answer), 2) << "named at both ends";

    EXPECT_EQ(answer.value("recordingFormats"_L1).toArray().size(), 8);
    EXPECT_TRUE(answer.value("plugins"_L1).toObject().contains("total"_L1));
    EXPECT_TRUE(answer.value("log"_L1).isArray());

    engine.stop(id);
}
