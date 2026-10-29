// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What each source block should be capturing, worked out from a graph
// written out by hand. The links it turns into are made against a real
// server in tests/pw/test_capture.cpp.

#include "blocks/applicationsource.h"
#include "blocks/inputdevice.h"
#include "blocks/outputdevice.h"
#include "blocks/systemaudio.h"
#include "core/sessionplan.h"

#include <QJsonArray>
#include <gtest/gtest.h>

using namespace rb;
using namespace rb::core;
using namespace Qt::StringLiterals;

namespace
{

std::uint32_t nextId = 1;

/// A source block. Unless the parameters say otherwise it takes everything
/// the application plays, so that a test about matching is about matching
/// and not about where the sound is going.
[[nodiscard]] BlockInstance sourceBlock(const QString &id, QJsonObject params)
{
    BlockInstance block;
    block.id = id;
    block.type = QString::fromUtf8(blocks::ApplicationSource::kTypeId.data(),
                                   static_cast<qsizetype>(blocks::ApplicationSource::kTypeId.size()));
    if (!params.contains("outputMode"_L1)) {
        params.insert("outputMode"_L1, u"any"_s);
    }
    block.params = std::move(params);
    return block;
}

/// A sink, for the tests that care where a stream is going.
[[nodiscard]] pw::PwNode sink(const QString &name)
{
    pw::PwNode node;
    node.object.id = nextId++;
    node.object.serial = node.object.id * 100;
    node.name = name;
    node.mediaClass = u"Audio/Sink"_s;
    node.kind = pw::NodeKind::Sink;
    return node;
}

[[nodiscard]] pw::PwLink playsInto(const pw::PwNode &stream, const pw::PwNode &sink)
{
    pw::PwLink link;
    link.object.id = nextId++;
    link.outputNode = stream.object.id;
    link.inputNode = sink.object.id;
    return link;
}

[[nodiscard]] pw::PwDefaults defaultsOf(const QString &sinkName)
{
    pw::PwDefaults defaults;
    defaults.audioSink = sinkName;
    return defaults;
}

[[nodiscard]] pw::PwNode stream(const QString &applicationName, const QString &binary)
{
    pw::PwNode node;
    node.object.id = nextId++;
    node.object.serial = node.object.id * 100;
    node.name = u"pw-cat"_s; // what most streams are called, and never unique
    node.mediaClass = u"Stream/Output/Audio"_s;
    node.kind = pw::NodeKind::PlaybackStream;
    node.applicationName = applicationName;
    node.applicationBinary = binary;
    return node;
}

/// Gives a node the ports of a layout, in order: outputs unless it says
/// otherwise.
void givePorts(const pw::PwNode &node,
               const QStringList &channels,
               QList<pw::PwPort> &ports,
               pw::PortDirection direction = pw::PortDirection::Output)
{
    std::uint32_t index = 0;
    for (const QString &channel : channels) {
        pw::PwPort port;
        port.object.id = nextId++;
        port.nodeId = node.object.id;
        port.direction = direction;
        port.index = index++;
        port.channel = channel;
        ports.append(port);
    }
}

[[nodiscard]] Session sessionWith(const QList<BlockInstance> &blocks)
{
    Session session;
    session.name = u"Capture"_s;
    for (const BlockInstance &block : blocks) {
        session.blocks.append(block);
    }
    return session;
}

} // namespace

TEST(CapturePlanTest, TheBinaryIsWhatIsMatchedOnWhenThereIsOne)
{
    // All three fields are stored so that a session found the application
    // however it was installed, but only the first one that is set is used:
    // every field of a match has to match, so a rule naming all three would
    // find nothing on a machine where the application came from elsewhere.
    const pw::EndpointMatch match = applicationMatch(sourceBlock(u"b1"_s,
                                                                 QJsonObject{
                                                                     {"binary"_L1, u"firefox"_s},
                                                                     {"flatpakId"_L1, u"org.mozilla.firefox"_s},
                                                                     {"name"_L1, u"Firefox"_s},
                                                                 }));
    EXPECT_EQ(match.applicationBinary, u"firefox"_s);
    EXPECT_TRUE(match.flatpakId.isEmpty());
    EXPECT_TRUE(match.applicationName.isEmpty());
    ASSERT_TRUE(match.kind.has_value());
    EXPECT_EQ(*match.kind, pw::NodeKind::PlaybackStream) << "a recording stream is not something to capture";
    EXPECT_TRUE(match.excludeOurs);
    EXPECT_TRUE(match.excludeNotifications);
    EXPECT_TRUE(match.excludeReplayed);
}

TEST(CapturePlanTest, AFlatpakIdOrAName)
{
    const pw::EndpointMatch flatpak = applicationMatch(
        sourceBlock(u"b1"_s, QJsonObject{{"flatpakId"_L1, u"org.videolan.VLC"_s}, {"name"_L1, u"VLC"_s}}));
    EXPECT_EQ(flatpak.flatpakId, u"org.videolan.VLC"_s);
    EXPECT_TRUE(flatpak.applicationName.isEmpty());

    const pw::EndpointMatch named = applicationMatch(sourceBlock(u"b1"_s, QJsonObject{{"name"_L1, u"Music"_s}}));
    EXPECT_EQ(named.applicationName, u"Music"_s);

    // Nothing chosen matches nothing, rather than everything.
    EXPECT_TRUE(applicationMatch(sourceBlock(u"b1"_s, QJsonObject{})).isEmpty());
}

TEST(CapturePlanTest, ABlockWithNoApplicationStillHasPorts)
{
    const Session session = sessionWith({sourceBlock(u"b1"_s, QJsonObject{})});
    const QList<CaptureTarget> plan = capturePlan(session, {}, {}, {}, {});

    ASSERT_EQ(plan.size(), 1);
    EXPECT_EQ(plan.at(0).blockId, u"b1"_s);
    EXPECT_EQ(plan.at(0).streamCount, 0);
    // A stereo pair, so that the first stream has somewhere to arrive and
    // the tile has something to show.
    EXPECT_EQ(plan.at(0).layout, dsp::stereoLayout());
}

TEST(CapturePlanTest, EveryStreamOfTheApplicationCounts)
{
    QList<pw::PwPort> ports;
    const pw::PwNode first = stream(u"Firefox"_s, u"firefox"_s);
    givePorts(first, {u"FL"_s, u"FR"_s}, ports);
    const pw::PwNode second = stream(u"Firefox"_s, u"firefox"_s);
    givePorts(second, {u"FL"_s, u"FR"_s}, ports);
    const pw::PwNode other = stream(u"mpv"_s, u"mpv"_s);
    givePorts(other, {u"FL"_s, u"FR"_s}, ports);

    const Session session = sessionWith({sourceBlock(u"b1"_s, QJsonObject{{"binary"_L1, u"firefox"_s}})});
    const QList<CaptureTarget> plan = capturePlan(session, {first, second, other}, ports, {}, {});

    ASSERT_EQ(plan.size(), 1);
    EXPECT_EQ(plan.at(0).streamCount, 2) << "the other application was counted, or one of ours was missed";
    EXPECT_EQ(plan.at(0).layout, dsp::stereoLayout());
}

TEST(CapturePlanTest, TheWidestLayoutWins)
{
    // One tab playing stereo and another playing to a 5.1 sink: the block
    // needs a port for every channel of both, because a link only ever joins
    // two ports of the same channel.
    QList<pw::PwPort> ports;
    const pw::PwNode stereo = stream(u"Firefox"_s, u"firefox"_s);
    givePorts(stereo, {u"FL"_s, u"FR"_s}, ports);
    const pw::PwNode surround = stream(u"Firefox"_s, u"firefox"_s);
    givePorts(surround, {u"FL"_s, u"FR"_s, u"FC"_s, u"LFE"_s, u"RL"_s, u"RR"_s}, ports);

    const Session session = sessionWith({sourceBlock(u"b1"_s, QJsonObject{{"binary"_L1, u"firefox"_s}})});
    const QList<CaptureTarget> plan = capturePlan(session, {stereo, surround}, ports, {}, {});

    ASSERT_EQ(plan.size(), 1);
    EXPECT_EQ(plan.at(0).layout, dsp::surround51Layout());
    EXPECT_EQ(plan.at(0).streamCount, 2);
}

TEST(CapturePlanTest, APortsOrderIsTheNodesOwnAndNotTheOrderTheyArrivedIn)
{
    QList<pw::PwPort> ports;
    const pw::PwNode node = stream(u"Player"_s, u"player"_s);
    // Announced out of order, as the server may.
    givePorts(node, {u"FR"_s}, ports);
    ports.last().index = 1;
    givePorts(node, {u"FL"_s}, ports);
    ports.last().index = 0;

    const Session session = sessionWith({sourceBlock(u"b1"_s, QJsonObject{{"binary"_L1, u"player"_s}})});
    const QList<CaptureTarget> plan = capturePlan(session, {node}, ports, {}, {});
    EXPECT_EQ(plan.at(0).layout, dsp::stereoLayout());
}

TEST(CapturePlanTest, OurOwnNodesAndAlertsAndReplaysAreNotCaptured)
{
    QList<pw::PwPort> ports;
    pw::PwNode ours = stream(u"Reverblabr"_s, u"reverblabr"_s);
    ours.isOurs = true;
    givePorts(ours, {u"FL"_s, u"FR"_s}, ports);
    pw::PwNode alert = stream(u"Player"_s, u"player"_s);
    alert.mediaRole = u"Notification"_s;
    givePorts(alert, {u"FL"_s, u"FR"_s}, ports);
    pw::PwNode replay = stream(u"Player"_s, u"player"_s);
    replay.linkGroup = u"loopback-1"_s;
    givePorts(replay, {u"FL"_s, u"FR"_s}, ports);
    const pw::PwNode real = stream(u"Player"_s, u"player"_s);
    givePorts(real, {u"FL"_s, u"FR"_s}, ports);

    const Session ourSession = sessionWith({sourceBlock(u"b1"_s, QJsonObject{{"binary"_L1, u"reverblabr"_s}})});
    EXPECT_EQ(capturePlan(ourSession, {ours}, ports, {}, {}).at(0).streamCount, 0)
        << "a session offered to capture itself";

    const Session session = sessionWith({sourceBlock(u"b1"_s, QJsonObject{{"binary"_L1, u"player"_s}})});
    const QList<CaptureTarget> plan = capturePlan(session, {alert, replay, real}, ports, {}, {});
    EXPECT_EQ(plan.at(0).streamCount, 1) << "an alert or a re-play was counted as the application playing";
}

TEST(CapturePlanTest, ABlockThatIsNotASourceIsNotInThePlan)
{
    BlockInstance effect;
    effect.id = u"fx"_s;
    effect.type = u"effect.gain"_s;
    const Session session = sessionWith({effect, sourceBlock(u"b1"_s, QJsonObject{{"binary"_L1, u"mpv"_s}})});

    const QList<CaptureTarget> plan = capturePlan(session, {}, {}, {}, {});
    ASSERT_EQ(plan.size(), 1);
    EXPECT_EQ(plan.at(0).blockId, u"b1"_s);
}

TEST(CapturePlanTest, TheDefaultModeTakesOnlyWhatGoesToTheDefaultOutput)
{
    // What most people mean by "the sound of this application": what it
    // sends anywhere else, it sends there on purpose.
    QList<pw::PwPort> ports;
    const pw::PwNode speakers = sink(u"alsa.speakers"_s);
    const pw::PwNode headset = sink(u"bluez.headset"_s);
    const pw::PwNode call = stream(u"Chat"_s, u"chat"_s);
    givePorts(call, {u"FL"_s, u"FR"_s}, ports);
    const pw::PwNode alertish = stream(u"Chat"_s, u"chat"_s);
    givePorts(alertish, {u"FL"_s, u"FR"_s}, ports);

    BlockInstance block = sourceBlock(u"b1"_s, QJsonObject{{"binary"_L1, u"chat"_s}});
    block.params.insert("outputMode"_L1, u"default"_s);
    const Session session = sessionWith({block});

    const QList<CaptureTarget> plan = capturePlan(session,
                                                  {speakers, headset, call, alertish},
                                                  ports,
                                                  {playsInto(call, speakers), playsInto(alertish, headset)},
                                                  defaultsOf(u"alsa.speakers"_s));
    ASSERT_EQ(plan.size(), 1);
    EXPECT_EQ(plan.at(0).streamCount, 1) << "the stream on the headset was counted as well";
    EXPECT_EQ(plan.at(0).match.playingTo, QStringList{u"alsa.speakers"_s});
}

TEST(CapturePlanTest, TheDefaultModeWithNoDefaultOutputTakesNothing)
{
    QList<pw::PwPort> ports;
    const pw::PwNode playing = stream(u"Chat"_s, u"chat"_s);
    givePorts(playing, {u"FL"_s, u"FR"_s}, ports);
    BlockInstance block = sourceBlock(u"b1"_s, QJsonObject{{"binary"_L1, u"chat"_s}});
    block.params.insert("outputMode"_L1, u"default"_s);

    const QList<CaptureTarget> plan = capturePlan(sessionWith({block}), {playing}, ports, {}, {});
    ASSERT_EQ(plan.size(), 1);
    // Nothing is playing to a device that is not there, so there is nothing
    // to capture — not everything.
    EXPECT_TRUE(plan.at(0).match.isEmpty());
    EXPECT_EQ(plan.at(0).streamCount, 0);
}

TEST(CapturePlanTest, TheChosenModeTakesOnlyWhatGoesToTheOutputsNamed)
{
    QList<pw::PwPort> ports;
    const pw::PwNode speakers = sink(u"alsa.speakers"_s);
    const pw::PwNode headset = sink(u"bluez.headset"_s);
    const pw::PwNode toSpeakers = stream(u"Chat"_s, u"chat"_s);
    givePorts(toSpeakers, {u"FL"_s, u"FR"_s}, ports);
    const pw::PwNode toHeadset = stream(u"Chat"_s, u"chat"_s);
    givePorts(toHeadset, {u"FL"_s, u"FR"_s}, ports);

    BlockInstance block = sourceBlock(u"b1"_s, QJsonObject{{"binary"_L1, u"chat"_s}});
    block.params.insert("outputMode"_L1, u"chosen"_s);
    block.params.insert("outputs"_L1, QJsonArray{u"bluez.headset"_s});

    const QList<CaptureTarget> plan = capturePlan(sessionWith({block}),
                                                  {speakers, headset, toSpeakers, toHeadset},
                                                  ports,
                                                  {playsInto(toSpeakers, speakers), playsInto(toHeadset, headset)},
                                                  defaultsOf(u"alsa.speakers"_s));
    ASSERT_EQ(plan.size(), 1);
    EXPECT_EQ(plan.at(0).streamCount, 1);
    EXPECT_EQ(plan.at(0).match.playingTo, QStringList{u"bluez.headset"_s});
}

TEST(CapturePlanTest, TheChosenModeWithNothingChosenTakesNothing)
{
    QList<pw::PwPort> ports;
    const pw::PwNode speakers = sink(u"alsa.speakers"_s);
    const pw::PwNode playing = stream(u"Chat"_s, u"chat"_s);
    givePorts(playing, {u"FL"_s, u"FR"_s}, ports);

    BlockInstance block = sourceBlock(u"b1"_s, QJsonObject{{"binary"_L1, u"chat"_s}});
    block.params.insert("outputMode"_L1, u"chosen"_s);
    block.params.insert("outputs"_L1, QJsonArray{});

    const QList<CaptureTarget> plan = capturePlan(sessionWith({block}),
                                                  {speakers, playing},
                                                  ports,
                                                  {playsInto(playing, speakers)},
                                                  defaultsOf(u"alsa.speakers"_s));
    // An empty list of conditions is not the absence of one.
    EXPECT_TRUE(plan.at(0).match.isEmpty());
    EXPECT_EQ(plan.at(0).streamCount, 0);
}

TEST(CapturePlanTest, AStreamNothingHasLinkedYetIsJudgedByWhatItAskedFor)
{
    // The policy takes a moment, and in that moment the stream is already
    // there with its ports. What it asked for is the best there is to go on;
    // without this a capture would flicker on at every start.
    QList<pw::PwPort> ports;
    const pw::PwNode speakers = sink(u"alsa.speakers"_s);
    pw::PwNode starting = stream(u"Chat"_s, u"chat"_s);
    starting.props.insert(u"target.object"_s, u"alsa.speakers"_s);
    givePorts(starting, {u"FL"_s, u"FR"_s}, ports);

    BlockInstance block = sourceBlock(u"b1"_s, QJsonObject{{"binary"_L1, u"chat"_s}});
    block.params.insert("outputMode"_L1, u"default"_s);

    const QList<CaptureTarget> plan =
        capturePlan(sessionWith({block}), {speakers, starting}, ports, {}, defaultsOf(u"alsa.speakers"_s));
    EXPECT_EQ(plan.at(0).streamCount, 1);
}

namespace
{

[[nodiscard]] BlockInstance systemBlock(const QString &id, QJsonObject params = {})
{
    BlockInstance block;
    block.id = id;
    block.type = QString::fromUtf8(blocks::SystemAudio::kTypeId.data(),
                                   static_cast<qsizetype>(blocks::SystemAudio::kTypeId.size()));
    block.params = std::move(params);
    return block;
}

} // namespace

TEST(SessionPlanTest, SystemAudioTakesEveryStreamButOurOwnAndTheRePlaysAndTheAlerts)
{
    QList<pw::PwPort> ports;
    const pw::PwNode player = stream(u"mpv"_s, u"mpv"_s);
    givePorts(player, {u"FL"_s, u"FR"_s}, ports);
    const pw::PwNode browser = stream(u"Firefox"_s, u"firefox"_s);
    givePorts(browser, {u"FL"_s, u"FR"_s}, ports);
    pw::PwNode ours = stream(u"Reverblabr"_s, u"reverblabr"_s);
    ours.isOurs = true;
    givePorts(ours, {u"FL"_s, u"FR"_s}, ports);
    pw::PwNode alert = stream(u"Plasma"_s, u"plasmashell"_s);
    alert.mediaRole = u"Notification"_s;
    givePorts(alert, {u"FL"_s, u"FR"_s}, ports);
    pw::PwNode replay = stream(u"Loopback"_s, u"pw-loopback"_s);
    replay.linkGroup = u"loopback-1"_s;
    givePorts(replay, {u"FL"_s, u"FR"_s}, ports);

    const QList<CaptureTarget> plan =
        capturePlan(sessionWith({systemBlock(u"sys"_s)}), {player, browser, ours, alert, replay}, ports, {}, {});
    ASSERT_EQ(plan.size(), 1);
    // **A kind on its own is a condition here**, because this block asked
    // for it; every other match would have to name something.
    EXPECT_FALSE(plan.at(0).match.isEmpty());
    EXPECT_EQ(plan.at(0).streamCount, 2) << "something was captured that should not have been";
}

TEST(SessionPlanTest, SystemAudioTakesAlertsWhenTheyAreAskedFor)
{
    QList<pw::PwPort> ports;
    const pw::PwNode player = stream(u"mpv"_s, u"mpv"_s);
    givePorts(player, {u"FL"_s, u"FR"_s}, ports);
    pw::PwNode alert = stream(u"Plasma"_s, u"plasmashell"_s);
    alert.eventId = u"message-new-instant"_s;
    givePorts(alert, {u"FL"_s, u"FR"_s}, ports);

    const BlockInstance block = systemBlock(u"sys"_s, QJsonObject{{"includeAlerts"_L1, true}});
    const QList<CaptureTarget> plan = capturePlan(sessionWith({block}), {player, alert}, ports, {}, {});
    EXPECT_EQ(plan.at(0).streamCount, 2);
    EXPECT_FALSE(plan.at(0).match.excludeNotifications);
}

TEST(SessionPlanTest, SystemAudioLeavesOutTheApplicationsItWasToldTo)
{
    QList<pw::PwPort> ports;
    const pw::PwNode player = stream(u"mpv"_s, u"mpv"_s);
    givePorts(player, {u"FL"_s, u"FR"_s}, ports);
    const pw::PwNode browser = stream(u"Firefox"_s, u"firefox"_s);
    givePorts(browser, {u"FL"_s, u"FR"_s}, ports);
    pw::PwNode sandboxed = stream(u"Video"_s, QString{});
    sandboxed.flatpakId = u"org.videolan.VLC"_s;
    givePorts(sandboxed, {u"FL"_s, u"FR"_s}, ports);

    // The same match rules the inventory hands the picker, and the same ones
    // an Application source stores.
    const BlockInstance block = systemBlock(
        u"sys"_s, QJsonObject{{"excluded"_L1, QJsonArray{u"binary:firefox"_s, u"flatpak:org.videolan.VLC"_s}}});
    const QList<CaptureTarget> plan = capturePlan(sessionWith({block}), {player, browser, sandboxed}, ports, {}, {});

    ASSERT_EQ(plan.size(), 1);
    EXPECT_EQ(plan.at(0).streamCount, 1) << "an excluded application was captured anyway";
    EXPECT_TRUE(plan.at(0).match.matches(player));
    EXPECT_FALSE(plan.at(0).match.matches(browser));
    EXPECT_FALSE(plan.at(0).match.matches(sandboxed));
}

TEST(SessionPlanTest, SystemAudioTakesTheWidestLayoutOfWhatIsPlaying)
{
    QList<pw::PwPort> ports;
    const pw::PwNode stereo = stream(u"mpv"_s, u"mpv"_s);
    givePorts(stereo, {u"FL"_s, u"FR"_s}, ports);
    const pw::PwNode surround = stream(u"Game"_s, u"game"_s);
    givePorts(surround, {u"FL"_s, u"FR"_s, u"FC"_s, u"LFE"_s, u"RL"_s, u"RR"_s}, ports);

    const QList<CaptureTarget> plan =
        capturePlan(sessionWith({systemBlock(u"sys"_s)}), {stereo, surround}, ports, {}, {});
    EXPECT_EQ(plan.at(0).layout, dsp::surround51Layout());
    EXPECT_EQ(plan.at(0).streamCount, 2);
}

// --- the microphone the application is itself recording from ---

namespace
{

/// An application's own capture stream: the application listening, which is
/// the one place in the engine that wants one.
[[nodiscard]] pw::PwNode recordStream(const QString &applicationName, const QString &binary)
{
    pw::PwNode node;
    node.object.id = nextId++;
    node.object.serial = node.object.id * 100;
    node.name = u"pw-cat"_s;
    node.mediaClass = u"Stream/Input/Audio"_s;
    node.kind = pw::NodeKind::RecordStream;
    node.applicationName = applicationName;
    node.applicationBinary = binary;
    return node;
}

[[nodiscard]] pw::PwNode source(const QString &name)
{
    pw::PwNode node;
    node.object.id = nextId++;
    node.object.serial = node.object.id * 100;
    node.name = name;
    node.mediaClass = u"Audio/Source"_s;
    node.kind = pw::NodeKind::Source;
    return node;
}

[[nodiscard]] pw::PwLink recordsFrom(const pw::PwNode &source, const pw::PwNode &stream)
{
    pw::PwLink link;
    link.object.id = nextId++;
    link.outputNode = source.object.id;
    link.inputNode = stream.object.id;
    return link;
}

[[nodiscard]] BlockInstance sourceIncludingInput(const QString &id, QJsonObject params)
{
    params.insert("includeInput"_L1, true);
    return sourceBlock(id, std::move(params));
}

} // namespace

TEST(SessionPlanTest, TheMicrophoneIsWhicheverOneTheApplicationIsUsing)
{
    // **Nothing about it is stored.** The application chooses a microphone
    // and we follow its own capture stream back to whatever source that is,
    // so what we capture is what it is hearing.
    const QList<BlockInstance> blocks{sourceIncludingInput(u"b1"_s, {{"binary"_L1, u"chatty"_s}})};
    const pw::PwNode playing = stream(u"Chatty"_s, u"chatty"_s);
    const pw::PwNode listening = recordStream(u"Chatty"_s, u"chatty"_s);
    const pw::PwNode headset = source(u"alsa_input.headset"_s);

    QList<pw::PwPort> ports;
    givePorts(playing, {u"FL"_s, u"FR"_s}, ports);
    givePorts(headset, {u"MONO"_s}, ports);

    const QList<pw::PwNode> nodes{playing, listening, headset};
    const QList<pw::PwLink> links{recordsFrom(headset, listening)};
    const QList<CaptureTarget> plan = capturePlan(sessionWith(blocks), nodes, ports, links, {});

    ASSERT_EQ(plan.size(), 1);
    EXPECT_EQ(plan.at(0).microphone.nodeName, u"alsa_input.headset"_s);
    EXPECT_EQ(plan.at(0).microphone.kind, pw::NodeKind::Source);
    // A mono microphone gets one port of its own, named apart from the
    // application's: the layout is the device's, not a stereo guess.
    ASSERT_EQ(plan.at(0).microphoneLayout.size(), 1U);
    EXPECT_EQ(plan.at(0).microphoneLayout.at(0), dsp::ChannelPosition::Mono);
    // The application's side is untouched by any of it.
    EXPECT_EQ(plan.at(0).layout.size(), 2U);
}

TEST(SessionPlanTest, SwitchingMicrophonesInTheApplicationSwitchesOurs)
{
    // No edit, no stored device: the next plan is worked out from the graph
    // as it stands, and the graph is where the answer lives.
    const QList<BlockInstance> blocks{sourceIncludingInput(u"b1"_s, {{"binary"_L1, u"chatty"_s}})};
    const pw::PwNode listening = recordStream(u"Chatty"_s, u"chatty"_s);
    const pw::PwNode headset = source(u"alsa_input.headset"_s);
    const pw::PwNode webcam = source(u"alsa_input.webcam"_s);

    QList<pw::PwPort> ports;
    givePorts(headset, {u"MONO"_s}, ports);
    givePorts(webcam, {u"FL"_s, u"FR"_s}, ports);
    const QList<pw::PwNode> nodes{listening, headset, webcam};

    const QList<CaptureTarget> first =
        capturePlan(sessionWith(blocks), nodes, ports, {recordsFrom(headset, listening)}, {});
    ASSERT_EQ(first.size(), 1);
    EXPECT_EQ(first.at(0).microphone.nodeName, u"alsa_input.headset"_s);

    const QList<CaptureTarget> second =
        capturePlan(sessionWith(blocks), nodes, ports, {recordsFrom(webcam, listening)}, {});
    ASSERT_EQ(second.size(), 1);
    EXPECT_EQ(second.at(0).microphone.nodeName, u"alsa_input.webcam"_s);
    // And the ports follow it, because a stereo webcam has two of them.
    EXPECT_EQ(second.at(0).microphoneLayout.size(), 2U);
}

TEST(SessionPlanTest, AStreamTheServerHasNotLinkedYetIsJudgedByWhatItAskedFor)
{
    // The same rule the playback side uses in the other direction: a stream
    // nothing has linked is judged by the target it asked for, so the first
    // plan after an application starts is already right.
    const QList<BlockInstance> blocks{sourceIncludingInput(u"b1"_s, {{"binary"_L1, u"chatty"_s}})};
    pw::PwNode listening = recordStream(u"Chatty"_s, u"chatty"_s);
    listening.props.insert(u"target.object"_s, u"alsa_input.headset"_s);
    const pw::PwNode headset = source(u"alsa_input.headset"_s);

    QList<pw::PwPort> ports;
    givePorts(headset, {u"MONO"_s}, ports);
    const QList<CaptureTarget> plan = capturePlan(sessionWith(blocks), {listening, headset}, ports, {}, {});

    ASSERT_EQ(plan.size(), 1);
    EXPECT_EQ(plan.at(0).microphone.nodeName, u"alsa_input.headset"_s);
}

TEST(SessionPlanTest, WithTheOptionOffThereIsNoMicrophoneAtAll)
{
    const QList<BlockInstance> blocks{sourceBlock(u"b1"_s, {{"binary"_L1, u"chatty"_s}})};
    const pw::PwNode listening = recordStream(u"Chatty"_s, u"chatty"_s);
    const pw::PwNode headset = source(u"alsa_input.headset"_s);
    QList<pw::PwPort> ports;
    givePorts(headset, {u"MONO"_s}, ports);

    const QList<CaptureTarget> plan =
        capturePlan(sessionWith(blocks), {listening, headset}, ports, {recordsFrom(headset, listening)}, {});
    ASSERT_EQ(plan.size(), 1);
    EXPECT_TRUE(plan.at(0).microphone.isEmpty());
    EXPECT_TRUE(plan.at(0).microphoneLayout.empty());
}

TEST(SessionPlanTest, IncludingTheInputOfAnApplicationThatIsNotRecordingWaitsWithPortsReady)
{
    // The same as an application that is not playing: the ports are there
    // for a source to be linked to when one appears, and nothing is linked
    // meanwhile because a match that names nothing matches nothing.
    const QList<BlockInstance> blocks{sourceIncludingInput(u"b1"_s, {{"binary"_L1, u"chatty"_s}})};
    const QList<CaptureTarget> plan = capturePlan(sessionWith(blocks), {}, {}, {}, {});

    ASSERT_EQ(plan.size(), 1);
    EXPECT_TRUE(plan.at(0).microphone.isEmpty());
    EXPECT_EQ(plan.at(0).microphoneLayout.size(), 2U);
}

TEST(SessionPlanTest, AnotherApplicationsMicrophoneIsNotOurs)
{
    // The match rule is the application's, so the capture stream has to be
    // that application's too: two calls in two programs are two calls.
    const QList<BlockInstance> blocks{sourceIncludingInput(u"b1"_s, {{"binary"_L1, u"chatty"_s}})};
    const pw::PwNode theirs = recordStream(u"Other"_s, u"other"_s);
    const pw::PwNode headset = source(u"alsa_input.headset"_s);
    QList<pw::PwPort> ports;
    givePorts(headset, {u"MONO"_s}, ports);

    const QList<CaptureTarget> plan =
        capturePlan(sessionWith(blocks), {theirs, headset}, ports, {recordsFrom(headset, theirs)}, {});
    ASSERT_EQ(plan.size(), 1);
    EXPECT_TRUE(plan.at(0).microphone.isEmpty());
}

TEST(SessionPlanTest, TheUrlsToOpenAreTheOnesSomebodyFilledIn)
{
    Session session = sessionWith({
        sourceBlock(u"b1"_s, {{"binary"_L1, u"chatty"_s}, {"openUrlOnStart"_L1, true}, {"url"_L1, u"https://a"_s}}),
        // The option off: not opened, whatever is typed in the field.
        sourceBlock(u"b2"_s, {{"binary"_L1, u"other"_s}, {"openUrlOnStart"_L1, false}, {"url"_L1, u"https://b"_s}}),
        // The option on and nothing typed: a block somebody has not
        // finished setting up, not a request to open an empty page.
        sourceBlock(u"b3"_s, {{"binary"_L1, u"third"_s}, {"openUrlOnStart"_L1, true}}),
        // The same URL again: one tab, not two.
        sourceBlock(u"b4"_s, {{"binary"_L1, u"fourth"_s}, {"openUrlOnStart"_L1, true}, {"url"_L1, u"https://a"_s}}),
    });

    EXPECT_EQ(urlsToOpen(session), QStringList{u"https://a"_s});
    EXPECT_TRUE(urlsToOpen(sessionWith({sourceBlock(u"b1"_s, {{"binary"_L1, u"chatty"_s}})})).isEmpty());
}

// --- devices that are virtual ---

namespace
{

[[nodiscard]] BlockInstance deviceBlock(const QString &id, std::string_view type, const QString &device)
{
    BlockInstance block;
    block.id = id;
    block.type = QString::fromUtf8(type.data(), static_cast<qsizetype>(type.size()));
    block.params = QJsonObject{{"device"_L1, device}};
    return block;
}

[[nodiscard]] pw::PwNode virtualMicrophone(const QString &name)
{
    pw::PwNode node = source(name);
    node.mediaClass = u"Audio/Source/Virtual"_s;
    return node;
}

} // namespace

TEST(DevicePlanTest, AnInputDeviceNamingAVirtualOutputRecordsItsMonitor)
{
    // Applications play into the sink; the block records what arrives there
    // from the sink's monitor ports, which are its outputs.
    const pw::PwNode everyApp = sink(u"virtual.everyapp"_s);
    QList<pw::PwPort> ports;
    givePorts(everyApp, {u"FL"_s, u"FR"_s}, ports, pw::PortDirection::Input);
    givePorts(everyApp, {u"FL"_s, u"FR"_s}, ports, pw::PortDirection::Output);

    const QList<CaptureTarget> plan =
        capturePlan(sessionWith({deviceBlock(u"in"_s, blocks::InputDevice::kTypeId, u"virtual.everyapp"_s)}),
                    {everyApp},
                    ports,
                    {},
                    {});

    ASSERT_EQ(plan.size(), 1);
    ASSERT_TRUE(plan.at(0).match.kind.has_value());
    EXPECT_EQ(*plan.at(0).match.kind, pw::NodeKind::Sink);
    EXPECT_EQ(plan.at(0).match.nodeName, u"virtual.everyapp"_s);
    EXPECT_EQ(plan.at(0).streamCount, 1) << "the virtual output was not found";
    EXPECT_EQ(plan.at(0).layout.size(), 2U);
}

TEST(DevicePlanTest, AnInputDeviceNamingNothingThereWaitsForASource)
{
    const QList<CaptureTarget> plan = capturePlan(
        sessionWith({deviceBlock(u"in"_s, blocks::InputDevice::kTypeId, u"not.plugged"_s)}), {}, {}, {}, {});

    ASSERT_EQ(plan.size(), 1);
    ASSERT_TRUE(plan.at(0).match.kind.has_value());
    EXPECT_EQ(*plan.at(0).match.kind, pw::NodeKind::Source);
    EXPECT_EQ(plan.at(0).streamCount, 0);
}

TEST(DevicePlanTest, AnOutputDeviceNamingAVirtualMicrophonePlaysIntoIt)
{
    // The ports that feed a virtual microphone are its inputs, the same
    // direction a sink is played into.
    const pw::PwNode microphone = virtualMicrophone(u"virtual.processed-mic"_s);
    QList<pw::PwPort> ports;
    givePorts(microphone, {u"MONO"_s}, ports, pw::PortDirection::Input);
    givePorts(microphone, {u"MONO"_s}, ports, pw::PortDirection::Output);

    const QList<OutputTarget> plan =
        outputPlan(sessionWith({deviceBlock(u"out"_s, blocks::OutputDevice::kTypeId, u"virtual.processed-mic"_s)}),
                   {microphone},
                   ports,
                   {});

    ASSERT_EQ(plan.size(), 1);
    ASSERT_TRUE(plan.at(0).match.kind.has_value());
    EXPECT_EQ(*plan.at(0).match.kind, pw::NodeKind::Source);
    EXPECT_TRUE(plan.at(0).devicePresent) << "the virtual microphone was not found";
    EXPECT_EQ(plan.at(0).layout.size(), 1U) << "the layout was not read from the ports that feed it";
}

TEST(DevicePlanTest, AnOutputDeviceNamingAMicrophoneWithNothingToPlayIntoIsNotThere)
{
    // A card's microphone has outputs only: there is nowhere to play to.
    const pw::PwNode card = source(u"alsa_input.usb"_s);
    QList<pw::PwPort> ports;
    givePorts(card, {u"FL"_s, u"FR"_s}, ports);

    const QList<OutputTarget> plan = outputPlan(
        sessionWith({deviceBlock(u"out"_s, blocks::OutputDevice::kTypeId, u"alsa_input.usb"_s)}), {card}, ports, {});

    ASSERT_EQ(plan.size(), 1);
    EXPECT_FALSE(plan.at(0).devicePresent);
    ASSERT_TRUE(plan.at(0).match.kind.has_value());
    EXPECT_EQ(*plan.at(0).match.kind, pw::NodeKind::Sink);
}

TEST(DevicePlanTest, OfTwoDevicesWithOneNameTheNewerIsMeant)
{
    // A virtual device being renamed is made again under the same name before
    // the old one goes; for that moment the newer one is the device.
    pw::PwNode older = virtualMicrophone(u"virtual.renamed"_s);
    pw::PwNode newer = virtualMicrophone(u"virtual.renamed"_s);
    ASSERT_GT(newer.object.serial, older.object.serial);
    QList<pw::PwPort> ports;
    givePorts(older, {u"MONO"_s}, ports, pw::PortDirection::Input);
    givePorts(newer, {u"FL"_s, u"FR"_s}, ports, pw::PortDirection::Input);

    const QList<OutputTarget> plan =
        outputPlan(sessionWith({deviceBlock(u"out"_s, blocks::OutputDevice::kTypeId, u"virtual.renamed"_s)}),
                   {older, newer},
                   ports,
                   {});

    ASSERT_EQ(plan.size(), 1);
    EXPECT_TRUE(plan.at(0).devicePresent);
    EXPECT_EQ(plan.at(0).layout.size(), 2U) << "the layout came from the device that is going away";
}

// ---------------------------------------------------------------------------
// What went or came back between two plans.

namespace
{

[[nodiscard]] QString typeOf(std::string_view id)
{
    return QString::fromUtf8(id.data(), static_cast<qsizetype>(id.size()));
}

[[nodiscard]] BlockInstance typedBlock(const QString &id, std::string_view type)
{
    BlockInstance block;
    block.id = id;
    block.type = typeOf(type);
    return block;
}

[[nodiscard]] CaptureTarget captured(const QString &blockId, const QString &nodeName, int streams)
{
    CaptureTarget target;
    target.blockId = blockId;
    target.match.nodeName = nodeName;
    target.streamCount = streams;
    return target;
}

[[nodiscard]] OutputTarget sentTo(const QString &blockId, const QString &device, bool present)
{
    OutputTarget target;
    target.blockId = blockId;
    target.deviceName = device;
    target.match.nodeName = device;
    target.devicePresent = present;
    return target;
}

} // namespace

TEST(PlanNoticesTest, AnInputDeviceThatGoesIsLostAndThatComesBackIsReturned)
{
    const Session session = sessionWith({typedBlock(u"mic"_s, blocks::InputDevice::kTypeId)});
    const SessionPlan there{{captured(u"mic"_s, u"usb-mic"_s, 1)}, {}};
    const SessionPlan gone{{captured(u"mic"_s, u"usb-mic"_s, 0)}, {}};

    EXPECT_EQ(planNotices(session, there, gone, {}), (QList<PlanNotice>{{NoticeCode::DeviceLost, u"mic"_s}}));
    EXPECT_EQ(planNotices(session, gone, there, {}), (QList<PlanNotice>{{NoticeCode::DeviceReturned, u"mic"_s}}));
    EXPECT_TRUE(planNotices(session, there, there, {}).isEmpty());
}

TEST(PlanNoticesTest, AnOutputDeviceThatGoesIsLostAndThatComesBackIsReturned)
{
    const Session session = sessionWith({typedBlock(u"out"_s, blocks::OutputDevice::kTypeId)});
    const SessionPlan there{{}, {sentTo(u"out"_s, u"headset"_s, true)}};
    const SessionPlan gone{{}, {sentTo(u"out"_s, u"headset"_s, false)}};

    EXPECT_EQ(planNotices(session, there, gone, {}), (QList<PlanNotice>{{NoticeCode::DeviceLost, u"out"_s}}));
    EXPECT_EQ(planNotices(session, gone, there, {}), (QList<PlanNotice>{{NoticeCode::DeviceReturned, u"out"_s}}));
}

TEST(PlanNoticesTest, AnApplicationComesBackOnlyAfterItWasLost)
{
    const Session session = sessionWith({typedBlock(u"app"_s, blocks::ApplicationSource::kTypeId)});
    const SessionPlan playing{{captured(u"app"_s, {}, 2)}, {}};
    const SessionPlan quiet{{captured(u"app"_s, {}, 0)}, {}};

    EXPECT_EQ(planNotices(session, playing, quiet, {}), (QList<PlanNotice>{{NoticeCode::CaptureLost, u"app"_s}}));
    // Seen for the first time since the session started: not news.
    EXPECT_TRUE(planNotices(session, quiet, playing, {}).isEmpty());
    EXPECT_EQ(planNotices(session, quiet, playing, {u"app"_s}),
              (QList<PlanNotice>{{NoticeCode::CaptureRegained, u"app"_s}}));
    // One stream more or less is not news either.
    const SessionPlan more{{captured(u"app"_s, {}, 3)}, {}};
    EXPECT_TRUE(planNotices(session, playing, more, {u"app"_s}).isEmpty());
}

TEST(PlanNoticesTest, AChoiceIsNotNews)
{
    // The person chose another device, or the default moved: the match
    // changed, and nothing "went".
    const Session session = sessionWith(
        {typedBlock(u"mic"_s, blocks::InputDevice::kTypeId), typedBlock(u"out"_s, blocks::OutputDevice::kTypeId)});
    const SessionPlan before{{captured(u"mic"_s, u"usb-mic"_s, 1)}, {sentTo(u"out"_s, u"headset"_s, true)}};
    const SessionPlan after{{captured(u"mic"_s, u"laptop-mic"_s, 0)}, {sentTo(u"out"_s, u"speakers"_s, false)}};
    EXPECT_TRUE(planNotices(session, before, after, {}).isEmpty());
    // A block that is new, or gone, is an edit.
    EXPECT_TRUE(planNotices(session, {}, before, {}).isEmpty());
    EXPECT_TRUE(planNotices(session, before, {}, {}).isEmpty());
}

TEST(PlanNoticesTest, SystemAudioFallingSilentIsNotNews)
{
    // Nothing playing is what a desktop mostly does.
    const Session session = sessionWith({typedBlock(u"all"_s, blocks::SystemAudio::kTypeId)});
    const SessionPlan playing{{captured(u"all"_s, {}, 3)}, {}};
    const SessionPlan quiet{{captured(u"all"_s, {}, 0)}, {}};
    EXPECT_TRUE(planNotices(session, playing, quiet, {}).isEmpty());
}

// **What a timer set to quit the captured applications asks to quit**:
// every stream of each application a source names, whatever it plays to,
// and nothing of anybody else's.
TEST(CapturePlanTest, TheCapturedApplicationsAreEveryStreamOfEachNamedApplication)
{
    pw::PwNode firefox = stream(u"Firefox"_s, u"firefox"_s);
    firefox.applicationPid = 4100;
    pw::PwNode firefoxAgain = stream(u"Firefox"_s, u"firefox"_s);
    firefoxAgain.applicationPid = 4100;
    pw::PwNode player = stream(u"Player"_s, u"player"_s);
    pw::PwNode recording = stream(u"Firefox"_s, u"firefox"_s);
    recording.kind = pw::NodeKind::RecordStream;
    const Session session = sessionWith({
        sourceBlock(u"b1"_s, QJsonObject{{"binary"_L1, u"firefox"_s}, {"outputMode"_L1, u"default"_s}}),
        sourceBlock(u"b2"_s, QJsonObject{{"binary"_L1, u"firefox"_s}}),
        sourceBlock(u"b3"_s, QJsonObject{}),
    });
    const QList<pw::PwNode> quit = capturedApplications(session, {firefox, firefoxAgain, player, recording});
    ASSERT_EQ(quit.size(), 2);
    EXPECT_EQ(quit.at(0).object.id, firefox.object.id);
    EXPECT_EQ(quit.at(1).object.id, firefoxAgain.object.id);
}
