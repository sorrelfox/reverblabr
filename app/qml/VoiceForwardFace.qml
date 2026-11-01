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

// What a Voice Forward shows on the canvas: Background as a number, a word
// under it, and the slider that sets it. The number and its word stack, since
// "-12 dB" and "background" do not fit side by side.
//
// **The slider is on the tile because the setting changes by programme**, and
// the card should not have to be open. It takes the same keys as the card's
// field, and it is the same number.
Item {
    id: face

    /// The block whose face this is, set once the face exists.
    property string blockId: ""

    readonly property VoiceForwardState reading: VoiceForwardState {
        blockId: face.blockId
    }
    readonly property VoiceForwardWords words: face.reading.words

    readonly property string kind: face.reading.kind
    readonly property string dot: face.words.dotOf(face.kind)
    readonly property color tint: face.reading.blockEnabled
        ? AppColors.categoryTint(BlockPresentation.category("effect.voiceforward"))
        : AppColors.muted

    readonly property string headline: face.reading.blockEnabled
        ? face.words.common.boostText(faceSlider.value)
        : KI18n.i18nc("@info Voice Forward is switched off", "Off")
    readonly property string caption: face.words.captionOf(face.kind, face.reading.betweenActing)
    /// Everything there is to say, for the tooltip.
    readonly property string detail: KI18n.i18nc("@info:tooltip", "Turns down what differs between left and right. A sound that is alike on both sides stays where it is")

    readonly property alias headlineLabel: headlineText
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
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            id: headlineText

            Layout.fillWidth: true
            text: face.headline
            font: AppFonts.readout
            lineHeightMode: AppMetrics.followsSystem ? Text.ProportionalHeight : Text.FixedHeight
            lineHeight: AppMetrics.followsSystem ? 1.0 : AppFonts.readoutPixels
            color: face.reading.blockEnabled ? AppColors.text : AppColors.muted
            Accessible.ignored: true
        }

        QQC2.Label {
            id: captionText

            Layout.fillWidth: true
            visible: face.caption !== ""
            text: face.caption
            font: AppFonts.tileCaption
            color: face.kind === "alike" ? AppColors.warningText : AppColors.muted
            elide: Text.ElideRight
            Accessible.ignored: true
        }

        FaceSlider {
            id: faceSlider

            Layout.fillWidth: true
            Layout.topMargin: AppMetrics.followsSystem ? 0 : (AppMetrics.sliderGrooveHeight - implicitHeight) / 2
            Layout.bottomMargin: AppMetrics.followsSystem ? 0 : (AppMetrics.sliderGrooveHeight - implicitHeight) / 2
            tint: face.tint
            from: face.words.lowestCut
            to: 0
            stepSize: 1
            snapMode: QQC2.Slider.SnapAlways

            Accessible.name: KI18n.i18nc("@label:slider how far Voice Forward turns the rest of the sound down", "Background")

            onMoved: SessionModel.setParam(face.blockId, "background", value, true)
            onPressedChanged: if (!pressed) {
                SessionModel.commitParam(face.blockId, "background");
            }
            Keys.onPressed: event => {
                if (event.key === Qt.Key_PageUp || event.key === Qt.Key_PageDown) {
                    const moved = faceSlider.value + (event.key === Qt.Key_PageUp ? 3 : -3);
                    SessionModel.setParam(face.blockId, "background", Math.max(faceSlider.from, Math.min(faceSlider.to, moved)));
                    event.accepted = true;
                }
            }

            SessionBound {
                control: faceSlider
                property: "value"
                value: face.reading.background
                suspended: faceSlider.pressed
            }
        }
    }
}
