// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/sessionruntime.h"

#include "blocks/applicationsource.h"
#include "blocks/outputdevice.h"
#include "blocks/pluginblock.h"
#include "core/paramjson.h"
#include "core/recordings.h"
#include "core/sessionplan.h"
#include "dsp/channelmap.h"
#include "graph/compiledgraph.h"
#include "graph/graphdiff.h"
#include "graph/segments.h"
#include "plugin/formats.h"
#include "pw/connection.h"
#include "pw/feedbackguard.h"
#include "pw/linkmanager.h"
#include "pw/pluginhostprocess.h"
#include "pw/registrymirror.h"
#include "record/recordingwriter.h"
#include "rt/audiotap.h"
#include "rt/meter.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <utility>

using namespace Qt::StringLiterals;

namespace rb::core
{

namespace
{

/// The rate a session is prepared for until PipeWire says otherwise.
constexpr double kStartingRate = 48000.0;

/// How long a lane is faded when an edit changes what it carries.
constexpr std::chrono::milliseconds kEditFade{10};

/// **How much audio a recording block's tap holds: ten seconds**, at the
/// session's rate or at 96 kHz, whichever is more. A disk that stalls for a
/// few seconds — a laptop's drive spinning up, a network folder catching its
/// breath — is not a reason to lose audio, and a session PipeWire moves up
/// to 96 kHz keeps its ten seconds without a new tap.
[[nodiscard]] std::size_t tapFrames(double sampleRate)
{
    constexpr double kSeconds = 10.0;
    constexpr double kRateFloor = 96000.0;
    return static_cast<std::size_t>(std::ceil(std::max(sampleRate, kRateFloor) * kSeconds));
}

/// How long a block is cross-faded when it is switched on or off, in
/// samples at the rate it is running at.
[[nodiscard]] std::uint32_t bypassFadeSamples(double sampleRate)
{
    return static_cast<std::uint32_t>(sampleRate * 0.010); // 10 ms
}

/// Whether a block's ports face PipeWire, and which way round. **The
/// block's own flags, not the category it is listed under**: a
/// generator listed under Outputs sends nothing to a device, and a block
/// under Sources that makes its own sound takes nothing from one.
[[nodiscard]] bool takesAudioIn(const dsp::BlockInfo &info)
{
    return info.inputsFromAudioSystem;
}

[[nodiscard]] bool sendsAudioOut(const dsp::BlockInfo &info)
{
    return info.outputsToAudioSystem;
}

/// Whether a block runs in a plugin host rather than in a segment's graph
/// (ADR-0014). **A plugin that is switched off does not**: it is compiled as
/// the pass-through every switched-off block is, and needs no host.
[[nodiscard]] bool runsInAHost(const QString &type, bool enabled, const dsp::Block &block)
{
    return enabled && blocks::PluginBlock::isPluginType(type.toStdString())
        && !static_cast<const blocks::PluginBlock &>(block).pluginId().empty();
}

[[nodiscard]] bool isPlugin(const QString &type)
{
    return blocks::PluginBlock::isPluginType(type.toStdString());
}

/// **A value as the audio thread takes it**: a number, or for a choice its
/// position in the list, as ParamStore keeps it. Nothing for a value
/// that is neither.
[[nodiscard]] std::optional<double> audioNumber(const dsp::ParamDescriptor &descriptor, const dsp::ParamValue &value)
{
    std::optional<double> number = value.asNumber();
    if (!number && descriptor.type == dsp::ParamType::Enum) {
        const std::string choice = value.text();
        const auto found = std::ranges::find(descriptor.choices, choice);
        if (found != descriptor.choices.end()) {
            number = static_cast<double>(std::distance(descriptor.choices.begin(), found));
        }
    }
    return number;
}

[[nodiscard]] QString fromStd(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

/// Everything a block's saved JSON has to say to it.
[[nodiscard]] dsp::ParamValueMap stateFromJson(const dsp::Block &block, const QJsonObject &params)
{
    dsp::ParamValueMap state;
    for (const dsp::ParamDescriptor &descriptor : block.params()) {
        const std::optional<dsp::ParamValue> value =
            valueFromJson(descriptor, params.value(QString::fromStdString(descriptor.id)));
        if (value) {
            state.emplace(descriptor.id, *value);
        }
    }
    // **A plugin's settings that its block cannot describe go along too**:
    // a plugin this machine has not got, or not scanned yet, keeps
    // what the file says, and saves it again. Their kind is only what JSON
    // says it is.
    if (blocks::PluginBlock::isPluginType(block.info().typeId)) {
        for (auto entry = params.constBegin(); entry != params.constEnd(); ++entry) {
            const std::string id = entry.key().toStdString();
            if (state.contains(id)) {
                continue;
            }
            if (entry->isBool()) {
                state.emplace(id, dsp::ParamValue::ofBool(entry->toBool()));
            } else if (entry->isDouble()) {
                state.emplace(id, dsp::ParamValue::ofFloat(static_cast<float>(entry->toDouble())));
            } else if (entry->isString()) {
                state.emplace(id, dsp::ParamValue::ofString(entry->toString().toStdString()));
            }
        }
    }
    return state;
}

} // namespace

QString runtimeErrorName(RuntimeError error)
{
    switch (error) {
    case RuntimeError::UnknownBlockType:
        return u"UnknownBlockType"_s;
    case RuntimeError::BlockRefused:
        return u"BlockRefused"_s;
    case RuntimeError::NotCompilable:
        return u"NotCompilable"_s;
    case RuntimeError::NodeRefused:
        return u"NodeRefused"_s;
    case RuntimeError::NotRunning:
        return u"NotRunning"_s;
    }
    return u"Unknown"_s;
}

class SessionRuntime::Private
{
public:
    /// One block of the session, and what it is made of.
    struct Instance {
        QString id;
        QString type;
        std::unique_ptr<dsp::Block> block;
        bool enabled = true;
        /// Where the audio thread leaves this block's levels. **Held by
        /// pointer** so that an instance can be moved from one list to the
        /// next during an edit: a meter is full of atomics and cannot be
        /// moved, and a meter that was rebuilt would blink.
        std::unique_ptr<rt::BlockMeter> meter = std::make_unique<rt::BlockMeter>();
        /// What this block's input ports carry, for a source block that is
        /// capturing something. Stereo until the graph says otherwise.
        dsp::ChannelLayout captureLayout = dsp::stereoLayout();
        /// What the microphone's ports carry, for an Application source
        /// that is capturing the other side of a call as well. Empty for
        /// every other block, and for that one when the option is off.
        dsp::ChannelLayout microphoneLayout;
        /// What its output ports carry, for an output block sending to a
        /// device. Stereo until there is a device to say otherwise.
        dsp::ChannelLayout deviceLayout = dsp::stereoLayout();
        /// Where a block that records leaves its audio for the writer, and
        /// what the writer calls that recording. **The instance's, not the
        /// block's**: a block replaced by a fresh one for a new layout is
        /// handed the same tap, so the recording goes on across it.
        std::shared_ptr<rt::AudioTap> tap;
        std::uint64_t recordingKey = 0;
        /// Whether the writer has been told to record this block, which is
        /// what switching it off or stopping the session undoes.
        bool recordingOn = false;

        /// Everything a PrepareSpec is made of that can change while a
        /// session is open. The rest — the longest cycle, where the meter
        /// is — is fixed for the life of the instance.
        struct PreparedFor {
            double sampleRate = 0.0;
            dsp::ChannelLayout captureLayout;
            dsp::ChannelLayout microphoneLayout;
            dsp::ChannelLayout deviceLayout;

            [[nodiscard]] bool operator==(const PreparedFor &other) const = default;
        };

        /// What this block was last prepared for, or nothing if it never has
        /// been.
        ///
        /// **Preparing is not something to do again for luck**. It
        /// may allocate — a meter's window is assigned, a limiter's
        /// lookahead line is made — and it resets what the block was in the
        /// middle of, so a gain gliding to a new value is stepped to it
        /// instead. Doing it on every edit meant every block in the session
        /// was re-prepared when a tile moved, and re-prepared underneath the
        /// audio thread that was still running it.
        std::optional<PreparedFor> preparedFor;

        /// What this block would have to be prepared for now.
        [[nodiscard]] PreparedFor wants(double rate) const
        {
            return PreparedFor{rate, captureLayout, microphoneLayout, deviceLayout};
        }
    };

    SessionRuntime *q = nullptr;
    pw::Connection &connection;
    pw::RegistryMirror &mirror;
    const dsp::BlockRegistry &registry;

    /// **One node per segment** (ADR-0014): a session with no plugin in it
    /// is one, and each plugin cuts it once more. They are all on the
    /// connection's one data loop, so their cycles never overlap.
    struct Segment {
        std::unique_ptr<pw::SessionNode> node;
        /// What it was last given, which the next edit is a difference
        /// against.
        graph::GraphSpec spec;
        /// Its input lanes, which a Recorder's pauses are read from.
        QList<pw::SessionLane> inputs;
        /// How late the sound already is when it arrives in this segment.
        std::uint32_t latencyBefore = 0;
        /// How many times its node had given way when last looked at.
        std::uint32_t overloadsSeen = 0;
    };
    std::vector<Segment> segments;
    /// Which segment each block that is not hosted runs in, as last split.
    QHash<QString, std::uint32_t> segmentOf;
    /// How the session was last cut: what the links between segments and
    /// hosts are made from.
    graph::SegmentPlan split;
    /// Where the audio crosses between segments, by boundary id. Kept across
    /// edits like the blocks are, and retired like them.
    std::map<std::string, std::unique_ptr<graph::SegmentBoundary>> boundaries;

    /// The whole spec's node ids as last split, which is how a link's
    /// hosted end finds its block.
    std::vector<QString> splitIds;
    /// The session's uuid, which the hosts' nodes are named after.
    QString sessionKey;

    /// A plugin block's host, and what it was started for.
    struct Host {
        std::unique_ptr<pw::PluginHostProcess> process;
        pw::PluginHostSpec spec;
        PluginHealth health = PluginHealth::Starting;
        /// **Linked around**: the sound goes from the segment before
        /// it straight to the segment after it. Set when a host fails, and
        /// kept until the one started in its place is ready.
        bool bypassed = false;
        /// Since this host was started, which decides whether its failure is
        /// worth starting another for.
        QElapsedTimer started;
    };
    /// By block id.
    std::map<QString, Host> hosts;
    /// The plugin parameters set since the block's state was last taken
    /// from its host, by block id: what a host given that state has to be
    /// sent after it, so a restart brings back what was set since.
    QHash<QString, QSet<std::uint32_t>> setSinceState;
    /// How long a host has to have run for its failure to start another by
    /// itself.
    std::chrono::milliseconds restartWindow{std::chrono::seconds{10}};
    /// **One manager per session.** A manager owns the links it made and
    /// leaves every other link alone, so two sessions capturing the same
    /// application do not fight over them.
    pw::LinkManager links;
    /// In the session's order, which is the order the graph names them in.
    std::vector<Instance> instances;
    /// The blocks an edit has taken out of the session, held until the graph
    /// that still points at them has been retired.
    ///
    /// **A compiled graph holds raw pointers to its blocks and to their
    /// meters** (`graph::NodeSpec::block`, `::meter`), and the audio thread
    /// goes on running that graph until `SessionNode::update()` has swapped
    /// it — which it does last of all, after the fade. Freeing a removed
    /// block where the edit first notices it is gone would leave the audio
    /// thread calling a destroyed object for the length of a fade, which is
    /// how it once crashed: a jump through a dead vtable inside
    /// `SessionProcessor::process`. So they wait here instead, and go when
    /// the swap has happened and nothing can reach them any more.
    std::vector<Instance> retiring;
    /// Blocks that a re-prepare replaced, held for exactly as long and for
    /// exactly the same reason as `retiring`: the live graph still points at
    /// them until it has been swapped.
    std::vector<std::unique_ptr<dsp::Block>> retiringBlocks;
    Session session;
    /// What each source block is capturing and each output block is sending
    /// to, as it was last worked out.
    SessionPlan plan;
    /// Waits for a burst of registry changes to settle before the plan is
    /// worked out again: an application starting is a node, a client, two
    /// ports and a link within a few milliseconds.
    QTimer replan;
    /// Looks at the nodes for one that has given way to keep the data loop
    /// alive. The node has done the giving way itself, in the cycle;
    /// all that is left here is to say so, and a tenth of a second is soon
    /// enough for that.
    QTimer overloadWatch;
    /// Every time the running graph has been worked out again, from an
    /// edit, a structural parameter or the registry moving under it.
    std::uint64_t replans = 0;
    double sampleRate = kStartingRate;
    bool running = false;
    /// The rings already reported while this run lasts, by their
    /// description.
    QSet<QString> reportedLoops;
    /// **The Application sources whose capture was said to be lost** in this
    /// run, so that their coming back is news and an application seen
    /// for the first time is not.
    QSet<QString> capturesLost;
    /// What each device block's device was called when it was last there:
    /// its description, for a notice that it has gone, when the graph no
    /// longer has it to ask.
    QHash<QString, QString> deviceNames;

    /// Which block each recording belongs to, by the writer's key. Kept
    /// after a block has gone, because its file's last word comes after.
    QHash<quint64, QString> recordingBlocks;
    std::uint64_t nextRecordingKey = 1;
    /// What the writer has said and the engine thread has not passed on yet.
    /// The writer speaks from its own thread; this is where the two meet.
    std::mutex recordingEventsMutex;
    std::vector<record::RecordingEvent> recordingEvents;
    bool recordingDeliveryPosted = false;
    /// **Last, so it goes first**: its thread may still be reporting while
    /// it finishes, and what it reports to must outlive it.
    std::unique_ptr<record::RecordingWriter> writer;

    Private(SessionRuntime *owner,
            pw::Connection &connection,
            pw::RegistryMirror &mirror,
            const dsp::BlockRegistry &registry)
        : q(owner)
        , connection(connection)
        , mirror(mirror)
        , registry(registry)
        , links(connection, mirror)
    {
        replan.setSingleShot(true);
        replan.setInterval(std::chrono::milliseconds{40});
        overloadWatch.setInterval(std::chrono::milliseconds{100});
    }

    /// Says so for every node that has given way since the last look.
    void watchOverloads()
    {
        for (Segment &segment : segments) {
            if (!segment.node) {
                continue;
            }
            const pw::SessionTelemetry telemetry = segment.node->telemetry();
            if (telemetry.overloads == segment.overloadsSeen) {
                continue;
            }
            segment.overloadsSeen = telemetry.overloads;
            QStringList heaviest;
            for (const std::array<char, pw::kOverloadIdBytes> &id : telemetry.heaviest) {
                if (id[0] != '\0') {
                    heaviest.append(QString::fromUtf8(id.data()));
                }
            }
            Q_EMIT q->overloaded(heaviest);
        }
    }

    /// Works the plan out again from the graph as it stands now.
    [[nodiscard]] SessionPlan planFor(const Session &wanted) const
    {
        return sessionPlan(wanted, mirror.nodes(), mirror.ports(), mirror.links(), mirror.defaults());
    }

    /// Keeps the description of every device \a made finds there, for when
    /// it goes.
    void rememberDevices(const SessionPlan &made)
    {
        QHash<QString, QString> present;
        for (const CaptureTarget &target : made.sources) {
            if (target.streamCount > 0 && !target.match.nodeName.isEmpty()) {
                present.insert(target.blockId, target.match.nodeName);
            }
        }
        for (const OutputTarget &target : made.outputs) {
            if (target.devicePresent && !target.deviceName.isEmpty()) {
                present.insert(target.blockId, target.deviceName);
            }
        }
        if (present.isEmpty()) {
            return;
        }
        const QList<pw::PwNode> nodes = mirror.nodes();
        for (auto it = present.cbegin(); it != present.cend(); ++it) {
            QString name = it.value();
            for (const pw::PwNode &node : nodes) {
                if (node.name == it.value()) {
                    name = !node.description.isEmpty() ? node.description
                        : !node.nick.isEmpty()         ? node.nick
                                                       : node.name;
                    break;
                }
            }
            deviceNames.insert(it.key(), name);
        }
    }

    /// Says \a notice, with the name of what it is about.
    void tell(const PlanNotice &notice)
    {
        QString subject;
        switch (notice.code) {
        case NoticeCode::DeviceLost:
        case NoticeCode::DeviceReturned:
            subject = deviceNames.value(notice.blockId);
            break;
        case NoticeCode::CaptureLost:
        case NoticeCode::CaptureRegained:
            if (notice.code == NoticeCode::CaptureLost) {
                capturesLost.insert(notice.blockId);
            } else {
                capturesLost.remove(notice.blockId);
            }
            if (const BlockInstance *block = session.findBlock(notice.blockId); block != nullptr) {
                // The ways of naming an application, most readable first.
                for (const char *key : {"name", "flatpakId", "binary"}) {
                    subject = block->params.value(QLatin1StringView(key)).toString();
                    if (!subject.isEmpty()) {
                        break;
                    }
                }
            }
            break;
        case NoticeCode::Count:
            return;
        }
        const std::string_view code = noticeCodeName(notice.code);
        Q_EMIT q->noticed(
            QString::fromLatin1(code.data(), static_cast<qsizetype>(code.size())), notice.blockId, subject);
    }

    /// Puts the layouts of \a made onto the instances, so that preparing and
    /// compiling use them. Returns whether any of them changed, which is
    /// what decides whether the node needs new ports.
    bool applyPlan(const SessionPlan &made)
    {
        rememberDevices(made);
        bool changed = false;
        for (const CaptureTarget &target : made.sources) {
            Instance *instance = find(target.blockId);
            if (instance == nullptr) {
                continue;
            }
            if (instance->captureLayout != target.layout) {
                instance->captureLayout = target.layout;
                changed = true;
            }
            if (instance->microphoneLayout != target.microphoneLayout) {
                // The application switching microphones is a change of
                // ports like any other, and gets the same edit.
                instance->microphoneLayout = target.microphoneLayout;
                changed = true;
            }
            // What the interface turns into "waiting for Firefox" or
            // "capturing 2 streams". The engine holds no words.
            instance->meter->publish(blocks::ApplicationSource::kStreamCountValue,
                                     static_cast<float>(target.streamCount));
        }
        for (const OutputTarget &target : made.outputs) {
            Instance *instance = find(target.blockId);
            if (instance == nullptr) {
                continue;
            }
            if (instance->deviceLayout != target.layout) {
                instance->deviceLayout = target.layout;
                changed = true;
            }
            // 1 or 0; the word "unavailable" is the interface's.
            instance->meter->publish(blocks::OutputDevice::kDevicePresentValue, target.devicePresent ? 1.0f : 0.0f);
        }
        return changed;
    }

    /// Tells the link manager what should be linked to what. The rules are
    /// matches and not ids, so this only has to be said again when the
    /// session or a layout changes — an application restarting is the
    /// manager's business and needs nothing from here ([ADR-0007]).
    ///
    /// **And the links that join a session cut around its plugins**: from
    /// each segment to the hosts after it, from each host to the segments
    /// that read it, and from a segment to a later one where a path goes
    /// round a plugin (ADR-0014). They are matches too — a host's node by
    /// its name — so a host that is not there yet is linked when it comes.
    void publishLinks()
    {
        if (segments.empty()) {
            return;
        }
        QList<pw::DesiredLink> wanted =
            sessionLinks(plan, [this](const QString &blockId) { return nodeOfBlock(blockId); });
        for (const graph::SegmentPlan::PortLink &link : linksAroundFailedHosts()) {
            const std::optional<pw::LinkEndpoint> from = endpointOf(link.from, pw::PortDirection::Output);
            const std::optional<pw::LinkEndpoint> to = endpointOf(link.to, pw::PortDirection::Input);
            if (from && to) {
                wanted.append(pw::DesiredLink{*from, *to, false});
            }
        }
        links.setDesiredLinks(wanted);
    }

    /// Whether the hosted node \a index of the last split is linked around.
    [[nodiscard]] bool bypassed(std::size_t index) const
    {
        if (index >= splitIds.size()) {
            return false;
        }
        const auto host = hosts.find(splitIds[index]);
        return host != hosts.end() && host->second.bypassed;
    }

    /// **The split's links, with every failed host linked around**:
    /// what went into its feed goes straight to what read its output,
    /// channel for channel — the segment before it to the segment after it,
    /// which PipeWire runs in the same cycle as it ran the three. A plugin
    /// with one input and more outputs has that one on all of them.
    [[nodiscard]] std::vector<graph::SegmentPlan::PortLink> linksAroundFailedHosts() const
    {
        std::vector<graph::SegmentPlan::PortLink> result;
        for (const graph::SegmentPlan::PortLink &link : split.links) {
            if (link.to.hosted && bypassed(link.to.index)) {
                continue; // into it: replaced below
            }
            if (!link.from.hosted || !bypassed(link.from.index)) {
                result.push_back(link);
                continue;
            }
            // Out of it: from what fed the same channel instead.
            std::optional<graph::SegmentPlan::PortRef> feed;
            for (const graph::SegmentPlan::PortLink &into : split.links) {
                if (!into.to.hosted || into.to.index != link.from.index) {
                    continue;
                }
                if (into.to.port == link.from.port || (!feed && into.to.port == 0)) {
                    feed = into.from;
                }
            }
            if (feed) {
                result.push_back(graph::SegmentPlan::PortLink{*feed, link.to});
            }
        }
        return result;
    }

    /// The node a block's ports are on, or null for a block that has none
    /// here.
    [[nodiscard]] pw::SessionNode *nodeOfBlock(const QString &blockId) const
    {
        const auto found = segmentOf.constFind(blockId);
        if (found == segmentOf.constEnd() || *found >= segments.size()) {
            return nullptr;
        }
        return segments[*found].node.get();
    }

    /// The name a plugin block's host gives its node: after the session and
    /// the block, so two sessions with a block of the same id never share.
    [[nodiscard]] QString hostInstance(const QString &blockId) const
    {
        return sessionKey + u'.' + blockId;
    }

    /// One end of a link between segments and hosts.
    [[nodiscard]] std::optional<pw::LinkEndpoint> endpointOf(const graph::SegmentPlan::PortRef &ref,
                                                             pw::PortDirection direction) const
    {
        if (!ref.hosted) {
            const graph::SegmentPlan::Boundary &boundary = split.boundaries[ref.index];
            if (boundary.segment >= segments.size() || !segments[boundary.segment].node) {
                return std::nullopt;
            }
            return segments[boundary.segment].node->endpoint(
                QString::fromStdString(boundary.id), graph::boundaryChannel(ref.port), direction);
        }
        const QString blockId = ref.index < splitIds.size() ? splitIds[ref.index] : QString{};
        const Instance *instance = findConst(blockId);
        if (instance == nullptr || !hosts.contains(blockId)) {
            return std::nullopt;
        }
        // The host names its ports after the plugin's own labels, which the
        // block has too.
        const dsp::BlockInfo &info = instance->block->info();
        const std::string_view label =
            direction == pw::PortDirection::Input ? info.inputs.label(ref.port) : info.outputs.label(ref.port);
        pw::LinkEndpoint endpoint;
        endpoint.node.nodeName = u"reverblabr.plugin."_s + hostInstance(blockId);
        endpoint.node.excludeOurs = false;
        endpoint.portName = fromStd(label);
        return endpoint;
    }

    /// Starts a host for every plugin block that runs in one and has none,
    /// or has one started for another plugin, and lets go of the hosts of
    /// blocks that are gone or switched off — which kills them, and their
    /// nodes go with them.
    void syncHosts()
    {
        std::map<QString, Host> next;
        for (const Instance &instance : instances) {
            if (!runsInAHost(instance.type, instance.enabled, *instance.block)) {
                continue;
            }
            const auto *plugin = static_cast<const blocks::PluginBlock *>(instance.block.get());
            pw::PluginHostSpec spec;
            spec.format = fromStd(plugin->format());
            spec.plugin = QString::fromStdString(plugin->pluginId());
            spec.instance = hostInstance(instance.id);
            spec.latencySamples = plugin->builtinLatency();
            spec.monoMode = QString::fromStdString(plugin->monoMode());
            // **Where the scan found it**: a CLAP plugin's host has to
            // be told which file to load.
            if (const std::optional<plugin::PluginDescription> described =
                    plugin::describePlugin(plugin->format(), plugin->pluginId());
                described && plugin->format() != "builtin") {
                spec.location = fromStd(described->location);
            }
            // **The same plugin keeps its host**, running or failed: a host
            // that failed stays linked around until somebody restarts it,
            // not until the next edit happens to come along.
            if (const auto found = hosts.find(instance.id); found != hosts.end() && found->second.spec == spec) {
                next.emplace(instance.id, std::move(found->second));
                hosts.erase(found);
                continue;
            }
            next.emplace(instance.id, startHost(instance.id, spec, false));
        }
        hosts = std::move(next);
        publishHealth();
    }

    /// A new host for \a blockId, linked around from the start when \a
    /// bypassed, until it is ready.
    [[nodiscard]] Host startHost(const QString &blockId, const pw::PluginHostSpec &spec, bool bypassed)
    {
        Host host;
        host.spec = spec;
        host.bypassed = bypassed;
        host.process = std::make_unique<pw::PluginHostProcess>();
        host.started.start();
        QObject::connect(host.process.get(),
                         &pw::PluginHostProcess::ready,
                         q,
                         [this, blockId](const QString &, quint32 latency) { hostReady(blockId, latency); });
        QObject::connect(host.process.get(),
                         &pw::PluginHostProcess::latencyChanged,
                         q,
                         [this, blockId](quint32 latency) { hostLatency(blockId, latency); });
        QObject::connect(host.process.get(), &pw::PluginHostProcess::failed, q, [blockId](const QString &code) {
            qWarning("the plugin host of %s refused: %s", qPrintable(blockId), qPrintable(code));
        });
        QObject::connect(host.process.get(), &pw::PluginHostProcess::exited, q, [this, blockId](int, bool) {
            hostFailed(blockId, PluginHealth::Crashed);
        });
        QObject::connect(host.process.get(), &pw::PluginHostProcess::hung, q, [this, blockId] {
            hostFailed(blockId, PluginHealth::Hung);
        });
        if (!host.process->start(spec)) {
            qWarning("the plugin host of %s could not be started", qPrintable(blockId));
            host.health = PluginHealth::Crashed;
            host.bypassed = true;
        }
        return host;
    }

    /// What each plugin block's host is doing, where the interface reads it
    /// (PluginBlock::kHostStateValue). Nought for a plugin not hosted.
    void publishHealth()
    {
        for (const Instance &instance : instances) {
            if (!isPlugin(instance.type)) {
                continue;
            }
            const auto host = hosts.find(instance.id);
            const float value = host == hosts.end() ? 0.0f : static_cast<float>(host->second.health);
            instance.meter->publish(blocks::PluginBlock::kHostStateValue, value);
        }
    }

    /// **A host has gone, or stopped running**: the block is linked
    /// around at once, a stopped host is killed rather than waited for, the
    /// latency it reported is taken off the paths beside it, and the
    /// interface is told. A host that had been running long enough is
    /// started again by itself; one that fails again soon after is left for
    /// somebody to restart.
    void hostFailed(const QString &blockId, PluginHealth health)
    {
        const auto host = hosts.find(blockId);
        if (host == hosts.end() || host->second.bypassed || !running) {
            return; // already linked around: a hung host's exit, after it was killed
        }
        Host &failed = host->second;
        failed.bypassed = true;
        failed.health = health;
        failed.process->unwatch();
        if (health == PluginHealth::Hung) {
            failed.process->kill();
        }
        publishLinks();
        // Now, not after the manager's settling time: this is the sound
        // coming back.
        links.reconcileNow();
        publishHealth();
        Q_EMIT q->pluginFailed(blockId, health == PluginHealth::Hung ? u"hung"_s : u"crashed"_s);
        hostLatency(blockId, 0);

        if (failed.started.elapsed() >= restartWindow.count()) {
            QMetaObject::invokeMethod(q, [this, blockId] { (void)restart(blockId); }, Qt::QueuedConnection);
        }
    }

    /// Starts \a blockId's plugin again in a host of its own. It stays
    /// linked around until the new host is ready, and is given the state
    /// and the values it had.
    [[nodiscard]] std::expected<void, QString> restart(const QString &blockId)
    {
        if (!running) {
            return std::unexpected(u"NotRunning"_s);
        }
        const Instance *instance = findConst(blockId);
        if (instance == nullptr) {
            return std::unexpected(u"blockNotFound"_s);
        }
        const auto host = hosts.find(blockId);
        if (host == hosts.end()) {
            return std::unexpected(u"notAHostedPlugin"_s);
        }
        const pw::PluginHostSpec spec = host->second.spec;
        const bool wasBypassed = host->second.bypassed;
        // The old one goes first: it is killed if it is still there, and its
        // node with it, so the new one's name is free.
        hosts.erase(host);
        hosts.emplace(blockId, startHost(blockId, spec, true));
        if (!wasBypassed) {
            publishLinks();
            links.reconcileNow();
        }
        publishHealth();
        return {};
    }

    /// How many cycles the segment feeding \a blockId's host has run: what
    /// its heartbeat is compared with.
    [[nodiscard]] std::uint64_t cyclesBefore(const QString &blockId) const
    {
        const std::string feed = blockId.toStdString() + ".feed";
        std::uint32_t segment = 0;
        for (const graph::SegmentPlan::Boundary &boundary : split.boundaries) {
            if (boundary.id == feed) {
                segment = boundary.segment;
            }
        }
        if (segment >= segments.size() || !segments[segment].node) {
            return 0;
        }
        return segments[segment].node->telemetry().cycles;
    }

    /// A host's node is in the graph: the plugin gets what the session
    /// holds for it — its saved state, or the values of its parameters —
    /// and the latency it reports is taken on.
    void hostReady(const QString &blockId, std::uint32_t latency)
    {
        Instance *instance = find(blockId);
        const auto host = hosts.find(blockId);
        if (instance == nullptr || host == hosts.end() || !isPlugin(instance->type)) {
            return;
        }
        const auto *plugin = static_cast<const blocks::PluginBlock *>(instance->block.get());
        Host &ready = host->second;
        const std::string state = plugin->state();
        if (!state.empty()) {
            // **The state, then what was set since it was taken**: the host
            // holds those back until the state is in, so none of them
            // reaches the plugin the state replaces.
            ready.process->setState(QString::fromStdString(state));
        }
        const QSet<std::uint32_t> since = setSinceState.value(blockId);
        const std::span<const dsp::ParamDescriptor> params = plugin->params();
        for (std::uint32_t index = 0; index < params.size(); ++index) {
            const std::optional<std::uint32_t> hostIndex = blocks::PluginBlock::pluginParamIndex(index);
            // A choice as its position, as a change to it is sent: a choice
            // was once left out here, and the plugin ran on its own.
            const std::optional<double> number = audioNumber(params[index], plugin->param(params[index].id));
            if (hostIndex && number && (state.empty() || since.contains(index))) {
                (void)ready.process->setParam(*hostIndex, static_cast<float>(*number));
            }
        }
        ready.health = PluginHealth::Running;
        ready.process->watch([this, blockId] { return cyclesBefore(blockId); });
        if (ready.bypassed) {
            // Back in: the links through it again.
            ready.bypassed = false;
            publishLinks();
            links.reconcileNow();
        }
        publishHealth();
        hostLatency(blockId, latency);
    }

    /// **The plugin's latency is the block's**: what the host
    /// reported becomes the block's latencySamples, and a change works the
    /// segments out again, so the paths beside it are held back by it.
    void hostLatency(const QString &blockId, std::uint32_t latency)
    {
        Instance *instance = find(blockId);
        if (instance == nullptr || !isPlugin(instance->type)) {
            return;
        }
        auto *plugin = static_cast<blocks::PluginBlock *>(instance->block.get());
        if (const auto host = hosts.find(blockId); host != hosts.end() && host->second.bypassed && latency != 0) {
            return; // a plugin linked around delays nothing
        }
        if (plugin->info().latencySamples == latency) {
            return;
        }
        plugin->setReportedLatency(latency);
        if (running) {
            // Not from inside the host's signal: an update blocks for a
            // fade, and the socket that said this may have more to say.
            QMetaObject::invokeMethod(
                q,
                [this] {
                    if (running) {
                        (void)q->update(session);
                    }
                },
                Qt::QueuedConnection);
        }
    }

    /// Gives every block that records a tap of its own, the first time it
    /// needs one. Before preparing, which hands the tap to the block.
    void ensureTaps()
    {
        for (Instance &instance : instances) {
            if (instance.tap || !instance.block->info().recordsAudio) {
                continue;
            }
            const std::uint32_t channels = std::max<std::uint32_t>(instance.block->info().inputs.maxCount, 1);
            instance.tap = std::make_shared<rt::AudioTap>(channels, tapFrames(sampleRate));
            instance.recordingKey = nextRecordingKey++;
        }
    }

    /// Tells the writer about every recording block it has not heard of,
    /// and to record the ones that are on. **Before the graph that runs
    /// them goes live**, so the first cycle they are given is in the file.
    void beginRecordings(const Session &wanted)
    {
        for (Instance &instance : instances) {
            if (!instance.tap) {
                continue;
            }
            if (!writer) {
                record::WriterOptions options;
                options.journalFolder = recordingJournalFolder().toStdString();
                writer = std::make_unique<record::RecordingWriter>(
                    [this](const record::RecordingEvent &event) { queueRecordingEvent(event); }, options);
            }
            if (!recordingBlocks.contains(instance.recordingKey)) {
                writer->attach(instance.recordingKey, instance.tap);
                recordingBlocks.insert(instance.recordingKey, instance.id);
            }
            if (instance.enabled && !instance.recordingOn) {
                const BlockInstance *block = wanted.findBlock(instance.id);
                const QString name = block != nullptr && !block->nickname.isEmpty() ? block->nickname : instance.id;
                writer->start(instance.recordingKey, recordingSettings(*instance.block, wanted.name, name));
                instance.recordingOn = true;
            }
        }
    }

    /// Finishes the files of recording blocks that have been switched off,
    /// and lets go of the ones that have been taken out. **After the graph
    /// that stopped running them is live**, so their last cycle is in.
    void endRecordings()
    {
        if (!writer) {
            return;
        }
        for (Instance &instance : instances) {
            if (instance.tap && !instance.enabled && instance.recordingOn) {
                writer->finish(instance.recordingKey);
                instance.recordingOn = false;
            }
        }
        for (Instance &gone : retiring) {
            if (gone.tap) {
                writer->detach(gone.recordingKey);
                gone.recordingOn = false;
            }
        }
    }

    /// Closes every file, and says so before returning: a session that has
    /// stopped has its recordings on the disk and every one of them
    /// reported.
    void finishRecordings()
    {
        if (!writer) {
            return;
        }
        for (Instance &instance : instances) {
            if (instance.tap && instance.recordingOn) {
                writer->finish(instance.recordingKey);
                instance.recordingOn = false;
            }
        }
        for (Instance &gone : retiring) {
            if (gone.tap) {
                writer->detach(gone.recordingKey);
            }
        }
        writer->sync();
        deliverRecordingEvents();
    }

    /// The writer's thread: keep the event and make sure the engine thread
    /// comes round for it.
    void queueRecordingEvent(const record::RecordingEvent &event)
    {
        bool post = false;
        {
            const std::lock_guard lock(recordingEventsMutex);
            recordingEvents.push_back(event);
            post = !std::exchange(recordingDeliveryPosted, true);
        }
        if (post) {
            QMetaObject::invokeMethod(q, [this] { deliverRecordingEvents(); }, Qt::QueuedConnection);
        }
    }

    /// The engine thread: says what the writer said.
    void deliverRecordingEvents()
    {
        std::vector<record::RecordingEvent> events;
        {
            const std::lock_guard lock(recordingEventsMutex);
            events.swap(recordingEvents);
            recordingDeliveryPosted = false;
        }
        for (const record::RecordingEvent &event : events) {
            const QString blockId = recordingBlocks.value(event.key);
            const QString path = QString::fromStdString(event.path.string());
            switch (event.kind) {
            case record::RecordingEvent::Kind::FileStarted:
                Q_EMIT q->recordingFileStarted(blockId, path);
                break;
            case record::RecordingEvent::Kind::FileFinished:
                Q_EMIT q->recordingFileFinished(blockId, path, event.frames, event.sampleRate);
                break;
            case record::RecordingEvent::Kind::Failed:
                Q_EMIT q->recordingFailed(blockId, path, QString::fromUtf8(record::fileErrorName(event.error)));
                break;
            case record::RecordingEvent::Kind::Paused:
                Q_EMIT q->recordingPaused(blockId, true);
                break;
            case record::RecordingEvent::Kind::Resumed:
                Q_EMIT q->recordingPaused(blockId, false);
                break;
            case record::RecordingEvent::Kind::Stopped:
                Q_EMIT q->recordingStopped(
                    blockId,
                    event.stopReason == record::RecordingEvent::StopReason::Silence ? u"silence"_s : u"timeLimit"_s);
                break;
            case record::RecordingEvent::Kind::TagsNotWritten:
                Q_EMIT q->recordingTagsNotWritten(blockId, path);
                break;
            case record::RecordingEvent::Kind::LowDiskSpace:
                Q_EMIT q->recordingLowDiskSpace(blockId, path, event.bytesFree);
                break;
            case record::RecordingEvent::Kind::Overflow: {
                const double rate = event.sampleRate > 0 ? event.sampleRate : sampleRate;
                Q_EMIT q->recordingOverflowed(blockId, path, static_cast<double>(event.frames) / rate);
                break;
            }
            }
        }
    }

    /// Tells every Recorder's tap which input lanes its pauses are read
    /// from. **Whenever the lanes may have been numbered again**, and
    /// whenever a source's say about its pauses may have changed; a few
    /// cycles either side of an edit may go by the old numbers, which costs
    /// at most a few milliseconds of a pause written or not.
    void applyGapLanes(const Session &wanted)
    {
        const auto leavesGaps = [this](const QString &blockId) {
            const Instance *source = find(blockId);
            if (source == nullptr || source->type.toStdString() != blocks::ApplicationSource::kTypeId) {
                return false;
            }
            return source->block->param("fillGapsWithSilence").asBool() == false;
        };
        for (Instance &instance : instances) {
            if (!instance.tap) {
                continue;
            }
            // The lanes of the node the Recorder runs in, which is the one
            // its pauses are read from.
            QStringList inputLanes;
            const auto segment = segmentOf.constFind(instance.id);
            if (segment != segmentOf.constEnd() && *segment < segments.size()) {
                for (const pw::SessionLane &lane : segments[*segment].inputs) {
                    inputLanes.append(lane.blockId);
                }
            }
            instance.tap->setGapLanes(recordingGapLanes(wanted, instance.id, inputLanes, leavesGaps));
        }
    }

    [[nodiscard]] Instance *find(const QString &id)
    {
        const auto found =
            std::ranges::find_if(instances, [&id](const Instance &instance) { return instance.id == id; });
        return found == instances.end() ? nullptr : &*found;
    }

    [[nodiscard]] const Instance *findConst(const QString &id) const
    {
        const auto found =
            std::ranges::find_if(instances, [&id](const Instance &instance) { return instance.id == id; });
        return found == instances.end() ? nullptr : &*found;
    }

    /// Makes the blocks the session asks for, keeping the ones that are
    /// already there. Everything a kept block remembers it goes on
    /// remembering, which is the point.
    [[nodiscard]] std::expected<void, RuntimeError> reconcileBlocks(const Session &wanted)
    {
        std::vector<Instance> next;
        next.reserve(static_cast<std::size_t>(wanted.blocks.size()));
        for (const BlockInstance &block : wanted.blocks) {
            const auto existing = std::ranges::find_if(instances, [&block](const Instance &instance) {
                return instance.id == block.id && instance.type == block.type;
            });
            if (existing != instances.end()) {
                existing->enabled = block.enabled;
                next.push_back(std::move(*existing));
                continue;
            }
            std::expected<std::unique_ptr<dsp::Block>, dsp::BlockError> made =
                registry.create(block.type.toStdString());
            if (!made) {
                return std::unexpected(RuntimeError::UnknownBlockType);
            }
            Instance instance;
            instance.id = block.id;
            instance.type = block.type;
            instance.enabled = block.enabled;
            instance.block = std::move(*made);
            // What the session saved for it, before it runs a cycle. A
            // setting the block does not know is ignored rather than
            // refused, so a session written by a later version still opens.
            if (!block.params.isEmpty()) {
                // **Which plugin first**: it says what the rest of the
                // settings are.
                if (const QJsonValue plugin = block.params.value("plugin"_L1);
                    isPlugin(block.type) && plugin.isString()) {
                    (void)instance.block->setParam(blocks::PluginBlock::kPluginParam,
                                                   dsp::ParamValue::ofString(plugin.toString().toStdString()));
                }
                (void)instance.block->loadState(stateFromJson(*instance.block, block.params));
            }
            next.push_back(std::move(instance));
        }
        // What is still in the old list is what the session no longer has:
        // an instance that was kept was moved out above, and a moved-from
        // one holds no block. Those are not destroyed here — see `retiring`.
        for (Instance &gone : instances) {
            if (gone.block != nullptr) {
                retiring.push_back(std::move(gone));
            }
        }
        instances = std::move(next);
        return {};
    }

    /// Gets ready every block that is not ready already.
    ///
    /// **Only the ones something really changed for**. Preparing may
    /// allocate and it resets what a block was in the middle of, so a block
    /// whose rate and layouts are the ones it was last prepared for is left
    /// alone — which is nearly all of them, nearly every edit.
    ///
    /// **And never a block the audio thread may be running.** A block that
    /// does need preparing again is replaced by a fresh one carrying its
    /// settings, prepared before it is anybody's, and the old one waits with
    /// the retired instances until the graph that points at it has been
    /// swapped. Preparing in place would reallocate the very buffers a cycle
    /// is reading.
    ///
    /// Nothing is replaced before the session is running, because then
    /// nothing can be running it: that is where a block keeps the state a
    /// recompile exists to preserve.
    [[nodiscard]] std::expected<void, RuntimeError> prepareBlocks()
    {
        for (Instance &instance : instances) {
            const Instance::PreparedFor wanted = instance.wants(sampleRate);
            if (instance.preparedFor == wanted) {
                continue;
            }
            if (running && instance.preparedFor.has_value()) {
                std::expected<std::unique_ptr<dsp::Block>, dsp::BlockError> fresh =
                    registry.create(instance.type.toStdString());
                if (!fresh) {
                    return std::unexpected(RuntimeError::UnknownBlockType);
                }
                // Its settings, not its memory: an envelope or a filter's
                // history belongs to the layout it was made for, and this is
                // a block being made for another one.
                (void)(*fresh)->loadState(instance.block->saveState());
                if (isPlugin(instance.type)) {
                    // What the host said is not a setting, and is still true.
                    static_cast<blocks::PluginBlock &>(**fresh).setReportedLatency(
                        instance.block->info().latencySamples);
                }
                retiringBlocks.push_back(std::move(instance.block));
                instance.block = std::move(*fresh);
            }

            dsp::PrepareSpec prepare;
            prepare.sampleRate = sampleRate;
            prepare.maxBlockFrames = dsp::kMaxBlockFrames;
            // A source block is given as many channels as the thing it is
            // capturing plays in, and is told which channel is which: a
            // downmix needs to know where the centre is, not merely how
            // many there are.
            const bool isSource = takesAudioIn(instance.block->info());
            const bool isOutput = sendsAudioOut(instance.block->info());
            prepare.outputChannels = isOutput ? static_cast<std::uint32_t>(instance.deviceLayout.size()) : 2;
            if (isSource) {
                // **Both groups, on one set of ports**, the application's
                // first: a block taking the microphone as well is told
                // where the second begins, because the two have channels of
                // the same name.
                prepare.inputLayout = instance.captureLayout;
                prepare.inputLayout.insert(
                    prepare.inputLayout.end(), instance.microphoneLayout.begin(), instance.microphoneLayout.end());
                prepare.secondInputFrom =
                    instance.microphoneLayout.empty() ? 0 : static_cast<std::uint32_t>(instance.captureLayout.size());
            }
            prepare.inputChannels = isSource ? static_cast<std::uint32_t>(prepare.inputLayout.size()) : 2;
            if (isOutput) {
                // An output block is given the session's stereo bus and asked
                // for whatever the device takes.
                prepare.outputLayout = instance.deviceLayout;
            }
            // Where the block publishes numbers of its own — a meter's level,
            // a gain reduction. The levels either side of it are the graph's
            // to measure and need nothing from the block.
            prepare.meter = instance.meter.get();
            prepare.recording = instance.tap.get();
            if (!instance.block->prepare(prepare)) {
                return std::unexpected(RuntimeError::BlockRefused);
            }
            instance.preparedFor = wanted;
        }
        return {};
    }

    /// The session as the graph compiler wants it. \a fadingBypass names the
    /// blocks whose on/off state has just changed.
    [[nodiscard]] graph::GraphSpec specFor(const Session &wanted, const QList<QString> &fadingBypass) const
    {
        graph::GraphSpec spec;
        spec.sampleRate = sampleRate;
        QHash<QString, std::size_t> indexOf;
        for (const BlockInstance &block : wanted.blocks) {
            const auto found =
                std::ranges::find_if(instances, [&block](const Instance &i) { return i.id == block.id; });
            if (found == instances.end()) {
                continue;
            }
            graph::NodeSpec node;
            node.id = block.id.toStdString();
            node.block = found->block.get();
            node.enabled = block.enabled;
            node.crossfadeSamples = fadingBypass.contains(block.id) ? bypassFadeSamples(sampleRate) : 0;
            node.meter = found->meter.get();
            indexOf.insert(block.id, spec.nodes.size());
            spec.nodes.push_back(std::move(node));
        }
        for (const Wire &wire : wanted.wires) {
            if (!indexOf.contains(wire.fromBlock) || !indexOf.contains(wire.toBlock)) {
                continue; // a wire to a block that is not there says nothing
            }
            graph::EdgeSpec edge;
            edge.fromNode = indexOf.value(wire.fromBlock);
            edge.fromPort = static_cast<std::uint32_t>(wire.fromPort);
            edge.toNode = indexOf.value(wire.toBlock);
            edge.toPort = static_cast<std::uint32_t>(wire.toPort);
            spec.edges.push_back(edge);
        }
        return spec;
    }

    /// A segment's lanes: a source block takes audio in, an output block
    /// sends it out, a boundary does one or the other, and everything else
    /// stays inside the graph.
    [[nodiscard]] QList<pw::SessionLane> lanes(std::uint32_t segment,
                                               bool input,
                                               const QHash<QString, std::uint32_t> &segmentOfBlock,
                                               const graph::SegmentPlan &cut) const
    {
        QList<pw::SessionLane> result;
        for (const Instance &instance : instances) {
            if (segmentOfBlock.value(instance.id, graph::kHosted) != segment) {
                continue;
            }
            if (input ? !takesAudioIn(instance.block->info()) : !sendsAudioOut(instance.block->info())) {
                continue;
            }
            pw::SessionLane lane;
            lane.blockId = instance.id;
            // A lane carries what is on the other end of it: what the
            // application is playing, or what the device takes.
            lane.layout = input ? instance.captureLayout : instance.deviceLayout;
            if (input && !instance.microphoneLayout.empty()) {
                // And, for an Application source taking the other side of a
                // call, the microphone after it — on ports of its own, or
                // its front left and the application's would be one port
                // and PipeWire would sum them.
                const QString role = fromStd(blocks::ApplicationSource::kMicrophoneRole);
                for (std::size_t channel = 0; channel < lane.layout.size(); ++channel) {
                    lane.channelRoles.append(QString{});
                }
                for (const dsp::ChannelPosition position : instance.microphoneLayout) {
                    lane.layout.push_back(position);
                    lane.channelRoles.append(role);
                }
            }
            result.append(lane);
        }
        for (const graph::SegmentPlan::Boundary &boundary : cut.boundaries) {
            const bool arriving = boundary.direction == graph::SegmentBoundary::Direction::Arriving;
            if (boundary.segment != segment || arriving != input) {
                continue;
            }
            pw::SessionLane lane;
            lane.blockId = QString::fromStdString(boundary.id);
            for (std::uint32_t port = 0; port < boundary.channels; ++port) {
                lane.layout.push_back(graph::boundaryChannel(port));
            }
            result.append(lane);
        }
        return result;
    }

    [[nodiscard]] std::vector<dsp::Block *> paramTargets() const
    {
        std::vector<dsp::Block *> targets;
        targets.reserve(instances.size());
        for (const Instance &instance : instances) {
            targets.push_back(instance.block.get());
        }
        return targets;
    }

    /// The session, cut and compiled: a graph and lanes for every segment.
    struct Built {
        graph::SegmentPlan cut;
        std::vector<QString> ids;
        QHash<QString, std::uint32_t> segmentOf;
        std::vector<pw::SessionGraph> graphs;
        std::vector<QList<pw::SessionLane>> inputs;
        std::vector<QList<pw::SessionLane>> outputs;
    };

    /// **Cuts the session around its hosted plugins and compiles every
    /// segment** (ADR-0014). A session with no plugin running in a host is
    /// one segment, compiled from exactly the spec it always was. \a
    /// fadingBypass names the blocks whose on/off state has just changed.
    [[nodiscard]] std::expected<Built, RuntimeError> build(const Session &wanted, const QList<QString> &fadingBypass)
    {
        const graph::GraphSpec whole = specFor(wanted, fadingBypass);
        Built built;
        std::vector<bool> hosted;
        for (const graph::NodeSpec &node : whole.nodes) {
            const QString id = QString::fromStdString(node.id);
            const Instance *instance = findConst(id);
            built.ids.push_back(id);
            hosted.push_back(instance != nullptr && runsInAHost(instance->type, node.enabled, *instance->block));
        }
        std::expected<graph::SegmentPlan, graph::GraphError> cut = graph::splitIntoSegments(whole, hosted);
        if (!cut) {
            return std::unexpected(RuntimeError::NotCompilable);
        }
        built.cut = std::move(*cut);
        for (std::size_t node = 0; node < built.ids.size(); ++node) {
            if (!built.cut.partition.hosted(node)) {
                built.segmentOf.insert(built.ids[node], built.cut.partition.segmentOf[node]);
            }
        }

        // The boundaries, kept from the last time where their shape is the
        // same: a live graph may be running one, and a new one of the same
        // shape would be a new pointer for nothing.
        for (const graph::SegmentPlan::Boundary &boundary : built.cut.boundaries) {
            std::unique_ptr<graph::SegmentBoundary> &slot = boundaries[boundary.id];
            if (slot && (slot->direction() != boundary.direction || slot->channels() != boundary.channels)) {
                retiringBlocks.push_back(std::move(slot));
            }
            if (!slot) {
                slot = std::make_unique<graph::SegmentBoundary>(boundary.direction, boundary.channels);
                dsp::PrepareSpec prepare;
                prepare.sampleRate = sampleRate;
                prepare.inputChannels = boundary.channels;
                prepare.outputChannels = boundary.channels;
                (void)slot->prepare(prepare);
            }
            slot->setCarriedLatency(boundary.carriedLatency);
        }

        const std::uint32_t count = built.cut.partition.segmentCount;
        for (std::uint32_t segment = 0; segment < count; ++segment) {
            graph::GraphSpec &spec = built.cut.segments[segment];
            std::uint32_t latencyBefore = 0;
            for (graph::NodeSpec &node : spec.nodes) {
                if (node.block == nullptr) {
                    graph::SegmentBoundary *boundary = boundaries.at(node.id).get();
                    node.block = boundary;
                    if (boundary->direction() == graph::SegmentBoundary::Direction::Arriving) {
                        latencyBefore = std::max(latencyBefore, boundary->info().latencySamples);
                    }
                }
            }
            std::expected<graph::CompiledGraph, graph::GraphError> compiled = graph::CompiledGraph::compile(spec);
            if (!compiled) {
                return std::unexpected(RuntimeError::NotCompilable);
            }
            // How many channels each block is metered on is how many outputs
            // the compiler gave it, which is not known until here: a block
            // with flexible ports takes its shape from what is wired to it.
            for (std::size_t node = 0; node < spec.nodes.size(); ++node) {
                const std::size_t origin = built.cut.origin[segment][node];
                if (origin == graph::SegmentPlan::kBoundaryNode) {
                    continue;
                }
                if (Instance *instance = find(built.ids[origin]); instance != nullptr) {
                    instance->meter->setChannelCount(compiled->portCounts(node).outputs);
                }
            }
            pw::SessionGraph result;
            result.graph = std::make_unique<graph::CompiledGraph>(std::move(*compiled));
            result.paramTargets = paramTargets();
            result.sampleRate = sampleRate;
            result.latencyBefore = latencyBefore;
            built.graphs.push_back(std::move(result));
            built.inputs.push_back(lanes(segment, true, built.segmentOf, built.cut));
            built.outputs.push_back(lanes(segment, false, built.segmentOf, built.cut));
        }
        return built;
    }

    /// Starts segment \a segment's node with what \a built made for it.
    [[nodiscard]] std::expected<void, RuntimeError>
    startSegment(std::uint32_t segment, const Session &wanted, Built &built)
    {
        Segment &made = segments[segment];
        made.node = std::make_unique<pw::SessionNode>(connection);
        made.overloadsSeen = 0;
        made.node->setFadeDuration(kEditFade);
        pw::SessionNodeSpec nodeSpec;
        nodeSpec.sessionId = wanted.id.toString(QUuid::WithoutBraces);
        nodeSpec.sessionName = wanted.name;
        nodeSpec.segment = segment;
        nodeSpec.sampleRate = static_cast<std::uint32_t>(sampleRate);
        nodeSpec.inputs = built.inputs[segment];
        nodeSpec.outputs = built.outputs[segment];
        if (!made.node->start(nodeSpec)) {
            made.node.reset();
            return std::unexpected(RuntimeError::NodeRefused);
        }
        if (!made.node->setGraph(std::move(built.graphs[segment]))) {
            made.node->stop();
            made.node.reset();
            return std::unexpected(RuntimeError::NotCompilable);
        }
        return {};
    }

    /// Remembers what \a built made, for the next edit and for the links.
    void keep(Built &built)
    {
        for (std::uint32_t segment = 0; segment < segments.size() && segment < built.cut.segments.size(); ++segment) {
            segments[segment].spec = built.cut.segments[segment];
            segments[segment].inputs = built.inputs[segment];
            segments[segment].latencyBefore = built.graphs.size() > segment ? built.graphs[segment].latencyBefore : 0;
        }
        // Boundaries no graph uses any more: every graph that did has been
        // swapped by now.
        for (auto it = boundaries.begin(); it != boundaries.end();) {
            const bool used = std::ranges::any_of(
                built.cut.boundaries, [&it](const graph::SegmentPlan::Boundary &b) { return b.id == it->first; });
            it = used ? std::next(it) : boundaries.erase(it);
        }
        split = std::move(built.cut);
        splitIds = std::move(built.ids);
        segmentOf = std::move(built.segmentOf);
    }

    /// Takes every segment's node out of the graph.
    void stopSegments()
    {
        for (Segment &segment : segments) {
            if (segment.node) {
                segment.node->stop();
            }
        }
        segments.clear();
    }
};

SessionRuntime::SessionRuntime(pw::Connection &connection,
                               pw::RegistryMirror &mirror,
                               const dsp::BlockRegistry &registry,
                               QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>(this, connection, mirror, registry))
{
    // **The graph moving is what changes a capture, not the session.** An
    // application starting, stopping or being moved to another sink is all
    // the same question — what should each source block be capturing now —
    // so every registry change asks it again once the burst has settled.
    const auto replan = [this] {
        if (d->running) {
            d->replan.start();
        }
    };
    connect(&mirror, &pw::RegistryMirror::nodeAdded, this, replan);
    connect(&mirror, &pw::RegistryMirror::nodeChanged, this, replan);
    connect(&mirror, &pw::RegistryMirror::nodeRemoved, this, replan);
    connect(&mirror, &pw::RegistryMirror::portAdded, this, replan);
    connect(&mirror, &pw::RegistryMirror::portRemoved, this, replan);
    // Where a stream is going is a matter of its links, and which output is
    // the default is WirePlumber's to say; both change what a source block
    // should be capturing.
    connect(&mirror, &pw::RegistryMirror::linkAdded, this, replan);
    connect(&mirror, &pw::RegistryMirror::linkRemoved, this, replan);
    connect(&mirror, &pw::RegistryMirror::defaultsChanged, this, replan);
    connect(&d->links, &pw::LinkManager::feedbackRefused, this, [this](const pw::FeedbackPath &path) {
        const QString described = path.describe();
        if (d->reportedLoops.contains(described)) {
            return;
        }
        d->reportedLoops.insert(described);
        Q_EMIT feedbackRefused(described);
    });
    connect(&d->overloadWatch, &QTimer::timeout, this, [this] { d->watchOverloads(); });
    connect(&d->replan, &QTimer::timeout, this, [this] {
        if (!d->running) {
            return;
        }
        const SessionPlan made = d->planFor(d->session);
        if (made == d->plan) {
            return;
        }
        // What went or came back, worked out before the plan is replaced
        // and said once the new one stands.
        const QList<PlanNotice> notices = planNotices(d->session, d->plan, made, d->capturesLost);
        d->plan = made;
        // A layout that changed means the node needs other ports, which is
        // an edit like any other; a count that changed is only news for the
        // interface, and the matches the manager holds still stand.
        if (d->applyPlan(made)) {
            // A session that will not recompile keeps running as it was: the
            // application it could not follow is the one thing that is
            // wrong, and stopping everything else over it would be worse.
            (void)update(d->session);
        } else {
            d->publishLinks();
        }
        for (const PlanNotice &notice : notices) {
            d->tell(notice);
        }
    });
}

SessionRuntime::~SessionRuntime()
{
    stop();
}

std::expected<void, RuntimeError> SessionRuntime::start(const Session &session)
{
    if (d->running) {
        stop();
    }
    if (const std::expected<void, RuntimeError> made = d->reconcileBlocks(session); !made) {
        return std::unexpected(made.error());
    }
    d->ensureTaps();
    // What each source block should be capturing decides how many input
    // ports it gets, so the plan comes before the blocks are prepared and
    // before anything is compiled.
    d->plan = d->planFor(session);
    (void)d->applyPlan(d->plan);
    if (const std::expected<void, RuntimeError> ready = d->prepareBlocks(); !ready) {
        return std::unexpected(ready.error());
    }

    d->sessionKey = session.id.toString(QUuid::WithoutBraces);
    d->syncHosts();
    std::expected<Private::Built, RuntimeError> built = d->build(session, {});
    if (!built) {
        d->hosts.clear();
        return std::unexpected(built.error());
    }

    d->beginRecordings(session);
    d->segments.resize(built->graphs.size());
    for (std::uint32_t segment = 0; segment < d->segments.size(); ++segment) {
        if (const std::expected<void, RuntimeError> started = d->startSegment(segment, session, *built); !started) {
            d->stopSegments();
            d->hosts.clear();
            d->finishRecordings();
            return std::unexpected(started.error());
        }
    }
    d->keep(*built);

    d->session = session;
    d->running = true;
    d->publishLinks();
    d->applyGapLanes(session);
    d->overloadWatch.start();
    return {};
}

void SessionRuntime::stop()
{
    d->replan.stop();
    d->overloadWatch.stop();
    d->reportedLoops.clear();
    d->capturesLost.clear();
    d->deviceNames.clear();
    // The links go before the nodes do: they end on their ports, and a link
    // to a port that is being taken away is a link the server would refuse.
    d->links.setDesiredLinks({});
    d->stopSegments();
    // The hosts go with the session: letting go of one kills it, and its
    // node goes with it.
    d->hosts.clear();
    // **Once no cycle can reach the taps**, so everything the session heard
    // is in the files by the time it is said to have stopped.
    d->finishRecordings();
    // The nodes are gone, and with them the processors and whatever graphs
    // they were holding, so nothing points at the blocks an earlier edit
    // retired, or at a boundary.
    d->retiring.clear();
    d->retiringBlocks.clear();
    d->boundaries.clear();
    d->segmentOf.clear();
    d->split = {};
    d->splitIds.clear();
    d->plan = {};
    d->running = false;
}

bool SessionRuntime::isRunning() const
{
    return d->running;
}

std::expected<void, RuntimeError> SessionRuntime::update(const Session &session)
{
    if (!d->running) {
        // Not running is not an error: the session is remembered and the
        // next start() uses it.
        d->session = session;
        return {};
    }
    ++d->replans;

    if (const std::expected<void, RuntimeError> made = d->reconcileBlocks(session); !made) {
        return std::unexpected(made.error());
    }
    d->ensureTaps();
    d->plan = d->planFor(session);
    (void)d->applyPlan(d->plan);
    if (const std::expected<void, RuntimeError> ready = d->prepareBlocks(); !ready) {
        return std::unexpected(ready.error());
    }

    // A block that has just been switched on or off is faded by the graph,
    // between what it makes of the signal and the signal itself. Its
    // lane must not be dipped as well, so it is left out of the lanes the
    // node fades.
    QList<QString> toggled;
    for (const BlockInstance &block : session.blocks) {
        const auto was = std::ranges::find_if(d->session.blocks.cbegin(),
                                              d->session.blocks.cend(),
                                              [&block](const BlockInstance &old) { return old.id == block.id; });
        if (was != d->session.blocks.cend() && was->enabled != block.enabled) {
            toggled.append(block.id);
        }
    }

    d->syncHosts();
    std::expected<Private::Built, RuntimeError> built = d->build(session, toggled);
    if (!built) {
        return std::unexpected(built.error());
    }

    d->beginRecordings(session);
    const std::size_t before = d->segments.size();
    const std::size_t after = built->graphs.size();
    d->segments.resize(std::max(before, after));
    for (std::uint32_t segment = 0; segment < after; ++segment) {
        if (segment >= before) {
            // **A segment a plugin has just made**, started as a session is.
            if (const std::expected<void, RuntimeError> started = d->startSegment(segment, session, *built); !started) {
                return std::unexpected(started.error());
            }
            continue;
        }
        pw::SessionUpdate edit;
        edit.inputs = built->inputs[segment];
        edit.outputs = built->outputs[segment];
        edit.graph = std::move(built->graphs[segment]);
        for (const std::string &id : graph::changedBlocks(d->segments[segment].spec, built->cut.segments[segment])) {
            const QString blockId = QString::fromStdString(id);
            if (!toggled.contains(blockId)) {
                edit.changedBlocks.append(blockId);
            }
        }
        if (!d->segments[segment].node->update(std::move(edit))) {
            return std::unexpected(RuntimeError::NotCompilable);
        }
    }
    // Segments no plugin cuts off any more.
    for (std::size_t segment = after; segment < before; ++segment) {
        d->segments[segment].node->stop();
    }
    d->segments.resize(after);
    d->endRecordings();
    // **Now, and not before.** Every node has taken its new graph by now —
    // an update does not come back until the audio thread has — so the
    // graphs that pointed at the blocks this edit took out are out of use
    // and they can be freed. Every path that returned early above kept
    // them, because an old graph may still be a live one there.
    d->retiring.clear();
    d->retiringBlocks.clear();
    d->keep(*built);

    d->session = session;
    // The ports the links end on may have just changed, so what is wanted is
    // said again.
    d->publishLinks();
    d->applyGapLanes(session);
    return {};
}

std::expected<ParamEffect, dsp::BlockError>
SessionRuntime::setParam(const QString &blockId, const QString &paramId, const QJsonValue &value)
{
    const std::string wanted = paramId.toStdString();
    std::size_t slot = 0;
    Private::Instance *instance = nullptr;
    for (std::size_t index = 0; index < d->instances.size(); ++index) {
        if (d->instances[index].id == blockId) {
            instance = &d->instances[index];
            slot = index;
            break;
        }
    }
    if (instance == nullptr) {
        return std::unexpected(dsp::BlockError::UnknownParam);
    }

    const std::span<const dsp::ParamDescriptor> params = instance->block->params();
    const auto descriptor =
        std::ranges::find_if(params, [&wanted](const dsp::ParamDescriptor &entry) { return entry.id == wanted; });
    if (descriptor == params.end()) {
        return std::unexpected(dsp::BlockError::UnknownParam);
    }
    // **Nothing is the default.** A session holds only the parameters that
    // were set, so undoing the first turn of a knob asks for a value that
    // was never there; what the block had then is what its descriptor says.
    const std::optional<dsp::ParamValue> parsed = value.isUndefined() || value.isNull()
        ? std::optional<dsp::ParamValue>(descriptor->defaultValue)
        : valueFromJson(*descriptor, value);
    if (!parsed) {
        return std::unexpected(dsp::BlockError::ParamTypeMismatch);
    }
    if (const std::expected<void, dsp::BlockError> set = instance->block->setParam(wanted, *parsed); !set) {
        return std::unexpected(set.error());
    }
    if (isPlugin(instance->type)) {
        // Remembered for a host that gets the block's state later: this goes
        // after it.
        d->setSinceState[blockId].insert(static_cast<std::uint32_t>(std::distance(params.begin(), descriptor)));
    }
    // A source that now keeps its pauses, or leaves them out, changes what
    // the Recorders after it write.
    if (d->running) {
        d->applyGapLanes(d->session);
    }
    // **A Recorder's settings reach the recording that is going on**: a
    // limit turned on applies to it now, and a format or a name from the
    // next file on.
    if (d->running && d->writer && instance->tap && instance->recordingOn) {
        const BlockInstance *block = d->session.findBlock(instance->id);
        const QString name = block != nullptr && !block->nickname.isEmpty() ? block->nickname : instance->id;
        d->writer->start(instance->recordingKey, recordingSettings(*instance->block, d->session.name, name));
    }

    // **A parameter the session plan reads is not queued.** Nothing on the
    // audio thread would read the number — what the change means is other
    // links and other ports — and the slot it would name is one the
    // recompile that is about to follow may give to another block. The
    // block itself has the value already, which is what the recompile then
    // builds the graph around.
    if (!descriptor->flags.automatable) {
        return ParamEffect::NeedsReplan;
    }

    // **A number reaches the audio thread through the queue**, so it takes
    // effect in the next cycle and the block smooths it; nothing is
    // recompiled for a knob being turned. A value that is not a number is
    // one the block applies to itself, above.
    // **Which choice of an enum is taken is a number too**, its
    // position in the list, as ParamStore keeps it: the block that switches
    // on one — the Input Switch starting a crossfade — has to hear it in
    // order with the numbers sent beside it, and a choice written into the
    // block from this thread alone arrives before any of them.
    const std::optional<double> number = audioNumber(*descriptor, *parsed);
    if (!d->running || !number) {
        return ParamEffect::Applied;
    }
    const auto index = static_cast<std::uint32_t>(std::distance(params.begin(), descriptor));
    // **A plugin's parameter goes to its host**, through the ring the host's
    // audio thread reads (ADR-0014); the graph never runs the plugin.
    if (runsInAHost(instance->type, instance->enabled, *instance->block)) {
        const std::optional<std::uint32_t> hostIndex = blocks::PluginBlock::pluginParamIndex(index);
        if (const auto host = d->hosts.find(blockId); host != d->hosts.end() && hostIndex) {
            (void)host->second.process->setParam(*hostIndex, static_cast<float>(*number));
        }
        return ParamEffect::Applied;
    }
    pw::SessionNode *node = d->nodeOfBlock(blockId);
    if (node == nullptr) {
        return ParamEffect::Applied;
    }
    pw::ParamChange change;
    change.target = static_cast<std::uint32_t>(slot);
    change.index = index;
    change.value = static_cast<float>(*number);
    (void)node->queueParam(change);
    return ParamEffect::Applied;
}

std::expected<void, QString> SessionRuntime::controlRecording(const QString &blockId, const QString &action)
{
    if (!d->running || !d->writer) {
        return std::unexpected(u"NotRunning"_s);
    }
    const Private::Instance *instance = d->find(blockId);
    if (instance == nullptr) {
        return std::unexpected(u"blockNotFound"_s);
    }
    if (!instance->tap) {
        return std::unexpected(u"notARecorder"_s);
    }
    if (action == "pause"_L1) {
        d->writer->pause(instance->recordingKey);
    } else if (action == "resume"_L1) {
        d->writer->resume(instance->recordingKey);
    } else if (action == "split"_L1) {
        d->writer->split(instance->recordingKey);
    } else {
        return std::unexpected(u"unknownAction"_s);
    }
    return {};
}

std::uint64_t SessionRuntime::replanCount() const
{
    return d->replans;
}

pw::SessionNode *SessionRuntime::node() const
{
    return d->segments.empty() ? nullptr : d->segments.front().node.get();
}

std::vector<pw::SessionNode *> SessionRuntime::nodes() const
{
    std::vector<pw::SessionNode *> all;
    for (const Private::Segment &segment : d->segments) {
        if (segment.node) {
            all.push_back(segment.node.get());
        }
    }
    return all;
}

std::uint32_t SessionRuntime::latencySamples() const
{
    std::uint32_t longest = 0;
    for (const Private::Segment &segment : d->segments) {
        if (segment.node) {
            longest = std::max(longest, segment.node->telemetry().processLatency + segment.latencyBefore);
        }
    }
    return longest;
}

pw::PluginHostProcess *SessionRuntime::pluginHost(const QString &blockId) const
{
    const auto found = d->hosts.find(blockId);
    return found == d->hosts.end() ? nullptr : found->second.process.get();
}

PluginHealth SessionRuntime::pluginHealth(const QString &blockId) const
{
    const auto found = d->hosts.find(blockId);
    return found == d->hosts.end() ? PluginHealth::NotHosted : found->second.health;
}

std::expected<void, QString> SessionRuntime::restartPlugin(const QString &blockId)
{
    return d->restart(blockId);
}

void SessionRuntime::setPluginRestartWindow(std::chrono::milliseconds window)
{
    d->restartWindow = window;
}

std::vector<SessionRuntime::MeteredBlock> SessionRuntime::meters() const
{
    std::vector<MeteredBlock> metered;
    metered.reserve(d->instances.size());
    for (const Private::Instance &instance : d->instances) {
        metered.push_back(
            MeteredBlock{instance.id, instance.meter.get(), instance.enabled, instance.block->info().latencySamples});
    }
    return metered;
}

QHash<QString, QJsonObject> SessionRuntime::blockState() const
{
    // **What a plugin holds is asked of its host**, so saving writes the
    // plugin as it is now rather than as it was put in.
    for (auto &[blockId, host] : d->hosts) {
        Private::Instance *instance = d->find(blockId);
        if (instance == nullptr || !host.process->isReady()) {
            continue;
        }
        if (const std::optional<QString> now = host.process->stateNow(); now) {
            (void)instance->block->setParam(blocks::PluginBlock::kStateParam,
                                            dsp::ParamValue::ofString(now->toStdString()));
            // Everything set so far is in the state now.
            d->setSinceState.remove(blockId);
        }
    }
    QHash<QString, QJsonObject> state;
    for (const Private::Instance &instance : d->instances) {
        QJsonObject params;
        for (const auto &[id, value] : instance.block->saveState()) {
            params.insert(QString::fromStdString(id), valueToJson(value));
        }
        state.insert(instance.id, params);
    }
    return state;
}

} // namespace rb::core
