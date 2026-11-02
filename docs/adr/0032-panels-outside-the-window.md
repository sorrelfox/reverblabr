<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0032: Settings panels outside the main window

- **Status:** Accepted
- **Date:** 2026-11-02

## Context

A detached settings panel is an item in the canvas, clipped to its viewport. People want to keep one in view outside the window, over the desktop, looking and working as it does inside: the same card, drag, buttons, controls, live meters, undo and keys, saved across runs.

On Wayland a program can't place an ordinary window, and isn't told where its own window is. Both matter here: a panel has to come back where it was left, and one dragged past the window's edge has to appear under the pointer. These were measured on Plasma 6.8 with Qt 6.11 and layer-shell-qt 6.8:

- A layer surface is placed exactly by its margins and takes the keyboard on a click with `KeyboardInteractivityOnDemand`. Its margins may run past its output, and KWin then draws it on the neighbouring one, with the pointer working on it. Which output it belongs to is fixed as it first appears.
- A press keeps the pointer on the window that took it. That window goes on being told where the pointer is, hundreds of pixels outside itself, until the release.
- An `xdg_popup` is told where it was put, relative to its parent. Asked for far off the top left, with the sliding Qt's popups ask for by default, it is slid to the corner of the work area, and the reply says how far that is from the window: `configure(-1013, -597)` for a window at (1013, 597). It works during a press and disturbs nothing.
- Qt reports that place with the window's own `x` added, and a Wayland window's `x` is where its screen starts, so it has to be subtracted on any screen but the first.
- A popup in a window of its own, on a layer surface like anywhere else, opens outside the card. On Wayland it takes the keyboard, and a combo box whose window loses the keyboard closes its popup, so it closes as it opens.
- A minimised window says it is windowed: `visibility` goes back to `Windowed`. Only `QWindow::isExposed()` tells.
- Qt treats a transient window as its parent for a window shortcut, and `isActive()` as well. A plain window can pass for working where a layer surface, which has no transient parent, does not.

## Decision

- **The same item in a window of its own.** `PanelWindow` is room for the card and its shadow, and the `SettingsPanel` is reparented into it. Nothing is built twice, so what is typed and how far it is scrolled come along.
- **A top-layer surface where there is one**, `PanelLayerWindow`, placed by its margins from the work area, with the keyboard on demand. It is the one kind of window that can come back where it was on Wayland, and the module is already what ADR-0030 probes for. It is not the overlay layer, which is for what must stay over everything.
- **Elsewhere a plain frameless window, transient for the main one.** On X11 it places itself. On a Wayland desktop with no layer shell the desktop places it and moves it, on a press, and its place isn't kept.
- **Dragging past the canvas's edge takes the panel out**, with the panel live on the pointer. `WindowOriginProbe` measures the main window's origin at each press on a header, by the popup above. Moving the item would take the press from the header, so once the pointer has left the canvas `WindowWatch` reads the main window's own events and the card follows them. Dropping a card wholly over the canvas brings it back in, where it was dropped. Where a window can't place itself the drag stays inside.
- **A button in the header does the same on every desktop**, and takes a panel out or back. The attach and close buttons work as they do inside.
- **What a panel gets from the canvas is made up for.** Keys it doesn't use go on to the canvas (`Keys.forwardTo`), so Del and Esc mean what they mean inside. The main window's actions with a shortcut are repeated as `Shortcut`s, so undo, redo and Run work with the keyboard in the panel.
- **Popups are made windows, dialogs go to the main window.** A card-sized window would clip them. `WindowWatch` goes through the panel before each press and key, as rows come and go with a block, and puts them back when the panel goes home. A dialog opens over the main window, where it opens for a panel inside, and that window takes the keyboard. `WindowWatch` keeps the window from hearing that the keyboard went to one of its own popups.
- **The window goes with the main window.** It is shown while the main window is up and exposed, and so hides when that is closed or minimised and comes back with it. It stays up while another program is in front, which is what it is for. Closing the panel, deleting its block and changing session take it away.
- **Remembered with the panel**, in the state file (`WindowState.detachedPanels`): `outside`, the screen's name, and the place on it. A place out of reach with today's screens is brought back; a screen that has gone becomes the main window's.

## Consequences

- A panel outside sits above other programs and above Reverblabr's own windows, the help window included, and can't go under one. A plain window on X11 stays above the main window only.
- The work area is what margins are measured from, since an exclusive zone of zero and the popup's sliding both use it.
- `FloatingLayerWindow` is no longer the one file naming the layer-shell module. `PanelLayerWindow` is the second, for the same reason, and likewise gets no static check.
- A drag of a window on a layer surface waits for each move to land, as ADR-0030 has it, and may end a step behind the pointer.
- A panel dropped over the canvas goes inside even if another program covers the main window there: a layer surface can't tell. It is where it was dropped, once that window is in front.
- The tooltips on a panel stay inside its card.
- `WindowOriginProbe`, the layer surface and the popups' focus can only be seen on a Wayland desktop, so the tests offscreen take the plain window, and feed the probe the answers the desktop gives.

## Alternatives considered

- **An ordinary window, moved by `startSystemMove()`.** Plasma would keep it above the main window and give it the keyboard, but it can't be put back where it was, or appear under the pointer, and the desktop decides where it opens. It is the fallback.
- **An `xdg_popup` as the panel.** It is placed relative to the main window with no need to measure anything. With a grab it closes at the first click elsewhere, and without one it can't have the keyboard, so a panel's fields would need their keys carried across by hand. It would also move with the main window.
- **One full-screen transparent layer surface on each screen, holding every panel.** Popups and dialogs would work untouched. One small meter would then redraw and recomposite a whole screen at its frame rate, input masks would have to follow every card and popup, and a card would be cut at the edge between two screens.
- **Wayland drag and drop**, with a picture of the card as the icon and a full-screen surface to catch the drop. A picture is not a live panel, and the cursor and Esc would be the drag's.
- **Asking KWin where the window is**, through a script or its D-Bus interface. ADR-0030 turned that down for the same reason: an interface with no promise behind it.
- **A transparent full-screen surface to read the pointer's place.** It would take a click meant for something else, and can't be asked during a press.
