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

// A text field, as the prototype draws a panel's: 26 pixels high,
// the words eight pixels in from a one-pixel border, the border in the
// accent while it is typed into. What it says before anything is typed is
// in the muted colour; what is selected is in the accent.
//
// Kirigami's search field is this field with its icons in it, so the
// library's search is drawn by it too.
T.TextField {
    id: control

    implicitWidth: implicitBackgroundWidth + leftInset + rightInset
                   || Math.max(contentWidth, placeholder.implicitWidth) + leftPadding + rightPadding
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding,
                             placeholder.implicitHeight + topPadding + bottomPadding)

    leftPadding: AppMetrics.fieldPadding
    rightPadding: AppMetrics.fieldPadding
    topPadding: AppMetrics.fieldVerticalPadding
    bottomPadding: AppMetrics.fieldVerticalPadding
    font: AppFonts.body
    opacity: control.enabled ? 1 : AppMetrics.disabledOpacity

    color: AppColors.text
    selectionColor: AppColors.accent
    selectedTextColor: AppColors.textOnAccent
    placeholderTextColor: AppColors.muted
    verticalAlignment: TextInput.AlignVCenter

    ContextMenu.menu: textMenu.menu

    TextMenu {
        id: textMenu

        editor: control
    }

    PlaceholderText {
        id: placeholder

        x: control.leftPadding
        y: control.topPadding
        width: control.width - (control.leftPadding + control.rightPadding)
        height: control.height - (control.topPadding + control.bottomPadding)

        text: control.placeholderText
        font: control.font
        color: control.placeholderTextColor
        verticalAlignment: control.verticalAlignment
        visible: !control.length && !control.preeditText
                 && (!control.activeFocus || control.horizontalAlignment !== Qt.AlignHCenter)
        elide: Text.ElideRight
        renderType: control.renderType
    }

    background: FieldFrame {
        active: control.activeFocus
        hovered: control.hovered
    }
}
