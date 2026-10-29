// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "pw/inventory.h"

#include "pw/registrymirror.h"

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace rb::pw
{

namespace
{

/// Sorts by what a person reads, and settles ties with the identifier, so
/// that the same graph always adds up to the same inventory. Without the
/// tie-break two devices called the same thing could swap places between two
/// runs and look like a change.
[[nodiscard]] bool
lessByLabel(const QString &leftLabel, const QString &leftKey, const QString &rightLabel, const QString &rightKey)
{
    const int byLabel = leftLabel.compare(rightLabel, Qt::CaseInsensitive);
    return byLabel != 0 ? byLabel < 0 : leftKey < rightKey;
}

/// Whether no hardware stands behind a device. **`node.virtual` is not
/// enough**: a null sink — which is what `pactl load-module
/// module-null-sink` and a `support.null-audio-sink` in a configuration file
/// both make — does not set it, and those are the virtual outputs people
/// route applications to. The devices Reverblabr makes itself say so with a
/// marker of their own ([ADR-0013]).
[[nodiscard]] bool isVirtualDevice(const PwNode &node)
{
    return node.isVirtual || node.mediaClass == "Audio/Source/Virtual"_L1
        || node.props.value(u"factory.name"_s) == "support.null-audio-sink"_L1
        || node.props.contains(u"reverblabr.virtual-device"_s);
}

// **What a Bluetooth device looks like in the graph**, read off PipeWire's
// SPA headers (`spa/utils/keys.h`, `spa/utils/names.h`) and WirePlumber
// 0.5's Bluetooth scripts (`monitors/bluez/*.lua`,
// `device/autoswitch-bluetooth-profile.lua`), with no headset to try it on:
//
// - the device says `device.api = bluez5`, and each node it makes carries
//   `device.id` and the `api.bluez5.*` properties;
// - for a device with a headset profile, WirePlumber puts a loopback in
//   front of the microphone: an `Audio/Source` called `bluez_input.<address>`
//   with `bluez5.loopback = true` and the device's `device.id`, there in
//   every profile, so that a recording can be linked to it before the
//   microphone exists;
// - the microphone itself, `bluez_input.<address>.<n>`, is made only in a
//   profile that has one (`headset-head-unit`, an A2DP codec with a back
//   channel, `bap-duplex`), with `bluez5.loopback = false`. In a playback
//   profile (`a2dp-sink`) the loopback has nothing behind it.

[[nodiscard]] bool isBluetoothLoopback(const PwNode &node)
{
    return node.props.value(u"bluez5.loopback"_s) == "true"_L1;
}

/// Whether a node belongs to a Bluetooth device, by its own properties or
/// its device's.
[[nodiscard]] bool isWirelessDevice(const PwNode &node, const QHash<std::uint32_t, const PwDevice *> &devices)
{
    const auto bluez = [](const QHash<QString, QString> &props) { return props.value(u"device.api"_s) == "bluez5"_L1; };
    if (bluez(node.props) || isBluetoothLoopback(node) || node.props.contains(u"api.bluez5.address"_s)) {
        return true;
    }
    const PwDevice *device = node.deviceId != 0 ? devices.value(node.deviceId) : nullptr;
    return device != nullptr && bluez(device->props);
}

/// Whether a wireless input is WirePlumber's loopback with no microphone
/// behind it: none of its device's other nodes is a source.
[[nodiscard]] bool isOnPlaybackProfile(const PwNode &node, const QList<PwNode> &nodes)
{
    if (!isBluetoothLoopback(node) || node.deviceId == 0) {
        return false;
    }
    return std::none_of(nodes.cbegin(), nodes.cend(), [&node](const PwNode &other) {
        return other.object.id != node.object.id && other.deviceId == node.deviceId && !isBluetoothLoopback(other)
            && other.mediaClass.startsWith("Audio/Source"_L1);
    });
}

/// What the inventory wants out of a `.desktop` file: its icon, and the
/// names the program it starts goes by.
struct DesktopEntry {
    QString icon;
    /// The program `Exec` and `TryExec` run, without its directory.
    QStringList programs;
    QString windowClass;
    bool hidden = false;
};

/// The program a desktop file's command line runs, without its directory.
/// **`env` is looked through**, because `Exec=env VAR=value program` is how
/// a desktop file sets something up for the program it starts.
[[nodiscard]] QString programOf(const QString &commandLine)
{
    const QStringList words = commandLine.split(u' ', Qt::SkipEmptyParts);
    for (qsizetype index = 0; index < words.size(); ++index) {
        QString word = words.at(index);
        word.remove(u'"');
        if (index == 0 && word == "env"_L1) {
            continue;
        }
        if (word.contains(u'=') && index > 0 && words.at(0) == "env"_L1) {
            continue;
        }
        return word.mid(word.lastIndexOf(u'/') + 1);
    }
    return {};
}

/// Reads the `[Desktop Entry]` group of a `.desktop` file. Hand-parsed rather
/// than given to QSettings, which reads the file as an INI and mangles keys
/// that carry a locale in brackets. The actions below it are other groups,
/// and their Exec and Icon lines are not the application's.
[[nodiscard]] DesktopEntry readDesktopEntry(const QString &path)
{
    DesktopEntry entry;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return entry;
    }
    bool inEntry = false;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.startsWith(u'[')) {
            inEntry = line == "[Desktop Entry]"_L1;
            continue;
        }
        if (!inEntry) {
            continue;
        }
        if (line.startsWith("Icon="_L1)) {
            entry.icon = line.mid(5).trimmed();
        } else if (line.startsWith("Exec="_L1) || line.startsWith("TryExec="_L1)) {
            const QString program = programOf(line.mid(line.indexOf(u'=') + 1));
            if (!program.isEmpty() && !entry.programs.contains(program)) {
                entry.programs.append(program);
            }
        } else if (line.startsWith("StartupWMClass="_L1)) {
            entry.windowClass = line.mid(15).trimmed();
        } else if (line == "Hidden=true"_L1) {
            entry.hidden = true;
        }
    }
    return entry;
}

[[nodiscard]] QString iconInDesktopFile(const QString &path)
{
    return readDesktopEntry(path).icon;
}

/// The icon of the desktop file that starts \a binary, or empty.
///
/// **For a desktop file not named after its program**: Fedora
/// installs Firefox as `org.mozilla.firefox.desktop`, and a growing number
/// of applications name theirs by reverse DNS, so looking a file up by the
/// program's name finds nothing. This reads every desktop file once and
/// keeps what it found, keyed by the directories it read — a test that
/// points XDG_DATA_HOME somewhere else gets the directory it asked for.
[[nodiscard]] QString iconOfProgram(const QString &binary, const QString &applicationName)
{
    static QMutex guard;
    static QString readFrom;
    static QList<DesktopEntry> entries;

    const QStringList directories = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    const QMutexLocker locked(&guard);
    if (const QString key = directories.join(u'\n'); key != readFrom) {
        readFrom = key;
        entries.clear();
        QSet<QString> seen;
        for (const QString &directory : directories) {
            QDirIterator files(directory, {u"*.desktop"_s}, QDir::Files);
            while (files.hasNext()) {
                const QString path = files.next();
                // The first directory to have a file of that name wins, as
                // it does for a desktop.
                if (seen.contains(files.fileName())) {
                    continue;
                }
                seen.insert(files.fileName());
                DesktopEntry entry = readDesktopEntry(path);
                if (!entry.hidden && !entry.icon.isEmpty()) {
                    entries.append(std::move(entry));
                }
            }
        }
    }
    for (const DesktopEntry &entry : std::as_const(entries)) {
        if (!binary.isEmpty() && entry.programs.contains(binary)) {
            return entry.icon;
        }
    }
    for (const DesktopEntry &entry : std::as_const(entries)) {
        if (!entry.windowClass.isEmpty()
            && (entry.windowClass.compare(binary, Qt::CaseInsensitive) == 0
                || entry.windowClass.compare(applicationName, Qt::CaseInsensitive) == 0)) {
            return entry.icon;
        }
    }
    return {};
}

} // namespace

QString applicationKey(const PwNode &node)
{
    return !node.applicationBinary.isEmpty() ? "binary:"_L1 + node.applicationBinary
        : !node.flatpakId.isEmpty()          ? "flatpak:"_L1 + node.flatpakId
        : !node.applicationName.isEmpty()    ? "name:"_L1 + node.applicationName
                                             : "node:"_L1 + node.name;
}

QString streamKindName(StreamKind kind)
{
    switch (kind) {
    case StreamKind::Application:
        return u"application"_s;
    case StreamKind::Replay:
        return u"replay"_s;
    case StreamKind::Alert:
        return u"alert"_s;
    }
    return u"application"_s;
}

int InventoryApp::streamCount() const
{
    return static_cast<int>(streams.size());
}

int InventoryDevice::channelCount() const
{
    return static_cast<int>(channels.size());
}

QString desktopIconName(const QString &flatpakId, const QString &binary, const QString &applicationName)
{
    // Looking an icon up means reading files, and the inventory is worked out
    // again every time the graph moves, so each answer is kept — including
    // the empty one, which is the expensive case: it has searched every
    // directory and found nothing.
    static QMutex guard;
    static QHash<QString, QString> known;
    const QString question = flatpakId + u'\n' + binary + u'\n' + applicationName;
    {
        const QMutexLocker locked(&guard);
        if (const auto found = known.constFind(question); found != known.constEnd()) {
            return *found;
        }
    }

    QStringList candidates;
    if (!flatpakId.isEmpty()) {
        candidates.append(flatpakId + ".desktop"_L1);
    }
    if (!binary.isEmpty()) {
        candidates.append(binary + ".desktop"_L1);
    }
    if (!applicationName.isEmpty()) {
        candidates.append(applicationName.toLower() + ".desktop"_L1);
        candidates.append(applicationName + ".desktop"_L1);
    }

    QString icon;
    for (const QString &candidate : std::as_const(candidates)) {
        const QString path = QStandardPaths::locate(QStandardPaths::ApplicationsLocation, candidate);
        if (path.isEmpty()) {
            continue;
        }
        icon = iconInDesktopFile(path);
        if (!icon.isEmpty()) {
            break;
        }
    }
    // No file named after it: the one that starts it, whatever it is called.
    if (icon.isEmpty()) {
        icon = iconOfProgram(binary, applicationName);
    }
    // A Flatpak names its icon after the application id often enough that the
    // guess is worth making when no desktop file was installed on this side
    // of the sandbox.
    if (icon.isEmpty() && !flatpakId.isEmpty()) {
        icon = flatpakId;
    }

    const QMutexLocker locked(&guard);
    known.insert(question, icon);
    return icon;
}

Inventory inventoryOf(const QList<PwNode> &nodes,
                      const QList<PwPort> &ports,
                      const QList<PwLink> &links,
                      const PwDefaults &defaults,
                      const QList<PwDevice> &devices)
{
    QHash<std::uint32_t, const PwDevice *> devicesById;
    devicesById.reserve(devices.size());
    for (const PwDevice &device : devices) {
        devicesById.insert(device.object.id, &device);
    }

    QHash<std::uint32_t, const PwNode *> byId;
    byId.reserve(nodes.size());
    for (const PwNode &node : nodes) {
        byId.insert(node.object.id, &node);
    }

    // Which nodes each node plays into. Built once: a graph with a hundred
    // streams and a thousand links would otherwise be walked a hundred times.
    QHash<std::uint32_t, QStringList> reaches;
    for (const PwLink &link : links) {
        const auto target = byId.constFind(link.inputNode);
        if (target == byId.constEnd() || (*target)->isOurs) {
            continue;
        }
        QStringList &names = reaches[link.outputNode];
        if (!names.contains((*target)->name)) {
            names.append((*target)->name);
        }
    }

    const auto channelsOf = [&ports](std::uint32_t nodeId, PortDirection direction) {
        QList<const PwPort *> mine;
        for (const PwPort &port : ports) {
            if (port.nodeId == nodeId && port.direction == direction && !port.channel.isEmpty()) {
                mine.append(&port);
            }
        }
        std::sort(mine.begin(), mine.end(), [](const PwPort *left, const PwPort *right) {
            return left->index < right->index;
        });
        QStringList channels;
        channels.reserve(mine.size());
        for (const PwPort *port : std::as_const(mine)) {
            channels.append(port->channel);
        }
        return channels;
    };

    const auto deviceOf = [&](const PwNode &node, PortDirection direction, const QString &defaultName) {
        InventoryDevice device;
        device.name = node.name;
        device.description = node.description.isEmpty() ? node.name : node.description;
        device.nick = node.nick;
        device.mediaClass = node.mediaClass;
        device.channels = channelsOf(node.object.id, direction);
        device.isDefault = !node.name.isEmpty() && node.name == defaultName;
        device.isVirtual = isVirtualDevice(node);
        device.isWireless = isWirelessDevice(node, devicesById);
        device.isOnPlaybackProfile = direction == PortDirection::Output && isOnPlaybackProfile(node, nodes);
        return device;
    };

    Inventory inventory;
    inventory.defaultSink = defaults.audioSink;
    inventory.defaultSource = defaults.audioSource;
    inventory.configuredSink = defaults.configuredAudioSink;
    inventory.configuredSource = defaults.configuredAudioSource;

    QHash<QString, InventoryApp> applications;
    for (const PwNode &node : nodes) {
        if (node.isOurs) {
            continue; // a session is never something a session can pick
        }
        switch (node.kind) {
        case NodeKind::Sink:
            inventory.outputs.append(deviceOf(node, PortDirection::Input, defaults.audioSink));
            // **What applications play into a virtual output can be
            // recorded**, from the sink's monitor ports. The monitor is never
            // the default input, whatever the sink is.
            if (isVirtualDevice(node)) {
                InventoryDevice monitor = deviceOf(node, PortDirection::Output, QString{});
                monitor.isMonitor = true;
                inventory.inputs.append(monitor);
            }
            break;
        case NodeKind::Source:
            // A virtual source's own outputs are flagged as monitor ports
            // ([ADR-0007]), so the direction is all there is to go on here.
            inventory.inputs.append(deviceOf(node, PortDirection::Output, defaults.audioSource));
            // **A virtual microphone can be played to**, through the input
            // ports that feed it. A card's source has none, and one that has
            // none is nothing to play to.
            if (node.mediaClass == "Audio/Source/Virtual"_L1) {
                if (InventoryDevice microphone = deviceOf(node, PortDirection::Input, QString{});
                    !microphone.channels.isEmpty()) {
                    inventory.outputs.append(microphone);
                }
            }
            break;
        case NodeKind::PlaybackStream: {
            InventoryStream stream;
            stream.name = node.name;
            stream.mediaName = node.props.value(u"media.name"_s);
            stream.sinks = reaches.value(node.object.id);
            stream.sinks.sort();
            stream.id = node.object.id;
            stream.serial = node.object.serial;
            // A re-played alert is still a re-play: counting it once, where
            // it starts, is what matters.
            stream.kind = node.isReplayed() ? StreamKind::Replay
                : node.isNotification()     ? StreamKind::Alert
                                            : StreamKind::Application;
            if (stream.kind != StreamKind::Application) {
                inventory.otherStreams.append(stream);
                break;
            }
            const QString key = applicationKey(node);
            InventoryApp &application = applications[key];
            if (application.key.isEmpty()) {
                application.key = key;
                application.displayName = !node.applicationName.isEmpty() ? node.applicationName
                    : !node.applicationBinary.isEmpty()                   ? node.applicationBinary
                    : !node.description.isEmpty()                         ? node.description
                                                                          : node.name;
                application.binary = node.applicationBinary;
                application.flatpakId = node.flatpakId;
                application.iconName = !node.applicationIconName.isEmpty()
                    ? node.applicationIconName
                    : desktopIconName(node.flatpakId, node.applicationBinary, node.applicationName);
            }
            application.streams.append(stream);
            for (const QString &sink : std::as_const(stream.sinks)) {
                if (!application.sinks.contains(sink)) {
                    application.sinks.append(sink);
                }
            }
            break;
        }
        case NodeKind::RecordStream:
        case NodeKind::Other:
            break;
        }
    }

    inventory.applications.reserve(applications.size());
    for (InventoryApp &application : applications) {
        std::sort(application.streams.begin(),
                  application.streams.end(),
                  [](const InventoryStream &left, const InventoryStream &right) { return left.serial < right.serial; });
        application.sinks.sort();
        inventory.applications.append(application);
    }

    std::sort(inventory.applications.begin(),
              inventory.applications.end(),
              [](const InventoryApp &left, const InventoryApp &right) {
                  return lessByLabel(left.displayName, left.key, right.displayName, right.key);
              });
    std::sort(inventory.otherStreams.begin(),
              inventory.otherStreams.end(),
              [](const InventoryStream &left, const InventoryStream &right) { return left.serial < right.serial; });
    const auto byDescription = [](const InventoryDevice &left, const InventoryDevice &right) {
        return lessByLabel(left.description, left.name, right.description, right.name);
    };
    std::sort(inventory.inputs.begin(), inventory.inputs.end(), byDescription);
    std::sort(inventory.outputs.begin(), inventory.outputs.end(), byDescription);
    return inventory;
}

namespace
{

[[nodiscard]] QJsonArray asJsonArray(const QStringList &strings)
{
    QJsonArray array;
    for (const QString &string : strings) {
        array.append(string);
    }
    return array;
}

[[nodiscard]] QJsonObject streamToJson(const InventoryStream &stream)
{
    return QJsonObject{
        {"node"_L1, stream.name},
        {"mediaName"_L1, stream.mediaName},
        {"kind"_L1, streamKindName(stream.kind)},
        {"sinks"_L1, asJsonArray(stream.sinks)},
    };
}

[[nodiscard]] QJsonObject deviceToJson(const InventoryDevice &device)
{
    return QJsonObject{
        {"node"_L1, device.name},
        {"description"_L1, device.description},
        {"nick"_L1, device.nick},
        {"mediaClass"_L1, device.mediaClass},
        {"channels"_L1, asJsonArray(device.channels)},
        {"channelCount"_L1, device.channelCount()},
        {"default"_L1, device.isDefault},
        {"virtual"_L1, device.isVirtual},
        {"monitor"_L1, device.isMonitor},
        {"wireless"_L1, device.isWireless},
        {"playbackProfile"_L1, device.isOnPlaybackProfile},
    };
}

} // namespace

QJsonObject inventoryToJson(const Inventory &inventory)
{
    QJsonArray applications;
    for (const InventoryApp &application : inventory.applications) {
        QJsonArray streams;
        for (const InventoryStream &stream : application.streams) {
            streams.append(streamToJson(stream));
        }
        applications.append(QJsonObject{
            {"key"_L1, application.key},
            {"name"_L1, application.displayName},
            {"icon"_L1, application.iconName},
            {"binary"_L1, application.binary},
            {"flatpakId"_L1, application.flatpakId},
            {"streamCount"_L1, application.streamCount()},
            {"sinks"_L1, asJsonArray(application.sinks)},
            {"streams"_L1, streams},
        });
    }
    QJsonArray otherStreams;
    for (const InventoryStream &stream : inventory.otherStreams) {
        otherStreams.append(streamToJson(stream));
    }
    QJsonArray inputs;
    for (const InventoryDevice &device : inventory.inputs) {
        inputs.append(deviceToJson(device));
    }
    QJsonArray outputs;
    for (const InventoryDevice &device : inventory.outputs) {
        outputs.append(deviceToJson(device));
    }
    return QJsonObject{
        {"applications"_L1, applications},
        {"otherStreams"_L1, otherStreams},
        {"inputs"_L1, inputs},
        {"outputs"_L1, outputs},
        {"defaults"_L1,
         QJsonObject{
             {"sink"_L1, inventory.defaultSink},
             {"source"_L1, inventory.defaultSource},
             {"configuredSink"_L1, inventory.configuredSink},
             {"configuredSource"_L1, inventory.configuredSource},
         }},
    };
}

class InventoryWatcher::Private
{
public:
    Private(InventoryWatcher *watcher, RegistryMirror &mirror)
        : q(watcher)
        , mirror(mirror)
    {
        settle.setSingleShot(true);
    }

    /// Works the inventory out and tells anybody listening, if it says
    /// anything new.
    void announce()
    {
        settle.stop();
        pending = false;
        Inventory now =
            inventoryOf(mirror.nodes(), mirror.ports(), mirror.links(), mirror.defaults(), mirror.devices());
        if (now == current) {
            return;
        }
        current = std::move(now);
        Q_EMIT q->changed(current);
    }

    /// Something in the graph moved. Waits for the burst to go quiet, but
    /// never past kLatest from the first change of the burst.
    void touch()
    {
        if (!pending) {
            pending = true;
            since.start();
        }
        const auto waited = std::chrono::milliseconds(since.elapsed());
        if (waited >= kLatest) {
            announce();
            return;
        }
        settle.start(std::min(kQuiet, kLatest - waited));
    }

    InventoryWatcher *q;
    RegistryMirror &mirror;
    Inventory current;
    QTimer settle;
    QElapsedTimer since;
    bool pending = false;
};

InventoryWatcher::InventoryWatcher(RegistryMirror &mirror, QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>(this, mirror))
{
    connect(&d->settle, &QTimer::timeout, this, [this] { d->announce(); });

    const auto touch = [this] { d->touch(); };
    connect(&mirror, &RegistryMirror::nodeAdded, this, touch);
    connect(&mirror, &RegistryMirror::nodeChanged, this, touch);
    connect(&mirror, &RegistryMirror::nodeRemoved, this, touch);
    connect(&mirror, &RegistryMirror::portAdded, this, touch);
    connect(&mirror, &RegistryMirror::portRemoved, this, touch);
    connect(&mirror, &RegistryMirror::linkAdded, this, touch);
    connect(&mirror, &RegistryMirror::linkRemoved, this, touch);
    connect(&mirror, &RegistryMirror::defaultsChanged, this, touch);
    connect(&mirror, &RegistryMirror::deviceAdded, this, touch);
    connect(&mirror, &RegistryMirror::deviceRemoved, this, touch);
    // The first picture, and the empty inventory a lost connection leaves,
    // are news whether or not a burst is under way.
    connect(&mirror, &RegistryMirror::ready, this, [this] { d->announce(); });
    connect(&mirror, &RegistryMirror::cleared, this, [this] { d->announce(); });
}

InventoryWatcher::~InventoryWatcher() = default;

Inventory InventoryWatcher::inventory() const
{
    return d->current;
}

void InventoryWatcher::refresh()
{
    d->announce();
}

} // namespace rb::pw
