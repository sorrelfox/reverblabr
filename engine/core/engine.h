// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/enginefacade.h"
#include "core/presetstore.h"
#include "core/sessionstore.h"
#include "core/telemetry.h"

#include <chrono>
#include <memory>

namespace rb::dsp
{
class BlockRegistry;
}

namespace rb::pw
{
class Connection;
class VirtualDeviceManager;
} // namespace rb::pw

namespace rb::core
{

class SecretStore;

/// The engine, answering the interface for real.
///
/// **It owns everything below the facade**: the sessions on disk, the ones
/// that are open, the runtime of each one that is running, and the
/// connection to PipeWire they all share. Nothing above it holds a pointer
/// to any of that.
///
/// **Live it on the engine thread.** Create it there and let the GUI reach
/// it through queued connections; every slot here may take as long as an
/// edit's fade, which is far longer than a GUI frame ([ADR-0003]).
///
/// **What a caller learns, it learns from signals.** A session is sent whole
/// once, as a snapshot, and after that only the edits, each with the
/// revision it produced. A caller that sees a revision it did not expect has
/// missed one and asks to resynchronise; that is what makes a gap visible
/// rather than silently wrong.
class Engine : public EngineFacade
{
    Q_OBJECT

public:
    /// How long after an edit a session is written to disk.
    ///
    /// **There is no Save**. Every edit is saved, and the wait is only
    /// so that a drag of a knob is one write rather than a hundred: about a
    /// second, which is longer than any gesture and shorter than anybody's
    /// patience. A session is also written at once when it is closed,
    /// stopped or deleted, and when the engine goes away, so the wait can
    /// never be the thing that loses an edit.
    static constexpr std::chrono::milliseconds kAutosaveDelay{1000};

    /// \a registry must outlive the engine. \a store says where sessions
    /// live; the default is the user's own directory.
    explicit Engine(const dsp::BlockRegistry &registry,
                    SessionStore store = SessionStore{},
                    PresetStore presets = PresetStore{},
                    QObject *parent = nullptr);
    ~Engine() override;

    /// Writes every session that is waiting to be written, now. **For a
    /// test**, which cannot wait a second per edit, and for anything that
    /// has to know the disk is up to date before it goes on.
    void flushAutosave();

    /// The preview stream's node in the graph while a recording is being
    /// listened to, and 0 otherwise. For a test that has to find it.
    [[nodiscard]] std::uint32_t previewNodeId() const;

    /// Whether anything is waiting to be written. For a test asking whether
    /// an edit armed the timer at all.
    [[nodiscard]] bool hasUnsavedChanges() const;

    /// Opens the connection to PipeWire and starts watching the graph.
    /// Sessions can be listed, made and edited without this; only running
    /// one needs it.
    void connectToPipeWire();

    /// Whether PipeWire is there. For a caller deciding whether Start is
    /// worth offering.
    [[nodiscard]] bool isConnected() const;

    /// The virtual devices (ADR-0013). The facade's calls for them go
    /// through this, and a test points it at files of its own.
    [[nodiscard]] pw::VirtualDeviceManager &virtualDevices();

    /// The runtime of an open session, or null. For tests and for the
    /// telemetry sampler; nothing above the facade uses it.
    [[nodiscard]] class SessionRuntime *runtimeOf(const QString &sessionId) const;

    /// The connection to PipeWire the sessions share. For tests, which stop
    /// and start it to stand in for PipeWire restarting; nothing above the
    /// facade uses it.
    [[nodiscard]] pw::Connection &connection();

    /// Where an open session's levels are published, or null when there is
    /// no such session. **This is how meters are read** — not through the
    /// facade's signals, which would send sixty messages a second to say
    /// what a pointer can say for nothing ([ADR-0003]). The slot lives as
    /// long as the session is open and reads as silence while it is idle.
    [[nodiscard]] const TelemetrySlot *telemetryOf(const QString &sessionId) const;

    /// **The same slot for a window in another process** (ADR-0018): a new
    /// read-only descriptor for the shared memory it lives in, which the
    /// caller owns and hands on. -1 when no such session is open, or when
    /// its slot could not be put in shared memory.
    [[nodiscard]] int shareTelemetry(const QString &sessionId) const;

    /// **Where Broadcast blocks' passwords are kept**: the desktop's secret
    /// store unless something else is put here. For a test, which must not
    /// touch the store of whoever runs it.
    void setSecretStore(std::unique_ptr<SecretStore> store);

    /// What listSessions() says, as its JSON array, **without saying it**:
    /// for the daemon's own bookkeeping (Auto Run), which must not put
    /// a SessionsListed on the bus for every client each time it looks.
    [[nodiscard]] QString sessionListing() const;

public Q_SLOTS:
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

public:
    class Private;

private:
    std::unique_ptr<Private> d;
};

} // namespace rb::core
