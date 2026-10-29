// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/enginefacade.h"
#include "core/telemetry.h"

#include <QDBusConnection>
#include <QDBusMessage>

#include <memory>

namespace rb::bus
{

/// An engine in another process, reached over the bus (ADR-0017): the
/// GUI's half of the bridge.
///
/// **Every method sends the call and returns**, as the facade's always
/// have: the answer is a signal, and every D-Bus signal of the interface is
/// re-emitted as the facade signal of the same name. Calls go out in the
/// order they are made, on one connection, which D-Bus keeps.
///
/// **It starts the daemon by calling it**: every call asks the bus to
/// activate the name, so the first one a window makes starts the engine.
///
/// **It comes back after the daemon does.** It watches the name, and when
/// another process takes it — the daemon restarted after a crash, or after
/// somebody stopped it — it opens again every session it had open and asks
/// again for everything it has asked for with a lasting answer, so the
/// window returns to what the new daemon has through the same snapshots
/// and lists as at its first start. A daemon that goes away while a window
/// is open is started again a moment later by those calls.
///
/// **Levels are read, not sent** (ADR-0018). telemetryOf() hands out a
/// read-only mapping of the memory the daemon publishes a session's
/// snapshots in, asked for once per session and then read on the window's
/// own frame tick with no message at all, exactly as a pointer into an
/// engine in this process is.
class RemoteEngine : public core::EngineFacade
{
    Q_OBJECT

public:
    explicit RemoteEngine(const QDBusConnection &connection = QDBusConnection::sessionBus(), QObject *parent = nullptr);
    ~RemoteEngine() override;

    /// Whether a daemon has the name now.
    [[nodiscard]] bool isConnected() const;

    /// **Where \a sessionId's levels are read**, or null while there is
    /// nowhere yet: a read-only mapping of the daemon's shared memory for
    /// it (ADR-0018). The first question about a session this window has
    /// open asks the daemon for it, without waiting; telemetryChanged()
    /// says when there is an answer. The address stays good until
    /// telemetryChanged() says otherwise.
    [[nodiscard]] const core::TelemetrySlot *telemetryOf(const QString &sessionId);

    void listSessions() override;
    void createSession(const QString &name) override;
    void openSession(const QString &sessionId) override;
    void saveSession(const QString &sessionId) override;
    void closeSession(const QString &sessionId) override;
    void deleteSession(const QString &sessionId) override;
    void exportSession(const QString &sessionId, const QString &path) override;
    void importSession(const QString &path) override;
    void listPresets(const QString &typeId) override;
    void applyPreset(const QString &sessionId, const QString &blockId, const QString &presetId) override;
    void savePreset(const QString &sessionId, const QString &blockId, const QString &name, bool replace) override;
    void renamePreset(const QString &typeId, const QString &presetId, const QString &name) override;
    void deletePreset(const QString &typeId, const QString &presetId) override;
    void applyEdit(const QString &sessionId, const QString &editJson) override;
    void setParam(const QString &sessionId,
                  const QString &blockId,
                  const QString &paramId,
                  const QString &valueJson) override;
    void undo(const QString &sessionId) override;
    void redo(const QString &sessionId) override;
    void getUndoState(const QString &sessionId) override;
    void controlRecording(const QString &sessionId, const QString &blockId, const QString &action) override;
    void setBroadcastPassword(const QString &sessionId, const QString &blockId, const QString &password) override;
    void getBroadcastStatus(const QString &sessionId, const QString &blockId) override;
    void restartPlugin(const QString &sessionId, const QString &blockId) override;
    void setPluginWindow(const QString &sessionId, const QString &blockId, bool open) override;
    void listPluginPresets(const QString &sessionId, const QString &blockId) override;
    void loadPluginPreset(const QString &sessionId, const QString &blockId, const QString &presetId) override;
    void savePluginPreset(const QString &sessionId, const QString &blockId, const QString &name) override;
    void analyse(const QString &sessionId, const QString &blockId, int channel) override;
    void start(const QString &sessionId) override;
    void stop(const QString &sessionId) override;
    void getSessionState(const QString &sessionId) override;
    void resynchronise(const QString &sessionId) override;
    void getInventory() override;
    void getBlockCatalogue() override;
    void getAudioSystem() override;
    void reconnectAudioSystem() override;
    void getDiagnostics() override;
    void listVirtualDevices() override;
    void createVirtualDevice(const QString &deviceJson) override;
    void renameVirtualDevice(const QString &deviceId, const QString &name) override;
    void setVirtualDeviceEnabled(const QString &deviceId, bool enabled) override;
    void deleteVirtualDevice(const QString &deviceId) override;
    void reloadVirtualDevices() override;
    void listRecordings(const QString &sessionId) override;
    void rescanRecordings() override;
    void getRecording(const QString &path) override;
    void renameRecording(const QString &path, const QString &name) override;
    void setRecordingTags(const QString &path, const QString &tagsJson) override;
    void previewRecording(const QString &path) override;
    void pausePreview() override;
    void seekPreview(double seconds) override;
    void stopPreview() override;
    void listPlugins() override;
    void rescanPlugins(bool retryBlocked) override;
    void describePlugin(const QString &format, const QString &id) override;
    void listTimers() override;
    void fireTimer(const QString &timerId, bool scheduled) override;

Q_SIGNALS:
    /// A daemon has the name, where none had it a moment ago.
    void connected();
    /// The daemon has left the bus: stopped, crashed or killed.
    void disconnected();
    /// A daemon that is not the first one this engine talked to has the
    /// name, and everything lasting has been asked for again.
    void restarted();
    /// A call could not be delivered: \a error is the bus's name for why.
    void callFailed(const QString &method, const QString &error);
    /// **What telemetryOf(\a sessionId) answers has changed**: it has
    /// been mapped, or it is being let go because the session closed or
    /// the daemon left. Emitted before an old mapping goes, so a reader
    /// that asks again here never keeps an address that has gone.
    void telemetryChanged(const QString &sessionId);

private Q_SLOTS:
    /// Every signal of the interface arrives here, and leaves as the
    /// facade's.
    void relayBusSignal(const QDBusMessage &message);

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace rb::bus
