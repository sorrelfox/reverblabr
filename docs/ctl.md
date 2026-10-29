<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# reverblabr-ctl

`reverblabr-ctl` runs and changes Reverblabr's sessions from a terminal, a script, cron or a desktop tool. It asks the engine through its [D-Bus API](dbus-api.md) and waits for the answer, so a script does not have to. The engine does the work, so a session changed here changes in every open Reverblabr window at the same time. `man reverblabr-ctl` has the same, in short.

```sh
reverblabr-ctl start "Evening radio"
reverblabr-ctl switch "Evening radio" B
reverblabr-ctl set "Evening radio" Voice gain 0.5
reverblabr-ctl split "Evening radio"
reverblabr-ctl status --json | jq -r '.sessions[] | select(.state == "running") | .name'
```

The first call starts the engine if it is installed and not running. It needs QtCore and QtDBus and nothing of the window, so it also runs where no Reverblabr window is open.

## Commands

| Command | Does |
|---|---|
| `list` | the sessions' names, one a line |
| `status [SESSION]` | every session and its state, or one's |
| `start SESSION` | runs it, opening it first if it is only on disk; one already running is left as it is |
| `stop SESSION` | stops it; one not running is left as it is |
| `split SESSION [RECORDER]` | ends the file each Recorder of a running session is writing, or the named one's, and starts the next with no gap |
| `switch SESSION [SWITCH] A\|B` | turns an Input Switch to A or B; the switch may be left out when the session has only one |
| `set SESSION BLOCK SETTING VALUE` | changes one setting, as one step the window can undo |

**Naming things:**
- A session is named by its name or its id, and a block by its name (the one given it in the window) or its id.
- A name that matches nothing exactly may match ignoring case.
- A name two sessions or two blocks share is refused, with their ids, rather than guessed.
- Names with spaces and any characters work; quote them as the shell needs.

**`set` reads the value for the setting it is.** A number must be in the setting's range and is written with a dot (`0.5`, whatever the language). A switch takes `true` or `false` (also `on`, `off`, `yes`, `no`, `1`, `0`), and a choice takes one of its choices. A hosted plugin's own settings are not in the block catalogue, so they are passed on as written: JSON if the value reads as a number, `true`, `false` or a quoted string, and as text otherwise. The plugin's host checks them.

**Changing a session nobody has open.** `set` and `switch` on a session that is only on disk open it, make the change, and close it again, so the change is saved and the session left as it was. Every open window hears the session open and close, so it shows the session's tab for that moment.

**Options:**
- `--json`: the answer as one line of JSON on standard output, [below](#the-json).
- `--timeout SECONDS`: how long to wait for each answer; 10 by default.
- `--no-activate`: asks only an engine that is already running, and never starts one. The shell completions use it, so pressing Tab never starts the engine.
- `--help`, `--version`, and `--` to end the options before a name that starts with a dash.

**Not here yet:** timers (`timers list`, `timer-fire`) come with the Schedule tab, and scripts (`run-script`) after 1.0.

## Exit status

| Code | Means |
|---|---|
| 0 | done, or nothing to do (a session already running, or already stopped) |
| 1 | the engine refused, with one of its [error codes](dbus-api.md#error-codes): a session could not start, a change was refused, a recording could not be split. Also `ctl.notRunning` and `ctl.dbusError` |
| 2 | the command line was wrong: an unknown command or option, a missing argument, a value the setting cannot take |
| 3 | no session, block or setting by that name, or more than one |
| 4 | no engine to ask: no session bus, or the engine is not installed, would not start, or with `--no-activate` is not running |
| 5 | the engine did not answer in time |

These numbers are only ever added to. The tests hold this table to the tool.

## The JSON

With `--json`, each command prints one JSON object on one line, and the same in every language: it holds codes and names, never a translated word. **Fields are only ever added.** A script ignores fields it does not know, as a D-Bus client does ([What stays the same](dbus-api.md#what-stays-the-same)).

A session is always `{"id", "name", "state"}`, where `state` is `idle`, `starting`, `running`, `stopping` or `error` for an open session, and `closed` for one only on disk.

| Command | Prints |
|---|---|
| `list`, `status` | `{"sessions": [session, …]}` |
| `status SESSION` | `{"session": session}` |
| `start`, `stop` | `{"session": session, "changed": bool}`; `changed` is false when there was nothing to do |
| `split` | `{"session": session, "split": [{"block": id, "file": path}, …]}`: each Recorder and the file it has started |
| `switch` | `{"session": session, "block": id, "param": "input", "value": "a"\|"b", "input": "a"\|"b"}` |
| `set` | `{"session": session, "block": id, "param": id, "value": value}`, with the value as it was sent |

A failure prints `{"error": {"code": code, "args": {…}}}` and exits with its [code](#exit-status). The code is the engine's own when the engine refused, and one of the tool's otherwise:

| Code | Arguments | Exit |
|---|---|---|
| `ctl.usage` | | 2 |
| `ctl.badValue` | param, value, type, and the setting's minimum and maximum, or its choices | 2 |
| `ctl.readOnly` | param | 2 |
| `ctl.sessionNotFound` | session | 3 |
| `ctl.sessionAmbiguous` | session, matches (the ids) | 3 |
| `ctl.blockNotFound` | block, type | 3 |
| `ctl.blockAmbiguous` | block, type, matches | 3 |
| `ctl.paramNotFound` | block, param | 3 |
| `ctl.notRunning` | session | 1 |
| `ctl.dbusError` | name, message | 1 |
| `ctl.unreachable` | reason: `noSessionBus`, `notRunning`, or the D-Bus error (`org.freedesktop.DBus.Error.ServiceUnknown`, …) | 4 |
| `ctl.timeout` | method | 5 |

## Languages

What the tool says to a person follows `LANGUAGE`, `LC_ALL`, `LC_MESSAGES` and `LANG`, from its own catalog, `reverblabr-ctl` (its template is made with the window's; see [i18n.md](i18n.md#tools)). `--json` never changes with the language.

## Completion

bash and zsh complete the commands, the options, `A` and `B`, and the sessions' names, one name a word even with spaces in it. They are installed where each shell finds them: `share/bash-completion/completions/reverblabr-ctl` and `share/zsh/site-functions/_reverblabr-ctl`.
