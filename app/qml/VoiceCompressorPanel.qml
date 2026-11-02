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

// The Voice Compressor's settings: what it is doing now, a voice type to start
// from, the curve, and the three controls, with what it takes off under them.
//
// **Link** keeps the gap between Compression and Ceiling: lowering
// Compression brings the sound up, lowering the Ceiling takes it down, so
// together a person hears what the compressor does and not a change of level.
// It belongs to the card and is not saved: it is a way of listening and no
// setting of the sound.
ColumnLayout {
    id: panel

    /// The block whose settings these are, set once the panel exists.
    property string blockId: ""

    readonly property real panelWidth: AppMetrics.followsSystem ? Kirigami.Units.gridUnit * 22 : 380

    readonly property VoiceCompressorState reading: VoiceCompressorState {
        blockId: panel.blockId
    }
    readonly property VoiceCompressorWords words: panel.reading.words

    /// Which of the voice types the three controls are, or nothing once one moved.
    readonly property string chosenPreset: {
        const found = panel.words.presets.find(preset => preset.compression === panel.reading.compression
                                                         && preset.gate === panel.reading.gate
                                                         && preset.ceiling === panel.reading.ceiling);
        return found === undefined ? "" : found.id;
    }

    /// What comes out now, by what the block says it took off.
    readonly property real outNowDb: panel.reading.current
        ? panel.reading.inputDb - panel.reading.totalDb - panel.reading.limitedDb
          + panel.words.makeupDb(panel.reading.compression, panel.reading.ceiling) + panel.reading.ceiling
        : -120

    readonly property string mostWords: KI18n.i18nc("@info the most Voice Compressor took off, 'Most taken off 7.2 dB'",
                                                    "Most taken off %1 dB", panel.words.decibels(panel.reading.mostDb))

    function choosePreset(id: string) {
        const preset = panel.words.presets.find(each => each.id === id);
        SessionModel.setParams(panel.blockId, [["compression", preset.compression], ["gate", preset.gate],
                                               ["ceiling", preset.ceiling]]);
    }

    /// Writes both controls: one edit, and one undo, unless a slider is held.
    function writeBoth(compression: real, ceiling: real, moving: bool) {
        if (moving) {
            SessionModel.setParam(panel.blockId, "compression", compression, true);
            SessionModel.setParam(panel.blockId, "ceiling", ceiling, true);
        } else {
            SessionModel.setParams(panel.blockId, [["compression", compression], ["ceiling", ceiling]]);
        }
    }

    /// Compression moved, and with it the Ceiling if they move together.
    function moveCompression(number: real, moving: bool) {
        const shifted = panel.reading.ceiling + number - panel.reading.compression;
        panel.writeBoth(number, linkControl.checked ? Math.max(panel.words.lowestCeiling, Math.min(0, shifted))
                                                    : panel.reading.ceiling, moving);
    }

    /// The Ceiling moved, and with it Compression if they move together.
    function moveCeiling(number: real, moving: bool) {
        const shifted = panel.reading.compression + number - panel.reading.ceiling;
        panel.writeBoth(linkControl.checked ? Math.max(panel.words.lowestCompression, Math.min(0, shifted))
                                            : panel.reading.compression, number, moving);
    }

    /// The parts, so that a test drives what a person drives.
    readonly property alias statusLabel: statusText
    readonly property alias presetChoices: presetRepeater
    readonly property alias graphItem: graphControl
    readonly property alias compressionRow: compressionControl
    readonly property alias gateRow: gateControl
    readonly property alias ceilingRow: ceilingControl
    readonly property alias linkBox: linkControl
    readonly property alias totalBar: totalMeter
    readonly property alias inputBar: inputMeter
    readonly property alias gateMarkItem: gateMark
    readonly property alias limiterBar: limiterMeter
    readonly property alias mostLabel: mostText
    readonly property alias resetButton: resetControl

    spacing: AppMetrics.panelRowGap

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
            text: panel.words.statusOf(panel.reading.kind, panel.reading.shownTotalDb)
            color: AppColors.text
            wrapMode: Text.WordWrap
            Accessible.role: Accessible.StaticText
            Accessible.name: statusText.text
        }
    }

    QQC2.Label {
        text: KI18n.i18nc("@label the Voice Compressor's choice of voice types", "Voice")
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

    CollapsibleSection {
        title: KI18n.i18nc("@title:group the Voice Compressor's graph", "Level in and out")
        expanded: true

        VoiceCompressorGraph {
            id: graphControl

            Layout.fillWidth: true
            words: panel.words
            compression: panel.reading.compression
            ceiling: panel.reading.ceiling
            gate: panel.reading.gate
            live: panel.reading.current
            inDb: panel.reading.inputDb
            outDb: panel.outNowDb
        }
    }

    TypedValueRow {
        id: compressionControl

        blockId: panel.blockId
        paramId: "compression"
        label: KI18n.i18nc("@label:slider the Voice Compressor's one compression control", "Compression")
        unit: KI18n.i18nc("@label the unit of Compression", "dB")
        spoken: KI18n.i18nc("@info a level, '-12 dB'", "%1 dB", panel.words.common.plainNumber(panel.reading.compression))
        value: panel.reading.compression
        from: panel.words.lowestCompression
        to: 0
        writes: false
        onEdited: (number, moving) => panel.moveCompression(number, moving)
    }

    QQC2.Label {
        id: compressionNote

        Layout.fillWidth: true
        text: KI18n.i18nc("@info under Compression, 'Ratio 3.3:1, level made up 9.4 dB'",
                          "Lower is more. Ratio %1:1, level made up %2 dB",
                          Number(panel.words.ratioAt(panel.reading.compression)).toLocaleString(Qt.locale(), 'f', 1),
                          panel.words.decibels(panel.words.makeupDb(panel.reading.compression, panel.reading.ceiling)))
        font: AppFonts.small
        color: AppColors.muted
        wrapMode: Text.WordWrap
    }

    TypedValueRow {
        id: gateControl

        blockId: panel.blockId
        paramId: "gate"
        label: KI18n.i18nc("@label:slider the level under which the Voice Compressor lowers the sound", "Gate")
        unit: KI18n.i18nc("@label the unit of Gate", "dB")
        spoken: panel.words.gateLine(panel.reading.gate)
        value: panel.reading.gate
        from: panel.words.gateOff
        to: 0
        lowestInField: KI18n.i18nc("@info the Voice Compressor's gate at its lowest", "Off")
        atLowest: KI18n.i18nc("@info under Gate at its lowest", "Nothing is lowered")
    }

    QQC2.Label {
        id: gateNote

        Layout.fillWidth: true
        text: KI18n.i18nc("@info under Gate, where the level in stands against it", "Set it just above the room between words, with the level coming in as a guide")
        font: AppFonts.small
        color: AppColors.muted
        wrapMode: Text.WordWrap
    }

    QQC2.Label {
        text: KI18n.i18nc("@label the Voice Compressor's input meter", "Level coming in")
        font: AppFonts.small
        color: AppColors.muted
    }

    LevelBar {
        id: inputMeter

        Layout.fillWidth: true
        name: KI18n.i18nc("@label the Voice Compressor's input meter", "Level coming in")
        db: panel.reading.inputDb
        from: panel.words.gateOff
        running: panel.reading.current

        // Where the Gate stands against the level, as both have one scale.
        Rectangle {
            id: gateMark

            visible: panel.reading.gate > panel.words.gateOff
            x: Math.round(parent.width * (panel.reading.gate - panel.words.gateOff) / -panel.words.gateOff)
            y: -Kirigami.Units.smallSpacing / 2
            width: 2
            height: parent.height + Kirigami.Units.smallSpacing
            color: AppColors.text
            Accessible.ignored: true
        }
    }

    TypedValueRow {
        id: ceilingControl

        blockId: panel.blockId
        paramId: "ceiling"
        label: KI18n.i18nc("@label:slider the highest level the Voice Compressor lets out", "Ceiling")
        unit: KI18n.i18nc("@label the unit of Ceiling", "dBFS")
        spoken: KI18n.i18nc("@info a level, '-6 dBFS'", "%1 dBFS", panel.words.common.plainNumber(panel.reading.ceiling))
        value: panel.reading.ceiling
        from: panel.words.lowestCeiling
        to: 0
        writes: false
        onEdited: (number, moving) => panel.moveCeiling(number, moving)
    }

    QQC2.CheckBox {
        id: linkControl

        text: KI18n.i18nc("@option:check keeps the gap between two of the Voice Compressor's controls", "Link Compression and Ceiling")
        Accessible.name: text
    }

    QQC2.Label {
        text: KI18n.i18nc("@label the Voice Compressor's total meter", "Taken off by compression and gate")
        font: AppFonts.small
        color: AppColors.muted
    }

    LevelBar {
        id: totalMeter

        Layout.fillWidth: true
        name: KI18n.i18nc("@label the Voice Compressor's total meter", "Taken off by compression and gate")
        db: -panel.reading.totalDb
        from: -40
        fillsDown: true
        warnsAtTop: false
        running: panel.reading.current
    }

    QQC2.Label {
        text: KI18n.i18nc("@label the Voice Compressor's limiter meter", "Taken off by the limiter")
        font: AppFonts.small
        color: AppColors.muted
    }

    LevelBar {
        id: limiterMeter

        Layout.fillWidth: true
        name: KI18n.i18nc("@label the Voice Compressor's limiter meter", "Taken off by the limiter")
        db: -panel.reading.limitedDb
        from: -20
        fillsDown: true
        warnsAtTop: false
        running: panel.reading.current
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            id: mostText

            Layout.fillWidth: true
            text: panel.mostWords
            color: AppColors.text
            font: AppFonts.mono
            Accessible.role: Accessible.StaticText
            Accessible.name: mostText.text
        }

        QQC2.Button {
            id: resetControl

            text: KI18n.i18nc("@action:button forgets the most Voice Compressor took off", "Reset")
            Accessible.name: KI18n.i18nc("@action:button", "Reset the most taken off")
            onClicked: panel.reading.resetMost()
        }
    }

    InfoLine {
        id: explanation

        text: KI18n.i18nc("@info what the Voice Compressor does",
                          "Holds a voice steady. The attack and release are set for you: a syllable lets go quickly, a long loud line lets go slowly. Too much at a high Ceiling can sound rough, so lower the Ceiling or the sound coming in")
    }
}
