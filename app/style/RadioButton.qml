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

// A radio button, as the prototype draws a panel's: a 15-pixel ring
// in the border colour, which turns to the accent with a 7-pixel dot in it
// once it is the one chosen.
T.RadioButton {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)

    padding: AppMetrics.smallSpacing
    horizontalPadding: 0
    // **A line of text tall**, as the prototype's rows are: the room
    // above and below a choice is the gap between rows, which the panel
    // around it sets, rather than padding of its own on top of that.
    verticalPadding: 0
    spacing: AppMetrics.rowSpacing
    font: AppFonts.body
    opacity: control.enabled ? 1 : AppMetrics.disabledOpacity

    indicator: Rectangle {
        implicitWidth: AppMetrics.indicatorSize
        implicitHeight: AppMetrics.indicatorSize
        x: control.text ? (control.mirrored ? control.width - width - control.rightPadding : control.leftPadding)
                        : control.leftPadding + (control.availableWidth - width) / 2
        y: control.topPadding + (control.availableHeight - height) / 2
        radius: width / 2
        color: "transparent"
        border.width: 1
        border.color: control.checked || control.visualFocus || control.hovered ? AppColors.accentMark : AppColors.controlBorder

        Rectangle {
            anchors.centerIn: parent
            width: AppMetrics.radioDotSize
            height: AppMetrics.radioDotSize
            radius: width / 2
            color: AppColors.accentMark
            visible: control.checked
        }

        FocusRing {
            shown: control.visualFocus
            controlRadius: parent.radius
        }
    }

    contentItem: CheckLabel {
        leftPadding: control.indicator && !control.mirrored ? control.indicator.width + control.spacing : 0
        rightPadding: control.indicator && control.mirrored ? control.indicator.width + control.spacing : 0

        text: control.text
        font: control.font
        color: AppColors.text
    }
}
