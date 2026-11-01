<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0030: Floating surfaces on the layer shell

- **Status:** Accepted
- **Date:** 2026-10-31

## Context

Speech Now shows a word beside a call's video, over other programs, and later blocks will put a level bar and a strip there too. A window has to stay above a maximised program. On Wayland `Qt.WindowStaysOnTopHint` is only a hint, and nothing else in Qt asks for it. KWin offers `zwlr_layer_shell_v1`, and `layer-shell-qt` lets a Qt window be such a surface through a QML module. That module is one more thing a desktop may not have, on Linux distributions in general and in the headless containers the tests run in.

The surfaces must also go on with the main window closed, over every open session, while the tray reads nothing with its popover shut and `Meters` follows the one session in front. A stopped session, an overloaded one and an engine that has died must all stop them from showing their last word.

## Decision

- **A runtime dependency, optional.** `layer-shell-qt`'s QML module `org.kde.layershell` is imported at run time only, so nothing is linked and nothing is found at build time. Without it the surfaces still work.
- **One file names the module:** `app/qml/FloatingLayerWindow.qml`, marked `QT_QML_SKIP_QMLLINT` and `QT_QML_SKIP_CACHEGEN`. `FloatingWindows.qml` loads it by name with `Qt.createComponent`, only when `Qt.platform.pluginName` is `wayland`. The load is the probe.
- **Elsewhere it is a plain window** with `Qt.WindowStaysOnTopHint`, and the block's card says the desktop may not keep it on top. A compositor with no layer shell is a Wayland session that passes the probe and gets an ordinary window from the library, with no such line on the card. Plasma has the protocol, and ADR-0004 makes Plasma the target.
- **An overlay-layer surface placed by its margins** from the top left of the screen, with no keyboard focus and an exclusive zone of -1, so it reserves no space and its margins are measured from the screen's edges.
- **One component, `FloatingWindow.qml`, two stored placements.** The label stack is kept in pixels and is dragged. The strip is kept in percent of the screen and lets clicks through. A placement belongs to this desktop, so it is in the state file through `WindowState`, and not in a session.
- **`FloatingSurface`, a C++ singleton beside `TrayState`,** runs its own 30 Hz timer while a row exists and reads `AppShell::telemetryOf()`. It takes the list of blocks from `TrayState`, which already follows every session, and says a change only when a row would look different. Main.qml hosts the window, and closes it with the main window when there is no tray, or it would keep the application running with no way back.
- **`TelemetryWatch` is what every surface goes by.** A block is not watched when it is not published, when the snapshot says `shedding` (added in telemetry version 4), or when `frame` has stood still for 500 ms.

## Consequences

- Nothing about the build changes, and a desktop without the module loses only the promise of staying on top.
- `FloatingLayerWindow.qml` gets no static check, so a mistake in it shows only on a Wayland session. It holds nothing but the attached properties.
- A drag on a layer surface moves the margins from the pointer's position, as the desktop can't move it. On a plain Wayland window the desktop moves it and Reverblabr can't read where to, so the place set on the card is the only one kept there.
- Being on the overlay layer, the label may be carried along in a full-screen share.
- A surface closes with the application. The daemon draws nothing.

## Alternatives considered

- **Linking `LayerShellQt::Interface` from C++.** It would make a devel package a build dependency for one window property, and needs a seam for builds without it, for what a QML import gives for nothing.
- **A KWin window rule, or asking KWin through a script.** That is a person's setting, or an interface with no promise behind it.
- **A plasmoid on the desktop.** It sits under other windows, which is where a call's video is.
- **A climbing count published by each block, to tell a stopped block from a quiet one.** Blocks are out of slots, and it could not tell overload from a stop in words. The frame and the snapshot say the same for every block.
