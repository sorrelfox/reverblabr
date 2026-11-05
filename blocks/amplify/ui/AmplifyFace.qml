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

// What an Amplify block shows on the canvas: the gain, and a slider to
// change it with, as a Volume tile draws them, up to 1000 %.
Item {
    id: face

    /// The block whose face this is, set once the face exists.
    property string blockId: ""

    readonly property var params: SessionModel.paramRevision >= 0 ? SessionModel.params(face.blockId) : ({})
    readonly property real gain: face.params.gain ?? 1.0
    readonly property real topPercent: 1000
    readonly property bool blockEnabled: SessionModel.revision >= 0 && (SessionModel.block(face.blockId).enabled ?? true)
    readonly property color tint: face.blockEnabled
        ? AppColors.categoryTint(BlockPresentation.category("effect.amplify"))
        : AppColors.muted

    readonly property alias slider: faceSlider
    readonly property alias percentLabel: percentText
    readonly property alias caption: captionText
    implicitWidth: Kirigami.Units.gridUnit * 12
    implicitHeight: Kirigami.Units.gridUnit * 6

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
                id: percentText

                // **The slider's own value, not the session's.** A dragged
                // value reaches the session once a tick, a little behind
                // the pointer, and the number being aimed with should be
                // exactly where the handle is.
                //
                // Whole per cent: a tenth of one is below what anybody can
                // hear and below what the slider can aim at.
                text: KI18n.i18nc("@info a gain as a percentage of the signal that arrived", "%1 %",
                                  Math.round(faceSlider.value))
                // The fixed-width face, so the number does not shift
                // sideways as it changes under a drag.
                font: AppFonts.readout
                // As tall as its size in the prototype (`line-height: 1`),
                // which is what centres the number and the slider on the
                // face together.
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

                text: KI18n.i18nc("@label what the number on an Amplify tile is", "gain")
                font: AppFonts.tileCaption
                color: AppColors.muted
                Layout.alignment: Qt.AlignBaseline
                Accessible.ignored: true
            }
        }

        FaceSlider {
            id: faceSlider

            Layout.fillWidth: true
            // **The groove is what is laid out**, as the prototype's
            // four-pixel track is, and the ring stands out of it above and
            // below; the slider still takes presses over the ring's height.
            Layout.topMargin: AppMetrics.followsSystem ? 0 : (AppMetrics.sliderGrooveHeight - implicitHeight) / 2
            Layout.bottomMargin: AppMetrics.followsSystem ? 0 : (AppMetrics.sliderGrooveHeight - implicitHeight) / 2
            tint: face.tint
            from: 0
            to: face.topPercent
            Accessible.name: KI18n.i18nc("@label:slider how much an Amplify block multiplies the sound by", "Gain")

            // While it moves the value is live: coalesced and sent sixty
            // times a second, which is what a smoothed gain can glide to.
            onMoved: SessionModel.setParam(face.blockId, "gain", VolumeScale.amplitudeOf(value), true)
            // And the last one always arrives, whatever the timer was doing
            // when the button came up.
            onPressedChanged: if (!pressed) {
                SessionModel.commitParam(face.blockId, "gain");
            }

            SessionBound {
                control: faceSlider
                property: "value"
                value: VolumeScale.percentOf(face.gain)
                suspended: faceSlider.pressed
            }
        }
    }
}
