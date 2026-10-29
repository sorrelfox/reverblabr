// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/channelmap.h"
#include "graph/compiledgraph.h"
#include "pw/linkmanager.h"
#include "pw/pwobjects.h"
#include "pw/sessionprocessor.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <chrono>
#include <cstdint>
#include <expected>
#include <memory>
#include <vector>

namespace rb::pw
{

class Connection;

/// The ports one block of the session presents to PipeWire.
///
/// A lane is a whole block's worth of ports: a source block's inputs or an
/// output block's outputs, one port per channel position. The layout is what
/// the ports carry, in port order, and the channel mapping to and from the
/// session's stereo bus is the block's own business ([ADR-0007]).
struct SessionLane {
    /// The session's block id, for example "b1". It names the block in the
    /// compiled graph too, which is how a port finds what it carries.
    QString blockId;
    dsp::ChannelLayout layout;
    /// **A tag per channel, for a block that takes audio from two places at
    /// once.** An Application source capturing the microphone as well has a
    /// front left of the application's and a front left of the
    /// microphone's, and a port is named after its channel — so one group
    /// carries a tag and its ports are `<blockId>:in_mic_FL` rather than
    /// `<blockId>:in_FL`.
    ///
    /// Empty, which is every other block, means one group and no tags. When
    /// it is given it is as long as the layout, and an empty entry is the
    /// untagged group.
    QStringList channelRoles;
};

/// What a session node is made of.
struct SessionNodeSpec {
    /// The session's uuid, which the node is named after.
    QString sessionId;
    /// What the user called the session; it becomes the node's description.
    QString sessionName;
    /// Which part of the session this node runs. There is one for now;
    /// out-of-process plugins will later split a session into several.
    std::uint32_t segment = 0;
    /// Blocks that take audio in from PipeWire, and blocks that send it out.
    QList<SessionLane> inputs;
    QList<SessionLane> outputs;
    /// What to ask for as `node.latency`, in frames; 0 leaves the choice to
    /// the server, which is what a session without a latency setting does.
    std::uint32_t latencyFrames = 0;
    /// The rate the latency is expressed against.
    std::uint32_t sampleRate = 48000;
};

/// A compiled graph and what the audio thread needs to run it.
struct SessionGraph {
    std::unique_ptr<graph::CompiledGraph> graph;
    /// The blocks that queued parameter changes address, by slot. The engine
    /// numbers them; ParamChange::target is an index into this.
    std::vector<dsp::Block *> paramTargets;
    /// The rate the blocks in it were prepared for.
    double sampleRate = 48000.0;
    /// **How late the sound already is when it arrives at this node**, for
    /// a segment after a plugin (ADR-0014): the delay the segments and
    /// hosts before it have reported already. The node tells the audio
    /// system its own share only, so that PipeWire, adding up the path,
    /// does not count the same samples twice.
    std::uint32_t latencyBefore = 0;
};

/// An edit, ready to be made audible.
///
/// **The ports and the graph move together**, because the order matters: a
/// port is added before the graph that uses it goes live, and removed only
/// after the graph that used it has gone ([ADR-0008]). Doing it in one call
/// is what keeps that order out of the caller's hands.
struct SessionUpdate {
    /// What the node's ports should be afterwards. A lane that is already
    /// there keeps its ports, and with them its links.
    QList<SessionLane> inputs;
    QList<SessionLane> outputs;
    /// The graph to run against them, compiled around the new lanes.
    SessionGraph graph;
    /// The blocks the edit can be heard through, from
    /// `rb::graph::changedBlocks()`. Their lanes are ramped down, swapped
    /// and ramped back up; every other lane carries on sample for sample.
    ///
    /// **A block being switched on or off does not belong here** if the
    /// graph is already fading it (`NodeSpec::crossfadeSamples`): that fade
    /// goes from one version of the sound to the other without ever dipping,
    /// and dipping the lane around it would undo the point of it.
    QList<QString> changedBlocks;
};

/// Why a session node could not do what it was asked.
enum class SessionNodeError : std::uint8_t {
    NotConnected, ///< There is no connection to PipeWire.
    AlreadyRunning, ///< The node is already started.
    NotRunning, ///< The node has not been started.
    FilterRefused, ///< The server would not take the node.
    PortRefused, ///< The server would not take one of its ports.
    EmptyLayout, ///< A lane with no channels in it, which has no ports.
    TooManyLanes, ///< More lanes than the source-present flags hold.
    NoSuchBlock, ///< A lane names a block the compiled graph does not have.
    PortCountMismatch, ///< The block has fewer ports than the lane's channels.
};

[[nodiscard]] QString sessionNodeErrorName(SessionNodeError error);

/// One running session, as PipeWire sees it: a filter node whose process
/// callback runs the session's compiled graph.
///
/// **The node is the session's place in the graph.** It carries no
/// `media.class`, so PulseAudio clients and the Plasma audio applet do not
/// see it ([ADR-0007]), and it never auto-connects: every link to it is the
/// link manager's doing. It sets `node.always-process`, so meters and
/// generators keep running with nothing linked ([ADR-0008]).
///
/// **The real-time work is in SessionProcessor**, which knows nothing of
/// PipeWire. This class is the shell: it makes the node and its ports, maps
/// each port's buffer before a cycle, and hands graphs over between cycles.
///
/// **A graph is handed over by a blocking invoke on the data loop**, which
/// only swaps pointers. When setGraph() returns, the graph before it is out
/// of use and has been freed on this thread; nothing is ever freed on the
/// audio thread.
///
/// Create it on the engine thread, where the connection lives. Nothing here
/// may be called from the audio thread.
class SessionNode : public QObject
{
    Q_OBJECT

public:
    /// The connection must outlive the node.
    explicit SessionNode(Connection &connection, QObject *parent = nullptr);
    ~SessionNode() override;

    /// Makes the node and its ports, and starts processing. The node has no
    /// graph until setGraph(), and until then its outputs are silent.
    [[nodiscard]] std::expected<void, SessionNodeError> start(const SessionNodeSpec &spec);

    /// Takes the node, its ports and every link to them out of the graph.
    /// Safe to call when it is not running.
    void stop();

    [[nodiscard]] bool isRunning() const;

    /// `node.name`, which is `reverblabr.<session-uuid>.seg<N>`. Empty until
    /// the node has started once; after stop() it is still the name it had,
    /// which is what the log wants.
    [[nodiscard]] QString nodeName() const;

    /// The node's id in the server's graph, or 0 before the server has given
    /// it one. Ids are reused, so nothing that has to mean this node later
    /// should hold it ([ADR-0007]).
    [[nodiscard]] std::uint32_t nodeId() const;

    /// Makes an edit audible: the ports it needs, the graph that uses them,
    /// and a fade over the lanes it can be heard through.
    ///
    /// The lanes it names are ramped down over fadeDuration(), the ports and
    /// the graph are changed while they are silent, and they are ramped back
    /// up. **Lanes the edit does not name are not touched at all**, so a
    /// session being edited in one place goes on sample for sample
    /// everywhere else.
    ///
    /// It blocks for about a fade while the ramp runs, which the engine
    /// thread may do and the GUI thread never does. If the node is not being
    /// scheduled the ramp cannot finish, and it gives up waiting and makes
    /// the edit anyway: silence cannot click.
    [[nodiscard]] std::expected<void, SessionNodeError> update(SessionUpdate edit);

    /// How long a lane is ramped down, and back up, across an edit. The
    /// default is 10 ms. Zero turns fading off, which is for a test that
    /// wants to see what an unfaded edit does.
    void setFadeDuration(std::chrono::milliseconds fade);
    [[nodiscard]] std::chrono::milliseconds fadeDuration() const;

    /// Hands \a graph to the audio thread and frees the one it replaces.
    ///
    /// Blocks until the audio thread has taken it, which is at most one
    /// cycle. The ports it needs must exist already: the order is add the
    /// port, then publish the graph that uses it ([ADR-0008]).
    [[nodiscard]] std::expected<void, SessionNodeError> setGraph(SessionGraph graph);

    /// Whether a graph is live at the moment.
    [[nodiscard]] bool hasGraph() const;

    /// How the link manager is told about one of this node's own ports.
    ///
    /// Two things about such an endpoint are easy to get wrong, and both are
    /// settled here. The manager leaves **our own nodes** out of a match
    /// unless it is told not to, and this node is one of ours. And a session
    /// node has one port per channel **per block**, so the channel alone
    /// does not say which port; the port is named.
    ///
    /// \a role names the group the port belongs to, for a block with more
    /// than one (see SessionLane::channelRoles). Empty — the usual — is the
    /// untagged group.
    [[nodiscard]] LinkEndpoint endpoint(const QString &blockId,
                                        dsp::ChannelPosition channel,
                                        PortDirection direction,
                                        const QString &role = {}) const;

    /// Queues a parameter change for the audio thread. False when the queue
    /// is full, which means the engine is publishing faster than the node is
    /// running: the caller reports it rather than retrying, because a value
    /// that late is already out of date.
    [[nodiscard]] bool queueParam(const ParamChange &change);

    /// What the audio thread reports: cycles, xruns, DSP load, and which
    /// lanes had audio arriving in the last cycle.
    [[nodiscard]] SessionTelemetry telemetry() const;

    /// Starts the peak load figure again, so it means "since you last
    /// looked". The telemetry sampler does this each period.
    void resetPeakLoad();

    /// How often the node is asked whether PipeWire has changed the rate
    /// under it. The default is 50 ms; tests shorten it.
    void setRateCheckInterval(std::chrono::milliseconds interval);

Q_SIGNALS:
    /// The node has started, or stopped. \a running says which.
    void runningChanged(bool running);

    /// PipeWire is running at \a sampleRate and the live graph was prepared
    /// for another one, so the node is putting out silence. The engine
    /// prepares a graph at the new rate and hands it over; nothing is
    /// re-prepared on the audio thread ([ADR-0008]).
    void rateChangeNeeded(quint32 sampleRate);

    /// The server refused the node, or the connection went away under it.
    /// The node has stopped by the time this arrives.
    void failed(rb::pw::SessionNodeError error);

public:
    /// The implementation. Named here only so that the file which defines it
    /// can reach it from PipeWire's C callbacks.
    class Private;

private:
    std::unique_ptr<Private> d;
};

} // namespace rb::pw
