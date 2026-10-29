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

// A text area: a session's notes. **The prototype has none**, so it
// is a text field that is taller, in the same frame, with the same padding
// and colours, so that the notes read as one more field of the Info pane.
T.TextArea {
    id: control

    implicitWidth: Math.max(contentWidth + leftPadding + rightPadding,
                            implicitBackgroundWidth + leftInset + rightInset,
                            placeholder.implicitWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(contentHeight + topPadding + bottomPadding,
                             implicitBackgroundHeight + topInset + bottomInset,
                             placeholder.implicitHeight + topPadding + bottomPadding)

    leftPadding: AppMetrics.fieldPadding
    rightPadding: AppMetrics.fieldPadding
    topPadding: AppMetrics.mediumSpacing
    bottomPadding: AppMetrics.mediumSpacing
    font: AppFonts.body
    opacity: control.enabled ? 1 : AppMetrics.disabledOpacity

    color: AppColors.text
    selectionColor: AppColors.accent
    selectedTextColor: AppColors.textOnAccent
    placeholderTextColor: AppColors.muted

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
        implicitHeight: AppMetrics.textAreaHeight
        active: control.activeFocus
        hovered: control.hovered
    }
}
