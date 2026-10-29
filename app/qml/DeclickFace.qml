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

// What a Declick block shows on the canvas: its threshold, "3.0 dB",
// with "threshold" beside it and a slider under it, as the design draws a
// value tile.
Item {
    id: face

    /// The block whose face this is, set once the face exists.
    property string blockId: ""

    readonly property var params: SessionModel.revision >= 0 ? SessionModel.params(face.blockId) : ({})
    readonly property real threshold: face.params.threshold ?? face.defaultThreshold
    readonly property bool blockEnabled: SessionModel.revision >= 0 && (SessionModel.block(face.blockId).enabled ?? true)
    /// The Advanced colour, or grey for a block that is off.
    readonly property color tint: face.blockEnabled
        ? AppColors.categoryTint(BlockPresentation.category("advanced.declick"))
        : AppColors.muted

    /// The block's range; tst_declick.qml holds it against the catalogue.
    readonly property real highestThreshold: 20
    readonly property real defaultThreshold: 3

    readonly property alias slider: faceSlider
    readonly property alias thresholdLabel: thresholdText
    readonly property alias caption: captionText

    implicitWidth: Kirigami.Units.gridUnit * 12
    implicitHeight: Kirigami.Units.gridUnit * 6

    DeclickWords {
        id: words
    }

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

            ElidedLabel {
                id: thresholdText

                // The slider's own value while it moves, so the number
                // follows a drag.
                text: words.decibels(faceSlider.pressed ? faceSlider.value : face.threshold)
                font: AppFonts.readout
                lineHeightMode: AppMetrics.followsSystem ? Text.ProportionalHeight : Text.FixedHeight
                lineHeight: AppMetrics.followsSystem ? 1.0 : AppFonts.readoutPixels
                color: face.blockEnabled ? AppColors.text : AppColors.muted
                elide: Text.ElideRight
                Layout.alignment: Qt.AlignBaseline
                Layout.maximumWidth: face.width
            }

            Item {
                Layout.fillWidth: true
            }

            QQC2.Label {
                id: captionText

                text: KI18n.i18nc("@label what the number on a Declick tile is", "threshold")
                font: AppFonts.tileCaption
                color: AppColors.muted
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
            to: face.highestThreshold
            stepSize: 0.5
            snapMode: QQC2.Slider.SnapAlways
            Accessible.name: KI18n.i18nc("@label:slider how far a click has to stand out to be taken out", "Threshold")

            onMoved: SessionModel.setParam(face.blockId, "threshold", value, true)
            onPressedChanged: if (!pressed) {
                SessionModel.commitParam(face.blockId, "threshold");
            }

            SessionBound {
                control: faceSlider
                property: "value"
                value: face.threshold
                suspended: faceSlider.pressed
            }
        }
    }
}
