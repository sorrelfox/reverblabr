// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "pwtest.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <numbers>
#include <thread>
#include <utility>

extern char **environ;

namespace rb::pwtest
{

namespace
{

using Env = std::vector<std::pair<std::string, std::string>>;

std::string envOr(const char *name, const std::string &fallback = {})
{
    const char *value = std::getenv(name);
    return value ? std::string(value) : fallback;
}

bool underHarness()
{
    return envOr("RB_PW_HARNESS") == "1";
}

std::string harnessDir()
{
    return envOr("RB_PW_HARNESS_DIR", "/tmp");
}

// The current environment with some variables replaced.
std::vector<std::string> environmentWith(const Env &overrides)
{
    std::vector<std::string> result;
    for (char **entry = environ; *entry; ++entry) {
        const std::string text(*entry);
        const std::string name = text.substr(0, text.find('='));
        const bool replaced = std::ranges::any_of(overrides, [&](const auto &item) { return item.first == name; });
        if (!replaced) {
            result.push_back(text);
        }
    }
    for (const auto &[name, value] : overrides) {
        result.push_back(name + '=' + value);
    }
    return result;
}

std::vector<char *> pointers(std::vector<std::string> &strings)
{
    std::vector<char *> result;
    result.reserve(strings.size() + 1);
    for (std::string &text : strings) {
        result.push_back(text.data());
    }
    result.push_back(nullptr);
    return result;
}

// Starts a program. Its stdin is /dev/null; stdout goes to stdoutFd (or
// /dev/null when -1) and stderr to stderrPath (or /dev/null when empty).
pid_t spawn(std::vector<std::string> argv, int stdoutFd, const std::string &stderrPath, const Env &env = {})
{
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    if (stdoutFd >= 0) {
        posix_spawn_file_actions_adddup2(&actions, stdoutFd, STDOUT_FILENO);
    } else {
        posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    }
    const std::string errorPath = stderrPath.empty() ? std::string("/dev/null") : stderrPath;
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, errorPath.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);

    std::vector<std::string> environment = environmentWith(env);
    std::vector<char *> args = pointers(argv);
    std::vector<char *> envp = pointers(environment);
    pid_t pid = -1;
    const int error = posix_spawnp(&pid, args[0], &actions, nullptr, args.data(), envp.data());
    posix_spawn_file_actions_destroy(&actions);
    if (error != 0) {
        ADD_FAILURE() << "could not start " << argv[0] << ": " << std::strerror(error);
        return -1;
    }
    return pid;
}

int waitForExit(pid_t pid)
{
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) { }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
}

// Stops a process: SIGTERM, then SIGKILL if it hasn't exited after two seconds.
void terminate(pid_t pid)
{
    if (pid <= 0) {
        return;
    }
    kill(pid, SIGTERM);
    for (int i = 0; i < 200; ++i) {
        int status = 0;
        const pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid || (result < 0 && errno == ECHILD)) {
            return;
        }
        std::this_thread::sleep_for(10ms);
    }
    kill(pid, SIGKILL);
    waitForExit(pid);
}

struct RunResult {
    int status = -1;
    std::string output;
};

// Runs a program to completion and collects its stdout.
RunResult run(std::vector<std::string> argv, const Env &env = {})
{
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0) {
        return {};
    }
    const pid_t pid = spawn(std::move(argv), fds[1], harnessDir() + "/logs/helpers.log", env);
    close(fds[1]);
    RunResult result;
    char buffer[65536];
    for (;;) {
        const ssize_t count = read(fds[0], buffer, sizeof buffer);
        if (count > 0) {
            result.output.append(buffer, static_cast<std::size_t>(count));
        } else if (count == 0 || errno != EINTR) {
            break;
        }
    }
    close(fds[0]);
    result.status = pid > 0 ? waitForExit(pid) : -1;
    return result;
}

std::string toText(const QJsonValue &value)
{
    switch (value.type()) {
    case QJsonValue::String:
        return value.toString().toStdString();
    case QJsonValue::Bool:
        return value.toBool() ? "true" : "false";
    case QJsonValue::Double: {
        const double number = value.toDouble();
        if (number == std::floor(number) && std::abs(number) < 1e15) {
            return std::to_string(static_cast<long long>(number));
        }
        return std::to_string(number);
    }
    case QJsonValue::Array:
        return QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact).toStdString();
    case QJsonValue::Object:
        return QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact).toStdString();
    default:
        return {};
    }
}

Graph::Props toProps(const QJsonObject &object)
{
    Graph::Props props;
    for (auto it = object.begin(); it != object.end(); ++it) {
        props.emplace(it.key().toStdString(), toText(it.value()));
    }
    return props;
}

std::uint32_t toId(const QJsonValue &value)
{
    return static_cast<std::uint32_t>(value.toInteger());
}

void writeLe(std::ofstream &out, std::uint32_t value, int bytes)
{
    for (int i = 0; i < bytes; ++i) {
        out.put(static_cast<char>((value >> (8 * i)) & 0xff));
    }
}

// Prints a pw-dump of the private graph at the first failure of each test.
class DumpOnFailure : public ::testing::EmptyTestEventListener
{
    // The name is taken here: GoogleTest holds a lock while it reports a
    // failure, and asking it for the current test then deadlocks.
    void OnTestStart(const ::testing::TestInfo &test) override
    {
        m_test = std::string(test.test_suite_name()) + '.' + test.name();
        m_dumped = false;
    }

    void OnTestPartResult(const ::testing::TestPartResult &result) override
    {
        // Outside the harness pw-dump would read the user's real session.
        if (!result.failed() || m_dumped || !underHarness()) {
            return;
        }
        m_dumped = true;
        std::cout << "\n---- pw-dump of the private graph at the first failure of " << m_test << " ----\n"
                  << dumpGraph() << "---- end of pw-dump ----\n"
                  << std::flush;
    }

    std::string m_test;
    bool m_dumped = false;
};

} // namespace

std::string Graph::Node::prop(const std::string &key) const
{
    const auto it = props.find(key);
    return it == props.end() ? std::string() : it->second;
}

std::string Graph::Port::prop(const std::string &key) const
{
    const auto it = props.find(key);
    return it == props.end() ? std::string() : it->second;
}

Graph Graph::capture()
{
    Graph graph;
    const RunResult dump = run({"pw-dump"});
    const QJsonArray objects = QJsonDocument::fromJson(QByteArray::fromStdString(dump.output)).array();
    for (const QJsonValue &item : objects) {
        const QJsonObject object = item.toObject();
        const QString type = object.value(u"type").toString();
        const QJsonObject info = object.value(u"info").toObject();
        const std::uint32_t id = toId(object.value(u"id"));
        if (type == u"PipeWire:Interface:Node") {
            Node node{id, toProps(info.value(u"props").toObject()), std::nullopt};
            const QJsonArray latency = info.value(u"params").toObject().value(u"ProcessLatency").toArray();
            if (!latency.isEmpty()) {
                node.processLatencySamples =
                    static_cast<std::uint32_t>(latency.first().toObject().value(u"rate").toInteger());
            }
            graph.nodes.push_back(std::move(node));
        } else if (type == u"PipeWire:Interface:Port") {
            const QJsonObject props = info.value(u"props").toObject();
            graph.ports.push_back({
                .id = id,
                .nodeId = toId(props.value(u"node.id")),
                .direction = info.value(u"direction").toString().toStdString(),
                .channel = props.value(u"audio.channel").toString().toStdString(),
                .monitor = props.value(u"port.monitor").toBool(),
                .props = toProps(props),
            });
        } else if (type == u"PipeWire:Interface:Link") {
            graph.links.push_back({
                .id = id,
                .outputNode = toId(info.value(u"output-node-id")),
                .outputPort = toId(info.value(u"output-port-id")),
                .inputNode = toId(info.value(u"input-node-id")),
                .inputPort = toId(info.value(u"input-port-id")),
            });
        } else if (type == u"PipeWire:Interface:Metadata"
                   && object.value(u"props").toObject().value(u"metadata.name").toString() == u"default") {
            for (const QJsonValue &entry : object.value(u"metadata").toArray()) {
                const QJsonObject fields = entry.toObject();
                if (fields.value(u"subject").toInteger() == 0) {
                    graph.defaults[fields.value(u"key").toString().toStdString()] = toText(fields.value(u"value"));
                }
            }
        }
    }
    return graph;
}

const Graph::Node *Graph::node(std::uint32_t id) const &
{
    const auto it = std::ranges::find_if(nodes, [id](const Node &node) { return node.id == id; });
    return it == nodes.end() ? nullptr : &*it;
}

const Graph::Node *Graph::findNode(const std::function<bool(const Node &)> &match) const &
{
    const auto it = std::ranges::find_if(nodes, match);
    return it == nodes.end() ? nullptr : &*it;
}

std::vector<Graph::Port> Graph::portsOf(std::uint32_t nodeId, const std::string &direction) const
{
    std::vector<Port> result;
    std::ranges::copy_if(ports, std::back_inserter(result), [&](const Port &port) {
        return port.nodeId == nodeId && port.direction == direction;
    });
    std::ranges::sort(result, {}, &Port::id);
    return result;
}

bool Graph::linked(std::uint32_t outputNode, std::uint32_t inputNode) const
{
    return std::ranges::any_of(
        links, [&](const Link &link) { return link.outputNode == outputNode && link.inputNode == inputNode; });
}

NodeMatch nodeNamed(const std::string &name)
{
    return [name](const Graph::Node &node) { return node.prop("node.name") == name; };
}

NodeMatch playbackStreamOf(const std::string &applicationName)
{
    return [applicationName](const Graph::Node &node) {
        return node.prop("media.class") == "Stream/Output/Audio" && node.prop("application.name") == applicationName;
    };
}

bool waitUntil(const std::function<bool(const Graph &)> &condition, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        if (condition(Graph::capture())) {
            return true;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(50ms);
    }
}

std::optional<Graph::Node> waitForNode(const NodeMatch &match, std::chrono::milliseconds timeout)
{
    std::optional<Graph::Node> found;
    const bool ok = waitUntil(
        [&](const Graph &graph) {
            if (const Graph::Node *node = graph.findNode(match)) {
                found = *node;
                return true;
            }
            return false;
        },
        timeout);
    return ok ? found : std::nullopt;
}

bool waitForNodeGone(const Graph::Node &node, std::chrono::milliseconds timeout)
{
    const std::string serial = node.prop("object.serial");
    return waitUntil(
        [&serial](const Graph &graph) {
            return !graph.findNode(
                [&serial](const Graph::Node &candidate) { return candidate.prop("object.serial") == serial; });
        },
        timeout);
}

bool waitForPorts(std::uint32_t nodeId,
                  const std::string &direction,
                  std::size_t count,
                  std::chrono::milliseconds timeout)
{
    return waitUntil([&](const Graph &graph) { return graph.portsOf(nodeId, direction).size() == count; }, timeout);
}

bool waitForLink(std::uint32_t outputNode, std::uint32_t inputNode, std::chrono::milliseconds timeout)
{
    return waitUntil([&](const Graph &graph) { return graph.linked(outputNode, inputNode); }, timeout);
}

FakeApp::FakeApp(FakeAppOptions options)
    : m_options(std::move(options))
{
    start();
}

FakeApp::~FakeApp()
{
    stop();
}

void FakeApp::start()
{
    if (running()) {
        return;
    }
    std::vector<std::string> argv;
    if (m_options.api == FakeAppOptions::Api::Native) {
        argv = {"pw-cat",
                "--playback",
                "-P",
                "{ application.name = \"" + m_options.applicationName + "\" application.process.binary = \""
                    + m_options.binary + "\" media.role = \"" + m_options.mediaRole + "\" }"};
        if (!m_options.target.empty()) {
            argv.insert(argv.end(), {"--target", m_options.target});
        }
    } else {
        std::string role = m_options.mediaRole;
        std::ranges::transform(role, role.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        argv = {"paplay",
                "--property=application.name=" + m_options.applicationName,
                "--property=application.process.binary=" + m_options.binary,
                "--property=media.role=" + role};
        if (!m_options.target.empty()) {
            argv.push_back("--device=" + m_options.target);
        }
    }
    argv.push_back(toneFile(m_options.toneHz));
    const std::string log = harnessDir() + "/logs/fakeapp-" + m_options.applicationName + ".log";
    m_pid = spawn(std::move(argv), -1, log);
    if (m_options.records) {
        startRecording();
    }
}

void FakeApp::startRecording()
{
    if (m_recorder > 0) {
        return;
    }
    // **The same application**, by the properties that name one: the block
    // matches a binary or a name, so this is that application listening.
    std::vector<std::string> argv{"pw-cat",
                                  "--record",
                                  "-P",
                                  "{ application.name = \"" + m_options.applicationName
                                      + "\" application.process.binary = \"" + m_options.binary + "\" }"};
    if (!m_options.recordFrom.empty()) {
        argv.insert(argv.end(), {"--target", m_options.recordFrom});
    }
    // What it records goes nowhere in particular; the stream is the point.
    argv.push_back(harnessDir() + "/logs/fakeapp-" + m_options.applicationName + "-record.wav");
    const std::string log = harnessDir() + "/logs/fakeapp-" + m_options.applicationName + "-record.log";
    m_recorder = spawn(std::move(argv), -1, log);
}

void FakeApp::stopRecording()
{
    terminate(m_recorder);
    m_recorder = -1;
}

void FakeApp::recordFrom(const std::string &sourceName)
{
    stopRecording();
    m_options.records = true;
    m_options.recordFrom = sourceName;
    startRecording();
}

void FakeApp::stop()
{
    stopRecording();
    terminate(m_pid);
    m_pid = -1;
}

void FakeApp::restart()
{
    stop();
    start();
}

bool FakeApp::running() const
{
    return m_pid > 0;
}

pid_t FakeApp::pid() const
{
    return m_pid;
}

const FakeAppOptions &FakeApp::options() const
{
    return m_options;
}

CommandResult runCommand(std::vector<std::string> argv)
{
    const RunResult result = run(std::move(argv));
    return {result.status, result.output};
}

void setVolume(std::uint32_t nodeId, double volume)
{
    const RunResult result = run({"wpctl", "set-volume", std::to_string(nodeId), std::to_string(volume)});
    EXPECT_EQ(result.status, 0) << "wpctl could not set the volume";
}

void setMute(std::uint32_t nodeId, bool muted)
{
    const RunResult result = run({"wpctl", "set-mute", std::to_string(nodeId), muted ? "1" : "0"});
    EXPECT_EQ(result.status, 0) << "wpctl could not set the mute state";
}

BackgroundProcess::BackgroundProcess(std::vector<std::string> argv, const std::string &logName)
    : m_log(harnessDir() + "/logs/" + logName)
{
    const int fd = open(m_log.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    m_pid = spawn(std::move(argv), fd, m_log + ".stderr");
    close(fd);
}

BackgroundProcess::~BackgroundProcess()
{
    terminate(m_pid);
}

pid_t BackgroundProcess::pid() const
{
    return m_pid;
}

std::string BackgroundProcess::output() const
{
    std::ifstream in(m_log);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

bool BackgroundProcess::waitForOutput(const std::string &text, std::chrono::milliseconds timeout) const
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (output().find(text) == std::string::npos) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(20ms);
    }
    return true;
}

void BackgroundProcess::kill()
{
    if (m_pid > 0) {
        ::kill(m_pid, SIGKILL);
        waitForExit(m_pid);
        m_pid = -1;
    }
}

bool signalProcess(pid_t pid, int signal)
{
    if (pid <= 1 || pid == ::getpid()) {
        ADD_FAILURE() << "refusing to signal pid " << pid << ": not a process this test started";
        return false;
    }
    return ::kill(pid, signal) == 0;
}

void moveStream(std::uint32_t streamNodeId, const std::string &sinkName)
{
    const RunResult result =
        run({"pw-metadata", "-n", "default", std::to_string(streamNodeId), "target.object", sinkName});
    EXPECT_EQ(result.status, 0) << "pw-metadata could not set target.object";
}

bool linkPorts(std::uint32_t outputPort, std::uint32_t inputPort)
{
    return run({"pw-link", std::to_string(outputPort), std::to_string(inputPort)}).status == 0;
}

bool unlinkPorts(std::uint32_t outputPort, std::uint32_t inputPort)
{
    return run({"pw-link", "--disconnect", std::to_string(outputPort), std::to_string(inputPort)}).status == 0;
}

std::size_t linkByChannel(std::uint32_t outputNode, std::uint32_t inputNode)
{
    const Graph graph = Graph::capture();
    const std::vector<Graph::Port> inputs = graph.portsOf(inputNode, "input");
    std::size_t made = 0;
    for (const Graph::Port &output : graph.portsOf(outputNode, "output")) {
        const auto input = std::ranges::find(inputs, output.channel, &Graph::Port::channel);
        if (input != inputs.end() && linkPorts(output.id, input->id)) {
            ++made;
        }
    }
    return made;
}

namespace
{

/// Records \a target with pw-cat, from a sink's monitor or as an ordinary
/// recording, and returns the peak level after the first 100 ms.
std::optional<double> recordPeakDb(const std::string &target, bool sinkMonitor, std::chrono::milliseconds duration)
{
    constexpr int kRate = 48000;
    constexpr int kChannels = 2;
    const std::size_t skipSamples = kRate / 10 * kChannels;
    const std::size_t wantedSamples =
        skipSamples + static_cast<std::size_t>(kRate * duration.count() / 1000) * kChannels;

    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0) {
        return std::nullopt;
    }
    const pid_t pid = spawn({"pw-cat",
                             "--record",
                             "--raw",
                             "--format",
                             "f32",
                             "--rate",
                             std::to_string(kRate),
                             "--channels",
                             std::to_string(kChannels),
                             "--target",
                             target,
                             "-P",
                             sinkMonitor ? "{ stream.capture.sink = true application.name = \"rb-level-tap\" }"
                                         : "{ application.name = \"rb-recorder\" }",
                             "-"},
                            fds[1],
                            harnessDir() + "/logs/level-tap.log");
    close(fds[1]);

    std::vector<float> samples;
    samples.reserve(wantedSamples);
    std::string pending;
    const auto deadline = std::chrono::steady_clock::now() + duration + 5s;
    while (samples.size() < wantedSamples && std::chrono::steady_clock::now() < deadline) {
        pollfd waitFor{fds[0], POLLIN, 0};
        if (poll(&waitFor, 1, 100) <= 0) {
            continue;
        }
        char buffer[16384];
        const ssize_t count = read(fds[0], buffer, sizeof buffer);
        if (count <= 0) {
            break;
        }
        pending.append(buffer, static_cast<std::size_t>(count));
        const std::size_t whole = pending.size() / sizeof(float);
        for (std::size_t i = 0; i < whole && samples.size() < wantedSamples; ++i) {
            float value;
            std::memcpy(&value, pending.data() + i * sizeof(float), sizeof(float));
            samples.push_back(value);
        }
        pending.erase(0, whole * sizeof(float));
    }
    close(fds[0]);
    terminate(pid);

    if (samples.size() < wantedSamples) {
        return std::nullopt;
    }
    float peak = 0.0f;
    for (std::size_t i = skipSamples; i < samples.size(); ++i) {
        peak = std::max(peak, std::abs(samples[i]));
    }
    return peak > 0.0f ? 20.0 * std::log10(static_cast<double>(peak)) : -200.0;
}

} // namespace

std::optional<double> measurePeakDb(const std::string &sinkName, std::chrono::milliseconds duration)
{
    return recordPeakDb(sinkName, true, duration);
}

std::optional<double> measureRecordedPeakDb(const std::string &sourceName, std::chrono::milliseconds duration)
{
    return recordPeakDb(sourceName, false, duration);
}

std::string dumpGraph()
{
    return run({"pw-dump"}).output;
}

std::optional<std::string> dumpHostSession()
{
    const std::string host = envOr("RB_HOST_XDG_RUNTIME_DIR");
    struct stat info{};
    if (host.empty() || stat((host + "/pipewire-0").c_str(), &info) != 0 || !S_ISSOCK(info.st_mode)) {
        return std::nullopt;
    }
    const RunResult result = run({"pw-dump"}, {{"XDG_RUNTIME_DIR", host}, {"PIPEWIRE_RUNTIME_DIR", host}});
    if (result.status != 0 || result.output.empty()) {
        return std::nullopt;
    }
    return result.output;
}

std::string toneFile(double hz)
{
    const std::string path = harnessDir() + "/tone-" + std::to_string(static_cast<int>(hz)) + ".wav";
    if (std::filesystem::exists(path)) {
        return path;
    }
    // 60 s of a sine, 16-bit stereo at 48 kHz. At 1 kHz that is 48 samples
    // per cycle, so the sample at a quarter cycle hits the peak exactly;
    // another frequency is for a test that needs to tell two applications
    // apart in one recording.
    constexpr std::uint32_t kRate = 48000;
    constexpr std::uint32_t kFrames = kRate * 60;
    constexpr std::uint32_t kDataBytes = kFrames * 2 * 2;
    const double amplitude = std::pow(10.0, kTonePeakDb / 20.0) * 32767.0;

    const std::string partial = path + ".part";
    {
        std::ofstream out(partial, std::ios::binary);
        out.write("RIFF", 4);
        writeLe(out, 36 + kDataBytes, 4);
        out.write("WAVEfmt ", 8);
        writeLe(out, 16, 4); // fmt chunk size
        writeLe(out, 1, 2); // PCM
        writeLe(out, 2, 2); // channels
        writeLe(out, kRate, 4);
        writeLe(out, kRate * 4, 4); // byte rate
        writeLe(out, 4, 2); // block align
        writeLe(out, 16, 2); // bits per sample
        out.write("data", 4);
        writeLe(out, kDataBytes, 4);
        for (std::uint32_t frame = 0; frame < kFrames; ++frame) {
            const auto value = static_cast<std::int16_t>(
                std::lround(amplitude * std::sin(2.0 * std::numbers::pi * hz * frame / kRate)));
            writeLe(out, static_cast<std::uint16_t>(value), 2);
            writeLe(out, static_cast<std::uint16_t>(value), 2);
        }
    }
    std::filesystem::rename(partial, path);
    return path;
}

void PipeWireTest::SetUpTestSuite()
{
    static bool listenerInstalled = false;
    if (!listenerInstalled) {
        ::testing::UnitTest::GetInstance()->listeners().Append(new DumpOnFailure);
        listenerInstalled = true;
    }
}

void PipeWireTest::SetUp()
{
    if (!underHarness()) {
        GTEST_FAIL() << "PipeWire integration tests only run inside the private instance: "
                        "tests/pw/harness/run-private-pipewire.sh <test binary>";
    }
}

void PipeWireTest::TearDown()
{
    if (!underHarness()) {
        return;
    }
    // Streams from this test (fake apps, level taps) must be gone before the next one starts.
    const bool clean = waitUntil([](const Graph &graph) {
        return !graph.findNode([](const Graph::Node &node) { return node.prop("media.class").starts_with("Stream/"); });
    });
    EXPECT_TRUE(clean) << "a stream from this test is still in the graph";
}

} // namespace rb::pwtest
