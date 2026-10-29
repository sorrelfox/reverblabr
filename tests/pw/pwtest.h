// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

// Helpers for integration tests against the private PipeWire instance that
// tests/pw/harness/run-private-pipewire.sh starts. The graph is observed with
// pw-dump, independently of Reverblabr's own PipeWire code, so the harness
// can referee that code.

#include <gtest/gtest.h>
#include <sys/types.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace rb::pwtest
{

using namespace std::chrono_literals;

/// Default limit for every wait. Generous, so a slow CI machine doesn't flake.
inline constexpr std::chrono::milliseconds kDefaultTimeout = 10s;

/// How long the tests that listen for something going wrong over time do
/// so: the soak, and the two that listen for a feedback ring.
///
/// **Ten seconds by default**, which is two thousand cycles at quantum 256:
/// long enough to catch a node that has stopped running, or one that loses a
/// cycle whenever something happens, and short enough that every full run of
/// the suite — mine and CI's — does not spend a minute listening to a tone
/// that is fine. `RB_SOAK_SECONDS=600` is the ten minutes the session node
/// is judged over.
[[nodiscard]] inline int soakSeconds()
{
    if (const char *value = std::getenv("RB_SOAK_SECONDS"); value != nullptr) {
        if (const int seconds = std::atoi(value); seconds > 0) {
            return seconds;
        }
    }
    return 10;
}

/// Node names of the harness's test devices (tests/pw/harness/pipewire.conf.d).
inline constexpr const char *kStereoSink = "rbtest.sink.stereo";
inline constexpr const char *kMonoSink = "rbtest.sink.mono";
inline constexpr const char *kSurroundSink = "rbtest.sink.surround51";
inline constexpr const char *kAuxSink = "rbtest.sink.aux4";
inline constexpr const char *kStereoSource = "rbtest.source.stereo";
inline constexpr const char *kMonoSource = "rbtest.source.mono";

/// Peak level of the test tone the fake apps play (1 kHz sine), in dBFS.
inline constexpr double kTonePeakDb = -12.0;

/// A snapshot of the private graph, taken with pw-dump.
struct Graph {
    using Props = std::map<std::string, std::string>;

    struct Node {
        std::uint32_t id = 0;
        Props props;
        /// The node's `ProcessLatency` param, as pw-dump shows it: the
        /// samples in its "rate" field, or nothing when it has not got one.
        std::optional<std::uint32_t> processLatencySamples;
        /// The property's value, or an empty string when it isn't set.
        [[nodiscard]] std::string prop(const std::string &key) const;
    };
    struct Port {
        std::uint32_t id = 0;
        std::uint32_t nodeId = 0;
        std::string direction; ///< "input" or "output"
        std::string channel; ///< audio.channel, for example "FL"
        bool monitor = false;
        Props props;
        /// The property's value, or an empty string when it isn't set.
        [[nodiscard]] std::string prop(const std::string &key) const;
    };
    struct Link {
        std::uint32_t id = 0;
        std::uint32_t outputNode = 0;
        std::uint32_t outputPort = 0;
        std::uint32_t inputNode = 0;
        std::uint32_t inputPort = 0;
    };

    std::vector<Node> nodes;
    std::vector<Port> ports;
    std::vector<Link> links;
    /// Entries of the "default" metadata, key to value, for subject 0.
    Props defaults;

    /// Takes a snapshot of the private graph.
    [[nodiscard]] static Graph capture();

    [[nodiscard]] const Node *node(std::uint32_t id) const &;
    [[nodiscard]] const Node *findNode(const std::function<bool(const Node &)> &match) const &;

    // **Deleted on a graph nobody named**, on purpose. Both return a
    // pointer into this graph's own vector, and capture() hands the graph
    // back by value -- so `Graph::capture().findNode(...)` leaves a pointer
    // to a node that died at the semicolon. It passes in debug and in
    // release, because the freed bytes still hold the right node; only
    // AddressSanitizer says so, and only if somebody runs it. It was
    // written three times in one file before anybody did. Name the graph.
    const Node *node(std::uint32_t id) const && = delete;
    const Node *findNode(const std::function<bool(const Node &)> &match) const && = delete;
    /// Ports of a node in one direction, in port-id order.
    [[nodiscard]] std::vector<Port> portsOf(std::uint32_t nodeId, const std::string &direction) const;
    /// True when at least one link runs from outputNode to inputNode.
    [[nodiscard]] bool linked(std::uint32_t outputNode, std::uint32_t inputNode) const;
};

using NodeMatch = std::function<bool(const Graph::Node &)>;

/// Matches the node with this node.name.
[[nodiscard]] NodeMatch nodeNamed(const std::string &name);
/// Matches the playback stream (Stream/Output/Audio) of an application.
[[nodiscard]] NodeMatch playbackStreamOf(const std::string &applicationName);

/// Polls until the condition holds for a snapshot; false on timeout.
[[nodiscard]] bool waitUntil(const std::function<bool(const Graph &)> &condition,
                             std::chrono::milliseconds timeout = kDefaultTimeout);
/// Polls until a node matches, and returns it; nothing on timeout.
[[nodiscard]] std::optional<Graph::Node> waitForNode(const NodeMatch &match,
                                                     std::chrono::milliseconds timeout = kDefaultTimeout);
/// Polls until this node is gone. Compares object.serial, not the id: PipeWire
/// reuses ids, so a replacement node can take the old id at once.
[[nodiscard]] bool waitForNodeGone(const Graph::Node &node, std::chrono::milliseconds timeout = kDefaultTimeout);
/// Polls until a node has count ports in the direction ("input" or "output").
[[nodiscard]] bool waitForPorts(std::uint32_t nodeId,
                                const std::string &direction,
                                std::size_t count,
                                std::chrono::milliseconds timeout = kDefaultTimeout);
/// Polls until a link runs from one node to the other.
[[nodiscard]] bool
waitForLink(std::uint32_t outputNode, std::uint32_t inputNode, std::chrono::milliseconds timeout = kDefaultTimeout);

/// How a fake application shows up in PipeWire.
struct FakeAppOptions {
    enum class Api {
        Native, ///< a PipeWire client (pw-cat)
        Pulse, ///< a PulseAudio client through pipewire-pulse (paplay)
    };
    Api api = Api::Native;
    std::string applicationName = "TestApp";
    std::string binary = "testapp"; ///< application.process.binary
    std::string mediaRole = "Music";
    std::string target; ///< node.name of the sink; empty plays to the default sink
    /// The frequency it plays, in hertz. Two applications playing different
    /// frequencies can be told apart in one recording, which is how summing
    /// is measured.
    double toneHz = 1000.0;
    /// **Also record**, the way a voice-call client does: a second stream
    /// of the same application, reading from recordFrom or from whatever
    /// the default source is. What it records goes nowhere; what matters is
    /// that the stream exists and is linked to a source, which is what the
    /// Application block follows to find the other side of the call.
    bool records = false;
    std::string recordFrom; ///< node.name of the source; empty is the default
};

/// A stand-in application that plays the test tone (stereo, 1 kHz, peak
/// kTonePeakDb) for a minute. It starts in the constructor and stops in the
/// destructor.
class FakeApp
{
public:
    explicit FakeApp(FakeAppOptions options = {});
    ~FakeApp();
    FakeApp(const FakeApp &) = delete;
    FakeApp &operator=(const FakeApp &) = delete;

    void start();
    void stop();
    void restart();
    /// Starts recording again from another source, the way somebody
    /// changing the microphone in the application's own settings would.
    void recordFrom(const std::string &sourceName);
    [[nodiscard]] bool running() const;
    [[nodiscard]] pid_t pid() const;
    [[nodiscard]] const FakeAppOptions &options() const;

private:
    void startRecording();
    void stopRecording();

    FakeAppOptions m_options;
    pid_t m_pid = -1;
    pid_t m_recorder = -1;
};

/// Result of a helper command run to completion.
struct CommandResult {
    int status = -1;
    std::string output; ///< stdout
};

/// Runs a command inside the private instance and collects its stdout.
CommandResult runCommand(std::vector<std::string> argv);

/// Sets a node's volume or mute through the policy, as a volume applet does.
/// The volume is on wpctl's cubic scale: 0.5 is -18 dB. Both return at once;
/// the change reaches the audio a few cycles later.
void setVolume(std::uint32_t nodeId, double volume);
void setMute(std::uint32_t nodeId, bool muted);

/// A helper program running in the background. Its stdout goes to a log file
/// in the harness directory; it is stopped (SIGTERM) in the destructor.
class BackgroundProcess
{
public:
    BackgroundProcess(std::vector<std::string> argv, const std::string &logName);
    ~BackgroundProcess();
    BackgroundProcess(const BackgroundProcess &) = delete;
    BackgroundProcess &operator=(const BackgroundProcess &) = delete;

    [[nodiscard]] pid_t pid() const;
    /// Everything the program printed on stdout so far.
    [[nodiscard]] std::string output() const;
    /// Polls until the output contains the text.
    [[nodiscard]] bool waitForOutput(const std::string &text,
                                     std::chrono::milliseconds timeout = kDefaultTimeout) const;
    /// Kills the program with SIGKILL, as a crash would, and reaps it.
    void kill();

private:
    std::string m_log;
    pid_t m_pid = -1;
};

/// kill(2) for one process a test started, and nothing else. **A pid of 1 or
/// below is not such a process**: kill(0) signals the test's process group
/// and kill(-1) every process the user owns, the desktop session included,
/// and a plugin host's pid() is -1 once it has been reaped (a host that dies
/// on its own is reaped while the test spins). So anything but an ordinary
/// pid of another process fails the test instead of being signalled.
[[nodiscard]] bool signalProcess(pid_t pid, int signal);

/// Asks the policy to move a stream to another sink, the way a volume applet does.
void moveStream(std::uint32_t streamNodeId, const std::string &sinkName);

/// Links two ports by global id, like pw-link. The link lingers: it stays
/// until one of the ports goes away or unlinkPorts() removes it.
[[nodiscard]] bool linkPorts(std::uint32_t outputPort, std::uint32_t inputPort);
[[nodiscard]] bool unlinkPorts(std::uint32_t outputPort, std::uint32_t inputPort);

/// Links every output port of one node to the input port of another that
/// carries the same audio.channel. Returns the number of links made.
std::size_t linkByChannel(std::uint32_t outputNode, std::uint32_t inputNode);

/// Records what reaches a sink, from its monitor, as stereo and returns the
/// peak level in dBFS. The first 100 ms are skipped. Nothing on failure.
[[nodiscard]] std::optional<double> measurePeakDb(const std::string &sinkName,
                                                  std::chrono::milliseconds duration = 500ms);

/// Records from a source the way an application recording from a
/// microphone does, as stereo, and returns the peak level in dBFS. The first
/// 100 ms are skipped. Nothing on failure.
[[nodiscard]] std::optional<double> measureRecordedPeakDb(const std::string &sourceName,
                                                          std::chrono::milliseconds duration = 500ms);

/// pw-dump output of the private graph, for failure reports.
[[nodiscard]] std::string dumpGraph();

/// pw-dump output of the caller's real PipeWire session, or nothing when the
/// harness found none (for example in CI).
[[nodiscard]] std::optional<std::string> dumpHostSession();

/// Path of the test tone, created on first use in the harness directory.
/// The test tone as a file, made on first use. \a hz is the frequency;
/// the default is the 1 kHz every other test measures.
[[nodiscard]] std::string toneFile(double hz = 1000.0);

/// Base fixture for PipeWire integration tests:
/// - fails at once when not run under the harness, so a test can never touch
///   a real session;
/// - prints a pw-dump of the private graph at a test's first failure;
/// - after each test, waits until no stream from it is left in the graph.
class PipeWireTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite();
    void SetUp() override;
    void TearDown() override;
};

} // namespace rb::pwtest
