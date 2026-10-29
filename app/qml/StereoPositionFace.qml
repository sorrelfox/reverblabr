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

// What a block with one left-to-right setting shows on the canvas:
// where it is set, "C", "L 30" or "R 30", and a slider to move it.
//
// **Laid out like the Volume tile**, as the design draws every value
// face: the setting first in the fixed-width face, a small word saying what
// it is beside it, and under them a thin track in the effects colour.
//
// Balance is the first block to wear it; the register says which parameter
// it moves and what the small word is, so a block that places sound between
// the two sides the same way needs no face of its own.
Item {
    id: face

    /// The block whose face this is, set once the face exists.
    property string blockId: ""
    /// The block's type, for its category colour.
    property string typeId: ""
    /// The parameter the slider moves, −100 … +100.
    property string paramId: ""
    /// The small word beside the setting: "balance".
    property string caption: ""
    /// What a screen reader calls the slider.
    property string sliderName: ""

    readonly property var params: SessionModel.revision >= 0 ? SessionModel.params(face.blockId) : ({})
    readonly property real position: face.params[face.paramId] ?? 0
    readonly property bool blockEnabled: SessionModel.revision >= 0 && (SessionModel.block(face.blockId).enabled ?? true)
    /// The category's colour, or grey for a block that is off.
    readonly property color tint: face.blockEnabled
        ? AppColors.categoryTint(BlockPresentation.category(face.typeId))
        : AppColors.muted

    /// The controls themselves, so that a test can drive what a person
    /// drives rather than the function behind it.
    readonly property alias slider: faceSlider
    readonly property alias positionLabel: positionText
    readonly property alias captionLabel: captionText

    implicitWidth: Kirigami.Units.gridUnit * 12
    implicitHeight: Kirigami.Units.gridUnit * 6

    StereoPositionWords {
        id: words
    }

    ColumnLayout {
        anchors {
            left: parent.left
            right: parent.right
            verticalCenter: parent.verticalCenter
            // The design's 12 pixels either side, less than the tile's own
            // edge takes.
            leftMargin: Kirigami.Units.smallSpacing * 3
            rightMargin: Kirigami.Units.smallSpacing * 3
        }
        spacing: Kirigami.Units.smallSpacing * 2

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            QQC2.Label {
                id: positionText

                // **The slider's own value, not the session's**, so the
                // setting follows a drag rather than jumping when it ends.
                text: words.shortText(faceSlider.value)
                font: AppFonts.readout
                lineHeightMode: AppMetrics.followsSystem ? Text.ProportionalHeight : Text.FixedHeight
                lineHeight: AppMetrics.followsSystem ? 1.0 : AppFonts.readoutPixels
                color: face.blockEnabled ? AppColors.text : AppColors.muted
                Layout.alignment: Qt.AlignBaseline
            }

            Item {
                Layout.fillWidth: true
            }

            QQC2.Label {
                id: captionText

                text: face.caption
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
            from: -100
            to: 100
            // Whole steps: a tenth of a per cent of one side is nothing
            // anybody hears, and the number on the tile is a whole one.
            stepSize: 1
            snapMode: QQC2.Slider.SnapAlways

            Accessible.name: face.sliderName

            // Live while it moves, coalesced to what the glide can follow,
            // and the last value always arrives when the button comes up.
            onMoved: SessionModel.setParam(face.blockId, face.paramId, value, true)
            onPressedChanged: if (!pressed) {
                SessionModel.commitParam(face.blockId, face.paramId);
            }

            SessionBound {
                control: faceSlider
                property: "value"
                value: face.position
                suspended: faceSlider.pressed
            }
        }
    }
}
