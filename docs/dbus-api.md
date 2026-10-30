<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# The D-Bus API

Everything Reverblabr's engine does can be asked of it over the session bus: opening and running sessions, editing them, recording, the virtual devices, the recordings and the plugins. The window uses nothing else, so anything a person does to a session in the window, a script can do too. This page describes that API, version 1, which is `io.github.reverblabr.Engine1`. [ADR-0020](adr/0020-public-dbus-api.md) is why it looks the way it does.

## Where it is

| | |
|---|---|
| Bus | the session bus |
| Name | `io.github.reverblabr.Engine` |
| Object | `/io/github/reverblabr/Engine` |
| Interface | `io.github.reverblabr.Engine1` |
| Definition | [`dbus/io.github.reverblabr.Engine1.xml`](../dbus/io.github.reverblabr.Engine1.xml), MIT |

The engine runs as `reverblabrd`. **The first call to the name starts it** (D-Bus activation), so a script never has to start it first. It stays while a session runs or somebody uses it, and leaves by itself a minute after neither ([ADR-0017](adr/0017-engine-daemon.md)).

Activation needs the engine installed: `reverblabrd`, its activation file in `/usr/share/dbus-1/services/` and its systemd user unit, as [packaging/README.md](../packaging/README.md) lists them. A build that has only been compiled cannot be started by the bus. A call to the name then fails with `org.freedesktop.DBus.Error.ServiceUnknown`, and the window falls back to an engine of its own, which does not serve this API.

`busctl --user introspect io.github.reverblabr.Engine /io/github/reverblabr/Engine` lists what is on the object. The same object also serves `io.github.reverblabr.Telemetry`, which is [not part of the API](#not-part-of-the-api).

## How it works

- **Calls are answered by signals.** No method returns a value: a call says what should happen, and what did happen arrives as a signal a moment later. `ListSessions` is answered by `SessionsListed`, `Start` by `StateChanged`. A client listens first and calls second. Every method below says what answers it.
- **Failures are the `Error` signal**, with a code from the [table below](#error-codes) and its arguments as a JSON object: `Error("session.notFound", "{\"session\":\"…\"}")`. Codes are for programs. The window holds the words for people, in their language.
  - A call the engine cannot make sense of is refused on the spot, with a D-Bus error: `org.freedesktop.DBus.Error.UnknownMethod` for a member it does not have, and `org.freedesktop.DBus.Error.InvalidArgs` for arguments that are not the member's signature exactly (a number where it takes a string, one argument too few).
  - A well-formed call is always accepted, and nothing else comes back as a D-Bus error.
- **Structured data is JSON**, in string arguments: sessions, edits, lists. A payload may grow new fields in a later release, so **a client ignores fields it does not know**. The shapes of a session and its blocks are in [architecture.md](architecture.md); the rest are described with the signals that carry them in the XML.
- **Sessions and blocks are named by id.** A session's id is the one `SessionOpened` and `SessionsListed` give, and a block's is its `id` in the session's snapshot.
- **A session's changes are numbered.** After `SessionOpened` comes a whole `SessionSnapshot`, and after that every edit arrives as a `SessionPatch` with the next revision. A client that sees a revision it did not expect has missed one, and asks for the whole session again with `Resynchronise`.
- **Levels and meters are not on the bus**: they are read from shared memory by the window, sixty times a second, and are not part of this API.

## Trying it

Watch what the engine says in one terminal:

```sh
busctl --user monitor io.github.reverblabr.Engine
```

and call it from another. `busctl` needs the argument signature before the arguments: `s` for each string, `b` for a boolean, `d` for a number, `i` for a whole number.

```sh
E="io.github.reverblabr.Engine /io/github/reverblabr/Engine io.github.reverblabr.Engine1"

# The sessions on disk: answered by SessionsListed.
busctl --user call $E ListSessions

# A new session: answered by SessionOpened with its id, then its snapshot.
busctl --user call $E CreateSession s "Evening radio"
S=…   # the id SessionOpened gave

# A Volume block, at half gain.
busctl --user call $E ApplyEdit ss "$S" \
  '{"op":"addBlock","block":{"id":"quieter","type":"effect.volume","pos":[0,0]}}'
busctl --user call $E SetParam ssss "$S" quieter gain 0.5

# Run it, ask how it is, stop it. Each is answered by StateChanged.
busctl --user call $E Start s "$S"
busctl --user call $E GetSessionState s "$S"
busctl --user call $E Stop s "$S"
```

The same with GLib's `gdbus`, and with Qt's `qdbus`:

```sh
gdbus monitor --session --dest io.github.reverblabr.Engine
gdbus call --session --dest io.github.reverblabr.Engine \
  --object-path /io/github/reverblabr/Engine \
  --method io.github.reverblabr.Engine1.Start "$S"

qdbus io.github.reverblabr.Engine /io/github/reverblabr/Engine io.github.reverblabr.Engine1.Stop "$S"
```

[`reverblabr-ctl`](ctl.md) wraps the common cases, waiting for the answer so that a script does not have to: `reverblabr-ctl start "Evening radio"`, `reverblabr-ctl status --json`.

## What stays the same

`Engine1` is only ever added to. Within it:

- **Nothing released is taken away or changed:**
  - no method or signal is removed or renamed;
  - no argument changes its name, type, order or direction;
  - no JSON field is removed or changes meaning;
  - no error code is removed or renamed;
  - no call starts doing something a client that relied on it would notice.
- **Things are added:** new methods, new signals, new fields in a JSON payload, new error codes, and new values of a string that is an open set (a session's state, an edit's `op`, a block type, an error's `reason`).
- **So a client ignores what it does not know:** an unknown field, an unknown signal, an unknown value. It treats an error code it does not know as a failure with that code.
- **A change that cannot be made by adding** goes into `io.github.reverblabr.Engine2`, served on the same object beside `Engine1`. `Engine1` stays for at least one release after that, marked deprecated.

The released surface is written down line by line in [`dbus/io.github.reverblabr.Engine1.api`](../dbus/io.github.reverblabr.Engine1.api), and the test `dbus.public-surface` fails on any change to it that is not an addition, on an addition that is not written down, and on anything written down that this page does not describe.

## Methods

### Sessions

| Method | Does | Answered by |
|---|---|---|
| `ListSessions()` | the sessions on disk | `SessionsListed` |
| `CreateSession(s name)` | makes a session and opens it | `SessionOpened`, then `SessionSnapshot` |
| `OpenSession(s sessionId)` | opens a session that is on disk | `SessionOpened` and `SessionSnapshot`, or `session.notFound` |
| `SaveSession(s sessionId)` | writes it to disk now (the engine also saves on its own, a second after each change) | nothing when it is written; `session.notSaved` if it cannot be |
| `CloseSession(s sessionId)` | stops it if it runs, and lets go of it | `SessionClosed` |
| `DeleteSession(s sessionId)` | closes it and removes it from disk | `SessionClosed` if it was open, `session.notFound` if there was no such file; `ListSessions` shows it gone |
| `ExportSession(s sessionId, s path)` | saves it, then writes a copy of the file to `path` | `session.notSaved` if the path cannot be written |
| `ImportSession(s path)` | opens a copy of a session file from anywhere, under an id of its own | `SessionOpened`, or `session.corruptFile` |

### Editing

| Method | Does | Answered by |
|---|---|---|
| `ApplyEdit(s sessionId, s editJson)` | one edit command, or a JSON array of them applied whole or not at all as one step to undo: `addBlock`, `removeBlock`, `moveBlock`, `connect`, `disconnect`, `setParam`, `rename`, `setNotes`, `setEnabled`, `setSessionSetting`, `applyPreset`, `setTimer`, `removeTimer` ([architecture.md](architecture.md)) | a `SessionPatch` per command, or `edit.refused` |
| `SetParam(s sessionId, s blockId, s paramId, s valueJson)` | one parameter, with the value as JSON; meant for a value that moves, so it produces no patch | `edit.refused` if refused |
| `Undo(s sessionId)`, `Redo(s sessionId)` | take back, or put back, the last thing done | patches, and `UndoStateChanged` |
| `GetUndoState(s sessionId)` | what Undo and Redo would do now | `UndoStateChanged` |

### Presets

| Method | Does | Answered by |
|---|---|---|
| `ListPresets(s typeId)` | every preset for a block type, shipped and the person's own | `PresetsListed` |
| `ApplyPreset(s sessionId, s blockId, s presetId)` | puts a preset's values on a block, as one step to undo | a patch, or `preset.refused` |
| `SavePreset(s sessionId, s blockId, s name, b replace)` | saves a block's settings as a preset | `PresetsListed`, or `preset.refused` |
| `RenamePreset(s typeId, s presetId, s name)` | renames one of the person's own presets | `PresetsListed`, or `preset.refused` |
| `DeletePreset(s typeId, s presetId)` | removes one of the person's own presets | `PresetsListed`, or `preset.refused` |

### Running

| Method | Does | Answered by |
|---|---|---|
| `Start(s sessionId)` | runs it | `StateChanged`, or `session.cannotRun` |
| `Stop(s sessionId)` | stops it | `StateChanged` |
| `GetSessionState(s sessionId)` | how it is | `StateChanged` |
| `Resynchronise(s sessionId)` | the whole session again, after a missed patch | `SessionSnapshot` |
| `ControlRecording(s sessionId, s blockId, s action)` | a running Recorder: `pause`, `resume`, or `split` (end this file and start the next, with no gap) | `RecordingPaused`, the files' own signals, or `recording.refused` |
| `SetBroadcastPassword(s sessionId, s blockId, s password)` | a Broadcast block's password, or a Live Stream block's stream key, kept in the desktop's password store (KWallet, or the Secret Service) and never in the session; empty removes it. A running stream tries again with it at once | `BroadcastStatusChanged`, or `broadcast.passwordNotSaved` |
| `GetBroadcastStatus(s sessionId, s blockId)` | where a Broadcast or Live Stream block's stream stands, and whether it has a password or key | `BroadcastStatusChanged` |
| `RestartPlugin(s sessionId, s blockId)` | starts a plugin block's plugin again in a host of its own | `plugin.restartRefused` if it cannot |
| `SetPluginWindow(s sessionId, s blockId, b open)` | shows or closes a plugin block's own window, which its host makes | `PluginWindowChanged` |
| `ListPluginPresets(s sessionId, s blockId)` | a plugin block's own presets, its format's | `PluginPresetsListed`, or `plugin.presetRefused` |
| `LoadPluginPreset(s sessionId, s blockId, s presetId)` | puts one of them on the block, as one edit to undo | `SessionPatch` for each value it changed, then `PluginPresetShown` |
| `SavePluginPreset(s sessionId, s blockId, s name)` | keeps the plugin's settings as a preset of its format, where its other hosts find it (an LV2 plugin's in `~/.lv2`, a VST3 plugin's in `~/.vst3/presets`); a CLAP plugin's go with the application's own, through `SavePreset` | `PluginPresetsListed`, or `plugin.presetRefused` |
| `Analyse(s sessionId, s blockId, i channel)` | takes one block's live spectra into the session's levels (for the window's curves; an empty `blockId` stops) | not answered: the spectra travel with the levels |

### What is out there

| Method | Does | Answered by |
|---|---|---|
| `GetInventory()` | the applications, inputs, outputs and defaults | `InventoryChanged` (also sent whenever they change) |
| `GetBlockCatalogue()` | every block type this build has, with its parameters | `BlockCatalogueChanged` |
| `GetAudioSystem()` | where the connection to the audio system stands and what it is made of | `AudioSystemChanged` (also sent whenever it changes) |
| `ReconnectAudioSystem()` | tries the connection again now | `AudioSystemChanged` |
| `GetDiagnostics()` | what the diagnostics page shows and a bug report carries: versions, each session's load, late cycles and real-time, our nodes, ports and links, the recording formats, the plugin scan, the engine's last log lines | `Diagnostics` |

### Virtual devices

| Method | Does | Answered by |
|---|---|---|
| `ListVirtualDevices()` | the virtual devices of both scopes | `VirtualDevicesChanged` |
| `CreateVirtualDevice(s deviceJson)` | makes one of the user's own: `{"name": "Every app", "direction": "output", "channels": "stereo"}` | `VirtualDeviceCreated`, then `VirtualDevicesChanged`, or `device.virtualNotCreated` |
| `RenameVirtualDevice(s deviceId, s name)` | renames one | `VirtualDevicesChanged`, or `device.virtualRefused` |
| `SetVirtualDeviceEnabled(s deviceId, b enabled)` | switches one on or off | `VirtualDevicesChanged`, or `device.virtualRefused` |
| `DeleteVirtualDevice(s deviceId)` | removes one | `VirtualDevicesChanged`, or `device.virtualRefused` |
| `ReloadVirtualDevices()` | reads the devices' files again, after something else changed them | `VirtualDevicesChanged` |

The all-users devices are written by a privileged helper the window asks through the desktop's password prompt, not by the engine, so they are read-only here (`device.virtualRefused` with `ReadOnlyScope`).

### Recordings

| Method | Does | Answered by |
|---|---|---|
| `ListRecordings(s sessionId)` | the recordings a session has made | `RecordingsChanged` |
| `RescanRecordings()` | checks the list against the disk | `RecordingsChanged` for every session whose list changed |
| `GetRecording(s path)` | one recording in full, with its outline and its tags | `RecordingDetails`, or `recording.notFound` |
| `RenameRecording(s path, s name)` | renames the file in its folder, keeping its extension | `RecordingRenamed` and `RecordingsChanged`, or `recording.renameRefused` |
| `SetRecordingTags(s path, s tagsJson)` | writes its title, artist and the rest into the file | `RecordingDetails`, or `recording.tagsNotSaved` |
| `PreviewRecording(s path)` | plays it to the default output, or goes on from where it was paused | `PreviewChanged`, or `recording.previewFailed` |
| `PausePreview()`, `StopPreview()` | pause or stop that | `PreviewChanged` |
| `SeekPreview(d seconds)` | moves it to a time in the file | `PreviewChanged` |

### Plugins

| Method | Does | Answered by |
|---|---|---|
| `ListPlugins()` | the plugins installed on this machine | `PluginsChanged` |
| `RescanPlugins(b retryBlocked)` | looks for what has changed, and optionally tries the blocklisted ones again | `PluginsChanged` as the scan starts and ends |
| `DescribePlugin(s format, s id)` | one plugin's settings, by its own names and ranges | `PluginDescribed` |

### Timers

| Method | Does | Answered by |
|---|---|---|
| `ListTimers()` | every session's timers, closed sessions' too | `TimersListed` |
| `FireTimer(s timerId, b scheduled)` | what a timer does when its time comes: opens its session if it is only on disk, starts it, and stops it once the timer's duration is over, quitting the applications it captures if the timer says so. A session already running is left running, and not stopped later. Each timer's systemd user timer calls it with `scheduled` true, through `reverblabr-ctl timer-fire --scheduled`; such a fire more than two minutes after the timer's time, as after a sleep, is refused. A fire by hand passes false and runs whenever it comes | `TimerFired`, or `timer.notFound` or `timer.late` |

## Signals

| Signal | Says |
|---|---|
| `SessionsListed(s sessionsJson)` | the sessions on disk: `[{id, name, state, sources, recordings, startedAt, autoRun, timers}]`, where `state` is an open session's (as `StateChanged` says it) or `closed` for one that is only on disk; `sources` are the blocks sound comes in through, `[{type, name}]` in the session's order (`name` is what somebody called the block, or empty); `recordings` is how many recordings the session has made; `startedAt`, only while it runs, is when it started, in milliseconds since the epoch; `autoRun` is `off`, or `login` for a session the background engine starts as it starts: the first time in a login and after a crash, or every time if the person chose that ([ADR-0021](adr/0021-auto-run.md), [ADR-0023](adr/0023-krunner-runner-in-the-daemon.md)); and `timers` are the session's timers this version can run, as the session file holds them ([architecture.md](architecture.md#session-data-model)) |
| `SessionOpened(s sessionId)` | a session is open; its snapshot follows |
| `TimersListed(s timersJson)` | every session's timers: each as the session file holds it ([architecture.md](architecture.md#session-data-model)), with `sessionId`, `sessionName`, `onCalendar` (the systemd calendar expression it starts on), `nextRun` (when it next starts, in milliseconds since the epoch; left out when it never will again) and, while it keeps its session running, `endsAt` |
| `TimerFired(s timerId, s sessionId, b started)` | a timer has started its session, or asked it to open and start (`started` true), or found it running already and left it to whoever started it (`started` false) |
| `SessionClosed(s sessionId)` | a session was closed |
| `SessionSnapshot(s sessionId, t revision, s sessionJson)` | the whole session, and the revision its patches continue from |
| `SessionPatch(s sessionId, t revision, s patchJson)` | what one edit changed |
| `UndoStateChanged(s sessionId, b canUndo, s undoCode, b canRedo, s redoCode)` | whether there is anything to undo or redo, and what each would do (`edit.` and the command's name, `edit.addBlock` and so on, or `edit.resetParams` for an `applyPreset` that only takes values off and names no preset, which is a block put back to its defaults; empty for nothing) |
| `StateChanged(s sessionId, s state)` | `idle`, `starting`, `running`, `stopping` or `error` |
| `InventoryChanged(s inventoryJson)` | the applications, inputs, outputs and defaults |
| `BlockCatalogueChanged(s catalogueJson)` | the block types and their parameters |
| `AudioSystemChanged(s audioSystemJson)` | the connection's state, the server and session manager and their versions, the driver, the rate and the quantum |
| `Diagnostics(s diagnosticsJson)` | the answer to `GetDiagnostics`, described in the interface XML; for a person and a bug report, so it grows freely and `version` says which description it follows |
| `OpenUrlRequested(s sessionId, s url)` | a block of the session asks for a URL to be opened when the session starts; the engine never opens it itself |
| `PresetsListed(s typeId, s presetsJson)` | the presets of a block type, whenever they change |
| `VirtualDevicesChanged(s devicesJson)` | the virtual devices and where each stands |
| `VirtualDeviceCreated(s deviceId)` | the id of the device just made, before the list that shows it |
| `RecordingFileStarted(s sessionId, s blockId, s path)` | a Recorder started a file |
| `RecordingFileFinished(s sessionId, s blockId, s path, s detailsJson)` | a Recorder's file is closed and complete: `{frames, sampleRate, seconds}` |
| `RecordingPaused(s sessionId, s blockId, b paused)` | a Recorder was paused or resumed |
| `RecordingStopped(s sessionId, s blockId, s reason)` | a Recorder ended by itself: `timeLimit` or `silence` |
| `BroadcastStatusChanged(s sessionId, s blockId, s statusJson)` | where a Broadcast or Live Stream block's stream stands: `{state, error, connectedSince, bytesSent, kbps, reconnects, retryIn, title, sampleRate, secure, passwordStored}`, and for a Live Stream block `{encoder, width, height, fps, framesSent, framesDropped}` too, described in the interface XML; when it changes, and once a second while it is on the air |
| `PluginsChanged(s pluginsJson)` | the installed plugins, whether a scan runs, the blocklisted ones, and the files a scan got only part of the way through (`incomplete`) |
| `PluginDescribed(s format, s id, s descriptionJson)` | one plugin's settings |
| `PluginWindowChanged(s sessionId, s blockId, b open, s reason)` | a plugin block's own window was shown, or has gone: `closed`, `unavailable`, `failed`, `crashed` or `notRunning` |
| `PluginPresetsListed(s sessionId, s blockId, s presetsJson)` | a plugin block's own presets, `{id, name}` each |
| `PluginPresetShown(s sessionId, s blockId, s presetId)` | the plugin's own preset a block's settings are, or empty when they are none |
| `RecordingsChanged(s sessionId, s recordingsJson)` | a session's recordings, newest first |
| `RecordingDetails(s path, s detailsJson)` | one recording in full |
| `RecordingRenamed(s oldPath, s newPath)` | a recording's file was renamed |
| `PreviewChanged(s previewJson)` | where the preview stands, ten times a second while it plays |
| `Error(s code, s argsJson)` | a failure, by [code](#error-codes) |
| `Notice(s code, s argsJson)` | something that is not a failure but worth knowing, by [code](#notice-codes): a device or an application went or came back |

The XML gives each argument's full shape.

## Error codes

Each is the `code` of an `Error` signal. Its arguments arrive by name in `argsJson`. A code marked *block* also names the block it concerns, as `block`.

`engine.daemonUnreachable`, which the window shows when it cannot reach `reverblabrd`, is the window's own and never sent on the bus: a client that cannot reach the engine hears it from D-Bus itself, as above.

| Code | Arguments | Means |
|---|---|---|
| `block.unknownType` | type | a session has a block type this build does not know; the block is kept |
| `graph.cycle` | | the session's wires form a cycle, so it cannot be compiled |
| `routing.feedbackLoop` | path | a link would feed a session's output back into its own input, so it was refused |
| `session.corruptFile` | file, backup | a session file could not be read, and was moved aside as `backup` |
| `session.notFound` | session | there is no session of that id |
| `session.notSaved` | session, reason | a session could not be written; nothing was lost and it is still open |
| `session.cannotRun` | session, reason | a session could not be started |
| `session.overloaded` | session | a session's audio took longer than its cycles; its blocks are stopped and it is silent until it is changed (*block*: the costliest) |
| `edit.refused` | reason | an edit was refused, and the session is as it was |
| `preset.refused` | preset, reason | a preset could not be applied, saved, renamed or deleted; nothing changed |
| `device.virtualNotCreated` | name, reason | a virtual device could not be made |
| `device.virtualRefused` | device, reason | a virtual device could not be changed; `ReadOnlyScope` means an all-users one |
| `device.virtualNodeRefused` | device | the audio system would not make a virtual device's node; it is tried again |
| `recording.failed` | file, reason | a Recorder could not make or write its file and has stopped writing (*block*) |
| `recording.overflow` | file, seconds | the disk did not keep up, and that many seconds are missing; it carries on |
| `recording.lowDiskSpace` | file, megabytes | a warning: under a gigabyte left on the disk being recorded to |
| `recording.refused` | reason | a Recorder could not be paused, resumed or split (*block*) |
| `recording.tagsNotWritten` | file | a finished recording's tags could not be written; the audio is whole (*block*) |
| `recording.previewFailed` | file, reason | a recording could not be listened to |
| `recording.notFound` | file | there is no such recording in the list |
| `recording.renameRefused` | file, reason | a recording was not renamed: `invalidName`, `exists`, `notFound` or `failed` |
| `recording.tagsNotSaved` | file | a recording's tags could not be written; the file is as it was |
| `broadcast.passwordNotSaved` | reason | a Broadcast block's password or a Live Stream block's key could not be put in the password store or taken out of it: `noSecretStore`, `secretStoreDenied`, `secretStoreFailed`, or `notABroadcast`; nothing was written anywhere else (*block*) |
| `broadcast.failed` | station, reason | a Broadcast or Live Stream block's stream stopped trying, because trying again cannot help: `authRejected`, `noPassword`, `notConfigured`, `encoderMissing`, a refused certificate (`certificateUntrusted`, `certificateExpired`, `certificateWrongHost`, `certificateInvalid`), `tlsFailed` or `trustStoreUnavailable`; station is a Live Stream block's name; it tries again when its settings or its password change (*block*) |
| `plugin.crashed` | plugin | a plugin's host went; the block is bypassed and can be restarted (*block*) |
| `plugin.stoppedResponding` | plugin | a plugin's host stopped responding and was ended; as above (*block*) |
| `plugin.restartRefused` | reason | a plugin could not be restarted (*block*) |
| `timer.notFound` | timer | `FireTimer` named a timer no session has; nothing was started |
| `timer.late` | timer | a scheduled `FireTimer` came more than two minutes after the timer's time, as after a sleep; nothing was started |
| `plugin.presetRefused` | preset, reason | a plugin's own preset could not be listed, put in or written (*block*) |

## Notice codes

Each is the `code` of a `Notice` signal: something that happened under a running session and is not a failure. Nothing was refused and the session carries on. Its arguments arrive by name in `argsJson`, and every notice also names its block, as `block`. The window turns some of them into desktop notifications; a client may do the same, or show them its own way.

| Code | Arguments | Means |
|---|---|---|
| `device.lost` | session, device | the device an Input Device or Output Device block uses went away; the block waits and carries on when it is back. `device` is its description when it was last seen |
| `device.returned` | session, device | that device is back |
| `capture.lost` | session, application | an Application source's application has no stream left to capture: it closed, or stopped playing |
| `capture.regained` | session, application | that application is playing again, after a `capture.lost` |

A device or an application the person changes, and a default that moves, is a choice and never a notice.

## The window's actions, and their calls

Every action a person takes on the engine from the window is one of these calls. The window only ever talks to the engine through this interface, so the list cannot fall behind it: an action with no call would be an action the window cannot do.

| In the window | Call |
|---|---|
| New session, the `+` beside the tabs | `CreateSession` |
| Opening a session from the list | `ListSessions`, `OpenSession` |
| Closing a tab | `CloseSession` |
| Delete Session… | `DeleteSession` |
| Export… / Import… | `ExportSession` / `ImportSession` |
| Saving | the engine saves a second after each change; `SaveSession` asks for it now |
| Adding, removing and moving blocks, wiring and unwiring them | `ApplyEdit` with `addBlock`, `removeBlock`, `moveBlock`, `connect`, `disconnect` |
| Renaming a block, a session's notes, switching a block on or off | `ApplyEdit` with `rename`, `setNotes`, `setEnabled` |
| The Info tab: title, automatic connections, rate, latency, Auto Run | `ApplyEdit` with `setSessionSetting` (`autoConnect`, `sampleRate`, `latency`, `autoRun`) |
| The Schedule tab: add, change, switch on or off, delete a timer | `ApplyEdit` with `setTimer`, `removeTimer`; the background engine writes the systemd timers ([architecture.md](architecture.md#timers)) |
| A block's settings: sliders, choices, switches | `SetParam` while it moves, `ApplyEdit` with `setParam` for a choice |
| Fit from audiogram (Hearing Compensation) | `ApplyEdit` with a list of `setParam`: the window works the band settings out from the audiogram with the published rule ([fitting-rules.md](fitting-rules.md)), and a client can do the same |
| Undo, Redo, and the Edit menu's words | `Undo`, `Redo`, `GetUndoState` |
| A block's presets: apply, save, rename, delete | `ListPresets`, `ApplyPreset`, `SavePreset`, `RenamePreset`, `DeletePreset` |
| Run / Stop, and what the status bar says | `Start`, `Stop`, `GetSessionState` |
| A Recorder's Pause, Resume and Split | `ControlRecording` |
| A Broadcast block's password, a Live Stream block's key, and their Status tabs | `SetBroadcastPassword`, `GetBroadcastStatus` |
| Restarting a plugin | `RestartPlugin` |
| A plugin's own window, and its own presets | `SetPluginWindow`, `ListPluginPresets`, `LoadPluginPreset`, `SavePluginPreset`; a control moved in the window arrives as `SessionPatch`, and a whole drag of it is one `Undo` |
| A curve with the sound drawn behind it | `Analyse` |
| The block library | `GetBlockCatalogue` |
| The application and device pickers | `GetInventory` |
| The audio system in the status bar, and Retry | `GetAudioSystem`, `ReconnectAudioSystem` |
| The Virtual Devices window: add, rename, switch, delete | `ListVirtualDevices`, `CreateVirtualDevice`, `RenameVirtualDevice`, `SetVirtualDeviceEnabled`, `DeleteVirtualDevice`, and `ReloadVirtualDevices` after the all-users helper |
| The Files tab | `ListRecordings`, `RescanRecordings` |
| The Recording Inspector: rename, tags, listen, seek | `GetRecording`, `RenameRecording`, `SetRecordingTags`, `PreviewRecording`, `PausePreview`, `SeekPreview`, `StopPreview` |
| The library's plugins, Rescan, a plugin's panel | `ListPlugins`, `RescanPlugins`, `DescribePlugin` |

**Not the engine's, so not here:**
- the window's own state: tabs, panels, their positions, the appearance, the menu bar;
- the all-users virtual devices, which go through the desktop's password prompt;
- a recording's Open Containing Folder, Open With… and Move to Trash, which the desktop's file handling does;
- opening a URL a block asked for (`OpenUrlRequested` says which);
- Report Bug and About.

**An Input Switch** needs nothing of its own: which input it plays is its `input` parameter (`a` or `b`), set with `SetParam`.

## Not part of the API

`io.github.reverblabr.Telemetry`, on the same object, hands the window and the panel widget of the same build the memory a session's levels are published in ([ADR-0018](adr/0018-telemetry-in-shared-memory.md), [ADR-0024](adr/0024-panel-widget.md)). Its layout is fixed by the build and checked by a fingerprint, and it may change in any release. Nothing outside Reverblabr should use it.

`org.kde.krunner1`, on `/io/github/reverblabr/Runner` under the same name, is what KRunner calls to offer the sessions ([ADR-0023](adr/0023-krunner-runner-in-the-daemon.md)). It speaks KRunner's protocol, says things in words for people rather than codes, and may change in any release. A script runs and stops sessions with `Start` and `Stop` above.

VST is a registered trademark of Steinberg Media Technologies GmbH.
