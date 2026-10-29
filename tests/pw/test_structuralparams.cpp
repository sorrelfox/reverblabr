// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Structural parameters on a running session. A parameter the
// session plan reads — which device to play to, which streams to capture —
// is not something a block can be handed; what has to change for it is
// links and ports. These tests set one through the engine's own interface
// while a tone is playing and watch the links move and the sound with them,
// which is the only evidence that settles it: an update() that was called
// proves nothing about where the audio went.
//
// And the other half of the bargain: a knob that is being dragged must
// still cost nothing but a number on a queue.

#include "blocks/applicationsource.h"
#include "blocks/builtinblocks.h"
#include "blocks/outputdevice.h"
#include "blocks/systemaudio.h"
#include "core/editcommand.h"
#include "core/engine.h"
#include "core/session.h"
#include "core/sessionruntime.h"
#include "core/sessionstore.h"
#include "dsp/blockregistry.h"
#include "sessionharness.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <optional>
#include <string>

using namespace rb;
using namespace rb::core;
using namespace rb::pwtest;
using namespace Qt::StringLiterals;

namespace
{

[[nodiscard]] dsp::BlockRegistry builtinRegistry()
{
    dsp::BlockRegistry registry;
    EXPECT_TRUE(blocks::registerBuiltinBlocks(registry));
    return registry;
}

[[nodiscard]] QString asJson(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

/// A value as SetParam takes it over the facade: wrapped in an array, so
/// that a bare string or number is a JSON document of its own.
[[nodiscard]] QString valueJson(const QJsonValue &value)
{
    return QString::fromUtf8(QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact));
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

/// A parameter as part of an edit, which is how a session is set up before
/// it runs. What the tests here are about is the other path: setParam on a
/// session that is already running.
[[nodiscard]] QString paramEdit(const QString &blockId, const QString &key, const QJsonValue &value)
{
    return asJson(editCommandToJson(SetParam{blockId, key, value}));
}

/// A capture filter that calls itself a sink, so that a test can hear what
/// an output block sent it. A null sink would swallow it.
[[nodiscard]] CaptureNodeOptions sinkOptions(const std::string &name)
{
    CaptureNodeOptions options;
    options.name = name;
    options.channels = {"FL", "FR"};
    options.mediaClass = "Audio/Sink";
    options.sineHz = 1000.0;
    return options;
}

/// The peak on a port, or silence if the port never ran.
[[nodiscard]] double peakOf(const std::map<std::string, double> &measured, const std::string &channel)
{
    const auto found = measured.find(channel);
    return found == measured.end() ? -200.0 : found->second;
}

} // namespace

using Structural = SessionTest;

TEST_F(Structural, ChoosingAnotherOutputDeviceMovesTheSoundToItWithoutARestart)
{
    QTemporaryDir sessions;
    ASSERT_TRUE(sessions.isValid());
    const dsp::BlockRegistry registry = builtinRegistry();
    Engine engine(registry, SessionStore{sessions.path()});
    QSignalSpy opened(&engine, &EngineFacade::sessionOpened);
    QSignalSpy errors(&engine, &EngineFacade::error);

    engine.connectToPipeWire();
    Waiter pump;
    ASSERT_TRUE(pump.pumpUntil([&] { return engine.isConnected(); }));

    CaptureNode chosen(sinkOptions("rbtest.structural.first"));
    CaptureNode wanted(sinkOptions("rbtest.structural.second"));
    const std::optional<Graph::Node> chosenNode = chosen.waitUntilReady();
    const std::optional<Graph::Node> wantedNode = wanted.waitUntilReady();
    ASSERT_TRUE(chosenNode.has_value() && wantedNode.has_value());

    engine.createSession(u"Structural"_s);
    ASSERT_EQ(opened.count(), 1);
    const QString id = opened.back().at(0).toString();
    engine.applyEdit(id, addBlockEdit(u"src"_s, blocks::ApplicationSource::kTypeId));
    engine.applyEdit(id, addBlockEdit(u"out"_s, blocks::OutputDevice::kTypeId));
    engine.applyEdit(id, paramEdit(u"src"_s, u"binary"_s, u"structapp"_s));
    engine.applyEdit(id, paramEdit(u"src"_s, u"outputMode"_s, u"any"_s));
    engine.applyEdit(id, paramEdit(u"out"_s, u"device"_s, u"rbtest.structural.first"_s));
    for (std::uint32_t channel = 0; channel < 2; ++channel) {
        engine.applyEdit(id, connectEdit(u"src"_s, channel, u"out"_s, channel));
    }

    engine.start(id);
    ASSERT_TRUE(errors.isEmpty()) << errors.back().at(0).toString().toStdString();
    ASSERT_NE(engine.runtimeOf(id), nullptr);
    const std::optional<Graph::Node> ours = pump.awaitNode(engine.runtimeOf(id)->node()->nodeName());
    ASSERT_TRUE(ours.has_value());

    FakeApp app(appOptions("StructApp", "structapp"));
    ASSERT_TRUE(pump.awaitMatch(playbackStreamOf("StructApp")).has_value());
    ASSERT_TRUE(pump.awaitLink(ours->id, chosenNode->id)) << "the session never reached the device it was pointed at";
    pump.pump(std::chrono::milliseconds{600});
    const std::map<std::string, SineCheck> before = chosen.measureSine(300ms);
    ASSERT_TRUE(before.contains("FL"));
    ASSERT_NEAR(before.at("FL").peakDb, kTonePeakDb, 0.5) << "the tone never arrived at the first device";

    // **The moment this issue is about.** Somebody chooses another device
    // while the session is running, and nothing else happens: no edit, no
    // restart, one setParam through the same interface the panel uses.
    engine.setParam(id, u"out"_s, u"device"_s, valueJson(u"rbtest.structural.second"_s));
    ASSERT_TRUE(errors.isEmpty()) << errors.back().at(0).toString().toStdString();

    ASSERT_TRUE(pump.awaitLink(ours->id, wantedNode->id)) << "the device was chosen and the links did not move";
    // **Nothing left behind**: a link to the device nobody chose any more
    // would go on playing there, which is the one thing a person switching
    // devices does not want.
    EXPECT_FALSE(Graph::capture().linked(ours->id, chosenNode->id)) << "the old device is still being played to";

    pump.pump(std::chrono::milliseconds{600});
    const std::map<std::string, SineCheck> heard = wanted.measureSine(300ms);
    ASSERT_TRUE(heard.contains("FL"));
    EXPECT_NEAR(heard.at("FL").peakDb, kTonePeakDb, 0.5) << "the sound did not follow the device that was chosen";
    EXPECT_LT(heard.at("FL").residualDb, -55.0) << "the move was heard as a click rather than a fade";
    EXPECT_LT(peakOf(chosen.measure(300ms), "FL"), kTonePeakDb - 40.0)
        << "the sound is still going to the device nobody chose any more";

    engine.stop(id);
}

TEST_F(Structural, AskingForAlertSoundsCapturesThemOnARunningSession)
{
    QTemporaryDir sessions;
    ASSERT_TRUE(sessions.isValid());
    const dsp::BlockRegistry registry = builtinRegistry();
    Engine engine(registry, SessionStore{sessions.path()});
    QSignalSpy opened(&engine, &EngineFacade::sessionOpened);
    QSignalSpy errors(&engine, &EngineFacade::error);

    engine.connectToPipeWire();
    Waiter pump;
    ASSERT_TRUE(pump.pumpUntil([&] { return engine.isConnected(); }));

    CaptureNode device(sinkOptions("rbtest.structural.alerts"));
    const std::optional<Graph::Node> deviceNode = device.waitUntilReady();
    ASSERT_TRUE(deviceNode.has_value());

    engine.createSession(u"Alerts"_s);
    const QString id = opened.back().at(0).toString();
    engine.applyEdit(id, addBlockEdit(u"sys"_s, blocks::SystemAudio::kTypeId));
    engine.applyEdit(id, addBlockEdit(u"out"_s, blocks::OutputDevice::kTypeId));
    engine.applyEdit(id, paramEdit(u"out"_s, u"device"_s, u"rbtest.structural.alerts"_s));
    for (std::uint32_t channel = 0; channel < 2; ++channel) {
        engine.applyEdit(id, connectEdit(u"sys"_s, channel, u"out"_s, channel));
    }

    engine.start(id);
    ASSERT_TRUE(errors.isEmpty()) << errors.back().at(0).toString().toStdString();
    ASSERT_NE(engine.runtimeOf(id), nullptr);
    const std::optional<Graph::Node> ours = pump.awaitNode(engine.runtimeOf(id)->node()->nodeName());
    ASSERT_TRUE(ours.has_value());

    // A fake application carrying media.role = Notification, which is what
    // a KDE notification sound looks like in the graph (pw.capture-semantics
    // is where that was established).
    FakeAppOptions alertOptions = appOptions("StructAlert", "structalert");
    alertOptions.mediaRole = "Notification";
    FakeApp alertApp(alertOptions);
    const std::optional<Graph::Node> alert = pump.awaitMatch(playbackStreamOf("StructAlert"));
    ASSERT_TRUE(alert.has_value());
    pump.pump(std::chrono::milliseconds{700});
    ASSERT_FALSE(Graph::capture().linked(alert->id, ours->id)) << "an alert was captured without being asked for";

    // Asked for, on a session that is running and playing.
    engine.setParam(id, u"sys"_s, u"includeAlerts"_s, valueJson(true));
    ASSERT_TRUE(errors.isEmpty()) << errors.back().at(0).toString().toStdString();
    ASSERT_TRUE(pump.awaitLink(alert->id, ours->id)) << "alert sounds were asked for and nothing was linked";

    pump.pump(std::chrono::milliseconds{700});
    EXPECT_NEAR(peakOf(device.measureTone(1000.0, 400ms), "FL"), kTonePeakDb, 0.5)
        << "the alert was linked and never heard";

    engine.stop(id);
}

TEST_F(Structural, ADraggedVolumeReplansNothingAndAChosenDeviceReplansOnce)
{
    QTemporaryDir sessions;
    ASSERT_TRUE(sessions.isValid());
    const dsp::BlockRegistry registry = builtinRegistry();
    Engine engine(registry, SessionStore{sessions.path()});
    QSignalSpy opened(&engine, &EngineFacade::sessionOpened);
    QSignalSpy errors(&engine, &EngineFacade::error);

    engine.connectToPipeWire();
    Waiter pump;
    ASSERT_TRUE(pump.pumpUntil([&] { return engine.isConnected(); }));

    CaptureNode device(sinkOptions("rbtest.structural.drag"));
    const std::optional<Graph::Node> deviceNode = device.waitUntilReady();
    ASSERT_TRUE(deviceNode.has_value());

    engine.createSession(u"Drag"_s);
    const QString id = opened.back().at(0).toString();
    engine.applyEdit(id, addBlockEdit(u"src"_s, blocks::ApplicationSource::kTypeId));
    engine.applyEdit(id, addBlockEdit(u"out"_s, blocks::OutputDevice::kTypeId));
    engine.applyEdit(id, paramEdit(u"src"_s, u"binary"_s, u"dragapp"_s));
    engine.applyEdit(id, paramEdit(u"src"_s, u"outputMode"_s, u"any"_s));
    engine.applyEdit(id, paramEdit(u"out"_s, u"device"_s, u"rbtest.structural.drag"_s));
    for (std::uint32_t channel = 0; channel < 2; ++channel) {
        engine.applyEdit(id, connectEdit(u"src"_s, channel, u"out"_s, channel));
    }

    engine.start(id);
    ASSERT_TRUE(errors.isEmpty()) << errors.back().at(0).toString().toStdString();
    SessionRuntime *runtime = engine.runtimeOf(id);
    ASSERT_NE(runtime, nullptr);
    const std::optional<Graph::Node> ours = pump.awaitNode(runtime->node()->nodeName());
    ASSERT_TRUE(ours.has_value());

    FakeApp app(appOptions("DragApp", "dragapp"));
    ASSERT_TRUE(pump.awaitMatch(playbackStreamOf("DragApp")).has_value());
    ASSERT_TRUE(pump.awaitLink(ours->id, deviceNode->id));
    pump.pump(std::chrono::milliseconds{600});

    // **Two seconds of dragging the output volume**, at the sixty a second
    // the interface coalesces to. The event loop is deliberately left
    // still: nothing else can plan the session while it is, so the count
    // below is this path's and nothing else's.
    const std::uint64_t replans = runtime->replanCount();
    constexpr int kTicks = 120;
    for (int tick = 1; tick <= kTicks; ++tick) {
        engine.setParam(id, u"out"_s, u"volume"_s, valueJson(1.0 - (0.5 * tick) / kTicks));
    }
    EXPECT_EQ(runtime->replanCount(), replans) << "a knob being dragged recompiled the graph";
    ASSERT_TRUE(errors.isEmpty()) << errors.back().at(0).toString().toStdString();

    // And the drag was still heard, which is what the queue is for: the
    // last value of it is half the signal, which is 6.02 dB down.
    pump.pump(std::chrono::milliseconds{600});
    const std::map<std::string, SineCheck> heard = device.measureSine(300ms);
    ASSERT_TRUE(heard.contains("FL"));
    EXPECT_NEAR(heard.at("FL").peakDb, kTonePeakDb - 6.02, 0.2) << "the dragged volume never reached the audio";

    // The other side of it: one structural change is one replan, not none
    // and not one per value.
    const std::uint64_t beforeDevice = runtime->replanCount();
    engine.setParam(id, u"out"_s, u"device"_s, valueJson(QString::fromLatin1(kStereoSink)));
    EXPECT_EQ(runtime->replanCount(), beforeDevice + 1) << "choosing a device did not plan the session again, once";

    engine.stop(id);
}
