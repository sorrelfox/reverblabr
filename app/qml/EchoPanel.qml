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

// The Echo's settings: how far apart the repeats are, how many of them
// there are, how dull they get and how loud they are against the original.
//
// **Four rows as the design writes a slider**: the name and the value on one
// line, the value in the accent, the slider under them. The time and the
// tone are logarithmic, so a short echo or a dark one has room.
ColumnLayout {
    id: panel

    /// The block whose settings these are, set once the panel exists.
    property string blockId: ""

    readonly property var params: SessionModel.paramRevision >= 0 ? SessionModel.params(panel.blockId) : ({})

    /// The rows, so that a test drives what a person drives.
    readonly property alias timeRow: timeSetting
    readonly property alias feedbackRow: feedbackSetting
    readonly property alias toneRow: toneSetting
    readonly property alias mixRow: mixSetting
    readonly property alias words: words

    spacing: AppMetrics.panelRowGap

    EchoWords {
        id: words
    }

    FilterWords {
        id: filterWords
    }

    /// One setting: its name and value on a line, and its slider under them.
    component SettingRow: ColumnLayout {
        id: row

        required property string paramId
        required property string title
        required property real lowest
        required property real highest
        required property real value
        /// Spread by octaves rather than evenly.
        property bool logarithmic: false
        /// What the value is written as.
        property string kind: "percent"

        readonly property alias slider: rowSlider
        readonly property alias readout: readoutText
        /// What the slider stands at, as the setting's own number.
        readonly property real sliderValue: row.logarithmic
            ? words.valueAt(rowSlider.value, row.lowest, row.highest)
            : rowSlider.value

        Layout.fillWidth: true
        spacing: 0

        function textOf(number: real): string {
            switch (row.kind) {
            case "time":
                return words.timeText(number);
            case "frequency":
                return filterWords.frequencyText(number);
            }
            return words.percentText(number);
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            QQC2.Label {
                text: row.title
                color: AppColors.text
                Layout.fillWidth: true
            }

            QQC2.Label {
                id: readoutText

                text: row.textOf(row.sliderValue)
                color: AppColors.accentText
                font.family: AppFonts.monoFamily
            }
        }

        QQC2.Slider {
            id: rowSlider

            Layout.fillWidth: true
            from: row.logarithmic ? 0 : row.lowest
            to: row.logarithmic ? 1 : row.highest
            stepSize: row.logarithmic ? 0 : 1
            snapMode: row.logarithmic ? QQC2.Slider.NoSnap : QQC2.Slider.SnapAlways
            Accessible.name: row.title
            Accessible.description: readoutText.text

            // Whole numbers: a millisecond, a hertz and a per cent are finer
            // than anybody hears in an echo.
            onMoved: SessionModel.setParam(panel.blockId, row.paramId, Math.round(row.sliderValue), true)
            onPressedChanged: if (!pressed) {
                SessionModel.commitParam(panel.blockId, row.paramId);
            }

            SessionBound {
                control: rowSlider
                property: "value"
                value: row.logarithmic ? words.positionOf(row.value, row.lowest, row.highest) : row.value
                suspended: rowSlider.pressed
            }
        }
    }

    SettingRow {
        id: timeSetting

        paramId: "time"
        title: KI18n.i18nc("@label:slider the time between an Echo's repeats", "Time")
        lowest: words.shortestTime
        highest: words.longestTime
        value: panel.params.time ?? words.defaultTime
        logarithmic: true
        kind: "time"
    }

    SettingRow {
        id: feedbackSetting

        paramId: "feedback"
        title: KI18n.i18nc("@label:slider how much of each repeat an Echo feeds back in", "Feedback")
        lowest: 0
        highest: words.highestFeedback
        value: panel.params.feedback ?? words.defaultFeedback
    }

    SettingRow {
        id: toneSetting

        paramId: "tone"
        title: KI18n.i18nc("@label:slider how bright an Echo's repeats stay", "Tone")
        lowest: words.lowestTone
        highest: words.highestTone
        value: panel.params.tone ?? words.defaultTone
        logarithmic: true
        kind: "frequency"
    }

    SettingRow {
        id: mixSetting

        paramId: "mix"
        title: KI18n.i18nc("@label:slider how loud an Echo's repeats are against the original", "Mix")
        lowest: 0
        highest: 100
        value: panel.params.mix ?? words.defaultMix
    }

    InfoLine {
        text: KI18n.i18nc("@info what the Echo's settings do",
                          "Feedback sets how many repeats you hear, Tone how quickly they lose their top, and Mix how loud they are against the original.")
    }
}
