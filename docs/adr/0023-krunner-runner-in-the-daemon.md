<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0023: The sessions in KRunner, as a D-Bus runner served by the daemon; Auto Run once a login

- **Status:** Proposed
- **Date:** 2026-10-29

## Context

Typing "rb" and part of a session's name in KRunner should offer to run or stop that session, or to open it in the window, and "rb rec" should say what is being recorded. It has to work with the window closed, which is what the background engine is for (ADR-0017).

What there is to work with:
- **KRunner takes runners in two forms.** A plugin is a shared library loaded into KRunner's process and built against KRunner's C++ API, whose ABI changes with the Frameworks. A D-Bus runner is any program that serves `org.kde.krunner1` (`Actions`, `Match`, `Run`, and `Teardown` and `SetActivationToken` when it wants them), named by a `.desktop` file in `share/krunner/dbusplugins`. KRunner calls it by its bus name, so activation starts it.
- **KRunner filters before it calls.** The file's `X-Plasma-Runner-Match-Regex` keeps every query that does not start with "rb" away from the runner, and `X-Plasma-API=DBus` (not `DBus2`) means KRunner asks for nothing when it loads, only when a query is for us.
- **`reverblabrd` already owns the sessions** and is started by the bus on the first call. It has their list to hand without a round trip: `Engine::sessionListing()`, which Auto Run reads for the same reason (ADR-0021).
- **The public API is asynchronous** (ADR-0020): a client calls `ListSessions` and waits for `SessionsListed`. KRunner calls `Match` on every keystroke and waits for the reply.
- **The daemon exits when nobody uses it** (ADR-0017): no client on the bus and nothing running. A client is whoever has called the engine's object. KRunner never leaves the bus while the person is logged in.
- **Auto Run started its sessions every time the engine started** (ADR-0021), and a query is a new way of starting it. A person who stopped an Auto Run session would see it run again after the engine's idle minute, at the next "rb", or at a `Teardown` KRunner sends to an engine that has gone. The same was already true of a window opened later.
- **Reading the sessions costs**: `sessionListing()` loads every session's file and counts its recordings, and `Match` comes with every keystroke.

## Decision

**The runner is served by `reverblabrd`, on an object of its own**: `org.kde.krunner1` at `/io/github/reverblabr/Runner`, under the engine's own name `io.github.reverblabr.Engine`. Its file, `io.github.reverblabr.Runner.desktop`, is installed to `share/krunner/dbusplugins` with `X-Plasma-API=DBus` and `X-Plasma-Runner-Match-Regex=^[Rr][Bb]( |$)`. It does not ask for the actions only once a login (`X-Plasma-Request-Actions-Once`): one failed first call would lose Open until the next login, and its words would stay in the language of the first query. KRunner asks once per query instead. The engine's activation file is what starts it, through the user unit, so nothing else is installed for it.

- **Match** reads the sessions straight from the engine, as Auto Run does, and answers at once. The list is kept between keystrokes and read again when a session opens, closes, changes state or is renamed, when anybody lists the sessions (that list is taken as it is), when KRunner closes, and after two seconds, for a file deleted or copied in by hand that no signal reports. `Run` always reads it afresh. "rb" lists every session; "rb" and a word, the sessions whose name holds it, the whole name above its start above anywhere in it. "rb" and three or more letters of "recording" (or of the word in the person's language) says which sessions have a recorder writing, or that none has. Recording is followed from the engine's signals the way the tray follows it.
- **The match itself runs or stops the session**, as its global shortcut does, and its words say which ("Run “Podcast”", "Stop “Podcast”"), with what the session is doing underneath. A session only on disk is opened and then run, and stays open in the daemon. **Open in Reverblabr** is the one action beside it.
- **Open starts the window** with `reverblabr --open-session=<id>`, and only for an id that is a session's (a UUID, written as the engine writes it), so nothing else KRunner hands back reaches a command line: a running window takes the request over, as it does the desktop entry's actions, and brings that tab forward; a window that is starting brings it forward once its own tabs are back. The token KRunner hands over in `SetActivationToken` goes to the window as `XDG_ACTIVATION_TOKEN`, so Wayland lets it come forward. A token is used by the one `Run` it came before, and forgotten at the end of it. The daemon then asks the user manager to put the window in a scope of its own in `app.slice`, as KIO's launcher does by default, so that stopping or restarting the engine's unit does not take the window with it. A refusal is a warning in the journal, since the window then stops with the engine. **A scope, not a transient service**: the window keeps the environment and the token it was started with, with nothing to quote into an `ExecStart`, and it starts the same way when there is no user manager at all; a service would run with the manager's environment, which is the desktop's only if the session has imported it.
- **A query does not make KRunner a client.** Only calls on the engine's own object count, so an engine a query started exits after its idle minute like any other.
- **Auto Run is once a login, and again after a crash** (amending ADR-0021). The engine notes in `$XDG_RUNTIME_DIR/reverblabr/autorun`, which the login manager makes at login and removes at the last logout, that it is running, and on a clean exit that it has exited. An engine that starts with no note there, or with one that says running (its last one crashed), starts the Auto Run sessions; one that finds "exited" opens again what was open, stopped, and starts nothing. So the first engine of a login runs them, whatever starts it, `Restart=on-failure` still brings them back, and a session the person stopped stays stopped until the next login. Without a runtime directory there is nowhere to note it, and Auto Run starts them every time, as before. Safe mode notes nothing.
- **Its words are the daemon's**: marked with `RB_I18NC`, looked up in the `reverblabr` catalog with plain gettext, as the daemon's notifications are. The file's name and comment are translated with the desktop entry's.
- **It is not part of the public API.** It speaks KRunner's protocol and says things in words, not codes; a script uses `Engine1`.

## Consequences

- No binary of ours is loaded into KRunner, and nothing has to be rebuilt when the Frameworks change their plugin ABI.
- A session set to run at login and stopped by hand stays stopped until the next login, which is what the Info tab and the docs said it did; `systemctl --user restart reverblabrd` is a clean exit, so it does not run them either.
- Typing "rb" starts the engine if it is not running, which costs a PipeWire connection and the sessions it had open, opened stopped. A query that starts with anything else never reaches it. The first query after login may wait for the engine to start.
- KRunner shows the sessions with the window closed, and its actions work without it: running and stopping are the engine's own calls.
- A build run from the source tree has no runner in KRunner: the file and the activation file are installed ones.
- The window takes `--open-session <id>` (or `=<id>`), which anything can use to open a session in it. Its value is never read as another option.
- A test that starts a daemon gives it a runtime directory of its own, so it never writes the note in the desktop's.
- The runner's file names the engine's bus name and path, so those stay as they are (ADR-0006 already promises the name).

## Alternatives considered

- **A KRunner plugin in C++.** It runs inside KRunner's process, so a crash in it takes KRunner down, and it is built against an ABI that changes with the Frameworks. It would still have to ask the daemon for everything.
- **A small helper of its own**, activated by its own name, that is a client of `Engine1`. Every `Match` would turn into `ListSessions` and a wait for `SessionsListed` (and that signal reaches every window each time), and it would have no way to see recording without following the signals from its start. It would be a client of the engine, so the engine would never exit while KRunner had asked once. It is one more process and one more activation file to install, for nothing the daemon cannot do on an object of its own.
- **Auto Run only when the unit starts the engine at login.** The engine cannot tell that start from an activation: both come through the same unit, with the same environment. And a crash restart would lose its sessions.
- **The window as a transient service** (`StartTransientUnit` with `ExecStart` and `Environment=XDG_ACTIVATION_TOKEN=…`). See the scope above: it is systemd's environment rather than the one the window was meant to have, and it still needs `startDetached` where there is no manager.
- **`X-Plasma-API=DBus2`**, which adds a `Config` call. KRunner makes that call when it loads its runners, so it would start the engine at every login whether or not anybody typed "rb". The match regex in the file does the same job without it.
