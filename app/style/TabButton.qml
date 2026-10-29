// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls.impl
import QtQuick.Templates as T
import io.github.reverblabr

// A tab, as the prototype draws the library's: 30 pixels high with a
// small corner. The one in front sits on a raised surface inside a
// hairline, its label in the text colour and heavier; the others are muted
// words on nothing, which come up to the text colour under the pointer.
//
// **The session tabs are not these.** They are the window's own row across
// the header, underlined rather than raised, and SessionTabs.qml draws them.
T.TabButton {
    id: control

    readonly property color ink: control.checked || control.hovered || control.visualFocus ? AppColors.text
                                                                                           : AppColors.muted

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    padding: 0
    horizontalPadding: AppMetrics.buttonPadding
    // The prototype's 7 between a tab's icon and its label.
    spacing: AppMetrics.buttonSpacing - 1
    font: control.checked ? AppFonts.strong : AppFonts.body
    opacity: control.enabled ? 1 : AppMetrics.disabledOpacity

    icon.width: AppMetrics.buttonIconSize
    icon.height: AppMetrics.buttonIconSize
    icon.color: control.ink

    contentItem: IconLabel {
        spacing: control.spacing
        mirrored: control.mirrored
        display: control.display

        icon: control.icon
        text: control.text
        font: control.font
        color: control.ink
    }

    background: Rectangle {
        implicitHeight: AppMetrics.sidebarTabHeight
        radius: AppMetrics.smallRadius
        color: control.checked ? AppColors.surface2 : "transparent"
        // `box-shadow: inset 0 0 0 1px var(--border)`: a hairline inside
        // the edge, which a border is.
        border.width: control.checked ? 1 : 0
        border.color: AppColors.border

        FocusRing {
            shown: control.visualFocus
            controlRadius: parent.radius
        }
    }
}
