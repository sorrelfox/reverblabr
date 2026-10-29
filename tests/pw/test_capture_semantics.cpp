// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// How capture and linking behave on PipeWire and WirePlumber: the evidence
// behind ADR 0007 (docs/adr/0007-capture-and-linking.md), one test per
// question of spike S1. The engine relies on each of these facts; if a
// PipeWire or WirePlumber update changes one, the test says which.
//
// Restarts, stream moves and clean-up after a crash are tested with the spike
// prototype, in spikes/s1-capture.

#include "capturenode.h"
#include "pwtest.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

using namespace rb::pwtest;

namespace
{

// Summing two identical signals adds 20 * log10(2) dB.
constexpr double kDoubledDb = 6.0206;
// What wpctl's cubic volume 0.5 does: 60 * log10(0.5) dB.
constexpr double kHalfVolumeDb = -18.0618;
constexpr double kSilenceDb = -200.0;

std::vector<std::string> channels(const std::vector<Graph::Port> &ports)
{
    std::vector<std::string> result;
    for (const Graph::Port &port : ports) {
        result.push_back(port.channel);
    }
    return result;
}

// The app's stream, once it plays to the given sink.
std::optional<Graph::Node> streamOn(const char *sinkName, const std::string &application = "TestApp")
{
    const auto stream = waitForNode(playbackStreamOf(application));
    if (!stream) {
        return std::nullopt;
    }
    const bool linked = waitUntil([&](const Graph &graph) {
        const Graph::Node *sink = graph.findNode(nodeNamed(sinkName));
        return sink && graph.linked(stream->id, sink->id);
    });
    return linked ? stream : std::nullopt;
}

// Links a node's ports into a capture node by channel and waits for the links.
bool captureFrom(std::uint32_t outputNode, const Graph::Node &capture)
{
    return linkByChannel(outputNode, capture.id) > 0 && waitForLink(outputNode, capture.id);
}

std::string pactl(const std::vector<std::string> &args)
{
    std::vector<std::string> argv{"pactl"};
    argv.insert(argv.end(), args.begin(), args.end());
    return runCommand(argv).output;
}

bool withPulse()
{
    return std::getenv("RB_PW_HARNESS_PULSE") == std::string("1");
}

} // namespace

class CaptureSemantics : public PipeWireTest
{
};

// Q1. Linking a stream into our node adds a second consumer; the app keeps
// playing to its sink, untouched.
TEST_F(CaptureSemantics, CapturingAStreamLeavesItPlayingToItsSink)
{
    CaptureNode capture;
    const auto node = capture.waitUntilReady();
    ASSERT_TRUE(node);
    FakeApp app;
    const auto stream = streamOn(kStereoSink);
    ASSERT_TRUE(stream);

    ASSERT_TRUE(captureFrom(stream->id, *node));

    const auto levels = capture.measureSettled();
    EXPECT_NEAR(levels.at("FL"), kTonePeakDb, 0.1);
    EXPECT_NEAR(levels.at("FR"), kTonePeakDb, 0.1);
    const Graph graph = Graph::capture();
    EXPECT_TRUE(graph.linked(stream->id, graph.findNode(nodeNamed(kStereoSink))->id));
    EXPECT_NEAR(*measurePeakDb(kStereoSink), kTonePeakDb, 0.1);
}

// Q3. The volume and mute a user sets on an app's stream are already applied
// at the stream's output ports: the engine must not apply them again.
TEST_F(CaptureSemantics, StreamVolumeAndMuteAreAppliedAtItsOutputPorts)
{
    CaptureNode capture;
    const auto node = capture.waitUntilReady();
    ASSERT_TRUE(node);
    FakeApp app;
    const auto stream = streamOn(kStereoSink);
    ASSERT_TRUE(stream);
    ASSERT_TRUE(captureFrom(stream->id, *node));

    setVolume(stream->id, 0.5);
    EXPECT_NEAR(capture.measureSettled().at("FL"), kTonePeakDb + kHalfVolumeDb, 0.1);
    setMute(stream->id, true);
    EXPECT_EQ(capture.measureSettled().at("FL"), kSilenceDb);
}

// Q3. A sink's own volume and mute are not applied to its monitor ports
// (monitor.channel-volumes defaults to false, as on ALSA sinks).
TEST_F(CaptureSemantics, SinkVolumeIsNotAppliedToItsMonitorPorts)
{
    CaptureNode capture;
    const auto node = capture.waitUntilReady();
    ASSERT_TRUE(node);
    FakeApp app;
    ASSERT_TRUE(streamOn(kStereoSink));
    const Graph graph = Graph::capture();
    const Graph::Node *sink = graph.findNode(nodeNamed(kStereoSink));
    ASSERT_TRUE(captureFrom(sink->id, *node)); // the sink's outputs are its monitor ports

    setVolume(sink->id, 0.5);
    setMute(sink->id, true);
    // Wait until the sink has applied both, then read its monitor.
    ASSERT_TRUE(waitUntil([&](const Graph &) {
        const auto info = runCommand({"wpctl", "get-volume", std::to_string(sink->id)}).output;
        return info.find("0.50") != std::string::npos && info.find("MUTED") != std::string::npos;
    }));
    const auto levels = capture.measureSettled();
    EXPECT_NEAR(levels.at("FL"), kTonePeakDb, 0.1);
    EXPECT_NEAR(levels.at("FR"), kTonePeakDb, 0.1);
}

// Q4. A stream is converted to the layout of the sink it plays to. A stereo
// app on a 5.1 sink has six ports; only FL and FR carry sound (no upmix).
TEST_F(CaptureSemantics, OnASurroundSinkAStereoStreamHasSixPortsAndOnlyTheFrontOnesCarrySound)
{
    CaptureNodeOptions options;
    options.channels = {"FL", "FR", "FC", "LFE", "RL", "RR"};
    CaptureNode capture(options);
    const auto node = capture.waitUntilReady();
    ASSERT_TRUE(node);
    FakeAppOptions appOptions;
    appOptions.target = kSurroundSink;
    FakeApp app(appOptions);
    const auto stream = streamOn(kSurroundSink);
    ASSERT_TRUE(stream);
    ASSERT_TRUE(waitForPorts(stream->id, "output", 6));

    ASSERT_TRUE(captureFrom(stream->id, *node));

    const auto levels = capture.measureSettled();
    EXPECT_NEAR(levels.at("FL"), kTonePeakDb, 0.1);
    EXPECT_NEAR(levels.at("FR"), kTonePeakDb, 0.1);
    for (const char *silent : {"FC", "LFE", "RL", "RR"}) {
        EXPECT_EQ(levels.at(silent), kSilenceDb) << silent;
    }
}

// Q4. On a mono sink the stream has one MONO port carrying the downmix, at
// the average of its channels (the tone is the same on both, so -12 dBFS).
TEST_F(CaptureSemantics, OnAMonoSinkAStereoStreamHasOneMonoPortWithTheDownmix)
{
    CaptureNodeOptions options;
    options.channels = {"MONO"};
    CaptureNode capture(options);
    const auto node = capture.waitUntilReady();
    ASSERT_TRUE(node);
    FakeAppOptions appOptions;
    appOptions.target = kMonoSink;
    FakeApp app(appOptions);
    const auto stream = streamOn(kMonoSink);
    ASSERT_TRUE(stream);
    ASSERT_TRUE(waitForPorts(stream->id, "output", 1));
    EXPECT_EQ(channels(Graph::capture().portsOf(stream->id, "output")), std::vector<std::string>{"MONO"});

    ASSERT_TRUE(captureFrom(stream->id, *node));

    EXPECT_NEAR(capture.measureSettled().at("MONO"), kTonePeakDb, 0.1);
}

// Q4. A sink whose channels have no positions (AUX) gets the stream unchanged:
// it stays FL and FR.
TEST_F(CaptureSemantics, OnAnAuxSinkAStereoStreamKeepsItsLayout)
{
    FakeAppOptions appOptions;
    appOptions.target = kAuxSink;
    FakeApp app(appOptions);
    const auto stream = streamOn(kAuxSink);
    ASSERT_TRUE(stream);
    ASSERT_TRUE(waitForPorts(stream->id, "output", 2));
    std::vector<std::string> layout = channels(Graph::capture().portsOf(stream->id, "output"));
    std::ranges::sort(layout);
    EXPECT_EQ(layout, (std::vector<std::string>{"FL", "FR"}));
}

// Q2. Moving a stream to a sink with another layout destroys its ports and
// creates new ones; port ids come back with other channels. Links to the old
// ports are gone, so capture must re-link on port appearance and never key a
// port by its id alone.
TEST_F(CaptureSemantics, MovingAStreamToAnotherLayoutRecreatesItsPorts)
{
    CaptureNode capture;
    const auto node = capture.waitUntilReady();
    ASSERT_TRUE(node);
    FakeApp app;
    const auto stream = streamOn(kStereoSink);
    ASSERT_TRUE(stream);
    ASSERT_TRUE(waitForPorts(stream->id, "output", 2));
    const std::vector<Graph::Port> before = Graph::capture().portsOf(stream->id, "output");
    ASSERT_TRUE(captureFrom(stream->id, *node));

    moveStream(stream->id, kSurroundSink);

    ASSERT_TRUE(waitForPorts(stream->id, "output", 6));
    const Graph after = Graph::capture();
    EXPECT_FALSE(after.linked(stream->id, node->id)) << "the links to the old ports should be gone";
    for (const Graph::Port &old : before) {
        const auto now = std::ranges::find(after.ports, old.id, &Graph::Port::id);
        if (now != after.ports.end()) {
            EXPECT_NE(now->prop("object.serial"), old.prop("object.serial"))
                << "port id " << old.id << " was reused by a new port";
        }
    }
}

// Q5. Several links into one input port are summed at unity gain. Links must
// therefore never be used to downmix: FL and FR into one port is +6 dB.
TEST_F(CaptureSemantics, LinksIntoOnePortAreSummedAtUnityGain)
{
    CaptureNode capture;
    const auto node = capture.waitUntilReady();
    ASSERT_TRUE(node);
    FakeApp app;
    const auto stream = streamOn(kStereoSink);
    ASSERT_TRUE(stream);
    ASSERT_TRUE(waitForPorts(stream->id, "output", 2));
    const Graph graph = Graph::capture();
    const auto outputs = graph.portsOf(stream->id, "output");
    const auto inputs = graph.portsOf(node->id, "input");

    ASSERT_TRUE(linkPorts(outputs[0].id, inputs[0].id));
    ASSERT_TRUE(linkPorts(outputs[1].id, inputs[0].id));

    EXPECT_TRUE(waitUntil(
        [&](const Graph &now) { return std::ranges::count(now.links, inputs[0].id, &Graph::Link::inputPort) == 2; }));
    EXPECT_NEAR(capture.measureSettled().at(inputs[0].channel), kTonePeakDb + kDoubledDb, 0.1);
}

// Q6. A KDE notification sound (KNotification through libcanberra and
// pipewire-pulse) is a playback stream with media.role "Notification" and an
// event.id.
TEST_F(CaptureSemantics, ANotificationSoundHasRoleNotificationAndAnEventId)
{
    if (!withPulse()) {
        GTEST_SKIP() << "needs pipewire-pulse";
    }
    BackgroundProcess notifier({RB_PW_HARNESS_DIR "/play-notification.py", toneFile(), "5"}, "notifier.log");
    const auto stream = waitForNode(playbackStreamOf("Test Notifier"), 5s);
    if (!stream && runCommand({"python3", "-c", "import ctypes; ctypes.CDLL('libcanberra.so.0')"}).status != 0) {
        GTEST_SKIP() << "libcanberra is not installed";
    }
    ASSERT_TRUE(stream) << "no stream from the notification sound";
    EXPECT_EQ(stream->prop("media.role"), "Notification");
    EXPECT_EQ(stream->prop("event.id"), "message-new-instant");
    EXPECT_EQ(stream->prop("application.id"), "org.example.notifier");
    notifier.kill();
}

// Q7. Loopbacks, filter chains, combined sinks and echo cancellers re-play audio they took
// from other nodes as playback streams of their own. Every such stream carries
// node.link-group; an application's own stream doesn't. Capturing every
// playback stream would count re-played audio twice.
TEST_F(CaptureSemantics, ReplayedStreamsCarryALinkGroupAndAppStreamsDont)
{
    FakeApp app;
    const auto appStream = streamOn(kStereoSink);
    ASSERT_TRUE(appStream);
    EXPECT_EQ(appStream->prop("node.link-group"), "");

    BackgroundProcess loopback({"pw-loopback",
                                "--capture-props=media.class=Audio/Sink node.name=rbtest.vsink",
                                "--playback-props=node.name=rbtest.vsink.replay"},
                               "loopback.log");
    BackgroundProcess filterChain({"pipewire", "-c", RB_PW_HARNESS_DIR "/filter-chain-copy.conf"}, "filter-chain.log");
    std::vector<std::string> replays{"rbtest.vsink.replay", "rbtest.fc.replay"};
    std::vector<std::string> modules;
    if (withPulse()) {
        modules.push_back(pactl({"load-module",
                                 "module-combine-sink",
                                 "sink_name=rbtest.combined",
                                 "slaves=" + std::string(kStereoSink) + ',' + kMonoSink}));
        replays.push_back(std::string("output.rbtest.combined_") + kStereoSink);
        replays.push_back(std::string("output.rbtest.combined_") + kMonoSink);
        modules.push_back(
            pactl({"load-module", "module-echo-cancel", "source_name=rbtest.ec.source", "sink_name=rbtest.ec.sink"}));
        replays.push_back("echo-cancel-playback");
    }

    for (const std::string &name : replays) {
        SCOPED_TRACE(name);
        const auto replay = waitForNode(nodeNamed(name));
        ASSERT_TRUE(replay);
        EXPECT_EQ(replay->prop("media.class"), "Stream/Output/Audio");
        EXPECT_NE(replay->prop("node.link-group"), "");
    }
    for (const std::string &module : modules) {
        (void)pactl({"unload-module", module.substr(0, module.find('\n'))});
    }
}

// Q8. PipeWire neither refuses nor breaks a feedback loop: a node that
// captures a sink's monitor and plays back into the same sink is linked both
// ways and stays that way, so the engine has to prevent loops itself. What
// the loop then does with the sound depends on the machine, and is measured
// rather than required: on the development machine it rang on at full level,
// while in a container it decayed to silence.
TEST_F(CaptureSemantics, AFeedbackLoopIsNotBrokenByPipeWire)
{
    auto app = std::make_unique<FakeApp>();
    ASSERT_TRUE(streamOn(kStereoSink));
    BackgroundProcess loop(
        {"pw-loopback",
         "--capture-props=stream.capture.sink=true target.object=" + std::string(kStereoSink)
             + " node.name=rbtest.loop.in",
         "--playback-props=target.object=" + std::string(kStereoSink) + " node.name=rbtest.loop.out"},
        "feedback-loop.log");
    ASSERT_TRUE(waitUntil([](const Graph &graph) {
        const Graph::Node *sink = graph.findNode(nodeNamed(kStereoSink));
        const Graph::Node *in = graph.findNode(nodeNamed("rbtest.loop.in"));
        const Graph::Node *out = graph.findNode(nodeNamed("rbtest.loop.out"));
        return sink && in && out && graph.linked(sink->id, in->id) && graph.linked(out->id, sink->id);
    }));
    const auto stream = waitForNode(playbackStreamOf("TestApp"));
    ASSERT_TRUE(stream);

    app.reset(); // the only real source stops
    ASSERT_TRUE(waitForNodeGone(*stream));

    // The cycle is still there: PipeWire removed neither link and reported no
    // error, so nothing but our own guard can prevent one.
    const Graph graph = Graph::capture();
    const Graph::Node *sink = graph.findNode(nodeNamed(kStereoSink));
    const Graph::Node *in = graph.findNode(nodeNamed("rbtest.loop.in"));
    const Graph::Node *out = graph.findNode(nodeNamed("rbtest.loop.out"));
    ASSERT_TRUE(sink && in && out);
    EXPECT_TRUE(graph.linked(sink->id, in->id)) << "the monitor link was removed";
    EXPECT_TRUE(graph.linked(out->id, sink->id)) << "the playback link was removed";

    const auto level = measurePeakDb(kStereoSink);
    ASSERT_TRUE(level);
    std::cout << "[measure] with the source stopped, the loop still carries " << *level << " dBFS\n";
}

// Q9. PulseAudio clients (the Plasma volume applet and its microphone
// indicator, pavucontrol) see no filter node at all, whatever its media.class
// and even while it captures from a source; they do see a capture stream.
TEST_F(CaptureSemantics, PulseClientsSeeCaptureStreamsButNoFilterNode)
{
    if (!withPulse()) {
        GTEST_SKIP() << "needs pipewire-pulse";
    }
    CaptureNodeOptions options;
    options.mediaClass = "Stream/Input/Audio";
    CaptureNode capture(options);
    const auto node = capture.waitUntilReady();
    ASSERT_TRUE(node);
    const Graph graph = Graph::capture();
    ASSERT_TRUE(captureFrom(graph.findNode(nodeNamed(kStereoSource))->id, *node));

    BackgroundProcess recorder({"pw-record", "-P", "{ application.name = \"RecorderApp\" }", "/dev/null"},
                               "recorder.log");
    ASSERT_TRUE(
        waitForNode([](const Graph::Node &candidate) { return candidate.prop("application.name") == "RecorderApp"; }));

    std::string outputs;
    ASSERT_TRUE(waitUntil([&](const Graph &) {
        outputs = pactl({"list", "source-outputs"});
        return outputs.find("RecorderApp") != std::string::npos;
    }));
    EXPECT_EQ(outputs.find(capture.options().name), std::string::npos);
    EXPECT_EQ(outputs.find("rbtest-capture"), std::string::npos);
}

// Q9. The policy never links a node that sets node.autoconnect = false, not
// even one that calls itself a capture stream.
TEST_F(CaptureSemantics, ThePolicyLeavesANonAutoconnectingNodeAlone)
{
    for (const char *mediaClass : {"", "Stream/Input/Audio"}) {
        SCOPED_TRACE(mediaClass);
        CaptureNodeOptions options;
        options.mediaClass = mediaClass;
        CaptureNode capture(options);
        const auto node = capture.waitUntilReady();
        ASSERT_TRUE(node);
        // A new stream makes the policy re-evaluate the graph.
        FakeApp app;
        ASSERT_TRUE(streamOn(kStereoSink));
        const Graph graph = Graph::capture();
        EXPECT_TRUE(std::ranges::none_of(graph.links, [&](const Graph::Link &link) {
            return link.inputNode == node->id || link.outputNode == node->id;
        }));
    }
}
