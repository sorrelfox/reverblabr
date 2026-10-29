<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0017: The engine in a daemon, reached over D-Bus and started on demand

- **Status:** Accepted
- **Date:** 2026-09-28

## Context

ADR-0003 put the engine inside the GUI at first, behind a facade shaped as if it were already remote, and said it would move into a daemon, `reverblabrd`, later. That time has come: sessions should keep running and recording when the window is closed or crashes, and the command-line tool, Auto Run, the public API and the scheduler all need an engine that is there without a window.

What there is to work with:
- **The facade holds.** Every one of its 40 methods and 21 signals is in `dbus/io.github.reverblabr.Engine.xml`; `engine.Facade.MatchesTheInterfaceDefinition` holds the C++ to it member by member, argument by argument. Every argument is a string, a bool, a double or a 64-bit count. No method returns anything: a call is answered by a signal. And a client that sees a gap in a session's patch numbers asks for a snapshot. So the facade already *is* a D-Bus interface, apart from nobody serving it.
- **Two things are not in it.** Telemetry is read through a pointer to a seqlock (`telemetryOf()`), on purpose (ADR-0003), so it can move into shared memory; that move came later, in ADR-0018. The all-users virtual devices are written through KAuth by the GUI (ADR-0013), which needs the desktop's prompt and so stays where the desktop is.
- **The bus is always there on the desktops we support** (ADR-0004): Plasma on a systemd user session, with a session bus that can start a service by name.

## Decision

**`reverblabrd` is the engine and nothing else**: the engine library, PipeWire, and Qt Core and Qt D-Bus, with no GUI and no KDE Frameworks (the CMake layering check already holds the engine to that). It owns every session: the files, which are open, which are running, the recordings and the presets. It serves the engine interface at `/io/github/reverblabr/Engine` under the well-known name `io.github.reverblabr.Engine` on the session bus.

**The bridge is generic, not generated.** On the daemon's side, one D-Bus object answers every method in the XML by calling the facade method of the same name (`CreateSession` → `createSession`) through Qt's meta-object system, and relays every facade signal as the D-Bus signal of the same name. On the GUI's side, `RemoteEngine` is an `EngineFacade` whose every method sends the call and whose every signal is the D-Bus signal re-emitted. The names are the XML's with the first letter lowered, which is the rule the conformance test already enforces, so a method added to the facade and the XML reaches the bus with nothing else written. The compiler still makes `RemoteEngine` spell out each method, since the facade is abstract. Calls are sent in order on one connection, and D-Bus keeps them in order, so a burst of edits arrives as it was made.

**Started on demand.** A D-Bus activation file names `reverblabrd.service`, a systemd user unit of `Type=dbus`. The first call to the name starts the daemon: through systemd where there is a user instance, and through the file's own `Exec=` where there is not. Nothing starts it at login until Auto Run enables the unit. Its output goes to the journal, which is where a unit's stderr goes; nothing else is needed for that.

**What happens when the window goes:** nothing, to the sessions. The daemon keeps every session that is open, running or not, and a running one goes on playing and recording. The daemon **exits on its own** once it has had no client for a minute and no session is running, so it does not linger after a person has closed the window on a stopped session. A client is a unique bus name that has called it and is still on the bus.

**What happens when the daemon goes:** it writes down which sessions are open, and when it starts again it opens them again, **stopped**. A crash is not a reason to start playing into somebody's speakers. `--safe-mode` opens nothing at all. The GUI watches the name: when a new daemon has it, `RemoteEngine` says so and asks again for everything it has asked for that has a lasting answer (the sessions it has open, the catalogue, the inventory, the audio system, the virtual devices, each session's undo state and recordings), so the window comes back to what the new daemon has, through the same snapshots and lists as a first start.

**The in-process engine stays**, as ADR-0003's facade always allowed: the tests run against it, `--mock` still runs the GUI with no PipeWire and no bus, and the GUI runs the engine in its own process with `--in-process`. **Which one the GUI uses by default stayed in-process until telemetry moved into shared memory** (ADR-0018), because until then a daemon's sessions had no meters in the window. **Now the default is the daemon.** A window asks the bus once, before it opens, and has the bus start the daemon if it is installed and not running. A window that cannot reach it, with no session bus, no activation file naming it (a build that has not been installed) or a daemon that would not start, runs the engine in its own process and says why in its banner, rather than coming up with nothing behind it. `--in-process` asks for that without asking the bus; `--daemon` keeps to the daemon whatever the bus answers.

**Tests never touch the user's bus or systemd.** They run under `dbus-run-session` with a configuration of their own whose service directory holds an activation file pointing at the build tree's `reverblabrd --mock`. No unit is ever installed by a test.

## Consequences

- A session outlives its window, which Auto Run, the CLI and the scheduler all need.
- The facade does not change. Adding to it stays one edit in the XML, the facade, the engine and the mock, plus one line in `RemoteEngine`.
- A GUI crash cannot take the audio with it; a daemon crash takes every session's audio and comes back with them stopped.
- The GUI's own state (which tabs, where panels sit) stays the GUI's; which sessions are open is the daemon's. A window opening against a daemon that already has sessions open shows the ones it asks for, as now.
- Until telemetry moves into shared memory, the daemon is opt-in, and a window on it has no meters, no wire colours and no state dots.
- Everything a client can do is on the bus, so anything on the bus as the user can do it. That is D-Bus's own trust boundary on a session bus, and the same as every other desktop service.

## Alternatives considered

- **Generating an adaptor and a proxy from the XML** with `qdbusxml2cpp`. Its adaptor forwards to methods of the D-Bus names (`CreateSession`), not the facade's, so it needs a hand-written layer of 40 methods anyway; a generator of our own needs Python in the build. The generic bridge is one class on each side and follows the XML by itself.
- **A socket and a protocol of our own.** Faster than D-Bus for bulk data, which is exactly what telemetry is and why that has its own path. Everything else is a few messages a second, and D-Bus brings activation, introspection, `busctl` and the public API with it.
- **Starting the daemon at login always.** Simple, but a process running all day for somebody who opens the window once a month; Auto Run enables the unit only when a session asks for it.
- **Keeping sessions running only while a window is open.** That is how the in-process engine behaves, and what this decision exists to end.
