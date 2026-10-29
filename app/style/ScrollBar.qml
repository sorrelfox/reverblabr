// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Templates as T
import io.github.reverblabr

// A scroll bar, as the prototype's thin ones: `scrollbar-width:
// thin; scrollbar-color: var(--well) transparent`. An eight-pixel band with
// nothing drawn in it, and a four-pixel thumb in the well colour with round
// ends, which widens and lightens while the pointer is over it or it is
// being dragged.
//
// **Shown whenever there is more to scroll to**, as the prototype's are,
// rather than only while scrolling: a library that runs past the bottom of
// the window says so before anybody reaches for the wheel.
T.ScrollBar {
    id: control

    readonly property bool engaged: control.hovered || control.pressed
    readonly property bool needed: control.size < 1.0

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    padding: (AppMetrics.scrollBarWidth - AppMetrics.scrollThumbWidth) / 2
    visible: control.policy !== T.ScrollBar.AlwaysOff
    minimumSize: control.orientation === Qt.Horizontal
                 ? AppMetrics.scrollThumbMinimum / Math.max(1, control.width)
                 : AppMetrics.scrollThumbMinimum / Math.max(1, control.height)

    contentItem: Item {
        implicitWidth: AppMetrics.scrollThumbWidth
        implicitHeight: AppMetrics.scrollThumbWidth
        visible: control.policy === T.ScrollBar.AlwaysOn || control.needed

        Rectangle {
            readonly property real across: control.engaged ? AppMetrics.scrollThumbActiveWidth
                                                           : AppMetrics.scrollThumbWidth

            anchors.centerIn: parent
            width: control.orientation === Qt.Horizontal ? parent.width : across
            height: control.orientation === Qt.Horizontal ? across : parent.height
            radius: across / 2
            color: control.pressed ? AppColors.muted
                 : control.hovered ? AppColors.border
                 : AppColors.well
        }
    }
}
