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

// What an Input Switch shows on the canvas: A and B, the one passing
// chosen, and a line saying what is heard. **The choice is the control**:
// a click on the other one switches to it over half a second, two with
// Shift, without opening anything.
Item {
    id: face

    /// The block whose face this is, set once the face exists.
    property string blockId: ""

    readonly property bool blockEnabled: SessionModel.revision >= 0 && (SessionModel.block(face.blockId).enabled ?? true)
    readonly property string detail: control.passing

    readonly property alias control: control
    readonly property alias aButton: aChoice
    readonly property alias bButton: bChoice
    readonly property alias captionLabel: captionText

    implicitWidth: Kirigami.Units.gridUnit * 8
    implicitHeight: Kirigami.Units.gridUnit * 5

    InputSwitchControl {
        id: control

        blockId: face.blockId
    }

    ColumnLayout {
        anchors {
            left: parent.left
            right: parent.right
            verticalCenter: parent.verticalCenter
            leftMargin: AppMetrics.faceMargin
            rightMargin: AppMetrics.faceMargin
        }
        spacing: AppMetrics.faceSpacing

        SegmentedFrame {
            Layout.alignment: Qt.AlignHCenter
            enabled: face.blockEnabled

            QQC2.ButtonGroup {
                id: choices
            }

            SegmentedChoice {
                id: aChoice

                text: control.letterA
                font: AppFonts.tileText
                checked: !control.onB
                QQC2.ButtonGroup.group: choices
                Accessible.name: control.nameA
                onClicked: control.switchTo("a", control.slowHeld())
            }

            SegmentedChoice {
                id: bChoice

                text: control.letterB
                font: AppFonts.tileText
                checked: control.onB
                QQC2.ButtonGroup.group: choices
                Accessible.name: control.nameB
                onClicked: control.switchTo("b", control.slowHeld())
            }
        }

        ElidedLabel {
            id: captionText

            text: control.passing
            font: AppFonts.tileCaption
            color: control.crossfading ? AppColors.liveText : AppColors.muted
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            Layout.fillWidth: true
            Accessible.ignored: true
        }
    }
}
