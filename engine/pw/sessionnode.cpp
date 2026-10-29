// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "pw/sessionnode.h"

#include "pw/connection.h"
#include "pw/logging.h"

#include <QTimer>
#include <pipewire/filter.h>
#include <pipewire/keys.h>
#include <pipewire/pipewire.h>
#include <spa/node/io.h>
#include <spa/param/latency-utils.h>
#include <spa/pod/builder.h>
#include <spa/utils/defs.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <new>
#include <optional>
#include <thread>
#include <utility>

using namespace Qt::StringLiterals;

namespace rb::pw
{

namespace
{

/// `pw_filter_get_dsp_buffer()` looks up a buffer the server has already
/// handed over: no allocation, no lock, no system call. It is the one call a
/// process callback makes, and PipeWire's own filters make it in the same
/// place. The compiler cannot know that, so the claim is made here, once.
[[nodiscard]] float *dspBuffer(void *port, std::uint32_t frames) noexcept RB_NONBLOCKING
{
    using GetBuffer = void *(*)(void *, std::uint32_t)RB_NONBLOCKING;
    const auto get = reinterpret_cast<GetBuffer>(&pw_filter_get_dsp_buffer);
    return static_cast<float *>(get(port, frames));
}

/// What the node keeps with each of its ports: where the port's own end of
/// its buffers is, as the filter hands it over in `io_changed`. The filter
/// sets it on the thread that talks to the server and the audio thread
/// reads it, so it is atomic.
struct PortIo {
    std::atomic<spa_io_buffers *> io{nullptr};
};

/// **Whether an output still holds what it wrote the cycle before**.
/// After a cycle, PipeWire hands the port's buffer on to its links and marks
/// the port as needing data again. When it has not — while the port's
/// buffers are being made again, as after a quantum change, there can be a
/// cycle in which the filter writes and nothing passes it on — the filter
/// keeps that buffer, and a buffer asked for now would be queued behind it:
/// that channel would play a cycle late for as long as the link lasted, a
/// cycle apart from its neighbours. So such a port gets no buffer this cycle
/// and the one it holds goes out; the reader hears it once, a cycle late,
/// and the next cycle is in step again.
[[nodiscard]] bool stillUnread(void *port) noexcept RB_NONBLOCKING
{
    const spa_io_buffers *io = static_cast<PortIo *>(port)->io.load(std::memory_order_acquire);
    return io != nullptr && io->status == SPA_STATUS_HAVE_DATA;
}

/// `<blockId>:<in|out>_<channel>`, for example "b1:in_FL", or
/// `<blockId>:<in|out>_<role>_<channel>` for a block whose ports come in
/// more than one group, for example "b1:in_mic_FL". One name, made in one
/// place, because the link manager has to be able to say it too.
[[nodiscard]] QString portNameFor(const QString &blockId, bool input, const QString &channel, const QString &role = {})
{
    const QString direction = input ? u"in"_s : u"out"_s;
    if (role.isEmpty()) {
        return u"%1:%2_%3"_s.arg(blockId, direction, channel);
    }
    return u"%1:%2_%3_%4"_s.arg(blockId, direction, role, channel);
}

[[nodiscard]] QString channelName(dsp::ChannelPosition position)
{
    const std::string_view name = dsp::channelPositionName(position);
    return QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
}

[[nodiscard]] std::uint32_t rateOf(const spa_io_position *position) noexcept RB_NONBLOCKING
{
    if (position == nullptr || position->clock.rate.num == 0) {
        return 0;
    }
    return position->clock.rate.denom / position->clock.rate.num;
}

} // namespace

QString sessionNodeErrorName(SessionNodeError error)
{
    switch (error) {
    case SessionNodeError::NotConnected:
        return u"NotConnected"_s;
    case SessionNodeError::AlreadyRunning:
        return u"AlreadyRunning"_s;
    case SessionNodeError::NotRunning:
        return u"NotRunning"_s;
    case SessionNodeError::FilterRefused:
        return u"FilterRefused"_s;
    case SessionNodeError::PortRefused:
        return u"PortRefused"_s;
    case SessionNodeError::EmptyLayout:
        return u"EmptyLayout"_s;
    case SessionNodeError::TooManyLanes:
        return u"TooManyLanes"_s;
    case SessionNodeError::NoSuchBlock:
        return u"NoSuchBlock"_s;
    case SessionNodeError::PortCountMismatch:
        return u"PortCountMismatch"_s;
    }
    return u"Unknown"_s;
}

class SessionNode::Private
{
public:
    /// One of the node's ports, as the control thread knows it. What the
    /// audio thread needs is in LiveGraph::inputs and ::outputs.
    struct PortEntry {
        std::string blockId;
        /// port.name, which is what says this is the same port as before:
        /// keeping it keeps the links that end on it.
        QString portName;
        QString channel;
        std::uint32_t channelIndex = 0;
        std::uint32_t lane = 0;
        bool input = false;
        void *handle = nullptr;
    };

    /// What a graph handover carries across to the data thread. Pointers
    /// rather than smart pointers, because it travels through a C callback.
    struct Swap {
        SessionProcessor *processor = nullptr;
        LiveGraph *next = nullptr;
        LiveGraph *retired = nullptr;
    };

    SessionNode *q = nullptr;
    Connection &connection;
    SessionProcessor processor;

    pw_filter *filter = nullptr;
    spa_hook listener{};
    SessionNodeSpec spec;
    std::vector<PortEntry> ports;
    /// The output ports' handles, for the cycles that have no graph to say
    /// what belongs in them.
    std::vector<void *> outputHandles;
    bool running = false;
    bool hasGraph = false;
    std::uint32_t reportedRate = 0;
    /// The process latency last told to the audio system, in samples, or
    /// nothing yet.
    std::optional<std::uint32_t> reportedLatency;
    std::chrono::milliseconds fade{10};
    QTimer *rateTimer = nullptr;

    explicit Private(Connection &connection)
        : connection(connection)
    {
    }

    /// Tells the audio system how long the session holds the sound back,
    /// as the node's `SPA_PARAM_ProcessLatency`. PipeWire adds it to
    /// the latency it reports on every port, which is what an application
    /// downstream — a recorder, a streaming tool — reads to line its own
    /// tracks up. Said again only when it changes.
    void reportLatency(std::uint32_t samples)
    {
        if (filter == nullptr || reportedLatency == samples) {
            return;
        }
        std::array<std::uint8_t, 256> storage{};
        spa_pod_builder builder{};
        spa_pod_builder_init(&builder, storage.data(), static_cast<std::uint32_t>(storage.size()));
        spa_process_latency_info info{};
        info.rate = samples;
        const spa_pod *param = spa_process_latency_build(&builder, SPA_PARAM_ProcessLatency, &info);
        if (param == nullptr || pw_filter_update_params(filter, nullptr, &param, 1) < 0) {
            qCWarning(RB_PW) << "the session's process latency could not be reported";
            return;
        }
        reportedLatency = samples;
    }

    [[nodiscard]] static int doSwap(spa_loop *, bool, std::uint32_t, const void *, std::size_t, void *userData)
    {
        auto *swap = static_cast<Swap *>(userData);
        swap->retired = swap->processor->exchange(swap->next);
        swap->next = nullptr;
        return 0;
    }

    /// Puts \a next in place and returns what it replaced, for this thread
    /// to free. A blocking invoke runs the exchange on the data thread
    /// between two cycles, so when it returns the old graph is out of use
    /// ([ADR-0008]).
    [[nodiscard]] std::unique_ptr<LiveGraph> handOver(std::unique_ptr<LiveGraph> next)
    {
        Swap swap{&processor, next.release(), nullptr};
        pw_loop_invoke(pw_filter_get_data_loop(filter), &doSwap, 0, nullptr, 0, true, &swap);
        // The callback runs even when the node is not being scheduled, so
        // there is nothing to wait for and nothing to time out.
        return std::unique_ptr<LiveGraph>(swap.retired);
    }

    static void onIoChanged(void *, void *port, std::uint32_t id, void *area, std::uint32_t size)
    {
        if (port != nullptr && id == SPA_IO_Buffers) {
            static_cast<PortIo *>(port)->io.store(
                area != nullptr && size >= sizeof(spa_io_buffers) ? static_cast<spa_io_buffers *>(area) : nullptr,
                std::memory_order_release);
        }
    }

    static void onProcess(void *data, spa_io_position *position) RB_NONBLOCKING
    {
        auto *d = static_cast<Private *>(data);

        CycleInfo cycle;
        cycle.frames = position != nullptr ? static_cast<std::uint32_t>(position->clock.duration) : 0;
        cycle.sampleRate = rateOf(position);
        cycle.position = position != nullptr ? position->clock.position : 0;
        cycle.clockId = position != nullptr ? position->clock.id : 0;
        cycle.graphCycle = position != nullptr ? position->clock.cycle : 0;
        cycle.xrunDuration = position != nullptr ? position->clock.xrun : 0;
        cycle.xrunFlag = position != nullptr && (position->clock.flags & SPA_IO_CLOCK_FLAG_XRUN_RECOVER) != 0;
#ifdef SPA_IO_CLOCK_FLAG_DISCONT
        cycle.discontinuity = position != nullptr && (position->clock.flags & SPA_IO_CLOCK_FLAG_DISCONT) != 0;
#endif

        if (LiveGraph *live = d->processor.live(); live != nullptr) {
            // A port with no buffer is one nothing is linked to; spike S2
            // found that is the only time PipeWire gives none.
            for (SessionPort &port : live->inputs) {
                port.buffer = dspBuffer(port.handle, cycle.frames);
            }
            for (SessionPort &port : live->outputs) {
                if (stillUnread(port.handle)) {
                    port.buffer = nullptr;
                    ++cycle.unreadOutputs;
                } else {
                    port.buffer = dspBuffer(port.handle, cycle.frames);
                }
            }
        } else {
            // No graph yet, or one has just been retired. The ports still
            // have to be written, or they carry whatever was in the memory.
            for (void *handle : d->outputHandles) {
                if (stillUnread(handle)) {
                    ++cycle.unreadOutputs;
                    continue;
                }
                if (float *buffer = dspBuffer(handle, cycle.frames); buffer != nullptr) {
                    std::fill_n(buffer, cycle.frames, 0.0f);
                }
            }
        }

        d->processor.process(cycle);
    }

    void checkRate()
    {
        const std::uint32_t wanted = processor.rateChangeWanted();
        if (wanted == 0) {
            reportedRate = 0;
            return;
        }
        if (wanted == reportedRate) {
            return;
        }
        reportedRate = wanted;
        qCInfo(RB_PW) << "the session node is running at" << wanted << "Hz and its graph is not; asking for a new one";
        Q_EMIT q->rateChangeNeeded(wanted);
    }

    /// The ports the lanes ask for, in the order they will be numbered.
    [[nodiscard]] static std::vector<PortEntry> wantedPorts(const QList<SessionLane> &inputs,
                                                            const QList<SessionLane> &outputs)
    {
        std::vector<PortEntry> wanted;
        const auto add = [&wanted](const QList<SessionLane> &lanes, bool input) {
            for (qsizetype laneIndex = 0; laneIndex < lanes.size(); ++laneIndex) {
                const SessionLane &lane = lanes.at(laneIndex);
                for (std::size_t channel = 0; channel < lane.layout.size(); ++channel) {
                    PortEntry entry;
                    entry.blockId = lane.blockId.toStdString();
                    entry.channel = channelName(lane.layout[channel]);
                    entry.portName = portNameFor(lane.blockId,
                                                 input,
                                                 entry.channel,
                                                 static_cast<qsizetype>(channel) < lane.channelRoles.size()
                                                     ? lane.channelRoles[static_cast<qsizetype>(channel)]
                                                     : QString{});
                    entry.channelIndex = static_cast<std::uint32_t>(channel);
                    entry.lane = static_cast<std::uint32_t>(laneIndex);
                    entry.input = input;
                    wanted.push_back(std::move(entry));
                }
            }
        };
        add(inputs, true);
        add(outputs, false);
        return wanted;
    }

    /// Asks the server for one port. Null when it will not have it.
    [[nodiscard]] void *createPort(const PortEntry &entry) const
    {
        void *port = pw_filter_add_port(filter,
                                        entry.input ? PW_DIRECTION_INPUT : PW_DIRECTION_OUTPUT,
                                        PW_FILTER_PORT_FLAG_MAP_BUFFERS,
                                        sizeof(PortIo),
                                        pw_properties_new(PW_KEY_FORMAT_DSP,
                                                          "32 bit float mono audio",
                                                          PW_KEY_PORT_NAME,
                                                          entry.portName.toUtf8().constData(),
                                                          PW_KEY_AUDIO_CHANNEL,
                                                          entry.channel.toUtf8().constData(),
                                                          nullptr),
                                        nullptr,
                                        0);
        if (port != nullptr) {
            // The filter hands over zeroed memory of that size.
            new (port) PortIo{};
        }
        return port;
    }

    /// Which output lanes these blocks are heard through, as the bit mask
    /// the audio thread fades by. Lanes are numbered within their direction,
    /// so this is over \a lanes, whichever set of them the caller means.
    [[nodiscard]] static std::uint32_t laneMask(const QList<SessionLane> &lanes, const QList<QString> &blocks)
    {
        std::uint32_t mask = 0;
        for (qsizetype laneIndex = 0; laneIndex < lanes.size() && laneIndex < static_cast<qsizetype>(kMaxLanes);
             ++laneIndex) {
            if (blocks.contains(lanes.at(laneIndex).blockId)) {
                mask |= 1U << static_cast<std::uint32_t>(laneIndex);
            }
        }
        return mask;
    }

    /// Waits for the audio thread to finish the ramp \a sequence, or gives
    /// up. False when it gave up, which means the node is not running cycles
    /// and there is nothing to click.
    [[nodiscard]] bool waitForFade(std::uint32_t sequence, std::chrono::milliseconds limit) const
    {
        const auto deadline = std::chrono::steady_clock::now() + limit;
        while (processor.telemetry().fadeDone != sequence) {
            if (std::chrono::steady_clock::now() >= deadline) {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::microseconds{250});
        }
        return true;
    }

    /// Fills in which node and port of \a graph each of our ports carries.
    [[nodiscard]] std::expected<void, SessionNodeError> bind(const graph::CompiledGraph &graph,
                                                             LiveGraph &live,
                                                             const std::vector<PortEntry> &against,
                                                             std::uint32_t fadedOutputLanes) const
    {
        for (const PortEntry &port : against) {
            std::size_t node = graph.nodeCount();
            for (std::size_t candidate = 0; candidate < graph.nodeCount(); ++candidate) {
                if (graph.nodeId(candidate) == port.blockId) {
                    node = candidate;
                    break;
                }
            }
            if (node == graph.nodeCount()) {
                qCWarning(RB_PW) << "the graph has no block" << QString::fromStdString(port.blockId)
                                 << "for a port of the session node";
                return std::unexpected(SessionNodeError::NoSuchBlock);
            }
            const graph::CompiledGraph::PortCounts counts = graph.portCounts(node);
            const std::uint32_t available = port.input ? counts.inputs : counts.outputs;
            if (port.channelIndex >= available) {
                qCWarning(RB_PW) << "block" << QString::fromStdString(port.blockId) << "has" << available
                                 << "ports where the node presents" << (port.channelIndex + 1);
                return std::unexpected(SessionNodeError::PortCountMismatch);
            }
            SessionPort bound;
            bound.handle = port.handle;
            bound.graphNode = node;
            bound.graphPort = port.channelIndex;
            bound.lane = port.lane;
            // A lane the edit is heard through arrives silent and is ramped
            // back up, so the swap itself cannot be heard.
            if (!port.input && (fadedOutputLanes & (1U << port.lane)) != 0) {
                bound.gain = 0.0f;
            }
            (port.input ? live.inputs : live.outputs).push_back(bound);
        }
        return {};
    }
};

SessionNode::SessionNode(Connection &connection, QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>(connection))
{
    d->q = this;
    d->rateTimer = new QTimer(this);
    d->rateTimer->setInterval(50);
    connect(d->rateTimer, &QTimer::timeout, this, [this] { d->checkRate(); });

    // The connection says this before it destroys anything of PipeWire's, so
    // the filter can still be taken apart properly here.
    connect(&connection, &Connection::registryGone, this, [this] {
        if (d->running) {
            qCWarning(RB_PW) << "the connection went away under the session node" << nodeName();
            stop();
            Q_EMIT failed(SessionNodeError::NotConnected);
        }
    });
}

SessionNode::~SessionNode()
{
    stop();
}

std::expected<void, SessionNodeError> SessionNode::start(const SessionNodeSpec &spec)
{
    if (d->running) {
        return std::unexpected(SessionNodeError::AlreadyRunning);
    }
    if (!d->connection.isConnected()) {
        return std::unexpected(SessionNodeError::NotConnected);
    }
    if (spec.inputs.size() > static_cast<qsizetype>(kMaxLanes)
        || spec.outputs.size() > static_cast<qsizetype>(kMaxLanes)) {
        return std::unexpected(SessionNodeError::TooManyLanes);
    }
    for (const SessionLane &lane : spec.inputs) {
        if (lane.layout.empty()) {
            return std::unexpected(SessionNodeError::EmptyLayout);
        }
    }
    for (const SessionLane &lane : spec.outputs) {
        if (lane.layout.empty()) {
            return std::unexpected(SessionNodeError::EmptyLayout);
        }
    }

    d->spec = spec;
    const QString name = u"reverblabr.%1.seg%2"_s.arg(spec.sessionId).arg(spec.segment);
    const QString description = u"Reverblabr — %1"_s.arg(spec.sessionName);

    // No media.class and no media.role on purpose: the node is neither a
    // device nor an application stream, and a role is how a stream asks the
    // policy for treatment. This one asks for none — every link to it is the
    // link manager's ([ADR-0007]).
    pw_properties *props = pw_properties_new(PW_KEY_NODE_NAME,
                                             name.toUtf8().constData(),
                                             PW_KEY_NODE_DESCRIPTION,
                                             description.toUtf8().constData(),
                                             PW_KEY_MEDIA_TYPE,
                                             "Audio",
                                             PW_KEY_MEDIA_CATEGORY,
                                             "Filter",
                                             PW_KEY_APP_NAME,
                                             "Reverblabr",
                                             PW_KEY_APP_ICON_NAME,
                                             "io.github.reverblabr.Reverblabr",
                                             PW_KEY_NODE_AUTOCONNECT,
                                             "false",
                                             // Meters and generators keep running with nothing
                                             // linked, and an idle device still suspends (spike S2).
                                             "node.always-process",
                                             "true",
                                             nullptr);
    if (spec.latencyFrames > 0 && spec.sampleRate > 0) {
        const QString latency = u"%1/%2"_s.arg(spec.latencyFrames).arg(spec.sampleRate);
        pw_properties_set(props, PW_KEY_NODE_LATENCY, latency.toUtf8().constData());
    }

    static const pw_filter_events events = [] {
        pw_filter_events result{};
        result.version = PW_VERSION_FILTER_EVENTS;
        result.process = &Private::onProcess;
        result.io_changed = &Private::onIoChanged;
        return result;
    }();

    d->filter = pw_filter_new(d->connection.core(), name.toUtf8().constData(), props);
    if (d->filter == nullptr) {
        qCWarning(RB_PW) << "the server would not make the session node" << name;
        d->spec = {};
        return std::unexpected(SessionNodeError::FilterRefused);
    }
    pw_filter_add_listener(d->filter, &d->listener, &events, d.get());

    bool refused = false;
    for (Private::PortEntry entry : Private::wantedPorts(spec.inputs, spec.outputs)) {
        entry.handle = d->createPort(entry);
        if (entry.handle == nullptr) {
            refused = true;
            break;
        }
        if (!entry.input) {
            d->outputHandles.push_back(entry.handle);
        }
        d->ports.push_back(std::move(entry));
    }

    if (refused) {
        qCWarning(RB_PW) << "the server would not take a port of the session node" << name;
        pw_filter_destroy(d->filter);
        d->filter = nullptr;
        d->ports.clear();
        d->outputHandles.clear();
        d->spec = {};
        return std::unexpected(SessionNodeError::PortRefused);
    }

    if (const int error = pw_filter_connect(d->filter, PW_FILTER_FLAG_RT_PROCESS, nullptr, 0); error < 0) {
        qCWarning(RB_PW) << "the session node" << name << "could not be connected:" << strerror(-error);
        pw_filter_destroy(d->filter);
        d->filter = nullptr;
        d->ports.clear();
        d->outputHandles.clear();
        d->spec = {};
        return std::unexpected(SessionNodeError::FilterRefused);
    }

    d->running = true;
    d->rateTimer->start();
    qCInfo(RB_PW) << "the session node" << name << "is running with" << d->ports.size() << "ports";
    Q_EMIT runningChanged(true);
    return {};
}

void SessionNode::stop()
{
    if (!d->running) {
        return;
    }
    d->rateTimer->stop();

    // Retire the graph before the filter goes, so it is freed here rather
    // than left for the destructor to find, and so no cycle can run against
    // half a node.
    const std::unique_ptr<LiveGraph> retired = d->handOver(nullptr);
    d->hasGraph = false;

    pw_filter_disconnect(d->filter);
    // Destroying the filter takes its ports with it, and the server removes
    // every link that ended on one.
    pw_filter_destroy(d->filter);
    d->filter = nullptr;
    d->ports.clear();
    d->outputHandles.clear();
    d->running = false;
    d->reportedRate = 0;
    qCInfo(RB_PW) << "the session node" << nodeName() << "has stopped";
    Q_EMIT runningChanged(false);
}

bool SessionNode::isRunning() const
{
    return d->running;
}

QString SessionNode::nodeName() const
{
    if (d->spec.sessionId.isEmpty()) {
        return {};
    }
    return u"reverblabr.%1.seg%2"_s.arg(d->spec.sessionId).arg(d->spec.segment);
}

std::uint32_t SessionNode::nodeId() const
{
    if (d->filter == nullptr) {
        return 0;
    }
    const std::uint32_t id = pw_filter_get_node_id(d->filter);
    return id == SPA_ID_INVALID ? 0 : id;
}

std::expected<void, SessionNodeError> SessionNode::setGraph(SessionGraph graph)
{
    if (!d->running) {
        return std::unexpected(SessionNodeError::NotRunning);
    }

    const bool wantsGraph = graph.graph != nullptr;
    const std::uint32_t latency =
        wantsGraph ? graph.graph->latencySamples() - std::min(graph.latencyBefore, graph.graph->latencySamples()) : 0;
    std::unique_ptr<LiveGraph> live;
    if (wantsGraph) {
        live = std::make_unique<LiveGraph>();
        live->sampleRate = graph.sampleRate;
        live->paramTargets = std::move(graph.paramTargets);
        live->laneCount = static_cast<std::uint32_t>(d->spec.inputs.size());
        live->blockNanos.assign(graph.graph->nodeCount(), 0);
        live->inputs.reserve(d->ports.size());
        live->outputs.reserve(d->ports.size());
        if (const std::expected<void, SessionNodeError> bound = d->bind(*graph.graph, *live, d->ports, 0); !bound) {
            return std::unexpected(bound.error());
        }
        live->graph = std::move(graph.graph);
    }

    // Freed here, on this thread, once the audio thread has let go of it.
    const std::unique_ptr<LiveGraph> retired = d->handOver(std::move(live));
    d->hasGraph = wantsGraph;
    d->reportLatency(latency);
    return {};
}

std::expected<void, SessionNodeError> SessionNode::update(SessionUpdate edit)
{
    if (!d->running) {
        return std::unexpected(SessionNodeError::NotRunning);
    }
    if (edit.inputs.size() > static_cast<qsizetype>(kMaxLanes)
        || edit.outputs.size() > static_cast<qsizetype>(kMaxLanes)) {
        return std::unexpected(SessionNodeError::TooManyLanes);
    }
    for (const SessionLane &lane : edit.inputs + edit.outputs) {
        if (lane.layout.empty()) {
            return std::unexpected(SessionNodeError::EmptyLayout);
        }
    }

    const std::uint32_t frames = d->fade.count() > 0 && d->spec.sampleRate > 0
        ? static_cast<std::uint32_t>(d->fade.count() * d->spec.sampleRate / 1000)
        : 0;

    // **Down first.** The lanes the edit is heard through go quiet before
    // anything about them changes, so the change itself cannot be heard.
    const std::uint32_t fadingNow = Private::laneMask(d->spec.outputs, edit.changedBlocks);
    if (fadingNow != 0 && frames > 0) {
        const std::uint32_t sequence = d->processor.requestFade(fadingNow, 0.0f, frames);
        if (!d->waitForFade(sequence, d->fade + std::chrono::milliseconds{100})) {
            qCDebug(RB_PW) << "the session node is not running cycles, so the edit needs no fade";
        }
    }

    // **Then the ports the new graph needs**, before the graph that uses
    // them ([ADR-0008]). A port that is wanted again keeps its handle, and
    // with it every link that ends on it.
    std::vector<Private::PortEntry> wanted = Private::wantedPorts(edit.inputs, edit.outputs);
    for (Private::PortEntry &entry : wanted) {
        const auto existing = std::ranges::find_if(
            d->ports, [&entry](const Private::PortEntry &port) { return port.portName == entry.portName; });
        entry.handle = existing != d->ports.end() ? existing->handle : d->createPort(entry);
        if (entry.handle == nullptr) {
            qCWarning(RB_PW) << "the server would not take the port" << entry.portName;
            return std::unexpected(SessionNodeError::PortRefused);
        }
    }

    const std::uint32_t fadingBack = Private::laneMask(edit.outputs, edit.changedBlocks);
    const bool wantsGraph = edit.graph.graph != nullptr;
    const std::uint32_t latency = wantsGraph
        ? edit.graph.graph->latencySamples() - std::min(edit.graph.latencyBefore, edit.graph.graph->latencySamples())
        : 0;
    std::unique_ptr<LiveGraph> live;
    if (wantsGraph) {
        live = std::make_unique<LiveGraph>();
        live->sampleRate = edit.graph.sampleRate;
        live->paramTargets = std::move(edit.graph.paramTargets);
        live->laneCount = static_cast<std::uint32_t>(edit.inputs.size());
        live->blockNanos.assign(edit.graph.graph->nodeCount(), 0);
        live->inputs.reserve(wanted.size());
        live->outputs.reserve(wanted.size());
        if (const std::expected<void, SessionNodeError> bound =
                d->bind(*edit.graph.graph, *live, wanted, frames > 0 ? fadingBack : 0);
            !bound) {
            // Nothing has been handed over, so the only thing to undo is
            // the ports this call made.
            for (const Private::PortEntry &entry : wanted) {
                const auto known = std::ranges::find_if(
                    d->ports, [&entry](const Private::PortEntry &port) { return port.portName == entry.portName; });
                if (known == d->ports.end()) {
                    pw_filter_remove_port(entry.handle);
                }
            }
            return std::unexpected(bound.error());
        }
        live->graph = std::move(edit.graph.graph);
    }

    const std::vector<Private::PortEntry> before = d->ports;
    d->ports = wanted;
    d->outputHandles.clear();
    for (const Private::PortEntry &entry : d->ports) {
        if (!entry.input) {
            d->outputHandles.push_back(entry.handle);
        }
    }

    // Freed here, on this thread, once the audio thread has let go of it.
    const std::unique_ptr<LiveGraph> retired = d->handOver(std::move(live));
    d->hasGraph = wantsGraph;
    d->reportLatency(latency);
    d->spec.inputs = edit.inputs;
    d->spec.outputs = edit.outputs;

    // **And the ports that are no longer wanted**, now that the graph which
    // used them is gone.
    for (const Private::PortEntry &entry : before) {
        const auto kept = std::ranges::find_if(
            d->ports, [&entry](const Private::PortEntry &port) { return port.portName == entry.portName; });
        if (kept == d->ports.end()) {
            pw_filter_remove_port(entry.handle);
        }
    }

    // **Back up.** From here the edit is audible.
    if (fadingBack != 0 && frames > 0) {
        (void)d->processor.requestFade(fadingBack, 1.0f, frames, edit.refilled ? latency : 0);
    }
    return {};
}

void SessionNode::setFadeDuration(std::chrono::milliseconds fade)
{
    d->fade = fade < std::chrono::milliseconds{0} ? std::chrono::milliseconds{0} : fade;
}

std::chrono::milliseconds SessionNode::fadeDuration() const
{
    return d->fade;
}

bool SessionNode::hasGraph() const
{
    return d->hasGraph;
}

LinkEndpoint SessionNode::endpoint(const QString &blockId,
                                   dsp::ChannelPosition channel,
                                   PortDirection direction,
                                   const QString &role) const
{
    LinkEndpoint endpoint;
    endpoint.node.nodeName = nodeName();
    // This node is one of ours, and the manager leaves ours out by default.
    endpoint.node.excludeOurs = false;
    endpoint.channel = channelName(channel);
    endpoint.portName = portNameFor(blockId, direction == PortDirection::Input, endpoint.channel, role);
    return endpoint;
}

bool SessionNode::queueParam(const ParamChange &change)
{
    return d->processor.queueParam(change);
}

SessionTelemetry SessionNode::telemetry() const
{
    SessionTelemetry telemetry = d->processor.telemetry();
    telemetry.processLatency = d->reportedLatency.value_or(0);
    return telemetry;
}

void SessionNode::resetPeakLoad()
{
    d->processor.resetPeakLoad();
}

void SessionNode::setRateCheckInterval(std::chrono::milliseconds interval)
{
    d->rateTimer->setInterval(static_cast<int>(interval.count()));
}

} // namespace rb::pw
