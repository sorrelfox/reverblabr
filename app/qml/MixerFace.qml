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

// What a Mixer shows on the canvas: the master volume, how many of
// its five inputs have something wired in, and a fader for the master.
//
// **Laid out like the Volume tile**, as the design draws the Mixer's face:
// the number, what it is beside it, and the fader under them in the
// Advanced colour. The strips are the panel's; a tile has room for one
// control, and the one that moves everything is the master.
Item {
    id: face

    /// The block whose face this is, set once the face exists.
    property string blockId: ""

    readonly property var params: SessionModel.revision >= 0 ? SessionModel.params(face.blockId) : ({})
    readonly property real master: face.params.master ?? 1
    readonly property bool blockEnabled: SessionModel.revision >= 0 && (SessionModel.block(face.blockId).enabled ?? true)
    readonly property color tint: face.blockEnabled
        ? AppColors.categoryTint(BlockPresentation.category("advanced.mixer"))
        : AppColors.muted
    /// How many of the five inputs something is wired into.
    readonly property int inputsInUse: {
        const lanes = [];
        const wires = SessionModel.revision >= 0 ? SessionModel.wiresTouching(face.blockId) : [];
        for (const wire of wires) {
            const lane = Math.floor(wire.toPort / 2);
            if (wire.toBlock === face.blockId && lanes.indexOf(lane) < 0) {
                lanes.push(lane);
            }
        }
        return lanes.length;
    }

    readonly property string caption: KI18n.i18ncp("@info how many of the Mixer's five inputs are in use",
                                                   "%1 of 5 inputs", "%1 of 5 inputs", face.inputsInUse)
    readonly property string detail: KI18n.i18nc("@info:tooltip the Mixer's master volume and inputs, '100 %, 3 of 5 inputs'",
                                                 "Master %1 %, %2", Math.round(face.master * 100), face.caption)

    readonly property alias masterLabel: masterText
    readonly property alias captionLabel: captionText
    readonly property alias slider: faceSlider

    implicitWidth: Kirigami.Units.gridUnit * 12
    implicitHeight: Kirigami.Units.gridUnit * 6

    ColumnLayout {
        anchors {
            left: parent.left
            right: parent.right
            verticalCenter: parent.verticalCenter
            leftMargin: Kirigami.Units.smallSpacing * 3
            rightMargin: Kirigami.Units.smallSpacing * 3
        }
        spacing: Kirigami.Units.smallSpacing * 2

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            QQC2.Label {
                id: masterText

                text: KI18n.i18nc("@info a volume as a percentage", "%1 %", Math.round(faceSlider.value))
                font: AppFonts.readout
                lineHeightMode: AppMetrics.followsSystem ? Text.ProportionalHeight : Text.FixedHeight
                lineHeight: AppMetrics.followsSystem ? 1.0 : AppFonts.readoutPixels
                color: face.blockEnabled ? AppColors.text : AppColors.muted
                Layout.alignment: Qt.AlignBaseline
            }

            Item {
                Layout.fillWidth: true
            }

            ElidedLabel {
                id: captionText

                text: face.caption
                font: AppFonts.tileCaption
                color: AppColors.muted
                elide: Text.ElideRight
                Layout.alignment: Qt.AlignBaseline
                Accessible.ignored: true
            }
        }

        FaceSlider {
            id: faceSlider

            Layout.fillWidth: true
            Layout.topMargin: AppMetrics.followsSystem ? 0 : (AppMetrics.sliderGrooveHeight - implicitHeight) / 2
            Layout.bottomMargin: AppMetrics.followsSystem ? 0 : (AppMetrics.sliderGrooveHeight - implicitHeight) / 2
            tint: face.tint
            from: 0
            to: 200
            Accessible.name: KI18n.i18nc("@label:slider", "Master volume")

            onMoved: SessionModel.setParam(face.blockId, "master", value / 100, true)
            onPressedChanged: if (!pressed) {
                SessionModel.commitParam(face.blockId, "master");
            }

            SessionBound {
                control: faceSlider
                property: "value"
                value: face.master * 100
                suspended: faceSlider.pressed
            }
        }
    }
}
