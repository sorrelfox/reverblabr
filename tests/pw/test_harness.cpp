// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Tests of the test harness itself: the private instance, its test devices,
// fake applications, stream moves, level measurement and isolation from the
// real session. They double as examples for engine integration tests.

#include "pwtest.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>
#include <vector>

using namespace rb::pwtest;

namespace
{

std::vector<std::string> channels(const std::vector<Graph::Port> &ports)
{
    std::vector<std::string> result;
    for (const Graph::Port &port : ports) {
        result.push_back(port.channel);
    }
    return result;
}

} // namespace

class Harness : public PipeWireTest
{
};

TEST_F(Harness, TestAppAppearsAsAPlaybackStream)
{
    FakeAppOptions options;
    options.applicationName = "TestApp";
    options.binary = "testapp";
    options.mediaRole = "Music";
    FakeApp app(options);

    const auto stream = waitForNode(playbackStreamOf("TestApp"));
    ASSERT_TRUE(stream) << "no playback stream from TestApp appeared";
    EXPECT_EQ(stream->prop("media.class"), "Stream/Output/Audio");
    EXPECT_EQ(stream->prop("application.name"), "TestApp");
    EXPECT_EQ(stream->prop("application.process.binary"), "testapp");
    EXPECT_EQ(stream->prop("media.role"), "Music");

    ASSERT_TRUE(waitForPorts(stream->id, "output", 2));
    const Graph graph = Graph::capture();
    EXPECT_EQ(channels(graph.portsOf(stream->id, "output")), (std::vector<std::string>{"FL", "FR"}));

    // The policy links a new stream to the default sink, and the tone arrives there.
    const Graph::Node *sink = graph.findNode(nodeNamed(kStereoSink));
    ASSERT_NE(sink, nullptr);
    EXPECT_TRUE(waitForLink(stream->id, sink->id)) << "the stream isn't linked to the default sink";
    const auto peak = measurePeakDb(kStereoSink);
    ASSERT_TRUE(peak) << "no audio could be recorded from the sink's monitor";
    EXPECT_NEAR(*peak, kTonePeakDb, 0.5);
}

TEST_F(Harness, PulseClientsAppearToo)
{
    if (std::getenv("RB_PW_HARNESS_PULSE") != std::string("1")) {
        GTEST_SKIP() << "the harness runs without pipewire-pulse (--no-pulse)";
    }
    FakeAppOptions options;
    options.api = FakeAppOptions::Api::Pulse;
    options.applicationName = "PulseApp";
    options.binary = "pulseapp";
    FakeApp app(options);

    const auto stream = waitForNode(playbackStreamOf("PulseApp"));
    ASSERT_TRUE(stream) << "no playback stream from PulseApp appeared";
    EXPECT_EQ(stream->prop("application.process.binary"), "pulseapp");
    EXPECT_EQ(stream->prop("media.role"), "Music");
    EXPECT_EQ(stream->prop("application.process.id"), std::to_string(app.pid()));
}

TEST_F(Harness, TestDevicesHaveTheirChannelLayouts)
{
    struct Device {
        const char *name;
        const char *mediaClass;
        const char *direction; // the direction that carries the device's channels
        std::vector<std::string> channels;
    };
    const std::vector<Device> devices{
        {kStereoSink, "Audio/Sink", "input", {"FL", "FR"}},
        {kMonoSink, "Audio/Sink", "input", {"MONO"}},
        {kSurroundSink, "Audio/Sink", "input", {"FL", "FR", "FC", "LFE", "RL", "RR"}},
        {kAuxSink, "Audio/Sink", "input", {"AUX0", "AUX1", "AUX2", "AUX3"}},
        {kStereoSource, "Audio/Source/Virtual", "output", {"FL", "FR"}},
        {kMonoSource, "Audio/Source/Virtual", "output", {"MONO"}},
    };

    const Graph graph = Graph::capture();
    for (const Device &device : devices) {
        SCOPED_TRACE(device.name);
        const Graph::Node *node = graph.findNode(nodeNamed(device.name));
        ASSERT_NE(node, nullptr);
        EXPECT_EQ(node->prop("media.class"), device.mediaClass);
        EXPECT_EQ(channels(graph.portsOf(node->id, device.direction)), device.channels);
    }
    EXPECT_EQ(graph.defaults.at("default.audio.sink"), R"({"name":"rbtest.sink.stereo"})");
    EXPECT_EQ(graph.defaults.at("default.audio.source"), R"({"name":"rbtest.source.stereo"})");
}

TEST_F(Harness, AStreamCanBeMovedToAnotherSink)
{
    FakeApp app;
    const auto stream = waitForNode(playbackStreamOf("TestApp"));
    ASSERT_TRUE(stream);
    const Graph graph = Graph::capture();
    const Graph::Node *stereo = graph.findNode(nodeNamed(kStereoSink));
    const Graph::Node *mono = graph.findNode(nodeNamed(kMonoSink));
    ASSERT_TRUE(stereo && mono);
    ASSERT_TRUE(waitForLink(stream->id, stereo->id));

    moveStream(stream->id, kMonoSink);

    EXPECT_TRUE(waitUntil([&](const Graph &now) {
        return now.linked(stream->id, mono->id) && !now.linked(stream->id, stereo->id);
    })) << "the stream should now play to the mono sink only";
}

TEST_F(Harness, ARestartedAppComesBackAsANewNode)
{
    FakeApp app;
    const auto first = waitForNode(playbackStreamOf("TestApp"));
    ASSERT_TRUE(first);

    app.restart();

    // The new stream may even get the old node id; object.serial is never reused.
    EXPECT_TRUE(waitForNodeGone(*first));
    const auto second = waitForNode(playbackStreamOf("TestApp"));
    ASSERT_TRUE(second);
    EXPECT_NE(second->prop("object.serial"), first->prop("object.serial"));
}

TEST_F(Harness, TheRealSessionIsUntouched)
{
    FakeApp app;
    ASSERT_TRUE(waitForNode(playbackStreamOf("TestApp")));

    const auto host = dumpHostSession();
    if (!host) {
        GTEST_SKIP() << "no real PipeWire session on this machine, which is normal in CI";
    }
    EXPECT_EQ(host->find("rbtest."), std::string::npos) << "a test device appeared in the real session";
    EXPECT_EQ(host->find("\"TestApp\""), std::string::npos) << "the fake app appeared in the real session";
}

// Fails on purpose. Only the CTest case pw.dump-on-failure runs it, to prove
// that a failing test prints a pw-dump of the private graph.
TEST_F(Harness, DISABLED_AFailureReportIncludesAGraphDump)
{
    ADD_FAILURE() << "deliberate failure: the report must contain a pw-dump of the private graph";
}
