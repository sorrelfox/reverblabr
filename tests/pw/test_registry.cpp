// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The engine's own PipeWire connection and registry mirror, run inside
// the private instance and judged against pw-dump, which watches the same
// graph without going through any of this code.

#include "pw/connection.h"
#include "pw/pwobjects.h"
#include "pw/registrymirror.h"
#include "pwtest.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QString>
#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <vector>

using namespace rb::pw;
using namespace rb::pwtest;
using namespace Qt::StringLiterals;

namespace
{

/// A Qt event loop and an engine connection, for one test.
///
/// The connection runs PipeWire's loop on this thread's Qt event loop, so
/// "letting events arrive" means letting Qt run, which is what pump() does.
class Engine
{
public:
    Engine()
    {
        m_connection.setRetryDelays(std::chrono::milliseconds{50}, std::chrono::milliseconds{200});
        m_mirror.setOwnApplicationName(u"reverblabr-registry-test"_s);
        m_connection.start();
    }

    Connection &connection()
    {
        return m_connection;
    }

    RegistryMirror &mirror()
    {
        return m_mirror;
    }

    /// Lets Qt, and so PipeWire, run for a while.
    void pump(std::chrono::milliseconds duration)
    {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < duration.count()) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        }
    }

    /// Lets Qt run until \a condition holds, or the time runs out. Returns
    /// whether it held.
    [[nodiscard]] bool pumpUntil(const std::function<bool()> &condition,
                                 std::chrono::milliseconds timeout = kDefaultTimeout)
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

    /// Waits until the connection is up and the mirror has a complete first
    /// picture of the graph — every object announced, and every one of them
    /// described. The mirror says when that is, so no test guesses at a time.
    [[nodiscard]] bool waitUntilSettled()
    {
        if (!pumpUntil([this] { return m_connection.isConnected(); })) {
            return false;
        }
        return pumpUntil([this] { return m_mirror.isReady(); });
    }

private:
    Connection m_connection;
    RegistryMirror m_mirror{m_connection};
};

/// The node names pw-dump sees, leaving out anything with no name.
std::set<std::string> dumpedNodeNames()
{
    const Graph graph = Graph::capture();
    std::set<std::string> names;
    for (const Graph::Node &node : graph.nodes) {
        const std::string name = node.prop("node.name");
        if (!name.empty()) {
            names.insert(name);
        }
    }
    return names;
}

/// The same, out of the mirror.
std::set<std::string> mirroredNodeNames(const RegistryMirror &mirror)
{
    std::set<std::string> names;
    for (const PwNode &node : mirror.nodes()) {
        if (!node.name.isEmpty()) {
            names.insert(node.name.toStdString());
        }
    }
    return names;
}

/// Every link pw-dump sees, as the four ids that identify it.
std::set<std::array<std::uint32_t, 4>> dumpedLinks()
{
    const Graph graph = Graph::capture();
    std::set<std::array<std::uint32_t, 4>> result;
    for (const Graph::Link &link : graph.links) {
        result.insert({link.outputNode, link.outputPort, link.inputNode, link.inputPort});
    }
    return result;
}

std::set<std::array<std::uint32_t, 4>> mirroredLinks(const RegistryMirror &mirror)
{
    std::set<std::array<std::uint32_t, 4>> result;
    for (const PwLink &link : mirror.links()) {
        result.insert({link.outputNode, link.outputPort, link.inputNode, link.inputPort});
    }
    return result;
}

/// Says which names are in one set and not the other, for a failure message.
std::string difference(const std::set<std::string> &expected, const std::set<std::string> &actual)
{
    std::string report;
    for (const std::string &name : expected) {
        if (!actual.contains(name)) {
            report += "\n  pw-dump has, the mirror does not: " + name;
        }
    }
    for (const std::string &name : actual) {
        if (!expected.contains(name)) {
            report += "\n  the mirror has, pw-dump does not: " + name;
        }
    }
    return report;
}

int argc = 1;
char programName[] = "reverblabr-registry-test";
char *argv[] = {programName, nullptr};

} // namespace

class Registry : public PipeWireTest
{
protected:
    void SetUp() override
    {
        PipeWireTest::SetUp();
        m_app = std::make_unique<QCoreApplication>(argc, argv);
    }

    void TearDown() override
    {
        m_app.reset();
        PipeWireTest::TearDown();
    }

private:
    std::unique_ptr<QCoreApplication> m_app;
};

TEST_F(Registry, ConnectsAndSaysSo)
{
    Engine engine;
    QSignalSpy ready(&engine.mirror(), &RegistryMirror::ready);
    EXPECT_TRUE(engine.waitUntilSettled());
    EXPECT_TRUE(engine.mirror().isReady());
    EXPECT_EQ(ready.count(), 1) << "the mirror says once when its first picture is complete";
    EXPECT_EQ(engine.connection().state(), ConnectionState::Connected);
    EXPECT_TRUE(engine.connection().isConnected());
    EXPECT_FALSE(engine.connection().serverVersion().isEmpty());
    EXPECT_EQ(engine.connection().reconnectCount(), 0u);
    EXPECT_TRUE(engine.connection().lastError().isEmpty());
}

TEST_F(Registry, StoppingDisconnectsAndEmptiesTheMirror)
{
    Engine engine;
    ASSERT_TRUE(engine.waitUntilSettled());
    ASSERT_GT(engine.mirror().size(), 0u);

    QSignalSpy cleared(&engine.mirror(), &RegistryMirror::cleared);
    engine.connection().stop();
    EXPECT_EQ(engine.connection().state(), ConnectionState::Disconnected);
    EXPECT_EQ(cleared.count(), 1);
    EXPECT_EQ(engine.mirror().size(), 0u);
    EXPECT_TRUE(engine.mirror().nodes().isEmpty());
    EXPECT_FALSE(engine.mirror().isReady());
}

TEST_F(Registry, MirrorsTheHarnessDevices)
{
    Engine engine;
    ASSERT_TRUE(engine.waitUntilSettled());

    for (const char *name : {kStereoSink, kMonoSink, kSurroundSink, kAuxSink}) {
        const std::optional<PwNode> node = engine.mirror().nodeNamed(QString::fromUtf8(name));
        ASSERT_TRUE(node.has_value()) << name;
        EXPECT_EQ(node->kind, NodeKind::Sink) << name;
        EXPECT_GT(node->object.serial, 0u) << name << " needs a serial to be identified over time";
    }

    const std::optional<PwNode> source = engine.mirror().nodeNamed(QString::fromUtf8(kStereoSource));
    ASSERT_TRUE(source.has_value());
    EXPECT_EQ(source->kind, NodeKind::Source);
}

TEST_F(Registry, KnowsAPortsChannelAndDirection)
{
    Engine engine;
    ASSERT_TRUE(engine.waitUntilSettled());
    const std::optional<PwNode> sink = engine.mirror().nodeNamed(QString::fromUtf8(kStereoSink));
    ASSERT_TRUE(sink.has_value());

    ASSERT_TRUE(
        engine.pumpUntil([&] { return engine.mirror().portsOf(sink->object.id, PortDirection::Input).size() == 2; }));
    const QList<PwPort> inputs = engine.mirror().portsOf(sink->object.id, PortDirection::Input);
    EXPECT_EQ(inputs[0].channel, u"FL"_s);
    EXPECT_EQ(inputs[1].channel, u"FR"_s);
    EXPECT_EQ(inputs[0].direction, PortDirection::Input);
    EXPECT_LT(inputs[0].index, inputs[1].index) << "ports come back in the node's own order";
}

TEST_F(Registry, ASurroundSinkHasAllSixChannels)
{
    Engine engine;
    ASSERT_TRUE(engine.waitUntilSettled());
    const std::optional<PwNode> sink = engine.mirror().nodeNamed(QString::fromUtf8(kSurroundSink));
    ASSERT_TRUE(sink.has_value());
    ASSERT_TRUE(
        engine.pumpUntil([&] { return engine.mirror().portsOf(sink->object.id, PortDirection::Input).size() == 6; }));

    QStringList channels;
    for (const PwPort &port : engine.mirror().portsOf(sink->object.id, PortDirection::Input)) {
        channels.append(port.channel);
    }
    EXPECT_EQ(channels, (QStringList{u"FL"_s, u"FR"_s, u"FC"_s, u"LFE"_s, u"RL"_s, u"RR"_s}));
}

TEST_F(Registry, SeesAnApplicationArriveAndLeaveWithinAHundredMilliseconds)
{
    Engine engine;
    ASSERT_TRUE(engine.waitUntilSettled());

    const auto stream = [&] {
        for (const PwNode &node : engine.mirror().nodesOfKind(NodeKind::PlaybackStream)) {
            if (node.applicationName == u"RegistryTestApp"_s) {
                return std::optional{node};
            }
        }
        return std::optional<PwNode>{};
    };

    FakeAppOptions options;
    options.applicationName = "RegistryTestApp";
    options.binary = "registrytestapp";
    auto app = std::make_unique<FakeApp>(options);

    // pw-dump is the referee: once it can see the stream, the mirror has a
    // tenth of a second to have seen it too.
    ASSERT_TRUE(waitForNode(playbackStreamOf("RegistryTestApp")));
    EXPECT_TRUE(engine.pumpUntil([&] { return stream().has_value(); }, std::chrono::milliseconds{100}))
        << "the mirror was still behind pw-dump after 100 ms";

    // What the stream *is* comes from binding it, which is a round trip
    // further on than the announcement the budget above is about.
    ASSERT_TRUE(engine.pumpUntil([&] {
        const std::optional<PwNode> node = stream();
        return node.has_value() && !node->applicationBinary.isEmpty();
    }));
    const PwNode seen = *stream();
    EXPECT_EQ(seen.applicationBinary, u"registrytestapp"_s);
    EXPECT_GT(seen.applicationPid, 0);
    EXPECT_FALSE(seen.isOurs);
    EXPECT_FALSE(seen.isNotification());

    app.reset();
    ASSERT_TRUE(
        waitUntil([](const Graph &graph) { return graph.findNode(playbackStreamOf("RegistryTestApp")) == nullptr; }));
    EXPECT_TRUE(engine.pumpUntil([&] { return !stream().has_value(); }, std::chrono::milliseconds{100}))
        << "the mirror still had the stream 100 ms after it had gone";
}

TEST_F(Registry, SeesANullSinkArriveAndLeaveWithinAHundredMilliseconds)
{
    Engine engine;
    ASSERT_TRUE(engine.waitUntilSettled());
    const QString name = u"rbtest.registry.appearing"_s;

    BackgroundProcess sink({"pw-loopback", "--capture-props", "media.class=Audio/Sink node.name=" + name.toStdString()},
                           "registry-null-sink");
    ASSERT_TRUE(waitForNode(nodeNamed(name.toStdString())));
    EXPECT_TRUE(
        engine.pumpUntil([&] { return engine.mirror().nodeNamed(name).has_value(); }, std::chrono::milliseconds{100}));

    sink.kill();
    ASSERT_TRUE(
        waitUntil([&](const Graph &graph) { return graph.findNode(nodeNamed(name.toStdString())) == nullptr; }));
    EXPECT_TRUE(
        engine.pumpUntil([&] { return !engine.mirror().nodeNamed(name).has_value(); }, std::chrono::milliseconds{100}));
}

TEST_F(Registry, ReadsTheDefaultSinkAndSource)
{
    Engine engine;
    ASSERT_TRUE(engine.waitUntilSettled());
    ASSERT_TRUE(engine.pumpUntil([&] { return !engine.mirror().defaults().audioSink.isEmpty(); }));

    const PwDefaults defaults = engine.mirror().defaults();
    EXPECT_EQ(defaults.audioSink, QString::fromUtf8(kStereoSink));
    EXPECT_EQ(defaults.audioSource, QString::fromUtf8(kStereoSource));

    const std::optional<PwNode> sink = engine.mirror().defaultSink();
    ASSERT_TRUE(sink.has_value());
    EXPECT_EQ(sink->name, QString::fromUtf8(kStereoSink));
    ASSERT_TRUE(engine.mirror().defaultSource().has_value());

    // And what pw-dump reads out of the same metadata agrees.
    const Graph graph = Graph::capture();
    const std::string dumped =
        graph.defaults.count("default.audio.sink") != 0 ? graph.defaults.at("default.audio.sink") : std::string{};
    EXPECT_NE(dumped.find(kStereoSink), std::string::npos) << dumped;
}

TEST_F(Registry, NoticesTheDefaultSinkChanging)
{
    Engine engine;
    ASSERT_TRUE(engine.waitUntilSettled());
    ASSERT_TRUE(engine.pumpUntil([&] { return !engine.mirror().defaults().audioSink.isEmpty(); }));
    QSignalSpy changed(&engine.mirror(), &RegistryMirror::defaultsChanged);

    const CommandResult result = runCommand(
        {"wpctl", "set-default", std::to_string(engine.mirror().nodeNamed(QString::fromUtf8(kMonoSink))->object.id)});
    ASSERT_EQ(result.status, 0) << result.output;

    EXPECT_TRUE(engine.pumpUntil([&] { return engine.mirror().defaults().audioSink == QString::fromUtf8(kMonoSink); }))
        << "the mirror still says " << engine.mirror().defaults().audioSink.toStdString();
    EXPECT_GT(changed.count(), 0);
    ASSERT_TRUE(engine.mirror().defaultSink().has_value());
    EXPECT_EQ(engine.mirror().defaultSink()->name, QString::fromUtf8(kMonoSink));
}

TEST_F(Registry, MarksOurOwnNodesAndOurOwnClient)
{
    Engine engine;
    ASSERT_TRUE(engine.waitUntilSettled());

    // Our own client is the one whose process is this one, which is known
    // once its own description has arrived.
    ASSERT_TRUE(engine.pumpUntil([&] {
        const QList<PwClient> all = engine.mirror().clients();
        return std::ranges::any_of(all, [](const PwClient &client) { return client.isUs; });
    }));
    const QList<PwClient> clients = engine.mirror().clients();
    const auto ours = std::ranges::find_if(clients, [](const PwClient &client) { return client.isUs; });
    ASSERT_NE(ours, clients.cend()) << "the mirror did not recognise its own connection";
    EXPECT_EQ(ours->applicationPid, static_cast<qint64>(::getpid()));

    // Nothing in the harness is ours, so nothing may be marked.
    for (const PwNode &node : engine.mirror().nodes()) {
        EXPECT_FALSE(node.isOurs) << node.name.toStdString() << " was marked as one of ours";
    }
}

TEST_F(Registry, RecognisesAReplayedStream)
{
    // A loopback re-plays what reaches it. Capturing both it and its source
    // would double the sound, so a source block has to be able to tell.
    Engine engine;
    ASSERT_TRUE(engine.waitUntilSettled());
    const QString name = u"rbtest.registry.loopback"_s;

    BackgroundProcess loopback(
        {"pw-loopback", "--capture-props", "media.class=Audio/Sink node.name=" + name.toStdString()},
        "registry-loopback");
    ASSERT_TRUE(waitForNode(nodeNamed(name.toStdString())));
    ASSERT_TRUE(engine.pumpUntil([&] {
        const std::optional<PwNode> node = engine.mirror().nodeNamed(name);
        return node.has_value() && !node->linkGroup.isEmpty();
    }));
    EXPECT_TRUE(engine.mirror().nodeNamed(name)->isReplayed());
}

TEST_F(Registry, MirrorsLinksAsTheyAreMade)
{
    Engine engine;
    ASSERT_TRUE(engine.waitUntilSettled());

    FakeAppOptions options;
    options.applicationName = "LinkTestApp";
    options.target = kStereoSink;
    const FakeApp app(options);

    const auto stream = waitForNode(playbackStreamOf("LinkTestApp"));
    ASSERT_TRUE(stream.has_value());
    const std::optional<PwNode> sink = engine.mirror().nodeNamed(QString::fromUtf8(kStereoSink));
    ASSERT_TRUE(sink.has_value());
    ASSERT_TRUE(waitForLink(stream->id, sink->object.id));

    EXPECT_TRUE(engine.pumpUntil([&] { return engine.mirror().isLinked(stream->id, sink->object.id); }))
        << "the mirror did not see the policy's links";
    EXPECT_FALSE(engine.mirror().linksOf(stream->id).isEmpty());
    EXPECT_EQ(mirroredLinks(engine.mirror()), dumpedLinks()) << "the mirror's links differ from pw-dump's";
}

TEST_F(Registry, ComesBackAfterTheConnectionIsLost)
{
    // stop() and start() stand in for the daemon going away: everything the
    // mirror held is thrown away and built again from nothing, which is what
    // a restart does.
    Engine engine;
    ASSERT_TRUE(engine.waitUntilSettled());
    const std::size_t before = engine.mirror().size();
    ASSERT_GT(before, 0u);

    QSignalSpy cleared(&engine.mirror(), &RegistryMirror::cleared);
    engine.connection().stop();
    ASSERT_EQ(engine.mirror().size(), 0u);
    EXPECT_EQ(cleared.count(), 1);

    engine.connection().start();
    ASSERT_TRUE(engine.waitUntilSettled());
    EXPECT_TRUE(engine.pumpUntil([&] { return engine.mirror().size() >= before; }))
        << "the mirror had " << engine.mirror().size() << " objects, not " << before;
    EXPECT_TRUE(engine.mirror().nodeNamed(QString::fromUtf8(kStereoSink)).has_value());
    EXPECT_EQ(mirroredNodeNames(engine.mirror()), dumpedNodeNames());
}

TEST_F(Registry, WaitsAndRetriesWhenTheServerIsNotThere)
{
    // With no server to reach, the connection must keep trying rather than
    // give up or spin, and must say which state it is in.
    const QByteArray realRuntime = qgetenv("PIPEWIRE_RUNTIME_DIR");
    qputenv("PIPEWIRE_RUNTIME_DIR", "/nonexistent/reverblabr-no-server");
    const QByteArray realRemote = qgetenv("PIPEWIRE_REMOTE");
    qputenv("PIPEWIRE_REMOTE", "reverblabr-no-such-socket");

    {
        Connection connection;
        connection.setRetryDelays(std::chrono::milliseconds{20}, std::chrono::milliseconds{40});
        connection.start();

        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 1000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        }
        EXPECT_NE(connection.state(), ConnectionState::Connected);
        EXPECT_GT(connection.reconnectCount(), 0u) << "it stopped trying";
        EXPECT_FALSE(connection.lastError().isEmpty());
        connection.stop();
        EXPECT_EQ(connection.state(), ConnectionState::Disconnected);
    }

    qputenv("PIPEWIRE_RUNTIME_DIR", realRuntime);
    if (realRemote.isEmpty()) {
        qunsetenv("PIPEWIRE_REMOTE");
    } else {
        qputenv("PIPEWIRE_REMOTE", realRemote);
    }
}

TEST_F(Registry, MatchesPwDumpAfterAThousandCreationsAndRemovals)
{
    // The mirror is built from events; pw-dump asks the server outright. A
    // thousand objects coming and going at random is where a missed or
    // mishandled event would show, and the two have to agree at the end.
    Engine engine;
    ASSERT_TRUE(engine.waitUntilSettled());

    QSignalSpy added(&engine.mirror(), &RegistryMirror::nodeAdded);
    QSignalSpy removed(&engine.mirror(), &RegistryMirror::nodeRemoved);

    constexpr int kEvents = 1000;
    constexpr int kLiveAtOnce = 12;
    std::mt19937 random(20260920); // fixed, so a failure can be reproduced

    std::vector<std::unique_ptr<BackgroundProcess>> live;
    int made = 0;
    int killed = 0;
    for (int event = 0; event < kEvents; ++event) {
        const bool create =
            live.size() < 2 || (live.size() < kLiveAtOnce && std::uniform_int_distribution<int>(0, 1)(random) == 1);
        if (create) {
            const std::string name = "rbtest.churn." + std::to_string(made++);
            live.push_back(std::make_unique<BackgroundProcess>(
                std::vector<std::string>{"pw-loopback", "--capture-props", "media.class=Audio/Sink node.name=" + name},
                "registry-churn"));
        } else {
            const auto victim = std::uniform_int_distribution<std::size_t>(0, live.size() - 1)(random);
            live[victim]->kill();
            live.erase(live.begin() + static_cast<std::ptrdiff_t>(victim));
            ++killed;
        }
        // Let the events through as they happen, rather than in one flood at
        // the end: that is what a running session looks like.
        engine.pump(std::chrono::milliseconds{5});
    }
    live.clear();
    EXPECT_EQ(made + killed, kEvents);

    // Everything has to settle before the two can be compared at all.
    ASSERT_TRUE(waitUntil(
        [](const Graph &graph) {
            return std::ranges::none_of(graph.nodes, [](const Graph::Node &node) {
                return node.prop("node.name").starts_with("rbtest.churn.");
            });
        },
        std::chrono::seconds{60}));

    const bool agreed = engine.pumpUntil([&] { return mirroredNodeNames(engine.mirror()) == dumpedNodeNames(); },
                                         std::chrono::seconds{20});
    EXPECT_TRUE(agreed) << "after " << kEvents << " events the mirror and pw-dump disagree:"
                        << difference(dumpedNodeNames(), mirroredNodeNames(engine.mirror()));
    EXPECT_EQ(mirroredLinks(engine.mirror()), dumpedLinks());

    // And the churn has to have been real: a test that quietly created
    // nothing would agree with pw-dump about nothing.
    EXPECT_GE(added.count(), made) << "the mirror saw fewer nodes appear than the test made";
    EXPECT_GE(removed.count(), made) << "the mirror saw fewer nodes go than the test made";
}

TEST_F(Registry, AClientThatLooksAndLeavesDoesNotCostUsTheConnection)
{
    // A global can go away between the mirror asking to bind it and the
    // server reading the request. A client that connects, dumps the graph
    // and leaves does exactly that, many times a second. The bind then never
    // happens, and destroying the proxy for it anyway is a protocol error
    // the server closes the connection over — which would take every running
    // session with it.
    Engine engine;
    ASSERT_TRUE(engine.waitUntilSettled());

    const int clientsBefore = engine.mirror().clients().size();
    for (int round = 0; round < 40; ++round) {
        // pw-dump is the shortest-lived client there is: it connects, asks
        // once and goes.
        (void)Graph::capture();
        engine.pump(std::chrono::milliseconds{10});
    }

    EXPECT_EQ(engine.connection().reconnectCount(), 0u)
        << "the connection was lost and remade while clients came and went";
    EXPECT_TRUE(engine.connection().isConnected());
    EXPECT_TRUE(engine.mirror().isReady()) << "the mirror was emptied and refilled, which only a reconnect does";
    // And it is still a mirror of the graph, not a pile of stale clients.
    EXPECT_LE(engine.mirror().clients().size(), clientsBefore + 2);
}

TEST_F(Registry, ReadsTheGraphsClockAndFollowsAForcedQuantum)
{
    // The graph's own rate and quantum, from PipeWire's settings
    // metadata, whether anything of ours is running or not — and a quantum
    // forced from outside, as `pw-metadata -n settings 0
    // clock.force-quantum 256` forces it, reflected within a second.
    Engine engine;
    ASSERT_TRUE(engine.waitUntilSettled());
    ASSERT_TRUE(engine.pumpUntil([&] { return engine.mirror().clockSettings().rate != 0; }));
    const PwClockSettings clock = engine.mirror().clockSettings();
    EXPECT_EQ(clock.rate, 48000U);
    EXPECT_EQ(clock.quantum, 1024U);
    EXPECT_EQ(clock.forceQuantum, 0U);
    EXPECT_EQ(clock.effectiveQuantum(), 1024U);

    QSignalSpy changed(&engine.mirror(), &RegistryMirror::clockSettingsChanged);
    CommandResult forced = runCommand({"pw-metadata", "-n", "settings", "0", "clock.force-quantum", "256"});
    ASSERT_EQ(forced.status, 0) << forced.output;
    EXPECT_TRUE(engine.pumpUntil([&] { return engine.mirror().clockSettings().effectiveQuantum() == 256; },
                                 std::chrono::milliseconds{1000}))
        << "the forced quantum was not seen within a second";
    EXPECT_GT(changed.count(), 0);

    // And let go of again: the graph's own quantum is back.
    forced = runCommand({"pw-metadata", "-n", "settings", "0", "clock.force-quantum", "0"});
    ASSERT_EQ(forced.status, 0) << forced.output;
    EXPECT_TRUE(engine.pumpUntil([&] { return engine.mirror().clockSettings().effectiveQuantum() == 1024; },
                                 std::chrono::milliseconds{1000}));
}

TEST_F(Registry, NamesTheDriverAndFollowsItWhenItGoesAndComesBack)
{
    // As near as a test gets to unplugging the device that drives
    // the graph: a sink that can drive is made the default, taken away, and
    // made again under the same name. The driver named follows each step —
    // the new sink, whatever WirePlumber falls back to, the sink again.
    Engine engine;
    ASSERT_TRUE(engine.waitUntilSettled());
    ASSERT_TRUE(engine.pumpUntil([&] { return engine.mirror().graphDriver().has_value(); }));
    const QString before = engine.mirror().graphDriver()->description;
    EXPECT_FALSE(before.isEmpty());

    const std::string name = "rbtest.clock.driver";
    const auto plugIn = [&] {
        const CommandResult made =
            runCommand({"pw-cli",
                        "create-node",
                        "adapter",
                        "{ factory.name=support.null-audio-sink node.name=" + name
                            + " node.description=\"Clock driver\" media.class=Audio/Sink audio.position=[ FL FR ]"
                              " priority.session=3000 object.linger=true }"});
        EXPECT_EQ(made.status, 0) << made.output;
        EXPECT_TRUE(
            engine.pumpUntil([&] { return engine.mirror().nodeNamed(QString::fromStdString(name)).has_value(); }));
    };
    const auto driverIs = [&](const QString &description) {
        return engine.pumpUntil(
            [&] {
                const std::optional<PwNode> driver = engine.mirror().graphDriver();
                return driver.has_value() && driver->description == description;
            },
            std::chrono::milliseconds{3000});
    };

    plugIn();
    const std::optional<PwNode> added = engine.mirror().nodeNamed(QString::fromStdString(name));
    ASSERT_TRUE(added.has_value());
    const CommandResult chosen = runCommand({"wpctl", "set-default", std::to_string(added->object.id)});
    ASSERT_EQ(chosen.status, 0) << chosen.output;
    EXPECT_TRUE(driverIs(u"Clock driver"_s)) << "the new default sink was not named as the driver";

    // Unplugged: WirePlumber falls back to another sink, and so does the
    // driver named.
    const CommandResult gone = runCommand({"pw-cli", "destroy", std::to_string(added->object.id)});
    ASSERT_EQ(gone.status, 0) << gone.output;
    EXPECT_TRUE(engine.pumpUntil([&] {
        const std::optional<PwNode> driver = engine.mirror().graphDriver();
        return driver.has_value() && driver->description != u"Clock driver"_s;
    })) << "the driver named was the one that had gone";

    // Plugged back in: WirePlumber remembers it as the chosen default, and
    // the driver named is it again.
    plugIn();
    EXPECT_TRUE(driverIs(u"Clock driver"_s)) << "the device back again was not named as the driver";

    const std::optional<PwNode> again = engine.mirror().nodeNamed(QString::fromStdString(name));
    ASSERT_TRUE(again.has_value());
    (void)runCommand({"pw-cli", "destroy", std::to_string(again->object.id)});
    (void)runCommand(
        {"wpctl", "set-default", std::to_string(engine.mirror().nodeNamed(QString::fromUtf8(kStereoSink))->object.id)});
}

TEST_F(Registry, TheDriverFollowedIsTheOneTheGivenNodeIsIn)
{
    // While something plays, the node it plays through names its
    // driver, and asking after that node gives that driver — the mono sink
    // here, which is not the default — rather than the default sink's.
    Engine engine;
    ASSERT_TRUE(engine.waitUntilSettled());
    FakeAppOptions options;
    options.applicationName = "ClockApp";
    options.binary = "clockapp";
    options.target = kMonoSink;
    const FakeApp app(options);
    ASSERT_TRUE(engine.pumpUntil([&] {
        for (const PwNode &node : engine.mirror().nodes()) {
            if (node.applicationName == u"ClockApp"_s && node.driverId != 0) {
                return true;
            }
        }
        return false;
    })) << "the application's stream never named a driver";

    std::uint32_t stream = 0;
    for (const PwNode &node : engine.mirror().nodes()) {
        if (node.applicationName == u"ClockApp"_s) {
            stream = node.object.id;
        }
    }
    const std::optional<PwNode> driver = engine.mirror().graphDriver(stream);
    ASSERT_TRUE(driver.has_value());
    EXPECT_EQ(driver->name, QString::fromUtf8(kMonoSink));
    EXPECT_EQ(driver->description, u"Test sink (mono)"_s);
    // Asked after nothing in particular, it is the default sink's graph.
    EXPECT_EQ(engine.mirror().graphDriver()->name, QString::fromUtf8(kStereoSink));
}
