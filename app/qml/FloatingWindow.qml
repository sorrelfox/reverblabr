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
    /// Lets clicks fall through. Nothing on a strip is for clicking.
    readonly property bool clickThrough: surface.surfaceName === FloatingWindows.strip

    /// The screen to be on when the placement names none: the main window's.
    property var homeScreen: FloatingWindows.screens[0] ?? null
    /// The screen it asks for.
    readonly property var placedOn: {
        return FloatingWindows.screenNamed(surface.placement.screen ?? "") ?? surface.homeScreen;
    }
    /// How far it sits from the screen's left and top edges, in pixels, kept
    /// on the screen. What the layer shell's margins are set from.
    readonly property int leftEdge: surface.edge(surface.placement.left ?? 0, surface.placedOn?.width ?? 0, surface.width)
    readonly property int topEdge: surface.edge(surface.placement.top ?? 0, surface.placedOn?.height ?? 0, surface.height)

    /// Pixels from a stored \a place, on a screen \a across wide, for a
    /// window \a size wide.
    function edge(place: real, across: real, size: real): int {
        const pixels = surface.inPercent ? place / 100 * across : place;
        return Math.round(Math.max(0, across > size ? Math.min(pixels, across - size) : pixels));
    }

    /// Writes down a drag of (\a dx, \a dy) pixels.
    function moveBy(dx: real, dy: real) {
        const across = surface.inPercent && surface.placedOn ? 100 / surface.placedOn.width : 1;
        const down = surface.inPercent && surface.placedOn ? 100 / surface.placedOn.height : 1;
        FloatingSurface.setPlacement(surface.surfaceName, {
            left: (surface.placement.left ?? 0) + dx * across,
            top: (surface.placement.top ?? 0) + dy * down
        }, true);
    }

    // **Not the main window's**, which it outlasts: closing that one must not
    // take this along.
    transientParent: null
    x: surface.leftEdge
    y: surface.topEdge
    width: Math.max(1, content.implicitWidth)
    height: Math.max(1, content.implicitHeight)
    screen: surface.placedOn
    color: "transparent"
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
        | (surface.clickThrough ? Qt.WindowTransparentForInput : 0)

    Loader {
        id: content

        anchors.fill: parent
        sourceComponent: surface.contentComponent
    }

    MouseArea {
        id: drag

        property point pressedAt

        anchors.fill: parent
        enabled: !surface.clickThrough
        cursorShape: Qt.SizeAllCursor
        Accessible.ignored: true
        onPressed: mouse => {
            drag.pressedAt = Qt.point(mouse.x, mouse.y);
            if (!surface.movedByMargins) {
                surface.startSystemMove();
            }
        }
        // The window moves under the pointer, so each move is measured from
        // where the press was and not from the last move.
        onPositionChanged: mouse => {
            if (surface.movedByMargins && pressed) {
                surface.moveBy(mouse.x - drag.pressedAt.x, mouse.y - drag.pressedAt.y);
            }
        }
        onReleased: {
            if (surface.movedByMargins) {
                FloatingSurface.setPlacement(surface.surfaceName, {});
            }
        }
    }
}
