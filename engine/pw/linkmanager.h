// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "pw/feedbackguard.h"
#include "pw/pwobjects.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>

namespace rb::pw
{

class Connection;
class RegistryMirror;

/// How a node is recognised, in terms that outlive it.
///
/// A restarted application is a new node with a new id, often the id its last
/// run had ([ADR-0007]), so nothing here is an id. Every field that is set
/// has to match; an empty field is not a condition.
struct EndpointMatch {
    /// node.name, exactly. This is how a device is named in a session file.
    QString nodeName;
    /// application.process.binary, for example "firefox".
    QString applicationBinary;
    QString applicationName;
    /// The Flatpak application id, for a sandboxed application.
    QString flatpakId;
    /// Only a node of this kind, for example a playback stream.
    std::optional<NodeKind> kind;
    /// **Every node of that kind**, rather than one named node. This is how
    /// "everything the system plays" is said; without it a match that names
    /// nothing matches nothing, which is what keeps a mistake quiet.
    bool everyNodeOfKind = false;
    /// Applications to leave out, by the match rule pw::applicationKey()
    /// gives — `binary:firefox` and so on. For a capture of everything with
    /// a few things taken out of it.
    QStringList excludedApplications;

    /// Leave out our own nodes, so a session never captures itself.
    bool excludeOurs = true;
    /// Leave out alert sounds, which nobody means to record.
    bool excludeNotifications = true;
    /// Only a node that is **playing to one of these** at the moment, by
    /// node.name. Empty is not a condition.
    ///
    /// This is how "capture what the application sends to the speakers, but
    /// not what it sends to the headset" is said. **It is not a property of
    /// the node**: where a stream is going is a matter of the links it has
    /// right now, so the manager answers it from the graph and matches()
    /// — which sees only the node — does not. A stream the policy has not
    /// linked anywhere yet is judged by the target it asked for.
    QStringList playingTo;
    /// Leave out streams that are re-playing something already in the graph —
    /// a loopback, a filter chain, a combined sink — because capturing both
    /// them and their source would double the sound.
    bool excludeReplayed = true;

    /// Whether this names any condition at all. An empty match matches
    /// nothing, rather than everything, so a mistake is silent about the
    /// graph rather than loud in it.
    [[nodiscard]] bool isEmpty() const;
    [[nodiscard]] bool matches(const PwNode &node) const;

    [[nodiscard]] bool operator==(const EndpointMatch &other) const = default;
};

/// One end of a wanted link: which node, and which of its channels.
struct LinkEndpoint {
    EndpointMatch node;
    /// audio.channel, for example "FL". Empty takes the node's only port of
    /// that direction, which is what a mono node has.
    QString channel;
    /// port.name, exactly, when a node has more than one port carrying the
    /// same channel and the channel alone would be ambiguous. Our own
    /// session node is such a node: every block with ports has its own FL,
    /// named after the block ([SessionNode::endpoint]). Empty means the
    /// channel decides.
    QString portName;

    [[nodiscard]] bool operator==(const LinkEndpoint &other) const = default;
};

/// A link the engine wants to exist.
///
/// A match may name more than one node — every stream of an application, say
/// — and then the link is wanted to each of them.
struct DesiredLink {
    LinkEndpoint from; ///< the output side
    LinkEndpoint to; ///< the input side
    /// A passive link does not make its nodes drive each other. Spike S1 found
    /// no case in M1 that needs one, so this is off; it is here because the
    /// decision belongs to the caller, not to the manager.
    bool passive = false;

    [[nodiscard]] bool operator==(const DesiredLink &other) const = default;
};

/// Why a link could not be made.
enum class LinkError : std::uint8_t {
    FeedbackLoop, ///< It would feed our own output back into our own input.
    NoOutputPort, ///< No node matched the output side, or it had no such channel.
    NoInputPort, ///< The same on the input side.
    NotConnected, ///< There is no connection to PipeWire at the moment.
    FactoryRefused, ///< The server would not make the link.
};

[[nodiscard]] QString linkErrorName(LinkError error);

/// Makes the links the engine asks for, and keeps them made.
///
/// The engine says **which links should exist**; this makes that true and
/// keeps it true as nodes and ports come and go. It is a reconciler, not a
/// set of event handlers: every registry change leads to the same question,
/// "what is wanted and what is there", so a restart, a move to another sink
/// and a change of channel layout all need no special case ([ADR-0007]).
///
/// **Links follow ports, not nodes.** A stream's ports appear after its node
/// and are made again when it moves to a sink with another layout, so a link
/// is made when the ports it needs are there and not before.
///
/// **A stream the policy moves is let go of first**. To move a stream
/// to a sink with another channel layout the policy lets go of its own link,
/// gives the stream the new layout — remaking its ports — and links it again.
/// A link of ours still on the old ports then goes with them, and the stream
/// can be left suspended where it lands: silent there, and unable to take
/// another layout, so moved back to a stereo output it stays mono, heard on
/// the left only. So:
/// - **Let go of before the move.** When the "default" metadata asks for a
///   stream to be moved, when the default output changes under a stream that
///   follows it, or when the policy lets go of its own link from it, our
///   links from that stream go at once.
/// - **Linked again once it has landed**: once a link of the policy's from it
///   is up, into a node whose channels are the stream's, or setLayoutWait()
///   has gone by for a stream that keeps a layout of its own. A link of ours
///   made while the policy's is still agreeing its format can make the
///   policy's fail.
/// - **Woken if it was left suspended.** The policy can be quicker than we
///   are. A stream we listened to that lands suspended gets a passive link of
///   ours on its new port, which wakes it, and which goes as soon as it runs.
///
/// **It only ever touches its own links.** Links it did not make are left
/// alone, whoever made them; a link of ours is removed by letting go of the
/// proxy that made it. Every link carries `object.linger = false`, so they
/// all disappear if this process is killed, and the captured application
/// carries on playing.
class LinkManager : public QObject
{
    Q_OBJECT

public:
    /// Both must outlive the manager.
    LinkManager(Connection &connection, RegistryMirror &mirror, QObject *parent = nullptr);
    ~LinkManager() override;

    /// Replaces what is wanted, and starts making it so.
    void setDesiredLinks(QList<DesiredLink> links);
    [[nodiscard]] QList<DesiredLink> desiredLinks() const;

    /// How many links the manager has made and still holds.
    [[nodiscard]] int ownedLinkCount() const;
    /// Whether the manager made the link between these two ports.
    [[nodiscard]] bool owns(std::uint32_t outputPort, std::uint32_t inputPort) const;

    /// How long a burst of registry changes is allowed to settle before the
    /// manager acts on it. A default-device change moves many things at once,
    /// and reconciling after each would make links only to unmake them.
    void setSettleTime(std::chrono::milliseconds settle);

    /// How long to wait before trying a link again after the server refused
    /// it, and the longest that wait grows to.
    void setRetryDelays(std::chrono::milliseconds first, std::chrono::milliseconds longest);

    /// The longest a stream the policy is moving goes unlinked by us, waiting
    /// to land. A stream that keeps a layout of its own is linked once
    /// this has gone by.
    void setLayoutWait(std::chrono::milliseconds wait);

    /// Reconciles now rather than after the settling time.
    void reconcileNow();

    /// How many links have been refused, or cut, for closing a loop. For the
    /// diagnostics, and for a test to see that the guard did something.
    [[nodiscard]] int feedbackRefusalCount() const;
    /// How many streams left suspended by a move have been woken.
    [[nodiscard]] int wakeUpCount() const;

    /// Whether a stream the policy is about to move is let go of first (on
    /// by default). Off, a move happens under our links, as it does anyway
    /// when the policy is quicker than we are; a test turns it off to see
    /// such a stream woken.
    void setLetGoBeforeMoves(bool letGo);

Q_SIGNALS:
    /// One pass finished: how many links it made and how many it let go.
    void reconciled(int created, int removed);
    /// A wanted link could not be made. \a detail names what was being
    /// linked, for the log and for the error the facade reports.
    void linkFailed(rb::pw::LinkError error, const QString &detail);

    /// A link was refused, or an existing one cut, because it would have fed
    /// the sound back into itself. The facade reports this as
    /// `routing.feedbackLoop` with the path as its argument, and the rest of
    /// the session carries on.
    void feedbackRefused(const rb::pw::FeedbackPath &path);

public:
    /// The implementation. Named here only so that the file which defines it
    /// can reach it from PipeWire's C callbacks.
    class Private;

private:
    std::unique_ptr<Private> d;
};

} // namespace rb::pw
