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

// The Voice Gauge block's settings: where the reading stands, the two marks,
// how it listens, where it shows and how it reads.
//
// **Both marks are written together.** Moving one past the other pushes it
// along, and the block works that out for itself, but a block can't write to
// the session. So the card writes both, one step of undo, and the session
// holds what the block uses.
//
// **Nothing here suggests a level.** The marks start Not set, and only a press
// of Use This Level or a typed value sets one.
//
// **Nothing is announced unasked.** A sentence at each crossing would talk
// over the person and would act as a warning, so the words are read when
// somebody lands on them, and a global shortcut asks for them from anywhere.
ColumnLayout {
    id: panel

    /// The block whose settings these are, set once the panel exists.
    property string blockId: ""

    /// The block's ranges; tst_voicegauge.qml holds them against the catalogue.
    readonly property real longestReading: 10
    readonly property real longestForget: 600
    readonly property real mostRoom: 9
    /// How long a name may be.
    readonly property int longestName: 24

    readonly property var words: reading.words
    readonly property string modelRefused: detectorWords.modelRefused(reading.detector)
    readonly property bool floating: reading.params.showFloating === true
    readonly property bool inTray: reading.params.showInTray === true
    /// Rows the window had no room for: said, not hidden.
    readonly property bool windowFull: panel.floating && FloatingSurface.leftOut > 0
    readonly property string placedOnName: reading.params.marksPlacedOn ?? ""

    /// The controls, so that a test drives what a person drives.
    readonly property alias statusLabel: statusText
    readonly property alias statusRow: statusLine
    readonly property alias nameField: nameControl
    readonly property alias meterLabel: meterText
    readonly property alias meter: meterBar
    readonly property alias positionLabel: positionText
    readonly property alias upperRow: upperControl
    readonly property alias lowerRow: lowerControl
    readonly property alias firstUseLine: firstUse
    readonly property alias useButton: useControl
    readonly property alias clearButton: clearControl
    readonly property alias placedOnLine: placedOnText
    readonly property alias detectorChoices: detectorRepeater
    readonly property alias detectorReason: refusedText
    readonly property alias floatingSwitch: floatingControl
    readonly property alias traySwitch: trayControl
    readonly property alias onTopLine: onTopText
    readonly property alias fullLine: fullText
    readonly property alias placeSection: place
    readonly property alias readingSection: readingPart
    readonly property alias readingRow: overControl
    readonly property alias forgetRow: forgetControl
    readonly property alias roomRow: roomControl
    readonly property alias infoLine: info
    readonly property alias reader: reading

    function chooseDetector(id: string) {
        if (id !== reading.chosenDetector) {
            SessionModel.setParam(panel.blockId, "detector", id);
        }
    }

    /// One mark moved to \a number: both are written. While a slider moves
    /// nothing is an edit yet, and the last value is one step of undo.
    function moveMark(lowerMoved: bool, number: real, moving: bool) {
        const marks = panel.words.moved(lowerMoved ? reading.marks.upper : number,
                                        lowerMoved ? number : reading.marks.lower, lowerMoved);
        if (moving) {
            SessionModel.setParam(panel.blockId, "upperMark", marks.upper, true);
            SessionModel.setParam(panel.blockId, "lowerMark", marks.lower, true);
        } else {
            SessionModel.setParams(panel.blockId, [["upperMark", marks.upper], ["lowerMark", marks.lower]]);
        }
    }

    /// The microphone this lane starts from, by the name it has now, or
    /// nothing when the lane does not start from one.
    function deviceName(): string {
        const wires = SessionModel.wires();
        const seen = [panel.blockId];
        for (let next = 0; next < seen.length; ++next) {
            if (SessionModel.blockType(seen[next]) === "source.device") {
                const device = SessionModel.param(seen[next], "device") ?? Inventory.defaultDevice;
                const node = device === Inventory.defaultDevice ? Inventory.defaultInput : device;
                return node === "" ? "" : Inventory.inputName(node);
            }
            for (const wire of wires) {
                if (wire.toBlock === seen[next] && seen.indexOf(wire.fromBlock) < 0) {
                    seen.push(wire.fromBlock);
                }
            }
        }
        return "";
    }

    /// Places both marks around the last 20 s of voice.
    function useThisLevel() {
        const marks = panel.words.around(reading.wholeLevel, reading.params.roomEachSide ?? 3);
        SessionModel.setParams(panel.blockId, [["upperMark", marks.upper], ["lowerMark", marks.lower],
                                               ["marksPlacedOn", panel.deviceName()]]);
    }

    function clearMarks() {
        SessionModel.setParams(panel.blockId, [["upperMark", panel.words.notSet], ["lowerMark", panel.words.notSet],
                                               ["marksPlacedOn", ""]]);
    }

    spacing: AppMetrics.panelRowGap

    VoiceGaugeState {
        id: reading

        blockId: panel.blockId
    }

    readonly property SpeechNowWords detectorWords: SpeechNowWords {}

    RowLayout {
        id: statusLine

        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing * 2
        activeFocusOnTab: true
        Accessible.role: Accessible.StaticText
        Accessible.name: KI18n.i18nc("@label what a Voice Gauge says now", "Now")
        Accessible.description: statusText.text

        VoiceGaugeDot {
            dot: panel.words.dot(reading.kind)
            Layout.alignment: Qt.AlignVCenter
        }

        QQC2.Label {
            id: statusText

            text: panel.words.status(reading.kind, reading.state)
            color: reading.kind === "noSound" ? AppColors.warningText : AppColors.text
            font: AppFonts.strong
            Layout.fillWidth: true
            Accessible.ignored: true
        }
    }

    QQC2.Label {
        text: KI18n.i18nc("@label what a Voice Gauge is called", "Name")
        color: AppColors.muted
    }

    QQC2.TextField {
        id: nameControl

        Layout.fillWidth: true
        maximumLength: panel.longestName
        placeholderText: KI18n.i18nc("@info:placeholder", "For example You")
        Accessible.name: KI18n.i18nc("@label what a Voice Gauge is called", "Name")
        onEditingFinished: {
            if (text !== (SessionModel.block(panel.blockId).nickname ?? "")) {
                SessionModel.renameBlock(panel.blockId, text);
            }
        }

        SessionBound {
            control: nameControl
            property: "text"
            value: SessionModel.paramRevision >= 0 ? (SessionModel.block(panel.blockId).nickname ?? "") : ""
            suspended: nameControl.activeFocus
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            text: KI18n.i18nc("@label the level of a voice over a few seconds", "Voice level")
            color: AppColors.muted
            Layout.fillWidth: true
        }

        QQC2.Label {
            id: meterText

            text: reading.headline
            color: AppColors.accentText
            font.family: AppFonts.monoFamily
            Accessible.ignored: true
        }
    }

    VoiceGaugeBar {
        id: meterBar

        Layout.fillWidth: true
        Layout.topMargin: AppMetrics.pullIntoRow
        implicitHeight: Kirigami.Units.smallSpacing * 2
        wholeScale: true
        level: reading.level
        shown: panel.words.showsNumber(reading.kind)
        held: reading.kind === "held"
        upper: reading.marks.upper
        lower: reading.marks.lower
        name: KI18n.i18nc("@label the level of a voice over a few seconds", "Voice level")
        spoken: meterText.text
    }

    QQC2.Label {
        id: positionText

        visible: panel.words.showsNumber(reading.kind)
        text: panel.words.position(panel.words.positionOf(reading.state))
        color: AppColors.text
        Layout.fillWidth: true
    }

    QQC2.Label {
        id: firstUse

        visible: !reading.anyMark
        text: KI18n.i18nc("@info first use of a Voice Gauge", "Talk as you do on a call. After 20 s of voice, Use This Level places two marks around that level")
        color: AppColors.muted
        font: AppFonts.small
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }

    TypedValueRow {
        id: upperControl

        blockId: panel.blockId
        paramId: "upperMark"
        label: KI18n.i18nc("@label a mark placed on a voice level", "Upper mark")
        unit: KI18n.i18nc("@label loudness units", "LUFS")
        spoken: panel.markWords(upperControl.value)
        value: reading.marks.upper
        from: panel.words.notSet
        to: panel.words.highestMark
        lowestInField: KI18n.i18nc("@info a mark with no value", "Not set")
        writes: false
        onEdited: (number, moving) => panel.moveMark(false, number, moving)
    }

    TypedValueRow {
        id: lowerControl

        blockId: panel.blockId
        paramId: "lowerMark"
        label: KI18n.i18nc("@label a mark placed on a voice level", "Lower mark")
        unit: KI18n.i18nc("@label loudness units", "LUFS")
        spoken: panel.markWords(lowerControl.value)
        value: reading.marks.lower
        from: panel.words.notSet
        to: panel.words.highestMark
        lowestInField: KI18n.i18nc("@info a mark with no value", "Not set")
        writes: false
        onEdited: (number, moving) => panel.moveMark(true, number, moving)
    }

    function markWords(mark: real): string {
        return panel.words.isSet(mark) ? panel.words.loudness(mark)
                                       : KI18n.i18nc("@info a mark with no value", "Not set");
    }

    // One above the other: the count takes the width of a line.
    ColumnLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        QQC2.Button {
            id: useControl

            text: panel.words.useThisLevel(reading.secondsCounted, reading.wholeLevelReady)
            enabled: reading.wholeLevelReady
            Layout.fillWidth: true
            // The same name while it counts, with the count as its value.
            Accessible.name: panel.words.useThisLevel(0, true)
            Accessible.description: text
            onClicked: panel.useThisLevel()
        }

        QQC2.Button {
            id: clearControl

            text: KI18n.i18nc("@action:button", "Clear Marks")
            enabled: reading.anyMark
            Layout.fillWidth: true
            Accessible.name: text
            onClicked: panel.clearMarks()
        }
    }

    QQC2.Label {
        id: placedOnText

        visible: panel.placedOnName !== ""
        text: KI18n.i18nc("@info the microphone the marks were placed on, %1 its name", "Marks placed on: %1", panel.placedOnName)
        color: AppColors.muted
        font: AppFonts.small
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }

    QQC2.Label {
        text: KI18n.i18nc("@label how Voice Gauge listens", "Detector")
        color: AppColors.muted
    }

    SegmentedFrame {
        shrinkable: true
        Layout.fillWidth: true

        QQC2.ButtonGroup {
            id: detectorGroup
        }

        Repeater {
            id: detectorRepeater

            model: ["model", "levelAndRhythm"]

            delegate: SegmentedChoice {
                id: detectorButton

                required property string modelData
                readonly property bool isModel: detectorButton.modelData === "model"

                text: panel.detectorWords.detectorChoice(detectorButton.modelData)
                enabled: !detectorButton.isModel || panel.modelRefused === ""
                Accessible.name: text
                QQC2.ToolTip.text: detectorButton.isModel && panel.modelRefused !== "" ? panel.modelRefused
                    : detectorButton.isModel
                    ? KI18n.i18nc("@info:tooltip", "Voice model is a small model on this computer")
                    : KI18n.i18nc("@info:tooltip", "Level and rhythm uses less of the processor, and can be wrong more often")
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                QQC2.ButtonGroup.group: detectorGroup
                onClicked: panel.chooseDetector(detectorButton.modelData)

                SessionBound {
                    control: detectorButton
                    property: "checked"
                    value: panel.modelRefused !== "" ? !detectorButton.isModel
                                                     : reading.chosenDetector === detectorButton.modelData
                }
            }
        }
    }

    QQC2.Label {
        id: refusedText

        visible: panel.modelRefused !== ""
        text: panel.modelRefused
        color: AppColors.muted
        font: AppFonts.small
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }

    QQC2.Label {
        text: KI18n.i18nc("@label where a Voice Gauge shows", "Show it on")
        color: AppColors.muted
    }

    QQC2.CheckBox {
        id: floatingControl

        text: KI18n.i18nc("@option:check", "Floating bar")
        // An edit and not a bare value: only an edit tells the surface.
        onToggled: SessionModel.setParams(panel.blockId, [["showFloating", floatingControl.checked]])

        SessionBound {
            control: floatingControl
            property: "checked"
            value: panel.floating
        }
    }

    QQC2.Label {
        id: onTopText

        visible: !FloatingWindows.keepsOnTop
        text: KI18n.i18nc("@info under the Floating bar check box", "This desktop may not keep it on top")
        color: AppColors.muted
        font: AppFonts.small
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }

    QQC2.Label {
        id: fullText

        visible: panel.windowFull
        text: KI18n.i18ncp("@info under the Floating bar check box",
                           "The window has room for four. %1 more is left out.",
                           "The window has room for four. %1 more are left out.",
                           FloatingSurface.leftOut)
        color: AppColors.muted
        font: AppFonts.small
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }

    QQC2.CheckBox {
        id: trayControl

        text: KI18n.i18nc("@option:check", "Show in system tray")
        QQC2.ToolTip.text: KI18n.i18nc("@info:tooltip",
                                       "Shows this meter when you click Reverblabr's icon in the system tray")
        QQC2.ToolTip.visible: hovered
        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
        // An edit and not a bare value: only an edit tells the tray.
        onToggled: SessionModel.setParams(panel.blockId, [["showInTray", trayControl.checked]])

        SessionBound {
            control: trayControl
            property: "checked"
            value: panel.inTray
        }
    }

    FloatingPlaceSection {
        id: place

        title: KI18n.i18nc("@title:group a part of the Voice Gauge card", "Place the bar")
    }

    CollapsibleSection {
        id: readingPart

        title: KI18n.i18nc("@title:group a part of the Voice Gauge card", "Reading")

        TypedValueRow {
            id: overControl

            blockId: panel.blockId
            paramId: "readingSeconds"
            label: KI18n.i18nc("@label how many seconds of voice a reading is the mean of", "Reading over")
            unit: KI18n.i18nc("@label seconds", "s")
            spoken: panel.words.secondsOfVoice(overControl.value)
            value: reading.params.readingSeconds ?? 3
            from: 1
            to: panel.longestReading
        }

        TypedValueRow {
            id: forgetControl

            blockId: panel.blockId
            paramId: "forgetSeconds"
            label: KI18n.i18nc("@label how long without a voice before a Voice Gauge starts again", "Forget after")
            unit: KI18n.i18nc("@label seconds", "s")
            spoken: panel.words.seconds(forgetControl.value)
            value: reading.params.forgetSeconds ?? 60
            from: 10
            to: panel.longestForget
            stepSize: 10
            pageStepSize: 60
            logarithmic: true
        }

        TypedValueRow {
            id: roomControl

            blockId: panel.blockId
            paramId: "roomEachSide"
            label: KI18n.i18nc("@label how far either side of a level Use This Level puts the marks", "Room each side")
            unit: KI18n.i18nc("@label loudness units", "LU")
            spoken: panel.words.roomEachSide(roomControl.value)
            value: reading.params.roomEachSide ?? 3
            from: 1
            to: panel.mostRoom
        }
    }

    InfoLine {
        id: info

        text: KI18n.i18nc("@info what Voice Gauge can't know",
                          "A level at this point, before the call program. It can't see a mute inside that program, or what the other side hears. A voice from a loudspeaker counts too")
    }
}
