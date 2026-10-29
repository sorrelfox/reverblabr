// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Output Device block against a real PipeWire: which device the
// sound reaches, what happens when the default moves, and what happens when
// the device is unplugged. What the block does to the signal on the way is
// measured offline in tests/unit/blocks/test_outputdevice.cpp.
//
// The device these tests play to is a capture filter that calls itself a
// sink, so that what arrived can be measured sample for sample — a null sink
// would swallow it.

#include "blocks/applicationsource.h"
#include "blocks/builtinblocks.h"
#include "blocks/inputdevice.h"
#include "blocks/outputdevice.h"
#include "blocks/peakrmsmeter.h"
#include "core/editcommand.h"
#include "core/engine.h"
#include "core/sessionplan.h"
#include "core/sessionruntime.h"
#include "dsp/blockregistry.h"
#include "dsp/decibel.h"
#include "sessionharness.h"

#include <QElapsedTimer>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace rb;
using namespace rb::core;
using namespace rb::pwtest;
using namespace Qt::StringLiterals;

namespace
{

[[nodiscard]] dsp::BlockRegistry deviceRegistry()
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

/// An application source going straight into an output device: the smallest
/// session made of nothing but main blocks.
[[nodiscard]] Session deviceSession(const QString &binary, const QString &device, double volume = 1.0)
{
    Session session;
    session.id = QUuid::createUuid();
    session.name = u"Devices"_s;
    BlockInstance source = block(u"src"_s, blocks::ApplicationSource::kTypeId);
    source.params = QJsonObject{{"binary"_L1, binary}, {"outputMode"_L1, u"any"_s}};
    BlockInstance out = block(u"out"_s, blocks::OutputDevice::kTypeId);
    out.params = QJsonObject{{"device"_L1, device}, {"volume"_L1, volume}};
    session.blocks = {source, out};
    for (std::uint32_t channel = 0; channel < 2; ++channel) {
        Wire wire;
        wire.fromBlock = u"src"_s;
        wire.fromPort = channel;
        wire.toBlock = u"out"_s;
        wire.toPort = channel;
        session.wires.append(wire);
    }
    return session;
}

/// An input device going straight to an output device: what a person who
/// wants their microphone in a session builds.
[[nodiscard]] Session inputSession(const QString &device, const QString &sink)
{
    Session session;
    session.id = QUuid::createUuid();
    session.name = u"Input"_s;
    BlockInstance source = block(u"src"_s, blocks::InputDevice::kTypeId);
    source.params = QJsonObject{{"device"_L1, device}};
    BlockInstance out = block(u"out"_s, blocks::OutputDevice::kTypeId);
    out.params = QJsonObject{{"device"_L1, sink}};
    session.blocks = {source, out};
    for (std::uint32_t channel = 0; channel < 2; ++channel) {
        Wire wire;
        wire.fromBlock = u"src"_s;
        wire.fromPort = channel;
        wire.toBlock = u"out"_s;
        wire.toPort = channel;
        session.wires.append(wire);
    }
    return session;
}

/// A capture filter that presents itself as a sink, so that a test can hear
/// what an output block sent.
[[nodiscard]] CaptureNodeOptions sinkOptions(const std::string &name, std::vector<std::string> channels)
{
    CaptureNodeOptions options;
    options.name = name;
    options.channels = std::move(channels);
    options.mediaClass = "Audio/Sink";
    options.sineHz = 1000.0;
    return options;
}

/// What a block's meter says about itself.
[[nodiscard]] float published(const SessionRuntime &runtime, const QString &blockId, std::size_t value)
{
    for (const SessionRuntime::MeteredBlock &metered : runtime.meters()) {
        if (metered.id == blockId && metered.meter != nullptr) {
            return metered.meter->value(value);
        }
    }
    return -1.0f;
}

/// A null device made for one test and taken away after it, which is what a
/// virtual device made by hand looks like to the graph.
class NullDevice
{
public:
    /// \a mediaClass empty makes a sink; `Audio/Source/Virtual` makes a
    /// virtual microphone.
    explicit NullDevice(const std::string &name, const std::string &mediaClass = {})
    {
        std::vector<std::string> argv{
            "pactl", "load-module", "module-null-sink", "sink_name=" + name, "channel_map=front-left,front-right"};
        if (!mediaClass.empty()) {
            argv.push_back("media.class=" + mediaClass);
        }
        m_module = std::atoi(runCommand(argv).output.c_str());
    }
    ~NullDevice()
    {
        if (m_module > 0) {
            (void)runCommand({"pactl", "unload-module", std::to_string(m_module)});
        }
    }
    NullDevice(const NullDevice &) = delete;
    NullDevice &operator=(const NullDevice &) = delete;

    [[nodiscard]] bool loaded() const
    {
        return m_module > 0;
    }

private:
    int m_module = 0;
};

[[nodiscard]] QString asJson(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

} // namespace

using Devices = SessionTest;

TEST_F(Devices, TheSessionIsHeardOnTheDeviceItWasPointedAt)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilReady());
    const dsp::BlockRegistry registry = deviceRegistry();
    SessionRuntime runtime(engine.connection(), engine.mirror(), registry);

    CaptureNode device(sinkOptions("rbtest.device.stereo", {"FL", "FR"}));
    ASSERT_TRUE(device.waitUntilReady().has_value());

    ASSERT_TRUE(runtime.start(deviceSession(u"deviceapp"_s, u"rbtest.device.stereo"_s)).has_value());
    const std::optional<Graph::Node> ours = engine.awaitNode(runtime.node()->nodeName());
    ASSERT_TRUE(ours.has_value());

    FakeApp app(appOptions("DeviceApp", "deviceapp"));
    ASSERT_TRUE(engine.awaitMatch(playbackStreamOf("DeviceApp")).has_value());
    ASSERT_TRUE(engine.pumpUntil([&] {
        const std::optional<pw::PwNode> sink = engine.mirror().nodeNamed(u"rbtest.device.stereo"_s);
        return sink && engine.mirror().isLinked(ours->id, sink->object.id);
    })) << "the session was never linked to the device it names";
    EXPECT_FLOAT_EQ(published(runtime, u"out"_s, blocks::OutputDevice::kDevicePresentValue), 1.0f);

    engine.pump(std::chrono::milliseconds{500});
    const std::map<std::string, SineCheck> heard = device.measureSine(300ms);
    ASSERT_TRUE(heard.contains("FL"));
    EXPECT_NEAR(heard.at("FL").peakDb, kTonePeakDb, 0.5) << "the tone did not arrive at the level it was played at";
    EXPECT_LT(heard.at("FL").residualDb, -55.0);

    runtime.stop();
}

TEST_F(Devices, HalfTheVolumeIsSixDecibelsDownOnTheDevice)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilReady());
    const dsp::BlockRegistry registry = deviceRegistry();
    SessionRuntime runtime(engine.connection(), engine.mirror(), registry);

    CaptureNode device(sinkOptions("rbtest.device.half", {"FL", "FR"}));
    ASSERT_TRUE(device.waitUntilReady().has_value());
    ASSERT_TRUE(runtime.start(deviceSession(u"halfapp"_s, u"rbtest.device.half"_s, 0.5)).has_value());
    const std::optional<Graph::Node> ours = engine.awaitNode(runtime.node()->nodeName());
    ASSERT_TRUE(ours.has_value());

    FakeApp app(appOptions("HalfApp", "halfapp"));
    ASSERT_TRUE(engine.awaitMatch(playbackStreamOf("HalfApp")).has_value());
    ASSERT_TRUE(engine.pumpUntil([&] {
        const std::optional<pw::PwNode> sink = engine.mirror().nodeNamed(u"rbtest.device.half"_s);
        return sink && engine.mirror().isLinked(ours->id, sink->object.id);
    }));
    engine.pump(std::chrono::milliseconds{600});

    const std::map<std::string, SineCheck> heard = device.measureSine(400ms);
    ASSERT_TRUE(heard.contains("FL"));
    EXPECT_NEAR(heard.at("FL").peakDb, kTonePeakDb - 6.02, 0.05) << "half the volume was not half the signal";

    runtime.stop();
}

TEST_F(Devices, FollowingTheDefaultMovesWithinTheQuarterSecondAndLeavesNothingBehind)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilReady());
    const dsp::BlockRegistry registry = deviceRegistry();
    SessionRuntime runtime(engine.connection(), engine.mirror(), registry);

    const std::optional<pw::PwNode> stereo = engine.mirror().nodeNamed(QString::fromLatin1(kStereoSink));
    const std::optional<pw::PwNode> mono = engine.mirror().nodeNamed(QString::fromLatin1(kMonoSink));
    ASSERT_TRUE(stereo.has_value() && mono.has_value());

    // The block follows the default output, which is where the harness's
    // stereo sink starts.
    ASSERT_TRUE(runtime.start(deviceSession(u"followapp"_s, u"default"_s)).has_value());
    const std::optional<Graph::Node> ours = engine.awaitNode(runtime.node()->nodeName());
    ASSERT_TRUE(ours.has_value());
    const std::uint32_t ourId = ours->id;
    ASSERT_TRUE(engine.pumpUntil([&] { return engine.mirror().isLinked(ourId, stereo->object.id); }))
        << "the session never reached the default output";

    QElapsedTimer sinceDefaultMoved;
    bool moved = false;
    QObject::connect(
        &engine.mirror(), &pw::RegistryMirror::defaultsChanged, &runtime, [&](const pw::PwDefaults &defaults) {
            if (!moved && defaults.audioSink == QString::fromLatin1(kMonoSink)) {
                sinceDefaultMoved.start();
                moved = true;
            }
        });

    (void)runCommand({"wpctl", "set-default", std::to_string(mono->object.id)});
    ASSERT_TRUE(engine.pumpUntil([&] { return moved; })) << "the default never moved";
    ASSERT_TRUE(engine.pumpUntil([&] { return engine.mirror().isLinked(ourId, mono->object.id); }))
        << "the session did not follow the default output";
    EXPECT_LE(sinceDefaultMoved.elapsed(), 250) << "following took " << sinceDefaultMoved.elapsed() << " ms";

    // **Nothing left behind.** A link to the device that is no longer the
    // default would go on playing there, which is the one thing a person
    // switching devices does not want.
    EXPECT_FALSE(engine.mirror().isLinked(ourId, stereo->object.id)) << "the old output is still being played to";
    // The mono sink takes one channel, so the node publishes one port.
    ASSERT_TRUE(engine.awaitPorts(ourId, "output", 1));

    (void)runCommand({"wpctl", "set-default", std::to_string(stereo->object.id)});
    ASSERT_TRUE(engine.pumpUntil([&] { return engine.mirror().isLinked(ourId, stereo->object.id); }));
    runtime.stop();
}

TEST_F(Devices, AStereoSessionOnASurroundDeviceUsesTheFrontChannelsOnly)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilReady());
    const dsp::BlockRegistry registry = deviceRegistry();
    SessionRuntime runtime(engine.connection(), engine.mirror(), registry);

    CaptureNode device(sinkOptions("rbtest.device.surround", {"FL", "FR", "FC", "LFE", "RL", "RR"}));
    ASSERT_TRUE(device.waitUntilReady().has_value());
    ASSERT_TRUE(runtime.start(deviceSession(u"surrounddev"_s, u"rbtest.device.surround"_s)).has_value());
    const std::optional<Graph::Node> ours = engine.awaitNode(runtime.node()->nodeName());
    ASSERT_TRUE(ours.has_value());

    // Six ports, one per channel of the device, named after the channels.
    ASSERT_TRUE(engine.awaitPorts(ours->id, "output", 6));
    FakeApp app(appOptions("SurroundDev", "surrounddev"));
    ASSERT_TRUE(engine.awaitMatch(playbackStreamOf("SurroundDev")).has_value());
    ASSERT_TRUE(engine.pumpUntil([&] {
        const std::optional<pw::PwNode> sink = engine.mirror().nodeNamed(u"rbtest.device.surround"_s);
        return sink && engine.mirror().isLinked(ours->id, sink->object.id);
    }));
    engine.pump(std::chrono::milliseconds{600});

    const std::map<std::string, SineCheck> heard = device.measureSine(400ms);
    ASSERT_TRUE(heard.contains("FL"));
    EXPECT_NEAR(heard.at("FL").peakDb, kTonePeakDb, 0.5);
    EXPECT_NEAR(heard.at("FR").peakDb, kTonePeakDb, 0.5);
    // **Nothing is invented**: a stereo session has nothing to say about a
    // centre or a surround channel.
    for (const char *const silent : {"FC", "LFE", "RL", "RR"}) {
        ASSERT_TRUE(heard.contains(silent));
        EXPECT_LT(heard.at(silent).peakDb, -80.0) << silent << " was given something to play";
    }

    runtime.stop();
}

TEST_F(Devices, ADeviceThatGoesAwayComesBackToTheSameSession)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilReady());
    const dsp::BlockRegistry registry = deviceRegistry();
    SessionRuntime runtime(engine.connection(), engine.mirror(), registry);

    // A null sink, because this test is about the device being destroyed and
    // made again — which is what unplugging one looks like from here.
    const int module = std::atoi(runCommand({"pactl",
                                             "load-module",
                                             "module-null-sink",
                                             "sink_name=rbtest.device.plug",
                                             "channel_map=front-left,front-right"})
                                     .output.c_str());
    ASSERT_GT(module, 0);
    ASSERT_TRUE(engine.pumpUntil([&] { return engine.mirror().nodeNamed(u"rbtest.device.plug"_s).has_value(); }));

    // What a person is told: that it went, by name, and that it is
    // back. Only while the session runs; starting it is not news.
    QList<QStringList> notices;
    QObject::connect(&runtime,
                     &SessionRuntime::noticed,
                     &runtime,
                     [&](const QString &code, const QString &blockId, const QString &subject) {
                         notices.append({code, blockId, subject});
                     });

    ASSERT_TRUE(runtime.start(deviceSession(u"plugapp"_s, u"rbtest.device.plug"_s)).has_value());
    const std::optional<Graph::Node> ours = engine.awaitNode(runtime.node()->nodeName());
    ASSERT_TRUE(ours.has_value());
    const std::uint32_t ourId = ours->id;
    ASSERT_TRUE(engine.pumpUntil([&] {
        const std::optional<pw::PwNode> sink = engine.mirror().nodeNamed(u"rbtest.device.plug"_s);
        return sink && engine.mirror().isLinked(ourId, sink->object.id);
    }));
    EXPECT_FLOAT_EQ(published(runtime, u"out"_s, blocks::OutputDevice::kDevicePresentValue), 1.0f);
    const QString description = engine.mirror().nodeNamed(u"rbtest.device.plug"_s)->description;
    EXPECT_TRUE(notices.isEmpty()) << "a device that was there from the start is not news";

    // Unplugged. The session goes on running and says the device is not
    // there; it does not stop and it does not choose another one.
    (void)runCommand({"pactl", "unload-module", std::to_string(module)});
    ASSERT_TRUE(engine.pumpUntil([&] { return !engine.mirror().nodeNamed(u"rbtest.device.plug"_s).has_value(); }));
    ASSERT_TRUE(engine.pumpUntil([&] {
        return published(runtime, u"out"_s, blocks::OutputDevice::kDevicePresentValue) == 0.0f;
    })) << "the block went on saying its device was there";
    EXPECT_TRUE(runtime.isRunning()) << "losing a device stopped the session";
    ASSERT_TRUE(engine.pumpUntil([&] { return !notices.isEmpty(); })) << "nobody was told the device went";
    EXPECT_EQ(notices.at(0),
              (QStringList{u"device.lost"_s, u"out"_s, description.isEmpty() ? u"rbtest.device.plug"_s : description}));

    // Plugged in again, under the same name, and the session finds it.
    const int again = std::atoi(runCommand({"pactl",
                                            "load-module",
                                            "module-null-sink",
                                            "sink_name=rbtest.device.plug",
                                            "channel_map=front-left,front-right"})
                                    .output.c_str());
    ASSERT_GT(again, 0);
    ASSERT_TRUE(engine.pumpUntil([&] {
        const std::optional<pw::PwNode> sink = engine.mirror().nodeNamed(u"rbtest.device.plug"_s);
        return sink && engine.mirror().isLinked(ourId, sink->object.id);
    })) << "the device came back and the session did not";
    EXPECT_FLOAT_EQ(published(runtime, u"out"_s, blocks::OutputDevice::kDevicePresentValue), 1.0f);
    ASSERT_TRUE(engine.pumpUntil([&] { return notices.size() >= 2; })) << "nobody was told the device was back";
    EXPECT_EQ(notices.at(1).at(0), u"device.returned"_s);
    EXPECT_EQ(notices.size(), 2) << "one change is one notice";

    runtime.stop();
    (void)runCommand({"pactl", "unload-module", std::to_string(again)});
}

TEST_F(Devices, TwoOutputBlocksPlayToTwoDevicesAtOnce)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilReady());
    const dsp::BlockRegistry registry = deviceRegistry();
    SessionRuntime runtime(engine.connection(), engine.mirror(), registry);

    CaptureNode first(sinkOptions("rbtest.device.one", {"FL", "FR"}));
    CaptureNode second(sinkOptions("rbtest.device.two", {"FL", "FR"}));
    ASSERT_TRUE(first.waitUntilReady().has_value());
    ASSERT_TRUE(second.waitUntilReady().has_value());

    // One source into two outputs, at different volumes, which is how a
    // person sends the same thing to the speakers and to a recorder.
    Session session = deviceSession(u"twoapp"_s, u"rbtest.device.one"_s);
    BlockInstance other = block(u"out2"_s, blocks::OutputDevice::kTypeId);
    other.params = QJsonObject{{"device"_L1, u"rbtest.device.two"_s}, {"volume"_L1, 0.5}};
    session.blocks.append(other);
    for (std::uint32_t channel = 0; channel < 2; ++channel) {
        Wire wire;
        wire.fromBlock = u"src"_s;
        wire.fromPort = channel;
        wire.toBlock = u"out2"_s;
        wire.toPort = channel;
        session.wires.append(wire);
    }
    ASSERT_TRUE(runtime.start(session).has_value());

    const std::optional<Graph::Node> ours = engine.awaitNode(runtime.node()->nodeName());
    ASSERT_TRUE(ours.has_value());
    FakeApp app(appOptions("TwoApp", "twoapp"));
    ASSERT_TRUE(engine.awaitMatch(playbackStreamOf("TwoApp")).has_value());
    ASSERT_TRUE(engine.pumpUntil([&] {
        const std::optional<pw::PwNode> one = engine.mirror().nodeNamed(u"rbtest.device.one"_s);
        const std::optional<pw::PwNode> two = engine.mirror().nodeNamed(u"rbtest.device.two"_s);
        return one && two && engine.mirror().isLinked(ours->id, one->object.id)
            && engine.mirror().isLinked(ours->id, two->object.id);
    })) << "both devices should be linked";
    engine.pump(std::chrono::milliseconds{600});

    const std::map<std::string, SineCheck> heardOne = first.measureSine(400ms);
    const std::map<std::string, SineCheck> heardTwo = second.measureSine(400ms);
    ASSERT_TRUE(heardOne.contains("FL") && heardTwo.contains("FL"));
    EXPECT_NEAR(heardOne.at("FL").peakDb, kTonePeakDb, 0.5);
    EXPECT_NEAR(heardTwo.at("FL").peakDb, kTonePeakDb - 6.02, 0.1) << "the second device has its own volume";

    runtime.stop();
}

TEST_F(Devices, AToneThroughAnInputDeviceReachesTheSession)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilReady());
    const dsp::BlockRegistry registry = deviceRegistry();
    SessionRuntime runtime(engine.connection(), engine.mirror(), registry);

    CaptureNode device(sinkOptions("rbtest.device.input", {"FL", "FR"}));
    ASSERT_TRUE(device.waitUntilReady().has_value());

    // **A virtual source is a null sink with another face on it**: what is
    // played into its sink side comes out of its capture side, which is how
    // a test gets a tone out of a microphone that does not exist.
    FakeAppOptions options = appOptions("InputApp", "inputapp");
    options.target = kStereoSource;
    FakeApp app(options);
    const std::optional<Graph::Node> stream = engine.awaitMatch(playbackStreamOf("InputApp"));
    ASSERT_TRUE(stream.has_value());
    const std::optional<Graph::Node> virtualSource = engine.awaitNode(QString::fromLatin1(kStereoSource));
    ASSERT_TRUE(virtualSource.has_value());
    // **The policy will not route a playback stream into a source**, whatever
    // it is really made of, so the test links it by hand. That is what
    // feeding a virtual microphone looks like from the outside, and it is
    // the harness's business rather than the engine's.
    ASSERT_EQ(linkByChannel(stream->id, virtualSource->id), 2U) << "the tone never reached the virtual source";

    ASSERT_TRUE(runtime.start(inputSession(QString::fromLatin1(kStereoSource), u"rbtest.device.input"_s)).has_value());
    const std::optional<Graph::Node> ours = engine.awaitNode(runtime.node()->nodeName());
    ASSERT_TRUE(ours.has_value());
    ASSERT_TRUE(engine.pumpUntil([&] {
        const std::optional<pw::PwNode> source = engine.mirror().nodeNamed(QString::fromLatin1(kStereoSource));
        return source && engine.mirror().isLinked(source->object.id, ours->id);
    })) << "the input device was never linked to the session; present "
        << published(runtime, u"src"_s, blocks::InputDevice::kDevicePresentValue) << ", our input ports "
        << Graph::capture().portsOf(ours->id, "input").size() << ", source ports "
        << (engine.mirror().nodeNamed(QString::fromLatin1(kStereoSource))
                ? engine.mirror()
                      .portsOf(engine.mirror().nodeNamed(QString::fromLatin1(kStereoSource))->object.id,
                               pw::PortDirection::Output)
                      .size()
                : -1);
    EXPECT_FLOAT_EQ(published(runtime, u"src"_s, blocks::InputDevice::kDevicePresentValue), 1.0f);

    engine.pump(std::chrono::milliseconds{600});
    const std::map<std::string, SineCheck> heard = device.measureSine(400ms);
    ASSERT_TRUE(heard.contains("FL"));
    EXPECT_NEAR(heard.at("FL").peakDb, kTonePeakDb, 0.5) << "the tone did not come through the device";
    EXPECT_LT(heard.at("FL").residualDb, -55.0);

    runtime.stop();
}

TEST_F(Devices, AMonoInputIsHeardOnBothSidesAtItsOwnLevel)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilReady());
    const dsp::BlockRegistry registry = deviceRegistry();
    SessionRuntime runtime(engine.connection(), engine.mirror(), registry);

    CaptureNode device(sinkOptions("rbtest.device.mono", {"FL", "FR"}));
    ASSERT_TRUE(device.waitUntilReady().has_value());

    FakeAppOptions options = appOptions("MonoApp", "monoapp");
    options.target.clear();
    FakeApp app(options);
    const std::optional<Graph::Node> stream = engine.awaitMatch(playbackStreamOf("MonoApp"));
    ASSERT_TRUE(stream.has_value());
    const std::optional<Graph::Node> virtualSource = engine.awaitNode(QString::fromLatin1(kMonoSource));
    ASSERT_TRUE(virtualSource.has_value());
    // One port, because the device is mono: the left side of the tone is
    // what it records.
    ASSERT_TRUE(engine.awaitPorts(stream->id, "output", 2));
    const Graph graph = Graph::capture();
    const std::vector<Graph::Port> from = graph.portsOf(stream->id, "output");
    const std::vector<Graph::Port> into = graph.portsOf(virtualSource->id, "input");
    ASSERT_FALSE(from.empty() && into.empty());
    ASSERT_TRUE(linkPorts(from.front().id, into.front().id)) << "the tone never reached the virtual microphone";

    ASSERT_TRUE(runtime.start(inputSession(QString::fromLatin1(kMonoSource), u"rbtest.device.mono"_s)).has_value());
    const std::optional<Graph::Node> ours = engine.awaitNode(runtime.node()->nodeName());
    ASSERT_TRUE(ours.has_value());
    // One channel in, because that is what the device has.
    ASSERT_TRUE(engine.awaitPorts(ours->id, "input", 1));
    ASSERT_TRUE(engine.pumpUntil([&] {
        const std::optional<pw::PwNode> source = engine.mirror().nodeNamed(QString::fromLatin1(kMonoSource));
        return source && engine.mirror().isLinked(source->object.id, ours->id);
    }));

    engine.pump(std::chrono::milliseconds{700});
    const std::map<std::string, SineCheck> heard = device.measureSine(400ms);
    ASSERT_TRUE(heard.contains("FL") && heard.contains("FR"));
    // **At its own level on both sides**, not 6 dB above it: one port linked
    // into two would have PipeWire sum it.
    EXPECT_NEAR(heard.at("FL").peakDb, kTonePeakDb, 0.5);
    EXPECT_NEAR(heard.at("FR").peakDb, heard.at("FL").peakDb, 0.05);

    runtime.stop();
}

TEST_F(Devices, TheChannelsOfAFourChannelInputCanBeChosen)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilReady());
    const dsp::BlockRegistry registry = deviceRegistry();
    SessionRuntime runtime(engine.connection(), engine.mirror(), registry);

    // An interface with four inputs, of the kind the pickers exist for.
    const int module = std::atoi(runCommand({"pactl",
                                             "load-module",
                                             "module-null-sink",
                                             "media.class=Audio/Source/Virtual",
                                             "sink_name=rbtest.device.aux",
                                             "channel_map=aux0,aux1,aux2,aux3"})
                                     .output.c_str());
    ASSERT_GT(module, 0);
    ASSERT_TRUE(engine.pumpUntil([&] { return engine.mirror().nodeNamed(u"rbtest.device.aux"_s).has_value(); }));

    CaptureNode device(sinkOptions("rbtest.device.picked", {"FL", "FR"}));
    ASSERT_TRUE(device.waitUntilReady().has_value());

    Session session = inputSession(u"rbtest.device.aux"_s, u"rbtest.device.picked"_s);
    session.blocks[0].params.insert("leftChannel"_L1, u"AUX2"_s);
    session.blocks[0].params.insert("rightChannel"_L1, u"AUX3"_s);
    ASSERT_TRUE(runtime.start(session).has_value());

    const std::optional<Graph::Node> ours = engine.awaitNode(runtime.node()->nodeName());
    ASSERT_TRUE(ours.has_value());
    // Four ports, one per channel of the device: the picking is done inside
    // the block, not by leaving channels unlinked.
    ASSERT_TRUE(engine.awaitPorts(ours->id, "input", 4));
    const Graph graph = Graph::capture();
    std::vector<std::string> names;
    for (const Graph::Port &port : graph.portsOf(ours->id, "input")) {
        names.push_back(port.prop("port.name"));
    }
    for (const char *const wanted : {"src:in_AUX0", "src:in_AUX1", "src:in_AUX2", "src:in_AUX3"}) {
        EXPECT_NE(std::ranges::find(names, std::string(wanted)), names.end()) << wanted << " is missing";
    }
    ASSERT_TRUE(engine.pumpUntil([&] {
        const std::optional<pw::PwNode> source = engine.mirror().nodeNamed(u"rbtest.device.aux"_s);
        return source && engine.mirror().isLinked(source->object.id, ours->id);
    })) << "the four-channel input was never linked";

    runtime.stop();
    (void)runCommand({"pactl", "unload-module", std::to_string(module)});
}

TEST_F(Devices, AnInputThatGoesAwayComesBackWithin500ms)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilReady());
    const dsp::BlockRegistry registry = deviceRegistry();
    SessionRuntime runtime(engine.connection(), engine.mirror(), registry);

    const auto load = [&] {
        return std::atoi(runCommand({"pactl",
                                     "load-module",
                                     "module-null-sink",
                                     "media.class=Audio/Source/Virtual",
                                     "sink_name=rbtest.device.mic",
                                     "channel_map=front-left,front-right"})
                             .output.c_str());
    };
    const int module = load();
    ASSERT_GT(module, 0);
    ASSERT_TRUE(engine.pumpUntil([&] { return engine.mirror().nodeNamed(u"rbtest.device.mic"_s).has_value(); }));

    ASSERT_TRUE(runtime.start(inputSession(u"rbtest.device.mic"_s, QString::fromLatin1(kStereoSink))).has_value());
    const std::optional<Graph::Node> ours = engine.awaitNode(runtime.node()->nodeName());
    ASSERT_TRUE(ours.has_value());
    const std::uint32_t ourId = ours->id;
    ASSERT_TRUE(engine.pumpUntil([&] {
        const std::optional<pw::PwNode> mic = engine.mirror().nodeNamed(u"rbtest.device.mic"_s);
        return mic && engine.mirror().isLinked(mic->object.id, ourId);
    }));

    (void)runCommand({"pactl", "unload-module", std::to_string(module)});
    ASSERT_TRUE(engine.pumpUntil([&] { return !engine.mirror().nodeNamed(u"rbtest.device.mic"_s).has_value(); }));
    ASSERT_TRUE(engine.pumpUntil([&] {
        return published(runtime, u"src"_s, blocks::InputDevice::kDevicePresentValue) == 0.0f;
    })) << "the block went on saying its microphone was there";
    EXPECT_TRUE(runtime.isRunning());

    // Plugged in again under the same name. The clock starts when the
    // device's ports appear, which is the first moment anything could be
    // linked to them.
    QElapsedTimer sincePorts;
    bool arrived = false;
    QObject::connect(&engine.mirror(), &pw::RegistryMirror::portAdded, &runtime, [&](const pw::PwPort &port) {
        const std::optional<pw::PwNode> owner = engine.mirror().node(port.nodeId);
        if (!arrived && owner && owner->name == u"rbtest.device.mic"_s) {
            sincePorts.start();
            arrived = true;
        }
    });
    const int again = load();
    ASSERT_GT(again, 0);
    ASSERT_TRUE(engine.pumpUntil([&] { return arrived; }));
    ASSERT_TRUE(engine.pumpUntil([&] {
        const std::optional<pw::PwNode> mic = engine.mirror().nodeNamed(u"rbtest.device.mic"_s);
        return mic && engine.mirror().isLinked(mic->object.id, ourId);
    })) << "the microphone came back and the session did not";
    EXPECT_LE(sincePorts.elapsed(), 500) << "it took " << sincePorts.elapsed() << " ms to be linked again";

    runtime.stop();
    (void)runCommand({"pactl", "unload-module", std::to_string(again)});
}

TEST_F(Devices, FollowingTheDefaultInputMovesWithinTheQuarterSecond)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilReady());
    const dsp::BlockRegistry registry = deviceRegistry();
    SessionRuntime runtime(engine.connection(), engine.mirror(), registry);

    const std::optional<pw::PwNode> stereo = engine.mirror().nodeNamed(QString::fromLatin1(kStereoSource));
    const std::optional<pw::PwNode> mono = engine.mirror().nodeNamed(QString::fromLatin1(kMonoSource));
    ASSERT_TRUE(stereo.has_value() && mono.has_value());

    ASSERT_TRUE(runtime.start(inputSession(u"default"_s, QString::fromLatin1(kStereoSink))).has_value());
    const std::optional<Graph::Node> ours = engine.awaitNode(runtime.node()->nodeName());
    ASSERT_TRUE(ours.has_value());
    const std::uint32_t ourId = ours->id;
    ASSERT_TRUE(engine.pumpUntil([&] { return engine.mirror().isLinked(stereo->object.id, ourId); }))
        << "the session never reached the default input";

    QElapsedTimer sinceMoved;
    bool moved = false;
    QObject::connect(
        &engine.mirror(), &pw::RegistryMirror::defaultsChanged, &runtime, [&](const pw::PwDefaults &defaults) {
            if (!moved && defaults.audioSource == QString::fromLatin1(kMonoSource)) {
                sinceMoved.start();
                moved = true;
            }
        });

    (void)runCommand({"wpctl", "set-default", std::to_string(mono->object.id)});
    ASSERT_TRUE(engine.pumpUntil([&] { return moved; })) << "the default input never moved";
    ASSERT_TRUE(engine.pumpUntil([&] { return engine.mirror().isLinked(mono->object.id, ourId); }))
        << "the session did not follow the default input";
    EXPECT_LE(sinceMoved.elapsed(), 250) << "following took " << sinceMoved.elapsed() << " ms";
    EXPECT_FALSE(engine.mirror().isLinked(stereo->object.id, ourId)) << "the old input is still being recorded";

    (void)runCommand({"wpctl", "set-default", std::to_string(stereo->object.id)});
    ASSERT_TRUE(engine.pumpUntil([&] { return engine.mirror().isLinked(stereo->object.id, ourId); }));
    runtime.stop();
}

TEST_F(Devices, ASessionCanNeverRecordItself)
{
    // The ring the feedback guard exists for cannot even be asked for here:
    // an input names a source and our own nodes are in no match at all
    // ([ADR-0007]). A session pointed at itself hears nothing and says its
    // device is not there, rather than feeding its own output back in.
    Pump engine;
    ASSERT_TRUE(engine.waitUntilReady());
    const dsp::BlockRegistry registry = deviceRegistry();
    SessionRuntime runtime(engine.connection(), engine.mirror(), registry);

    ASSERT_TRUE(runtime.start(inputSession(u"default"_s, QString::fromLatin1(kStereoSink))).has_value());
    const std::optional<Graph::Node> ours = engine.awaitNode(runtime.node()->nodeName());
    ASSERT_TRUE(ours.has_value());
    const QString ourName = runtime.node()->nodeName();
    ASSERT_TRUE(engine.pumpUntil([&] { return engine.mirror().nodeNamed(ourName).has_value(); }));

    Session pointedAtItself = inputSession(ourName, QString::fromLatin1(kStereoSink));
    pointedAtItself.id = QUuid::createUuid();
    ASSERT_TRUE(runtime.update(pointedAtItself).has_value());
    engine.pump(std::chrono::milliseconds{500});

    EXPECT_FLOAT_EQ(published(runtime, u"src"_s, blocks::InputDevice::kDevicePresentValue), 0.0f)
        << "the session found itself as a recording device";
    EXPECT_FALSE(engine.mirror().isLinked(ours->id, ours->id)) << "the session is linked to itself";

    runtime.stop();
}

// --- virtual devices ---

TEST_F(Devices, WhatApplicationsPlayIntoAVirtualOutputReachesTheSession)
{
    // The first reason for a virtual output: every application sends its
    // sound there, a session records it from the sink's monitor, processes
    // it and plays it to the real card.
    Pump engine;
    ASSERT_TRUE(engine.waitUntilReady());
    const dsp::BlockRegistry registry = deviceRegistry();
    SessionRuntime runtime(engine.connection(), engine.mirror(), registry);

    const NullDevice everyApp("rbtest.virtual.everyapp");
    ASSERT_TRUE(everyApp.loaded());
    ASSERT_TRUE(engine.pumpUntil([&] { return engine.mirror().nodeNamed(u"rbtest.virtual.everyapp"_s).has_value(); }));
    CaptureNode card(sinkOptions("rbtest.device.card", {"FL", "FR"}));
    ASSERT_TRUE(card.waitUntilReady().has_value());

    FakeAppOptions options = appOptions("EveryApp", "everyapp");
    options.target = "rbtest.virtual.everyapp";
    FakeApp app(options);
    ASSERT_TRUE(engine.awaitMatch(playbackStreamOf("EveryApp")).has_value());

    ASSERT_TRUE(runtime.start(inputSession(u"rbtest.virtual.everyapp"_s, u"rbtest.device.card"_s)).has_value());
    const std::optional<Graph::Node> ours = engine.awaitNode(runtime.node()->nodeName());
    ASSERT_TRUE(ours.has_value());
    ASSERT_TRUE(engine.pumpUntil([&] {
        const std::optional<pw::PwNode> everyAppNode = engine.mirror().nodeNamed(u"rbtest.virtual.everyapp"_s);
        return everyAppNode && engine.mirror().isLinked(everyAppNode->object.id, ours->id);
    })) << "the session was never linked to the virtual output's monitor";
    EXPECT_FLOAT_EQ(published(runtime, u"src"_s, blocks::InputDevice::kDevicePresentValue), 1.0f);

    engine.pump(std::chrono::milliseconds{600});
    const std::map<std::string, SineCheck> heard = card.measureSine(300ms);
    ASSERT_TRUE(heard.contains("FL") && heard.contains("FR"));
    EXPECT_NEAR(heard.at("FL").peakDb, kTonePeakDb, 0.5) << "what the application played did not arrive at its level";
    EXPECT_NEAR(heard.at("FR").peakDb, kTonePeakDb, 0.5);
    EXPECT_LT(heard.at("FL").residualDb, -55.0);

    runtime.stop();
}

TEST_F(Devices, WhatASessionPlaysIntoAVirtualMicrophoneIsWhatAnApplicationRecords)
{
    // The second reason: a session processes a microphone and hands the
    // result to a call or a recorder, which records from a virtual
    // microphone as it would from a real one.
    Pump engine;
    ASSERT_TRUE(engine.waitUntilReady());
    const dsp::BlockRegistry registry = deviceRegistry();
    SessionRuntime runtime(engine.connection(), engine.mirror(), registry);

    const NullDevice microphone("rbtest.virtual.mic", "Audio/Source/Virtual");
    ASSERT_TRUE(microphone.loaded());
    ASSERT_TRUE(engine.pumpUntil([&] { return engine.mirror().nodeNamed(u"rbtest.virtual.mic"_s).has_value(); }));

    ASSERT_TRUE(runtime.start(deviceSession(u"micapp"_s, u"rbtest.virtual.mic"_s)).has_value());
    const std::optional<Graph::Node> ours = engine.awaitNode(runtime.node()->nodeName());
    ASSERT_TRUE(ours.has_value());
    FakeApp app(appOptions("MicApp", "micapp"));
    ASSERT_TRUE(engine.awaitMatch(playbackStreamOf("MicApp")).has_value());
    ASSERT_TRUE(engine.pumpUntil([&] {
        const std::optional<pw::PwNode> mic = engine.mirror().nodeNamed(u"rbtest.virtual.mic"_s);
        return mic && engine.mirror().isLinked(ours->id, mic->object.id);
    })) << "the session was never linked into the virtual microphone";
    EXPECT_FLOAT_EQ(published(runtime, u"out"_s, blocks::OutputDevice::kDevicePresentValue), 1.0f);

    engine.pump(std::chrono::milliseconds{300});
    const std::optional<double> recorded = measureRecordedPeakDb("rbtest.virtual.mic", 300ms);
    ASSERT_TRUE(recorded.has_value()) << "nothing could be recorded from the virtual microphone";
    EXPECT_NEAR(*recorded, kTonePeakDb, 0.5) << "the recording did not hear the session at its level";

    runtime.stop();
}

TEST_F(Devices, PlayingBackIntoTheVirtualOutputASessionRecordsIsRefusedOnce)
{
    // A virtual output and its monitor are one node, so a session recording
    // one and playing into the same one is a ring of two: it would run away
    // within a second. The link that closes it is refused, the person is told
    // once, and the rest of the session carries on.
    QTemporaryDir sessions;
    ASSERT_TRUE(sessions.isValid());
    const dsp::BlockRegistry registry = deviceRegistry();
    Engine facade(registry, SessionStore{sessions.path()});
    QSignalSpy opened(&facade, &EngineFacade::sessionOpened);
    QSignalSpy errors(&facade, &EngineFacade::error);
    facade.connectToPipeWire();
    Pump engine;
    ASSERT_TRUE(engine.waitUntilReady());
    ASSERT_TRUE(engine.pumpUntil([&] { return facade.isConnected(); }));

    const NullDevice loop("rbtest.virtual.loop");
    ASSERT_TRUE(loop.loaded());
    ASSERT_TRUE(engine.pumpUntil([&] { return engine.mirror().nodeNamed(u"rbtest.virtual.loop"_s).has_value(); }));

    facade.createSession(u"Loop"_s);
    ASSERT_EQ(opened.count(), 1);
    const QString id = opened.back().at(0).toString();
    for (const BlockInstance &made : inputSession(u"rbtest.virtual.loop"_s, u"rbtest.virtual.loop"_s).blocks) {
        AddBlock command;
        command.block = made;
        facade.applyEdit(id, asJson(editCommandToJson(command)));
    }
    for (const Wire &wire : inputSession(u"rbtest.virtual.loop"_s, u"rbtest.virtual.loop"_s).wires) {
        facade.applyEdit(id, asJson(editCommandToJson(Connect{wire, -1})));
    }
    ASSERT_TRUE(errors.isEmpty()) << errors.back().at(0).toString().toStdString();
    facade.start(id);

    const auto loopErrors = [&] {
        int count = 0;
        for (const QList<QVariant> &error : errors) {
            count += error.at(0).toString() == u"routing.feedbackLoop"_s ? 1 : 0;
        }
        return count;
    };
    ASSERT_TRUE(engine.pumpUntil([&] { return loopErrors() > 0; })) << "the ring was never reported";
    const QJsonObject args = QJsonDocument::fromJson(errors.back().at(1).toString().toUtf8()).object();
    EXPECT_FALSE(args.value(u"path"_s).toString().isEmpty()) << "the error does not say which way round";

    SessionRuntime *const runtime = facade.runtimeOf(id);
    ASSERT_NE(runtime, nullptr);
    const std::optional<pw::PwNode> ours = engine.mirror().nodeNamed(runtime->node()->nodeName());
    const std::optional<pw::PwNode> device = engine.mirror().nodeNamed(u"rbtest.virtual.loop"_s);
    ASSERT_TRUE(ours && device);
    EXPECT_FALSE(engine.mirror().isLinked(ours->object.id, device->object.id)
                 && engine.mirror().isLinked(device->object.id, ours->object.id))
        << "both halves of the ring were made";

    // The graph moving asks the question again; the answer is not news.
    FakeApp app(appOptions("LoopApp", "loopapp"));
    ASSERT_TRUE(engine.awaitMatch(playbackStreamOf("LoopApp")).has_value());
    engine.pump(std::chrono::milliseconds{500});
    EXPECT_EQ(loopErrors(), 1) << "the same ring was reported again";
    EXPECT_TRUE(runtime->isRunning()) << "a refused link stopped the session";

    facade.stop(id);
}

TEST_F(Devices, ARingSomebodyElseClosesAroundASessionIsBrokenAndReportedOnce)
{
    // A session records one virtual output and plays into another: no ring.
    // Then somebody else — a patchbay, a script — joins the second one's
    // monitor back into the first, and the sound can go round for ever
    // through our two links and theirs. Nothing but a link has changed: every
    // node is already running and no port comes or goes. The manager still
    // has to see it, let go of the one link of ours that closes the
    // ring, keep the other, and say so once. Their link is not ours to touch.
    Pump engine;
    ASSERT_TRUE(engine.waitUntilReady());
    const dsp::BlockRegistry registry = deviceRegistry();
    SessionRuntime runtime(engine.connection(), engine.mirror(), registry);
    QSignalSpy refused(&runtime, &SessionRuntime::feedbackRefused);

    const NullDevice from("rbtest.virtual.ringfrom");
    const NullDevice to("rbtest.virtual.ringto");
    ASSERT_TRUE(from.loaded() && to.loaded());
    ASSERT_TRUE(engine.pumpUntil([&] {
        return engine.mirror().nodeNamed(u"rbtest.virtual.ringfrom"_s).has_value()
            && engine.mirror().nodeNamed(u"rbtest.virtual.ringto"_s).has_value();
    }));
    FakeAppOptions options = appOptions("RingApp", "ringapp");
    options.target = "rbtest.virtual.ringfrom";
    FakeApp app(options);
    ASSERT_TRUE(engine.awaitMatch(playbackStreamOf("RingApp")).has_value());

    ASSERT_TRUE(runtime.start(inputSession(u"rbtest.virtual.ringfrom"_s, u"rbtest.virtual.ringto"_s)).has_value());
    const std::optional<Graph::Node> ours = engine.awaitNode(runtime.node()->nodeName());
    ASSERT_TRUE(ours.has_value());
    const std::optional<pw::PwNode> fromNode = engine.mirror().nodeNamed(u"rbtest.virtual.ringfrom"_s);
    const std::optional<pw::PwNode> toNode = engine.mirror().nodeNamed(u"rbtest.virtual.ringto"_s);
    ASSERT_TRUE(fromNode && toNode);
    const auto recording = [&] { return engine.mirror().isLinked(fromNode->object.id, ours->id); };
    const auto playing = [&] { return engine.mirror().isLinked(ours->id, toNode->object.id); };
    ASSERT_TRUE(engine.pumpUntil([&] { return recording() && playing(); })) << "the session never ran";
    // Everything starting sets moving — node states, the policy — has stopped
    // before the ring is closed, so only the link itself can be noticed.
    engine.pump(std::chrono::milliseconds{500});
    ASSERT_EQ(refused.count(), 0) << "an ordinary session was mistaken for a ring";

    std::vector<std::pair<std::uint32_t, std::uint32_t>> theirs;
    for (const pw::PwPort &out : engine.mirror().portsOf(toNode->object.id, pw::PortDirection::Output)) {
        for (const pw::PwPort &in : engine.mirror().portsOf(fromNode->object.id, pw::PortDirection::Input)) {
            if (out.channel == in.channel) {
                theirs.emplace_back(out.object.id, in.object.id);
            }
        }
    }
    ASSERT_EQ(theirs.size(), 2U);
    for (const auto &[out, in] : theirs) {
        ASSERT_TRUE(linkPorts(out, in));
    }

    QElapsedTimer timer;
    timer.start();
    EXPECT_TRUE(engine.pumpUntil([&] { return !(recording() && playing()); }, std::chrono::milliseconds{500}))
        << "the ring still stood after " << timer.elapsed() << " ms";
    EXPECT_TRUE(engine.pumpUntil([&] { return refused.count() > 0; }, std::chrono::seconds{1}))
        << "the ring was never reported";
    // Only the link that closes the ring goes: the session keeps recording or
    // keeps playing, and the other half is not taken with it.
    engine.pump(std::chrono::milliseconds{300});
    EXPECT_NE(recording(), playing()) << "recording " << recording() << ", playing " << playing();
    EXPECT_TRUE(engine.awaitGraph([&](const Graph &graph) {
        const auto joined = [&graph](std::uint32_t output, std::uint32_t input) {
            return std::ranges::any_of(graph.links, [&](const Graph::Link &link) {
                return link.outputNode == output && link.inputNode == input;
            });
        };
        return !(joined(fromNode->object.id, ours->id) && joined(ours->id, toNode->object.id));
    })) << "pw-dump still shows the ring whole";
    EXPECT_TRUE(engine.mirror().isLinked(toNode->object.id, fromNode->object.id))
        << "somebody else's link was taken away";

    // The graph moving asks again; the answer is not news.
    FakeApp other(appOptions("RingOther", "ringother"));
    ASSERT_TRUE(engine.awaitMatch(playbackStreamOf("RingOther")).has_value());
    engine.pump(std::chrono::milliseconds{500});
    QStringList reported;
    for (const QList<QVariant> &report : refused) {
        reported.append(report.at(0).toString());
    }
    EXPECT_EQ(refused.count(), 1) << "the same ring was reported again: " << reported.join(u" | "_s).toStdString();
    EXPECT_TRUE(runtime.isRunning()) << "a refused link stopped the session";

    for (const auto &[out, in] : theirs) {
        static_cast<void>(unlinkPorts(out, in));
    }
    runtime.stop();
}

TEST_F(Devices, AMeterInTheMiddleOfALaneSaysWhatIsPassingThroughIt)
{
    // The whole path in one test: a block publishes a number of its own, the
    // runtime gives it somewhere to publish it, and it is there to be read
    // while the session runs.
    Pump engine;
    ASSERT_TRUE(engine.waitUntilReady());
    const dsp::BlockRegistry registry = deviceRegistry();
    SessionRuntime runtime(engine.connection(), engine.mirror(), registry);

    CaptureNode device(sinkOptions("rbtest.device.meter", {"FL", "FR"}));
    ASSERT_TRUE(device.waitUntilReady().has_value());

    // Application → meter → device, which is where a person drops one in.
    Session session = deviceSession(u"meterapp"_s, u"rbtest.device.meter"_s);
    BlockInstance meter = block(u"meter"_s, blocks::PeakRmsMeter::kTypeId);
    session.blocks.insert(1, meter);
    session.wires.clear();
    for (std::uint32_t channel = 0; channel < 2; ++channel) {
        Wire toMeter;
        toMeter.fromBlock = u"src"_s;
        toMeter.fromPort = channel;
        toMeter.toBlock = u"meter"_s;
        toMeter.toPort = channel;
        session.wires.append(toMeter);
        Wire toOut;
        toOut.fromBlock = u"meter"_s;
        toOut.fromPort = channel;
        toOut.toBlock = u"out"_s;
        toOut.toPort = channel;
        session.wires.append(toOut);
    }
    ASSERT_TRUE(runtime.start(session).has_value());
    const std::optional<Graph::Node> ours = engine.awaitNode(runtime.node()->nodeName());
    ASSERT_TRUE(ours.has_value());

    FakeApp app(appOptions("MeterApp", "meterapp"));
    ASSERT_TRUE(engine.awaitMatch(playbackStreamOf("MeterApp")).has_value());
    ASSERT_TRUE(engine.pumpUntil([&] {
        return published(runtime, u"meter"_s, blocks::PeakRmsMeter::kRmsLeftValue) > 0.0f;
    })) << "the meter never published a level";
    engine.pump(std::chrono::milliseconds{700});

    // The tone is −12 dBFS at its peak, so its root mean square is 3.01 dB
    // below that.
    const float left = published(runtime, u"meter"_s, blocks::PeakRmsMeter::kRmsLeftValue);
    EXPECT_NEAR(dsp::gainToDb(left), kTonePeakDb - 3.01, 0.5) << "the meter read " << dsp::gainToDb(left) << " dBFS";
    // And the audio went through it untouched.
    const std::map<std::string, SineCheck> heard = device.measureSine(300ms);
    ASSERT_TRUE(heard.contains("FL"));
    EXPECT_NEAR(heard.at("FL").peakDb, kTonePeakDb, 0.5);

    runtime.stop();
}
