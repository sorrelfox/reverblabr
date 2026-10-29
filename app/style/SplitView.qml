// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import io.github.reverblabr

// Panes side by side, divided as the prototype divides the library from the
// canvas: by a one-pixel line in the border colour, which takes the accent
// while it is under the pointer or being dragged. **Drawn here only so that
// the window keeps its proportions**: Basic's six-pixel bar moved the whole
// canvas; the rest of the split handle's look belongs with the sliders and
// lists.
//
// The line is one pixel, the grip is not: the handle takes the pointer a
// few pixels either side of it, so it is not a one-pixel target.
T.SplitView {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    handle: Rectangle {
        id: line

        readonly property bool horizontal: control.orientation === Qt.Horizontal

        implicitWidth: line.horizontal ? 1 : control.width
        implicitHeight: line.horizontal ? control.height : 1
        color: T.SplitHandle.pressed || T.SplitHandle.hovered ? AppColors.accent : AppColors.border
        containmentMask: Item {
            x: line.horizontal ? -AppMetrics.smallSpacing : 0
            y: line.horizontal ? 0 : -AppMetrics.smallSpacing
            width: line.horizontal ? 1 + AppMetrics.smallSpacing * 2 : line.width
            height: line.horizontal ? line.height : 1 + AppMetrics.smallSpacing * 2
        }
    }
}
