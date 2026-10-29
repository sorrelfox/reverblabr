// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/enginefacade.h"

#include <QString>
#include <QVariantList>

#include <utility>
#include <vector>

namespace rb::test
{

/// An engine that does nothing but write down every call, with its
/// arguments, in the order they came: what the bridge test holds the far end
/// of the bus to.
class RecordingFacade final : public core::EngineFacade
{
    Q_OBJECT

public:
    using EngineFacade::EngineFacade;

    /// Every call, by its facade name.
    std::vector<std::pair<QString, QVariantList>> calls;

    void listSessions() override
    {
        record("listSessions", {});
    }
    void createSession(const QString &name) override
    {
        record("createSession", {name});
    }
    void openSession(const QString &sessionId) override
    {
        record("openSession", {sessionId});
    }
    void saveSession(const QString &sessionId) override
    {
        record("saveSession", {sessionId});
    }
    void closeSession(const QString &sessionId) override
    {
        record("closeSession", {sessionId});
    }
    void deleteSession(const QString &sessionId) override
    {
        record("deleteSession", {sessionId});
    }
    void exportSession(const QString &sessionId, const QString &path) override
    {
        record("exportSession", {sessionId, path});
    }
    void importSession(const QString &path) override
    {
        record("importSession", {path});
    }
    void listPresets(const QString &typeId) override
    {
        record("listPresets", {typeId});
    }
    void applyPreset(const QString &sessionId, const QString &blockId, const QString &presetId) override
    {
        record("applyPreset", {sessionId, blockId, presetId});
    }
    void savePreset(const QString &sessionId, const QString &blockId, const QString &name, bool replace) override
    {
        record("savePreset", {sessionId, blockId, name, replace});
    }
    void renamePreset(const QString &typeId, const QString &presetId, const QString &name) override
    {
        record("renamePreset", {typeId, presetId, name});
    }
    void deletePreset(const QString &typeId, const QString &presetId) override
    {
        record("deletePreset", {typeId, presetId});
    }
    void applyEdit(const QString &sessionId, const QString &editJson) override
    {
        record("applyEdit", {sessionId, editJson});
    }
    void setParam(const QString &sessionId,
                  const QString &blockId,
                  const QString &paramId,
                  const QString &valueJson) override
    {
        record("setParam", {sessionId, blockId, paramId, valueJson});
    }
    void undo(const QString &sessionId) override
    {
        record("undo", {sessionId});
    }
    void redo(const QString &sessionId) override
    {
        record("redo", {sessionId});
    }
    void getUndoState(const QString &sessionId) override
    {
        record("getUndoState", {sessionId});
    }
    void controlRecording(const QString &sessionId, const QString &blockId, const QString &action) override
    {
        record("controlRecording", {sessionId, blockId, action});
    }
    void setBroadcastPassword(const QString &sessionId, const QString &blockId, const QString &password) override
    {
        record("setBroadcastPassword", {sessionId, blockId, password});
    }
    void getBroadcastStatus(const QString &sessionId, const QString &blockId) override
    {
        record("getBroadcastStatus", {sessionId, blockId});
    }
    void restartPlugin(const QString &sessionId, const QString &blockId) override
    {
        record("restartPlugin", {sessionId, blockId});
    }
    void analyse(const QString &sessionId, const QString &blockId, int channel) override
    {
        record("analyse", {sessionId, blockId, channel});
    }
    void start(const QString &sessionId) override
    {
        record("start", {sessionId});
    }
    void stop(const QString &sessionId) override
    {
        record("stop", {sessionId});
    }
    void getSessionState(const QString &sessionId) override
    {
        record("getSessionState", {sessionId});
    }
    void resynchronise(const QString &sessionId) override
    {
        record("resynchronise", {sessionId});
    }
    void getInventory() override
    {
        record("getInventory", {});
    }
    void getBlockCatalogue() override
    {
        record("getBlockCatalogue", {});
    }
    void getAudioSystem() override
    {
        record("getAudioSystem", {});
    }
    void reconnectAudioSystem() override
    {
        record("reconnectAudioSystem", {});
    }
    void getDiagnostics() override
    {
        record("getDiagnostics", {});
    }
    void listVirtualDevices() override
    {
        record("listVirtualDevices", {});
    }
    void createVirtualDevice(const QString &deviceJson) override
    {
        record("createVirtualDevice", {deviceJson});
    }
    void renameVirtualDevice(const QString &deviceId, const QString &name) override
    {
        record("renameVirtualDevice", {deviceId, name});
    }
    void setVirtualDeviceEnabled(const QString &deviceId, bool enabled) override
    {
        record("setVirtualDeviceEnabled", {deviceId, enabled});
    }
    void deleteVirtualDevice(const QString &deviceId) override
    {
        record("deleteVirtualDevice", {deviceId});
    }
    void reloadVirtualDevices() override
    {
        record("reloadVirtualDevices", {});
    }
    void listRecordings(const QString &sessionId) override
    {
        record("listRecordings", {sessionId});
    }
    void rescanRecordings() override
    {
        record("rescanRecordings", {});
    }
    void getRecording(const QString &path) override
    {
        record("getRecording", {path});
    }
    void renameRecording(const QString &path, const QString &name) override
    {
        record("renameRecording", {path, name});
    }
    void setRecordingTags(const QString &path, const QString &tagsJson) override
    {
        record("setRecordingTags", {path, tagsJson});
    }
    void previewRecording(const QString &path) override
    {
        record("previewRecording", {path});
    }
    void pausePreview() override
    {
        record("pausePreview", {});
    }
    void seekPreview(double seconds) override
    {
        record("seekPreview", {seconds});
    }
    void stopPreview() override
    {
        record("stopPreview", {});
    }
    void listPlugins() override
    {
        record("listPlugins", {});
    }
    void rescanPlugins(bool retryBlocked) override
    {
        record("rescanPlugins", {retryBlocked});
    }
    void describePlugin(const QString &format, const QString &id) override
    {
        record("describePlugin", {format, id});
    }
    void listTimers() override
    {
        record("listTimers", {});
    }
    void fireTimer(const QString &timerId, bool scheduled) override
    {
        record("fireTimer", {timerId, scheduled});
    }

public Q_SLOTS:
    /// A slot of this object that is not the facade's, as the mock's test
    /// hooks and the engine's connectToPipeWire() are: nobody on the bus may
    /// call it.
    void notOnTheBus()
    {
        record("notOnTheBus", {});
    }

private:
    void record(const char *name, QVariantList arguments)
    {
        calls.emplace_back(QString::fromLatin1(name), std::move(arguments));
    }
};

} // namespace rb::test
