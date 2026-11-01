// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window

// **A small window over other programs** (ADR-0030): what it shows, its size
// and how it is placed. Two surfaces share this and differ in their stored
// placement. The label stack is in pixels and is dragged; the strip is in
// percent of the screen and lets clicks through to what lies under it.
//
// This is the plain window, with a hint that may or may not be kept. Where the
// layer shell is there, FloatingLayerWindow adds its attached properties to
// this and nothing else.
Window {
    id: surface

    /// Which stored placement this is (FloatingWindows).
    property string surfaceName: FloatingWindows.labelStack
    /// What it shows. Its implicit size is the window's.
    property Component contentComponent: null
    /// Dragging moves the margins, rather than asking the desktop to move it.
    property bool movedByMargins: false

    readonly property var placement: FloatingSurface.placementRevision >= 0 ? FloatingSurface.placement(surface.surfaceName) : ({})
    readonly property bool inPercent: surface.placement.unit === "percent"
    readonly property bool isStrip: surface.surfaceName === FloatingWindows.strip
    /// Place Strip is under way here: the one time this takes the keyboard.
    readonly property bool placing: surface.isStrip && FloatingWindows.placing
    /// Lets clicks fall through. Nothing on a strip is for clicking, unless it
    /// is being placed.
    readonly property bool clickThrough: surface.isStrip && !surface.placing
    /// A drag is worked out here and not left to the desktop. A place in
    /// percent has to be written, and the desktop would not say.
    readonly property bool movesItself: surface.movedByMargins || surface.inPercent

    /// The screen to be on when the placement names none: the main window's,
    /// which it learns only once it has opened.
    property var homeScreen: FloatingWindows.screens[0] ?? null
    /// The screen it asks for. **A main window still opening has a screen with
    /// no size yet**, and a place in percent of nothing is the top left corner.
    readonly property var placedOn: {
        const named = FloatingWindows.screenNamed(surface.placement.screen ?? "");
        if (named) {
            return named;
        }
        return (surface.homeScreen?.width ?? 0) > 0 ? surface.homeScreen : FloatingWindows.screens[0];
    }
    /// How far it sits from the screen's left and top edges, in pixels, kept
    /// on the screen. What the layer shell's margins are set from.
    readonly property int leftEdge: surface.edge(surface.placement.left ?? 0, surface.placedOn?.width ?? 0, surface.width)
    readonly property int topEdge: surface.edge(surface.placement.top ?? 0, surface.placedOn?.height ?? 0, surface.height)

    /// Pixels from a stored \a place, on a screen \a across wide, for a
    /// window \a size wide. **A percent is of the room left over**, so 50 is
    /// the middle whatever the size, and 100 is the far edge.
    function edge(place: real, across: real, size: real): int {
        const pixels = surface.inPercent ? place / 100 * Math.max(0, across - size) : place;
        return Math.round(Math.max(0, across > size ? Math.min(pixels, across - size) : pixels));
    }

    /// Whether the desktop says where it moved this window to. X11 does. On
    /// Wayland a plain window is not told (ADR-0030).
    property bool readsItsPlace: Qt.platform.pluginName === "xcb"

    /// Writes down where the desktop has put it, if that is not where it was
    /// asked to be. **Asked of the window, as nothing tells it a move ended:**
    /// the desktop holds the pointer through a system move, so no release
    /// reaches the mouse area. Only a place that came from elsewhere differs
    /// from the edges, so what the card sets is never written back.
    function noteWhereItIs() {
        const on = surface.screen;
        if (!surface.readsItsPlace || surface.movedByMargins || surface.inPercent || !on) {
            return;
        }
        const left = surface.x - on.virtualX;
        const top = surface.y - on.virtualY;
        const moved = on.name !== (surface.placedOn?.name ?? on.name);
        if (left === surface.leftEdge && top === surface.topEdge && !moved) {
            return;
        }
        const changes = { left: left, top: top };
        if (moved && on.name !== "") {
            changes.screen = on.name;
        }
        FloatingSurface.setPlacement(surface.surfaceName, changes);
    }

    /// The keys while placing, for a test.
    readonly property alias placingKeys: keys
    /// The pointer's side of a drag, for a test.
    readonly property alias pointer: drag

    /// \a place kept on a screen \a across wide, for a window \a size wide.
    function onTheScreen(place: real, across: real, size: real): real {
        const most = surface.inPercent ? 100 : Math.max(0, across - size);
        return across > 0 ? Math.max(0, Math.min(place, most)) : place;
    }

    /// Writes down a drag of (\a dx, \a dy) pixels. **Kept on the screen as
    /// it is written:** a stored place past the edge is what the card would
    /// show, and a move back would have that distance to undo first.
    function moveBy(dx: real, dy: real) {
        const wide = surface.placedOn?.width ?? 0;
        const high = surface.placedOn?.height ?? 0;
        const across = surface.inPercent && surface.placedOn ? 100 / Math.max(1, wide - surface.width) : 1;
        const down = surface.inPercent && surface.placedOn ? 100 / Math.max(1, high - surface.height) : 1;
        FloatingSurface.setPlacement(surface.surfaceName, {
            left: surface.onTheScreen((surface.placement.left ?? 0) + dx * across, wide, surface.width),
            top: surface.onTheScreen((surface.placement.top ?? 0) + dy * down, high, surface.height)
        }, true);
    }

    // **Not the main window's**, which it outlasts: closing that one must not
    // take this along.
    transientParent: null
    x: (surface.placedOn?.virtualX ?? 0) + surface.leftEdge
    y: (surface.placedOn?.virtualY ?? 0) + surface.topEdge
    width: Math.max(1, content.implicitWidth)
    height: Math.max(1, content.implicitHeight)
    screen: surface.placedOn
    color: "transparent"
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
        | (surface.placing ? 0 : Qt.WindowDoesNotAcceptFocus)
        | (surface.clickThrough ? Qt.WindowTransparentForInput : 0)

    onXChanged: settled.restart()
    onYChanged: settled.restart()
    // **A layer surface's margins and focus take effect with its next
    // commit**, and a window with nothing new to draw makes none: a strip
    // showing Quiet would not move until something else repainted it.
    onLeftEdgeChanged: surface.update()
    onTopEdgeChanged: surface.update()
    onPlacingChanged: {
        surface.update();
        if (surface.placing) {
            surface.requestActivate();
            keys.forceActiveFocus();
        }
    }
    // Esc's place is still known, and a window that goes must not leave the
    // keyboard promised to nothing.
    Component.onDestruction: {
        if (surface.placing) {
            FloatingWindows.endPlacing(false);
        }
    }

    // A move is many positions, and this is a file in a person's home
    // directory: written once, when it has stopped.
    Timer {
        id: settled

        interval: 300
        onTriggered: surface.noteWhereItIs()
    }

    Loader {
        id: content

        anchors.fill: parent
        sourceComponent: surface.contentComponent
    }

    // Arrows move it by a little and the Page keys by more; Home and End are
    // the sideways Page keys. Written once, as a drag is.
    Item {
        id: keys

        readonly property int stepPixels: 10
        readonly property int pagePixels: 50

        focus: surface.placing
        Keys.onPressed: event => event.accepted = surface.placing && keys.press(event.key)

        /// Does what \a key means while placing, and says whether it means
        /// anything.
        function press(key: int): bool {
            switch (key) {
            case Qt.Key_Left:
                surface.moveBy(-keys.stepPixels, 0);
                return true;
            case Qt.Key_Right:
                surface.moveBy(keys.stepPixels, 0);
                return true;
            case Qt.Key_Up:
                surface.moveBy(0, -keys.stepPixels);
                return true;
            case Qt.Key_Down:
                surface.moveBy(0, keys.stepPixels);
                return true;
            case Qt.Key_Home:
                surface.moveBy(-keys.pagePixels, 0);
                return true;
            case Qt.Key_End:
                surface.moveBy(keys.pagePixels, 0);
                return true;
            case Qt.Key_PageUp:
                surface.moveBy(0, -keys.pagePixels);
                return true;
            case Qt.Key_PageDown:
                surface.moveBy(0, keys.pagePixels);
                return true;
            case Qt.Key_Return:
            case Qt.Key_Enter:
                FloatingWindows.endPlacing(true);
                return true;
            case Qt.Key_Escape:
                FloatingWindows.endPlacing(false);
                return true;
            }
            return false;
        }
    }

    MouseArea {
        id: drag

        property point pressedAt

        /// Follows the pointer, now at (\a x, \a y) in the window. A window
        /// that moves itself has to wait for a move to reach the screen: until
        /// it has, the pointer's place is still measured from the old one, so
        /// the same distance would be added again with each event and the
        /// window would run away from the pointer.
        function follow(x: real, y: real) {
            if (!landing.running) {
                surface.moveBy(x - drag.pressedAt.x, y - drag.pressedAt.y);
                landing.restart();
            }
        }

        anchors.fill: parent
        enabled: !surface.clickThrough
        cursorShape: Qt.SizeAllCursor
        Accessible.ignored: true
        onPressed: mouse => {
            drag.pressedAt = Qt.point(mouse.x, mouse.y);
            if (!surface.movesItself) {
                surface.startSystemMove();
            }
        }
        // The window moves under the pointer, so each move is measured from
        // where the press was and not from the last move.
        onPositionChanged: mouse => {
            if (surface.movesItself && pressed) {
                drag.follow(mouse.x, mouse.y);
            }
        }
        onReleased: {
            if (surface.movesItself && !surface.placing) {
                FloatingSurface.setPlacement(surface.surfaceName, {});
            }
        }

        Timer {
            id: landing

            // Two frames at 60 Hz, and a compositor's own frame.
            interval: 50
        }
    }
}
