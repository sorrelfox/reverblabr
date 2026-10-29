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

// One row of a menu, as the prototype's menus list them: the words
// in the body text, and the key that does the same thing muted at the far
// end ("New Session … Ctrl+N"); a tick in front of a chosen row, where the
// prototype writes one; an arrow on a row that opens another menu.
//
// **No icon.** The prototype's menus have none, and the actions a menu
// shows keep theirs for the buttons that show them elsewhere.
//
// Under the pointer or the keyboard a row is raised onto the surface a step
// up, as a library row is; pressed, it sinks into the well.
T.MenuItem {
    id: control

    /// The key that does the same thing as this row, written the way the
    /// desktop writes keys; empty when there is none. **Read from the row's
    /// action, never listened for**: the action's own shortcut is the one
    /// that works, and this only puts it into words.
    readonly property string shortcutText: control.action ? Appearance.shortcutText(control.action.shortcut) : ""

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)

    padding: 0
    horizontalPadding: AppMetrics.rowSpacing
    spacing: AppMetrics.mediumSpacing
    font: AppFonts.body
    opacity: control.enabled ? 1 : AppMetrics.disabledOpacity

    contentItem: Item {
        // Room for the tick in every row of a menu that has one anywhere,
        // so the words of all its rows start in one column.
        readonly property real indicatorRoom: (control.checkable || (control.menu as Menu)?.hasCheckables === true)
                                              && control.indicator ? control.indicator.width + control.spacing : 0
        readonly property real arrowRoom: control.subMenu && control.arrow ? control.arrow.width + control.spacing : 0
        readonly property real keyRoom: key.visible ? key.implicitWidth + AppMetrics.shortcutSpacing : 0

        implicitWidth: indicatorRoom + words.implicitWidth + keyRoom + arrowRoom
        implicitHeight: Math.max(words.implicitHeight, key.implicitHeight)

        IconLabel {
            id: words

            x: control.mirrored ? parent.width - width - parent.indicatorRoom : parent.indicatorRoom
            width: parent.width - parent.indicatorRoom - parent.keyRoom - parent.arrowRoom
            height: parent.height
            mirrored: control.mirrored
            display: T.AbstractButton.TextOnly
            alignment: Qt.AlignLeft | Qt.AlignVCenter

            text: control.text
            font: control.font
            // **A row that destroys something is written in the danger
            // colour**, as the prototype writes its one such row.
            color: control instanceof DestructiveMenuItem ? AppColors.dangerText : AppColors.text
        }

        Text {
            id: key

            x: control.mirrored ? parent.arrowRoom : parent.width - width - parent.arrowRoom
            anchors.verticalCenter: parent.verticalCenter
            visible: control.shortcutText !== "" && !control.subMenu
            text: control.shortcutText
            font: AppFonts.small
            color: AppColors.muted
        }
    }

    indicator: ColorImage {
        x: control.mirrored ? control.width - width - control.rightPadding : control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        width: AppMetrics.menuGlyphSize
        height: AppMetrics.menuGlyphSize
        sourceSize.width: width
        sourceSize.height: height

        visible: control.checked
        source: control.checkable ? AppGlyphs.check : ""
        color: AppColors.text
    }

    arrow: ColorImage {
        x: control.mirrored ? control.leftPadding : control.width - width - control.rightPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        width: AppMetrics.menuGlyphSize
        height: AppMetrics.menuGlyphSize
        sourceSize.width: width
        sourceSize.height: height

        visible: control.subMenu
        mirror: control.mirrored
        source: control.subMenu ? AppGlyphs.submenu : ""
        color: AppColors.muted
    }

    background: Rectangle {
        implicitWidth: AppMetrics.menuMinimumWidth - AppMetrics.menuPadding * 2
        implicitHeight: AppMetrics.mediumControlHeight
        radius: AppMetrics.smallRadius
        color: control.down ? AppColors.well
             : control.highlighted ? AppColors.surface2
             : "transparent"
    }
}
