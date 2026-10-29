<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0024: The panel widget is a Plasma package with a small QML plugin of its own

- **Status:** Accepted
- **Date:** 2026-10-29

## Context

Sessions run in the background engine with the window closed (ADR-0017), and the window's tray icon goes when the window quits. Somebody who keeps a session running all day wants to see it and stop it from the panel, without opening anything: a Plasma widget, "Reverblabr Sessions", with Reverblabr's icon in the panel and every saved session, with Run, Stop and a small level bar, in its popup.

What there is to work with:
- **Everything about sessions is on the public API**, `io.github.reverblabr.Engine1` (ADR-0020): the list, each session's state, Run and Stop, recordings starting and ending. Calls are answered by signals.
- **Levels are not on it.** They are in a sealed memfd per open session, handed out by `io.github.reverblabr.Telemetry.Share()` as a file descriptor, to a reader of the same build (ADR-0018). Plain QML cannot map memory.
- **Plasma 6 has a QML module for D-Bus**, `org.kde.plasma.workspace.dbus` (installed with plasma-workspace 6.7 here). It can call methods and watch signals, but not map a descriptor. It is a workspace module, not a framework with a promise of stability.
- **The engine stays while anybody who has called it is still on the bus**, and leaves a minute after nobody is and nothing runs (ADR-0017). The desktop shell never leaves the bus. A widget that called the engine on the shell's connection would keep it running for the whole session, and one that asked for the list at login would start it at every login, for everybody who has the widget.
- **The shell runs every widget in one process**: the widget must cost nothing while nothing runs, and must not bring the engine's code, PipeWire or the window's into that process.
- **Translations**: Plasma gives every widget's QML the translation domain `plasma_applet_<id>`, on the widget's own context, through the global `i18n` calls. The shell's QML engine is shared by every widget, so a singleton such as `KI18n` would read one domain for all of them.

## Decision

**A Plasma package, `io.github.reverblabr.sessions`, and a QML module of our own, `io.github.reverblabr.plasmoid`, which the package imports.** Both are built from `plasmoid/` and installed by the normal build (`RB_BUILD_PLASMOID`, on by default; nothing of Plasma is needed to build them).

- **The package** is `metadata.json` and `contents/ui/`. The compact representation is the icon with a dot in the theme's positive colour while any session runs and a red one while any records. The full one is the list, drawn with Plasma's components and Kirigami's theme colours, so it follows the Plasma theme, light or dark. Its words are the global `i18nc` calls, in the widget's domain, extracted into `plasma_applet_io.github.reverblabr.sessions.pot`.
- **The plugin** is one model, `SessionList`, in C++ with QtCore, QtDBus and QtQml only, and `reverblabr-telemetry`: the memfd reader, split out of the engine's library for this so that it carries nothing else. The layering check holds it to that.
- **Sessions through the public API, levels through the telemetry handshake.** The handshake is not public, but the widget is part of Reverblabr: it is built and installed with the engine, and its memory is checked against the same layout fingerprint a window's is. A widget of another build shows no levels rather than wrong ones.
- **It listens on the shell's connection and calls on its own.** The signals it uses (`SessionsListed`, `StateChanged`, `SessionOpened`, `SessionClosed`, the recording files' start and end, `Error`) are heard on the shell's connection, which never calls the engine. Every call goes through a connection of the widget's own, opened for it and closed three seconds after the last answer it waits for. So the engine counts the widget as a client only for those seconds.
- **It never starts the engine by being there.** At load it asks the bus whether the engine has its name. Only opening the popup asks for the list with activation, as opening the window does, and Run on a session that is not open opens it in the engine first. A new engine on the bus is asked for its list once, without activation.
- **It costs nothing while nothing runs.** Nothing is asked again on a timer. The levels are read twenty times a second only while the popup is open and a session runs, and the memory is let go when the popup closes.

## Consequences

- With the window closed, the widget shows what runs and records, and Run and Stop work. It does not keep a stopped engine alive, and a login without running sessions starts nothing because of it.
- The plugin is a shared library in the shell's process, so the telemetry reader is now a position-independent library of its own, linked by the engine as before.
- **Recording is known from what the widget has heard.** The API has no question for "is this session recording now", so a widget added while a session already records shows the dot from the next file on. Adding that question to `Engine1` would close the gap.
- **The widget's name and description in the widget explorer are not translated yet**: they are in `metadata.json`, which the build does not merge translations into.
- `plasmoid.list` and `plasmoid.representations` test the model and the representations offscreen against an engine of the test's own on a private bus with no activation files, including that the widget lets the engine go and starts none. `plasmoid.package` checks the package with `kpackagetool6`. What only a desktop shows (the widget in a real panel, the theme switching, the shell's CPU while idle) is checked by hand.
- Distribution packages ship two more paths: the package under `share/plasma/plasmoids/` and the module under the QML directory.

## Alternatives considered

- **Plain QML with `org.kde.plasma.workspace.dbus`.** No compiled code, but no levels, since nothing in it can map the memory; a module from plasma-workspace rather than a framework; and its calls go through the shell's own connection, which would keep the engine running for the whole desktop session.
- **Levels over the bus at a low rate.** A new signal on the public API that every client would get, sent whether anybody looks or not, which is what ADR-0018 removed for the window.
- **A C++ applet (`Plasma::Applet` with the package compiled in).** Needs libplasma's development files to build, for nothing the QML module does not already give.
- **Using the window's model, `SessionOverview`.** It is built on the engine facade and the window's shell, which would bring the engine's code and the window's into the desktop shell.
