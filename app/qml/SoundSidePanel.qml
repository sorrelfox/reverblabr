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

// The Sound Side block's settings: what it reads now, the gate, the band, the
// strip on screen and where it sits.
//
// **Direction is not announced.** A stream of "left, right, left" would bury
// everything else a screen reader says, so a sentence comes once, for a change
// of state, and Side now is there for somebody who lands on it.
//
// **From and To are written together.** To stays an octave above From, and the
// block works that out for itself, but a block can't write to the session. So
// the card writes both, one step of undo, and the session holds what is used.
//
// **Size, Show and the place are this desktop's**, like the label's: there is
// one strip whichever block it shows, so they go to FloatingSurface and are
// no edit.
ColumnLayout {
    id: panel

    /// The block whose settings these are, set once the panel exists.
    property string blockId: ""

    /// The block's ranges; tst_soundside.qml holds them against the catalogue.
    readonly property real lowestGate: -70
    readonly property real highestGate: -20
    readonly property real lowestFrom: 100
    readonly property real highestFrom: 8000
    readonly property real lowestTo: 500
    readonly property real highestTo: 16000

    readonly property var words: reading.words
    readonly property string kind: reading.kind
    readonly property real gate: reading.gate
    readonly property string band: reading.params.band ?? "all"
    readonly property real from: reading.params.fromHz ?? 1000
    readonly property real to: reading.params.toHz ?? 6000
    readonly property bool stripOn: reading.params.showStrip === true
    /// Another Sound Side has the strip, and there is one.
    readonly property bool stripTaken: panel.stripOn
        && ((FloatingSurface.strip.sessionId ?? "") !== SessionModel.sessionId
            || (FloatingSurface.strip.blockId ?? "") !== panel.blockId)
    readonly property var placement: FloatingSurface.placementRevision >= 0 ? FloatingSurface.placement("strip") : ({})
    /// A One Ear Mix this block comes after, or nothing.
    readonly property string foldBefore: SessionModel.revision >= 0 ? panel.oneEarMixBefore() : ""

    /// The controls, so that a test drives what a person drives.
    readonly property alias statusLabel: statusText
    readonly property alias statusRow: statusLine
    readonly property alias sideStrip: sideNow
    readonly property alias sideLabel: sideValue
    readonly property alias alikeLine: alikeText
    readonly property alias oneSideLine: oneSideText
    readonly property alias levelMeter: levelBar
    readonly property alias levelLabel: levelValue
    readonly property alias gateLine: gateMark
    readonly property alias gateRow: gateControl
    readonly property alias bandChoices: bandRepeater
    readonly property alias fromRow: fromControl
    readonly property alias toRow: toControl
    readonly property alias stripSwitch: stripControl
    readonly property alias onTopLine: onTopText
    readonly property alias takenLine: takenText
    readonly property alias sizeChoices: sizeRepeater
    readonly property alias showChoices: showRepeater
    readonly property alias placeButton: placeControl
    readonly property alias testButton: testControl
    readonly property alias placeSection: place
    readonly property alias moveLine: moveText
    readonly property alias moveButton: moveControl
    readonly property alias infoLine: info
    readonly property alias reader: reading

    /// The nearest One Ear Mix along the wires back from this block.
    function oneEarMixBefore(): string {
        const wires = SessionModel.wires();
        const seen = [panel.blockId];
        for (let next = 0; next < seen.length; ++next) {
            if (next > 0 && SessionModel.blockType(seen[next]) === "hearing.oneearmix") {
                return seen[next];
            }
            for (const wire of wires) {
                if (wire.toBlock === seen[next] && seen.indexOf(wire.fromBlock) < 0) {
                    seen.push(wire.fromBlock);
                }
            }
        }
        return "";
    }

    function chooseBand(id: string) {
        if (id !== panel.band) {
            SessionModel.setParam(panel.blockId, "band", id);
        }
    }

    /// One edge moved to \a number: both are written, as one step of undo.
    function moveEdge(toMoved: bool, number: real, moving: bool) {
        const edges = panel.words.octaveApart(toMoved ? panel.from : number, toMoved ? number : panel.to, toMoved);
        if (moving) {
            SessionModel.setParam(panel.blockId, "fromHz", edges.from, true);
            SessionModel.setParam(panel.blockId, "toHz", edges.to, true);
        } else {
            SessionModel.setParams(panel.blockId, [["fromHz", edges.from], ["toHz", edges.to]]);
        }
    }

    function showStrip(on: bool) {
        // An edit and not a bare value: only an edit tells the surface.
        SessionModel.setParams(panel.blockId, [["showStrip", on]]);
        if (on) {
            Announcer.say(panel, "soundSideStrip", FloatingWindows.keepsOnTop
                ? KI18n.i18nc("@info:status spoken, of a Sound Side", "Strip on")
                : KI18n.i18nc("@info:status spoken, of a Sound Side", "Strip may not stay on top"));
        }
    }

    onKindChanged: {
        if (panel.kind === "same") {
            Announcer.say(panel, "soundSideAlike", KI18n.i18nc("@info:status spoken, of a Sound Side", "Sides identical"));
        }
    }

    spacing: AppMetrics.panelRowGap

    SoundSideState {
        id: reading

        blockId: panel.blockId
    }

    RowLayout {
        id: statusLine

        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing * 2
        activeFocusOnTab: true
        Accessible.role: Accessible.StaticText
        Accessible.name: KI18n.i18nc("@label what a Sound Side says now", "Now")
        Accessible.description: statusText.text

        QQC2.Label {
            id: statusText

            text: panel.words.status(panel.kind)
            color: panel.kind === "same" ? AppColors.warningText : AppColors.text
            font: AppFonts.strong
            Layout.fillWidth: true
            Accessible.ignored: true
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            text: KI18n.i18nc("@label which side of the mix is louder now", "Side now")
            color: AppColors.muted
            Layout.fillWidth: true
        }

        QQC2.Label {
            id: sideValue

            text: panel.words.sideNow(panel.kind, reading.shown.percent ?? 0)
            color: AppColors.accentText
            font.family: AppFonts.monoFamily
            Accessible.ignored: true
        }
    }

    SoundSideStrip {
        id: sideNow

        Layout.fillWidth: true
        implicitHeight: AppMetrics.stripSize("small").height
        shown: reading.shown
        gate: panel.gate
        show: panel.placement.show ?? "mark"
        markWidth: AppMetrics.stripSize("small").mark
        wordSize: AppMetrics.stripSize("small").word
        word: panel.words.stripWord(panel.kind)
        activeFocusOnTab: true
        Accessible.role: Accessible.ProgressBar
        Accessible.name: KI18n.i18nc("@label which side of the mix is louder now", "Side now")
        Accessible.description: panel.words.sideSpoken(panel.kind, reading.shown.percent ?? 0)
    }

    // Under the track and not under the plate, so that the figures are true.
    Item {
        Layout.fillWidth: true
        Layout.topMargin: AppMetrics.pullIntoRow
        implicitHeight: scaleFigures.itemAt(0)?.implicitHeight ?? 0

        Repeater {
            id: scaleFigures

            model: [100, 50, 0, 50, 100]

            delegate: QQC2.Label {
                id: figure

                required property int modelData
                required property int index

                x: sideNow.trackLeft + sideNow.track.width * figure.index / 4 - figure.width / 2
                text: figure.modelData
                color: AppColors.muted
                font: AppFonts.small
                Accessible.ignored: true
            }
        }
    }

    QQC2.Label {
        id: alikeText

        visible: panel.kind === "same"
        text: KI18n.i18nc("@info a Sound Side whose two sides are one sound", "Both sides carry the same sound here, so there is no side to show. Put this block before One Ear Mix, and turn off the program's own mono option")
        color: AppColors.warningText
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }

    QQC2.Label {
        id: oneSideText

        visible: reading.shown.oneSideOnly === true
        text: KI18n.i18nc("@info a Sound Side whose sound stays on one side", "Sound on one side only")
        color: AppColors.muted
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }

    QQC2.Label {
        id: moveText

        visible: panel.foldBefore !== ""
        text: KI18n.i18nc("@info %1 is what the block is called", "%1 comes before this block, so the sides may be gone by here", panel.foldName())
        color: AppColors.muted
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }

    function foldName(): string {
        const name = panel.foldBefore === "" ? "" : (SessionModel.block(panel.foldBefore).nickname ?? "");
        return name !== "" ? name : BlockPresentation.name("hearing.oneearmix");
    }

    QQC2.Button {
        id: moveControl

        visible: panel.foldBefore !== ""
        text: KI18n.i18nc("@action:button", "Move Before One Ear Mix")
        Layout.fillWidth: true
        Accessible.name: text
        onClicked: SessionModel.moveBlockBefore(panel.blockId, panel.foldBefore)
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            text: KI18n.i18nc("@label how loud this lane is", "Level on this lane")
            color: AppColors.muted
            Layout.fillWidth: true
        }

        QQC2.Label {
            id: levelValue

            text: levelBar.running ? panel.words.level(reading.shown.level ?? panel.words.noLevel) : ""
            color: AppColors.accentText
            font.family: AppFonts.monoFamily
            Accessible.ignored: true
        }
    }

    LevelBar {
        id: levelBar

        Layout.fillWidth: true
        Layout.topMargin: AppMetrics.pullIntoRow
        implicitHeight: Kirigami.Units.smallSpacing * 2
        from: panel.lowestGate
        name: KI18n.i18nc("@label how loud this lane is", "Level on this lane")
        db: reading.shown.level ?? panel.words.noLevel
        running: panel.kind !== "stopped" && panel.kind !== "off"

        // Where the gate stands.
        Rectangle {
            id: gateMark

            x: (panel.gate - levelBar.from) / (levelBar.to - levelBar.from) * parent.width
            width: 1
            height: parent.height
            color: AppColors.text
        }
    }

    TypedValueRow {
        id: gateControl

        blockId: panel.blockId
        paramId: "gateDb"
        label: KI18n.i18nc("@label under this level a Sound Side shows no side", "Level gate")
        unit: KI18n.i18nc("@label decibels against full scale", "dBFS")
        spoken: panel.words.level(gateControl.value)
        value: panel.gate
        from: panel.lowestGate
        to: panel.highestGate
    }

    QQC2.Label {
        text: KI18n.i18nc("@label which part of the sound a Sound Side reads", "Band")
        color: AppColors.muted
    }

    SegmentedFrame {
        shrinkable: true
        Layout.fillWidth: true

        QQC2.ButtonGroup {
            id: bandGroup
        }

        Repeater {
            id: bandRepeater

            model: ["all", "range"]

            delegate: SegmentedChoice {
                id: bandButton

                required property string modelData

                text: panel.words.bandName(bandButton.modelData)
                Accessible.name: text
                QQC2.ButtonGroup.group: bandGroup
                onClicked: panel.chooseBand(bandButton.modelData)

                SessionBound {
                    control: bandButton
                    property: "checked"
                    value: panel.band === bandButton.modelData
                }
            }
        }
    }

    TypedValueRow {
        id: fromControl

        visible: panel.band === "range"
        blockId: panel.blockId
        paramId: "fromHz"
        label: KI18n.i18nc("@label the bottom of the band a Sound Side reads", "From")
        unit: KI18n.i18nc("@label hertz", "Hz")
        spoken: panel.words.hertz(fromControl.value)
        value: panel.from
        from: panel.lowestFrom
        to: panel.highestFrom
        stepSize: 100
        pageStepSize: 500
        logarithmic: true
        writes: false
        onEdited: (number, moving) => panel.moveEdge(false, number, moving)
    }

    TypedValueRow {
        id: toControl

        visible: panel.band === "range"
        blockId: panel.blockId
        paramId: "toHz"
        label: KI18n.i18nc("@label the top of the band a Sound Side reads", "To")
        unit: KI18n.i18nc("@label hertz", "Hz")
        spoken: panel.words.hertz(toControl.value)
        value: panel.to
        from: panel.lowestTo
        to: panel.highestTo
        stepSize: 100
        pageStepSize: 500
        logarithmic: true
        writes: false
        onEdited: (number, moving) => panel.moveEdge(true, number, moving)
    }

    QQC2.Switch {
        id: stripControl

        text: KI18n.i18nc("@option:check", "Strip on screen")
        Accessible.name: text
        onToggled: panel.showStrip(stripControl.checked)

        SessionBound {
            control: stripControl
            property: "checked"
            value: panel.stripOn
        }
    }

    QQC2.Label {
        id: onTopText

        visible: !FloatingWindows.keepsOnTop
        text: KI18n.i18nc("@info under the Strip on screen switch", "May not stay on top here")
        color: AppColors.muted
        font: AppFonts.small
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }

    QQC2.Label {
        id: takenText

        visible: panel.stripTaken
        text: KI18n.i18nc("@info under the Strip on screen switch", "There is one strip, and another Sound Side has it")
        color: AppColors.muted
        font: AppFonts.small
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }

    QQC2.Label {
        visible: panel.stripOn
        text: KI18n.i18nc("@info under the Strip on screen switch", "It shows while this window or the tray icon is running")
        color: AppColors.muted
        font: AppFonts.small
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }

    QQC2.Label {
        text: KI18n.i18nc("@label how big the strip on screen is", "Strip size")
        color: AppColors.muted
    }

    SegmentedFrame {
        shrinkable: true
        Layout.fillWidth: true

        QQC2.ButtonGroup {
            id: sizeGroup
        }

        Repeater {
            id: sizeRepeater

            model: ["small", "medium", "large"]

            delegate: SegmentedChoice {
                id: sizeButton

                required property string modelData

                text: panel.words.sizeName(sizeButton.modelData)
                Accessible.name: text
                QQC2.ButtonGroup.group: sizeGroup
                onClicked: FloatingSurface.setPlacement("strip", { size: sizeButton.modelData })

                SessionBound {
                    control: sizeButton
                    property: "checked"
                    value: panel.placement.size === sizeButton.modelData
                }
            }
        }
    }

    QQC2.Label {
        text: KI18n.i18nc("@label what the strip on screen draws", "Show")
        color: AppColors.muted
    }

    SegmentedFrame {
        shrinkable: true
        Layout.fillWidth: true

        QQC2.ButtonGroup {
            id: showGroup
        }

        Repeater {
            id: showRepeater

            model: ["mark", "bars"]

            delegate: SegmentedChoice {
                id: showButton

                required property string modelData

                text: panel.words.showName(showButton.modelData)
                Accessible.name: text
                QQC2.ButtonGroup.group: showGroup
                onClicked: FloatingSurface.setPlacement("strip", { show: showButton.modelData })

                SessionBound {
                    control: showButton
                    property: "checked"
                    value: panel.placement.show === showButton.modelData
                }
            }
        }
    }

    // One above the other, as a longer language would cut them off side by side.
    ColumnLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        QQC2.Button {
            id: placeControl

            text: KI18n.i18nc("@action:button", "Place Strip…")
            enabled: !FloatingWindows.placing
            Layout.fillWidth: true
            Accessible.name: text
            QQC2.ToolTip.text: KI18n.i18nc("@info:tooltip", "Do this before playing: it takes the keyboard while it lasts")
            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            onClicked: FloatingWindows.startPlacing()
        }

        QQC2.Button {
            id: testControl

            text: KI18n.i18nc("@action:button", "Test Strip")
            Layout.fillWidth: true
            Accessible.name: text
            onClicked: FloatingWindows.sweepStrip()
        }
    }

    FloatingPlaceSection {
        id: place

        title: KI18n.i18nc("@title:group a part of the Sound Side card", "Placement")
        surfaceName: "strip"
        showsSize: false
        note: KI18n.i18nc("@info the strip's place: 'across 50 %, down 4 %'", "%1 %, %2 %",
                          Math.round(place.placement.left ?? 0), Math.round(place.placement.top ?? 0))
        acrossLabel: KI18n.i18nc("@label where the strip sits across the screen", "Across")
        downLabel: KI18n.i18nc("@label where the strip sits down the screen", "Down")
        unit: KI18n.i18nc("@label percent of the screen", "%")
        stepSize: 1
        pageStepSize: 5
        acrossTop: 100
        downTop: 100
    }

    InfoLine {
        id: info

        text: KI18n.i18nc("@info what Sound Side can't know",
                          "Shows the louder side of the whole mix, never what made the sound. Music and voices move it too, and it can be wrong. Some games and tournaments forbid tools like this: check their rules")
    }
}
