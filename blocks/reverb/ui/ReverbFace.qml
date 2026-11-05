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

// What a Reverb shows on the canvas: how long its room rings and how
// loud it is against the original, and a slider for the ring.
//
// **A value face**, laid out like the other one-number tiles: the decay in
// the fixed-width face, the mix beside it where the others have their small
// word, and under them the decay's logarithmic slider on the effects
// colour's track — the same slider as the panel's. The pre-delay, the size,
// the damping and the width are in the panel and the tooltip.
Item {
    id: face

    /// The block whose face this is, set once the face exists.
    property string blockId: ""

    readonly property var params: SessionModel.paramRevision >= 0 ? SessionModel.params(face.blockId) : ({})
    readonly property real decay: face.params.decay ?? words.defaultDecay
    readonly property real mix: face.params.mix ?? words.defaultMix
    readonly property bool blockEnabled: SessionModel.revision >= 0 && (SessionModel.block(face.blockId).enabled ?? true)
    /// The effects colour, or grey for a block that is off.
    readonly property color tint: face.blockEnabled
        ? AppColors.categoryTint(BlockPresentation.category("effect.reverb"))
        : AppColors.muted

    /// Everything there is to say, for the tooltip.
    readonly property string detail: KI18n.i18nc("@info:tooltip a Reverb's settings, 'Decay 2.00 s, size 50 %, pre-delay 20 ms, damping 40 %, width 100 %, 25 % mix'",
                                                 "Decay %1, size %2, pre-delay %3, damping %4, width %5, %6 mix",
                                                 words.timeText(face.decay),
                                                 words.percentText(face.params.size ?? words.defaultSize),
                                                 words.timeText(face.params.preDelay ?? words.defaultPreDelay),
                                                 words.percentText(face.params.damping ?? words.defaultDamping),
                                                 words.percentText(face.params.width ?? words.defaultWidth),
                                                 words.percentText(face.mix))

    readonly property alias headlineLabel: headlineText
    readonly property alias captionLabel: captionText
    readonly property alias slider: decaySlider

    implicitWidth: Kirigami.Units.gridUnit * 12
    implicitHeight: Kirigami.Units.gridUnit * 6

    ReverbWords {
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

                // The slider's own value, so the number follows a drag.
                text: words.timeText(words.valueAt(decaySlider.value, words.shortestDecay, words.longestDecay))
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

                text: KI18n.i18nc("@label how loud a Reverb is against the original, on its tile: '25 % mix'",
                                  "%1 mix", words.percentText(face.mix))
                font: AppFonts.tileCaption
                color: AppColors.muted
                Layout.alignment: Qt.AlignBaseline
                Accessible.ignored: true
            }
        }

        FaceSlider {
            id: decaySlider

            Layout.fillWidth: true
            Layout.topMargin: AppMetrics.followsSystem ? 0 : (AppMetrics.sliderGrooveHeight - implicitHeight) / 2
            Layout.bottomMargin: AppMetrics.followsSystem ? 0 : (AppMetrics.sliderGrooveHeight - implicitHeight) / 2
            tint: face.tint
            from: 0
            to: 1
            Accessible.name: KI18n.i18nc("@label:slider how long a Reverb's room rings", "Decay")

            // Whole milliseconds: nobody hears a fraction of one in a decay.
            onMoved: SessionModel.setParam(face.blockId, "decay",
                                           Math.round(words.valueAt(value, words.shortestDecay, words.longestDecay)), true)
            onPressedChanged: if (!pressed) {
                SessionModel.commitParam(face.blockId, "decay");
            }

            SessionBound {
                control: decaySlider
                property: "value"
                value: words.positionOf(face.decay, words.shortestDecay, words.longestDecay)
                suspended: decaySlider.pressed
            }
        }
    }
}
