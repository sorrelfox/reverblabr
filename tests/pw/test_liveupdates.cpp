// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Editing a session while it runs: the graph is recompiled and handed
// over, ports come and go around it, and none of it is audible as anything
// but the fade it was given. What comes out is judged by capture filters
// that share no code with the engine, and the graph by pw-dump.

#include "sessionharness.h"

#include <QSignalSpy>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <map>
#include <random>
#include <string>

using namespace rb;
using namespace rb::pw;
using namespace rb::pwtest;
using namespace Qt::StringLiterals;

namespace
{

/// A residual this far down means the sine came through whole: no click, no
/// gap, no repeated block. A fade of a few milliseconds is a gentle enough
/// slope that it does not raise it either, which is the whole point of
/// fading rather than swapping.
constexpr double kCleanResidualDb = -55.0;

/// Clicks a busy machine may add to a window of sound on its own: a cycle
/// delivered late leaves a hole, which clicks going in and coming out, and
/// two such hiccups. A session that is broken clicks in a steady share of
/// every cycle, many times this.
constexpr std::uint64_t kClicksABusyMachineCosts = 4;

/// The window with the fewest clicked cycles of those measured.
struct QuietestWindow {
    std::uint64_t clickedCycles = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t cycles = 0;
    double residualDb = 0.0; ///< its loudest click, for a failure message
    double peakDb = -200.0;
};

/// Measures up to \a windows windows of 400 ms on the capture's FL port and
/// keeps the quietest, stopping early at one that clicks no more than
/// \a enough times.
[[nodiscard]] QuietestWindow quietestWindow(CaptureNode &capture, std::uint64_t enough, int windows)
{
    QuietestWindow quietest;
    for (int attempt = 0; attempt < windows && quietest.clickedCycles > enough; ++attempt) {
        const std::map<std::string, SineCheck> sound = capture.measureSine(400ms);
        if (!sound.contains("FL")) {
            ADD_FAILURE() << "nothing arrived on FL";
            return quietest;
        }
        const SineCheck &fl = sound.at("FL");
        if (fl.clickedCycles < quietest.clickedCycles) {
            quietest = {fl.clickedCycles, fl.cycles, fl.residualDb, fl.peakDb};
        }
    }
    return quietest;
}

/// **The baseline has to be clean too.** A verdict relative to the same
/// sound before any change would pass a session that clicked from the
/// start, because it would click as much afterwards as before. So the
/// quietest window before any change is held to the machine's hiccups
/// alone: under a parallel build and under 32 spinning cores it clicked
/// in none of its cycles in each of 44 readings.
void expectCleanBeforeAnyChange(const QuietestWindow &calm)
{
    ASSERT_LE(calm.clickedCycles, kClicksABusyMachineCosts)
        << "the sine was not clean even before anything changed: " << calm.clickedCycles << " of " << calm.cycles
        << " cycles clicked in the quietest of three windows (loudest " << calm.residualDb << " dB)";
}

/// A session running under a tone, with each lane listened to separately.
///
/// The tone goes into both lanes and each lane's output has a capture filter
/// of its own, so an edit to one lane can be heard against the other.
class Running
{
public:
    EditableSession session;
    std::unique_ptr<SessionNode> node;
    std::unique_ptr<FakeApp> app;
    std::array<std::unique_ptr<CaptureNode>, EditableSession::kLanes> capture;
    std::optional<Graph::Node> ours;
    std::optional<Graph::Node> stream;
    std::array<std::optional<Graph::Node>, EditableSession::kLanes> captureNode;

    /// Everything up to sound arriving at both capture filters. Uses
    /// ASSERT-style failures, so call it with ASSERT_NO_FATAL_FAILURE.
    void start(Pump &engine, const QString &sessionId, const QString &name)
    {
        node = std::make_unique<SessionNode>(engine.connection());
        SessionNodeSpec spec;
        spec.sessionId = sessionId;
        spec.sessionName = name;
        spec.sampleRate = kRate;
        spec.inputs = session.inputLanes();
        spec.outputs = session.outputLanes();
        ASSERT_TRUE(node->start(spec).has_value());
        ASSERT_TRUE(node->setGraph(session.compile()).has_value());

        ours = engine.awaitNode(node->nodeName());
        ASSERT_TRUE(ours.has_value());
        ASSERT_TRUE(engine.awaitPorts(ours->id, "input", 2 * EditableSession::kLanes));
        ASSERT_TRUE(engine.awaitPorts(ours->id, "output", 2 * EditableSession::kLanes));

        app = std::make_unique<FakeApp>(appOptions(name.toStdString(), name.toLower().toStdString()));
        stream = engine.awaitMatch(playbackStreamOf(name.toStdString()));
        ASSERT_TRUE(stream.has_value());
        ASSERT_TRUE(engine.awaitPorts(stream->id, "output", 2));

        // The tone into every lane's source, by name: the channel alone
        // does not say which lane a port belongs to.
        const Graph graph = Graph::capture();
        for (const Graph::Port &from : graph.portsOf(stream->id, "output")) {
            for (const Graph::Port &to : graph.portsOf(ours->id, "input")) {
                if (to.channel == from.channel) {
                    EXPECT_TRUE(linkPorts(from.id, to.id));
                }
            }
        }

        for (int lane = 0; lane < EditableSession::kLanes; ++lane) {
            capture[lane] = std::make_unique<CaptureNode>(
                captureOptions("rbtest.capture." + name.toLower().toStdString() + std::to_string(lane), 1000.0));
            captureNode[lane] = capture[lane]->waitUntilReady();
            ASSERT_TRUE(captureNode[lane].has_value());
        }
        ASSERT_NO_FATAL_FAILURE(linkOutputs(engine));
        engine.pump(std::chrono::milliseconds{500});
    }

    /// Links every lane's output to its own capture filter, by port name.
    void linkOutputs(Pump &engine)
    {
        const Graph graph = Graph::capture();
        for (int lane = 0; lane < EditableSession::kLanes; ++lane) {
            if (!captureNode[lane]) {
                continue;
            }
            const std::string prefix = EditableSession::outputId(lane).toStdString() + ":";
            for (const Graph::Port &from : graph.portsOf(ours->id, "output")) {
                if (!from.prop("port.name").starts_with(prefix)) {
                    continue;
                }
                for (const Graph::Port &to : graph.portsOf(captureNode[lane]->id, "input")) {
                    if (to.channel == from.channel && !graph.linked(ours->id, captureNode[lane]->id)) {
                        EXPECT_TRUE(linkPorts(from.id, to.id));
                    }
                }
            }
        }
        engine.pump(std::chrono::milliseconds{50});
    }

    /// Hands the session as it now stands to the node, fading the lanes the
    /// change can be heard through.
    [[nodiscard]] std::expected<void, SessionNodeError> publish(const graph::GraphSpec &before)
    {
        SessionUpdate update;
        update.inputs = session.inputLanes();
        update.outputs = session.outputLanes();
        update.graph = session.compile();
        for (const std::string &id : graph::changedBlocks(before, session.spec())) {
            update.changedBlocks.append(QString::fromStdString(id));
        }
        return node->update(std::move(update));
    }
};

} // namespace

using LiveUpdates = SessionTest;

TEST_F(LiveUpdates, AnEditIsHeardOnItsOwnLaneAndNowhereElse)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilConnected());

    Running running;
    ASSERT_NO_FATAL_FAILURE(running.start(engine, u"aaaa-0001"_s, u"EditApp"_s));

    const std::map<std::string, SineCheck> lane0Before = running.capture[0]->measureSine(300ms);
    const std::map<std::string, SineCheck> lane1Before = running.capture[1]->measureSine(300ms);
    ASSERT_TRUE(lane0Before.contains("FL"));
    ASSERT_TRUE(lane1Before.contains("FL"));
    ASSERT_LT(lane0Before.at("FL").residualDb, kCleanResidualDb) << "the tone was not clean before the edit";
    ASSERT_NEAR(lane0Before.at("FL").peakDb, kTonePeakDb, 0.2);
    ASSERT_NEAR(lane1Before.at("FL").peakDb, kTonePeakDb, 0.2);

    // Put a block that halves the signal into the middle of the first lane.
    const graph::GraphSpec was = running.session.spec();
    ASSERT_TRUE(running.session.addEffect(0));
    ASSERT_TRUE(running.publish(was).has_value());
    engine.pump(std::chrono::milliseconds{300});

    const std::map<std::string, SineCheck> lane0After = running.capture[0]->measureSine(300ms);
    const std::map<std::string, SineCheck> lane1After = running.capture[1]->measureSine(300ms);
    ASSERT_TRUE(lane0After.contains("FL"));
    ASSERT_TRUE(lane1After.contains("FL"));

    EXPECT_NEAR(lane0After.at("FL").peakDb, lane0Before.at("FL").peakDb - 6.02, 0.3)
        << "the block that was added is not being heard";
    EXPECT_LT(lane0After.at("FL").residualDb, kCleanResidualDb) << "the edit left a click on its own lane";
    EXPECT_NEAR(lane1After.at("FL").peakDb, lane1Before.at("FL").peakDb, 0.05)
        << "the other lane changed level, and nothing was done to it";
    EXPECT_LT(lane1After.at("FL").residualDb, kCleanResidualDb) << "the edit was heard on a lane it never touched";
    EXPECT_EQ(lane1After.at("FL").nullBuffers, 0U);
}

TEST_F(LiveUpdates, AThousandEditsWhileASinePlaysLeaveItClean)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilConnected());

    // **A short cycle, so a thousand edits take seconds rather than most of
    // a minute**. An edit waits for the audio thread to hand the graph
    // over, which costs one cycle each; with nothing asking for a small one
    // the private server runs about 2048 frames a cycle, 43 ms, and the
    // edits took 40 s. The same edits and the same checks at 128 frames take
    // four. 64 was tried and is too tight a deadline for the unoptimised
    // build, which lost two or three cycles in the edits two runs in three.
    ForcedClock clock;
    clock.setQuantum(128);

    Running running;
    // Short fades, or a thousand of them would take a quarter of an hour.
    ASSERT_NO_FATAL_FAILURE(running.start(engine, u"aaaa-0002"_s, u"ChurnApp"_s));
    running.node->setFadeDuration(std::chrono::milliseconds{2});

    // What the machine costs this session with nothing being edited, so
    // that the edits are judged against the load rather than against zero.
    const MachineCost machine(engine, *running.node, std::chrono::milliseconds{500});
    // And what it costs the sound on the lane that is never edited, the same
    // way AChangeOfQuantumNeedsNoRecompile reads it.
    const QuietestWindow calm = quietestWindow(*running.capture[1], 0, 3);
    ASSERT_NO_FATAL_FAILURE(expectCleanBeforeAnyChange(calm));
    const std::uint64_t cyclesBefore = running.node->telemetry().cycles;
    const std::uint64_t xrunsBefore = running.node->telemetry().xruns;
    // Start the residual afresh, so it counts only what the edits did.
    (void)running.capture[0]->measureSine(1ms);
    (void)running.capture[1]->measureSine(1ms);

    QElapsedTimer editing;
    editing.start();

    std::mt19937 random(20260920); // fixed, so a failure can be reproduced
    int added = 0;
    int removed = 0;
    for (int edit = 0; edit < 1000; ++edit) {
        // Only the first lane is edited. The second is what says that an
        // edit is not heard where it was not made.
        const graph::GraphSpec was = running.session.spec();
        const bool grow = std::uniform_int_distribution<int>(0, 1)(random) == 1;
        if (!(grow ? running.session.addEffect(0) : running.session.removeEffect(0))) {
            continue; // the lane is as long, or as short, as it goes
        }
        grow ? ++added : ++removed;
        ASSERT_TRUE(running.publish(was).has_value()) << "edit " << edit;
        engine.pump(std::chrono::milliseconds{1});
    }
    EXPECT_GT(added, 100);
    EXPECT_GT(removed, 100);

    const qint64 took = editing.elapsed();

    const SessionTelemetry telemetry = running.node->telemetry();
    EXPECT_GT(telemetry.cycles, cyclesBefore) << "the node stopped running somewhere in the edits";
    // An edit that costs a cycle costs one every time, which over a thousand
    // of them is a thousand; what the allowance lets through is the machine
    // being busy elsewhere for a moment — in bursts, which the half-second
    // reading rarely catches, so judged as a share of the cycles run.
    const std::uint64_t lost = telemetry.xruns - xrunsBefore;
    const std::uint64_t allowed =
        machine.allowanceFor(std::chrono::milliseconds{took}, telemetry.cycles - cyclesBefore);
    EXPECT_LE(lost, allowed) << "a thousand edits cost the node " << lost << " cycles, where " << allowed
                             << " were allowed (the machine lost " << machine.lost() << " while nothing was edited)";
    EXPECT_EQ(engine.connection().reconnectCount(), 0U);

    // What the two lanes heard through all of it.
    const std::map<std::string, SineCheck> edited = running.capture[0]->measureSine(200ms);
    const std::map<std::string, SineCheck> untouched = running.capture[1]->measureSine(200ms);
    ASSERT_TRUE(untouched.contains("FL"));
    EXPECT_EQ(untouched.at("FL").nullBuffers, 0U);
    // **In clicks, against the same lane before the edits**: one
    // window's loudest click is the machine as often as it is the session.
    // A lane the edits had damaged clicks in a steady share of every cycle.
    const std::uint64_t allowedClicks = calm.clickedCycles + kClicksABusyMachineCosts;
    const QuietestWindow after = quietestWindow(*running.capture[1], allowedClicks, 5);
    EXPECT_LE(after.clickedCycles, allowedClicks)
        << "a thousand edits to one lane were heard on the other: " << after.clickedCycles << " of " << after.cycles
        << " cycles clicked in its quietest of five windows (loudest " << after.residualDb << " dB), where "
        << allowedClicks << " were allowed (it clicked in " << calm.clickedCycles << " of " << calm.cycles
        << " before the edits)";
    std::cerr << "a thousand edits: " << added << " added, " << removed << " removed, "
              << telemetry.cycles - cyclesBefore << " cycles, " << lost << " xruns; the edited lane's"
              << " residual was " << edited.at("FL").residualDb << " dB and the untouched lane's "
              << untouched.at("FL").residualDb << " dB\n";

    const std::vector<Graph::Port> outputs = Graph::capture().portsOf(running.ours->id, "output");
    EXPECT_EQ(outputs.size(), 2U * EditableSession::kLanes) << "a port was left behind, or one is missing";
}

TEST_F(LiveUpdates, ALaneCanBeTakenOutAndPutBackWhileTheOtherPlaysOn)
{
    // Ports are added before the graph that uses them and removed after the
    // graph that used them has gone ([ADR-0008]). This is that order, seen
    // from outside.
    Pump engine;
    ASSERT_TRUE(engine.waitUntilConnected());

    Running running;
    ASSERT_NO_FATAL_FAILURE(running.start(engine, u"aaaa-0003"_s, u"LaneApp"_s));
    const std::map<std::string, SineCheck> before = running.capture[1]->measureSine(200ms);
    ASSERT_TRUE(before.contains("FL"));
    ASSERT_LT(before.at("FL").residualDb, kCleanResidualDb);

    const graph::GraphSpec was = running.session.spec();
    running.session.setLaneActive(0, false);
    ASSERT_TRUE(running.publish(was).has_value());

    ASSERT_TRUE(engine.pumpUntil([&] {
        const Graph graph = Graph::capture();
        return graph.portsOf(running.ours->id, "input").size() == 2
            && graph.portsOf(running.ours->id, "output").size() == 2;
    })) << "the ports of the lane that was switched off are still there";

    const Graph without = Graph::capture();
    EXPECT_TRUE(std::ranges::none_of(without.portsOf(running.ours->id, "input"), [](const Graph::Port &port) {
        return port.prop("port.name").starts_with("src0:");
    }));

    // And back again.
    const graph::GraphSpec without0 = running.session.spec();
    running.session.setLaneActive(0, true);
    ASSERT_TRUE(running.publish(without0).has_value());

    ASSERT_TRUE(engine.pumpUntil([&] {
        return Graph::capture().portsOf(running.ours->id, "input").size() == 2 * EditableSession::kLanes;
    })) << "the ports of the lane that came back never appeared";

    const std::map<std::string, SineCheck> after = running.capture[1]->measureSine(200ms);
    ASSERT_TRUE(after.contains("FL"));
    EXPECT_NEAR(after.at("FL").peakDb, before.at("FL").peakDb, 0.05) << "the lane that was left alone changed level";
    EXPECT_LT(after.at("FL").residualDb, kCleanResidualDb)
        << "taking a lane out and putting it back was heard on the other lane";
    EXPECT_EQ(engine.connection().reconnectCount(), 0U);
}

TEST_F(LiveUpdates, AChangeOfQuantumNeedsNoRecompile)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilConnected());

    ForcedClock clock;
    clock.setQuantum(64);

    Running running;
    ASSERT_NO_FATAL_FAILURE(running.start(engine, u"aaaa-0004"_s, u"QuantumApp"_s));
    ASSERT_TRUE(engine.pumpUntil([&] { return running.node->telemetry().frames == 64; }))
        << "the quantum never settled at 64 frames";

    // What this machine costs the session before a quantum is changed at
    // all, read at 64 frames because that is the quantum the changes end on
    // as well.
    const MachineCost machine(engine, *running.node);
    // And what it costs the sound: the clicks of the quietest of three
    // windows, at the same quantum, before anything is changed.
    const QuietestWindow calm = quietestWindow(*running.capture[0], 0, 3);
    ASSERT_NO_FATAL_FAILURE(expectCleanBeforeAnyChange(calm));
    const std::uint64_t xrunsBefore = running.node->telemetry().xruns;
    const std::uint64_t cyclesBefore = running.node->telemetry().cycles;
    QElapsedTimer sinceBefore;
    sinceBefore.start();

    constexpr std::uint64_t kChanges = 4;
    for (const int quantum : {2048, 64, 2048, 64}) {
        clock.setQuantum(quantum);
        ASSERT_TRUE(engine.pumpUntil([&] {
            return running.node->telemetry().frames == static_cast<std::uint32_t>(quantum);
        })) << "the quantum never reached "
            << quantum;
        engine.pump(std::chrono::milliseconds{300});
        // Buffers are made for the largest cycle the engine allows, so a
        // change of quantum needs no new graph at all ([ADR-0008]).
        EXPECT_TRUE(running.node->hasGraph());
    }

    // **Settled is a thing to wait for, not a length of time.** Spike S2
    // found that PipeWire itself can click while it reconfigures the graph
    // around a new quantum — by as much as -8 dBFS, whatever the node does
    // ([ADR-0008]) — and on a slow machine it can still be at it half a
    // second later. What has to be true is that nothing lasts: the node
    // stops losing cycles, and then the sine is whole again.
    std::uint64_t seen = running.node->telemetry().xruns;
    ASSERT_TRUE(engine.pumpUntil([&] {
        const std::uint64_t now = running.node->telemetry().xruns;
        const bool quiet = now == seen;
        seen = now;
        return quiet;
    })) << "the node never stopped losing cycles after the changes";
    engine.pump(std::chrono::milliseconds{400});

    // **Judged in clicks, against the same sound before anything changed.**
    // One window's loudest click is a measurement of the machine: a cycle
    // delivered late leaves a hole as loud as the tone itself, and under a
    // parallel build that happened in all three windows this test used to
    // offer. No lost cycle was counted anywhere when it did, and the
    // same sound clicked just as often before any quantum was changed at
    // all. What a broken session does instead is click in a steady share of
    // every cycle: skipping one cycle in eight after the change clicks in 50
    // of every 300, in every window. So the quietest of five windows may
    // click as often as the same sound did before any change, and a couple
    // of hiccups more.
    const std::uint64_t allowedClicks = calm.clickedCycles + kClicksABusyMachineCosts;
    const QuietestWindow after = quietestWindow(*running.capture[0], allowedClicks, 5);
    EXPECT_LE(after.clickedCycles, allowedClicks)
        << "the sine went on clicking after the last change: " << after.clickedCycles << " of " << after.cycles
        << " cycles clicked in the quietest of five windows (loudest " << after.residualDb << " dB), where "
        << allowedClicks << " were allowed (the same sound clicked in " << calm.clickedCycles << " of " << calm.cycles
        << " before any change)";
    EXPECT_NEAR(after.peakDb, kTonePeakDb, 0.3);

    // **A switch may cost a cycle; a session may not.** Reconfiguring the
    // graph around a new quantum is the server's doing, and on a busy
    // machine the node can be late for one cycle of it — this test failed
    // that way about one full run in ten. What would be ours to answer for
    // is a switch that cost many cycles, or a session that went on losing
    // them afterwards, so those are what is measured: a cycle a change, on
    // top of whatever the machine was costing anyway over the stretch the
    // changes took — its bursts included, as a share of the cycles run.
    const SessionTelemetry afterTheChanges = running.node->telemetry();
    const std::uint64_t acrossTheChanges = afterTheChanges.xruns - xrunsBefore;
    const std::uint64_t allowedAcross = kChanges
        + machine.allowanceFor(std::chrono::milliseconds{sinceBefore.elapsed()}, afterTheChanges.cycles - cyclesBefore);
    EXPECT_LE(acrossTheChanges, allowedAcross)
        << "four changes of quantum cost " << acrossTheChanges << " cycles, where " << allowedAcross
        << " were allowed (the machine lost " << machine.lost() << " with nothing being asked of it)";

    // **And afterwards, the quietest second of three.** A session that is
    // still losing cycles loses one in every cycle — hundreds a second at
    // 64 frames — and cannot produce a quiet second however many it is
    // offered, while a machine that is busy elsewhere loses one now and
    // then and produces one easily. What "quiet" means is what the reading
    // above found, not zero: an absolute zero here is a measurement of the
    // machine, and it failed on a runner whose cores were shared with a
    // second pipeline and under `ctest -j8` at home — and a burst the
    // reading happened to miss is judged as a share of the second's cycles.
    std::uint64_t quietest = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t allowedInIt = 0;
    for (int attempt = 0; attempt < 3 && quietest > allowedInIt; ++attempt) {
        const SessionTelemetry settled = running.node->telemetry();
        engine.pump(std::chrono::milliseconds{1000});
        const SessionTelemetry second = running.node->telemetry();
        const std::uint64_t lost = second.xruns - settled.xruns;
        if (lost < quietest) {
            quietest = lost;
            allowedInIt = machine.allowanceFor(std::chrono::milliseconds{1000}, second.cycles - settled.cycles);
        }
    }
    EXPECT_LE(quietest, allowedInIt) << "the session went on losing cycles after the last change: " << quietest
                                     << " in the quietest second of three, where " << allowedInIt << " were allowed";
}

TEST_F(LiveUpdates, AChangeOfRateIsPreparedForOffTheAudioThreadAndTheSoundComesBack)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilConnected());

    ForcedClock clock;
    clock.setRate(48000);

    Running running;
    ASSERT_NO_FATAL_FAILURE(running.start(engine, u"aaaa-0005"_s, u"RateApp"_s));
    running.node->setRateCheckInterval(std::chrono::milliseconds{5});

    // Whenever PipeWire changes the rate under it, the node says so and the
    // engine prepares a graph for the new rate. Nothing is prepared on the
    // audio thread ([ADR-0008]).
    QSignalSpy rates(running.node.get(), &SessionNode::rateChangeNeeded);
    QObject::connect(running.node.get(), &SessionNode::rateChangeNeeded, running.node.get(), [&](quint32 rate) {
        running.session.forgetPreparation();
        SessionUpdate update;
        update.inputs = running.session.inputLanes();
        update.outputs = running.session.outputLanes();
        update.graph = running.session.compile(rate);
        // Every block is prepared again, so every lane is faded.
        for (const SessionLane &lane : update.outputs) {
            update.changedBlocks.append(lane.blockId);
        }
        EXPECT_TRUE(running.node->update(std::move(update)).has_value());
    });

    ASSERT_TRUE(engine.pumpUntil([&] { return running.node->telemetry().sampleRate == 48000; }));
    ASSERT_GT(running.capture[0]->measureSettled().at("FL"), -40.0) << "nothing arrived before the rate changed";

    for (const int rate : {44100, 48000}) {
        clock.setRate(rate);
        ASSERT_TRUE(engine.pumpUntil([&] {
            return running.node->telemetry().sampleRate == static_cast<std::uint32_t>(rate);
        })) << "the node never saw "
            << rate << " Hz";
        engine.pump(std::chrono::milliseconds{600});

        const std::map<std::string, double> levels = running.capture[0]->measureSettled();
        ASSERT_TRUE(levels.contains("FL"));
        EXPECT_GT(levels.at("FL"), -40.0) << "the sound did not come back at " << rate << " Hz";
        EXPECT_NEAR(levels.at("FL"), kTonePeakDb, 0.5) << "and it came back at the wrong level at " << rate << " Hz";
    }

    EXPECT_GT(rates.count(), 0) << "the node never asked for a graph at the new rate";
    EXPECT_EQ(engine.connection().reconnectCount(), 0U);
}
