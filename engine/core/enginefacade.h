// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QObject>
#include <QString>

namespace rb::core
{

/// What a session is doing, as the `StateChanged` signal spells it.
namespace sessionState
{
inline constexpr QLatin1StringView idle{"idle"};
inline constexpr QLatin1StringView starting{"starting"};
inline constexpr QLatin1StringView running{"running"};
inline constexpr QLatin1StringView stopping{"stopping"};
inline constexpr QLatin1StringView error{"error"};
/// Only in `SessionsListed`: on disk and not open. `StateChanged` never says
/// it, since a session that is not open has nothing to change.
inline constexpr QLatin1StringView closed{"closed"};
} // namespace sessionState

/// The only way to talk to the engine.
///
/// **It is the D-Bus interface, written in C++.** `dbus/
/// io.github.reverblabr.Engine1.xml` is the source of truth and a test holds
/// this to it, method for method and argument for argument. In M1 the engine
/// is a library in the same process ([ADR-0003]) and nothing is on a bus,
/// but designing to the interface now is what makes moving the engine into a
/// daemon later a matter of plumbing rather than redesign.
///
/// **Nothing returns a value.** A call says what should happen, and what did
/// happen arrives as a signal. That is what keeps a call from blocking the
/// caller — a GUI thread waiting on the engine is a GUI thread that stutters
/// — and it is the shape a bus call would have anyway. Failures arrive as
/// error(), never as a return code.
///
/// **Structured data travels as JSON strings**, for the same reason the XML
/// gives: a session can grow a field without breaking a client that has not
/// learned it yet.
///
/// **Telemetry does not come through here.** Meters and activity lights are
/// read from a snapshot, not pushed through signals sixty times a second.
///
/// The implementations are Engine, which runs sessions for real, and
/// MockEngine, which pretends convincingly enough for interface work to
/// start before the engine is finished.
class EngineFacade : public QObject
{
    Q_OBJECT

public:
    explicit EngineFacade(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    ~EngineFacade() override = default;

public Q_SLOTS:
    /// The sessions on disk. Answered by sessionsListed().
    virtual void listSessions() = 0;
    /// Makes a session and opens it.
    virtual void createSession(const QString &name) = 0;
    virtual void openSession(const QString &sessionId) = 0;
    virtual void saveSession(const QString &sessionId) = 0;
    /// Stops the session if it is running, and lets go of it.
    virtual void closeSession(const QString &sessionId) = 0;
    /// Closes it and removes it from disk.
    virtual void deleteSession(const QString &sessionId) = 0;

    /// Writes \a sessionId to \a path, for sending to somebody or keeping
    /// outside the sessions directory. **Saved first**, so the file is what
    /// is on the screen rather than what was last autosaved.
    virtual void exportSession(const QString &sessionId, const QString &path) = 0;

    /// Reads a session file from anywhere, gives it an id of its own and
    /// opens it.
    ///
    /// **An import is a copy.** The file that was imported is left exactly
    /// as it was, and importing the same file twice makes two sessions —
    /// otherwise opening a session somebody sent would silently overwrite
    /// one of your own whose id happened to match.
    virtual void importSession(const QString &path) = 0;

    /// Every preset for \a typeId, shipped and the person's own, as
    /// presetsListed().
    virtual void listPresets(const QString &typeId) = 0;

    /// Puts \a presetId's values on \a blockId, as **one undoable edit**.
    ///
    /// **The engine reads the preset, so a caller never carries its values.**
    /// The edit that results arrives as a patch like any other, which is
    /// what lets a second client follow it without a preset store of its
    /// own.
    virtual void applyPreset(const QString &sessionId, const QString &blockId, const QString &presetId) = 0;

    /// Saves what \a blockId is set to now as a preset called \a name.
    /// Without \a replace, saving onto an existing preset is refused, so a
    /// caller can ask first.
    virtual void savePreset(const QString &sessionId, const QString &blockId, const QString &name, bool replace) = 0;

    /// Renames or removes one of the person's own presets. Both are refused
    /// for a preset that ships with the application.
    virtual void renamePreset(const QString &typeId, const QString &presetId, const QString &name) = 0;
    virtual void deletePreset(const QString &typeId, const QString &presetId) = 0;

    /// One edit, as the JSON of an edit command (editcommand.h).
    virtual void applyEdit(const QString &sessionId, const QString &editJson) = 0;
    /// One parameter. The value is JSON so that a number, a string, a bool
    /// and a device reference all fit one signature.
    virtual void
    setParam(const QString &sessionId, const QString &blockId, const QString &paramId, const QString &valueJson) = 0;
    /// Takes back the last thing a person did — a gesture, not an event, so
    /// a drag of a slider or of several tiles comes back in one. Every
    /// command it applies arrives as a patch, so a client follows an undo
    /// exactly as it follows an edit. Nothing to undo is not an error.
    virtual void undo(const QString &sessionId) = 0;
    virtual void redo(const QString &sessionId) = 0;
    /// Whether there is anything to undo or redo. Answered by
    /// undoStateChanged().
    virtual void getUndoState(const QString &sessionId) = 0;

    /// **Pause, resume or split a running Recorder's recording**, at
    /// the frame it has got to when the engine takes the call. \a action is
    /// "pause", "resume" or "split".
    virtual void controlRecording(const QString &sessionId, const QString &blockId, const QString &action) = 0;

    /// **Starts a plugin block's plugin again**, after its host
    /// crashed or stopped responding. Refused with plugin.restartRefused.
    virtual void restartPlugin(const QString &sessionId, const QString &blockId) = 0;
    /// **Takes \a blockId's live spectra and band values into the session's
    /// telemetry** from now on, or nobody's when it is empty:
    /// the spectrum after the block and the one before it, of \a channel —
    /// kBothChannels (-1) for both sides mixed, 0 or 1 for one, the ear of
    /// a block that works on one ear. Not answered: they arrive with the
    /// meters, and a block that is not running has none.
    virtual void analyse(const QString &sessionId, const QString &blockId, int channel) = 0;

    virtual void start(const QString &sessionId) = 0;
    virtual void stop(const QString &sessionId) = 0;
    /// Answered by stateChanged().
    virtual void getSessionState(const QString &sessionId) = 0;
    /// The whole session again, for a client that has seen a gap in the
    /// patch numbering and cannot catch up.
    virtual void resynchronise(const QString &sessionId) = 0;

    /// The applications, inputs, outputs and defaults. Answered by
    /// inventoryChanged(), whose JSON the interface XML describes.
    virtual void getInventory() = 0;

    /// **What blocks this build has, and what each of them can be asked.**
    /// Answered by blockCatalogueChanged(). It does not change while the
    /// engine runs, so a client asks once.
    virtual void getBlockCatalogue() = 0;

    /// Where the connection to the audio system stands, and what it is made
    /// of. Answered by audioSystemChanged().
    virtual void getAudioSystem() = 0;
    /// Tries the connection again now rather than when the wait between
    /// attempts runs out — what a Retry in the interface calls. A
    /// connection that is already up is left alone.
    virtual void reconnectAudioSystem() = 0;
    /// **What the diagnostics page shows and a bug report carries**:
    /// versions, the audio system, each open session's load, late cycles
    /// and whether it runs real-time, our nodes, ports and links, which
    /// recording formats work, what the plugin scan found, and this
    /// engine's last log lines. Answered by diagnostics(), once.
    virtual void getDiagnostics() = 0;

    /// **Virtual devices** ([ADR-0013]): outputs apps play into and
    /// microphones apps record from, which live in PipeWire's own
    /// configuration and so outlive the application. Every one of these is
    /// answered by virtualDevicesChanged(); a refusal arrives as error()
    /// with nothing changed.
    virtual void listVirtualDevices() = 0;
    /// One of the user's own, enabled, from `{name, direction, channels}`.
    /// Answered by virtualDeviceCreated() first.
    virtual void createVirtualDevice(const QString &deviceJson) = 0;
    virtual void renameVirtualDevice(const QString &deviceId, const QString &name) = 0;
    virtual void setVirtualDeviceEnabled(const QString &deviceId, bool enabled) = 0;
    virtual void deleteVirtualDevice(const QString &deviceId) = 0;
    /// Reads both files again, for when one was changed from outside —
    /// which is what the all-users helper does.
    virtual void reloadVirtualDevices() = 0;

    /// **The recordings a session has made**, from the recordings
    /// index. Answered by recordingsChanged().
    virtual void listRecordings(const QString &sessionId) = 0;
    /// Checks the index against the disk, so that a file deleted, moved or
    /// trashed outside leaves the list. Answered by recordingsChanged() for
    /// every session whose list changed.
    virtual void rescanRecordings() = 0;

    /// **Inspecting a recording**: the whole of one, renaming it and
    /// writing its tags. Answered by recordingDetails() and
    /// recordingRenamed(), or error().
    virtual void getRecording(const QString &path) = 0;
    virtual void renameRecording(const QString &path, const QString &name) = 0;
    virtual void setRecordingTags(const QString &path, const QString &tagsJson) = 0;

    /// **Listening to a recording**: one file at a time, to the
    /// default output, never through a session. Answered by
    /// previewChanged(), or error() with recording.previewFailed.
    virtual void previewRecording(const QString &path) = 0;
    virtual void pausePreview() = 0;
    virtual void seekPreview(double seconds) = 0;
    virtual void stopPreview() = 0;
    /// **The plugins installed on this machine**, from the plugin
    /// cache; the first time, a scan for what changed too. Answered by
    /// pluginsChanged().
    virtual void listPlugins() = 0;
    /// Scans for what has changed, and \a retryBlocked looks into the
    /// blocklisted candidates again. Answered by pluginsChanged().
    virtual void rescanPlugins(bool retryBlocked) = 0;
    /// What one plugin's settings are. Answered by pluginDescribed().
    virtual void describePlugin(const QString &format, const QString &id) = 0;

Q_SIGNALS:
    /// A JSON array of `{id, name, modified}`.
    void sessionsListed(const QString &sessionsJson);
    void sessionOpened(const QString &sessionId);
    void sessionClosed(const QString &sessionId);
    /// The whole session, and the number the patches after it start from.
    void sessionSnapshot(const QString &sessionId, quint64 revision, const QString &sessionJson);
    /// What one edit changed, and the revision it produced. **Patches are
    /// numbered so that a gap is visible rather than silently wrong**: a
    /// client that sees a revision it did not expect asks to resynchronise.
    void sessionPatch(const QString &sessionId, quint64 revision, const QString &patchJson);
    /// One of rb::core::sessionState.
    void stateChanged(const QString &sessionId, const QString &state);
    /// What undo and redo would do now. The codes name the step —
    /// `edit.addBlock` and so on — and are empty when there is nothing to
    /// take back.
    void undoStateChanged(const QString &sessionId,
                          bool canUndo,
                          const QString &undoCode,
                          bool canRedo,
                          const QString &redoCode);
    /// Every preset for one block type, as a JSON array of
    /// `{id, name, factory, params}`. **A factory preset's name is empty**,
    /// because its words are translated from its id (docs/i18n.md).
    void presetsListed(const QString &typeId, const QString &presetsJson);
    void inventoryChanged(const QString &inventoryJson);
    /// Every block type, with the parameter descriptors the settings panel
    /// is generated from. The shape is in the interface XML. **Ids only**:
    /// the words a person reads are the interface's and are translated.
    void blockCatalogueChanged(const QString &catalogueJson);
    /// Where the connection to the audio system stands and what it is made
    /// of, as the JSON core::audioSystemFromJson() reads. Sent whenever the
    /// answer changes, so an interface follows it rather than asking.
    void audioSystemChanged(const QString &audioSystemJson);
    /// The answer to getDiagnostics(), as the interface XML describes.
    void diagnostics(const QString &diagnosticsJson);
    /// **A URL a session asked to have opened when it started**, one signal
    /// each, sent once per start. The engine says what should be opened and
    /// never opens it: that is KIO's business in the interface and
    /// `xdg-open`'s in the daemon.
    void openUrlRequested(const QString &sessionId, const QString &url);
    /// The virtual devices of both scopes, as the interface XML describes:
    /// `{devices: [...], unreadable: {scope: reason}}`. **Sent in answer to
    /// every call about them**, so a caller that asked always hears back, and
    /// whenever the list or where a device stands changes.
    void virtualDevicesChanged(const QString &devicesJson);
    /// The id of the device createVirtualDevice() made, sent before the list
    /// that shows it.
    void virtualDeviceCreated(const QString &deviceId);
    /// **A Recorder block has started writing a file**, at \a path. Every
    /// file that is started is finished by recordingFileFinished(), whatever
    /// else happens to it.
    void recordingFileStarted(const QString &sessionId, const QString &blockId, const QString &path);
    /// A Recorder block's file is closed and complete, as
    /// `{frames, sampleRate, seconds}`.
    void recordingFileFinished(const QString &sessionId,
                               const QString &blockId,
                               const QString &path,
                               const QString &detailsJson);
    /// A Recorder was paused (true) or resumed (false) by controlRecording().
    void recordingPaused(const QString &sessionId, const QString &blockId, bool paused);
    /// A Recorder ended its recording by itself, "timeLimit" or "silence",
    /// and finished its file.
    void recordingStopped(const QString &sessionId, const QString &blockId, const QString &reason);
    /// \a sessionId's recordings, newest first, as the interface XML
    /// describes them. Sent in answer to listRecordings() and whenever the
    /// list changes.
    void recordingsChanged(const QString &sessionId, const QString &recordingsJson);
    /// One recording in full, as the interface XML describes it.
    void recordingDetails(const QString &path, const QString &detailsJson);
    void recordingRenamed(const QString &oldPath, const QString &newPath);
    /// Where the preview stands, as the interface XML describes it.
    void previewChanged(const QString &previewJson);
    /// The plugins installed on this machine, as the interface XML describes
    /// them. Sent in answer to listPlugins(), and as a scan starts and ends.
    void pluginsChanged(const QString &pluginsJson);
    /// One plugin's settings, as the interface XML describes them.
    void pluginDescribed(const QString &format, const QString &id, const QString &descriptionJson);
    /// A dotted code and its arguments as a JSON object, **never text for a
    /// person to read**: the engine has no idea what language anybody
    /// speaks. The GUI holds the messages.
    void error(const QString &code, const QString &argsJson);
    /// **Something a person may want to hear about that is not an error**:
    /// one of core::kNoticeCodes, with its arguments and the block it
    /// is about as a JSON object — a device a running session uses went or
    /// came back, an application it captures stopped playing or started
    /// again. Like error(), a code and never words; the interface decides
    /// whether it is worth a notification.
    void notice(const QString &code, const QString &argsJson);
};

} // namespace rb::core
