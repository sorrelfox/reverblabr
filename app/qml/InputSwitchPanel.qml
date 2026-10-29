// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.ki18n

// The Input Switch's settings, as the design draws them: the two
// inputs named one above the other, a switch between them whose knob sits
// beside the one passing, and a line saying what is heard.
//
// **No presets row**: a preset of which input is passing is a click.
ColumnLayout {
    id: panel

    /// The block whose settings these are, set once the panel exists.
    property string blockId: ""

    readonly property bool wantsPresets: false

    readonly property alias control: control
    readonly property alias toggle: switchArea
    readonly property alias explanation: explanationText

    spacing: AppMetrics.panelRowGap

    InputSwitchControl {
        id: control

        blockId: panel.blockId
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: AppMetrics.labelSpacing

        ColumnLayout {
            spacing: Kirigami.Units.largeSpacing

            QQC2.Label {
                text: control.nameA
                font.weight: Font.DemiBold
                color: control.onB ? AppColors.muted : AppColors.text
            }

            QQC2.Label {
                text: control.nameB
                font.weight: Font.DemiBold
                color: control.onB ? AppColors.text : AppColors.muted
            }
        }

        // A switch standing up: the knob beside the input that passes. It
        // moves with the crossfade while the block runs.
        Rectangle {
            id: pill

            implicitWidth: Kirigami.Units.gridUnit * 1.3
            implicitHeight: Kirigami.Units.gridUnit * 2.8
            radius: width / 2
            color: AppColors.accent

            Rectangle {
                readonly property real inset: (pill.width - width) / 2

                width: pill.width - Kirigami.Units.smallSpacing * 1.5
                height: width
                radius: width / 2
                x: inset
                y: inset + control.position * (pill.height - height - inset * 2)
                color: AppColors.surface

                Behavior on y {
                    enabled: !control.crossfading
                    NumberAnimation {
                        duration: Kirigami.Units.shortDuration
                    }
                }
            }

            MouseArea {
                id: switchArea

                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: mouse => control.toggle((mouse.modifiers & Qt.ShiftModifier) !== 0)
            }

            Accessible.role: Accessible.Button
            Accessible.name: KI18n.i18nc("@action:button switch the Input Switch to its other input",
                                         "Switch to %1", control.onB ? control.nameA : control.nameB)
            Accessible.onPressAction: control.toggle(false)
            activeFocusOnTab: true
            Keys.onSpacePressed: event => control.toggle((event.modifiers & Qt.ShiftModifier) !== 0)
        }

        QQC2.Label {
            id: explanationText

            text: control.onB
                ? KI18n.i18nc("@info what the Input Switch does now", "Sound from input B goes through; input A is not heard.")
                : KI18n.i18nc("@info what the Input Switch does now", "Sound from input A goes through; input B is not heard.")
            color: AppColors.muted
            font: AppFonts.small
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }

    InfoLine {
        text: KI18n.i18nc("@info how the Input Switch switches",
                          "Click to crossfade over half a second; with Shift, over two seconds.")
    }
}
