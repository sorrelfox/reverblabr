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

// What a Mono Enhancer shows on the canvas: where the width starts,
// and a slider to move it.
//
// **A value face, as the design draws this one**: the crossover in the
// fixed-width face with a small "crossover" beside it, and the slider under
// them on the effects colour's track, logarithmic as the panel's is, so the
// two are one control for one number.
Item {
    id: face

    /// The block whose face this is, set once the face exists.
    property string blockId: ""

    readonly property var params: SessionModel.revision >= 0 ? SessionModel.params(face.blockId) : ({})
    readonly property real crossover: face.params.crossover ?? face.defaultHz
    readonly property bool blockEnabled: SessionModel.revision >= 0 && (SessionModel.block(face.blockId).enabled ?? true)
    /// The effects colour, or grey for a block that is off.
    readonly property color tint: face.blockEnabled
        ? AppColors.categoryTint(BlockPresentation.category("effect.monoenhancer"))
        : AppColors.muted

    readonly property real lowestHz: 100
    readonly property real highestHz: 2000
    readonly property real defaultHz: 500

    /// Everything there is to say, for the tooltip.
    readonly property string detail: KI18n.i18nc("@info:tooltip a Mono Enhancer's setting, 'Width above 500 Hz'",
                                                 "Width above %1", words.frequencyText(face.crossover))

    readonly property alias headlineLabel: headlineText
    readonly property alias captionLabel: captionText
    readonly property alias slider: crossoverSlider

    implicitWidth: Kirigami.Units.gridUnit * 12
    implicitHeight: Kirigami.Units.gridUnit * 6

    FilterWords {
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

            QQC2.Label {
                id: headlineText

                // **The slider's own value, not the session's**: the number
                // follows the hand through a drag.
                text: words.frequencyText(words.frequencyAt(crossoverSlider.value, face.lowestHz, face.highestHz))
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

                text: KI18n.i18nc("@label what the number on a Mono Enhancer tile is", "crossover")
                font: AppFonts.tileCaption
                color: AppColors.muted
                Layout.alignment: Qt.AlignBaseline
                Accessible.ignored: true
            }
        }

        FaceSlider {
            id: crossoverSlider

            Layout.fillWidth: true
            Layout.topMargin: AppMetrics.followsSystem ? 0 : (AppMetrics.sliderGrooveHeight - implicitHeight) / 2
            Layout.bottomMargin: AppMetrics.followsSystem ? 0 : (AppMetrics.sliderGrooveHeight - implicitHeight) / 2
            tint: face.tint
            from: 0
            to: 1
            Accessible.name: KI18n.i18nc("@label:slider where a Mono Enhancer's width starts", "Crossover")

            // Whole hertz: nobody hears a fraction of one at a crossover.
            onMoved: SessionModel.setParam(face.blockId, "crossover",
                                           Math.round(words.frequencyAt(value, face.lowestHz, face.highestHz)), true)
            onPressedChanged: if (!pressed) {
                SessionModel.commitParam(face.blockId, "crossover");
            }

            SessionBound {
                control: crossoverSlider
                property: "value"
                value: words.positionOf(face.crossover, face.lowestHz, face.highestHz)
                suspended: crossoverSlider.pressed
            }
        }
    }
}
