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

// A button, as the prototype draws one ([ADR-0012]).
//
// **Three looks, from the prototype's own buttons:**
// - plain: a raised surface in a border, the label in the text colour (a
//   panel's buttons, "Remove all blocks");
// - chosen, or `highlighted`: filled with the accent, the label in white
//   (a primary button, the chosen side of a segmented row);
// - flat: nothing behind it until it is hovered or chosen, and the smaller
//   corner of a choice inside a segmented row's frame.
//
// Under the pointer a fill goes a step brighter, as the prototype's
// `filter: brightness(1.1)` does; pressed, a step darker.
T.Button {
    id: control

    readonly property bool filled: control.checked || control.highlighted
    readonly property color fill: control.filled ? AppColors.accent
                                                 : control.flat ? (control.hovered || control.down ? AppColors.surface2
                                                                                                   : "transparent")
                                                                : AppColors.surface2
    readonly property color ink: control.filled ? AppColors.textOnAccent : AppColors.text

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    padding: 0
    horizontalPadding: AppMetrics.buttonPadding
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
        implicitHeight: AppMetrics.mediumControlHeight
        radius: control.flat ? AppMetrics.smallRadius : AppMetrics.controlRadius
        color: control.down ? Qt.darker(control.fill, 1.1)
             : control.hovered && !control.flat ? Qt.lighter(control.fill, 1.1)
             : control.fill
        border.width: control.flat ? 0 : 1
        border.color: control.filled ? AppColors.accent : control.visualFocus ? AppColors.accentMark : AppColors.controlBorder

        FocusRing {
            shown: control.visualFocus
            controlRadius: parent.radius
        }
    }
}
