// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import org.kde.kirigami as Kirigami
import io.github.reverblabr

// A list that drops down, as the prototype draws a panel's: a field
// 26 pixels high with the choice six pixels in and the prototype's chevron
// at its end, the border in the accent while the list is open.
//
// **Its list is part of it, not a menu.** The prototype leaves the open
// list to the browser; here it is the field's own frame and colour below
// the field, with the choices as the library's rows are drawn, so a device
// list and a presets list open as part of the panel they are in.
//
// **An icon beside the choice when one is hinted**, through the same
// Kirigami style hint the desktop style draws a list's icon from: the
// application a block captures is named with its program's icon, whichever
// style the window has.
T.ComboBox {
    id: control

    readonly property string hintedIcon: control.Kirigami.StyleHints.iconName !== ""
                                         ? control.Kirigami.StyleHints.iconName
                                         : control.Kirigami.StyleHints.iconSource.toString()
    readonly property real iconRoom: control.hintedIcon !== "" ? AppMetrics.buttonIconSize + control.spacing : 0
    /// How tall the window is, which the open list stays inside.
    readonly property real windowHeight: Window.height

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)

    padding: 0
    spacing: AppMetrics.smallSpacing
    leftPadding: (!control.mirrored || !indicator || !indicator.visible ? 0 : indicator.width + spacing)
                 + (control.mirrored ? 0 : control.iconRoom) + AppMetrics.comboPadding
    rightPadding: (control.mirrored || !indicator || !indicator.visible ? 0 : indicator.width + spacing)
                  + (control.mirrored ? control.iconRoom : 0) + AppMetrics.comboPadding
    font: AppFonts.body
    opacity: control.enabled ? 1 : AppMetrics.disabledOpacity

    delegate: ItemDelegate {
        required property var model
        required property int index

        width: ListView.view.width
        text: model[control.textRole]
        font.weight: control.currentIndex === index ? AppFonts.demiBold : Font.Normal
        highlighted: control.highlightedIndex === index
        hoverEnabled: control.hoverEnabled
    }

    indicator: Chevron {
        x: control.mirrored ? AppMetrics.comboPadding : control.width - width - AppMetrics.comboPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        up: control.popup.visible
    }

    contentItem: T.TextField {
        id: textInput

        implicitHeight: contentHeight + topPadding + bottomPadding
        padding: 0
        topPadding: AppMetrics.fieldVerticalPadding
        bottomPadding: AppMetrics.fieldVerticalPadding

        text: control.editable ? control.editText : control.displayText

        enabled: control.editable
        // **Only an editable box's field is a field**. Otherwise it
        // is where the choice is drawn, and a screen reader offered it as a
        // second, nameless text field inside the box; the desktop style
        // hides it the same way. When it is one, it is called what the box
        // is, since that is where the focus goes.
        Accessible.ignored: !control.editable
        Accessible.name: control.Accessible.name
        Accessible.description: control.Accessible.description
        autoScroll: control.editable
        readOnly: control.down
        inputMethodHints: control.inputMethodHints
        validator: control.validator
        selectByMouse: control.selectTextByMouse

        font: control.font
        color: AppColors.text
        selectionColor: AppColors.accent
        selectedTextColor: AppColors.textOnAccent
        verticalAlignment: Text.AlignVCenter

        ContextMenu.onRequested: position => textMenu.popup(position)

        TextMenu {
            id: textMenu

            editor: textInput
        }
    }

    background: FieldFrame {
        active: control.visualFocus || control.popup.visible || (control.editable && control.activeFocus)
        hovered: control.hovered
        color: control.down && !control.editable ? AppColors.surface2 : AppColors.field

        Kirigami.Icon {
            x: control.mirrored ? parent.width - width - AppMetrics.comboPadding : AppMetrics.comboPadding
            anchors.verticalCenter: parent.verticalCenter
            implicitWidth: AppMetrics.buttonIconSize
            implicitHeight: AppMetrics.buttonIconSize
            source: control.hintedIcon
            visible: control.hintedIcon !== ""
            Accessible.ignored: true
        }

        FocusRing {
            shown: control.visualFocus && !control.editable
            controlRadius: parent.radius
        }
    }

    popup: T.Popup {
        y: control.height + AppMetrics.popupGap
        width: control.width
        height: Math.min(contentItem.implicitHeight + topPadding + bottomPadding,
                         control.windowHeight - topMargin - bottomMargin)
        topMargin: AppMetrics.popupMargin
        bottomMargin: AppMetrics.popupMargin
        padding: AppMetrics.hairSpacing
        font: control.font

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.delegateModel
            currentIndex: control.highlightedIndex
            highlightMoveDuration: 0

            ScrollBar.vertical: ScrollBar {}
        }

        background: Rectangle {
            radius: AppMetrics.controlRadius
            color: AppColors.field
            border.width: 1
            border.color: AppColors.border
        }
    }
}
