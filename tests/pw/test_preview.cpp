// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Listening to a recording against the private PipeWire: the
// preview is a player's stream on the default output, and a session
// running beside it runs exactly as it did — the same rate, the same
// quantum, no lost cycles, not a click in its tone and not a trace of the
// preview in what it plays.

#include "blocks/builtinblocks.h"
#include "blocks/outputdevice.h"
#include "blocks/wave.h"
#include "core/editcommand.h"
#include "core/engine.h"
#include "core/session.h"
#include "core/telemetry.h"
#include "dsp/blockregistry.h"
#include "sessionharness.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <gtest/gtest.h>
#include <sndfile.h>

#include <cmath>
#include <iostream>
#include <numbers>
#include <vector>

using namespace rb;
using namespace rb::core;
using namespace rb::pwtest;
using namespace Qt::StringLiterals;
using namespace std::chrono_literals;

namespace
{

[[nodiscard]] QString asJson(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

[[nodiscard]] QString addBlock(const QString &id, std::string_view type, const QJsonObject &params)
{
    BlockInstance block;
    block.id = id;
    block.type = QString::fromUtf8(type.data(), static_cast<qsizetype>(type.size()));
    block.params = params;
    AddBlock command;
    command.block = block;
    return asJson(editCommandToJson(command));
}

[[nodiscard]] QString connect(const QString &from, const QString &to, std::uint32_t channel)
{
    Wire wire;
    wire.fromBlock = from;
    wire.fromPort = channel;
    wire.toBlock = to;
    wire.toPort = channel;
    return asJson(editCommandToJson(Connect{wire, -1}));
}

/// A recording to listen to: \a seconds of a 440 Hz tone at 44.1 kHz —
/// **another rate than the graph's**, which is the case that could pull
/// the graph over to it if the preview asked.
[[nodiscard]] bool writeTone(const QString &path, double seconds)
{
    SF_INFO info{};
    info.samplerate = 44100;
    info.channels = 2;
    info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
    SNDFILE *file = sf_open(path.toUtf8().constData(), SFM_WRITE, &info);
    if (file == nullptr) {
        return false;
    }
    const auto frames = static_cast<std::size_t>(seconds * 44100.0);
    std::vector<float> samples(frames * 2);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const auto value = static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * 440.0 * frame / 44100.0));
        samples[frame * 2] = value;
        samples[frame * 2 + 1] = value;
    }
    const bool whole =
        sf_writef_float(file, samples.data(), static_cast<sf_count_t>(frames)) == static_cast<sf_count_t>(frames);
    sf_close(file);
    return whole;
}

[[nodiscard]] QJsonObject lastPreview(const QSignalSpy &spy)
{
    return spy.isEmpty() ? QJsonObject{} : QJsonDocument::fromJson(spy.back().at(0).toString().toUtf8()).object();
}

} // namespace

using Preview = SessionTest;

TEST_F(Preview, ARecordingPlaysToTheDefaultOutputAndTheSessionBesideItRunsAsItDid)
{
    CaptureNodeOptions deviceOptions = captureOptions("rbtest.preview.session", 1000.0);
    deviceOptions.mediaClass = "Audio/Sink";
    CaptureNode device(deviceOptions);
    ASSERT_TRUE(device.waitUntilReady().has_value());

    QTemporaryDir sessions;
    QTemporaryDir files;
    ASSERT_TRUE(sessions.isValid() && files.isValid());
    const QString recording = files.filePath(u"Take 2026-09-27.wav"_s);
    ASSERT_TRUE(writeTone(recording, 4.0));

    dsp::BlockRegistry registry;
    ASSERT_TRUE(blocks::registerBuiltinBlocks(registry));
    Engine engine(registry, SessionStore{sessions.path()});
    QSignalSpy opened(&engine, &EngineFacade::sessionOpened);
    QSignalSpy previews(&engine, &EngineFacade::previewChanged);
    QSignalSpy errors(&engine, &EngineFacade::error);
    const auto said = [&errors] {
        std::string text;
        for (const QList<QVariant> &error : errors) {
            text += error.at(0).toString().toStdString() + " " + error.at(1).toString().toStdString() + "; ";
        }
        return text;
    };

    engine.connectToPipeWire();
    Waiter pump;
    ASSERT_TRUE(pump.pumpUntil([&] { return engine.isConnected(); }));
    engine.createSession(u"Beside"_s);
    ASSERT_EQ(opened.count(), 1);
    const QString id = opened.back().at(0).toString();

    // A session playing a 1 kHz tone to a device of the test's own, which
    // is not the default output the preview goes to.
    engine.applyEdit(id,
                     addBlock(u"wave"_s,
                              blocks::Wave::kTypeId,
                              {{"waveform"_L1, u"sine"_s}, {"frequency"_L1, 1000.0}, {"level"_L1, -12.0}}));
    engine.applyEdit(id,
                     addBlock(u"out"_s, blocks::OutputDevice::kTypeId, {{"device"_L1, u"rbtest.preview.session"_s}}));
    for (std::uint32_t channel = 0; channel < 2; ++channel) {
        engine.applyEdit(id, connect(u"wave"_s, u"out"_s, channel));
    }
    engine.start(id);
    ASSERT_TRUE(errors.isEmpty()) << said();
    ASSERT_TRUE(pump.pumpUntil(
        [&] {
            const std::map<std::string, double> levels = device.measure(200ms);
            const auto left = levels.find("FL");
            return left != levels.end() && left->second > -20.0;
        },
        10s))
        << "the session's tone never reached its device";
    pump.pump(500ms); // past the Wave's own fade-in

    const auto telemetry = [&engine, &id] {
        const TelemetrySlot *slot = engine.telemetryOf(id);
        return slot == nullptr ? TelemetrySnapshot{} : slot->load();
    };

    // --- the session alone ---
    const TelemetrySnapshot alone = telemetry();
    const std::map<std::string, SineCheck> before = device.measureSine(1500ms);
    // What 440 Hz reads on the device with nothing at 440 Hz playing: the
    // 1 kHz tone's own spread over a short window, −75 to −90 dB depending
    // on where the window falls. The preview's tone, were it there, would
    // read −6.
    const std::map<std::string, double> no440 = device.measureTone(440.0, 400ms);
    const TelemetrySnapshot aloneAfter = telemetry();
    ASSERT_GT(alone.sampleRate, 0U);

    // --- and with a recording being listened to beside it ---
    engine.previewRecording(recording);
    ASSERT_TRUE(errors.isEmpty()) << said();
    ASSERT_TRUE(pump.pumpUntil([&] { return lastPreview(previews).value("state"_L1).toString() == "playing"_L1; }));
    std::uint32_t previewNode = 0;
    ASSERT_TRUE(pump.pumpUntil([&] { return (previewNode = engine.previewNodeId()) != 0; }))
        << "the preview never had a node";

    // **A player's stream, linked by the session manager to the default
    // output, and to nothing of the session's.**
    std::uint32_t sinkNode = 0;
    std::uint32_t sessionNode = 0;
    ASSERT_TRUE(pump.awaitGraph([&](const Graph &graph) {
        const Graph::Node *sink = graph.findNode(nodeNamed(kStereoSink));
        const Graph::Node *node = graph.findNode([&](const Graph::Node &candidate) {
            return candidate.prop("node.name").starts_with("reverblabr." + id.toStdString());
        });
        sinkNode = sink != nullptr ? sink->id : 0;
        sessionNode = node != nullptr ? node->id : 0;
        return sinkNode != 0 && sessionNode != 0 && graph.linked(previewNode, sinkNode);
    })) << "the preview was not linked to the default output";
    {
        const Graph graph = Graph::capture();
        const Graph::Node *node = graph.node(previewNode);
        ASSERT_NE(node, nullptr);
        EXPECT_EQ(node->prop("node.name"), "reverblabr.preview");
        EXPECT_EQ(node->prop("media.role"), "Music");
        EXPECT_EQ(node->prop("media.class"), "Stream/Output/Audio");
        EXPECT_FALSE(graph.linked(previewNode, sessionNode)) << "the preview went into the session";
        EXPECT_FALSE(graph.linked(sessionNode, previewNode));
    }

    const TelemetrySnapshot during = telemetry();
    const std::map<std::string, SineCheck> beside = device.measureSine(1500ms);
    const std::map<std::string, double> previewInSession = device.measureTone(440.0, 400ms);
    const TelemetrySnapshot duringAfter = telemetry();
    // The measurements above run the capture's own loop, not this one, so
    // the preview's ticks are delivered now.
    pump.pump(250ms);
    const double played = lastPreview(previews).value("seconds"_L1).toDouble();

    // The graph the session runs in has not moved: the preview asked for
    // nothing, and a file at 44.1 kHz did not pull it over. (Its own device
    // may be driven apart from the default output here; the test after
    // this one shares one driver, and is the one a preview that forced a
    // quantum of 256 failed: 256 against 1024.)
    EXPECT_EQ(during.sampleRate, alone.sampleRate);
    EXPECT_EQ(duringAfter.sampleRate, alone.sampleRate);
    EXPECT_EQ(during.frames, alone.frames) << "the quantum changed under the session";
    EXPECT_EQ(duringAfter.xruns - during.xruns, aloneAfter.xruns - alone.xruns)
        << "the session lost cycles while the preview played";
    for (const char *channel : {"FL", "FR"}) {
        SCOPED_TRACE(channel);
        const SineCheck &quiet = before.at(channel);
        const SineCheck &busy = beside.at(channel);
        EXPECT_EQ(busy.lostCycles, 0U);
        EXPECT_LE(busy.clickedCycles, quiet.clickedCycles) << "the session's tone clicked while the preview played";
        EXPECT_NEAR(busy.peakDb, quiet.peakDb, 0.05) << "the session's level moved";
        EXPECT_LT(previewInSession.at(channel), -50.0) << "the preview's tone reached the session's device";
        std::cout << channel << ": alone " << quiet.peakDb << " dB, " << quiet.clickedCycles << "/" << quiet.cycles
                  << " clicked; beside the preview " << busy.peakDb << " dB, " << busy.clickedCycles << "/"
                  << busy.cycles << " clicked; 440 Hz in the session " << no440.at(channel) << " dB alone, "
                  << previewInSession.at(channel) << " dB beside the preview\n";
    }
    std::cout << "graph " << alone.sampleRate << " Hz, quantum " << alone.frames << " both times; xruns "
              << (aloneAfter.xruns - alone.xruns) << " alone and " << (duringAfter.xruns - during.xruns)
              << " beside; the preview had played " << played << " s\n";
    EXPECT_GT(played, 1.5) << "the preview did not move";

    // --- pause, seek and the end of the file ---
    engine.pausePreview();
    ASSERT_EQ(lastPreview(previews).value("state"_L1).toString(), u"paused"_s);
    const double pausedAt = lastPreview(previews).value("seconds"_L1).toDouble();
    pump.pump(400ms);
    EXPECT_EQ(lastPreview(previews).value("state"_L1).toString(), u"paused"_s);
    engine.seekPreview(3.5);
    EXPECT_EQ(lastPreview(previews).value("state"_L1).toString(), u"paused"_s) << "a seek started a paused preview";
    EXPECT_NEAR(lastPreview(previews).value("seconds"_L1).toDouble(), 3.5, 0.01);
    EXPECT_LT(pausedAt, 3.5);
    engine.previewRecording(recording);
    ASSERT_TRUE(pump.pumpUntil([&] { return lastPreview(previews).value("state"_L1).toString() == "stopped"_L1; }, 5s))
        << "the preview did not stop at the end of its file";
    EXPECT_DOUBLE_EQ(lastPreview(previews).value("seconds"_L1).toDouble(), 4.0);
    EXPECT_EQ(engine.previewNodeId(), 0U) << "the stream was kept after the end";
    EXPECT_TRUE(errors.isEmpty()) << said();

    engine.stop(id);
}

// **The same output, the same driver.** Above, the session plays to a device
// of the test's own, which a different driver may run; here it plays to the
// default output, where the preview goes too, so both are driven by one
// clock in one cycle — the case where a preview could cost the session
// anything. What reaches that output is read off its monitor.
TEST_F(Preview, ASessionAndAPreviewOnOneOutputEachKeepTheirOwnSound)
{
    QTemporaryDir sessions;
    QTemporaryDir files;
    ASSERT_TRUE(sessions.isValid() && files.isValid());
    const QString recording = files.filePath(u"Take.wav"_s);
    ASSERT_TRUE(writeTone(recording, 4.0));

    dsp::BlockRegistry registry;
    ASSERT_TRUE(blocks::registerBuiltinBlocks(registry));
    Engine engine(registry, SessionStore{sessions.path()});
    QSignalSpy opened(&engine, &EngineFacade::sessionOpened);
    QSignalSpy previews(&engine, &EngineFacade::previewChanged);
    QSignalSpy errors(&engine, &EngineFacade::error);
    engine.connectToPipeWire();
    Waiter pump;
    ASSERT_TRUE(pump.pumpUntil([&] { return engine.isConnected(); }));
    engine.createSession(u"Shared"_s);
    ASSERT_EQ(opened.count(), 1);
    const QString id = opened.back().at(0).toString();
    engine.applyEdit(id,
                     addBlock(u"wave"_s,
                              blocks::Wave::kTypeId,
                              {{"waveform"_L1, u"sine"_s}, {"frequency"_L1, 1000.0}, {"level"_L1, -12.0}}));
    engine.applyEdit(
        id, addBlock(u"out"_s, blocks::OutputDevice::kTypeId, {{"device"_L1, QString::fromUtf8(kStereoSink)}}));
    for (std::uint32_t channel = 0; channel < 2; ++channel) {
        engine.applyEdit(id, connect(u"wave"_s, u"out"_s, channel));
    }

    // Listening at the default output's monitor.
    CaptureNode monitor(captureOptions("rbtest.preview.monitor"));
    const std::optional<Graph::Node> listener = monitor.waitUntilReady();
    ASSERT_TRUE(listener.has_value());
    std::uint32_t sinkNode = 0;
    ASSERT_TRUE(pump.awaitGraph([&](const Graph &graph) {
        const Graph::Node *sink = graph.findNode(nodeNamed(kStereoSink));
        sinkNode = sink != nullptr ? sink->id : 0;
        return sinkNode != 0;
    }));
    ASSERT_EQ(linkByChannel(sinkNode, listener->id), 2U);

    engine.start(id);
    const auto sessionLevel = [&monitor] { return monitor.measureTone(1000.0, 400ms); };
    ASSERT_TRUE(pump.pumpUntil([&] { return sessionLevel().at("FL") > -20.0; }, 10s))
        << "the session's tone never reached the default output";
    pump.pump(500ms);
    const auto telemetry = [&engine, &id] {
        const TelemetrySlot *slot = engine.telemetryOf(id);
        return slot == nullptr ? TelemetrySnapshot{} : slot->load();
    };
    const TelemetrySnapshot alone = telemetry();
    const std::map<std::string, double> sessionAlone = sessionLevel();
    pump.pump(1000ms);
    const TelemetrySnapshot aloneAfter = telemetry();

    engine.previewRecording(recording);
    ASSERT_TRUE(pump.pumpUntil([&] { return engine.previewNodeId() != 0; }));
    ASSERT_TRUE(pump.awaitGraph([&](const Graph &graph) { return graph.linked(engine.previewNodeId(), sinkNode); }));
    pump.pump(300ms);
    const TelemetrySnapshot during = telemetry();
    const std::map<std::string, double> sessionBeside = sessionLevel();
    const std::map<std::string, double> previewHeard = monitor.measureTone(440.0, 400ms);
    pump.pump(600ms);
    const TelemetrySnapshot duringAfter = telemetry();

    EXPECT_EQ(during.sampleRate, alone.sampleRate);
    EXPECT_EQ(during.frames, alone.frames) << "the quantum changed under the session";
    EXPECT_EQ(duringAfter.xruns - during.xruns, aloneAfter.xruns - alone.xruns)
        << "the session lost cycles while the preview played";
    for (const char *channel : {"FL", "FR"}) {
        SCOPED_TRACE(channel);
        // The session's tone is where it was, and the preview's is there
        // beside it, at its own level (a 0.5 sine is −6 dBFS).
        EXPECT_NEAR(sessionBeside.at(channel), sessionAlone.at(channel), 0.1);
        EXPECT_NEAR(previewHeard.at(channel), -6.02, 1.0) << "the preview was not heard on the default output";
        std::cout << channel << ": the session's 1 kHz " << sessionAlone.at(channel) << " dB alone, "
                  << sessionBeside.at(channel) << " dB beside the preview; the preview's 440 Hz "
                  << previewHeard.at(channel) << " dB\n";
    }
    std::cout << "one output: graph " << alone.sampleRate << " Hz, quantum " << alone.frames << "; xruns "
              << (aloneAfter.xruns - alone.xruns) << " alone and " << (duringAfter.xruns - during.xruns) << " beside\n";
    EXPECT_TRUE(errors.isEmpty());
    engine.stopPreview();
    engine.stop(id);
}

TEST_F(Preview, AFileThatCannotBeReadSaysSoAndPlaysNothing)
{
    QTemporaryDir sessions;
    dsp::BlockRegistry registry;
    ASSERT_TRUE(blocks::registerBuiltinBlocks(registry));
    Engine engine(registry, SessionStore{sessions.path()});
    QSignalSpy errors(&engine, &EngineFacade::error);
    engine.connectToPipeWire();
    Waiter pump;
    ASSERT_TRUE(pump.pumpUntil([&] { return engine.isConnected(); }));

    engine.previewRecording(sessions.filePath(u"nothing here.wav"_s));
    ASSERT_EQ(errors.count(), 1);
    EXPECT_EQ(errors.back().at(0).toString(), u"recording.previewFailed"_s);
    EXPECT_TRUE(errors.back().at(1).toString().contains(u"unreadable"_s));
    EXPECT_EQ(engine.previewNodeId(), 0U);
}
