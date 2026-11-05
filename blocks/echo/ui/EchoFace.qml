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

// What an Echo shows on the canvas: how far apart its repeats are, and
// a slider to move them.
//
// **A value face**, laid out like the other one-number tiles: the time in
// the fixed-width face with a small "echo" beside it, and under them a
// logarithmic slider on the effects colour's track — the same slider as the
// panel's, so the two are one control for one number. The feedback, the
// tone and the mix are in the panel and the tooltip.
Item {
    id: face

    /// The block whose face this is, set once the face exists.
    property string blockId: ""

    readonly property var params: SessionModel.paramRevision >= 0 ? SessionModel.params(face.blockId) : ({})
    readonly property real time: face.params.time ?? words.defaultTime
    readonly property bool blockEnabled: SessionModel.revision >= 0 && (SessionModel.block(face.blockId).enabled ?? true)
    /// The effects colour, or grey for a block that is off.
    readonly property color tint: face.blockEnabled
        ? AppColors.categoryTint(BlockPresentation.category("effect.echo"))
        : AppColors.muted

    /// Everything there is to say, for the tooltip.
    readonly property string detail: KI18n.i18nc("@info:tooltip an Echo's settings, 'Every 350 ms, 40 % feedback, tone 5.00 kHz, 35 % mix'",
                                                 "Every %1, %2 feedback, tone %3, %4 mix", words.timeText(face.time),
                                                 words.percentText(face.params.feedback ?? words.defaultFeedback),
                                                 filterWords.frequencyText(face.params.tone ?? words.defaultTone),
                                                 words.percentText(face.params.mix ?? words.defaultMix))

    readonly property alias headlineLabel: headlineText
    readonly property alias captionLabel: captionText
    readonly property alias slider: timeSlider

    implicitWidth: Kirigami.Units.gridUnit * 12
    implicitHeight: Kirigami.Units.gridUnit * 6

    EchoWords {
        id: words
    }

    FilterWords {
        id: filterWords
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
                text: words.timeText(words.valueAt(timeSlider.value, words.shortestTime, words.longestTime))
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

                text: KI18n.i18nc("@label what the number on an Echo tile is", "echo")
                font: AppFonts.tileCaption
                color: AppColors.muted
                Layout.alignment: Qt.AlignBaseline
                Accessible.ignored: true
            }
        }

        FaceSlider {
            id: timeSlider

            Layout.fillWidth: true
            Layout.topMargin: AppMetrics.followsSystem ? 0 : (AppMetrics.sliderGrooveHeight - implicitHeight) / 2
            Layout.bottomMargin: AppMetrics.followsSystem ? 0 : (AppMetrics.sliderGrooveHeight - implicitHeight) / 2
            tint: face.tint
            from: 0
            to: 1
            Accessible.name: KI18n.i18nc("@label:slider the time between an Echo's repeats", "Time")

            // Whole milliseconds: nobody hears a fraction of one between
            // repeats.
            onMoved: SessionModel.setParam(face.blockId, "time",
                                           Math.round(words.valueAt(value, words.shortestTime, words.longestTime)), true)
            onPressedChanged: if (!pressed) {
                SessionModel.commitParam(face.blockId, "time");
            }

            SessionBound {
                control: timeSlider
                property: "value"
                value: words.positionOf(face.time, words.shortestTime, words.longestTime)
                suspended: timeSlider.pressed
            }
        }
    }
}
