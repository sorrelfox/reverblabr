// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/engine.h"

#include "blocks/broadcast.h"
#include "blocks/pluginblock.h"
#include "buildversion.h"
#include "core/audiosystem.h"
#include "core/blockcatalogue.h"
#include "core/broadcasts.h"
#ifdef RB_HAVE_LIVESTREAM
#include "core/livestreams.h"
#endif
#include "core/diagnostics.h"
#include "core/editcommand.h"
#include "core/edithistory.h"
#include "core/engineinfo.h"
#include "core/errorcodes.h"
#include "core/logring.h"
#include "core/pluginscanner.h"
#include "core/preset.h"
#include "core/presetactions.h"
#include "core/presetstore.h"
#include "core/quitapplications.h"
#include "core/recordinglibrary.h"
#include "core/recordingplayer.h"
#include "core/recordings.h"
#include "core/secretstore.h"
#include "core/sessionlisting.h"
#include "core/sessionplan.h"
#include "core/sessionruntime.h"
#include "core/telemetry.h"
#include "core/timerrunner.h"
#include "core/timers.h"
#include "core/virtualdevices.h"
#include "dsp/blockregistry.h"
#include "pw/connection.h"
#include "pw/inventory.h"
#include "pw/pwobjects.h"
#include "pw/registrymirror.h"
#include "pw/sessionnode.h"
#include "pw/virtualdevicemanager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLoggingCategory>
#include <QProcess>
#include <QScopedValueRollback>
#include <QSet>
#include <QTimer>
#include <QUuid>

#include <functional>
#include <map>
#include <optional>
#include <utility>

using namespace Qt::StringLiterals;

Q_STATIC_LOGGING_CATEGORY(lcEngine, "reverblabr.engine.timers")

namespace rb::core
{

namespace
{

[[nodiscard]] QString asText(std::string_view name)
{
    return QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
}

[[nodiscard]] QString asJson(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

[[nodiscard]] QString asJson(const QJsonArray &array)
{
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

} // namespace

class Engine::Private
{
public:
    /// One session the engine is holding.
    struct Open {
        Session session;
        std::unique_ptr<SessionRuntime> runtime;
        QString state = QString(sessionState::idle);
        /// When it last began running, in milliseconds since the epoch, or
        /// nought while it is not running. **The engine's clock, not a
        /// window's**: a window opened an hour later still says when it began.
        qint64 startedAtMs = 0;
        /// Where this session's levels are published. It exists for as long
        /// as the session is open, whether or not it is running, so that an
        /// interface can hold a pointer to it and read it on its own frame
        /// tick without asking anybody anything.
        std::unique_ptr<TelemetrySampler> telemetry = std::make_unique<TelemetrySampler>();
        EditHistory history;
        /// What was last said about the history, so that a drag of a knob
        /// does not send the same answer a hundred times.
        UndoState announced;
        /// The blocks whose plugin's own preset was asked for, and whose
        /// values have not come back yet: what comes is that edit.
        QSet<QString> loadingPreset;
    };

    Engine *q = nullptr;
    const dsp::BlockRegistry &registry;
    SessionStore store;
    /// Where presets live: the ones that ship and the person's own.
    PresetStore presets;
    pw::Connection connection;
    pw::RegistryMirror mirror{connection};
    pw::InventoryWatcher inventoryWatch{mirror};
    /// The virtual devices (ADR-0013): their files, and their nodes in the
    /// graph kept in step with them.
    pw::VirtualDeviceManager virtualDevices{connection, mirror};
    /// Set while a call about virtual devices is being carried out, so that
    /// the manager's own announcements wait for the call's answer:
    /// a created device's id goes out before the list that shows it, and a
    /// call is answered by exactly one list.
    bool answeringVirtualDevices = false;
    /// **The recordings index**: every file a Recorder has written,
    /// kept by the engine so that nothing recorded while no window is open
    /// is missed.
    RecordingLibrary library{defaultLibraryPath()};
    /// **Listening to a recording**: a player of its own on the
    /// shared connection, apart from every session.
    RecordingPlayer player{connection};
    /// The plugins installed on this machine. Scans the first time
    /// somebody asks, not when the engine starts: an engine a test makes
    /// has no business describing the machine's plugins.
    PluginScanner plugins{defaultPluginCachePath()};
    bool pluginsScanned = false;
    /// **std::map, not QHash**: a QHash needs values it can copy, and an
    /// open session owns its runtime.
    std::map<QString, Open> open;
    /// The sessions that have been edited since they were last written.
    QSet<QString> unsaved;
    /// **Running when PipeWire went away**, and to run again once it is back.
    /// A Stop or a close in between takes a session off.
    QSet<QString> resumeWhenConnected;
    /// Set while resumeSessions() starts one, which is not a person's Start.
    bool resuming = false;
    /// The session being stopped because the audio system went, to be
    /// started again once it is back: not a person's Stop either.
    QString restarting;
    /// Armed by an edit, and restarted by the next one, so that a drag of a
    /// knob is one write rather than a hundred.
    QTimer autosave;
    /// What a timer does when its time comes. Made once the engine is.
    std::unique_ptr<TimerRunner> timers;
    /// **Where Broadcast blocks' passwords are kept** (ADR-0026).
    std::unique_ptr<SecretStore> secrets = desktopSecretStore();
    /// What each Broadcast block last said, by its secret key, and whether
    /// the store has a password for it, once that is known: what a window
    /// that asks is told.
    QHash<QString, QJsonObject> broadcastStatus;
    QHash<QString, bool> passwordStored;
    /// **The newest request to the store about each password**, so an
    /// answer that comes after a newer request has been made (a read that
    /// started before a new password was written) is let go rather than
    /// put over the newer one's.
    QHash<QString, quint64> secretRequests;
    /// Why the store could not be asked for a password, while that is
    /// the last word about it: what broadcast.failed then says.
    QHash<QString, QString> secretErrors;

    Private(Engine *owner, const dsp::BlockRegistry &registry, SessionStore store, PresetStore presets)
        : q(owner)
        , registry(registry)
        , store(std::move(store))
        , presets(std::move(presets))
    {
        autosave.setSingleShot(true);
        autosave.setInterval(Engine::kAutosaveDelay);
        QObject::connect(&autosave, &QTimer::timeout, q, [this] { writeUnsaved(); });
    }

    /// Says \a sessionId has been edited, and starts the clock again.
    ///
    /// **Restarted, not left running.** A knob being dragged is an edit every
    /// few milliseconds, and a timer that was only armed once would write the
    /// session in the middle of the gesture and then again at the end.
    void markUnsaved(const QString &sessionId)
    {
        unsaved.insert(sessionId);
        autosave.start();
    }

    /// **Quits the applications \a sessionId captures**, for a timer set to
    /// when its run is over (core/quitapplications.h): a process by the id
    /// the server saw, a Flatpak by the id the portal gave it.
    void quitApplications(const QString &sessionId)
    {
        const Open *session = find(sessionId);
        if (session == nullptr) {
            return;
        }
        for (const QuitTarget &target :
             quitTargets(capturedApplications(session->session, mirror.nodes()), mirror.clients())) {
            if (!target.flatpakId.isEmpty()) {
                qCInfo(lcEngine) << "quitting the Flatpak" << target.flatpakId;
                QProcess::startDetached(u"flatpak"_s, {u"kill"_s, target.flatpakId});
                continue;
            }
            const char *said = "asked to quit";
            switch (quitProcess(target.pid, target.binary)) {
            case QuitResult::Signalled:
                break;
            case QuitResult::Gone:
                said = "gone already";
                break;
            case QuitResult::NotOurs:
                said = "left alone: another user's";
                break;
            case QuitResult::WrongProgram:
                said = "left alone: it isn't running that program";
                break;
            }
            qCInfo(lcEngine) << "quitting" << target.binary << "( process" << target.pid << "):" << said;
        }
    }

    /// Writes one session, if it has anything to write, and says so if it
    /// could not. Takes it off the unsaved list either way: a file that
    /// cannot be written will not start writing because it was asked twice,
    /// and the error has already been reported.
    void write(Open &session)
    {
        const QString id = session.session.id.toString(QUuid::WithoutBraces);
        if (!unsaved.remove(id)) {
            return;
        }
        collectBlockState(session);
        if (const std::expected<void, SessionError> saved = store.save(session.session); !saved) {
            fail(ErrorCode::SessionNotSaved, {session.session.name, asText(sessionErrorName(saved.error()))});
        }
    }

    void writeUnsaved()
    {
        // Taken by value: write() takes each id off the list as it goes, and
        // a report of a failure must not be walking the list it changes.
        const QSet<QString> waiting = unsaved;
        for (const QString &id : waiting) {
            if (Open *session = find(id); session != nullptr) {
                write(*session);
            } else {
                unsaved.remove(id); // closed before the clock ran out
            }
        }
    }

    /// \a blockId, when there is one, says which block the error is about,
    /// so the interface can show it on that block's tile.
    void fail(ErrorCode code, const QStringList &arguments, const QString &blockId = {})
    {
        QJsonObject args;
        if (!blockId.isEmpty()) {
            args.insert(u"block"_s, blockId);
        }
        const ErrorCodeInfo &info = kErrorCodes[static_cast<std::size_t>(code)];
        for (qsizetype index = 0; index < arguments.size() && index < static_cast<qsizetype>(info.arguments.size());
             ++index) {
            args.insert(asText(info.arguments[static_cast<std::size_t>(index)]), arguments.at(index));
        }
        Q_EMIT q->error(asText(info.name), asJson(args));
    }

    [[nodiscard]] Open *find(const QString &sessionId)
    {
        const auto found = open.find(sessionId);
        return found == open.end() ? nullptr : &found->second;
    }

    /// Finds the session, or says there is none and gives back null.
    [[nodiscard]] Open *require(const QString &sessionId)
    {
        Open *session = find(sessionId);
        if (session == nullptr) {
            fail(ErrorCode::SessionNotFound, {sessionId});
        }
        return session;
    }

    /// Says where \a blockId's stream stands, with whether it has a password
    /// when that is known.
    void announceBroadcast(const QString &sessionId, const QString &blockId)
    {
        const QString key = broadcastSecretKey(sessionId, blockId);
        QJsonObject status = broadcastStatus.value(key, QJsonObject{{u"state"_s, u"idle"_s}});
        if (const auto stored = passwordStored.constFind(key); stored != passwordStored.constEnd()) {
            status.insert(u"passwordStored"_s, *stored);
        }
        Q_EMIT q->broadcastStatusChanged(sessionId, blockId, asJson(status));
    }

    /// A new request to the store about \a key: its number, which an
    /// answer has to still be the newest of to count.
    [[nodiscard]] quint64 newSecretRequest(const QString &key)
    {
        return ++secretRequests[key];
    }

    [[nodiscard]] bool isNewest(const QString &key, quint64 request) const
    {
        return secretRequests.value(key) == request;
    }

    /// Asks the store for \a blockId's password, for its stream.
    void readBroadcastPassword(const QString &sessionId, const QString &blockId)
    {
        const QString key = broadcastSecretKey(sessionId, blockId);
        const quint64 request = newSecretRequest(key);
        secrets->read(key, q, [this, sessionId, blockId, key, request](const SecretResult &result) {
            if (!isNewest(key, request)) {
                return;
            }
            Open *open = find(sessionId);
            SessionRuntime *runtime = open != nullptr ? open->runtime.get() : nullptr;
            if (!result.ok) {
                // **Not "no password"**: the store could not be asked, which
                // is said as it is and not remembered, so the next start
                // asks again.
                secretErrors.insert(key, result.error);
                if (runtime != nullptr) {
                    runtime->setBroadcastPasswordUnavailable(blockId);
                }
                return;
            }
            secretErrors.remove(key);
            passwordStored.insert(key, result.secret.has_value());
            if (runtime != nullptr) {
                runtime->setBroadcastPassword(blockId, result.secret);
            }
        });
    }

    /// Removes the password kept under \a key, and everything known of it.
    void forgetBroadcastPassword(const QString &key)
    {
        passwordStored.remove(key);
        broadcastStatus.remove(key);
        secretErrors.remove(key);
        (void)newSecretRequest(key);
        secrets->write(key, QString(), q, [](const SecretResult &) { });
    }

    /// **The passwords of Broadcast blocks that are gone from \a session
    /// for good**: blocks this engine has known a password for in it, and
    /// that are not in it now its history is let go of, so no undo can
    /// bring them back.
    void forgetRemovedBroadcasts(const QString &sessionId, const Session &session)
    {
        const QString prefix = broadcastSecretKey(sessionId, QString());
        const QList<QString> known = passwordStored.keys();
        for (const QString &key : known) {
            if (key.startsWith(prefix) && session.findBlock(key.mid(prefix.size())) == nullptr) {
                forgetBroadcastPassword(key);
            }
        }
    }

    /// Whether \a blockId in \a session is a block that streams with a
    /// secret: a Broadcast block's password, a Live Stream block's key.
    /// **Of a type this build has**: a session made where there is such a
    /// block, opened where there is none, has no secret to keep.
    [[nodiscard]] bool isBroadcast(const Open &session, const QString &blockId) const
    {
        const BlockInstance *block = session.session.findBlock(blockId);
        return block != nullptr && isStreamingType(block->type.toStdString())
            && registry.contains(block->type.toStdString());
    }

    void sendSnapshot(const Open &session)
    {
        const QString id = session.session.id.toString(QUuid::WithoutBraces);
        Q_EMIT q->sessionSnapshot(id, session.session.revision, asJson(sessionToJson(session.session)));
    }

    /// Starts again the sessions PipeWire took down, once the connection is
    /// back. Each starts as it would have from the window, the links to
    /// what it captures coming as those nodes appear.
    void resumeSessions()
    {
        if (!connection.isConnected()) {
            return;
        }
        const QSet<QString> waiting = std::exchange(resumeWhenConnected, {});
        for (const QString &id : waiting) {
            if (find(id) != nullptr) {
                resuming = true;
                q->start(id);
                resuming = false;
            }
        }
    }

    void setState(Open &session, QLatin1StringView state)
    {
        if (state == sessionState::running && session.state != sessionState::running) {
            session.startedAtMs = QDateTime::currentMSecsSinceEpoch();
        } else if (state != sessionState::running) {
            session.startedAtMs = 0;
        }
        session.state = QString(state);
        Q_EMIT q->stateChanged(session.session.id.toString(QUuid::WithoutBraces), session.state);
    }

    /// Puts what the blocks now hold back into the session, so that saving
    /// writes what is being heard rather than what was last typed.
    void collectBlockState(Open &session)
    {
        if (!session.runtime) {
            return;
        }
        const QHash<QString, QJsonObject> state = session.runtime->blockState();
        for (BlockInstance &block : session.session.blocks) {
            const auto found = state.constFind(block.id);
            if (found != state.constEnd()) {
                block.params = *found;
            }
        }
    }

    /// Tells the runtime about a session that has changed, if it is running.
    void refresh(Open &session)
    {
        if (!session.runtime || !session.runtime->isRunning()) {
            return;
        }
        if (const std::expected<void, RuntimeError> updated = session.runtime->update(session.session); !updated) {
            fail(ErrorCode::SessionCannotRun, {session.session.name, runtimeErrorName(updated.error())});
        }
    }

    /// Says what undo and redo would do, when that has changed. \a always
    /// says it whatever it is, for a client that has just asked.
    void announceUndo(Open &session, bool always = false)
    {
        const UndoState now = UndoState::of(session.history);
        if (!always && now == session.announced) {
            return; // a knob being dragged does not change the answer
        }
        session.announced = now;
        Q_EMIT q->undoStateChanged(
            session.session.id.toString(QUuid::WithoutBraces), now.canUndo, now.undoCode, now.canRedo, now.redoCode);
    }

    /// Sends a parameter to the block that is running, if the command is
    /// one and a session is.
    ///
    /// **A parameter is not part of the graph.** It goes to the block
    /// itself, because a block that is already running keeps the state it
    /// has rather than being built again from the session, so a recompile
    /// would not carry it. Without this an edit or an undo would be right in
    /// the session and wrong in the sound.
    ///
    /// A value of nothing is how undo asks for a parameter that had never
    /// been set, and the runtime reads it as the block's own default.
    ///
    /// Whether the parameter was a structural one is not asked here: every
    /// caller of this is a step of commands that refreshes once when it has
    /// applied them all, and a replan in the middle of a step would be a
    /// recompile per command.
    void pushParam(Open &session, const EditCommand &command)
    {
        if (!session.runtime) {
            return;
        }
        const auto push = [this, &session](const QString &blockId, const QString &key, const QJsonValue &value) {
            if (const std::expected<ParamEffect, dsp::BlockError> set = session.runtime->setParam(blockId, key, value);
                !set) {
                fail(ErrorCode::EditRefused, {asText(dsp::blockErrorName(set.error()))});
            }
        };
        if (const auto *param = std::get_if<SetParam>(&command); param != nullptr) {
            push(param->blockId, param->key, param->value);
            return;
        }
        // **A preset reaches the running block too**, every value it sets
        // and every one it clears (nothing, which is the default). Which
        // plugin first, then its state, then the rest: a plugin block's
        // settings mean something only for its plugin, and the values
        // after a state are held back until it is in.
        const auto *preset = std::get_if<ApplyPreset>(&command);
        if (preset == nullptr) {
            return;
        }
        QStringList keys = preset->params.keys();
        std::ranges::stable_partition(keys, [](const QString &key) { return key == "plugin"_L1 || key == "state"_L1; });
        if (keys.size() > 1 && keys.at(0) == "state"_L1 && keys.at(1) == "plugin"_L1) {
            keys.swapItemsAt(0, 1);
        }
        for (const QString &key : keys) {
            push(preset->blockId, key, preset->params.value(key));
        }
        for (const QString &key : preset->clear) {
            push(preset->blockId, key, QJsonValue{QJsonValue::Null});
        }
    }

    /// **Values from a plugin's own side**, into the session: the plugin
    /// and the block have them already, so nothing is sent back to the
    /// host, but every client is told, one patch per command. \a edit puts
    /// them on the undo stack as one step, or into the gesture under way —
    /// a control dragged in the plugin's window; otherwise they only follow
    /// what something else did (a state put back) and are nobody's edit.
    void takeFromPlugin(Open &session, const std::vector<EditCommand> &commands, bool edit)
    {
        if (commands.empty()) {
            return;
        }
        const QString id = session.session.id.toString(QUuid::WithoutBraces);
        const std::uint64_t startedAt = session.session.revision;
        Session trial = session.session;
        const std::expected<EditInverse, EditBatchError> applied = applyEdits(trial, commands);
        if (!applied) {
            return; // a block the session no longer has
        }
        session.session = std::move(trial);
        if (edit) {
            if (commands.size() == 1) {
                session.history.record(commands.front(), *applied);
            } else {
                session.history.record(commands, *applied);
            }
            announceUndo(session);
        }
        markUnsaved(id);
        for (std::size_t index = 0; index < commands.size(); ++index) {
            Q_EMIT q->sessionPatch(id, startedAt + index + 1, asJson(editCommandToJson(commands[index])));
        }
    }

    /// Applies the commands of a step and tells every client, one patch per
    /// command, as though a person had made each edit.
    ///
    /// **A step that will not apply means the history is wrong about the
    /// session**, which nothing here can put right, so it is thrown away
    /// rather than left to mislead the next Ctrl+Z.
    bool applyStep(Open &session, const std::vector<EditCommand> &commands)
    {
        const QString id = session.session.id.toString(QUuid::WithoutBraces);
        for (const EditCommand &command : commands) {
            const std::expected<EditInverse, EditError> applied = core::applyEdit(session.session, command);
            if (!applied) {
                fail(ErrorCode::EditRefused, {asText(editErrorName(applied.error()))});
                session.history.clear();
                return false;
            }
            pushParam(session, command);
            Q_EMIT q->sessionPatch(id, session.session.revision, asJson(editCommandToJson(command)));
        }
        refresh(session);
        return true;
    }

    [[nodiscard]] QString inventoryJson() const
    {
        return asJson(pw::inventoryToJson(inventoryWatch.inventory()));
    }

    /// The virtual devices, as the facade sends them.
    void publishVirtualDevices()
    {
        Q_EMIT q->virtualDevicesChanged(
            virtualDevicesMessage(virtualDevices.devices(),
                                  virtualDevices.scopeError(pw::VirtualDeviceScope::User),
                                  virtualDevices.scopeError(pw::VirtualDeviceScope::System)));
    }

    /// What to call a device in a refusal: its name, or the id it was asked
    /// by when there is no such device.
    [[nodiscard]] QString virtualDeviceName(const QString &deviceId) const
    {
        const std::optional<pw::VirtualDeviceState> state = virtualDevices.device(deviceId);
        return state ? state->device.name : deviceId;
    }

    /// Carries out one change to a device and answers it: the list when it
    /// was made, device.virtualRefused with nothing changed when it was not.
    void changeVirtualDevice(const QString &deviceId,
                             const std::function<std::expected<void, pw::VirtualDeviceError>()> &change)
    {
        // The name before the change, for a refusal to be about.
        const QString name = virtualDeviceName(deviceId);
        std::expected<void, pw::VirtualDeviceError> changed;
        {
            const QScopedValueRollback answering(answeringVirtualDevices, true);
            changed = change();
        }
        if (!changed) {
            fail(ErrorCode::VirtualDeviceRefused, {name, virtualDeviceReason(changed.error())});
            return;
        }
        publishVirtualDevices();
    }

    /// What the connection and the graph say about the audio system.
    ///
    /// **The session manager is looked up rather than assumed.** It is a
    /// client of the server like any other, and the one place it says what
    /// it is and which version it is, is its own client object.
    [[nodiscard]] AudioSystemInfo audioSystemNow() const
    {
        AudioSystemInfo info;
        info.state = pw::connectionStateName(connection.state());
        // The engine is a libpipewire client by construction, so this is
        // knowledge and not a guess; the version is the server's own word.
        info.serverName = u"PipeWire"_s;
        info.serverVersion = connection.serverVersion();
        for (const pw::PwClient &client : mirror.clients()) {
            const bool isSessionManager = client.props.value(u"wireplumber.daemon"_s) == "true"_L1
                || client.applicationName.compare(u"WirePlumber"_s, Qt::CaseInsensitive) == 0;
            if (isSessionManager) {
                // Its own spelling of its own name, not ours.
                info.sessionManagerName = client.applicationName;
                info.sessionManagerVersion = client.props.value(u"application.version"_s);
                break;
            }
        }
        // **The graph's clock and its driver, running or not**. The
        // driver followed is the one of a session of ours, when one is in
        // a graph, because that is the graph whose figures the status bar
        // is showing; otherwise the default sink's.
        std::optional<std::uint32_t> ours;
        for (const pw::PwNode &node : mirror.nodes()) {
            if (node.isOurs && node.driverId != 0) {
                ours = node.object.id;
                break;
            }
        }
        if (const std::optional<pw::PwNode> driver = mirror.graphDriver(ours)) {
            info.driver = !driver->description.isEmpty() ? driver->description
                : !driver->nick.isEmpty()                ? driver->nick
                                                         : driver->name;
        }
        const pw::PwClockSettings clock = mirror.clockSettings();
        info.rate = static_cast<int>(clock.effectiveRate());
        info.quantum = static_cast<int>(clock.effectiveQuantum());
        return info;
    }

    /// Says so when it has changed. **Only when**: the graph revises client
    /// objects often, and a signal per revision saying the same thing would
    /// rebind everything in the interface that reads it.
    void publishAudioSystem(bool always = false)
    {
        const AudioSystemInfo now = audioSystemNow();
        if (!always && now == audioSystem) {
            return;
        }
        audioSystem = now;
        Q_EMIT q->audioSystemChanged(audioSystemToJson(audioSystem));
    }

    /// What was last said about it, so that the same thing is not said
    /// twice.
    AudioSystemInfo audioSystem;
};

Engine::Engine(const dsp::BlockRegistry &registry, SessionStore store, PresetStore presets, QObject *parent)
    : EngineFacade(parent)
    , d(std::make_unique<Private>(this, registry, std::move(store), std::move(presets)))
{
    // **Before anything records again**: a recording a crash cut off plays
    // up to its last header update, and after this to its end.
    (void)repairInterruptedRecordings(recordingJournalFolder());
    // And then the index is checked against the disk, so the files just
    // repaired are read back at their whole length, and anything deleted
    // while the engine was not running leaves the list.
    // **What a plugin block asks about its plugin**, answered from
    // the plugin cache: the engine never loads a plugin to find out.
    plugin::setPluginInfoSource(
        [this](std::string_view format, std::string_view id) -> std::optional<plugin::PluginDescription> {
            return d->plugins.describe(QString::fromUtf8(format.data(), static_cast<qsizetype>(format.size())),
                                       QString::fromUtf8(id.data(), static_cast<qsizetype>(id.size())));
        });
    const auto announcePlugins = [this] {
        Q_EMIT pluginsChanged(asJson(
            pluginsJson(d->plugins.plugins(), d->plugins.blocked(), d->plugins.isScanning(), d->plugins.incomplete())));
    };
    connect(&d->plugins, &PluginScanner::scanningChanged, this, announcePlugins);
    connect(&d->plugins, &PluginScanner::changed, this, announcePlugins);
    connect(&d->library, &RecordingLibrary::changed, this, [this](const QString &sessionId) {
        Q_EMIT recordingsChanged(sessionId, asJson(d->library.recordings(sessionId)));
    });
    d->library.rescan();

    d->timers = std::make_unique<TimerRunner>(
        this,
        [this] { return sessionListing(); },
        [this](const QString &sessionId) { d->quitApplications(sessionId); },
        [this](const QString &sessionId) { return d->restarting == sessionId; });
    connect(d->timers.get(), &TimerRunner::fired, this, &EngineFacade::timerFired);
    connect(d->timers.get(), &TimerRunner::notFound, this, [this](const QString &timerId) {
        d->fail(ErrorCode::TimerNotFound, {timerId});
    });
    connect(d->timers.get(), &TimerRunner::late, this, [this](const QString &timerId) {
        d->fail(ErrorCode::TimerLate, {timerId});
    });

    connect(&d->player, &RecordingPlayer::changed, this, [this](const QJsonObject &status) {
        Q_EMIT previewChanged(asJson(status));
    });
    connect(&d->player, &RecordingPlayer::failed, this, [this](const QString &path, const QString &reason) {
        d->fail(ErrorCode::RecordingPreviewFailed, {path, reason});
    });

    // The watcher decides when the graph has said something the lists can
    // show; everything else the server revises is none of the GUI's business.
    connect(&d->inventoryWatch, &pw::InventoryWatcher::changed, this, [this](const pw::Inventory &inventory) {
        Q_EMIT inventoryChanged(asJson(pw::inventoryToJson(inventory)));
    });

    // **The connection tells the interface what it can expect.** A window
    // that has to ask every second whether PipeWire is there is a window
    // that finds out a second late; this way the banner appears as the
    // connection goes and is gone as it comes back.
    connect(&d->connection, &pw::Connection::stateChanged, this, [this] {
        d->publishAudioSystem();
        d->resumeSessions();
    });
    // The session manager is a client of the server like any other, so what
    // it is and which version it is arrive with the rest of the graph.
    connect(&d->mirror, &pw::RegistryMirror::clientAdded, this, [this] { d->publishAudioSystem(); });
    connect(&d->mirror, &pw::RegistryMirror::clientRemoved, this, [this] { d->publishAudioSystem(); });
    // The clock and the driver move while the window is open: a forced
    // quantum, a device unplugged and another taking over the graph.
    connect(&d->mirror, &pw::RegistryMirror::clockSettingsChanged, this, [this] { d->publishAudioSystem(); });
    connect(&d->mirror, &pw::RegistryMirror::defaultsChanged, this, [this] { d->publishAudioSystem(); });
    connect(&d->mirror, &pw::RegistryMirror::nodeAdded, this, [this] { d->publishAudioSystem(); });
    connect(&d->mirror, &pw::RegistryMirror::nodeChanged, this, [this] { d->publishAudioSystem(); });
    connect(&d->mirror, &pw::RegistryMirror::nodeRemoved, this, [this] { d->publishAudioSystem(); });

    // **The devices say when they change**: a node PipeWire has made, one
    // that has gone, a connection that came back. While a call is being
    // answered, the call's own answer says it instead.
    connect(&d->virtualDevices, &pw::VirtualDeviceManager::devicesChanged, this, [this] {
        if (!d->answeringVirtualDevices) {
            d->publishVirtualDevices();
        }
    });
    connect(&d->virtualDevices,
            &pw::VirtualDeviceManager::nodeRefused,
            this,
            // The detail is for the log, where the manager has put it.
            [this](const QString &deviceId, const QString &) {
                d->fail(ErrorCode::VirtualDeviceNodeRefused, {d->virtualDeviceName(deviceId)});
            });
}

Engine::~Engine()
{
    plugin::setPluginInfoSource({});
    // **What was edited a moment ago is still written.** The autosave clock
    // does not survive the engine, so this is the last chance for an edit
    // made inside the final second.
    d->writeUnsaved();
    for (auto &[id, session] : d->open) {
        session.runtime.reset();
    }
}

void Engine::connectToPipeWire()
{
    d->connection.start();
}

bool Engine::isConnected() const
{
    return d->connection.isConnected();
}

const TelemetrySlot *Engine::telemetryOf(const QString &sessionId) const
{
    Private::Open *session = d->find(sessionId);
    return session == nullptr ? nullptr : &session->telemetry->slot();
}

int Engine::shareTelemetry(const QString &sessionId) const
{
    Private::Open *session = d->find(sessionId);
    return session == nullptr ? -1 : session->telemetry->shareReadOnly();
}

pw::VirtualDeviceManager &Engine::virtualDevices()
{
    return d->virtualDevices;
}

pw::Connection &Engine::connection()
{
    return d->connection;
}

SessionRuntime *Engine::runtimeOf(const QString &sessionId) const
{
    Private::Open *session = d->find(sessionId);
    return session == nullptr ? nullptr : session->runtime.get();
}

void Engine::listSessions()
{
    Q_EMIT sessionsListed(sessionListing());
}

QString Engine::sessionListing() const
{
    QJsonArray sessions;
    for (const QUuid &id : d->store.list()) {
        const std::expected<Session, SessionLoadFailure> loaded = d->store.load(id);
        if (!loaded) {
            continue; // a file that will not open is not a session to offer
        }
        // **Where each stands**, so a client can say what runs without
        // asking each session and hearing session.notFound for every one
        // that is only on disk: the Error signal reaches every
        // window, and a script's question would put a banner in each.
        //
        // **An open session is described as it is now**, not as its file
        // last said: its blocks and its name may have moved on since.
        const QString sessionId = loaded->id.toString(QUuid::WithoutBraces);
        const Private::Open *open = d->find(sessionId);
        const int recordings = d->library.recordingCount(sessionId);
        sessions.append(open != nullptr ? sessionListEntry(open->session, open->state, recordings, open->startedAtMs)
                                        : sessionListEntry(*loaded, QString(sessionState::closed), recordings, 0));
    }
    return QString::fromUtf8(QJsonDocument(sessions).toJson(QJsonDocument::Compact));
}

void Engine::createSession(const QString &name)
{
    Private::Open session;
    session.session.id = QUuid::createUuid();
    session.session.name = name;
    const QString id = session.session.id.toString(QUuid::WithoutBraces);

    if (const std::expected<void, SessionError> saved = d->store.save(session.session); !saved) {
        d->fail(ErrorCode::SessionNotSaved, {name, asText(sessionErrorName(saved.error()))});
        return;
    }
    d->open.emplace(id, std::move(session));
    Q_EMIT sessionOpened(id);
    d->sendSnapshot(d->open.at(id));
}

void Engine::openSession(const QString &sessionId)
{
    if (d->find(sessionId) != nullptr) {
        // **Already open, and said so again in full**, as the interface
        // promises: a window that has just started, while the daemon kept
        // the session open for it, only learns it has a tab from this.
        Q_EMIT sessionOpened(sessionId);
        d->sendSnapshot(*d->find(sessionId));
        return;
    }
    const QUuid id(sessionId);
    if (id.isNull()) {
        d->fail(ErrorCode::SessionNotFound, {sessionId});
        return;
    }
    std::expected<Session, SessionLoadFailure> loaded =
        d->store.load(id, [this](QStringView type) { return d->registry.contains(type.toString().toStdString()); });
    if (!loaded) {
        if (!loaded.error().backupPath.isEmpty()) {
            d->fail(ErrorCode::SessionCorruptFile, {d->store.pathFor(id), loaded.error().backupPath});
        } else {
            d->fail(ErrorCode::SessionNotFound, {sessionId});
        }
        return;
    }
    // A block type this build does not have is kept, and said so once.
    for (const BlockInstance &block : loaded->blocks) {
        if (block.unknownType) {
            d->fail(ErrorCode::BlockUnknownType, {block.type});
        }
    }

    Private::Open session;
    session.session = std::move(*loaded);
    d->open.emplace(sessionId, std::move(session));
    Q_EMIT sessionOpened(sessionId);
    d->sendSnapshot(d->open.at(sessionId));
}

void Engine::saveSession(const QString &sessionId)
{
    Private::Open *session = d->require(sessionId);
    if (session == nullptr) {
        return;
    }
    // **Asked for, so written whether or not anything changed.** Autosave is
    // what keeps a session safe; this is a caller saying "now", and
    // answering "there was nothing to do" would make it wonder.
    d->unsaved.insert(sessionId);
    d->write(*session);
}

void Engine::flushAutosave()
{
    d->writeUnsaved();
}

bool Engine::hasUnsavedChanges() const
{
    return !d->unsaved.isEmpty();
}

void Engine::closeSession(const QString &sessionId)
{
    Private::Open *session = d->require(sessionId);
    if (session == nullptr) {
        return;
    }
    d->resumeWhenConnected.remove(sessionId);
    session->telemetry->watch(nullptr);
    session->telemetry->stop();
    if (session->runtime) {
        session->runtime->stop();
        session->runtime.reset();
    }
    // **Before it is let go of, not a second later.** The autosave clock may
    // have most of a second left on it, and after this there is no session
    // to write.
    d->write(*session);
    // Its history goes with it, so a Broadcast block taken out can no
    // longer come back with an undo, and its password has no use.
    d->forgetRemovedBroadcasts(sessionId, session->session);
    d->open.erase(sessionId);
    Q_EMIT sessionClosed(sessionId);
}

void Engine::deleteSession(const QString &sessionId)
{
    const QUuid id(sessionId);
    // **Not written on the way out.** Closing writes what is waiting, and
    // writing a file about to be deleted is at best wasted and at worst a
    // file that comes back.
    d->unsaved.remove(sessionId);
    // **Its passwords go with it**: a secret nothing can use any more is
    // one more thing in the store for nothing.
    std::expected<Session, SessionLoadFailure> doomed = d->find(sessionId) != nullptr
        ? std::expected<Session, SessionLoadFailure>(d->find(sessionId)->session)
        : d->store.load(id);
    if (doomed) {
        for (const BlockInstance &block : doomed->blocks) {
            if (isStreamingType(block.type.toStdString())) {
                d->forgetBroadcastPassword(broadcastSecretKey(sessionId, block.id));
            }
        }
    }
    if (d->find(sessionId) != nullptr) {
        closeSession(sessionId);
    }
    if (const std::expected<void, SessionError> removed = d->store.remove(id); !removed) {
        d->fail(ErrorCode::SessionNotFound, {sessionId});
    }
}

void Engine::exportSession(const QString &sessionId, const QString &path)
{
    Private::Open *session = d->require(sessionId);
    if (session == nullptr) {
        return;
    }
    // **Saved first.** What is exported is what is on the screen, not what
    // the autosave clock last got round to writing.
    d->unsaved.insert(sessionId);
    d->write(*session);
    if (const std::expected<void, SessionError> saved = d->store.saveFile(session->session, path); !saved) {
        d->fail(ErrorCode::SessionNotSaved, {session->session.name, asText(sessionErrorName(saved.error()))});
    }
}

void Engine::importSession(const QString &path)
{
    std::expected<Session, SessionLoadFailure> loaded = d->store.loadFile(
        path, [this](QStringView type) { return d->registry.contains(type.toString().toStdString()); });
    if (!loaded) {
        if (!loaded.error().backupPath.isEmpty()) {
            d->fail(ErrorCode::SessionCorruptFile, {path, loaded.error().backupPath});
        } else {
            d->fail(ErrorCode::SessionNotFound, {path});
        }
        return;
    }
    // **An import is a copy.** A new id, so that importing a session
    // somebody sent cannot overwrite one of your own whose id happens to
    // match, and so that importing the same file twice gives two sessions.
    // The file that was imported is left exactly as it was.
    Private::Open session;
    session.session = std::move(*loaded);
    session.session.id = QUuid::createUuid();
    session.session.revision = 0;
    // Its timers are the copy's own, or both sessions would answer to one
    // systemd timer.
    session.session.timers = timersWithNewIds(session.session.timers);
    const QString id = session.session.id.toString(QUuid::WithoutBraces);

    if (const std::expected<void, SessionError> saved = d->store.save(session.session); !saved) {
        d->fail(ErrorCode::SessionNotSaved, {session.session.name, asText(sessionErrorName(saved.error()))});
        return;
    }
    // A block type this build does not have is kept, and said so, exactly
    // as it is for a session opened from the sessions directory.
    for (const BlockInstance &block : session.session.blocks) {
        if (block.unknownType) {
            d->fail(ErrorCode::BlockUnknownType, {block.type});
        }
    }
    d->open.emplace(id, std::move(session));
    Q_EMIT sessionOpened(id);
    d->sendSnapshot(d->open.at(id));
}

void Engine::listPresets(const QString &typeId)
{
    Q_EMIT presetsListed(typeId, asJson(presetsToJson(d->presets.presetsFor(typeId))));
}

void Engine::applyPreset(const QString &sessionId, const QString &blockId, const QString &presetId)
{
    Private::Open *session = d->require(sessionId);
    if (session == nullptr) {
        return;
    }
    const BlockInstance *block = session->session.findBlock(blockId);
    if (block == nullptr) {
        d->fail(ErrorCode::PresetRefused, {presetId, asText(presetErrorName(PresetError::NotFound))});
        return;
    }
    // **What a plugin holds now is taken first**, so that undoing the
    // preset puts back the plugin's own state as it was, not as it was when
    // the session was last saved.
    if (blocks::PluginBlock::isPluginType(block->type.toStdString())) {
        d->collectBlockState(*session);
        block = session->session.findBlock(blockId);
    }
    const std::expected<Preset, PresetError> preset = d->presets.preset(block->type, presetId);
    if (!preset) {
        d->fail(ErrorCode::PresetRefused, {presetId, asText(presetErrorName(preset.error()))});
        return;
    }
    const std::expected<ApplyPreset, PresetError> edit =
        applyPresetEdit(d->registry, session->session, blockId, *preset);
    if (!edit) {
        d->fail(ErrorCode::PresetRefused, {presetId, asText(presetErrorName(edit.error()))});
        return;
    }
    // **Through applyEdit, like anything else.** It is one edit, so it is
    // one step to undo, it is numbered like every other change, and the
    // patch it sends is what a second client follows.
    applyEdit(sessionId, asJson(editCommandToJson(*edit)));
}

void Engine::savePreset(const QString &sessionId, const QString &blockId, const QString &name, bool replace)
{
    Private::Open *session = d->require(sessionId);
    if (session == nullptr) {
        return;
    }
    // A plugin's state as it is now, for a preset of a plugin block.
    if (const BlockInstance *block = session->session.findBlock(blockId);
        block != nullptr && blocks::PluginBlock::isPluginType(block->type.toStdString())) {
        d->collectBlockState(*session);
    }
    const std::expected<Preset, PresetError> preset = presetOfBlock(d->registry, session->session, blockId, name);
    if (!preset) {
        d->fail(ErrorCode::PresetRefused, {name, asText(presetErrorName(preset.error()))});
        return;
    }
    const std::expected<Preset, PresetError> saved = d->presets.save(*preset, replace);
    if (!saved) {
        d->fail(ErrorCode::PresetRefused, {name, asText(presetErrorName(saved.error()))});
        return;
    }
    // **The block is left showing what it was just saved as.** Somebody who
    // saves the settings they are listening to expects the menu to say so
    // rather than "Manual".
    ApplyPreset marker;
    marker.blockId = blockId;
    marker.presetId = saved->id;
    marker.params = saved->params;
    applyEdit(sessionId, asJson(editCommandToJson(marker)));
    listPresets(preset->typeId);
}

void Engine::renamePreset(const QString &typeId, const QString &presetId, const QString &name)
{
    if (const std::expected<Preset, PresetError> renamed = d->presets.rename(typeId, presetId, name); !renamed) {
        d->fail(ErrorCode::PresetRefused, {presetId, asText(presetErrorName(renamed.error()))});
        return;
    }
    listPresets(typeId);
}

void Engine::deletePreset(const QString &typeId, const QString &presetId)
{
    if (const std::expected<void, PresetError> removed = d->presets.remove(typeId, presetId); !removed) {
        d->fail(ErrorCode::PresetRefused, {presetId, asText(presetErrorName(removed.error()))});
        return;
    }
    listPresets(typeId);
}

void Engine::applyEdit(const QString &sessionId, const QString &editJson)
{
    Private::Open *session = d->require(sessionId);
    if (session == nullptr) {
        return;
    }
    const QJsonDocument document = QJsonDocument::fromJson(editJson.toUtf8());
    const std::expected<std::vector<EditCommand>, EditJsonError> commands = editCommandsFromJson(document);
    if (!commands) {
        d->fail(ErrorCode::EditRefused, {asText(editJsonErrorName(commands.error()))});
        return;
    }
    // **Ports are checked where the registry is** (blockcatalogue.h): the
    // session alone cannot say how many ports a block type has, so a wire
    // to a port that does not exist would otherwise be taken here and
    // refused by the graph compiler when somebody pressed Start.
    for (const EditCommand &command : *commands) {
        if (const auto *connect = std::get_if<Connect>(&command); connect != nullptr) {
            if (const QString refused = wireRefusal(session->session, d->registry, connect->wire); !refused.isEmpty()) {
                d->fail(ErrorCode::EditRefused, {refused});
                return;
            }
        }
        // **A timer as a client writes it is checked**: putting one back
        // unread is undo's, not a caller's, and one id is one systemd
        // timer, so no two sessions may have it.
        if (const auto *timer = std::get_if<SetTimer>(&command); timer != nullptr) {
            if (timer->raw) {
                d->fail(ErrorCode::EditRefused, {asText(editErrorName(EditError::TimerNotValid))});
                return;
            }
            if (timerIdTakenElsewhere(sessionListing(), sessionId, timer->timer.value("id"_L1).toString())) {
                d->fail(ErrorCode::EditRefused, {u"timerIdInUse"_s});
                return;
            }
        }
    }
    // **Whole or not at all.** A list is one thing the caller asked for, so
    // a second wire the session will not have must not leave the first one
    // drawn. The work is done on a copy and kept only if all of it applied,
    // which costs a session's worth of blocks and wires for the length of
    // an edit and saves the half-applied state having to be unpicked.
    const std::uint64_t startedAt = session->session.revision;
    Session trial = session->session;
    const std::expected<EditInverse, EditBatchError> applied = applyEdits(trial, *commands);
    if (!applied) {
        d->fail(ErrorCode::EditRefused, {asText(editErrorName(applied.error().error))});
        return;
    }
    session->session = std::move(trial);
    session->history.record(*commands, *applied);
    d->markUnsaved(sessionId);
    for (const EditCommand &command : *commands) {
        d->pushParam(*session, command);
    }

    // The edit is sent on as it was understood, not as it arrived, so every
    // client sees the same thing whatever it wrote. One patch a command,
    // each carrying the revision it made, because a client follows the
    // revisions one at a time however many arrived together.
    for (std::size_t index = 0; index < commands->size(); ++index) {
        Q_EMIT sessionPatch(sessionId, startedAt + index + 1, asJson(editCommandToJson((*commands)[index])));
    }
    d->refresh(*session);
    d->announceUndo(*session);
}

void Engine::setParam(const QString &sessionId,
                      const QString &blockId,
                      const QString &paramId,
                      const QString &valueJson)
{
    Private::Open *session = d->require(sessionId);
    if (session == nullptr) {
        return;
    }
    // **Parsed, and refused when it will not parse.** A document that is not
    // an array is not a value the interface sent: `QJsonValue::fromVariant()`
    // of a variant that holds nothing is **null**, not undefined, so reading
    // it that way took an unparseable value for a deliberate null and wrote
    // it into the session. Found while testing autosave.
    QJsonParseError parsed{};
    const QJsonDocument document = QJsonDocument::fromJson(valueJson.toUtf8(), &parsed);
    const QJsonValue value = parsed.error == QJsonParseError::NoError && document.isArray()
        ? document.array().at(0)
        : QJsonValue{QJsonValue::Undefined};
    if (value.isUndefined()) {
        d->fail(ErrorCode::EditRefused, {u"malformedValue"_s});
        return;
    }

    // The session remembers it, so that saving writes it, and so does the
    // history: a knob is undone like anything else, and a whole drag of one
    // comes back in a single step.
    for (BlockInstance &block : session->session.blocks) {
        if (block.id == blockId) {
            const QJsonValue previous = block.params.value(paramId);
            block.params.insert(paramId, value);
            session->history.record(SetParam{blockId, paramId, value},
                                    EditInverse{SetParam{blockId, paramId, previous}});
            d->announceUndo(*session);
            // A whole drag of a knob is one write, because the clock starts
            // again with every value.
            d->markUnsaved(sessionId);
            break;
        }
    }
    // **No patch.** A knob being turned is too frequent to send back, and
    // the client that turned it already knows.
    if (!session->runtime) {
        return;
    }
    const std::expected<ParamEffect, dsp::BlockError> set = session->runtime->setParam(blockId, paramId, value);
    if (!set) {
        d->fail(ErrorCode::EditRefused, {asText(dsp::blockErrorName(set.error()))});
        return;
    }
    if (*set == ParamEffect::NeedsReplan) {
        // **A parameter the session plan reads is an edit to the shape of
        // the session**, however it arrived: which device to play to, which
        // application to capture, whether alert sounds are wanted. Without
        // this the value would sit in the session — right in the file, right
        // in the interface — and be heard only after some unrelated edit
        // happened to recompile the graph, which is how it once was.
        //
        // **This cannot happen sixty times a second**, which is the reason
        // setParam is not applyEdit in the first place. What streams values
        // as it moves is a slider, and a slider is only ever offered for a
        // number the block can take through its queue; a device, a checkbox
        // or a name typed in arrives once, when the choice is made. The
        // catalogue says which is which, so an interface can tell too.
        // **The one exception is a number that changes what its block is
        // prepared for**, Frequency Raising's lowest move: that replans
        // when a drag crosses the line, not on every value of it.
        d->refresh(*session);
    }
}

void Engine::undo(const QString &sessionId)
{
    Private::Open *session = d->require(sessionId);
    if (session == nullptr) {
        return;
    }
    const std::optional<EditStep> step = session->history.takeUndo();
    if (!step) {
        return; // nothing to take back is not a failure
    }
    (void)d->applyStep(*session, step->undo);
    // **An undo is an edit to the file like any other**. Left off,
    // the disk kept what had been taken back until something else was
    // edited, and a restart brought it back. Marked even when the step was
    // refused part-way: what did apply is in the session now.
    d->markUnsaved(sessionId);
    d->announceUndo(*session);
}

void Engine::redo(const QString &sessionId)
{
    Private::Open *session = d->require(sessionId);
    if (session == nullptr) {
        return;
    }
    const std::optional<EditStep> step = session->history.takeRedo();
    if (!step) {
        return;
    }
    (void)d->applyStep(*session, step->redo);
    d->markUnsaved(sessionId);
    d->announceUndo(*session);
}

void Engine::getUndoState(const QString &sessionId)
{
    Private::Open *session = d->require(sessionId);
    if (session != nullptr) {
        d->announceUndo(*session, true);
    }
}

void Engine::controlRecording(const QString &sessionId, const QString &blockId, const QString &action)
{
    Private::Open *session = d->require(sessionId);
    if (session == nullptr) {
        return;
    }
    if (!session->runtime) {
        d->fail(ErrorCode::RecordingRefused, {u"NotRunning"_s}, blockId);
        return;
    }
    if (const std::expected<void, QString> done = session->runtime->controlRecording(blockId, action); !done) {
        d->fail(ErrorCode::RecordingRefused, {done.error()}, blockId);
    }
}

void Engine::setBroadcastPassword(const QString &sessionId, const QString &blockId, const QString &password)
{
    Private::Open *session = d->require(sessionId);
    if (session == nullptr) {
        return;
    }
    if (!d->isBroadcast(*session, blockId)) {
        d->fail(ErrorCode::BroadcastPasswordNotSaved, {u"notABroadcast"_s}, blockId);
        return;
    }
    const QString key = broadcastSecretKey(sessionId, blockId);
    const quint64 request = d->newSecretRequest(key);
    d->secrets->write(
        key, password, this, [this, sessionId, blockId, key, password, request](const SecretResult &result) {
            if (!d->isNewest(key, request)) {
                return;
            }
            if (!result.ok) {
                d->fail(ErrorCode::BroadcastPasswordNotSaved, {result.error}, blockId);
                return;
            }
            d->passwordStored.insert(key, !password.isEmpty());
            d->secretErrors.remove(key);
            if (Private::Open *open = d->find(sessionId); open != nullptr && open->runtime) {
                open->runtime->setBroadcastPassword(
                    blockId, password.isEmpty() ? std::nullopt : std::optional<QString>(password));
            }
            d->announceBroadcast(sessionId, blockId);
        });
}

void Engine::getBroadcastStatus(const QString &sessionId, const QString &blockId)
{
    Private::Open *session = d->require(sessionId);
    if (session == nullptr) {
        return;
    }
    const QString key = broadcastSecretKey(sessionId, blockId);
    if (!d->isBroadcast(*session, blockId) || d->passwordStored.contains(key)) {
        d->announceBroadcast(sessionId, blockId);
        return;
    }
    // Whether there is a password is the store's to say, the first time.
    const quint64 request = d->newSecretRequest(key);
    d->secrets->read(key, this, [this, sessionId, blockId, key, request](const SecretResult &result) {
        if (!d->isNewest(key, request)) {
            return;
        }
        if (result.ok) {
            d->passwordStored.insert(key, result.secret.has_value());
        }
        d->announceBroadcast(sessionId, blockId);
    });
}

void Engine::setSecretStore(std::unique_ptr<SecretStore> store)
{
    d->secrets = std::move(store);
}

void Engine::restartPlugin(const QString &sessionId, const QString &blockId)
{
    Private::Open *session = d->require(sessionId);
    if (session == nullptr) {
        return;
    }
    if (!session->runtime) {
        d->fail(ErrorCode::PluginRestartRefused, {u"NotRunning"_s}, blockId);
        return;
    }
    if (const std::expected<void, QString> done = session->runtime->restartPlugin(blockId); !done) {
        d->fail(ErrorCode::PluginRestartRefused, {done.error()}, blockId);
    }
}

void Engine::setPluginWindow(const QString &sessionId, const QString &blockId, bool open)
{
    Private::Open *session = d->require(sessionId);
    if (session == nullptr) {
        return;
    }
    // **Refused as a window that is not showing**, with the reason: a
    // panel's button follows the answer whatever it is.
    const std::expected<void, QString> done =
        session->runtime ? session->runtime->setPluginWindow(blockId, open) : std::unexpected(u"NotRunning"_s);
    if (!done) {
        Q_EMIT pluginWindowChanged(
            sessionId, blockId, false, done.error() == "NotRunning"_L1 ? u"notRunning"_s : u"unavailable"_s);
    }
}

void Engine::listPluginPresets(const QString &sessionId, const QString &blockId)
{
    Private::Open *session = d->require(sessionId);
    if (session == nullptr) {
        return;
    }
    const std::expected<void, QString> done =
        session->runtime ? session->runtime->listPluginPresets(blockId) : std::unexpected(u"NotRunning"_s);
    if (!done) {
        d->fail(ErrorCode::PluginPresetRefused, {QString{}, done.error()}, blockId);
    }
}

void Engine::loadPluginPreset(const QString &sessionId, const QString &blockId, const QString &presetId)
{
    Private::Open *session = d->require(sessionId);
    if (session == nullptr) {
        return;
    }
    // The plugin's state as it is now, in the session, for the undo.
    d->collectBlockState(*session);
    const std::expected<void, QString> done =
        session->runtime ? session->runtime->loadPluginPreset(blockId, presetId) : std::unexpected(u"NotRunning"_s);
    if (!done) {
        d->fail(ErrorCode::PluginPresetRefused, {presetId, done.error()}, blockId);
        return;
    }
    session->loadingPreset.insert(blockId);
}

void Engine::savePluginPreset(const QString &sessionId, const QString &blockId, const QString &name)
{
    Private::Open *session = d->require(sessionId);
    if (session == nullptr) {
        return;
    }
    const std::expected<void, QString> done =
        session->runtime ? session->runtime->savePluginPreset(blockId, name) : std::unexpected(u"NotRunning"_s);
    if (!done) {
        d->fail(ErrorCode::PluginPresetRefused, {name, done.error()}, blockId);
    }
}

void Engine::analyse(const QString &sessionId, const QString &blockId, int channel)
{
    Private::Open *session = d->require(sessionId);
    if (session != nullptr) {
        session->telemetry->analyse(blockId, channel);
    }
}

void Engine::start(const QString &sessionId)
{
    Private::Open *session = d->require(sessionId);
    if (session == nullptr) {
        return;
    }
    if (session->runtime && session->runtime->isRunning()) {
        d->setState(*session, sessionState::running);
        return;
    }
    d->setState(*session, sessionState::starting);
    if (!session->runtime) {
        session->runtime = std::make_unique<SessionRuntime>(d->connection, d->mirror, d->registry);
        // The rest of the session carries on; the person is told which way
        // round the sound would have gone.
        connect(session->runtime.get(), &SessionRuntime::feedbackRefused, this, [this](const QString &path) {
            d->fail(ErrorCode::RoutingFeedbackLoop, {path});
        });
        // **PipeWire went away under it** (PipeWire or WirePlumber
        // restarting): stopped properly, so its recordings are finished and
        // its meters go down, and started again as soon as the connection is
        // back. Meanwhile it is starting, which is what it is waiting to do.
        connect(session->runtime.get(), &SessionRuntime::audioSystemLost, this, [this, sessionId] {
            if (d->find(sessionId) == nullptr) {
                return;
            }
            {
                // Said to whoever follows the session, the timers above all:
                // this stop is the engine's, and it starts the session again.
                const QScopedValueRollback restarting(d->restarting, sessionId);
                stop(sessionId);
            }
            d->resumeWhenConnected.insert(sessionId);
            d->setState(*d->find(sessionId), sessionState::starting);
            // The connection may already be back by the time this is heard.
            d->resumeSessions();
        });
        connect(session->runtime.get(),
                &SessionRuntime::recordingFileStarted,
                this,
                [this, sessionId](const QString &blockId, const QString &path) {
                    d->library.fileStarted(sessionId, blockId, path);
                    Q_EMIT recordingFileStarted(sessionId, blockId, path);
                });
        connect(session->runtime.get(),
                &SessionRuntime::recordingFileFinished,
                this,
                [this, sessionId](const QString &blockId, const QString &path, quint64 frames, quint32 sampleRate) {
                    QJsonObject details;
                    details.insert(u"frames"_s, static_cast<double>(frames));
                    details.insert(u"sampleRate"_s, static_cast<double>(sampleRate));
                    details.insert(u"seconds"_s, sampleRate > 0 ? static_cast<double>(frames) / sampleRate : 0.0);
                    d->library.fileFinished(sessionId, blockId, path, frames, sampleRate);
                    Q_EMIT recordingFileFinished(sessionId, blockId, path, asJson(details));
                });
        connect(session->runtime.get(),
                &SessionRuntime::recordingFailed,
                this,
                [this](const QString &blockId, const QString &path, const QString &reason) {
                    d->fail(ErrorCode::RecordingFailed, {path, reason}, blockId);
                });
        connect(session->runtime.get(),
                &SessionRuntime::recordingOverflowed,
                this,
                [this](const QString &blockId, const QString &path, double seconds) {
                    d->fail(ErrorCode::RecordingOverflow, {path, QString::number(seconds, 'f', 1)}, blockId);
                });
        connect(session->runtime.get(),
                &SessionRuntime::pluginFailed,
                this,
                [this, sessionId](const QString &blockId, const QString &reason) {
                    // The plugin's name as the block has it, for the words.
                    QString plugin = blockId;
                    if (Private::Open *open = d->find(sessionId); open != nullptr) {
                        if (const BlockInstance *block = open->session.findBlock(blockId); block != nullptr) {
                            plugin = block->params.value("plugin"_L1).toString(u"test.gain"_s);
                        }
                    }
                    d->fail(reason == "hung"_L1 ? ErrorCode::PluginStoppedResponding : ErrorCode::PluginCrashed,
                            {plugin},
                            blockId);
                });
        // **A plugin's own window, and what is done in it.**
        connect(session->runtime.get(),
                &SessionRuntime::pluginWindowChanged,
                this,
                [this, sessionId](const QString &blockId, bool open, const QString &reason) {
                    // **A window that has gone lets go of what it held**:
                    // closed, failed or its host down, it will never say.
                    if (Private::Open *session = d->find(sessionId); session != nullptr && !open) {
                        session->history.endGesture();
                    }
                    Q_EMIT pluginWindowChanged(sessionId, blockId, open, reason);
                });
        connect(session->runtime.get(),
                &SessionRuntime::pluginParamChanged,
                this,
                [this, sessionId](const QString &blockId, const QString &paramId, const QJsonValue &value) {
                    if (Private::Open *open = d->find(sessionId); open != nullptr) {
                        d->takeFromPlugin(*open, {SetParam{blockId, paramId, value}}, true);
                    }
                });
        connect(session->runtime.get(),
                &SessionRuntime::pluginGesture,
                this,
                [this, sessionId](const QString &blockId, const QString &paramId, bool begin) {
                    if (Private::Open *open = d->find(sessionId); open != nullptr) {
                        if (begin) {
                            open->history.beginGesture(blockId, paramId);
                        } else {
                            open->history.endGesture(blockId, paramId);
                        }
                    }
                });
        connect(session->runtime.get(),
                &SessionRuntime::pluginValuesChanged,
                this,
                [this, sessionId](const QString &blockId, const QJsonObject &values, bool edit) {
                    Private::Open *open = d->find(sessionId);
                    if (open == nullptr) {
                        return;
                    }
                    std::vector<EditCommand> commands;
                    // **A preset put in is undone with the plugin's state**:
                    // what a preset sets beyond the settings (an LSP
                    // plugin's files and graphs) comes back only with it.
                    // The state before was taken when it was asked for.
                    if (edit && open->loadingPreset.remove(blockId) && open->runtime) {
                        const QJsonValue state = open->runtime->blockState().value(blockId).value(u"state"_s);
                        if (state.isString()) {
                            commands.emplace_back(SetParam{blockId, u"state"_s, state});
                        }
                    }
                    for (auto value = values.begin(); value != values.end(); ++value) {
                        commands.emplace_back(SetParam{blockId, value.key(), value.value()});
                    }
                    d->takeFromPlugin(*open, commands, edit);
                });
        connect(session->runtime.get(),
                &SessionRuntime::pluginPresetsListed,
                this,
                [this, sessionId](const QString &blockId, const QJsonArray &presets) {
                    Q_EMIT pluginPresetsListed(sessionId, blockId, asJson(presets));
                });
        connect(session->runtime.get(), &SessionRuntime::pluginPresetsRefused, this, [this](const QString &blockId) {
            d->fail(ErrorCode::PluginPresetRefused, {QString{}, u"notListed"_s}, blockId);
        });
        connect(session->runtime.get(),
                &SessionRuntime::pluginPresetShown,
                this,
                [this, sessionId](const QString &blockId, const QString &presetId) {
                    // Said after the values, or instead of them when it was
                    // refused: no longer waited for either way.
                    if (Private::Open *open = d->find(sessionId); open != nullptr) {
                        open->loadingPreset.remove(blockId);
                    }
                    Q_EMIT pluginPresetShown(sessionId, blockId, presetId);
                });
        connect(session->runtime.get(),
                &SessionRuntime::pluginPresetSaved,
                this,
                [this, sessionId](const QString &blockId, const QString &presetId) {
                    if (presetId.isEmpty()) {
                        d->fail(ErrorCode::PluginPresetRefused, {QString{}, u"notSaved"_s}, blockId);
                        return;
                    }
                    // The list with it in, which is what a panel shows.
                    listPluginPresets(sessionId, blockId);
                });
        connect(session->runtime.get(),
                &SessionRuntime::noticed,
                this,
                [this, sessionId](const QString &code, const QString &blockId, const QString &subject) {
                    const Private::Open *open = d->find(sessionId);
                    QJsonObject args{
                        {u"block"_s, blockId},
                        {u"session"_s, open != nullptr ? open->session.name : sessionId},
                    };
                    args.insert(code.startsWith("device."_L1) ? u"device"_s : u"application"_s, subject);
                    Q_EMIT notice(code, asJson(args));
                });
        connect(
            session->runtime.get(), &SessionRuntime::overloaded, this, [this, sessionId](const QStringList &heaviest) {
                // Shown on the heaviest block that is one of the
                // session's, rather than a boundary between segments.
                const Private::Open *open = d->find(sessionId);
                QString blockId;
                for (const QString &id : heaviest) {
                    if (open != nullptr && open->session.findBlock(id) != nullptr) {
                        blockId = id;
                        break;
                    }
                }
                d->fail(ErrorCode::SessionOverloaded, {open != nullptr ? open->session.name : sessionId}, blockId);
            });
        connect(session->runtime.get(),
                &SessionRuntime::recordingPaused,
                this,
                [this, sessionId](const QString &blockId, bool paused) {
                    Q_EMIT recordingPaused(sessionId, blockId, paused);
                });
        connect(session->runtime.get(),
                &SessionRuntime::recordingStopped,
                this,
                [this, sessionId](const QString &blockId, const QString &reason) {
                    Q_EMIT recordingStopped(sessionId, blockId, reason);
                });
        connect(session->runtime.get(),
                &SessionRuntime::recordingTagsNotWritten,
                this,
                [this](const QString &blockId, const QString &path) {
                    d->fail(ErrorCode::RecordingTagsNotWritten, {path}, blockId);
                });
        // **A Broadcast block's password**, from the secret store, the first
        // time its stream starts in this runtime.
        connect(session->runtime.get(),
                &SessionRuntime::broadcastPasswordWanted,
                this,
                [this, sessionId](const QString &blockId) { d->readBroadcastPassword(sessionId, blockId); });
        connect(session->runtime.get(),
                &SessionRuntime::broadcastStatusChanged,
                this,
                [this, sessionId](const QString &blockId, const QJsonObject &status) {
                    const QString key = broadcastSecretKey(sessionId, blockId);
                    const QJsonObject was = d->broadcastStatus.value(key);
                    d->broadcastStatus.insert(key, status);
                    // **Said once each time it gives up**, on the block:
                    // a stream that stops trying is something a person
                    // has to put right.
                    const QString state = status.value(u"state"_s).toString();
                    if (state == "failed"_L1
                        && (was.value(u"state"_s).toString() != state
                            || was.value(u"error"_s) != status.value(u"error"_s))) {
                        const Private::Open *open = d->find(sessionId);
                        const BlockInstance *block = open != nullptr ? open->session.findBlock(blockId) : nullptr;
                        QString station =
                            block != nullptr ? block->params.value("stationName"_L1).toString() : QString();
                        if (station.trimmed().isEmpty()) {
                            station = block != nullptr && !block->nickname.isEmpty() ? block->nickname : blockId;
                        }
                        // A store that could not be asked says why, in its
                        // own words.
                        QString reason = status.value(u"error"_s).toString();
                        if (reason == "passwordUnavailable"_L1) {
                            reason = d->secretErrors.value(key, reason);
                        }
                        d->fail(ErrorCode::BroadcastFailed, {station, reason}, blockId);
                    }
                    d->announceBroadcast(sessionId, blockId);
                });
        connect(session->runtime.get(),
                &SessionRuntime::recordingLowDiskSpace,
                this,
                [this](const QString &blockId, const QString &path, quint64 bytesFree) {
                    d->fail(ErrorCode::RecordingLowDiskSpace, {path, QString::number(bytesFree >> 20)}, blockId);
                });
    }
    if (const std::expected<void, RuntimeError> started = session->runtime->start(session->session); !started) {
        d->fail(ErrorCode::SessionCannotRun, {session->session.name, runtimeErrorName(started.error())});
        d->setState(*session, sessionState::error);
        return;
    }
    session->telemetry->watch(session->runtime.get());
    session->telemetry->start();
    d->setState(*session, sessionState::running);
    // **After the session is running**, and only on a start that did
    // something: a second Start on a session that is already running is not
    // a start, and must not open the call again.
    if (d->resuming) {
        return; // the call was opened when the session first started
    }
    for (const QString &url : urlsToOpen(session->session)) {
        Q_EMIT openUrlRequested(sessionId, url);
    }
}

void Engine::stop(const QString &sessionId)
{
    Private::Open *session = d->require(sessionId);
    if (session == nullptr) {
        return;
    }
    d->resumeWhenConnected.remove(sessionId);
    if (!session->runtime || !session->runtime->isRunning()) {
        d->setState(*session, sessionState::idle);
        return;
    }
    d->setState(*session, sessionState::stopping);
    // **Stopping changes the session**: the blocks' own state is collected
    // out of the runtime that is about to go away, so it is written rather
    // than waiting on a clock that may outlive what it describes.
    d->markUnsaved(sessionId);
    d->collectBlockState(*session);
    // The sampler lets go of the runtime before it stops, and it stops
    // before the runtime does: it reads the meters of the blocks the runtime
    // owns, and the last snapshot it takes on the way out has to be the
    // silence that takes the meters on the screen down.
    session->telemetry->watch(nullptr);
    session->telemetry->stop();
    session->runtime->stop();
    d->setState(*session, sessionState::idle);
}

void Engine::getSessionState(const QString &sessionId)
{
    Private::Open *session = d->require(sessionId);
    if (session != nullptr) {
        Q_EMIT stateChanged(sessionId, session->state);
    }
}

void Engine::resynchronise(const QString &sessionId)
{
    Private::Open *session = d->require(sessionId);
    if (session != nullptr) {
        d->sendSnapshot(*session);
    }
}

void Engine::getInventory()
{
    // A question asked now is answered now: whatever burst of changes is
    // still settling, the caller gets the graph as it stands.
    d->inventoryWatch.refresh();
    Q_EMIT inventoryChanged(d->inventoryJson());
}

void Engine::getBlockCatalogue()
{
    UnavailableChoices unavailable = recordingUnavailableChoices();
    unavailable.merge(broadcastUnavailableChoices());
#ifdef RB_HAVE_LIVESTREAM
    unavailable.merge(liveStreamUnavailableChoices());
    // **The graphics cards are asked on a thread of their own**, the first
    // time: until they have answered, the catalogue offers the software
    // encoders alone, and it is said again once they have.
    (void)probeVideoEncodersFor(this, [this] { getBlockCatalogue(); });
#endif
    Q_EMIT blockCatalogueChanged(asJson(blockCatalogueJson(d->registry, unavailable)));
}

void Engine::getAudioSystem()
{
    // A question asked now is answered now, whether or not the answer has
    // changed since the last one.
    d->publishAudioSystem(true);
}

namespace
{

/// **Our nodes, their ports, and every link that touches one**: the
/// part of a pw-dump that is ours, in the mirror's words, with each link's
/// ends named as "node:port" so that it reads without looking anything up.
[[nodiscard]] QJsonObject
ourGraphJson(const QList<pw::PwNode> &nodes, const QList<pw::PwPort> &ports, const QList<pw::PwLink> &links)
{
    QHash<std::uint32_t, QString> nodeNames;
    QSet<std::uint32_t> ours;
    QJsonArray nodesJson;
    for (const pw::PwNode &node : nodes) {
        nodeNames.insert(node.object.id, node.name);
        if (!node.isOurs) {
            continue;
        }
        ours.insert(node.object.id);
        QJsonObject props;
        for (auto it = node.props.cbegin(); it != node.props.cend(); ++it) {
            props.insert(it.key(), it.value());
        }
        nodesJson.append(QJsonObject{
            {"id"_L1, static_cast<qint64>(node.object.id)},
            {"name"_L1, node.name},
            {"description"_L1, node.description},
            {"mediaClass"_L1, node.mediaClass},
            {"driverId"_L1, static_cast<qint64>(node.driverId)},
            {"props"_L1, props},
        });
    }
    QHash<std::uint32_t, QString> portNames;
    QJsonArray portsJson;
    for (const pw::PwPort &port : ports) {
        portNames.insert(port.object.id, port.name);
        if (!ours.contains(port.nodeId)) {
            continue;
        }
        portsJson.append(QJsonObject{
            {"id"_L1, static_cast<qint64>(port.object.id)},
            {"nodeId"_L1, static_cast<qint64>(port.nodeId)},
            {"direction"_L1, pw::portDirectionName(port.direction)},
            {"name"_L1, port.name},
            {"channel"_L1, port.channel},
        });
    }
    const auto end = [&](std::uint32_t node, std::uint32_t port) -> QString {
        return nodeNames.value(node, QString::number(node)) + u':' + portNames.value(port, QString::number(port));
    };
    QJsonArray linksJson;
    for (const pw::PwLink &link : links) {
        if (!ours.contains(link.outputNode) && !ours.contains(link.inputNode)) {
            continue;
        }
        linksJson.append(QJsonObject{
            {"id"_L1, static_cast<qint64>(link.object.id)},
            {"outputNode"_L1, static_cast<qint64>(link.outputNode)},
            {"outputPort"_L1, static_cast<qint64>(link.outputPort)},
            {"inputNode"_L1, static_cast<qint64>(link.inputNode)},
            {"inputPort"_L1, static_cast<qint64>(link.inputPort)},
            {"from"_L1, end(link.outputNode, link.outputPort)},
            {"to"_L1, end(link.inputNode, link.inputPort)},
        });
    }
    return QJsonObject{{"nodes"_L1, nodesJson}, {"ports"_L1, portsJson}, {"links"_L1, linksJson}};
}

/// One session's line in the diagnostics: what its nodes say about how it
/// is keeping up, added up across the nodes a session cut around its
/// plugins is made of (ADR-0014).
[[nodiscard]] QJsonObject sessionDiagnosticsJson(const QString &id,
                                                 const QString &name,
                                                 const QString &state,
                                                 const SessionRuntime *runtime,
                                                 const TelemetrySampler &telemetry)
{
    QJsonObject json{
        {"id"_L1, id},
        {"name"_L1, name},
        {"state"_L1, state},
        {"xrunHistory"_L1, xrunHistoryJson(telemetry.xrunHistory())},
    };
    const bool running = runtime != nullptr && runtime->isRunning();
    json.insert("running"_L1, running);
    if (!running) {
        return json;
    }
    const std::vector<pw::SessionNode *> nodes = runtime->nodes();
    std::uint64_t cycles = 0;
    std::uint64_t xruns = 0;
    std::uint64_t lostCycles = 0;
    std::uint64_t overloads = 0;
    std::uint64_t longRunsNotRealtime = 0;
    double load = 0.0;
    double peakLoad = 0.0;
    std::uint32_t rate = 0;
    std::uint32_t quantum = 0;
    // **Real-time only if every node is**, and not known until each has run
    // a cycle and asked: rtkit can refuse one thread and not another.
    bool realtimeKnown = !nodes.empty();
    bool realtime = true;
    for (const pw::SessionNode *node : nodes) {
        const pw::SessionTelemetry node_telemetry = node->telemetry();
        cycles = std::max(cycles, node_telemetry.cycles);
        xruns = std::max(xruns, node_telemetry.xruns);
        lostCycles += node_telemetry.lostCycles;
        overloads += node_telemetry.overloads;
        longRunsNotRealtime += node_telemetry.longRunsNotRealtime;
        load += node_telemetry.lastLoad;
        peakLoad += node_telemetry.peakLoad;
        if (node == nodes.front()) {
            rate = node_telemetry.sampleRate;
            quantum = node_telemetry.frames;
        }
        if (node_telemetry.cycles == 0) {
            realtimeKnown = false;
        }
        realtime = realtime && node_telemetry.realtime != 0;
    }
    json.insert("nodes"_L1, static_cast<qint64>(nodes.size()));
    json.insert("cycles"_L1, static_cast<qint64>(cycles));
    json.insert("xruns"_L1, static_cast<qint64>(xruns));
    json.insert("lostCycles"_L1, static_cast<qint64>(lostCycles));
    json.insert("overloads"_L1, static_cast<qint64>(overloads));
    json.insert("dspLoad"_L1, load);
    json.insert("peakDspLoad"_L1, peakLoad);
    json.insert("rate"_L1, static_cast<qint64>(rate));
    json.insert("quantum"_L1, static_cast<qint64>(quantum));
    json.insert("latencySamples"_L1, static_cast<qint64>(runtime->latencySamples()));
    json.insert("realtime"_L1, realtimeKnown ? QJsonValue(realtime) : QJsonValue());
    json.insert("longRunsNotRealtime"_L1, static_cast<qint64>(longRunsNotRealtime));
    return json;
}

} // namespace

void Engine::getDiagnostics()
{
    const EngineInfo info = engineInfo();
    QJsonArray sessions;
    for (const auto &[id, open] : d->open) {
        sessions.append(sessionDiagnosticsJson(id, open.session.name, open.state, open.runtime.get(), *open.telemetry));
    }
    const QJsonObject answer{
        {"version"_L1, kDiagnosticsVersion},
        {"engine"_L1,
         QJsonObject{
             {"process"_L1, QCoreApplication::applicationName()},
             {"version"_L1, QString::fromUtf8(rb::versionFull())},
             {"pid"_L1, QCoreApplication::applicationPid()},
             {"pipeWireHeaders"_L1, info.pipeWireHeaders},
             {"pipeWireLibrary"_L1, info.pipeWireLibrary},
             {"logKept"_L1, LogRing::isInstalled()},
         }},
        {"audioSystem"_L1, QJsonDocument::fromJson(audioSystemToJson(d->audioSystemNow()).toUtf8()).object()},
        {"sessions"_L1, sessions},
        {"graph"_L1, ourGraphJson(d->mirror.nodes(), d->mirror.ports(), d->mirror.links())},
        {"recordingFormats"_L1, recordingFormatsJson()},
        {"plugins"_L1,
         pluginScanJson(d->plugins.plugins(), d->plugins.blocked(), d->plugins.isScanning(), d->plugins.incomplete())},
        {"log"_L1, logJson()},
    };
    Q_EMIT diagnostics(QString::fromUtf8(QJsonDocument(answer).toJson(QJsonDocument::Compact)));
}

void Engine::reconnectAudioSystem()
{
    if (d->connection.isConnected()) {
        // **Nothing to do, and doing it anyway would cost the graph.**
        // Tearing a live connection down to build it again would take every
        // running session's node with it.
        d->publishAudioSystem(true);
        return;
    }
    // **Stop, then start.** A connection that is waiting is already trying
    // and will try again when its delay runs out; asking it to start again
    // is a no-op. Taking it down first is what puts the wait back to the
    // beginning and makes the next attempt happen now — which is what a
    // person pressing Retry is asking for. There is nothing to tear down:
    // if there were, we would not be here.
    d->connection.stop();
    d->connection.start();
}

void Engine::listVirtualDevices()
{
    d->publishVirtualDevices();
}

void Engine::createVirtualDevice(const QString &deviceJson)
{
    const std::expected<VirtualDeviceRequest, QString> request = virtualDeviceRequestFromJson(deviceJson);
    if (!request) {
        d->fail(ErrorCode::VirtualDeviceNotCreated, {virtualDeviceRequestName(deviceJson), request.error()});
        return;
    }
    std::expected<pw::VirtualDevice, pw::VirtualDeviceError> made;
    {
        const QScopedValueRollback answering(d->answeringVirtualDevices, true);
        made = d->virtualDevices.create(request->name, request->direction, request->channels);
    }
    if (!made) {
        d->fail(ErrorCode::VirtualDeviceNotCreated, {request->name, virtualDeviceReason(made.error())});
        return;
    }
    Q_EMIT virtualDeviceCreated(made->id);
    d->publishVirtualDevices();
}

void Engine::renameVirtualDevice(const QString &deviceId, const QString &name)
{
    d->changeVirtualDevice(deviceId, [&] { return d->virtualDevices.rename(deviceId, name); });
}

void Engine::setVirtualDeviceEnabled(const QString &deviceId, bool enabled)
{
    d->changeVirtualDevice(deviceId, [&] { return d->virtualDevices.setEnabled(deviceId, enabled); });
}

void Engine::deleteVirtualDevice(const QString &deviceId)
{
    d->changeVirtualDevice(deviceId, [&] { return d->virtualDevices.remove(deviceId); });
}

void Engine::reloadVirtualDevices()
{
    {
        const QScopedValueRollback answering(d->answeringVirtualDevices, true);
        d->virtualDevices.reload();
    }
    d->publishVirtualDevices();
}

void Engine::listPlugins()
{
    Q_EMIT pluginsChanged(asJson(
        pluginsJson(d->plugins.plugins(), d->plugins.blocked(), d->plugins.isScanning(), d->plugins.incomplete())));
    if (!std::exchange(d->pluginsScanned, true)) {
        d->plugins.scan();
    }
}

void Engine::describePlugin(const QString &format, const QString &id)
{
    Q_EMIT pluginDescribed(format, id, asJson(QJsonObject{{"params"_L1, d->plugins.paramsJson(format, id)}}));
}

void Engine::listTimers()
{
    Q_EMIT timersListed(d->timers->timerListing(QDateTime::currentDateTime()));
}

void Engine::fireTimer(const QString &timerId, bool scheduled)
{
    d->timers->fire(timerId, scheduled);
}

void Engine::rescanPlugins(bool retryBlocked)
{
    d->pluginsScanned = true;
    d->plugins.scan(retryBlocked);
}

void Engine::listRecordings(const QString &sessionId)
{
    Q_EMIT recordingsChanged(sessionId, asJson(d->library.recordings(sessionId)));
}

void Engine::rescanRecordings()
{
    d->library.rescan();
}

void Engine::previewRecording(const QString &path)
{
    d->player.play(path);
}

void Engine::pausePreview()
{
    d->player.pause();
}

void Engine::seekPreview(double seconds)
{
    d->player.seek(seconds);
}

void Engine::stopPreview()
{
    d->player.stop();
}

std::uint32_t Engine::previewNodeId() const
{
    return d->player.streamNodeId();
}

void Engine::getRecording(const QString &path)
{
    if (const std::optional<QJsonObject> details = d->library.details(path)) {
        Q_EMIT recordingDetails(path, asJson(*details));
        return;
    }
    d->fail(ErrorCode::RecordingNotFound, {path});
}

void Engine::renameRecording(const QString &path, const QString &name)
{
    // A file being listened to is let go of first: the preview names it.
    if (d->player.status().value(u"path"_s).toString() == path) {
        d->player.stop();
    }
    const std::expected<QString, QString> renamed = d->library.rename(path, name);
    if (!renamed) {
        d->fail(ErrorCode::RecordingRenameRefused, {path, renamed.error()});
        return;
    }
    Q_EMIT recordingRenamed(path, *renamed);
}

void Engine::setRecordingTags(const QString &path, const QString &tagsJson)
{
    if (d->player.status().value(u"path"_s).toString() == path) {
        d->player.stop();
    }
    const QJsonObject tags = QJsonDocument::fromJson(tagsJson.toUtf8()).object();
    if (!d->library.saveTags(path, tags)) {
        d->fail(ErrorCode::RecordingTagsNotSaved, {path});
        return;
    }
    getRecording(path);
}

} // namespace rb::core
