// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls.Basic.impl
import QtQuick.Templates as T
import io.github.reverblabr

// A number with steps. **The prototype has none**, so it is its text
// field with the steps a number field has in a page: a pair of the
// prototype's chevrons stacked at the end, up above down, each half of the
// field's height, lit under the pointer.
T.SpinBox {
    id: control

    // The steps' width is in the padding, as Basic's.
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentItem.implicitWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             up.implicitIndicatorHeight + down.implicitIndicatorHeight)

    padding: 0
    leftPadding: control.mirrored ? AppMetrics.stepperWidth : 0
    rightPadding: control.mirrored ? 0 : AppMetrics.stepperWidth
    font: AppFonts.body
    opacity: control.enabled ? 1 : AppMetrics.disabledOpacity

    validator: IntValidator {
        locale: control.locale.name
        bottom: Math.min(control.from, control.to)
        top: Math.max(control.from, control.to)
    }

    contentItem: TextInput {
        z: 2
        text: control.displayText
        clip: width < implicitWidth
        leftPadding: AppMetrics.fieldPadding
        rightPadding: AppMetrics.smallSpacing
        topPadding: AppMetrics.fieldVerticalPadding
        bottomPadding: AppMetrics.fieldVerticalPadding

        font: control.font
        color: AppColors.text
        selectionColor: AppColors.accent
        selectedTextColor: AppColors.textOnAccent
        horizontalAlignment: Qt.AlignLeft
        verticalAlignment: Qt.AlignVCenter

        readOnly: !control.editable
        // **The focus goes to the field, so the field says what the box is**,
        // as the desktop style's does: otherwise a screen reader
        // reads "text, 250" with nothing to say what 250 is.
        Accessible.name: control.Accessible.name
        Accessible.description: control.Accessible.description
        validator: control.validator
        inputMethodHints: control.inputMethodHints

        ContextMenu.menu: TextEditingContextMenu {
            editor: parent
        }
    }

    up.indicator: Rectangle {
        x: control.mirrored ? 0 : control.width - width
        y: 0
        implicitWidth: AppMetrics.stepperWidth
        implicitHeight: AppMetrics.mediumControlHeight / 2
        height: control.height / 2
        radius: AppMetrics.controlRadius
        color: control.up.pressed ? AppColors.surface2 : "transparent"

        Chevron {
            anchors.centerIn: parent
            up: true
            color: control.up.hovered || control.up.pressed ? AppColors.text : AppColors.muted
            opacity: control.value < Math.max(control.from, control.to) || control.wrap ? 1 : AppMetrics.disabledOpacity
        }
    }

    down.indicator: Rectangle {
        x: control.mirrored ? 0 : control.width - width
        y: control.height / 2
        implicitWidth: AppMetrics.stepperWidth
        implicitHeight: AppMetrics.mediumControlHeight / 2
        height: control.height / 2
        radius: AppMetrics.controlRadius
        color: control.down.pressed ? AppColors.surface2 : "transparent"

        Chevron {
            anchors.centerIn: parent
            color: control.down.hovered || control.down.pressed ? AppColors.text : AppColors.muted
            opacity: control.value > Math.min(control.from, control.to) || control.wrap ? 1 : AppMetrics.disabledOpacity
        }
    }

    background: FieldFrame {
        active: control.activeFocus
        hovered: control.hovered
    }
}
