// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// A session, as the user saved it, turned into sound. The blocks are
// made from the registry, the graph is compiled around them, PipeWire gets a
// node with a port for every source and every output, and an edit changes
// all of that while it runs.

#include "core/sessionruntime.h"
#include "core/telemetry.h"
#include "sessionharness.h"

#include <QJsonObject>
#include <QUuid>
#include <gtest/gtest.h>

#include <algorithm>
#include <iostream>
#include <map>
#include <memory>
#include <string>

using namespace rb;
using namespace rb::core;
using namespace rb::pwtest;
using namespace Qt::StringLiterals;

namespace
{

/// A registry with the blocks a test session is made of. They stand in for
/// the Input Device, Volume and Output Device blocks, which come later
/// and need the facade this is part of.
[[nodiscard]] dsp::BlockRegistry testRegistry()
{
    dsp::BlockRegistry registry;
    EXPECT_TRUE(registry.add(std::string{test::PassThroughSource::kTypeId}, &test::PassThroughSource::make));
    EXPECT_TRUE(registry.add(std::string{test::FixedGainEffect::kTypeId}, &test::FixedGainEffect::make));
    EXPECT_TRUE(registry.add(std::string{test::PassThroughOutput::kTypeId}, &test::PassThroughOutput::make));
    EXPECT_TRUE(registry.add(std::string{test::LifetimeWatchedEffect::kTypeId}, &test::LifetimeWatchedEffect::make));
    EXPECT_TRUE(registry.add(std::string{test::StallingEffect::kTypeId}, &test::StallingEffect::make));
    return registry;
}

[[nodiscard]] BlockInstance block(const QString &id, std::string_view type)
{
    BlockInstance instance;
    instance.id = id;
    instance.type = QString::fromUtf8(type.data(), static_cast<qsizetype>(type.size()));
    return instance;
}

[[nodiscard]] Wire wire(const QString &from, const QString &to)
{
    Wire result;
    result.fromBlock = from;
    result.toBlock = to;
    result.fromPort = 0;
    result.toPort = 0;
    return result;
}

/// source -> gain -> output, in stereo, which is the shape of the smallest
/// session that does anything.
[[nodiscard]] Session chainSession()
{
    Session session;
    session.id = QUuid::createUuid();
    session.name = u"Runtime"_s;
    session.blocks = {block(u"src"_s, test::PassThroughSource::kTypeId),
                      block(u"fx"_s, test::FixedGainEffect::kTypeId),
                      block(u"out"_s, test::PassThroughOutput::kTypeId)};
    session.wires = {wire(u"src"_s, u"fx"_s), wire(u"fx"_s, u"out"_s)};
    // Both channels, so the stereo tone goes through whole.
    Wire right = wire(u"src"_s, u"fx"_s);
    right.fromPort = 1;
    right.toPort = 1;
    Wire rightOut = wire(u"fx"_s, u"out"_s);
    rightOut.fromPort = 1;
    rightOut.toPort = 1;
    session.wires.append(right);
    session.wires.append(rightOut);
    return session;
}

} // namespace

using Runtime = SessionTest;

TEST_F(Runtime, ASavedSessionBecomesANodeWithAPortForEverySourceAndOutput)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilConnected());

    const dsp::BlockRegistry registry = testRegistry();
    SessionRuntime runtime(engine.connection(), engine.mirror(), registry);
    const Session session = chainSession();
    ASSERT_TRUE(runtime.start(session).has_value());
    ASSERT_TRUE(runtime.isRunning());
    ASSERT_NE(runtime.node(), nullptr);

    const std::optional<Graph::Node> ours = engine.awaitNode(runtime.node()->nodeName());
    ASSERT_TRUE(ours.has_value());
    // The source faces PipeWire inwards and the output outwards; the gain
    // between them stays inside the graph.
    ASSERT_TRUE(engine.awaitPorts(ours->id, "input", 2));
    ASSERT_TRUE(engine.awaitPorts(ours->id, "output", 2));

    const Graph graph = Graph::capture();
    for (const Graph::Port &port : graph.portsOf(ours->id, "input")) {
        EXPECT_TRUE(port.prop("port.name").starts_with("src:")) << port.prop("port.name");
    }
    for (const Graph::Port &port : graph.portsOf(ours->id, "output")) {
        EXPECT_TRUE(port.prop("port.name").starts_with("out:")) << port.prop("port.name");
    }
    EXPECT_EQ(ours->prop("node.description"), "Reverblabr — Runtime");

    runtime.stop();
    EXPECT_FALSE(runtime.isRunning());
    ASSERT_TRUE(engine.pumpUntil([&] {
        const Graph graph = Graph::capture();
        return graph.node(ours->id) == nullptr;
    }));
}

TEST_F(Runtime, ATonePlayedIntoASessionComesOutTheOtherSide)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilConnected());

    const dsp::BlockRegistry registry = testRegistry();
    SessionRuntime runtime(engine.connection(), engine.mirror(), registry);
    ASSERT_TRUE(runtime.start(chainSession()).has_value());

    const std::optional<Graph::Node> ours = engine.awaitNode(runtime.node()->nodeName());
    ASSERT_TRUE(ours.has_value());
    ASSERT_TRUE(engine.awaitPorts(ours->id, "input", 2));
    ASSERT_TRUE(engine.awaitPorts(ours->id, "output", 2));

    FakeApp app(appOptions("RuntimeApp", "runtimeapp"));
    const std::optional<Graph::Node> stream = engine.awaitMatch(playbackStreamOf("RuntimeApp"));
    ASSERT_TRUE(stream.has_value());
    ASSERT_TRUE(engine.awaitPorts(stream->id, "output", 2));
    ASSERT_EQ(linkByChannel(stream->id, ours->id), 2U);

    CaptureNode capture(captureOptions("rbtest.capture.runtime", 1000.0));
    const std::optional<Graph::Node> captureNode = capture.waitUntilReady();
    ASSERT_TRUE(captureNode.has_value());
    ASSERT_EQ(linkByChannel(ours->id, captureNode->id), 2U);
    engine.pump(std::chrono::milliseconds{500});

    const std::map<std::string, SineCheck> heard = capture.measureSine(300ms);
    ASSERT_TRUE(heard.contains("FL"));
    // The session halves the signal on its way through, which is the gain
    // block doing what the session says.
    EXPECT_NEAR(heard.at("FL").peakDb, kTonePeakDb - 6.02, 0.3);
    EXPECT_LT(heard.at("FL").residualDb, -55.0) << "what came out was not the tone that went in";
    EXPECT_EQ(heard.at("FL").nullBuffers, 0U);
}

TEST_F(Runtime, AnEditToARunningSessionIsAppliedWithoutStoppingIt)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilConnected());

    const dsp::BlockRegistry registry = testRegistry();
    SessionRuntime runtime(engine.connection(), engine.mirror(), registry);
    Session session = chainSession();
    ASSERT_TRUE(runtime.start(session).has_value());

    const std::optional<Graph::Node> ours = engine.awaitNode(runtime.node()->nodeName());
    ASSERT_TRUE(ours.has_value());
    ASSERT_TRUE(engine.awaitPorts(ours->id, "output", 2));

    FakeApp app(appOptions("EditedApp", "editedapp"));
    const std::optional<Graph::Node> stream = engine.awaitMatch(playbackStreamOf("EditedApp"));
    ASSERT_TRUE(stream.has_value());
    ASSERT_TRUE(engine.awaitPorts(stream->id, "output", 2));
    ASSERT_EQ(linkByChannel(stream->id, ours->id), 2U);

    CaptureNode capture(captureOptions("rbtest.capture.edited", 1000.0));
    const std::optional<Graph::Node> captureNode = capture.waitUntilReady();
    ASSERT_TRUE(captureNode.has_value());
    ASSERT_EQ(linkByChannel(ours->id, captureNode->id), 2U);
    engine.pump(std::chrono::milliseconds{400});
    const double before = capture.measureSettled().at("FL");
    EXPECT_NEAR(before, kTonePeakDb - 6.02, 0.3);

    // Switch the gain block off. The graph cross-fades between what it makes
    // of the signal and the signal itself, so the level comes up and
    // nothing clicks.
    ASSERT_EQ(session.blocks[1].id, u"fx"_s);
    session.blocks[1].enabled = false;
    ASSERT_TRUE(runtime.update(session).has_value());
    engine.pump(std::chrono::milliseconds{400});

    const std::map<std::string, SineCheck> after = capture.measureSine(300ms);
    ASSERT_TRUE(after.contains("FL"));
    EXPECT_NEAR(after.at("FL").peakDb, kTonePeakDb, 0.3) << "switching the gain off should let the tone through";
    EXPECT_LT(after.at("FL").residualDb, -55.0) << "the edit was heard as a click";
    EXPECT_TRUE(runtime.isRunning());
    EXPECT_EQ(engine.connection().reconnectCount(), 0U);
}

// **Removing a block from a running session used to free it under the audio
// thread**. The compiled graph holds raw pointers to its blocks and
// their meters, and the audio thread goes on running the old graph until
// `SessionNode::update()` has swapped it over — which it does last, after
// fading the lanes down. An edit that destroyed the removed block where it
// first noticed it was gone left a whole fade's worth of cycles calling a
// destroyed object, and the fault was a jump through the dead vtable.
//
// The watched block counts the cycles it runs and latches that count as it
// is destroyed, which is what lets the two orders be told apart: a block
// freed at the top of the edit is freed at the count the test read just
// before asking; one freed after the swap is freed at a higher count,
// because the node cannot finish a fade without the audio thread running at
// least one more cycle through the block.
TEST_F(Runtime, ABlockRemovedFromARunningSessionIsFreedAfterTheGraphIsSwapped)
{
    test::lifetimeWatch().reset();

    Pump engine;
    ASSERT_TRUE(engine.waitUntilConnected());

    const dsp::BlockRegistry registry = testRegistry();
    SessionRuntime runtime(engine.connection(), engine.mirror(), registry);
    Session session = chainSession();
    // The watched block stands where the plain gain block does, so the rest
    // of the session, and what it sounds like, is the same.
    session.blocks[1].type = QString::fromUtf8(test::LifetimeWatchedEffect::kTypeId.data(),
                                               static_cast<qsizetype>(test::LifetimeWatchedEffect::kTypeId.size()));
    ASSERT_TRUE(runtime.start(session).has_value());

    const std::optional<Graph::Node> ours = engine.awaitNode(runtime.node()->nodeName());
    ASSERT_TRUE(ours.has_value());
    ASSERT_TRUE(engine.awaitPorts(ours->id, "output", 2));

    FakeApp app(appOptions("RemovedApp", "removedapp"));
    const std::optional<Graph::Node> stream = engine.awaitMatch(playbackStreamOf("RemovedApp"));
    ASSERT_TRUE(stream.has_value());
    ASSERT_TRUE(engine.awaitPorts(stream->id, "output", 2));
    ASSERT_EQ(linkByChannel(stream->id, ours->id), 2U);

    CaptureNode capture(captureOptions("rbtest.capture.removed", 1000.0));
    const std::optional<Graph::Node> captureNode = capture.waitUntilReady();
    ASSERT_TRUE(captureNode.has_value());
    ASSERT_EQ(linkByChannel(ours->id, captureNode->id), 2U);
    engine.pump(std::chrono::milliseconds{400});

    // It is in the graph and the audio thread is running it, which is what
    // makes taking it out the dangerous moment rather than a bookkeeping one.
    const std::uint64_t ranBefore = test::lifetimeWatch().cycles.load();
    ASSERT_GT(ranBefore, 0U) << "the block under test never ran, so nothing was proved";
    EXPECT_NEAR(capture.measureSettled().at("FL"), kTonePeakDb - 6.02, 0.3);

    // Take it out, and wire what is left straight through, which is what
    // removing a block from the middle of a lane leaves behind.
    session.blocks.removeAt(1);
    session.wires.clear();
    for (std::uint32_t channel = 0; channel < 2; ++channel) {
        Wire straight = wire(u"src"_s, u"out"_s);
        straight.fromPort = channel;
        straight.toPort = channel;
        session.wires.append(straight);
    }
    ASSERT_TRUE(runtime.update(session).has_value());

    EXPECT_EQ(test::lifetimeWatch().destructions.load(), 1) << "the removed block was never freed";
    EXPECT_GT(test::lifetimeWatch().cyclesWhenDestroyed.load(), ranBefore)
        << "the removed block was freed before the audio thread let go of the graph running it";

    engine.pump(std::chrono::milliseconds{400});

    // And the session is still running, and now says what it has to say
    // without the block in the way.
    const std::map<std::string, SineCheck> after = capture.measureSine(300ms);
    ASSERT_TRUE(after.contains("FL"));
    EXPECT_NEAR(after.at("FL").peakDb, kTonePeakDb, 0.3) << "the lane should carry the tone whole now";
    EXPECT_LT(after.at("FL").residualDb, -55.0) << "removing the block was heard as a click";
    EXPECT_TRUE(runtime.isRunning());
    EXPECT_EQ(engine.connection().reconnectCount(), 0U);
}

// **Switching a source off stops the sound, and keeps the capture**.
// A disabled block in the middle of a lane is passed through; one at the end
// of a lane has nothing to pass through, so it goes silent. What it must
// *not* do is let go of the application it is capturing: switching a source
// back on has to be immediate, and dropping the links would make it wait for
// the link manager to make them again.
TEST_F(Runtime, ASourceSwitchedOffGoesSilentButIsStillCapturing)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilConnected());

    const dsp::BlockRegistry registry = testRegistry();
    SessionRuntime runtime(engine.connection(), engine.mirror(), registry);
    Session session = chainSession();
    ASSERT_TRUE(runtime.start(session).has_value());

    const std::optional<Graph::Node> ours = engine.awaitNode(runtime.node()->nodeName());
    ASSERT_TRUE(ours.has_value());
    ASSERT_TRUE(engine.awaitPorts(ours->id, "output", 2));

    FakeApp app(appOptions("SilencedApp", "silencedapp"));
    const std::optional<Graph::Node> stream = engine.awaitMatch(playbackStreamOf("SilencedApp"));
    ASSERT_TRUE(stream.has_value());
    ASSERT_TRUE(engine.awaitPorts(stream->id, "output", 2));
    ASSERT_EQ(linkByChannel(stream->id, ours->id), 2U);

    CaptureNode capture(captureOptions("rbtest.capture.silenced", 1000.0));
    const std::optional<Graph::Node> captureNode = capture.waitUntilReady();
    ASSERT_TRUE(captureNode.has_value());
    ASSERT_EQ(linkByChannel(ours->id, captureNode->id), 2U);
    engine.pump(std::chrono::milliseconds{400});
    EXPECT_NEAR(capture.measureSettled().at("FL"), kTonePeakDb - 6.02, 0.3);

    // Off goes the source.
    ASSERT_EQ(session.blocks[0].id, u"src"_s);
    session.blocks[0].enabled = false;
    ASSERT_TRUE(runtime.update(session).has_value());
    engine.pump(std::chrono::milliseconds{400});

    const std::map<std::string, SineCheck> quiet = capture.measureSine(300ms);
    ASSERT_TRUE(quiet.contains("FL"));
    EXPECT_LT(quiet.at("FL").peakDb, -80.0) << "a source that is off was still heard";

    // **And it is still capturing.** The links from the application to the
    // block's ports are the link manager's, and nothing about switching a
    // block off is a reason to give them up: switching it back on has to be
    // immediate rather than waiting for them to be made again.
    const auto linksFromTheApplication = [&] {
        const Graph graph = Graph::capture();
        return std::ranges::count_if(graph.links, [&](const Graph::Link &link) {
            return link.outputNode == stream->id && link.inputNode == ours->id;
        });
    };
    EXPECT_EQ(linksFromTheApplication(), 2) << "the application stopped being captured";

    // Back on, and the sound comes back.
    session.blocks[0].enabled = true;
    ASSERT_TRUE(runtime.update(session).has_value());
    engine.pump(std::chrono::milliseconds{400});
    const std::map<std::string, SineCheck> again = capture.measureSine(300ms);
    ASSERT_TRUE(again.contains("FL"));
    EXPECT_NEAR(again.at("FL").peakDb, kTonePeakDb - 6.02, 0.3) << "the source did not come back";
    EXPECT_LT(again.at("FL").residualDb, -55.0) << "switching the source back on was heard as a click";
    EXPECT_TRUE(runtime.isRunning());
}

// **An edit leaves alone every block it changed nothing for**.
//
// Preparing a block may allocate and it throws away what the block was in
// the middle of — a gain gliding to a new value is stepped to it, a meter's
// 300 ms window is cleared. It used to happen to every block in the session
// on every edit, and underneath the audio thread that was still running
// them, which is the same class of fault as the crash when a block was
// freed under the audio thread.
TEST_F(Runtime, AnEditDoesNotPrepareTheBlocksItChangedNothingFor)
{
    test::lifetimeWatch().reset();

    Pump engine;
    ASSERT_TRUE(engine.waitUntilConnected());

    const dsp::BlockRegistry registry = testRegistry();
    SessionRuntime runtime(engine.connection(), engine.mirror(), registry);
    Session session = chainSession();
    session.blocks[1].type = QString::fromUtf8(test::LifetimeWatchedEffect::kTypeId.data(),
                                               static_cast<qsizetype>(test::LifetimeWatchedEffect::kTypeId.size()));
    ASSERT_TRUE(runtime.start(session).has_value());

    const std::optional<Graph::Node> ours = engine.awaitNode(runtime.node()->nodeName());
    ASSERT_TRUE(ours.has_value());
    ASSERT_TRUE(engine.awaitPorts(ours->id, "output", 2));
    engine.pump(std::chrono::milliseconds{200});

    // Starting prepares it once, and it has been running since.
    ASSERT_EQ(test::lifetimeWatch().prepares.load(), 1);
    ASSERT_GT(test::lifetimeWatch().cycles.load(), 0U) << "the block under test never ran";

    // Edits that change nothing about this block: a tile moved, a block
    // renamed, another one switched off. Every one of them used to re-prepare
    // every block in the session.
    session.blocks[0].position = QPointF{120.0, 40.0};
    ASSERT_TRUE(runtime.update(session).has_value());
    session.blocks[0].nickname = u"The microphone"_s;
    ASSERT_TRUE(runtime.update(session).has_value());
    session.blocks[2].enabled = false;
    ASSERT_TRUE(runtime.update(session).has_value());
    engine.pump(std::chrono::milliseconds{200});

    EXPECT_EQ(test::lifetimeWatch().prepares.load(), 1) << "an edit prepared a block it changed nothing for";
    EXPECT_FALSE(test::lifetimeWatch().preparedAfterRunning.load())
        << "a block was prepared while the audio thread could have been inside it";
    EXPECT_EQ(test::lifetimeWatch().destructions.load(), 0) << "the block was replaced when it needed no replacing";
    EXPECT_TRUE(runtime.isRunning());
}

// And what it remembers is still there afterwards, which is the reason the
// blocks live outside the compiled graph at all.
TEST_F(Runtime, WhatABlockRemembersSurvivesAnUnrelatedEdit)
{
    test::lifetimeWatch().reset();

    Pump engine;
    ASSERT_TRUE(engine.waitUntilConnected());

    const dsp::BlockRegistry registry = testRegistry();
    SessionRuntime runtime(engine.connection(), engine.mirror(), registry);
    Session session = chainSession();
    session.blocks[1].type = QString::fromUtf8(test::LifetimeWatchedEffect::kTypeId.data(),
                                               static_cast<qsizetype>(test::LifetimeWatchedEffect::kTypeId.size()));
    ASSERT_TRUE(runtime.start(session).has_value());
    const std::optional<Graph::Node> ours = engine.awaitNode(runtime.node()->nodeName());
    ASSERT_TRUE(ours.has_value());
    engine.pump(std::chrono::milliseconds{200});

    const std::uint64_t remembered = test::lifetimeWatch().cycles.load();
    ASSERT_GT(remembered, 0U);

    for (int edit = 0; edit < 8; ++edit) {
        session.blocks[0].position = QPointF{120.0 + edit * 10, 40.0};
        ASSERT_TRUE(runtime.update(session).has_value());
    }
    engine.pump(std::chrono::milliseconds{200});

    // The counter is the stand-in for an envelope or a filter's history:
    // preparing sets it back to nought, so it going on counting is the block
    // having been left as it was.
    EXPECT_GT(test::lifetimeWatch().cycles.load(), remembered);
    EXPECT_EQ(test::lifetimeWatch().prepares.load(), 1) << "eight edits prepared the block again";
    EXPECT_FALSE(test::lifetimeWatch().preparedAfterRunning.load());
}

// A block the session has only just gained has never run, so preparing it
// is safe and necessary — the other half of the rule.
TEST_F(Runtime, ABlockAddedToARunningSessionIsPrepared)
{
    test::lifetimeWatch().reset();

    Pump engine;
    ASSERT_TRUE(engine.waitUntilConnected());

    const dsp::BlockRegistry registry = testRegistry();
    SessionRuntime runtime(engine.connection(), engine.mirror(), registry);
    Session session = chainSession();
    ASSERT_TRUE(runtime.start(session).has_value());
    const std::optional<Graph::Node> ours = engine.awaitNode(runtime.node()->nodeName());
    ASSERT_TRUE(ours.has_value());
    engine.pump(std::chrono::milliseconds{200});
    ASSERT_EQ(test::lifetimeWatch().prepares.load(), 0) << "there is no watched block in the session yet";

    BlockInstance added;
    added.id = u"extra"_s;
    added.type = QString::fromUtf8(test::LifetimeWatchedEffect::kTypeId.data(),
                                   static_cast<qsizetype>(test::LifetimeWatchedEffect::kTypeId.size()));
    session.blocks.append(added);
    ASSERT_TRUE(runtime.update(session).has_value());
    engine.pump(std::chrono::milliseconds{200});

    EXPECT_EQ(test::lifetimeWatch().prepares.load(), 1) << "a block that had never been prepared was not prepared";
    EXPECT_FALSE(test::lifetimeWatch().preparedAfterRunning.load());
    EXPECT_TRUE(runtime.isRunning());
}

TEST_F(Runtime, ASessionNamingABlockThisBuildDoesNotHaveIsRefusedAndSaysSo)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilConnected());

    const dsp::BlockRegistry registry = testRegistry();
    SessionRuntime runtime(engine.connection(), engine.mirror(), registry);
    Session session = chainSession();
    session.blocks.append(block(u"future"_s, "effect.somethingFromNextYear"));

    const std::expected<void, RuntimeError> started = runtime.start(session);
    ASSERT_FALSE(started.has_value());
    EXPECT_EQ(started.error(), RuntimeError::UnknownBlockType);
    EXPECT_FALSE(runtime.isRunning());
    EXPECT_EQ(runtimeErrorName(RuntimeError::UnknownBlockType), u"UnknownBlockType"_s);
}

// **The runtime says when a session has given way**, once, and which
// of its blocks cost the most: the node has stopped the session to keep the
// data loop alive, and this is where the person hears about it.
TEST_F(Runtime, ASessionThatGivesWayToKeepTheLoopAliveSaysWhichBlockCostTheMost)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilConnected());
    ForcedClock clock;
    clock.setQuantum(256);

    const dsp::BlockRegistry registry = testRegistry();
    SessionRuntime runtime(engine.connection(), engine.mirror(), registry);
    Session session = chainSession();
    session.blocks[1].type = QString::fromUtf8(test::StallingEffect::kTypeId.data(),
                                               static_cast<qsizetype>(test::StallingEffect::kTypeId.size()));
    QStringList heaviest;
    int said = 0;
    QObject::connect(&runtime, &SessionRuntime::overloaded, [&](const QStringList &blocks) {
        heaviest = blocks;
        ++said;
    });
    ASSERT_TRUE(runtime.start(session).has_value());
    ASSERT_TRUE(engine.pumpUntil([&] { return runtime.node()->telemetry().frames == 256; }));

    // 6 ms of every 5.3 ms cycle, which never lets the loop sleep.
    struct Hold {
        Hold()
        {
            test::StallingEffect::holdAllEvery(1, 6000);
        }
        ~Hold()
        {
            test::StallingEffect::holdAllEvery(0, 0);
        }
    };
    // **Only a real-time thread gives way**: the node says which its
    // data loop is, and one rtkit refused (its burst limit) or that was
    // never asked for (RB_PW_NO_REALTIME) goes on, and says so instead.
    const auto notRealtime = [&] {
        const pw::SessionTelemetry telemetry = runtime.node()->telemetry();
        return telemetry.realtime == 0 && telemetry.longRunsNotRealtime > 0;
    };
    {
        const Hold hold;
        ASSERT_TRUE(engine.pumpUntil([&] { return said > 0 || notRealtime(); }, std::chrono::milliseconds{3000}))
            << "the session never said it had given way, nor that it was not real-time";
        if (notRealtime()) {
            engine.pump(std::chrono::milliseconds{500});
        }
    }
    if (notRealtime()) {
        std::cerr << "the data loop is not real-time (rtkit refused, or RB_PW_NO_REALTIME): it went on\n";
        EXPECT_EQ(said, 0) << "a session no limit holds said it had given way";
        runtime.stop();
        return;
    }
    std::cerr << "the data loop is real-time: the session gave way\n";
    EXPECT_EQ(heaviest.value(0), u"fx"_s) << heaviest.join(u", "_s).toStdString();
    engine.pump(std::chrono::milliseconds{500});
    EXPECT_EQ(said, 1) << "one occasion, said once";
    runtime.stop();
}

TEST_F(Runtime, TheTelemetrySamplerRemembersWhenTheSessionWasLate)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilConnected());
    ForcedClock clock;
    clock.setQuantum(256);

    const dsp::BlockRegistry registry = testRegistry();
    SessionRuntime runtime(engine.connection(), engine.mirror(), registry);
    Session session = chainSession();
    session.blocks[1].type = QString::fromUtf8(test::StallingEffect::kTypeId.data(),
                                               static_cast<qsizetype>(test::StallingEffect::kTypeId.size()));
    ASSERT_TRUE(runtime.start(session).has_value());
    ASSERT_TRUE(engine.pumpUntil([&] { return runtime.node()->telemetry().frames == 256; }));

    TelemetrySampler sampler;
    sampler.watch(&runtime);
    // 20 ms in every eighth cycle of 5.3 ms: late, then sleeping again, so
    // nothing gives way and the node goes on counting.
    struct Hold {
        Hold()
        {
            test::StallingEffect::holdAllEvery(8, 20000);
        }
        ~Hold()
        {
            test::StallingEffect::holdAllEvery(0, 0);
        }
    };
    {
        const Hold hold;
        // Sampled as the timer would, the whole time.
        (void)engine.pumpUntil(
            [&] {
                sampler.sample();
                return sampler.xrunHistory().size() >= 3;
            },
            std::chrono::milliseconds{3000});
    }
    runtime.stop();

    const std::vector<XrunEvent> history = sampler.xrunHistory();
    ASSERT_GE(history.size(), 3U) << "a session late every eighth cycle for three seconds was late fewer than "
                                     "three times, as the samples saw it";
    std::uint32_t added = 0;
    for (std::size_t index = 0; index < history.size(); ++index) {
        added += history[index].added;
        EXPECT_GT(history[index].added, 0U);
        EXPECT_EQ(history[index].total, added) << "event " << index << ": each total is what came before, and more";
        if (index > 0) {
            EXPECT_GE(history[index].atMs, history[index - 1].atMs);
        }
    }
}
