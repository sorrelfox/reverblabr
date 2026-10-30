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

// The Pitch Shift's tile: the shift, "+3 st", and a slider of whole
// semitones under it. The cents are in the number, not on the slider.
Item {
    id: face

    /// The block whose face this is, set once the face exists.
    property string blockId: ""

    readonly property var params: SessionModel.paramRevision >= 0 ? SessionModel.params(face.blockId) : ({})
    readonly property int semitones: face.params.semitones ?? 0
    readonly property real cents: face.params.cents ?? 0
    readonly property bool blockEnabled: SessionModel.revision >= 0 && (SessionModel.block(face.blockId).enabled ?? true)
    /// The Effects colour, or grey for a block that is off.
    readonly property color tint: face.blockEnabled
        ? AppColors.categoryTint(BlockPresentation.category("effect.pitchshift"))
        : AppColors.muted

    /// The block's range; tst_pitchshift.qml holds it against the catalogue.
    readonly property int widestSemitones: 12

    readonly property alias slider: faceSlider
    readonly property alias shiftLabel: shiftText
    readonly property alias caption: captionText

    implicitWidth: Kirigami.Units.gridUnit * 12
    implicitHeight: Kirigami.Units.gridUnit * 6

    PitchShiftWords {
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
                id: shiftText

                // The slider's own value while it moves, so the number
                // follows a drag.
                text: words.shift((faceSlider.pressed ? faceSlider.value : face.semitones) + face.cents / 100)
                font: AppFonts.readout
                lineHeightMode: AppMetrics.followsSystem ? Text.ProportionalHeight : Text.FixedHeight
                lineHeight: AppMetrics.followsSystem ? 1.0 : AppFonts.readoutPixels
                color: face.blockEnabled ? AppColors.text : AppColors.muted
                Layout.alignment: Qt.AlignBaseline
                Layout.maximumWidth: face.width
            }

            Item {
                Layout.fillWidth: true
            }

            ElidedLabel {
                id: captionText

                text: KI18n.i18nc("@label what the number on a Pitch Shift tile is", "pitch")
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
            from: -face.widestSemitones
            to: face.widestSemitones
            stepSize: 1
            snapMode: QQC2.Slider.SnapAlways
            Accessible.name: KI18n.i18nc("@label:slider how far the pitch is moved, in semitones", "Semitones")

            onMoved: SessionModel.setParam(face.blockId, "semitones", Math.round(value), true)
            onPressedChanged: if (!pressed) {
                SessionModel.commitParam(face.blockId, "semitones");
            }

            SessionBound {
                control: faceSlider
                property: "value"
                value: face.semitones
                suspended: faceSlider.pressed
            }
        }
    }
}
