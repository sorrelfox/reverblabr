// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "bus/remoteengine.h"

#include "bus/enginebus.h"
#include "core/sharedtelemetry.h"

#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDBusUnixFileDescriptor>
#include <QLoggingCategory>
#include <QMetaMethod>
#include <QSet>
#include <QTimer>
#include <unistd.h>

#include <map>
#include <optional>
#include <utility>
#include <vector>

using namespace Qt::StringLiterals;

Q_LOGGING_CATEGORY(lcRemoteEngine, "reverblabr.remote")

namespace rb::bus
{

namespace
{

/// The calls whose answer lasts — a list, a catalogue, a state — and is
/// worth asking for again of a daemon that has just started. Opening a
/// session is kept apart, since closing one takes it back.
[[nodiscard]] bool isLasting(const QString &method)
{
    static const QSet<QString> kLasting{
        u"ListSessions"_s,
        u"ListPresets"_s,
        u"GetUndoState"_s,
        u"GetSessionState"_s,
        u"GetInventory"_s,
        u"GetBlockCatalogue"_s,
        u"GetAudioSystem"_s,
        u"ListVirtualDevices"_s,
        u"ListRecordings"_s,
        u"ListPlugins"_s,
        u"DescribePlugin"_s,
        u"Analyse"_s,
    };
    return kLasting.contains(method);
}

/// The calls of which only the latest for a session lasts: the one block a
/// session's spectrum is taken from. Asked again in their first form, an
/// earlier block would win over the later one.
[[nodiscard]] bool isLatestPerSession(const QString &method)
{
    return method == "Analyse"_L1;
}

} // namespace

class RemoteEngine::Private
{
public:
    RemoteEngine *q = nullptr;
    QDBusConnection connection;
    QDBusServiceWatcher watcher;
    /// Whether some daemon has had the name while this engine was
    /// listening, and whether one has it now.
    bool everConnected = false;
    bool connected = false;
    /// The sessions this client has been told are open, in the order they
    /// opened.
    QStringList opened;
    /// The lasting calls made, the latest of each (method, arguments), or
    /// of each session for isLatestPerSession(), in the order they were
    /// first made.
    std::vector<std::pair<QString, QVariantList>> lasting;
    /// Asks for everything again a moment after the daemon has gone, which
    /// starts a new one.
    QTimer revive;
    /// The revive has asked already, so the daemon it started need not be
    /// asked twice.
    bool replayed = false;

    /// One session's telemetry as this window reads it (ADR-0018). There is
    /// an entry for every session somebody has asked telemetryOf() about.
    struct Telemetry {
        std::optional<core::SharedTelemetry> mapping;
        /// Which Share() the answer being waited for belongs to; one from
        /// before the session closed or the daemon left is not used.
        quint64 asked = 0;
        bool asking = false;
        /// The daemon said it has no such session, or handed memory that
        /// is not what this build reads: not asked again until the session
        /// opens again, or another daemon has the name.
        bool refused = false;
    };
    std::map<QString, Telemetry> telemetry;
    quint64 shares = 0;

    /// Asks the daemon for \a sessionId's memory, if somebody wants it,
    /// this window has the session open and nothing is mapped or asked yet.
    void askTelemetry(const QString &sessionId)
    {
        const auto found = telemetry.find(sessionId);
        if (found == telemetry.end() || found->second.mapping || found->second.asking || found->second.refused
            || !connected || !opened.contains(sessionId)) {
            return;
        }
        Telemetry &entry = found->second;
        entry.asking = true;
        entry.asked = ++shares;
        QDBusMessage message =
            QDBusMessage::createMethodCall(kServiceName, kObjectPath, kTelemetryInterfaceName, u"Share"_s);
        message.setArguments({sessionId});
        // **Not a reason to start a daemon**: a new one would not have the
        // session, and opening it again is what brings its telemetry back.
        message.setAutoStartService(false);
        auto *watch = new QDBusPendingCallWatcher(connection.asyncCall(message), q);
        QObject::connect(watch,
                         &QDBusPendingCallWatcher::finished,
                         q,
                         [this, sessionId, asked = entry.asked](QDBusPendingCallWatcher *done) {
                             done->deleteLater();
                             const auto still = telemetry.find(sessionId);
                             if (still == telemetry.end() || still->second.asked != asked) {
                                 return;
                             }
                             still->second.asking = false;
                             const QDBusPendingReply<QDBusUnixFileDescriptor> reply = *done;
                             if (reply.isError()) {
                                 qCWarning(lcRemoteEngine) << "no telemetry for" << sessionId << reply.error().name();
                                 still->second.refused = reply.error().name() == kNoTelemetryError;
                                 return;
                             }
                             // The reply keeps its own descriptor; the
                             // mapping takes this one and closes it.
                             auto mapped = core::SharedTelemetry::map(::dup(reply.value().fileDescriptor()));
                             if (!mapped) {
                                 qCWarning(lcRemoteEngine)
                                     << "the telemetry of" << sessionId << "is not memory this build can read";
                                 still->second.refused = true;
                                 return;
                             }
                             still->second.mapping.emplace(std::move(*mapped));
                             Q_EMIT q->telemetryChanged(sessionId);
                         });
    }

    /// **A daemon's memory is not the next one's**: every mapping goes, and
    /// is asked for again once the session is open in the daemon that has
    /// the name now.
    void dropAllTelemetry()
    {
        std::vector<QString> wanted;
        for (const auto &entry : telemetry) {
            wanted.push_back(entry.first);
        }
        for (const QString &sessionId : wanted) {
            dropTelemetry(sessionId, false);
        }
    }

    /// Lets go of \a sessionId's telemetry, telling readers first. With
    /// \a forget false the wish to read it stays, for the daemon that
    /// comes next.
    void dropTelemetry(const QString &sessionId, bool forget)
    {
        const auto found = telemetry.find(sessionId);
        if (found == telemetry.end()) {
            return;
        }
        std::optional<core::SharedTelemetry> old = std::move(found->second.mapping);
        found->second.mapping.reset();
        found->second.asking = false;
        found->second.refused = false;
        found->second.asked = ++shares;
        if (forget) {
            telemetry.erase(found);
        }
        if (old) {
            Q_EMIT q->telemetryChanged(sessionId);
        }
        // `old` is unmapped here, after every reader has been told.
    }

    Private(RemoteEngine *owner, const QDBusConnection &bus)
        : q(owner)
        , connection(bus)
        , watcher(kServiceName, bus, QDBusServiceWatcher::WatchForOwnerChange)
    {
    }

    void call(const QString &method, QVariantList arguments)
    {
        if (isLatestPerSession(method)) {
            std::erase_if(lasting, [&](const auto &entry) {
                return entry.first == method && entry.second.value(0) == arguments.value(0);
            });
            lasting.emplace_back(method, arguments);
        } else if (isLasting(method)) {
            const auto same = std::ranges::find_if(
                lasting, [&](const auto &entry) { return entry.first == method && entry.second == arguments; });
            if (same == lasting.end()) {
                lasting.emplace_back(method, arguments);
            }
        }
        send(method, std::move(arguments));
    }

    void send(const QString &method, QVariantList arguments)
    {
        QDBusMessage message = QDBusMessage::createMethodCall(kServiceName, kObjectPath, kInterfaceName, method);
        message.setArguments(arguments);
        // **The call is what starts the daemon** when nobody has the name.
        message.setAutoStartService(true);
        auto *watch = new QDBusPendingCallWatcher(connection.asyncCall(message), q);
        QObject::connect(watch, &QDBusPendingCallWatcher::finished, q, [this, method](QDBusPendingCallWatcher *done) {
            if (done->isError()) {
                qCWarning(lcRemoteEngine) << method << "failed:" << done->error().name() << done->error().message();
                Q_EMIT q->callFailed(method, done->error().name());
            }
            done->deleteLater();
        });
    }

    /// Opens again what was open and asks again for what lasts.
    void replay()
    {
        for (const QString &sessionId : std::as_const(opened)) {
            send(u"OpenSession"_s, {sessionId});
            // **What it is doing now**: a session that was running is
            // stopped in a daemon that has just started, and the window is
            // told so whether or not it had asked before.
            send(u"GetSessionState"_s, {sessionId});
        }
        for (const auto &[method, arguments] : std::as_const(lasting)) {
            send(method, arguments);
        }
    }

    /// A signal from the daemon, emitted as the facade signal of the same
    /// name.
    void relay(const QDBusMessage &message)
    {
        // **Open is open**: a session this client has is not opened again
        // when a new daemon opens it, or when it is asked for again. The
        // snapshot that follows still comes through, and says what it holds.
        if (message.member() == "SessionOpened"_L1 && !message.arguments().isEmpty()
            && opened.contains(message.arguments().constFirst().toString())) {
            // A daemon that has opened it again has new memory for it.
            askTelemetry(message.arguments().constFirst().toString());
            return;
        }
        const QByteArray name = facadeNameOf(message.member()).toLatin1();
        const QMetaObject &meta = core::EngineFacade::staticMetaObject;
        for (int index = meta.methodOffset(); index < meta.methodCount(); ++index) {
            const QMetaMethod signal = meta.method(index);
            if (signal.methodType() != QMetaMethod::Signal || signal.name() != name) {
                continue;
            }
            QVariantList arguments = message.arguments();
            if (!convertArguments(signal, arguments)) {
                qCWarning(lcRemoteEngine) << "a" << message.member() << "signal with arguments that do not fit";
                return;
            }
            invoke(q, signal, arguments);
            return;
        }
    }
};

RemoteEngine::RemoteEngine(const QDBusConnection &connection, QObject *parent)
    : EngineFacade(parent)
    , d(std::make_unique<Private>(this, connection))
{
    // **Every signal of the interface, by the name the XML gives it**,
    // from whoever has the well-known name — which the bus follows as it
    // passes from one daemon to the next.
    const QMetaObject &meta = core::EngineFacade::staticMetaObject;
    for (int index = meta.methodOffset(); index < meta.methodCount(); ++index) {
        const QMetaMethod signal = meta.method(index);
        if (signal.methodType() == QMetaMethod::Signal) {
            d->connection.connect(kServiceName,
                                  kObjectPath,
                                  kInterfaceName,
                                  busNameOf(signal.name()),
                                  this,
                                  SLOT(relayBusSignal(QDBusMessage)));
        }
    }
    // Which sessions this client has open, for opening them again.
    connect(this, &EngineFacade::sessionOpened, this, [this](const QString &sessionId) {
        if (!d->opened.contains(sessionId)) {
            d->opened.append(sessionId);
        }
        d->askTelemetry(sessionId);
    });
    connect(this, &EngineFacade::sessionClosed, this, [this](const QString &sessionId) {
        d->opened.removeAll(sessionId);
        d->dropTelemetry(sessionId, true);
    });

    d->revive.setSingleShot(true);
    d->revive.setInterval(1000);
    connect(&d->revive, &QTimer::timeout, this, [this] {
        if (!d->connected) {
            d->replay();
            d->replayed = true;
        }
    });
    connect(&d->watcher,
            &QDBusServiceWatcher::serviceOwnerChanged,
            this,
            [this](const QString &, const QString &, const QString &owner) {
                if (owner.isEmpty()) {
                    if (d->connected) {
                        d->connected = false;
                        qCInfo(lcRemoteEngine) << "the engine left the bus";
                        d->dropAllTelemetry();
                        Q_EMIT disconnected();
                        d->revive.start();
                    }
                    return;
                }
                const bool again = d->everConnected;
                // The name can pass straight from one daemon to the next.
                d->dropAllTelemetry();
                d->connected = true;
                d->everConnected = true;
                d->revive.stop();
                Q_EMIT connected();
                if (again) {
                    // The calls the revive made are queued behind the
                    // activation, and reach this daemon without asking again.
                    if (!std::exchange(d->replayed, false)) {
                        qCInfo(lcRemoteEngine) << "a new engine has the bus name; asking for everything again";
                        d->replay();
                    }
                    Q_EMIT restarted();
                }
            });
    if (d->connection.interface() != nullptr && d->connection.interface()->isServiceRegistered(kServiceName).value()) {
        d->connected = true;
        d->everConnected = true;
    }
}

RemoteEngine::~RemoteEngine() = default;

bool RemoteEngine::isConnected() const
{
    return d->connected;
}

const core::TelemetrySlot *RemoteEngine::telemetryOf(const QString &sessionId)
{
    if (sessionId.isEmpty()) {
        return nullptr;
    }
    Private::Telemetry &entry = d->telemetry[sessionId];
    if (entry.mapping) {
        return &entry.mapping->slot();
    }
    d->askTelemetry(sessionId);
    return nullptr;
}

void RemoteEngine::relayBusSignal(const QDBusMessage &message)
{
    d->relay(message);
}

void RemoteEngine::listSessions()
{
    d->call(u"ListSessions"_s, {});
}

void RemoteEngine::createSession(const QString &name)
{
    d->call(u"CreateSession"_s, {name});
}

void RemoteEngine::openSession(const QString &sessionId)
{
    d->call(u"OpenSession"_s, {sessionId});
}

void RemoteEngine::saveSession(const QString &sessionId)
{
    d->call(u"SaveSession"_s, {sessionId});
}

void RemoteEngine::closeSession(const QString &sessionId)
{
    d->call(u"CloseSession"_s, {sessionId});
}

void RemoteEngine::deleteSession(const QString &sessionId)
{
    d->call(u"DeleteSession"_s, {sessionId});
}

void RemoteEngine::exportSession(const QString &sessionId, const QString &path)
{
    d->call(u"ExportSession"_s, {sessionId, path});
}

void RemoteEngine::importSession(const QString &path)
{
    d->call(u"ImportSession"_s, {path});
}

void RemoteEngine::listPresets(const QString &typeId)
{
    d->call(u"ListPresets"_s, {typeId});
}

void RemoteEngine::applyPreset(const QString &sessionId, const QString &blockId, const QString &presetId)
{
    d->call(u"ApplyPreset"_s, {sessionId, blockId, presetId});
}

void RemoteEngine::savePreset(const QString &sessionId, const QString &blockId, const QString &name, bool replace)
{
    d->call(u"SavePreset"_s, {sessionId, blockId, name, replace});
}

void RemoteEngine::renamePreset(const QString &typeId, const QString &presetId, const QString &name)
{
    d->call(u"RenamePreset"_s, {typeId, presetId, name});
}

void RemoteEngine::deletePreset(const QString &typeId, const QString &presetId)
{
    d->call(u"DeletePreset"_s, {typeId, presetId});
}

void RemoteEngine::applyEdit(const QString &sessionId, const QString &editJson)
{
    d->call(u"ApplyEdit"_s, {sessionId, editJson});
}

void RemoteEngine::setParam(const QString &sessionId,
                            const QString &blockId,
                            const QString &paramId,
                            const QString &valueJson)
{
    d->call(u"SetParam"_s, {sessionId, blockId, paramId, valueJson});
}

void RemoteEngine::undo(const QString &sessionId)
{
    d->call(u"Undo"_s, {sessionId});
}

void RemoteEngine::redo(const QString &sessionId)
{
    d->call(u"Redo"_s, {sessionId});
}

void RemoteEngine::getUndoState(const QString &sessionId)
{
    d->call(u"GetUndoState"_s, {sessionId});
}

void RemoteEngine::controlRecording(const QString &sessionId, const QString &blockId, const QString &action)
{
    d->call(u"ControlRecording"_s, {sessionId, blockId, action});
}

void RemoteEngine::restartPlugin(const QString &sessionId, const QString &blockId)
{
    d->call(u"RestartPlugin"_s, {sessionId, blockId});
}

void RemoteEngine::analyse(const QString &sessionId, const QString &blockId, int channel)
{
    d->call(u"Analyse"_s, {sessionId, blockId, channel});
}

void RemoteEngine::start(const QString &sessionId)
{
    d->call(u"Start"_s, {sessionId});
}

void RemoteEngine::stop(const QString &sessionId)
{
    d->call(u"Stop"_s, {sessionId});
}

void RemoteEngine::getSessionState(const QString &sessionId)
{
    d->call(u"GetSessionState"_s, {sessionId});
}

void RemoteEngine::resynchronise(const QString &sessionId)
{
    d->call(u"Resynchronise"_s, {sessionId});
}

void RemoteEngine::getInventory()
{
    d->call(u"GetInventory"_s, {});
}

void RemoteEngine::getBlockCatalogue()
{
    d->call(u"GetBlockCatalogue"_s, {});
}

void RemoteEngine::getAudioSystem()
{
    d->call(u"GetAudioSystem"_s, {});
}

void RemoteEngine::reconnectAudioSystem()
{
    d->call(u"ReconnectAudioSystem"_s, {});
}

void RemoteEngine::getDiagnostics()
{
    d->call(u"GetDiagnostics"_s, {});
}

void RemoteEngine::listVirtualDevices()
{
    d->call(u"ListVirtualDevices"_s, {});
}

void RemoteEngine::createVirtualDevice(const QString &deviceJson)
{
    d->call(u"CreateVirtualDevice"_s, {deviceJson});
}

void RemoteEngine::renameVirtualDevice(const QString &deviceId, const QString &name)
{
    d->call(u"RenameVirtualDevice"_s, {deviceId, name});
}

void RemoteEngine::setVirtualDeviceEnabled(const QString &deviceId, bool enabled)
{
    d->call(u"SetVirtualDeviceEnabled"_s, {deviceId, enabled});
}

void RemoteEngine::deleteVirtualDevice(const QString &deviceId)
{
    d->call(u"DeleteVirtualDevice"_s, {deviceId});
}

void RemoteEngine::reloadVirtualDevices()
{
    d->call(u"ReloadVirtualDevices"_s, {});
}

void RemoteEngine::listRecordings(const QString &sessionId)
{
    d->call(u"ListRecordings"_s, {sessionId});
}

void RemoteEngine::rescanRecordings()
{
    d->call(u"RescanRecordings"_s, {});
}

void RemoteEngine::getRecording(const QString &path)
{
    d->call(u"GetRecording"_s, {path});
}

void RemoteEngine::renameRecording(const QString &path, const QString &name)
{
    d->call(u"RenameRecording"_s, {path, name});
}

void RemoteEngine::setRecordingTags(const QString &path, const QString &tagsJson)
{
    d->call(u"SetRecordingTags"_s, {path, tagsJson});
}

void RemoteEngine::previewRecording(const QString &path)
{
    d->call(u"PreviewRecording"_s, {path});
}

void RemoteEngine::pausePreview()
{
    d->call(u"PausePreview"_s, {});
}

void RemoteEngine::seekPreview(double seconds)
{
    d->call(u"SeekPreview"_s, {seconds});
}

void RemoteEngine::stopPreview()
{
    d->call(u"StopPreview"_s, {});
}

void RemoteEngine::listPlugins()
{
    d->call(u"ListPlugins"_s, {});
}

void RemoteEngine::rescanPlugins(bool retryBlocked)
{
    d->call(u"RescanPlugins"_s, {retryBlocked});
}

void RemoteEngine::describePlugin(const QString &format, const QString &id)
{
    d->call(u"DescribePlugin"_s, {format, id});
}
} // namespace rb::bus
