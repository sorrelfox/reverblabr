// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The engine's link manager, run inside the private instance and
// judged against pw-dump, which watches the same graph without going through
// any of this code.

#include "capturenode.h"
#include "pw/connection.h"
#include "pw/feedbackguard.h"
#include "pw/linkmanager.h"
#include "pw/pwobjects.h"
#include "pw/registrymirror.h"
#include "pwtest.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QString>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <functional>
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

/// A Qt event loop, an engine connection, a mirror and a link manager.
class Engine
{
public:
    Engine()
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

    RegistryMirror &mirror()
    {
        return m_mirror;
    }

    LinkManager &manager()
    {
        return m_manager;
    }

    void pump(std::chrono::milliseconds duration)
    {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < duration.count()) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        }
    }

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

    /// Turns the loop until pw-dump shows what the condition asks for. A
    /// link the manager counts is a request that reaches the server only
    /// when the loop turns again, so a pw-dump read after `pumpUntil()` on
    /// `ownedLinkCount()` must not sit still while it waits.
    [[nodiscard]] bool awaitGraph(const std::function<bool(const Graph &)> &condition,
                                  std::chrono::milliseconds timeout = kDefaultTimeout)
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

    [[nodiscard]] bool waitUntilSettled()
    {
        return pumpUntil([this] { return m_connection.isConnected(); })
            && pumpUntil([this] { return m_mirror.isReady(); });
    }

private:
    Connection m_connection;
    RegistryMirror m_mirror{m_connection};
    LinkManager m_manager{m_connection, m_mirror};
};

/// Links between two nodes as pw-dump sees them, by the ports they join.
std::set<std::pair<std::uint32_t, std::uint32_t>> dumpedLinksBetween(std::uint32_t outputNode, std::uint32_t inputNode)
{
    const Graph graph = Graph::capture();
    std::set<std::pair<std::uint32_t, std::uint32_t>> result;
    for (const Graph::Link &link : graph.links) {
        if (link.outputNode == outputNode && link.inputNode == inputNode) {
            result.insert({link.outputPort, link.inputPort});
        }
    }
    return result;
}

/// How many links pw-dump sees between two nodes, counting duplicates.
int dumpedLinkCountBetween(std::uint32_t outputNode, std::uint32_t inputNode)
{
    const Graph graph = Graph::capture();
    return static_cast<int>(std::ranges::count_if(graph.links, [&](const Graph::Link &link) {
        return link.outputNode == outputNode && link.inputNode == inputNode;
    }));
}

/// A link that wants one channel of a named node joined to the same channel
/// of another.
DesiredLink byName(const QString &from, const QString &to, const QString &channel)
{
    DesiredLink wanted;
    wanted.from.node.nodeName = from;
    wanted.from.node.excludeReplayed = false;
    wanted.from.channel = channel;
    wanted.to.node.nodeName = to;
    wanted.to.node.excludeReplayed = false;
    wanted.to.channel = channel;
    return wanted;
}

int argc = 1;
char programName[] = "reverblabr-linkmanager-test";
char *argv[] = {programName, nullptr};

} // namespace

class Links : public PipeWireTest
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

TEST_F(Links, AnEmptyMatchAsksForNothing)
{
    // A match that says nothing must match nothing, so a mistake in a session
    // is silent about the graph rather than loud in it.
    EndpointMatch empty;
    EXPECT_TRUE(empty.isEmpty());

    PwNode node;
    node.name = u"anything"_s;
    EXPECT_FALSE(empty.matches(node));
}

TEST_F(Links, AMatchLeavesOutOurOwnNodesAlertsAndReplays)
{
    EndpointMatch match;
    match.applicationBinary = u"firefox"_s;

    PwNode node;
    node.applicationBinary = u"firefox"_s;
    EXPECT_TRUE(match.matches(node));

    PwNode ours = node;
    ours.isOurs = true;
    EXPECT_FALSE(match.matches(ours));

    PwNode alert = node;
    alert.mediaRole = u"Notification"_s;
    EXPECT_FALSE(match.matches(alert));

    PwNode replayed = node;
    replayed.linkGroup = u"loopback-1"_s;
    EXPECT_FALSE(match.matches(replayed));

    PwNode wrongKind = node;
    wrongKind.kind = NodeKind::Sink;
    match.kind = NodeKind::PlaybackStream;
    EXPECT_FALSE(match.matches(wrongKind));
}

TEST_F(Links, MakesTheLinksItIsAskedFor)
{
    Engine engine;
    ASSERT_TRUE(engine.waitUntilSettled());

    CaptureNodeOptions options;
    options.name = "rbtest.links.target";
    options.channels = {"FL", "FR"};
    CaptureNode capture(options);
    const std::optional<Graph::Node> target = capture.waitUntilReady();
    ASSERT_TRUE(target.has_value());
    ASSERT_TRUE(engine.pumpUntil([&] { return engine.mirror().nodeNamed(u"rbtest.links.target"_s).has_value(); }));

    const QString source = QString::fromUtf8(kStereoSource);
    engine.manager().setDesiredLinks(
        {byName(source, u"rbtest.links.target"_s, u"FL"_s), byName(source, u"rbtest.links.target"_s, u"FR"_s)});

    ASSERT_TRUE(engine.pumpUntil([&] { return engine.manager().ownedLinkCount() == 2; }));

    const std::optional<PwNode> sourceNode = engine.mirror().nodeNamed(source);
    ASSERT_TRUE(sourceNode.has_value());
    ASSERT_TRUE(engine.awaitGraph([&](const Graph &graph) { return graph.linked(sourceNode->object.id, target->id); }));
    EXPECT_EQ(dumpedLinkCountBetween(sourceNode->object.id, target->id), 2)
        << "pw-dump has to see exactly the two links, and no more";

    // And they join the same channels to each other, not across.
    for (const auto &[outputPort, inputPort] : dumpedLinksBetween(sourceNode->object.id, target->id)) {
        const std::optional<PwPort> from = engine.mirror().port(outputPort);
        const std::optional<PwPort> to = engine.mirror().port(inputPort);
        ASSERT_TRUE(from.has_value());
        ASSERT_TRUE(to.has_value());
        EXPECT_EQ(from->channel, to->channel);
    }
}

TEST_F(Links, LetsGoOfALinkItNoLongerWants)
{
    Engine engine;
    ASSERT_TRUE(engine.waitUntilSettled());

    CaptureNodeOptions options;
    options.name = "rbtest.links.dropped";
    options.channels = {"FL", "FR"};
    CaptureNode capture(options);
    ASSERT_TRUE(capture.waitUntilReady().has_value());
    const QString source = QString::fromUtf8(kStereoSource);
    ASSERT_TRUE(engine.pumpUntil([&] { return engine.mirror().nodeNamed(u"rbtest.links.dropped"_s).has_value(); }));

    engine.manager().setDesiredLinks({byName(source, u"rbtest.links.dropped"_s, u"FL"_s)});
    ASSERT_TRUE(engine.pumpUntil([&] { return engine.manager().ownedLinkCount() == 1; }));

    engine.manager().setDesiredLinks({});
    ASSERT_TRUE(engine.pumpUntil([&] { return engine.manager().ownedLinkCount() == 0; }));

    const std::optional<PwNode> sourceNode = engine.mirror().nodeNamed(source);
    const std::optional<PwNode> targetNode = engine.mirror().nodeNamed(u"rbtest.links.dropped"_s);
    ASSERT_TRUE(sourceNode.has_value());
    ASSERT_TRUE(targetNode.has_value());
    EXPECT_TRUE(engine.awaitGraph(
        [&](const Graph &graph) { return !graph.linked(sourceNode->object.id, targetNode->object.id); }));
}

TEST_F(Links, RelinksAnApplicationEachTimeItRestarts)
{
    // The whole point of reconciling rather than handling events: a restarted
    // application is a new node with new ports, and needs no special case.
    Engine engine;
    ASSERT_TRUE(engine.waitUntilSettled());

    CaptureNodeOptions target;
    target.name = "rbtest.links.restarts";
    target.channels = {"FL", "FR"};
    CaptureNode capture(target);
    ASSERT_TRUE(capture.waitUntilReady().has_value());
    ASSERT_TRUE(engine.pumpUntil([&] { return engine.mirror().nodeNamed(u"rbtest.links.restarts"_s).has_value(); }));

    DesiredLink left;
    left.from.node.applicationBinary = u"restartingapp"_s;
    left.from.node.kind = NodeKind::PlaybackStream;
    left.from.channel = u"FL"_s;
    left.to.node.nodeName = u"rbtest.links.restarts"_s;
    left.to.node.excludeReplayed = false;
    left.to.channel = u"FL"_s;
    DesiredLink right = left;
    right.from.channel = u"FR"_s;
    right.to.channel = u"FR"_s;
    engine.manager().setDesiredLinks({left, right});

    FakeAppOptions options;
    options.applicationName = "RestartingApp";
    options.binary = "restartingapp";
    FakeApp app(options);

    constexpr int kRestarts = 20;
    for (int round = 0; round < kRestarts; ++round) {
        if (round > 0) {
            app.restart();
        }
        // pw-dump is the referee: once it can see the stream's ports, the
        // manager has 200 ms to have linked them.
        const std::optional<Graph::Node> stream = waitForNode(playbackStreamOf("RestartingApp"));
        ASSERT_TRUE(stream.has_value()) << "round " << round;
        ASSERT_TRUE(waitForPorts(stream->id, "output", 2)) << "round " << round;

        // Counting links is not enough: the previous run's could still be
        // there. They have to join *this* stream's ports.
        const auto linkedToThisStream = [&] {
            const std::optional<PwNode> sink = engine.mirror().nodeNamed(u"rbtest.links.restarts"_s);
            const std::optional<PwNode> playing = engine.mirror().node(stream->id);
            if (!sink || !playing) {
                return false;
            }
            const QList<PwPort> outputs = engine.mirror().portsOf(playing->object.id, PortDirection::Output);
            const QList<PwPort> inputs = engine.mirror().portsOf(sink->object.id, PortDirection::Input);
            int joined = 0;
            for (const PwPort &output : outputs) {
                for (const PwPort &input : inputs) {
                    if (output.channel == input.channel && !output.channel.isEmpty()
                        && engine.manager().owns(output.object.id, input.object.id)) {
                        ++joined;
                    }
                }
            }
            return joined == 2;
        };

        EXPECT_TRUE(engine.pumpUntil(linkedToThisStream, std::chrono::milliseconds{200}))
            << "round " << round << ": the manager had " << engine.manager().ownedLinkCount()
            << " links, and they did not all join this run's ports";
    }
}

TEST_F(Links, MakesNoDuplicatesOverAThousandReconciles)
{
    Engine engine;
    ASSERT_TRUE(engine.waitUntilSettled());

    CaptureNodeOptions options;
    options.name = "rbtest.links.duplicates";
    options.channels = {"FL", "FR"};
    CaptureNode capture(options);
    const std::optional<Graph::Node> target = capture.waitUntilReady();
    ASSERT_TRUE(target.has_value());
    ASSERT_TRUE(engine.pumpUntil([&] { return engine.mirror().nodeNamed(u"rbtest.links.duplicates"_s).has_value(); }));

    const QString source = QString::fromUtf8(kStereoSource);
    const QList<DesiredLink> both{byName(source, u"rbtest.links.duplicates"_s, u"FL"_s),
                                  byName(source, u"rbtest.links.duplicates"_s, u"FR"_s)};
    engine.manager().setDesiredLinks(both);
    ASSERT_TRUE(engine.pumpUntil([&] { return engine.manager().ownedLinkCount() == 2; }));

    // A thousand passes over an unchanged graph must add nothing.
    std::mt19937 random(20260920); // fixed, so a failure can be reproduced
    for (int pass = 0; pass < 1000; ++pass) {
        if (std::uniform_int_distribution<int>(0, 9)(random) == 0) {
            // Now and again, ask for the same thing in a different order.
            engine.manager().setDesiredLinks({both[1], both[0]});
        }
        engine.manager().reconcileNow();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 0);
    }
    ASSERT_TRUE(engine.pumpUntil([&] { return engine.manager().ownedLinkCount() == 2; }));

    const std::optional<PwNode> sourceNode = engine.mirror().nodeNamed(source);
    ASSERT_TRUE(sourceNode.has_value());
    EXPECT_EQ(dumpedLinkCountBetween(sourceNode->object.id, target->id), 2)
        << "a thousand passes left more links than were asked for";
}

TEST_F(Links, NeverTouchesALinkItDidNotMake)
{
    // Another client's links are not ours to remove, whatever they join.
    Engine engine;
    ASSERT_TRUE(engine.waitUntilSettled());

    CaptureNodeOptions options;
    options.name = "rbtest.links.foreign";
    options.channels = {"FL", "FR"};
    CaptureNode capture(options);
    const std::optional<Graph::Node> target = capture.waitUntilReady();
    ASSERT_TRUE(target.has_value());
    const std::optional<Graph::Node> source = waitForNode(nodeNamed(kStereoSource));
    ASSERT_TRUE(source.has_value());

    // A link made by somebody else entirely, as pw-link would.
    const std::size_t made = linkByChannel(source->id, target->id);
    ASSERT_EQ(made, 2u);
    ASSERT_TRUE(waitForLink(source->id, target->id));
    const auto foreign = dumpedLinksBetween(source->id, target->id);
    ASSERT_EQ(foreign.size(), 2u);

    // The manager wants nothing at all, and reconciles many times over.
    engine.manager().setDesiredLinks({});
    for (int pass = 0; pass < 50; ++pass) {
        engine.manager().reconcileNow();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 0);
    }
    engine.pump(std::chrono::milliseconds{200});

    EXPECT_EQ(engine.manager().ownedLinkCount(), 0);
    EXPECT_EQ(dumpedLinksBetween(source->id, target->id), foreign) << "a link the manager did not make was changed";

    // And asking for a link that is already there adds no second one.
    engine.manager().setDesiredLinks({byName(QString::fromUtf8(kStereoSource), u"rbtest.links.foreign"_s, u"FL"_s)});
    engine.pump(std::chrono::milliseconds{300});
    EXPECT_EQ(dumpedLinkCountBetween(source->id, target->id), 2)
        << "the manager duplicated a link somebody else had already made";
    EXPECT_EQ(engine.manager().ownedLinkCount(), 0);

    for (const auto &[outputPort, inputPort] : foreign) {
        static_cast<void>(unlinkPorts(outputPort, inputPort));
    }
}

TEST_F(Links, SaysSoWhenThereIsNoPortToLink)
{
    Engine engine;
    ASSERT_TRUE(engine.waitUntilSettled());

    // A node that is not there cannot be linked, and the manager must not
    // spin trying: it simply has nothing to do.
    engine.manager().setDesiredLinks({byName(u"rbtest.links.nothing"_s, u"rbtest.links.nowhere"_s, u"FL"_s)});
    QSignalSpy reconciled(&engine.manager(), &LinkManager::reconciled);
    engine.pump(std::chrono::milliseconds{200});

    EXPECT_EQ(engine.manager().ownedLinkCount(), 0);
    EXPECT_GT(reconciled.count(), 0) << "it still ran a pass";
}

TEST_F(Links, AChannelThatIsNotThereIsNotGuessedAt)
{
    // Putting the signal on a channel the user did not choose is worse than
    // not linking at all.
    Engine engine;
    ASSERT_TRUE(engine.waitUntilSettled());

    CaptureNodeOptions options;
    options.name = "rbtest.links.mono";
    options.channels = {"FL"};
    CaptureNode capture(options);
    ASSERT_TRUE(capture.waitUntilReady().has_value());
    ASSERT_TRUE(engine.pumpUntil([&] { return engine.mirror().nodeNamed(u"rbtest.links.mono"_s).has_value(); }));

    engine.manager().setDesiredLinks({byName(QString::fromUtf8(kStereoSource), u"rbtest.links.mono"_s, u"FR"_s)});
    engine.pump(std::chrono::milliseconds{300});
    EXPECT_EQ(engine.manager().ownedLinkCount(), 0);
}

TEST_F(Links, ItsLinksDieWithItWhenItIsKilled)
{
    // object.linger = false is the promise, and it cannot be checked from
    // inside the process that made the links. A helper makes them and is
    // killed outright.
    Engine engine;
    ASSERT_TRUE(engine.waitUntilSettled());

    CaptureNodeOptions options;
    options.name = "rbtest.links.killed";
    options.channels = {"FL", "FR"};
    CaptureNode capture(options);
    const std::optional<Graph::Node> target = capture.waitUntilReady();
    ASSERT_TRUE(target.has_value());
    const std::optional<Graph::Node> source = waitForNode(nodeNamed(kStereoSource));
    ASSERT_TRUE(source.has_value());

    BackgroundProcess helper({RB_LINK_HELPER, kStereoSource, "rbtest.links.killed"}, "link-helper");
    ASSERT_TRUE(helper.waitForOutput("linked")) << helper.output();
    ASSERT_TRUE(waitForLink(source->id, target->id));
    EXPECT_EQ(dumpedLinkCountBetween(source->id, target->id), 2);

    helper.kill();
    EXPECT_TRUE(waitUntil([&](const Graph &graph) { return !graph.linked(source->id, target->id); }))
        << "links outlived the process that made them";
}

// --- the feedback guard, in a real graph ---
//
// A sink and its monitor are **the same node**, so a ring can be built out of
// the harness's own devices and needs nothing invented: playing into a sink
// and capturing what comes out of it is a ring of one or two.
//
// The other shape the guard has to see — two nodes that pass sound between
// themselves through a `node.link-group`, as a loopback or a filter chain
// does — is covered by the unit tests in tests/unit/pw. It is not built here
// because pw-loopback 1.6.9 crashes when given both --capture-props and
// --playback-props, and a filter chain cannot present its playback side as a
// virtual source.

TEST_F(Links, RefusesALinkThatWouldCloseARing)
{
    // Our session already sends what reaches one sink into another. Sending
    // the second one's output back into the first closes the ring, and the
    // sound would go round for ever.
    Engine engine;
    ASSERT_TRUE(engine.waitUntilSettled());

    const QString first = QString::fromUtf8(kStereoSink);
    const QString second = QString::fromUtf8(kMonoSink);

    engine.manager().setDesiredLinks({byName(first, second, u"MONO"_s)});
    // The mono sink has one port; the stereo sink's monitor has two. Link the
    // left one of them into it.
    QList<DesiredLink> outward;
    DesiredLink forward;
    forward.from.node.nodeName = first;
    forward.from.node.excludeReplayed = false;
    forward.from.channel = u"FL"_s;
    forward.to.node.nodeName = second;
    forward.to.node.excludeReplayed = false;
    forward.to.channel = u"MONO"_s;
    outward.append(forward);
    engine.manager().setDesiredLinks(outward);
    ASSERT_TRUE(engine.pumpUntil([&] { return engine.manager().ownedLinkCount() == 1; }))
        << "the ordinary link was not made";

    QSignalSpy refused(&engine.manager(), &LinkManager::feedbackRefused);
    DesiredLink back;
    back.from.node.nodeName = second;
    back.from.node.excludeReplayed = false;
    back.from.channel = u"MONO"_s;
    back.to.node.nodeName = first;
    back.to.node.excludeReplayed = false;
    back.to.channel = u"FL"_s;
    engine.manager().setDesiredLinks({forward, back});

    ASSERT_TRUE(engine.pumpUntil([&] { return engine.manager().feedbackRefusalCount() > 0; }));
    EXPECT_EQ(engine.manager().ownedLinkCount(), 1) << "the ring-closing link must not have been made";
    ASSERT_GT(refused.count(), 0);

    const FeedbackPath path = refused.at(0).at(0).value<FeedbackPath>();
    EXPECT_GE(path.nodes.size(), 2) << "the way round has to be reported: " << path.describe().toStdString();
    EXPECT_FALSE(path.describe().isEmpty());
}

TEST_F(Links, RefusesARingWhoseTwoHalvesAreAskedForTogether)
{
    // Both links of a ring wanted at once, with every port already there:
    // one pass of the manager makes the first and is asked about the second
    // before the server has said the first exists. The guard has to count
    // the links the manager has just made as well as the ones the mirror has
    // heard of, or the ring is made whole (seen under asan, where the
    // server was slow to announce the first link).
    Engine engine;
    ASSERT_TRUE(engine.waitUntilSettled());

    const QString first = QString::fromUtf8(kStereoSink);
    const QString second = QString::fromUtf8(kMonoSink);
    DesiredLink forward;
    forward.from.node.nodeName = first;
    forward.from.node.excludeReplayed = false;
    forward.from.channel = u"FL"_s;
    forward.to.node.nodeName = second;
    forward.to.node.excludeReplayed = false;
    forward.to.channel = u"MONO"_s;
    DesiredLink back;
    back.from.node.nodeName = second;
    back.from.node.excludeReplayed = false;
    back.from.channel = u"MONO"_s;
    back.to.node.nodeName = first;
    back.to.node.excludeReplayed = false;
    back.to.channel = u"FL"_s;

    QSignalSpy refused(&engine.manager(), &LinkManager::feedbackRefused);
    engine.manager().setDesiredLinks({forward, back});

    ASSERT_TRUE(engine.pumpUntil([&] { return engine.manager().feedbackRefusalCount() > 0; }))
        << "the ring was never noticed; links owned: " << engine.manager().ownedLinkCount();
    engine.pump(std::chrono::milliseconds{300});
    EXPECT_EQ(engine.manager().ownedLinkCount(), 1) << "both halves of the ring were made";
    ASSERT_GT(refused.count(), 0);
}

TEST_F(Links, RefusesToCaptureWhatItIsPlayingInto)
{
    // The simplest ring of all, and the one a user is most likely to build by
    // accident: a session whose input is the monitor of the sink its output
    // goes to. Here that is one node, so the ring is of one.
    Engine engine;
    ASSERT_TRUE(engine.waitUntilSettled());

    const QString sink = QString::fromUtf8(kStereoSink);
    engine.manager().setDesiredLinks({byName(sink, sink, u"FL"_s), byName(sink, sink, u"FR"_s)});

    ASSERT_TRUE(engine.pumpUntil([&] { return engine.manager().feedbackRefusalCount() > 0; }));
    EXPECT_EQ(engine.manager().ownedLinkCount(), 0);
}

TEST_F(Links, DoesNotLetARingStandWhenItClosesAroundItLater)
{
    // A link that was safe when it was made stops being safe when the graph
    // changes around it — a default device moves, or something else adds a
    // link. Within a quarter of a second the ring must be gone, and it must
    // not come back.
    Engine engine;
    ASSERT_TRUE(engine.waitUntilSettled());

    const std::optional<Graph::Node> stereo = waitForNode(nodeNamed(kStereoSink));
    const std::optional<Graph::Node> mono = waitForNode(nodeNamed(kMonoSink));
    ASSERT_TRUE(stereo.has_value());
    ASSERT_TRUE(mono.has_value());

    DesiredLink ours;
    ours.from.node.nodeName = QString::fromUtf8(kStereoSink);
    ours.from.node.excludeReplayed = false;
    ours.from.channel = u"FL"_s;
    ours.to.node.nodeName = QString::fromUtf8(kMonoSink);
    ours.to.node.excludeReplayed = false;
    ours.to.channel = u"MONO"_s;
    engine.manager().setDesiredLinks({ours});
    ASSERT_TRUE(engine.pumpUntil([&] { return engine.manager().ownedLinkCount() == 1; }));
    ASSERT_EQ(engine.manager().feedbackRefusalCount(), 0) << "an ordinary link was mistaken for a ring";

    // Somebody else now joins the mono sink's monitor back to the stereo
    // sink, which closes the ring around our link.
    const QList<PwPort> monoOut = engine.mirror().portsOf(mono->id, PortDirection::Output);
    const QList<PwPort> stereoIn = engine.mirror().portsOf(stereo->id, PortDirection::Input);
    ASSERT_FALSE(monoOut.isEmpty());
    ASSERT_FALSE(stereoIn.isEmpty());
    ASSERT_TRUE(linkPorts(monoOut.front().object.id, stereoIn.front().object.id));

    // pw-dump is the referee for when the ring actually exists; the manager's
    // 250 ms is counted from there, not from when the tool was asked.
    ASSERT_TRUE(waitForLink(mono->id, stereo->id));

    QElapsedTimer timer;
    timer.start();
    EXPECT_TRUE(
        engine.pumpUntil([&] { return engine.manager().ownedLinkCount() == 0; }, std::chrono::milliseconds{250}))
        << "the ring was still there after " << timer.elapsed() << " ms, with " << engine.manager().ownedLinkCount()
        << " of our links in it";

    // PipeWire's scheduler will not run a graph with a ring in it, so it may
    // take the link away before the manager gets to. What matters either way
    // is that the ring does not come back: the manager is asked for the same
    // link every pass, and has to keep refusing it.
    EXPECT_TRUE(engine.pumpUntil([&] { return engine.manager().feedbackRefusalCount() > 0; }, std::chrono::seconds{2}))
        << "the manager did not recognise the ring";
    engine.pump(std::chrono::milliseconds{500});
    EXPECT_EQ(engine.manager().ownedLinkCount(), 0) << "the manager made the ring again";

    static_cast<void>(unlinkPorts(monoOut.front().object.id, stereoIn.front().object.id));
}

TEST_F(Links, AStreamOfOtherLinksNeitherHoldsThePassOffNorRunsIt)
{
    // Every link anybody makes is a reason to look for a ring again,
    // and links are made all the time — a stream is linked port by port, a
    // patchbay makes a dozen at once. The manager must look within one settle
    // time of the first, however many follow, and must not look once per
    // link.
    Engine engine;
    const std::chrono::milliseconds settle{200};
    engine.manager().setSettleTime(settle);
    ASSERT_TRUE(engine.waitUntilSettled());

    const std::optional<Graph::Node> surround = waitForNode(nodeNamed(kSurroundSink));
    const std::optional<Graph::Node> aux = waitForNode(nodeNamed(kAuxSink));
    ASSERT_TRUE(surround.has_value() && aux.has_value());
    ASSERT_TRUE(engine.pumpUntil([&] {
        return engine.mirror().portsOf(surround->id, PortDirection::Output).size() == 6
            && engine.mirror().portsOf(aux->id, PortDirection::Input).size() == 4;
    }));
    const QList<PwPort> outputs = engine.mirror().portsOf(surround->id, PortDirection::Output);
    const QList<PwPort> inputs = engine.mirror().portsOf(aux->id, PortDirection::Input);

    // Nothing of ours in the way, and nothing waiting.
    engine.pump(settle * 2);
    QSignalSpy passes(&engine.manager(), &LinkManager::reconciled);

    // Every monitor port of the one into every input of the other: two dozen
    // links, a few settle times' worth of them.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> theirs;
    for (const PwPort &out : outputs) {
        for (const PwPort &in : inputs) {
            theirs.emplace_back(out.object.id, in.object.id);
        }
    }
    QElapsedTimer timer;
    timer.start();
    QStringList when; // the time of each pass, for the messages below
    QObject::connect(&engine.manager(), &LinkManager::reconciled, &engine.manager(), [&] {
        when.append(QString::number(timer.elapsed()));
    });
    for (const auto &[out, in] : theirs) {
        ASSERT_TRUE(linkPorts(out, in));
        engine.pump(std::chrono::milliseconds{60});
    }
    const qint64 streamMs = timer.elapsed();
    const qsizetype duringTheStream = passes.count();
    engine.pump(settle * 2);
    const qint64 totalMs = timer.elapsed();

    // A pass at most once a settle time, and one more for the tail.
    const qsizetype most = static_cast<qsizetype>(totalMs / settle.count()) + 2;
    EXPECT_LE(passes.count(), most) << theirs.size() << " links over " << totalMs << " ms made " << passes.count()
                                    << " passes, at " << when.join(u", "_s).toStdString() << " ms";
    EXPECT_LT(passes.count(), static_cast<qsizetype>(theirs.size()))
        << "one pass per link, at " << when.join(u", "_s).toStdString() << " ms";
    // The stream lasts several settle times; a pass that every new link put
    // off again would not have happened yet.
    if (streamMs > 3 * settle.count()) {
        EXPECT_GE(duringTheStream, 1) << "no pass in " << streamMs << " ms of links arriving; passes at "
                                      << when.join(u", "_s).toStdString() << " ms";
    }
    EXPECT_GE(passes.count(), 1) << "the links were never looked at";

    for (const auto &[out, in] : theirs) {
        static_cast<void>(unlinkPorts(out, in));
    }
}

TEST_F(Links, MakesNoFalsePositiveForAnOrdinaryChain)
{
    // The everyday shape: something into the session, the session out to a
    // device, and a second session on another device entirely. Nothing here
    // may be refused.
    Engine engine;
    ASSERT_TRUE(engine.waitUntilSettled());

    CaptureNodeOptions one;
    one.name = "rbtest.fb.chain.a";
    one.channels = {"FL", "FR"};
    CaptureNode sessionOne(one);
    CaptureNodeOptions two;
    two.name = "rbtest.fb.chain.b";
    two.channels = {"FL", "FR"};
    CaptureNode sessionTwo(two);
    ASSERT_TRUE(sessionOne.waitUntilReady().has_value());
    ASSERT_TRUE(sessionTwo.waitUntilReady().has_value());
    ASSERT_TRUE(engine.pumpUntil([&] { return engine.mirror().nodeNamed(u"rbtest.fb.chain.b"_s).has_value(); }));

    FakeAppOptions options;
    options.applicationName = "ChainApp";
    options.binary = "chainapp";
    const FakeApp app(options);
    ASSERT_TRUE(waitForNode(playbackStreamOf("ChainApp")));

    QList<DesiredLink> wanted;
    for (const QString &channel : {u"FL"_s, u"FR"_s}) {
        wanted.append(byName(QString::fromUtf8(kStereoSource), u"rbtest.fb.chain.a"_s, channel));
        wanted.append(byName(QString::fromUtf8(kStereoSink), u"rbtest.fb.chain.b"_s, channel));
    }
    engine.manager().setDesiredLinks(wanted);

    ASSERT_TRUE(engine.pumpUntil([&] { return engine.manager().ownedLinkCount() == 4; }))
        << "only " << engine.manager().ownedLinkCount() << " of the four ordinary links were made";
    EXPECT_EQ(engine.manager().feedbackRefusalCount(), 0) << "an ordinary chain was mistaken for a ring";
}

TEST_F(Links, TwoSessionsOnTheSameSinkStayBounded)
{
    // Two sessions both playing to the speakers and both trying to capture
    // what reaches them. Each is a ring of its own, and left alone the level
    // would be at full scale within seconds. The guard refuses the capture
    // links, so the sink stays where the application put it.
    Engine engine;
    ASSERT_TRUE(engine.waitUntilSettled());

    CaptureNodeOptions first;
    first.name = "rbtest.fb.two.a";
    first.channels = {"FL", "FR"};
    CaptureNode sessionOne(first);
    CaptureNodeOptions second;
    second.name = "rbtest.fb.two.b";
    second.channels = {"FL", "FR"};
    CaptureNode sessionTwo(second);
    ASSERT_TRUE(sessionOne.waitUntilReady().has_value());
    ASSERT_TRUE(sessionTwo.waitUntilReady().has_value());
    ASSERT_TRUE(engine.pumpUntil([&] { return engine.mirror().nodeNamed(u"rbtest.fb.two.b"_s).has_value(); }));

    const QString sink = QString::fromUtf8(kStereoSink);
    QList<DesiredLink> wanted;
    for (const QString &channel : {u"FL"_s, u"FR"_s}) {
        // What each session captures: the monitor of the sink it plays to.
        wanted.append(byName(sink, u"rbtest.fb.two.a"_s, channel));
        wanted.append(byName(sink, u"rbtest.fb.two.b"_s, channel));
        // And a ring for each: the same sink's monitor back into itself.
        wanted.append(byName(sink, sink, channel));
    }
    engine.manager().setDesiredLinks(wanted);
    ASSERT_TRUE(engine.pumpUntil([&] { return engine.manager().feedbackRefusalCount() > 0; }));

    // Something real to listen to, so the level is not simply silence.
    FakeAppOptions options;
    options.applicationName = "BoundedApp";
    options.target = kStereoSink;
    const FakeApp app(options);
    ASSERT_TRUE(waitForNode(playbackStreamOf("BoundedApp")));

    // **As long as the soak**, ten seconds unless RB_SOAK_SECONDS says
    // otherwise, as its System Audio twin listens: a ring gains its
    // loop's gain on every trip round and is at full scale within a second
    // or two, so a minute on every run was fifty seconds of a tone that was
    // fine. RB_SOAK_SECONDS=60 is the minute the feedback guard was judged
    // over.
    const qint64 listenMs = static_cast<qint64>(soakSeconds()) * 1000;
    QElapsedTimer timer;
    timer.start();
    double loudest = -200.0;
    while (timer.elapsed() < listenMs) {
        engine.pump(std::chrono::milliseconds{100});
        const std::optional<double> peak = measurePeakDb(kStereoSink, std::chrono::milliseconds{200});
        if (peak) {
            loudest = std::max(loudest, *peak);
        }
        ASSERT_LT(loudest, -1.0) << "the level ran away to " << loudest << " dBFS after " << timer.elapsed() << " ms";
    }
    EXPECT_LT(loudest, -1.0) << "loudest was " << loudest << " dBFS";
    EXPECT_GT(loudest, -40.0) << "nothing was playing, so the test proved nothing";
    EXPECT_EQ(engine.manager().ownedLinkCount(), 4) << "the four capture links into the sessions should be there";
}

TEST_F(Links, AStreamItsMoveLeftSuspendedIsWokenAndComesBackWithItsLayout)
{
    // **A move that remakes a stream's ports under our links**: to
    // move a stream to a sink with another layout the policy gives it that
    // layout, remaking its ports, and a link of ours still on the old ones
    // leaves the stream suspended where it lands — silent there, and unable
    // to take another layout. The manager lets go of a stream it is told is
    // moving; this one it is not let go of first, as when the policy is
    // quicker, so the repair is what is under test.
    Engine engine;
    ASSERT_TRUE(engine.waitUntilSettled());
    engine.manager().setLetGoBeforeMoves(false);

    CaptureNodeOptions target;
    target.name = "rbtest.links.mover";
    target.channels = {"FL", "FR"};
    target.props = {{"node.always-process", "true"}};
    CaptureNode capture(target);
    ASSERT_TRUE(capture.waitUntilReady().has_value());
    ASSERT_TRUE(engine.pumpUntil([&] { return engine.mirror().nodeNamed(u"rbtest.links.mover"_s).has_value(); }));

    DesiredLink left;
    left.from.node.applicationBinary = u"movingapp"_s;
    left.from.node.kind = NodeKind::PlaybackStream;
    left.from.channel = u"FL"_s;
    left.to.node.nodeName = u"rbtest.links.mover"_s;
    left.to.node.excludeReplayed = false;
    left.to.channel = u"FL"_s;
    DesiredLink right = left;
    right.from.channel = u"FR"_s;
    right.to.channel = u"FR"_s;
    engine.manager().setDesiredLinks({left, right});

    FakeAppOptions options;
    options.applicationName = "MovingApp";
    options.binary = "movingapp";
    options.target.clear(); // so the policy will move it
    FakeApp app(options);
    const std::optional<Graph::Node> found = waitForNode(playbackStreamOf("MovingApp"));
    ASSERT_TRUE(found.has_value());
    const std::uint32_t stream = found->id;
    ASSERT_TRUE(engine.pumpUntil([&] { return engine.manager().ownedLinkCount() == 2; }));

    const auto channelsOf = [&](std::uint32_t node, PortDirection direction) {
        QStringList channels;
        for (const PwPort &port : engine.mirror().portsOf(node, direction)) {
            if (!port.isMonitor) {
                channels.append(port.channel);
            }
        }
        channels.sort();
        return channels;
    };
    const auto playsOn = [&](const char *sinkName) {
        const std::optional<PwNode> sink = engine.mirror().nodeNamed(QString::fromLatin1(sinkName));
        const std::optional<PwNode> playing = engine.mirror().node(stream);
        return sink && playing && engine.mirror().isLinked(stream, sink->object.id)
            && channelsOf(stream, PortDirection::Output) == channelsOf(sink->object.id, PortDirection::Input);
    };
    const auto runs = [&] {
        const std::optional<PwNode> playing = engine.mirror().node(stream);
        return playing && playing->state == NodeState::Running;
    };

    // To the mono sink, with our links on its stereo ports. There it has
    // nothing we want (the links name FL and FR), and it must play.
    moveStream(stream, kMonoSink);
    ASSERT_TRUE(engine.pumpUntil([&] { return playsOn(kMonoSink); }, std::chrono::seconds{5}))
        << "the policy never gave it the mono sink's layout";
    EXPECT_TRUE(engine.pumpUntil(runs, std::chrono::seconds{3}))
        << "the stream was left suspended on the mono sink: silent there";
    EXPECT_EQ(engine.manager().wakeUpCount(), 1);
    EXPECT_TRUE(engine.pumpUntil([&] { return engine.manager().ownedLinkCount() == 0; }))
        << "a link of ours stayed on the stream after it was woken";

    // And back: it takes the stereo layout again, and our links return.
    moveStream(stream, kStereoSink);
    EXPECT_TRUE(engine.pumpUntil([&] { return playsOn(kStereoSink); }, std::chrono::seconds{5}))
        << "the stream came back to stereo with the layout "
        << channelsOf(stream, PortDirection::Output).join(u',').toStdString();
    EXPECT_TRUE(engine.pumpUntil([&] { return engine.manager().ownedLinkCount() == 2; }));
}
