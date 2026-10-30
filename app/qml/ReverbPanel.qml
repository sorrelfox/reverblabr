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

// The Reverb's settings: when the room starts, how big it is, how
// long it rings, how quickly its top fades, how wide it is and how loud
// against the original.
//
// **Six rows as the design writes a slider**, as the Echo's: the name and
// the value on one line, the value in the accent, the slider under them.
// The decay is logarithmic, so a short room has as much room as a hall.
ColumnLayout {
    id: panel

    /// The block whose settings these are, set once the panel exists.
    property string blockId: ""

    readonly property var params: SessionModel.paramRevision >= 0 ? SessionModel.params(panel.blockId) : ({})

    /// The rows, so that a test drives what a person drives.
    readonly property alias preDelayRow: preDelaySetting
    readonly property alias sizeRow: sizeSetting
    readonly property alias decayRow: decaySetting
    readonly property alias dampingRow: dampingSetting
    readonly property alias widthRow: widthSetting
    readonly property alias mixRow: mixSetting
    readonly property alias words: words

    spacing: AppMetrics.panelRowGap

    ReverbWords {
        id: words
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
        /// What the value is written as: "percent" or "time".
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
            return row.kind === "time" ? words.timeText(number) : words.percentText(number);
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

            // Whole numbers: a millisecond and a per cent are finer than
            // anybody hears in a room.
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
        id: preDelaySetting

        paramId: "preDelay"
        title: KI18n.i18nc("@label:slider how long a Reverb waits before its room is heard", "Pre-delay")
        lowest: 0
        highest: words.longestPreDelay
        value: panel.params.preDelay ?? words.defaultPreDelay
        kind: "time"
    }

    SettingRow {
        id: sizeSetting

        paramId: "size"
        title: KI18n.i18nc("@label:slider how big a Reverb's room is, from small to a hall", "Size")
        lowest: 0
        highest: 100
        value: panel.params.size ?? words.defaultSize
    }

    SettingRow {
        id: decaySetting

        paramId: "decay"
        title: KI18n.i18nc("@label:slider how long a Reverb's room rings", "Decay")
        lowest: words.shortestDecay
        highest: words.longestDecay
        value: panel.params.decay ?? words.defaultDecay
        logarithmic: true
        kind: "time"
    }

    SettingRow {
        id: dampingSetting

        paramId: "damping"
        title: KI18n.i18nc("@label:slider how much sooner the top of a Reverb's room fades", "Damping")
        lowest: 0
        highest: 100
        value: panel.params.damping ?? words.defaultDamping
    }

    SettingRow {
        id: widthSetting

        paramId: "width"
        title: KI18n.i18nc("@label:slider how wide a Reverb's room sounds, from the middle to both sides", "Width")
        lowest: 0
        highest: 100
        value: panel.params.width ?? words.defaultWidth
    }

    SettingRow {
        id: mixSetting

        paramId: "mix"
        title: KI18n.i18nc("@label:slider how loud a Reverb is against the original", "Mix")
        lowest: 0
        highest: 100
        value: panel.params.mix ?? words.defaultMix
    }

    InfoLine {
        text: KI18n.i18nc("@info what the Reverb's settings do",
                          "Decay sets how long the room rings, Size how big it is, Damping how much sooner its top fades, and Mix how loud it is against the original.")
    }
}
