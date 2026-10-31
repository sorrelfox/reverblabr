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

// Auto Level's settings: what it is doing now, what its settings do to sound
// as a curve, two switches and a line saying what it does, and, folded away,
// its numbers and the curve as a table.
//
// **Two switches are the whole of it for most people.** The numbers behind
// them are under "Advanced". The curve is open once the block can bring sound
// down, and folded on a plain Auto Level, so that card stays short.
ColumnLayout {
    id: panel

    /// The block whose settings these are, set once the panel exists.
    property string blockId: ""

    readonly property real panelWidth: AppMetrics.followsSystem ? Kirigami.Units.gridUnit * 22 : 380

    readonly property AutoLevelState reading: AutoLevelState {
        blockId: panel.blockId
    }
    readonly property AutoLevelWords words: panel.reading.words
    readonly property var settings: panel.reading.settings
    readonly property bool boostOn: panel.settings.boostOn
    readonly property bool cutOn: panel.settings.cutOn
    readonly property bool anyOn: panel.reading.anyOn

    /// The time the range line is read at. **A property, moved by a timer**:
    /// a stopped session notes nothing, and a binding does not watch the clock.
    property real nowMs: Date.now()

    /// The range line's words, or nothing while the last minute was silent.
    readonly property string rangeWords: {
        const range = AutoLevelHistory.revision >= 0 ? AutoLevelHistory.rangeOf(panel.blockId, panel.nowMs) : null;
        return range === null ? "" : panel.words.rangeText(range.low, range.high);
    }

    /// What the table shows, a row at a time: `{ inText, outText, changeText,
    /// now }`, the last row being the sound now.
    readonly property var tableRows: {
        const set = panel.settings;
        const rows = panel.words.tableLevels(set.target).map(lufs => {
            const change = panel.words.gainFor(lufs, set);
            return { inText: panel.words.loudnessText(lufs), outText: panel.words.loudnessText(lufs + change),
                     changeText: panel.words.boostText(change), now: false };
        });
        if (panel.reading.live) {
            const gain = panel.reading.shownGainDb;
            rows.push({
                inText: KI18n.i18nc("@label the live row of Auto Level's table, 'Now -30 LUFS'", "Now %1",
                                    panel.words.readingText(panel.reading.readingLufs)),
                outText: panel.words.readingText(panel.reading.readingLufs + gain),
                changeText: panel.words.boostText(gain), now: true });
        }
        return rows;
    }

    /// The same a cell at a time, left to right, as the grid lays them.
    readonly property var tableCellTexts: {
        const texts = [];
        for (const row of panel.tableRows) {
            texts.push(row.inText, row.outText, row.changeText);
        }
        return texts;
    }

    readonly property string musicWords: KI18n.i18nc("@info Auto Level when the program in front of it declares music",
                                                     "This lane's program says it is playing music. This block takes the loud and soft out of it. Turn it off, or move the program to a lane of its own.")

    /// The controls, so that a test drives what a person drives.
    readonly property alias statusLabel: statusText
    readonly property alias statusDot: dot
    readonly property alias graphSection: curve
    readonly property alias graphView: graphItem
    readonly property alias liveLabel: liveText
    readonly property alias boostSwitch: boostControl
    readonly property alias cutSwitch: cutControl
    readonly property alias explanationLine: explanation
    readonly property alias musicLine: musicText
    readonly property alias rangeLabel: rangeText
    readonly property alias advancedSection: advanced
    readonly property alias targetRow: targetControl
    readonly property alias maximumRow: maximumControl
    readonly property alias maximumCutRow: maximumCutControl
    readonly property alias bandRow: bandControl
    readonly property alias tableSection: table
    readonly property alias tableCells: cells

    spacing: AppMetrics.panelRowGap

    // Open for what can come down, folded on a plain Auto Level. The person's
    // own folding stands from then on.
    onBlockIdChanged: curve.expanded = SessionModel.param(panel.blockId, "cut") === true

    Timer {
        interval: 1000
        repeat: true
        running: true
        onTriggered: panel.nowMs = Date.now()
    }

    Connections {
        target: panel.reading

        function onDeclaredMusicChanged() {
            if (panel.reading.declaredMusic) {
                Announcer.say(panel, "autoLevelMusic:" + panel.blockId, panel.musicWords);
            }
        }
    }

    // --- what it is doing now ---
    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        Rectangle {
            id: dot

            implicitWidth: Kirigami.Units.smallSpacing * 2
            implicitHeight: implicitWidth
            radius: width / 2
            color: panel.reading.state === "stopped" || panel.reading.state === "off" ? AppColors.muted
                : panel.reading.state === "left" || panel.reading.state === "quiet" || panel.reading.state === "bothOff"
                    ? AppColors.okMark : AppColors.liveMark
            Accessible.ignored: true
        }

        QQC2.Label {
            id: statusText

            Layout.fillWidth: true
            // Read on request and never announced: it changes all the time.
            text: panel.words.statusOf(panel.reading.state, panel.reading.shownGainDb, panel.reading.shownPullDb)
            color: AppColors.text
            wrapMode: Text.WordWrap
            Accessible.role: Accessible.StaticText
            Accessible.name: statusText.text
        }
    }

    CollapsibleSection {
        id: curve

        title: KI18n.i18nc("@title:group Auto Level's graph", "Level in and out")

        AutoLevelGraph {
            id: graphItem

            Layout.fillWidth: true
            settings: panel.settings
            words: panel.words
            live: panel.reading.live
            inLufs: panel.reading.readingLufs
            outLufs: panel.reading.readingLufs + panel.reading.gainDb
        }
    }

    QQC2.Label {
        id: liveText

        Layout.fillWidth: true
        visible: panel.reading.live
        text: panel.words.liveText(panel.reading.readingLufs, panel.reading.shownGainDb)
        color: AppColors.accentText
        font: AppFonts.mono
        wrapMode: Text.WordWrap
        Accessible.role: Accessible.StaticText
        Accessible.name: liveText.text
    }

    QQC2.Switch {
        id: boostControl

        Layout.fillWidth: true
        text: KI18n.i18nc("@option:check Auto Level's lift on or off", "Lift quiet passages")
        onToggled: SessionModel.setParam(panel.blockId, "boost", boostControl.checked)

        SessionBound {
            control: boostControl
            property: "checked"
            value: panel.boostOn
        }
    }

    QQC2.Switch {
        id: cutControl

        Layout.fillWidth: true
        text: KI18n.i18nc("@option:check Auto Level's cut on or off", "Bring loud passages down")
        onToggled: SessionModel.setParam(panel.blockId, "cut", cutControl.checked)

        SessionBound {
            control: cutControl
            property: "checked"
            value: panel.cutOn
        }
    }

    InfoLine {
        id: explanation

        text: KI18n.i18nc("@info what Auto Level does",
                          "Brings quiet passages up and loud ones down towards a steady loudness, and leaves what is near it alone. For voices on a call. On music or a film it takes the loud and soft out: turn it off, or give them a lane of their own.")
    }

    // What the program says of itself. Only while it says so, and only words.
    QQC2.Label {
        id: musicText

        Layout.fillWidth: true
        visible: panel.reading.declaredMusic
        text: panel.musicWords
        color: AppColors.warningText
        wrapMode: Text.WordWrap
    }

    QQC2.Label {
        id: rangeText

        Layout.fillWidth: true
        visible: panel.rangeWords !== ""
        text: panel.rangeWords
        color: AppColors.muted
        font: AppFonts.small
    }

    CollapsibleSection {
        id: advanced

        title: KI18n.i18nc("@title:group the less usual Auto Level settings", "Advanced")
        note: KI18n.i18nc("@info the four values on the closed Advanced row, '-23 LUFS · 18 · 18 · ±1 LU'",
                          "%1 · %2 · %3 · %4", panel.words.loudnessText(panel.settings.target),
                          Math.round(panel.settings.maximumBoost), Math.round(panel.settings.maximumCut),
                          panel.words.bandText(panel.settings.band))

        AutoLevelValueRow {
            id: targetControl

            blockId: panel.blockId
            paramId: "target"
            label: KI18n.i18nc("@label:slider", "Target loudness")
            unit: KI18n.i18nc("@label the unit of Target loudness", "LUFS")
            spoken: panel.words.loudnessText(panel.settings.target)
            value: panel.settings.target
            from: panel.words.lowestTarget
            to: panel.words.highestTarget
            enabled: panel.anyOn
        }

        AutoLevelValueRow {
            id: maximumControl

            Layout.topMargin: AppMetrics.panelRowGap
            blockId: panel.blockId
            paramId: "maximumBoost"
            label: KI18n.i18nc("@label:slider", "Maximum boost")
            unit: KI18n.i18nc("@label the unit of Maximum boost", "dB")
            spoken: panel.words.decibelText(panel.settings.maximumBoost)
            value: panel.settings.maximumBoost
            from: 0
            to: panel.words.highestMaximum
            enabled: panel.boostOn
        }

        AutoLevelValueRow {
            id: maximumCutControl

            Layout.topMargin: AppMetrics.panelRowGap
            blockId: panel.blockId
            paramId: "maximumCut"
            label: KI18n.i18nc("@label:slider", "Maximum cut")
            unit: KI18n.i18nc("@label the unit of Maximum cut", "dB")
            spoken: panel.words.decibelText(panel.settings.maximumCut)
            value: panel.settings.maximumCut
            from: 0
            to: panel.words.highestMaximumCut
            enabled: panel.cutOn
            atLowest: KI18n.i18nc("@info Maximum cut at 0", "At 0 dB nothing is turned down")
        }

        AutoLevelValueRow {
            id: bandControl

            Layout.topMargin: AppMetrics.panelRowGap
            blockId: panel.blockId
            paramId: "band"
            label: KI18n.i18nc("@label:slider", "Leave alone")
            unit: KI18n.i18nc("@label the unit of Leave alone", "± LU")
            spoken: panel.words.bandText(panel.settings.band)
            value: panel.settings.band
            from: 0
            to: panel.words.highestBand
            enabled: panel.anyOn
            atLowest: KI18n.i18nc("@info Leave alone at 0", "At 0 every level is moved to the target")
        }
    }

    CollapsibleSection {
        id: table

        title: KI18n.i18nc("@title:group Auto Level's graph as numbers", "Graph as a table")
        note: KI18n.i18nc("@info the columns of Auto Level's table", "In, Out, Change")

        GridLayout {
            id: cells

            Layout.fillWidth: true
            columns: 3
            columnSpacing: Kirigami.Units.largeSpacing

            QQC2.Label {
                text: KI18n.i18nc("@title:column of Auto Level's table", "In")
                font: AppFonts.strong
                color: AppColors.muted
            }

            QQC2.Label {
                text: KI18n.i18nc("@title:column of Auto Level's table", "Out")
                font: AppFonts.strong
                color: AppColors.muted
            }

            QQC2.Label {
                text: KI18n.i18nc("@title:column of Auto Level's table", "Change")
                font: AppFonts.strong
                color: AppColors.muted
            }

            Repeater {
                model: panel.tableCellTexts

                delegate: QQC2.Label {
                    required property string modelData
                    required property int index

                    text: modelData
                    color: AppColors.text
                    font: index >= panel.tableCellTexts.length - 3 && panel.reading.live ? AppFonts.strong : AppFonts.body
                    Accessible.role: Accessible.Cell
                    Accessible.name: modelData
                }
            }
        }
    }
}
