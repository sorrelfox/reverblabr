// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

// One of the square buttons at the header's end: the theme, the menu,
// and minimise, maximise and close, as the prototype draws them — a glyph
// in a 28-pixel square that only shows its square under the pointer.
//
// **The words are the button's even though it shows none**: they are its
// tooltip and what a screen reader says, and a button with no words is one
// nobody can find without a pointer.
QQC2.AbstractButton {
    id: button

    /// The glyph, one of AppGlyphs', or an icon theme's name for it while
    /// following the system.
    property var glyph
    /// How big it is drawn: the window buttons' 22, the theme's 16.
    property real glyphSize: AppMetrics.buttonIconSize
    /// Muted at rest, as the theme and menu buttons are; the window buttons
    /// are drawn in the text colour.
    property bool muted: false
    /// Close: red under the pointer, with a white glyph.
    property bool destructive: false

    readonly property bool lit: button.hovered || button.down || button.visualFocus

    implicitWidth: AppMetrics.windowButtonSize
    implicitHeight: AppMetrics.windowButtonSize
    padding: 0
    display: QQC2.AbstractButton.IconOnly

    Accessible.role: Accessible.Button
    Accessible.name: button.text

    QQC2.ToolTip.text: button.text
    QQC2.ToolTip.visible: button.hovered && button.text !== ""
    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

    background: Rectangle {
        radius: AppMetrics.controlRadius
        color: !button.lit ? "transparent"
             : button.destructive ? AppColors.danger
             : AppColors.surface2
        border.width: button.visualFocus ? 1 : 0
        border.color: AppColors.accentMark
    }

    contentItem: Item {
        Kirigami.Icon {
            anchors.centerIn: parent
            width: button.glyphSize
            height: button.glyphSize
            source: button.glyph
            isMask: true
            color: button.lit && button.destructive
                 ? (AppColors.followsSystem ? AppColors.textOnAccent : AppColors.inkOnDanger)
                 : button.muted && !button.lit ? AppColors.muted
                 : AppColors.text
        }
    }
}
