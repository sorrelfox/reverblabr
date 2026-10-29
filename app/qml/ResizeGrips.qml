// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma ComponentBehavior: Bound

import QtQuick

// The edges of a window with no frame, which resize it.
//
// **The compositor still does the resizing**: a press here hands the pointer
// to it with `startSystemResize()`, so snapping to the other windows and to
// the screen's edges, and the size shown while dragging, are what every other
// window gets. What we do is say where an edge is, since there is no frame for
// the compositor to find one in.
//
// **Inside the window, not outside it.** The shadow outside is the
// compositor's picture, not part of the window, so it takes no presses; the
// grips are a thin band along the inside of the edge, over whatever is
// drawn there, and a wider square in each corner.
Item {
    id: grips

    /// The window the edges belong to.
    required property Window window

    /// Each grip, by the edges it moves, for a test to press.
    readonly property alias grips: repeater

    Repeater {
        id: repeater

        model: [
            { edges: Qt.TopEdge | Qt.LeftEdge, cursor: Qt.SizeFDiagCursor },
            { edges: Qt.TopEdge | Qt.RightEdge, cursor: Qt.SizeBDiagCursor },
            { edges: Qt.BottomEdge | Qt.LeftEdge, cursor: Qt.SizeBDiagCursor },
            { edges: Qt.BottomEdge | Qt.RightEdge, cursor: Qt.SizeFDiagCursor },
            { edges: Qt.TopEdge, cursor: Qt.SizeVerCursor },
            { edges: Qt.BottomEdge, cursor: Qt.SizeVerCursor },
            { edges: Qt.LeftEdge, cursor: Qt.SizeHorCursor },
            { edges: Qt.RightEdge, cursor: Qt.SizeHorCursor }
        ]

        delegate: MouseArea {
            id: grip

            required property var modelData

            readonly property int edges: grip.modelData.edges
            readonly property bool corner: (grip.edges & (Qt.TopEdge | Qt.BottomEdge)) !== 0
                                        && (grip.edges & (Qt.LeftEdge | Qt.RightEdge)) !== 0
            readonly property bool atTop: (grip.edges & Qt.TopEdge) !== 0
            readonly property bool atBottom: (grip.edges & Qt.BottomEdge) !== 0
            readonly property bool atLeft: (grip.edges & Qt.LeftEdge) !== 0
            readonly property bool atRight: (grip.edges & Qt.RightEdge) !== 0
            readonly property real reach: grip.corner ? AppMetrics.resizeCorner : AppMetrics.resizeGrip

            // A corner above the edges beside it, so the square wins.
            z: grip.corner ? 1 : 0
            x: grip.atRight ? grips.width - grip.reach : 0
            y: grip.atBottom ? grips.height - grip.reach : 0
            width: grip.atLeft || grip.atRight ? grip.reach : grips.width
            height: grip.atTop || grip.atBottom ? grip.reach : grips.height
            hoverEnabled: true
            cursorShape: grip.modelData.cursor
            acceptedButtons: Qt.LeftButton
            onPressed: grips.window.startSystemResize(grip.edges)
        }
    }
}
