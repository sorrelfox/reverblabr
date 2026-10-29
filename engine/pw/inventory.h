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
#include <QStringList>

#include <chrono>
#include <cstdint>
#include <memory>

class QJsonObject;

namespace rb::pw
{

class RegistryMirror;

/// What a playing stream is, which decides which list it belongs in.
///
/// [ADR-0007] asks the inventory to tell these apart: an application's own
/// stream is something a person recognises and can pick, a re-play is the
/// same audio arriving a second time, and an alert is a notification sound
/// that nobody wants recorded.
enum class StreamKind : std::uint8_t {
    Application, ///< An application playing its own audio.
    Replay, ///< A loopback, filter chain, combined sink or echo canceller.
    Alert, ///< A notification sound.
};

[[nodiscard]] QString streamKindName(StreamKind kind);

/// One `Stream/Output/Audio` node: one thing an application is playing.
struct InventoryStream {
    /// node.name. **Not an identifier**: every stream pw-cat plays is
    /// called `pw-cat`, and two windows of one player share a name as
    /// readily. What tells two streams of one application apart is the
    /// serial, for as long as the connection lasts; what a session file
    /// stores is the application's key, never this.
    QString name;
    /// media.name. A browser puts the tab's title here, which is what makes
    /// per-tab selection possible later; most applications repeat their own
    /// name.
    QString mediaName;
    StreamKind kind = StreamKind::Application;
    /// node.name of every node this stream currently reaches. Usually one
    /// sink; empty while the policy has not linked it anywhere.
    QStringList sinks;

    std::uint32_t id = 0;
    std::uint64_t serial = 0;

    [[nodiscard]] bool operator==(const InventoryStream &other) const = default;
};

/// One application: every stream that belongs to the same program.
struct InventoryApp {
    /// **The match rule**, which is what a session file stores: one of
    /// `binary:<name>`, `flatpak:<app id>`, `name:<application.name>` or
    /// `node:<node.name>`, in that order of preference. Streams that produce
    /// the same rule are the same application.
    QString key;
    QString displayName; ///< What to put in the picker.
    /// An icon name for the theme, from `application.icon-name` or from the
    /// `.desktop` file that matches. Empty when nothing matched.
    QString iconName;
    QString binary; ///< application.process.binary
    QString flatpakId; ///< The portal's app id, when there is one.
    QList<InventoryStream> streams;
    /// node.name of every node the application's streams reach, each once.
    QStringList sinks;

    [[nodiscard]] int streamCount() const;
    [[nodiscard]] bool operator==(const InventoryApp &other) const = default;
};

/// One input or output: a card, a virtual device, a null sink.
struct InventoryDevice {
    QString name; ///< node.name, the stable identifier.
    QString description; ///< What to put in the picker.
    QString nick;
    QString mediaClass;
    /// audio.channel of each of the node's audio ports, in the node's own
    /// port order: `FL`, `FR`, `AUX0`… A port that names no channel is not
    /// an audio port and is not counted.
    QStringList channels;
    bool isDefault = false;
    /// A device no hardware stands behind: a null sink, a virtual
    /// microphone, a loopback.
    bool isVirtual = false;
    /// **An input that is a sink's monitor**: what applications play into a
    /// virtual output, offered for recording. It is the same node as the
    /// output of the same name, read on its output ports.
    bool isMonitor = false;

    [[nodiscard]] int channelCount() const;
    [[nodiscard]] bool operator==(const InventoryDevice &other) const = default;
};

/// The lists the pickers and the blocks draw from: who is playing, what can
/// be recorded, what can be played to, and what the defaults are.
///
/// **Our own nodes are never in it.** A session does not offer itself as a
/// source, and a picker that could choose it would let a person build a loop
/// with two clicks.
struct Inventory {
    /// Applications with at least one stream of their own, by display name.
    QList<InventoryApp> applications;
    /// The streams that are not an application's own: re-plays and alerts.
    /// The System Audio exclusion list is built from these.
    QList<InventoryStream> otherStreams;
    /// Everything that can be recorded from: every source, and **the
    /// monitor of every virtual sink**, which is how a session records what
    /// applications were sent to a virtual output. A card's own monitor is
    /// not offered; nobody routes an application to a sound card in order
    /// to process it.
    QList<InventoryDevice> inputs;
    /// Everything that can be played to: every sink, and **every virtual
    /// microphone**, which is how a session hands what it made to an
    /// application that records.
    QList<InventoryDevice> outputs;

    /// What WirePlumber has actually made the default, by node.name.
    QString defaultSink;
    QString defaultSource;
    /// What the user chose, which differs from the above when the chosen
    /// device is unplugged.
    QString configuredSink;
    QString configuredSource;

    [[nodiscard]] bool operator==(const Inventory &other) const = default;
};

/// The inventory the graph adds up to, worked out from nothing but its
/// arguments.
///
/// Kept apart from the mirror so that it can be checked against a graph
/// written out by hand, without a PipeWire instance to arrange one.
///
/// The one thing it reaches outside its arguments for is the icon: an
/// application that does not name one is looked up among the installed
/// `.desktop` files.
[[nodiscard]] Inventory inventoryOf(const QList<PwNode> &nodes,
                                    const QList<PwPort> &ports,
                                    const QList<PwLink> &links,
                                    const PwDefaults &defaults);

/// The inventory as the facade sends it, and as the GUI reads it.
[[nodiscard]] QJsonObject inventoryToJson(const Inventory &inventory);

/// **The match rule that names the application a node belongs to**: one of
/// `binary:<name>`, `flatpak:<app id>`, `name:<application.name>` or
/// `node:<node.name>`, in that order of preference.
///
/// This is what a session file stores and what an exclusion list holds, so
/// it lives here rather than being worked out twice: the inventory groups by
/// it and a match leaves nodes out by it.
[[nodiscard]] QString applicationKey(const PwNode &node);

/// The icon name for an application, looked up among the installed
/// `.desktop` files by Flatpak id, then by binary, then by name.
///
/// Answers are remembered for as long as the process runs: this reads files,
/// and the inventory is worked out again every time the graph moves.
[[nodiscard]] QString desktopIconName(const QString &flatpakId, const QString &binary, const QString &applicationName);

/// The inventory, kept current while the mirror is.
///
/// **Bursts are gathered up.** A single application starting adds a node, a
/// client, two ports and a link within a few milliseconds, and a card
/// appearing adds dozens; each would otherwise be an inventory of its own
/// sent to the GUI. Changes are held for a short quiet spell and sent as
/// one, and never held longer than kLatest, so that an application appearing
/// is in the lists well inside the 250 ms the GUI asks for.
///
/// **A change that changes no list is not a change.** The mirror reports
/// every property the server revises, most of which — volume, latency,
/// format — the pickers cannot see; the inventory is compared with the one
/// before it and nothing is emitted when they match.
class InventoryWatcher : public QObject
{
    Q_OBJECT

public:
    /// How long a burst has to go quiet before the inventory is sent.
    static constexpr std::chrono::milliseconds kQuiet{50};
    /// How long a burst may hold the inventory back, however busy it is.
    static constexpr std::chrono::milliseconds kLatest{200};

    /// Follows \a mirror, which must outlive the watcher.
    explicit InventoryWatcher(RegistryMirror &mirror, QObject *parent = nullptr);
    ~InventoryWatcher() override;

    /// The inventory as last sent. Cheap: nothing is worked out here.
    [[nodiscard]] Inventory inventory() const;

    /// Works the inventory out now rather than when the burst settles, and
    /// emits changed() if it has. For a caller that has just been asked a
    /// question and cannot wait 50 ms for the answer.
    void refresh();

Q_SIGNALS:
    void changed(const rb::pw::Inventory &inventory);

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace rb::pw

Q_DECLARE_METATYPE(rb::pw::InventoryStream)
Q_DECLARE_METATYPE(rb::pw::InventoryApp)
Q_DECLARE_METATYPE(rb::pw::InventoryDevice)
Q_DECLARE_METATYPE(rb::pw::Inventory)
