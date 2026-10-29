// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// System Audio against a real PipeWire: everything that is playing,
// and what is left out of it. Each application plays a frequency of its own,
// so that what did and did not arrive can be read off one recording.

#include "blocks/builtinblocks.h"
#include "blocks/outputdevice.h"
#include "blocks/systemaudio.h"
#include "core/sessionplan.h"
#include "core/sessionruntime.h"
#include "dsp/blockregistry.h"
#include "sessionharness.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <gtest/gtest.h>

#include <cstdlib>
#include <iostream>
#include <map>
#include <string>

using namespace rb;
using namespace rb::core;
using namespace rb::pwtest;
using namespace Qt::StringLiterals;

namespace
{

/// The three applications the tests play, each on its own frequency.
constexpr double kFirstHz = 1000.0;
constexpr double kSecondHz = 1500.0;
constexpr double kThirdHz = 2500.0;

[[nodiscard]] dsp::BlockRegistry systemRegistry()
{
    dsp::BlockRegistry registry;
    EXPECT_TRUE(blocks::registerBuiltinBlocks(registry));
    return registry;
}

[[nodiscard]] BlockInstance block(const QString &id, std::string_view type)
{
    BlockInstance made;
    made.id = id;
    made.type = QString::fromUtf8(type.data(), static_cast<qsizetype>(type.size()));
    return made;
}

/// System Audio into an output device, which is what "record everything"
/// looks like as a session.
[[nodiscard]] Session systemSession(const QString &sink, const QStringList &excluded = {}, bool alerts = false)
{
    Session session;
    session.id = QUuid::createUuid();
    session.name = u"System"_s;
    BlockInstance source = block(u"sys"_s, blocks::SystemAudio::kTypeId);
    QJsonObject params{{"includeAlerts"_L1, alerts}};
    if (!excluded.isEmpty()) {
        QJsonArray rules;
        for (const QString &rule : excluded) {
            rules.append(rule);
        }
        params.insert("excluded"_L1, rules);
    }
    source.params = params;
    BlockInstance out = block(u"out"_s, blocks::OutputDevice::kTypeId);
    out.params = QJsonObject{{"device"_L1, sink}};
    session.blocks = {source, out};
    for (std::uint32_t channel = 0; channel < 2; ++channel) {
        Wire wire;
        wire.fromBlock = u"sys"_s;
        wire.fromPort = channel;
        wire.toBlock = u"out"_s;
        wire.toPort = channel;
        session.wires.append(wire);
    }
    return session;
}

[[nodiscard]] CaptureNodeOptions sinkOptions(const std::string &name)
{
    CaptureNodeOptions options;
    options.name = name;
    options.channels = {"FL", "FR"};
    options.mediaClass = "Audio/Sink";
    return options;
}

[[nodiscard]] FakeAppOptions toneApp(const std::string &name, const std::string &binary, double hz)
{
    FakeAppOptions options = appOptions(name, binary);
    options.toneHz = hz;
    return options;
}

/// How long the two-session test listens for a runaway. Ten seconds by
/// default; the minute the acceptance criterion asks for is
/// RB_SOAK_SECONDS=60.
[[nodiscard]] int boundedSeconds()
{
    return soakSeconds();
}

} // namespace

using SystemAudio = SessionTest;

TEST_F(SystemAudio, EverythingThatIsPlayingArrivesAndNothingElseDoes)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilReady());
    const dsp::BlockRegistry registry = systemRegistry();
    SessionRuntime runtime(engine.connection(), engine.mirror(), registry);

    CaptureNode device(sinkOptions("rbtest.system.all"));
    ASSERT_TRUE(device.waitUntilReady().has_value());
    ASSERT_TRUE(runtime.start(systemSession(u"rbtest.system.all"_s)).has_value());
    const std::optional<Graph::Node> ours = engine.awaitNode(runtime.node()->nodeName());
    ASSERT_TRUE(ours.has_value());

    FakeApp first(toneApp("SysOne", "sysone", kFirstHz));
    FakeApp second(toneApp("SysTwo", "systwo", kSecondHz));
    FakeApp third(toneApp("SysThree", "systhree", kThirdHz));
    ASSERT_TRUE(engine.pumpUntil([&] {
        int captured = 0;
        for (const pw::PwNode &node : engine.mirror().nodesOfKind(pw::NodeKind::PlaybackStream)) {
            captured += engine.mirror().isLinked(node.object.id, ours->id) ? 1 : 0;
        }
        return captured == 3;
    })) << "the three applications were not all captured";
    engine.pump(std::chrono::milliseconds{700});

    // Each one at the level it was played at: PipeWire sums them at our
    // ports and the block takes nothing off.
    for (const double hz : {kFirstHz, kSecondHz, kThirdHz}) {
        const std::map<std::string, double> heard = device.measureTone(hz, 400ms);
        ASSERT_TRUE(heard.contains("FL")) << hz;
        EXPECT_NEAR(heard.at("FL"), kTonePeakDb, 0.1) << "the tone at " << hz << " Hz did not arrive whole";
    }
    // And nothing was invented at a frequency nobody played.
    const std::map<std::string, double> silence = device.measureTone(3500.0, 400ms);
    EXPECT_LT(silence.at("FL"), kTonePeakDb - 40.0);

    runtime.stop();
}

TEST_F(SystemAudio, AnAlertIsLeftOutUnlessItIsAskedFor)
{
    // **The alert here is a fake application carrying media.role =
    // Notification**, not libcanberra. What a real KDE notification looks
    // like in the graph is spike S1's evidence and is checked in
    // pw.capture-semantics; libcanberra plays a sample and stops, which is
    // not something a level can be measured against.
    Pump engine;
    ASSERT_TRUE(engine.waitUntilReady());
    const dsp::BlockRegistry registry = systemRegistry();
    SessionRuntime runtime(engine.connection(), engine.mirror(), registry);

    CaptureNode device(sinkOptions("rbtest.system.alerts"));
    ASSERT_TRUE(device.waitUntilReady().has_value());
    ASSERT_TRUE(runtime.start(systemSession(u"rbtest.system.alerts"_s)).has_value());
    const std::optional<Graph::Node> ours = engine.awaitNode(runtime.node()->nodeName());
    ASSERT_TRUE(ours.has_value());

    FakeApp music(toneApp("SysMusic", "sysmusic", kFirstHz));
    ASSERT_TRUE(engine.pumpUntil([&] {
        const Graph graph = Graph::capture();
        const Graph::Node *stream = graph.findNode(playbackStreamOf("SysMusic"));
        return stream && engine.mirror().isLinked(stream->id, ours->id);
    }));

    FakeAppOptions alertOptions = toneApp("SysAlert", "sysalert", kSecondHz);
    alertOptions.mediaRole = "Notification";
    FakeApp alertApp(alertOptions);
    const std::optional<Graph::Node> alert = engine.awaitMatch(playbackStreamOf("SysAlert"));
    ASSERT_TRUE(alert.has_value());
    engine.pump(std::chrono::milliseconds{700});

    EXPECT_FALSE(engine.mirror().isLinked(alert->id, ours->id)) << "the alert was captured without being asked for";
    EXPECT_LT(device.measureTone(kSecondHz, 400ms).at("FL"), kTonePeakDb - 30.0)
        << "the alert was heard in the recording";
    EXPECT_NEAR(device.measureTone(kFirstHz, 400ms).at("FL"), kTonePeakDb, 0.2) << "the music was lost";

    // Asked for, it arrives like anything else — and the music is still
    // there beside it, at its own level.
    Session withAlerts = systemSession(u"rbtest.system.alerts"_s, {}, true);
    withAlerts.id = QUuid::createUuid();
    ASSERT_TRUE(runtime.update(withAlerts).has_value());
    ASSERT_TRUE(engine.pumpUntil([&] { return engine.mirror().isLinked(alert->id, ours->id); }))
        << "the alert was asked for and did not arrive";
    engine.pump(std::chrono::milliseconds{700});
    EXPECT_NEAR(device.measureTone(kSecondHz, 400ms).at("FL"), kTonePeakDb, 0.2);
    EXPECT_NEAR(device.measureTone(kFirstHz, 400ms).at("FL"), kTonePeakDb, 0.2);

    runtime.stop();
}

TEST_F(SystemAudio, AnExclusionTakesEffectAndComesBack)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilReady());
    const dsp::BlockRegistry registry = systemRegistry();
    SessionRuntime runtime(engine.connection(), engine.mirror(), registry);

    CaptureNode device(sinkOptions("rbtest.system.exclude"));
    ASSERT_TRUE(device.waitUntilReady().has_value());
    ASSERT_TRUE(runtime.start(systemSession(u"rbtest.system.exclude"_s)).has_value());
    const std::optional<Graph::Node> ours = engine.awaitNode(runtime.node()->nodeName());
    ASSERT_TRUE(ours.has_value());

    FakeApp kept(toneApp("SysKept", "syskept", kFirstHz));
    FakeApp dropped(toneApp("SysDropped", "sysdropped", kSecondHz));
    const std::optional<Graph::Node> droppedStream = engine.awaitMatch(playbackStreamOf("SysDropped"));
    ASSERT_TRUE(droppedStream.has_value());
    ASSERT_TRUE(engine.pumpUntil([&] { return engine.mirror().isLinked(droppedStream->id, ours->id); }));

    // The user adds it to the list. The engine is told the moment the edit
    // is applied, so the quarter second is the graph's to spend.
    QElapsedTimer sinceEdit;
    sinceEdit.start();
    Session excluded = systemSession(u"rbtest.system.exclude"_s, {u"binary:sysdropped"_s});
    excluded.id = QUuid::createUuid();
    ASSERT_TRUE(runtime.update(excluded).has_value());
    ASSERT_TRUE(engine.pumpUntil([&] { return !engine.mirror().isLinked(droppedStream->id, ours->id); }))
        << "the excluded application went on being captured";
    EXPECT_LE(sinceEdit.elapsed(), 250) << "the exclusion took " << sinceEdit.elapsed() << " ms";

    engine.pump(std::chrono::milliseconds{700});
    EXPECT_NEAR(device.measureTone(kFirstHz, 400ms).at("FL"), kTonePeakDb, 0.2) << "the other application was lost";
    EXPECT_LT(device.measureTone(kSecondHz, 400ms).at("FL"), kTonePeakDb - 40.0)
        << "the excluded application is still in the recording";

    // And taking it off the list brings it back.
    Session restored = systemSession(u"rbtest.system.exclude"_s);
    restored.id = QUuid::createUuid();
    ASSERT_TRUE(runtime.update(restored).has_value());
    ASSERT_TRUE(engine.pumpUntil([&] { return engine.mirror().isLinked(droppedStream->id, ours->id); }))
        << "the exclusion was removed and the application did not come back";

    runtime.stop();
}

TEST_F(SystemAudio, AStreamThatIsAlreadyInTheRecordingIsNotTakenTwice)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilReady());
    if (std::getenv("RB_PW_HARNESS_PULSE") == nullptr || std::string(std::getenv("RB_PW_HARNESS_PULSE")) != "1") {
        GTEST_SKIP() << "needs pipewire-pulse for a combined sink";
    }
    const dsp::BlockRegistry registry = systemRegistry();
    SessionRuntime runtime(engine.connection(), engine.mirror(), registry);

    CaptureNode device(sinkOptions("rbtest.system.replay"));
    ASSERT_TRUE(device.waitUntilReady().has_value());
    ASSERT_TRUE(runtime.start(systemSession(u"rbtest.system.replay"_s)).has_value());
    const std::optional<Graph::Node> ours = engine.awaitNode(runtime.node()->nodeName());
    ASSERT_TRUE(ours.has_value());

    // A combined sink plays whatever reaches it to two others, as streams of
    // its own. Capturing those as well as the application would put the same
    // audio into the recording twice.
    // **atoi, not the string**: pactl prints the index with a newline after
    // it, and unload-module takes that for a module name.
    const int module = std::atoi(runCommand({"pactl",
                                             "load-module",
                                             "module-combine-sink",
                                             "sink_name=rbtest.system.combined",
                                             std::string("slaves=") + kStereoSink + ',' + kMonoSink})
                                     .output.c_str());
    ASSERT_GT(module, 0);
    FakeAppOptions options = toneApp("SysCombined", "syscombined", kFirstHz);
    options.target = "rbtest.system.combined";
    FakeApp app(options);

    ASSERT_TRUE(engine.pumpUntil([&] {
        const Graph graph = Graph::capture();
        const Graph::Node *stream = graph.findNode(playbackStreamOf("SysCombined"));
        return stream && engine.mirror().isLinked(stream->id, ours->id);
    })) << "the application itself was never captured";
    engine.pump(std::chrono::milliseconds{800});

    // Every re-play carries node.link-group, and none of them is ours.
    int replays = 0;
    for (const pw::PwNode &node : engine.mirror().nodesOfKind(pw::NodeKind::PlaybackStream)) {
        if (!node.isReplayed()) {
            continue;
        }
        ++replays;
        EXPECT_FALSE(engine.mirror().isLinked(node.object.id, ours->id))
            << node.name.toStdString() << " is a re-play and was captured as well";
    }
    EXPECT_GT(replays, 0) << "the combined sink made no re-played streams, so nothing was proved";

    // One copy, at the level it was played at — not two, which would be
    // 6 dB more.
    const std::map<std::string, double> heard = device.measureTone(kFirstHz, 400ms);
    ASSERT_TRUE(heard.contains("FL"));
    EXPECT_NEAR(heard.at("FL"), kTonePeakDb, 0.3) << "the audio was counted twice";

    runtime.stop();
    // The module's own re-played streams go with it, and the next test
    // starts with a graph that has nothing of this one left in it.
    (void)runCommand({"pactl", "unload-module", std::to_string(module)});
}

TEST_F(SystemAudio, TwoSessionsCapturingEverythingStayQuietTogether)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilReady());
    const dsp::BlockRegistry registry = systemRegistry();
    SessionRuntime first(engine.connection(), engine.mirror(), registry);
    SessionRuntime second(engine.connection(), engine.mirror(), registry);

    CaptureNode device(sinkOptions("rbtest.system.two"));
    ASSERT_TRUE(device.waitUntilReady().has_value());
    // **Both capture everything, and both play to the same device.** If our
    // own nodes were not left out of every match, each would capture the
    // other and the level would climb until it clipped.
    ASSERT_TRUE(first.start(systemSession(u"rbtest.system.two"_s)).has_value());
    ASSERT_TRUE(second.start(systemSession(u"rbtest.system.two"_s)).has_value());
    const std::optional<Graph::Node> oursFirst = engine.awaitNode(first.node()->nodeName());
    const std::optional<Graph::Node> oursSecond = engine.awaitNode(second.node()->nodeName());
    ASSERT_TRUE(oursFirst.has_value() && oursSecond.has_value());

    FakeApp app(toneApp("SysTwoApp", "systwoapp", kFirstHz));
    ASSERT_TRUE(engine.pumpUntil([&] {
        const Graph graph = Graph::capture();
        const Graph::Node *stream = graph.findNode(playbackStreamOf("SysTwoApp"));
        return stream && engine.mirror().isLinked(stream->id, oursFirst->id)
            && engine.mirror().isLinked(stream->id, oursSecond->id);
    })) << "both sessions should have captured the application";

    EXPECT_FALSE(engine.mirror().isLinked(oursFirst->id, oursSecond->id)) << "one session captured the other";
    EXPECT_FALSE(engine.mirror().isLinked(oursSecond->id, oursFirst->id)) << "one session captured the other";

    // Two sessions playing the same tone to one device: 6 dB above one, and
    // there it stays.
    const int seconds = boundedSeconds();
    double loudest = -200.0;
    QElapsedTimer running;
    running.start();
    while (running.elapsed() < seconds * 1000) {
        engine.pump(std::chrono::milliseconds{200});
        const std::map<std::string, double> heard = device.measure(200ms);
        if (heard.contains("FL")) {
            loudest = std::max(loudest, heard.at("FL"));
        }
    }
    std::cerr << "two sessions capturing everything, " << seconds << " s: loudest " << loudest << " dBFS" << std::endl;
    EXPECT_LT(loudest, 0.0) << "the recording clipped, which is what runaway feedback sounds like";
    EXPECT_LT(loudest, kTonePeakDb + 9.0) << "the level climbed past two copies of one tone";

    first.stop();
    second.stop();
}
