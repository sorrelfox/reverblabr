// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QHash>
#include <QMetaType>
#include <QString>
#include <QStringView>

#include <cstdint>

namespace rb::pw
{

/// PipeWire's own id for an object, and the serial that never repeats.
///
/// **Ids are reused.** A restarted application can come back with the id its
/// last run had, straight away ([ADR-0007]). Anything that has to mean the
/// same object over time — a saved reference, a link the manager is waiting
/// for — holds the serial. The id is for talking to the server now.
struct PwObjectId {
    std::uint32_t id = 0;
    std::uint64_t serial = 0;

    [[nodiscard]] bool operator==(const PwObjectId &other) const = default;
};

enum class PortDirection : std::uint8_t {
    Input,
    Output,
};

[[nodiscard]] QString portDirectionName(PortDirection direction);

/// What kind of thing a node is, worked out from `media.class` so that
/// callers do not each parse it again.
enum class NodeKind : std::uint8_t {
    Other, ///< Anything with no media.class, our own nodes included.
    Sink, ///< Audio/Sink — something audio can be played to.
    Source, ///< Audio/Source or Audio/Source/Virtual — something to record from.
    PlaybackStream, ///< Stream/Output/Audio — an application playing.
    RecordStream, ///< Stream/Input/Audio — an application recording.
};

[[nodiscard]] QString nodeKindName(NodeKind kind);
[[nodiscard]] NodeKind nodeKindFromMediaClass(QStringView mediaClass);

/// What the server is doing with a node (`pw_node_info.state`).
enum class NodeState : std::uint8_t {
    Unknown, ///< not said yet
    Error,
    Creating,
    Suspended, ///< not scheduled, its formats let go of
    Idle, ///< ready but not scheduled
    Running,
};

/// A node: a device, a virtual device, or an application's stream.
struct PwNode {
    PwObjectId object;
    /// node.name — stable across restarts for a device, which is why a
    /// session file holds this and never an id.
    QString name;
    /// node.description, what a person would call it.
    QString description;
    QString nick; ///< node.nick
    QString mediaClass;
    NodeKind kind = NodeKind::Other;
    QString mediaRole; ///< media.role, for example "Notification"

    QString applicationName; ///< application.name
    QString applicationBinary; ///< application.process.binary
    qint64 applicationPid = -1; ///< application.process.id
    QString applicationIconName; ///< application.icon-name
    /// The Flatpak application id, when the stream came through the portal.
    QString flatpakId;

    /// node.link-group. A re-played stream — a loopback, a filter chain, a
    /// combined sink, an echo canceller — carries one, and capturing it as
    /// well as its source would double the sound ([ADR-0007]).
    QString linkGroup;
    /// event.id. An alert sound carries one, as does media.role Notification.
    QString eventId;
    bool isVirtual = false; ///< node.virtual

    std::uint32_t clientId = 0; ///< client.id, or 0
    std::uint32_t deviceId = 0; ///< device.id, or 0
    /// node.driver-id: the node driving this one's graph, or 0. The
    /// server rewrites it when the node moves to another driver.
    std::uint32_t driverId = 0;
    /// node.driver: whether the node can drive a graph at all.
    bool canDrive = false;
    /// What the server is doing with it, from its info rather than its
    /// properties.
    NodeState state = NodeState::Unknown;

    /// One of ours. Set from the client that owns it and from its name, so
    /// that a caller can leave our own nodes out without knowing how we
    /// name them.
    bool isOurs = false;

    /// Everything the server said about it, for the diagnostics page.
    QHash<QString, QString> props;

    /// Whether this is an alert sound, which a session should not capture.
    [[nodiscard]] bool isNotification() const;
    /// Whether this is a stream that is re-playing something already in the
    /// graph.
    [[nodiscard]] bool isReplayed() const;
};

/// One port of a node. Audio ports carry one channel each.
struct PwPort {
    PwObjectId object;
    std::uint32_t nodeId = 0;
    PortDirection direction = PortDirection::Input;
    /// port.id, the node's own numbering.
    std::uint32_t index = 0;
    QString name; ///< port.name
    /// audio.channel, for example "FL". Empty on a port that is not audio.
    QString channel;
    /// port.monitor. **Not enough on its own to tell a sink's monitor from a
    /// virtual source's outputs**: a virtual source flags its real outputs
    /// this way too ([ADR-0007]).
    bool isMonitor = false;

    QHash<QString, QString> props;
};

/// One link between two ports.
/// Where a link is in being set up (`pw_link_info.state`). A link exists
/// the moment it is asked for; it carries sound once it is Paused or Active,
/// its format and buffers agreed.
enum class LinkState : std::uint8_t {
    Unknown, ///< not said yet
    Error,
    Unlinked,
    Init,
    Negotiating,
    Allocating,
    Paused,
    Active,
};

struct PwLink {
    PwObjectId object;
    std::uint32_t outputNode = 0;
    std::uint32_t outputPort = 0;
    std::uint32_t inputNode = 0;
    std::uint32_t inputPort = 0;
    LinkState state = LinkState::Unknown;

    /// Whether it carries sound: its format and buffers are agreed.
    [[nodiscard]] bool isUp() const
    {
        return state == LinkState::Paused || state == LinkState::Active;
    }
};

/// A client of the server: an application, or us.
struct PwClient {
    PwObjectId object;
    QString applicationName;
    QString applicationBinary;
    qint64 applicationPid = -1;
    bool isUs = false; ///< This process's own connection.
    QHash<QString, QString> props;
};

/// A device: a sound card, a Bluetooth adapter's profile.
struct PwDevice {
    PwObjectId object;
    QString name;
    QString description;
    QString nick;
    QString mediaClass;
    QHash<QString, QString> props;
};

/// What WirePlumber has decided the defaults are.
///
/// We read these and never write them: the policy is WirePlumber's
/// ([ADR-0002]). The `configured` pair is what the user chose; the other is
/// what is actually in use, which differs when the chosen one is unplugged.
/// What PipeWire's `settings` metadata says about the graph's clock:
/// the rate and the quantum it runs at when nothing asks for another, and
/// what somebody has forced them to from outside (`pw-metadata -n settings 0
/// clock.force-quantum 256`). Nought is "not said", or for a forced value
/// "not forced".
struct PwClockSettings {
    std::uint32_t rate = 0; ///< clock.rate
    std::uint32_t quantum = 0; ///< clock.quantum
    std::uint32_t minQuantum = 0; ///< clock.min-quantum
    std::uint32_t maxQuantum = 0; ///< clock.max-quantum
    std::uint32_t forceRate = 0; ///< clock.force-rate
    std::uint32_t forceQuantum = 0; ///< clock.force-quantum

    /// The rate the graph runs at: forced if it is, the setting if not.
    [[nodiscard]] std::uint32_t effectiveRate() const
    {
        return forceRate != 0 ? forceRate : rate;
    }

    /// The same for the quantum.
    [[nodiscard]] std::uint32_t effectiveQuantum() const
    {
        return forceQuantum != 0 ? forceQuantum : quantum;
    }

    [[nodiscard]] bool operator==(const PwClockSettings &other) const = default;
};

struct PwDefaults {
    QString audioSink; ///< default.audio.sink
    QString audioSource; ///< default.audio.source
    QString configuredAudioSink; ///< default.configured.audio.sink
    QString configuredAudioSource; ///< default.configured.audio.source

    [[nodiscard]] bool operator==(const PwDefaults &other) const = default;
};

/// The typed fields of a node, a port and a device, read out of the object's
/// own properties (`props`), as the mirror reads them every time the server
/// says something new about one.
///
/// **Apart from the mirror so that a graph written out by hand is read the
/// same way**: a test loads a pw-dump and gets the objects the mirror would
/// have made. What needs the rest of the graph, a node's client and whether
/// the node is ours, is still the mirror's to fill in.
void readNodeProperties(PwNode &node);
void readPortProperties(PwPort &port);
void readDeviceProperties(PwDevice &device);

} // namespace rb::pw

Q_DECLARE_METATYPE(rb::pw::PwNode)
Q_DECLARE_METATYPE(rb::pw::PwPort)
Q_DECLARE_METATYPE(rb::pw::PwLink)
Q_DECLARE_METATYPE(rb::pw::PwClient)
Q_DECLARE_METATYPE(rb::pw::PwDevice)
Q_DECLARE_METATYPE(rb::pw::PwDefaults)
Q_DECLARE_METATYPE(rb::pw::PwClockSettings)
