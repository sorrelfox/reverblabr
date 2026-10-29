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

// What a Volume block shows on the canvas: how loud it is, and a slider to
// change it with.
//
// **As the design draws it**: the percentage in the fixed-width face
// with a small "gain" beside it, and under them a thin track filled in the
// effects colour up to a ring. Nothing else: whether sound is going through
// is the header's state dot and the wires, and the block's limiter keeps it
// from clipping, so a meter here would say what the tile already says.
//
// **The number is the point of the face.** A tile is glanced at, not read,
// so the percentage is the first thing on it.
//
// **The slider is the same slider as the panel's.** It writes an amplitude
// and shows a percentage, and it stops where the overdrive setting stops —
// a face that offered a different range from the panel would be two
// controls for one number.
Item {
    id: face

    /// The block whose face this is. **Not a required property**: a face is
    /// made from a Component in the register, which cannot see the context
    /// it is loaded into, so it is set once the face exists.
    property string blockId: ""

    /// Re-read when the session changes, so a value set anywhere — the
    /// panel, undo, a second window — is what the face shows.
    readonly property var params: SessionModel.revision >= 0 ? SessionModel.params(face.blockId) : ({})
    readonly property real gain: face.params.gain ?? VolumeScale.defaultGain
    readonly property string overdrive: face.params.overdrive ?? VolumeScale.defaultOverdrive
    readonly property bool muted: face.params.muted === true
    readonly property real topPercent: VolumeScale.percentOf(VolumeScale.topAmplitude(face.overdrive))
    readonly property bool blockEnabled: SessionModel.revision >= 0 && (SessionModel.block(face.blockId).enabled ?? true)
    /// The effects colour, or grey for a block that is off.
    readonly property color tint: face.blockEnabled
        ? AppColors.categoryTint(BlockPresentation.category("effect.volume"))
        : AppColors.muted

    /// The controls themselves, so that a test can drive what a person
    /// drives rather than the function behind it.
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

                // **The slider's own value, not the session's.** A knob
                // being turned is not news the rest of the window hears
                // about until the gesture ends, and a number that stood
                // still through a drag and then jumped would be a number
                // nobody could aim with.
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
                color: face.muted || !face.blockEnabled ? AppColors.muted : AppColors.text
                Layout.alignment: Qt.AlignBaseline
            }

            Item {
                Layout.fillWidth: true
            }

            QQC2.Label {
                id: captionText

                text: KI18n.i18nc("@label what the number on a Volume tile is", "gain")
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
            // **A muted fader still moves.** Setting the level to come back
            // to while nothing is going out is half of what a mute is for —
            // and the face has no mute on it, so a slider that went dead
            // here could not be brought back here either.

            Accessible.name: KI18n.i18nc("@label:slider", "Volume")

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
