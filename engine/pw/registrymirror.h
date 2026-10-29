// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "pw/pwobjects.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QStringView>

#include <cstdint>
#include <memory>
#include <optional>

namespace rb::pw
{

class Connection;

/// A typed copy of the parts of PipeWire's graph the engine cares about,
/// kept current for as long as the connection lasts.
///
/// Everything the engine asks about the graph — which applications are
/// playing, which sinks exist, what is linked to what, which device is the
/// default — is answered from here rather than from a fresh query, so that an
/// answer is never a round trip and never half of one.
///
/// **It empties and fills again when the connection is remade.** Ids from
/// before a reconnect mean nothing afterwards; a caller that has to recognise
/// the same object across one holds its serial and looks it up again.
///
/// **It reads and never writes.** WirePlumber owns the policy ([ADR-0002]),
/// so the defaults are observed, not set.
///
/// The mirror lives on the thread the connection runs on, which is where
/// PipeWire's events arrive, so its signals are emitted directly and nothing
/// here blocks PipeWire's loop. Anything on another thread — the GUI — reaches
/// it through the engine facade, which is where the queueing belongs.
class RegistryMirror : public QObject
{
    Q_OBJECT

public:
    /// Follows \a connection: fills while it is connected and empties when it
    /// is not. \a connection must outlive the mirror.
    explicit RegistryMirror(Connection &connection, QObject *parent = nullptr);
    ~RegistryMirror() override;

    [[nodiscard]] QList<PwNode> nodes() const;
    [[nodiscard]] QList<PwPort> ports() const;
    [[nodiscard]] QList<PwLink> links() const;
    [[nodiscard]] QList<PwClient> clients() const;
    [[nodiscard]] QList<PwDevice> devices() const;
    [[nodiscard]] PwDefaults defaults() const;
    /// What the `settings` metadata says about the graph's clock.
    [[nodiscard]] PwClockSettings clockSettings() const;

    [[nodiscard]] std::optional<PwNode> node(std::uint32_t id) const;
    [[nodiscard]] std::optional<PwNode> nodeBySerial(std::uint64_t serial) const;
    /// The node with that `node.name`. Names are unique among live nodes.
    [[nodiscard]] std::optional<PwNode> nodeNamed(QStringView name) const;
    [[nodiscard]] std::optional<PwPort> port(std::uint32_t id) const;
    [[nodiscard]] std::optional<PwLink> link(std::uint32_t id) const;
    [[nodiscard]] std::optional<PwClient> client(std::uint32_t id) const;
    [[nodiscard]] std::optional<PwDevice> device(std::uint32_t id) const;

    /// A node's ports in one direction, in the node's own port order.
    [[nodiscard]] QList<PwPort> portsOf(std::uint32_t nodeId, PortDirection direction) const;
    /// Every node of one kind, in id order.
    [[nodiscard]] QList<PwNode> nodesOfKind(NodeKind kind) const;
    /// Whether any link runs from one node to the other.
    [[nodiscard]] bool isLinked(std::uint32_t outputNode, std::uint32_t inputNode) const;
    /// The links that touch a node, in either direction.
    [[nodiscard]] QList<PwLink> linksOf(std::uint32_t nodeId) const;

    /// The node WirePlumber has made the default sink or source, if it is
    /// there. Reads defaults() and looks the name up.
    [[nodiscard]] std::optional<PwNode> defaultSink() const;
    [[nodiscard]] std::optional<PwNode> defaultSource() const;

    /// Where the "default" metadata asks \a nodeId to go — the
    /// `target.object` a volume applet sets to move a stream — or nothing.
    [[nodiscard]] QString moveTarget(std::uint32_t nodeId) const;

    /// The node driving the graph a person is listening to: the
    /// driver of \a following when that is given and has one — the node of
    /// a session of ours that is running — and otherwise the driver of the
    /// default sink, or the default sink itself when it drives its own
    /// graph. Nothing when there is no telling.
    [[nodiscard]] std::optional<PwNode> graphDriver(std::optional<std::uint32_t> following = std::nullopt) const;

    /// How a node is recognised as one of ours. A node is ours when its
    /// client is this process's, when its `application.name` matches, or when
    /// its `node.name` starts with the prefix. The defaults suit the engine;
    /// a test sets its own.
    void setOwnApplicationName(QString name);
    void setOwnNodeNamePrefix(QString prefix);

    /// Whether the first complete picture of the graph is in.
    ///
    /// A registry announcement says an object exists; what it *is* comes from
    /// binding it and waiting for the server to answer. Until ready() the
    /// mirror knows what is there but may not yet know everything about it,
    /// so anything that has to decide from a property waits for this rather
    /// than for a length of time.
    [[nodiscard]] bool isReady() const;

    /// How many objects are held, for a test and for the diagnostics page.
    [[nodiscard]] std::size_t size() const;

Q_SIGNALS:
    void nodeAdded(const rb::pw::PwNode &node);
    /// The server said something new about a node it had already announced.
    void nodeChanged(const rb::pw::PwNode &node);
    void nodeRemoved(std::uint32_t id, std::uint64_t serial);
    /// The server started, stopped or suspended a node (PwNode::state).
    /// Apart from nodeChanged, because it happens all the time and says
    /// nothing about what a node is.
    void nodeStateChanged(const rb::pw::PwNode &node);

    void portAdded(const rb::pw::PwPort &port);
    void portRemoved(std::uint32_t id, std::uint64_t serial);

    void linkAdded(const rb::pw::PwLink &link);
    void linkRemoved(std::uint32_t id, std::uint64_t serial);
    /// A link got further in being set up, or failed (PwLink::state).
    void linkStateChanged(const rb::pw::PwLink &link);

    void clientAdded(const rb::pw::PwClient &client);
    void clientRemoved(std::uint32_t id, std::uint64_t serial);

    void deviceAdded(const rb::pw::PwDevice &device);
    void deviceRemoved(std::uint32_t id, std::uint64_t serial);

    void defaultsChanged(const rb::pw::PwDefaults &defaults);
    /// Somebody asked the policy to move \a nodeId: its `target.object` in
    /// the "default" metadata changed while the mirror was watching.
    /// The policy has not moved it yet; it is about to.
    void moveRequested(std::uint32_t nodeId);
    void clockSettingsChanged(const rb::pw::PwClockSettings &settings);

    /// The first complete picture of the graph is in. Emitted once per
    /// connection, after cleared() when the connection is remade.
    void ready();

    /// The mirror has been emptied because the connection went away. Every id
    /// handed out before this is meaningless.
    void cleared();

public:
    /// The implementation. Named in the header only so that the file which
    /// defines it can reach it from PipeWire's C callbacks, which are free
    /// functions and cannot be members.
    class Private;

private:
    std::unique_ptr<Private> d;
};

} // namespace rb::pw
