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

// Night Level's settings. A block that starts neutral needs its first press
// here, so How much comes before everything but what it is doing now.
ColumnLayout {
    id: panel

    /// The block whose settings these are, set once the panel exists.
    property string blockId: ""

    readonly property real panelWidth: AppMetrics.followsSystem ? Kirigami.Units.gridUnit * 22 : 380

    readonly property NightLevelState reading: NightLevelState {
        blockId: panel.blockId
    }
    readonly property NightLevelWords words: panel.reading.words
    readonly property SpeechNowWords detectorWords: SpeechNowWords {}

    /// The time the range line is read at, moved by a timer: a stopped session
    /// notes nothing, and a binding does not watch the clock.
    property real nowMs: Date.now()

    readonly property string rangeWords: {
        const range = AutoLevelHistory.revision >= 0 ? AutoLevelHistory.rangeOf(panel.blockId, panel.nowMs) : null;
        return range === null ? "" : panel.words.common.rangeText(range.low, range.high);
    }

    /// Which of the presets the amounts are, or nothing once a slider moved.
    readonly property string chosenPreset: {
        const found = panel.words.presets.find(preset => preset.cut === panel.reading.cut
                                                         && preset.boost === panel.reading.boost);
        return found === undefined ? "" : found.id;
    }

    /// The level in a large line, and the small line under it.
    readonly property string levelHeadline: panel.reading.pivotFrom === "typed"
        ? panel.words.common.loudnessText(panel.reading.typedPivot)
        : (panel.reading.foundLufs > panel.words.noLevel ? panel.words.common.loudnessText(panel.reading.foundLufs)
                                                         : KI18n.i18nc("@info:status Night Level", "No speech yet"))
    readonly property string levelNote: {
        if (panel.reading.pivotFrom === "typed") {
            return KI18n.i18nc("@info under Night Level's typed level", "Typed. The block goes by this and never lets it go");
        }
        if (panel.reading.foundLufs <= panel.words.noLevel) {
            return KI18n.i18nc("@info under Night Level's level", "Nothing changes until it hears speech");
        }
        return panel.reading.held ? panel.words.heldLine(panel.reading.seconds)
                                  : KI18n.i18nc("@info under Night Level's found level", "Found on this lane. From a voice detector, it can be wrong.");
    }
    /// Why the voice model does not listen, or nothing.
    readonly property string detectorNote: !panel.reading.current || panel.reading.detector === 0 ? ""
        : KI18n.i18nc("@info which detector Night Level uses and why, 'Level and rhythm is listening. This build has no voice model'",
                      "%1 is listening. %2", panel.detectorWords.levelName,
                      panel.detectorWords.modelRefused(panel.reading.detector))

    readonly property bool peakLineShown: panel.reading.anyAmount && panel.reading.ceilingDb <= -1
    /// What the table shows, a row at a time, the last row being the sound now.
    readonly property var tableRows: {
        const rows = panel.words.tableLevels.map(lu => {
            const change = panel.words.changeFor(lu, panel.reading.cut, panel.reading.boost);
            return { inText: panel.words.common.plainNumber(lu), outText: panel.outNumber(lu + change),
                     changeText: panel.outNumber(change), now: false };
        });
        if (panel.reading.current && panel.reading.hasPivot) {
            const lu = panel.reading.readingLufs - panel.reading.pivotLufs;
            rows.push({
                inText: KI18n.i18nc("@label the live row of Night Level's table, 'Now -10'", "Now %1", panel.outNumber(lu)),
                outText: panel.outNumber(lu + panel.reading.shownGainDb),
                changeText: panel.outNumber(panel.reading.shownGainDb), now: true });
        }
        return rows;
    }
    readonly property var tableCellTexts: {
        const texts = [];
        for (const row of panel.tableRows) {
            texts.push(row.inText, row.outText, row.changeText);
        }
        return texts;
    }
    readonly property string liveWords: panel.reading.current && panel.reading.hasPivot
        ? KI18n.i18nc("@info the sound coming in and going out of Night Level, 'In -30 LUFS · Out -27 LUFS · Change +3 dB'",
                      "In %1 · Out %2 · Change %3", panel.words.common.readingText(panel.reading.readingLufs),
                      panel.words.common.readingText(panel.reading.readingLufs + panel.reading.shownGainDb),
                      panel.words.common.boostText(panel.reading.shownGainDb))
        : ""

    /// A number to one place, signed, for the table.
    function outNumber(value: real): string {
        const rounded = Number(value.toFixed(1));
        const number = Number(Math.abs(rounded)).toLocaleString(Qt.locale(), 'f', 1);
        return rounded > 0 ? "+" + number : rounded < 0 ? "-" + number : number;
    }

    function choosePreset(id: string) {
        const preset = panel.words.presets.find(each => each.id === id);
        SessionModel.setParams(panel.blockId, [["cut", preset.cut], ["boost", preset.boost]]);
    }

    /// **Typed starts from the level found**, or from the usual speech level
    /// with none, so that the press itself moves nothing it need not.
    function choosePivotFrom(id: string) {
        if (id === panel.reading.pivotFrom) {
            return;
        }
        if (id === "typed") {
            const found = panel.reading.foundLufs;
            const start = found > panel.words.noLevel ? Math.round(found) : panel.words.defaultTyped;
            SessionModel.setParams(panel.blockId, [["pivotFrom", id], ["typedPivot", start]]);
        } else {
            SessionModel.setParam(panel.blockId, "pivotFrom", id);
        }
    }

    /// The controls, so that a test drives what a person drives.
    readonly property alias statusLabel: statusText
    readonly property alias statusDot: dot
    readonly property alias presetChoices: presetRepeater
    readonly property alias levelLabel: levelText
    readonly property alias levelNoteLabel: levelNoteText
    readonly property alias detectorLabel: detectorText
    readonly property alias fromChoices: fromRepeater
    readonly property alias typedRow: typedControl
    readonly property alias graphSection: curveSection
    readonly property alias graphView: graphItem
    readonly property alias liveLabel: liveText
    readonly property alias loudRow: loudControl
    readonly property alias quietRow: quietControl
    readonly property alias explanationLine: explanation
    readonly property alias peakLabel: peakText
    readonly property alias musicLine: musicText
    readonly property alias rangeLabel: rangeText
    readonly property alias tableSection: table

    spacing: AppMetrics.panelRowGap

    Timer {
        interval: 1000
        repeat: true
        running: true
        onTriggered: panel.nowMs = Date.now()
    }

    // --- what it is doing now ---
    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        Rectangle {
            id: dot

            readonly property string word: panel.words.dotOf(panel.reading.kind)

            implicitWidth: Kirigami.Units.smallSpacing * 2
            implicitHeight: implicitWidth
            radius: width / 2
            color: dot.word === "off" ? AppColors.muted : dot.word === "ok" ? AppColors.okMark : AppColors.liveMark
            Accessible.ignored: true
        }

        QQC2.Label {
            id: statusText

            Layout.fillWidth: true
            // Read on request and never announced: it changes all the time.
            text: panel.words.statusOf(panel.reading.kind, panel.reading.shownGainDb)
            color: AppColors.text
            wrapMode: Text.WordWrap
            Accessible.role: Accessible.StaticText
            Accessible.name: statusText.text
        }
    }

    // --- how much ---
    QQC2.Label {
        text: KI18n.i18nc("@label Night Level's choice of amounts", "How much")
        font: AppFonts.small
        color: AppColors.muted
    }

    SegmentedFrame {
        shrinkable: true
        Layout.fillWidth: true

        Repeater {
            id: presetRepeater

            model: panel.words.presets

            delegate: SegmentedChoice {
                id: presetButton

                required property var modelData

                text: panel.words.presetName(presetButton.modelData.id)
                Accessible.name: text
                onClicked: panel.choosePreset(presetButton.modelData.id)

                SessionBound {
                    control: presetButton
                    property: "checked"
                    value: panel.chosenPreset === presetButton.modelData.id
                }
            }
        }
    }

    // --- the level ---
    QQC2.Label {
        id: levelText

        Layout.fillWidth: true
        text: panel.levelHeadline
        font: AppFonts.readout
        color: AppColors.accentText
        Accessible.role: Accessible.StaticText
        Accessible.name: levelText.text
    }

    QQC2.Label {
        id: levelNoteText

        Layout.fillWidth: true
        text: panel.levelNote
        font: AppFonts.small
        color: AppColors.muted
        wrapMode: Text.WordWrap
    }

    QQC2.Label {
        id: detectorText

        Layout.fillWidth: true
        visible: panel.detectorNote !== ""
        text: panel.detectorNote
        font: AppFonts.small
        color: AppColors.muted
        wrapMode: Text.WordWrap
    }

    QQC2.Label {
        text: KI18n.i18nc("@label where Night Level's speech level comes from", "Speech level")
        font: AppFonts.small
        color: AppColors.muted
    }

    SegmentedFrame {
        shrinkable: true
        Layout.fillWidth: true

        QQC2.ButtonGroup {
            id: fromGroup
        }

        Repeater {
            id: fromRepeater

            model: ["found", "typed"]

            delegate: SegmentedChoice {
                id: fromButton

                required property string modelData

                text: panel.words.pivotFromName(fromButton.modelData)
                Accessible.name: text
                QQC2.ButtonGroup.group: fromGroup
                onClicked: panel.choosePivotFrom(fromButton.modelData)

                SessionBound {
                    control: fromButton
                    property: "checked"
                    value: panel.reading.pivotFrom === fromButton.modelData
                }
            }
        }
    }

    TypedValueRow {
        id: typedControl

        visible: panel.reading.pivotFrom === "typed"
        blockId: panel.blockId
        paramId: "typedPivot"
        label: KI18n.i18nc("@label:slider Night Level's speech level, typed", "Typed level")
        unit: KI18n.i18nc("@label the unit of Typed level", "LUFS")
        spoken: panel.words.common.loudnessText(panel.reading.typedPivot)
        value: panel.reading.typedPivot
        from: panel.words.lowestPivot
        to: panel.words.highestPivot
    }

    CollapsibleSection {
        id: curveSection

        title: KI18n.i18nc("@title:group Night Level's graph", "Level in and out")
        expanded: true

        NightLevelGraph {
            id: graphItem

            Layout.fillWidth: true
            words: panel.words
            cut: panel.reading.cut
            boost: panel.reading.boost
            live: panel.reading.current && panel.reading.hasPivot
            inLu: panel.reading.readingLufs - panel.reading.pivotLufs
            outLu: panel.reading.readingLufs + panel.reading.gainDb - panel.reading.pivotLufs
            showsPeakLine: panel.peakLineShown && panel.reading.hasPivot
            peakLu: panel.reading.ceilingDb - panel.reading.pivotLufs
        }
    }

    QQC2.Label {
        id: liveText

        Layout.fillWidth: true
        visible: panel.liveWords !== ""
        text: panel.liveWords
        color: AppColors.accentText
        font: AppFonts.mono
        wrapMode: Text.WordWrap
        Accessible.role: Accessible.StaticText
        Accessible.name: liveText.text
    }

    TypedValueRow {
        id: loudControl

        blockId: panel.blockId
        paramId: "cut"
        label: KI18n.i18nc("@label:slider how far Night Level turns loud sounds down", "Loud sounds")
        unit: KI18n.i18nc("@label the unit of Loud sounds", "%")
        spoken: KI18n.i18nc("@info an amount, '100 %'", "%1 %", Math.round(panel.reading.cut))
        value: panel.reading.cut
        from: 0
        to: 100
        stepSize: 5
        pageStepSize: 25
    }

    TypedValueRow {
        id: quietControl

        blockId: panel.blockId
        paramId: "boost"
        label: KI18n.i18nc("@label:slider how far Night Level lifts quiet sounds", "Quiet sounds")
        unit: KI18n.i18nc("@label the unit of Quiet sounds", "%")
        spoken: KI18n.i18nc("@info an amount, '100 %'", "%1 %", Math.round(panel.reading.boost))
        value: panel.reading.boost
        from: 0
        to: 100
        stepSize: 5
        pageStepSize: 25
    }

    InfoLine {
        id: explanation

        text: KI18n.i18nc("@info what Night Level does",
                          "Keeps speech where it is and brings louder and quieter sounds towards it. For films and shows. It presses music too, so turn it off for music. The speech level comes from a voice detector, and it can be wrong")
    }

    QQC2.Label {
        id: peakText

        Layout.fillWidth: true
        visible: panel.peakLineShown
        text: panel.words.peakLine(panel.reading.ceilingDb)
        color: AppColors.muted
        font: AppFonts.small
        wrapMode: Text.WordWrap
    }

    // What the program says of itself. Only while it says so, and only words.
    QQC2.Label {
        id: musicText

        Layout.fillWidth: true
        visible: panel.reading.declaredMusic
        text: KI18n.i18nc("@info Night Level when the program in front of it declares music",
                          "This lane's program says music. This block presses the loud and soft of it. Turn it off, or move the program to a lane of its own.")
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
        id: table

        title: KI18n.i18nc("@title:group Night Level's graph as numbers", "Graph as a table")
        note: KI18n.i18nc("@info the columns of Night Level's table", "In, Out, Change")

        GridLayout {
            Layout.fillWidth: true
            columns: 3
            columnSpacing: Kirigami.Units.largeSpacing

            QQC2.Label {
                text: KI18n.i18nc("@title:column of Night Level's table", "In")
                font: AppFonts.strong
                color: AppColors.muted
            }

            QQC2.Label {
                text: KI18n.i18nc("@title:column of Night Level's table", "Out")
                font: AppFonts.strong
                color: AppColors.muted
            }

            QQC2.Label {
                text: KI18n.i18nc("@title:column of Night Level's table", "Change")
                font: AppFonts.strong
                color: AppColors.muted
            }

            Repeater {
                model: panel.tableCellTexts

                delegate: QQC2.Label {
                    required property string modelData

                    text: modelData
                    color: AppColors.text
                    font: AppFonts.body
                    Accessible.role: Accessible.Cell
                    Accessible.name: modelData
                }
            }
        }
    }
}
