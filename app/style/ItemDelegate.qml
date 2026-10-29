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

// A row of a list, as the prototype draws the library's: nothing
// behind it until the pointer is over it, then a surface with a small
// corner, and the raised surface while it is pressed.
//
// **The current row is the pressed one's colour**, since the prototype has
// none of its own: a list the keyboard moves through has to say where it
// is. With the keyboard actually in the list, a hairline in the accent goes
// round it too.
T.ItemDelegate {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)

    horizontalPadding: AppMetrics.delegateHorizontalPadding
    verticalPadding: AppMetrics.delegateVerticalPadding
    spacing: AppMetrics.rowSpacing
    font: AppFonts.body
    opacity: control.enabled ? 1 : AppMetrics.disabledOpacity

    icon.width: AppMetrics.buttonIconSize
    icon.height: AppMetrics.buttonIconSize
    icon.color: AppColors.text

    contentItem: IconLabel {
        spacing: control.spacing
        mirrored: control.mirrored
        display: control.display
        alignment: control.display === IconLabel.IconOnly || control.display === IconLabel.TextUnderIcon
                   ? Qt.AlignCenter : Qt.AlignLeft

        icon: control.icon
        text: control.text
        font: control.font
        color: AppColors.text
    }

    background: Rectangle {
        implicitHeight: AppMetrics.controlHeight
        radius: AppMetrics.controlRadius
        color: control.down || control.highlighted ? AppColors.surface2
             : control.hovered ? AppColors.surface
             : "transparent"
        border.width: control.visualFocus ? 1 : 0
        border.color: AppColors.accentMark
    }
}
