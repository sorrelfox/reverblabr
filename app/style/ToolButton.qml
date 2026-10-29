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

// A tool button, as the prototype draws the header's and a panel's:
// a 28-pixel square with nothing behind it and the icon in the muted
// colour, which lifts to the text colour on a raised surface under the
// pointer. One that stays pressed — a mode that is on — keeps the raised
// surface and the text colour, so that "on" can be told from "off" without
// hovering.
T.ToolButton {
    id: control

    readonly property bool lifted: control.hovered || control.down || control.checked || control.highlighted
    readonly property color ink: control.lifted || control.visualFocus ? AppColors.text : AppColors.muted

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    // A square round an icon; room either side of words.
    padding: (AppMetrics.controlHeight - AppMetrics.buttonIconSize) / 2
    horizontalPadding: control.display === T.AbstractButton.IconOnly ? control.padding : AppMetrics.buttonPadding
    spacing: AppMetrics.buttonSpacing
    font: AppFonts.button
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
        implicitWidth: AppMetrics.controlHeight
        implicitHeight: AppMetrics.controlHeight
        radius: AppMetrics.controlRadius
        color: control.down ? Qt.darker(AppColors.surface2, 1.1)
             : control.lifted ? AppColors.surface2
             : "transparent"

        FocusRing {
            shown: control.visualFocus
            controlRadius: parent.radius
        }
    }
}
