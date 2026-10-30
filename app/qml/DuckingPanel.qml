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

// The Ducking block's settings: whether it is ducking now, the level
// the main input has to reach, how far down the background goes, and —
// folded away — how quickly.
//
// **Two sliders a person needs and three they might.** The threshold and
// the ducked volume are what somebody sets up a ducker with; the attack,
// hold and release have defaults that suit speech over music, and are there
// for the voice that talks fast or the music that should come back slowly.
ColumnLayout {
    id: panel

    /// The block whose settings these are, set once the panel exists.
    property string blockId: ""

    readonly property var params: SessionModel.paramRevision >= 0 ? SessionModel.params(panel.blockId) : ({})
    readonly property real thresholdDb: panel.params.threshold ?? -30
    readonly property real duckedVolume: panel.params.duckedVolume ?? 0.4
    readonly property real attackMs: panel.params.attack ?? 50
    readonly property real holdMs: panel.params.hold ?? 300
    readonly property real releaseMs: panel.params.release ?? 500

    /// What the block published last. **Settable**, so a test can say what
    /// a running block would have.
    property var published: Meters.frame >= 0 && Meters.live ? (Meters.levels(panel.blockId).values ?? []) : []
    readonly property bool ducking: (panel.published[words.activeValue] ?? 0) > 0.5

    /// The controls, so that a test drives what a person drives.
    readonly property alias statusLabel: statusText
    readonly property alias thresholdSlider: thresholdControl
    readonly property alias thresholdReadout: thresholdText
    readonly property alias volumeSlider: volumeControl
    readonly property alias volumeReadout: volumeText
    readonly property alias timingSection: timing
    readonly property alias attackSlider: attackRow.slider
    readonly property alias holdSlider: holdRow.slider
    readonly property alias releaseSlider: releaseRow.slider
    readonly property alias words: words

    spacing: AppMetrics.panelRowGap

    DuckingWords {
        id: words
    }

    // --- whether it is ducking now ---
    RowLayout {
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignHCenter
        spacing: Kirigami.Units.smallSpacing

        Rectangle {
            implicitWidth: AppMetrics.stateDotSize
            implicitHeight: implicitWidth
            radius: width / 2
            color: panel.ducking ? AppColors.liveMark : AppColors.muted
            Accessible.ignored: true
        }

        QQC2.Label {
            id: statusText

            text: panel.ducking
                ? KI18n.i18nc("@info the Ducking block is turning the background down now", "Ducking")
                : KI18n.i18nc("@info the Ducking block is leaving the background alone now", "Not ducking")
            color: panel.ducking ? AppColors.liveText : AppColors.muted
        }
    }

    // --- the threshold ---
    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            text: KI18n.i18nc("@label:slider the level the main input has to reach to duck", "Main threshold")
            color: AppColors.text
            Layout.fillWidth: true
        }

        QQC2.Label {
            id: thresholdText

            text: words.decibelText(thresholdControl.value)
            color: AppColors.accentText
            font.family: AppFonts.monoFamily
            horizontalAlignment: Text.AlignRight
        }
    }

    QQC2.Slider {
        id: thresholdControl

        Layout.fillWidth: true
        Layout.topMargin: AppMetrics.pullIntoRow
        from: -60
        to: 0
        Accessible.name: KI18n.i18nc("@label:slider", "Main threshold")

        onMoved: SessionModel.setParam(panel.blockId, "threshold", value, true)
        onPressedChanged: if (!pressed) {
            SessionModel.commitParam(panel.blockId, "threshold");
        }

        SessionBound {
            control: thresholdControl
            property: "value"
            value: panel.thresholdDb
            suspended: thresholdControl.pressed
        }
    }

    InfoLine {
        text: KI18n.i18nc("@info what the Ducking block's threshold does",
                          "The background is turned down while the main input is louder than this.")
    }

    // --- the ducked volume ---
    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            text: KI18n.i18nc("@label:slider how far the Ducking block turns the background down", "Ducked volume")
            color: AppColors.text
            // A translation goes onto a second line rather than under what is
            // beside it.
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
            Layout.fillWidth: true
        }

        QQC2.Label {
            id: volumeText

            text: KI18n.i18nc("@info a volume and the same in decibels, '40 % · -8.0 dB'", "%1 · %2",
                              words.percentText(volumeControl.value / 100),
                              words.amplitudeDbText(volumeControl.value / 100))
            color: AppColors.accentText
            font.family: AppFonts.monoFamily
            horizontalAlignment: Text.AlignRight
        }
    }

    QQC2.Slider {
        id: volumeControl

        Layout.fillWidth: true
        Layout.topMargin: AppMetrics.pullIntoRow
        from: 0
        to: 100
        Accessible.name: KI18n.i18nc("@label:slider", "Ducked volume")

        onMoved: SessionModel.setParam(panel.blockId, "duckedVolume", value / 100, true)
        onPressedChanged: if (!pressed) {
            SessionModel.commitParam(panel.blockId, "duckedVolume");
        }

        SessionBound {
            control: volumeControl
            property: "value"
            value: panel.duckedVolume * 100
            suspended: volumeControl.pressed
        }
    }

    InfoLine {
        text: KI18n.i18nc("@info what the Ducking block's ducked volume does",
                          "While the main input is heard, the background plays at this volume.")
    }

    // --- how quickly ---
    CollapsibleSection {
        id: timing

        title: KI18n.i18nc("@title:group how quickly the Ducking block works", "Timing")

        TimeRow {
            id: attackRow

            paramId: "attack"
            label: KI18n.i18nc("@label:slider how long the background takes to go down", "Attack")
            lowest: 5
            highest: 1000
            logarithmic: true
            current: panel.attackMs
        }

        TimeRow {
            id: holdRow

            paramId: "hold"
            label: KI18n.i18nc("@label:slider how long the background stays down after the main input stops",
                               "Hold")
            lowest: 0
            highest: 2000
            current: panel.holdMs
        }

        TimeRow {
            id: releaseRow

            paramId: "release"
            label: KI18n.i18nc("@label:slider how long the background takes to come back up", "Release")
            lowest: 10
            highest: 5000
            logarithmic: true
            current: panel.releaseMs
        }

        InfoLine {
            text: KI18n.i18nc("@info what the Ducking block's timing does",
                              "The attack is how long the background takes to go down, the hold how long it stays down after the main input stops, and the release how long it takes to come back.")
        }
    }

    /// One time, on a slider that is logarithmic where the range is wide,
    /// with the number beside it.
    component TimeRow: ColumnLayout {
        id: row

        required property string paramId
        required property string label
        required property real lowest
        required property real highest
        required property real current
        property bool logarithmic: false

        readonly property alias slider: timeSlider

        function positionOf(ms: real): real {
            return row.logarithmic ? Math.log(ms / row.lowest) / Math.log(row.highest / row.lowest)
                                   : (ms - row.lowest) / (row.highest - row.lowest);
        }

        function msAt(position: real): real {
            return row.logarithmic ? row.lowest * Math.pow(row.highest / row.lowest, position)
                                   : row.lowest + position * (row.highest - row.lowest);
        }

        Layout.fillWidth: true
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            QQC2.Label {
                text: row.label
                color: AppColors.text
                Layout.fillWidth: true
            }

            QQC2.Label {
                text: words.millisecondsText(row.msAt(timeSlider.value))
                color: AppColors.accentText
                font.family: AppFonts.monoFamily
                horizontalAlignment: Text.AlignRight
            }
        }

        QQC2.Slider {
            id: timeSlider

            Layout.fillWidth: true
            from: 0
            to: 1
            Accessible.name: row.label

            onMoved: SessionModel.setParam(panel.blockId, row.paramId, row.msAt(value), true)
            onPressedChanged: if (!pressed) {
                SessionModel.commitParam(panel.blockId, row.paramId);
            }

            SessionBound {
                control: timeSlider
                property: "value"
                value: row.positionOf(row.current)
                suspended: timeSlider.pressed
            }
        }
    }
}
