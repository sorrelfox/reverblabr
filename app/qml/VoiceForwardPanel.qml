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

// Voice Forward's settings: Background, what it really takes out, what the mask
// found, Hold level, and Hold for before.
//
// **Taken out now is there because a dB on the slider does not buy a dB of
// separation.** The white line is Background, and the bar shows how much lower
// the sound is, often much less.
ColumnLayout {
    id: panel

    /// The block whose settings these are, set once the panel exists.
    property string blockId: ""

    readonly property VoiceForwardState reading: VoiceForwardState {
        blockId: panel.blockId
    }
    readonly property VoiceForwardWords words: panel.reading.words

    readonly property string takenOutWords: panel.reading.current
        ? panel.words.common.boostText(Math.min(0, panel.reading.takenOutDb))
        : KI18n.i18nc("@info nothing to measure, the session is not running", "Not live")
    readonly property string foundWords: panel.reading.current
        ? panel.words.foundName(panel.reading.found)
        : KI18n.i18nc("@info nothing to measure, the session is not running", "Not live")
    readonly property string holdWords: panel.words.holdLine(panel.reading.holdLevel, panel.reading.current,
                                                             panel.reading.liftDb, panel.reading.limitDb)
    readonly property string beforeWords: panel.words.beforeLine(panel.reading.holdLevel, panel.reading.current,
                                                                 panel.reading.takenOutDb)

    /// The controls, so that a test drives what a person drives.
    readonly property alias statusLabel: statusText
    readonly property alias statusDot: dot
    readonly property alias backgroundRow: backgroundControl
    readonly property alias takenOutBar: takenOutMeter
    readonly property alias takenOutLabel: takenOutText
    readonly property alias backgroundMark: mark
    readonly property alias foundLabel: foundText
    readonly property alias holdBox: holdControl
    readonly property alias holdLabel: holdText
    readonly property alias beforeButton: beforeControl
    readonly property alias beforeHeldLabel: beforeHeldText
    readonly property alias beforeLabel: beforeText
    readonly property alias betweenSection: betweenPart
    readonly property alias betweenRow: betweenControl
    readonly property alias explanationLine: explanation
    readonly property alias delayLabel: delayText

    spacing: AppMetrics.panelRowGap

    // B, wherever the focus is in the card (HoldForBefore).
    Keys.onPressed: event => {
        event.accepted = beforeControl.keyDown(event);
    }
    Keys.onReleased: event => {
        event.accepted = beforeControl.keyUp(event);
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
            color: dot.word === "off" ? AppColors.muted
                : dot.word === "ok" ? AppColors.okMark
                : dot.word === "warning" ? AppColors.warningMark : AppColors.liveMark
            Accessible.ignored: true
        }

        QQC2.Label {
            id: statusText

            Layout.fillWidth: true
            text: panel.words.statusOf(panel.reading.kind)
            color: AppColors.text
            wrapMode: Text.WordWrap
            Accessible.role: Accessible.StaticText
            Accessible.name: statusText.text
        }
    }

    TypedValueRow {
        id: backgroundControl

        blockId: panel.blockId
        paramId: "background"
        label: KI18n.i18nc("@label:slider how far Voice Forward turns the rest of the sound down", "Background")
        unit: KI18n.i18nc("@label the unit of Background", "dB")
        spoken: panel.words.common.boostText(panel.reading.background)
        value: panel.reading.background
        from: panel.words.lowestCut
        to: 0
        QQC2.ToolTip.visible: backgroundHover.hovered
        QQC2.ToolTip.text: KI18n.i18nc("@info:tooltip", "How far the rest of the sound goes down while there is sound in the centre")

        HoverHandler {
            id: backgroundHover
        }
    }

    // --- taken out now ---
    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            Layout.fillWidth: true
            text: KI18n.i18nc("@label how much lower Voice Forward has made the sound", "Taken out now")
            color: AppColors.text
            QQC2.ToolTip.visible: takenHover.hovered
            QQC2.ToolTip.text: KI18n.i18nc("@info:tooltip", "How much lower this sound is now, before any lift")

            HoverHandler {
                id: takenHover
            }
        }

        QQC2.Label {
            id: takenOutText

            text: panel.takenOutWords
            font: AppFonts.small
            color: AppColors.muted
            Accessible.role: Accessible.StaticText
            Accessible.name: takenOutText.text
        }
    }

    LevelBar {
        id: takenOutMeter

        Layout.fillWidth: true
        name: KI18n.i18nc("@label how much lower Voice Forward has made the sound", "Taken out now")
        from: panel.words.lowestCut
        db: Math.min(0, panel.reading.takenOutDb)
        running: panel.reading.current
        warnsAtTop: false
        fillsDown: true

        // Where Background stands.
        Rectangle {
            id: mark

            x: takenOutMeter.width * panel.reading.background / panel.words.lowestCut - width / 2
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 1
            color: AppColors.text
            visible: panel.reading.background < 0
            Accessible.ignored: true
        }
    }

    // --- found now ---
    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            Layout.fillWidth: true
            text: KI18n.i18nc("@label what Voice Forward's mask found", "Found now")
            color: AppColors.text
        }

        QQC2.Label {
            id: foundText

            text: panel.foundWords
            color: AppColors.text
            Accessible.role: Accessible.StaticText
            Accessible.name: foundText.text
        }
    }

    // --- hold level ---
    QQC2.CheckBox {
        id: holdControl

        text: KI18n.i18nc("@option:check Voice Forward", "Hold level")
        Accessible.name: text
        onToggled: SessionModel.setParam(panel.blockId, "holdLevel", checked)
        QQC2.ToolTip.visible: hovered
        QQC2.ToolTip.text: KI18n.i18nc("@info:tooltip", "Slowly raises the whole sound by up to 6 dB, after what was taken out")

        SessionBound {
            control: holdControl
            property: "checked"
            value: panel.reading.holdLevel
        }
    }

    QQC2.Label {
        id: holdText

        Layout.fillWidth: true
        visible: panel.holdWords !== ""
        text: panel.holdWords
        font: AppFonts.small
        color: AppColors.muted
        wrapMode: Text.WordWrap
    }

    // --- comparing ---
    HoldForBefore {
        id: beforeControl

        blockId: panel.blockId
    }

    QQC2.Label {
        id: beforeHeldText

        Layout.fillWidth: true
        visible: beforeControl.holding
        text: KI18n.i18nc("@info while Hold for before is on", "Hearing the sound before this block")
        font: AppFonts.small
        color: AppColors.muted
        wrapMode: Text.WordWrap
    }

    QQC2.Label {
        id: beforeText

        Layout.fillWidth: true
        visible: panel.beforeWords !== ""
        text: panel.beforeWords
        font: AppFonts.small
        color: AppColors.muted
        wrapMode: Text.WordWrap
    }

    CollapsibleSection {
        id: betweenPart

        title: KI18n.i18nc("@title:group Voice Forward", "Between voices")
        note: panel.words.common.boostText(panel.reading.between)

        TypedValueRow {
            id: betweenControl

            blockId: panel.blockId
            paramId: "betweenVoices"
            label: KI18n.i18nc("@label:slider how far Voice Forward turns the sound down when the centre is quiet", "Between voices")
            unit: KI18n.i18nc("@label the unit of Between voices", "dB")
            spoken: panel.words.common.boostText(panel.reading.between)
            value: panel.reading.between
            from: panel.words.lowestCut
            to: 0
            QQC2.ToolTip.visible: betweenHover.hovered
            QQC2.ToolTip.text: KI18n.i18nc("@info:tooltip", "How far the rest goes down when the centre is quiet")

            HoverHandler {
                id: betweenHover
            }
        }
    }

    InfoLine {
        id: explanation

        text: KI18n.i18nc("@info what Voice Forward can't do", "Finds what is alike on both sides. It can't tell a voice from centred music or a drum, and a voice off to one side goes down with the rest")
    }

    QQC2.Label {
        id: delayText

        Layout.fillWidth: true
        visible: panel.reading.latencyMs > 0
        text: panel.words.delayLine(panel.reading.latencyMs)
        font: AppFonts.small
        color: AppColors.muted
        wrapMode: Text.WordWrap
    }
}
