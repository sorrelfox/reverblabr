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

// One of the menu bar's titles: the body text, on the raised surface
// while its menu is open or it is under the pointer, as a tool button is.
T.MenuBarItem {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)

    padding: 0
    horizontalPadding: AppMetrics.rowSpacing
    spacing: AppMetrics.buttonSpacing
    font: AppFonts.body

    icon.width: AppMetrics.buttonIconSize
    icon.height: AppMetrics.buttonIconSize
    icon.color: AppColors.text

    contentItem: IconLabel {
        spacing: control.spacing
        mirrored: control.mirrored
        display: control.display
        alignment: Qt.AlignLeft

        icon: control.icon
        text: control.text
        font: control.font
        color: AppColors.text
    }

    background: Rectangle {
        implicitHeight: AppMetrics.controlHeight
        radius: AppMetrics.controlRadius
        color: control.down || control.highlighted ? AppColors.surface2 : "transparent"
    }
}
