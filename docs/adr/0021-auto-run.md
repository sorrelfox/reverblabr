<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0021: Auto Run is a session setting; the background engine starts those sessions, and keeps its unit enabled while there are any

- **Status:** Accepted
- **Date:** 2026-09-29

## Context

Some sessions should always be running: a microphone chain for calls, a stream of the system's sound to a recorder, a hearing chain. That calls for a per-session **Auto Run** setting, Off or On login, and for those sessions to be running after logging in without anybody opening the window.

What there is:
- **The engine runs in `reverblabrd`** (ADR-0017), a D-Bus service with a systemd user unit, `reverblabrd.service` (`Type=dbus`, `WantedBy=graphical-session.target`). Nothing enables the unit; the bus starts it when a window asks. The daemon reopens what was open before (`daemon.json`) but starts nothing.
- **Session settings** (`autoConnect`, `sampleRate`, `latency`) are in the session file and change through the `setSessionSetting` edit, which is undoable and crosses the bus as `ApplyEdit` (ADR-0020).
- **A build tree has no unit installed.** The window then runs its own engine and says so.

## Decision

**The setting is the session's.** `SessionSettings::autoRun` is `"off"` or `"login"`, set with `setSessionSetting` like the others: one step to undo, saved in the file, and nothing new on the bus. A file from a later version with a value this one does not know loads as `"off"`, never as a session started unasked. `SessionsListed` entries carry `autoRun` (a new JSON field, which ADR-0020 allows), so the daemon and the Sessions window know it without opening anything.

**The background engine starts them, every time it starts.** At start, unless in `--safe-mode`, the daemon opens and starts every session set to `"login"`: one only on disk is opened first, one it reopened from `daemon.json` is started where it is. That is at login when the unit is enabled, but also after a crash restart (`Restart=on-failure`) and when a window's first call activates it: "On login" means the session runs whenever the engine does. A person who stops such a session has it stopped until the engine next starts.

**The unit is enabled exactly while any session wants it.** The daemon keeps it in step through the user service manager on the session bus (`org.freedesktop.systemd1.Manager`): `EnableUnitFiles` or `DisableUnitFiles` on `reverblabrd.service`, then `Reload`, as `systemctl --user enable` does, and `GetUnitFileState` to know where it stands. It looks when it starts (a unit left enabled by a session since deleted is disabled), after every change of the setting, after a session opens or closes, and whenever anybody lists the sessions (which is how a session deleted without being open is noticed). It reads the list through `Engine::sessionListing()` rather than `listSessions()`, so looking puts no `SessionsListed` on the bus for every window.

**Only the daemon talks to systemd.** A window never enables or disables anything: the window on the daemon has nothing to do, and a window running its own engine is exactly the case where the unit is not installed. That window says, in the Info tab, that Auto Run needs the background engine.

**What the manager says is final.** A **masked** unit is somebody's decision: said in the log, never unmasked or enabled over. **Not installed** (the manager knows no such unit file) and **no manager on the bus** are said in the log, and nothing is done. The daemon's log is in the journal and in the diagnostics bundle.

## Consequences

- A session set to run at login keeps the engine from exiting when idle, since something runs.
- Turning Auto Run off everywhere, or deleting the last such session, disables the unit again; the next login starts nothing.
- The unit file must ship as `packaging/README.md` says, or Auto Run does nothing but say so.
- The checks that need a real login (enabled unit, log out, log in, sessions running) are made by hand, on the desktop; the tests run against a service manager of their own on a private bus (`bus.autorun`).
- **Not done here:** an XDG autostart entry for the window's tray at login, an optional extra; and a notice in the window when the daemon cannot enable the unit (masked, or no manager), which today is only in the log.

## Alternatives considered

- **Writing the `graphical-session.target.wants` symlink under `$XDG_CONFIG_HOME/systemd/user` by hand.** No manager needed, easy to test, but it ignores where the unit is really installed, what its `[Install]` section says, masking, and presets, and the manager would not know until a reload. `systemctl` goes through the manager; so does this.
- **An XDG autostart entry for `reverblabrd`.** Works without systemd, but Plasma already starts user services through systemd, the unit is what the bus activates, and two ways of starting the same daemon would race at login.
- **Auto Run as the daemon's state (a list in `daemon.json`)** rather than the session's. Then a session exported and imported, or copied to another machine, would lose it, and the Sessions window would have to ask the daemon rather than read the list.
- **Starting the sessions only when started at login** (telling a login start from an activation). Both come through systemd with the same environment, and "always running while the engine runs" is simpler to explain and to rely on.
