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

// The Speech Now block's settings: its word now, its name, how it listens,
// where it shows, and how it times a pause.
//
// **The status word changes silently.** A sentence on every change would be
// spoken over the talker, so it is the row's accessible value, read when
// somebody lands on it, and a global shortcut asks for it from anywhere.
//
// **Where the label sits is not a setting of the block.** It is this desktop's
// (FloatingSurface), and one window holds every block's row, so that section
// reads and writes there and no edit or undo comes of it.
ColumnLayout {
    id: panel

    /// The block whose settings these are, set once the panel exists.
    property string blockId: ""

    /// The block's ranges; tst_speechnow.qml holds them against the catalogue.
    readonly property real longestHold: 2000
    readonly property real longestMark: 10
    readonly property real longestShortTalk: 2000
    /// How long a name may be.
    readonly property int longestName: 24

    readonly property var placement: FloatingSurface.placementRevision >= 0 ? FloatingSurface.placement("label") : ({})
    /// The screen the numbers below are measured on.
    readonly property var placedOn: {
        return FloatingWindows.screenNamed(panel.placement.screen ?? "") ?? FloatingWindows.screens[0] ?? null;
    }

    readonly property string modelRefused: words.modelRefused(reading.detector)
    readonly property bool floating: reading.params.showFloating === true
    readonly property bool inTray: reading.params.showInTray === true
    /// Rows the label had no room for: said, not hidden.
    readonly property bool labelFull: panel.floating && FloatingSurface.leftOut > 0

    /// The controls, so that a test drives what a person drives.
    readonly property alias statusLabel: statusText
    readonly property alias statusRow: statusLine
    readonly property alias nameField: nameControl
    readonly property alias voiceLabel: voiceText
    readonly property alias detectorChoices: detectorRepeater
    readonly property alias detectorReason: refusedText
    readonly property alias floatingSwitch: floatingControl
    readonly property alias traySwitch: trayControl
    readonly property alias onTopLine: onTopText
    readonly property alias fullLine: fullText
    readonly property alias placeSection: place
    readonly property alias sizeChoices: sizeRepeater
    readonly property alias screenChoice: screenControl
    readonly property alias leftField: leftControl
    readonly property alias topField: topControl
    readonly property alias timingSection: timing
    readonly property alias holdRow: holdControl
    readonly property alias markRow: markControl
    readonly property alias shortestRow: shortestControl
    readonly property alias infoLine: info
    readonly property alias reader: reading

    function chooseDetector(id: string) {
        if (id !== reading.chosenDetector) {
            SessionModel.setParam(panel.blockId, "detector", id);
        }
    }

    spacing: AppMetrics.panelRowGap

    SpeechNowState {
        id: reading

        blockId: panel.blockId
    }

    SpeechNowWords {
        id: words
    }

    RowLayout {
        id: statusLine

        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing * 2
        activeFocusOnTab: true
        Accessible.role: Accessible.StaticText
        Accessible.name: KI18n.i18nc("@label what a Speech Now says now", "Now")
        Accessible.description: statusText.text

        SpeechNowDot {
            kind: reading.kind
            Layout.alignment: Qt.AlignVCenter
        }

        QQC2.Label {
            id: statusText

            text: words.word(reading.kind)
            color: AppColors.text
            font: AppFonts.strong
            Layout.fillWidth: true
            Accessible.ignored: true
        }
    }

    QQC2.Label {
        text: KI18n.i18nc("@label what a Speech Now is called", "Name")
        color: AppColors.muted
    }

    QQC2.TextField {
        id: nameControl

        Layout.fillWidth: true
        maximumLength: panel.longestName
        placeholderText: KI18n.i18nc("@info:placeholder", "For example Call or You")
        Accessible.name: KI18n.i18nc("@label what a Speech Now is called", "Name")
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
            text: KI18n.i18nc("@label how likely a voice is on the lane", "Voice on this lane")
            color: AppColors.muted
            Layout.fillWidth: true
        }

        QQC2.Label {
            id: voiceText

            /// Twice a second: a number that moves with the signal says
            /// nothing anybody can read.
            property real shown: 0

            text: reading.watching ? words.percent(voiceText.shown)
                                   : KI18n.i18nc("@info nothing to read a voice from", "Not live")
            color: AppColors.accentText
            font.family: AppFonts.monoFamily
            Accessible.name: KI18n.i18nc("@label how likely a voice is on the lane", "Voice on this lane")
            Accessible.description: text

            Timer {
                interval: 500
                repeat: true
                triggeredOnStart: true
                running: reading.watching
                onTriggered: voiceText.shown = reading.voice
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.topMargin: AppMetrics.pullIntoRow
        implicitHeight: Kirigami.Units.smallSpacing * 2
        radius: AppMetrics.barRadius
        color: AppColors.well
        Accessible.ignored: true

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: reading.watching ? parent.width * Math.max(0, Math.min(1, reading.voice)) : 0
            radius: parent.radius
            color: AppColors.accent
        }

        // Where a frame counts as voiced.
        Rectangle {
            x: parent.width / 2
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 1
            color: AppColors.muted
        }
    }

    QQC2.Label {
        text: KI18n.i18nc("@label how Speech Now listens", "Detector")
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

                text: words.detectorChoice(detectorButton.modelData)
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
        text: KI18n.i18nc("@label where a Speech Now shows", "Show it on")
        color: AppColors.muted
    }

    QQC2.CheckBox {
        id: floatingControl

        text: KI18n.i18nc("@option:check", "Floating label")
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
        text: KI18n.i18nc("@info under the Floating label check box", "This desktop may not keep it on top")
        color: AppColors.muted
        font: AppFonts.small
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }

    QQC2.Label {
        id: fullText

        visible: panel.labelFull
        text: KI18n.i18ncp("@info under the Floating label check box",
                           "The label has room for four. %1 more is left out.",
                           "The label has room for four. %1 more are left out.",
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

    CollapsibleSection {
        id: place

        title: KI18n.i18nc("@title:group a part of the Speech Now card", "Place the label")

        RowLayout {
            Layout.fillWidth: true
            spacing: AppMetrics.labelSpacing

            QQC2.Label {
                text: KI18n.i18nc("@label how big the floating label is", "Size")
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

                    model: ["small", "large"]

                    delegate: SegmentedChoice {
                        id: sizeButton

                        required property string modelData

                        text: words.sizeName(sizeButton.modelData)
                        Accessible.name: text
                        QQC2.ButtonGroup.group: sizeGroup
                        onClicked: FloatingSurface.setPlacement("label", { size: sizeButton.modelData })

                        SessionBound {
                            control: sizeButton
                            property: "checked"
                            value: panel.placement.size === sizeButton.modelData
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: AppMetrics.labelSpacing

            QQC2.Label {
                text: KI18n.i18nc("@label which screen the floating label is on", "Screen")
                color: AppColors.muted
            }

            QQC2.ComboBox {
                id: screenControl

                readonly property var names: FloatingWindows.screens.map(screen => screen.name).filter(name => name !== "")

                Layout.fillWidth: true
                model: [KI18n.i18nc("@item:inlistbox the screen the main window is on", "The window's")].concat(screenControl.names)
                currentIndex: screenControl.names.indexOf(panel.placement.screen ?? "") + 1
                Accessible.name: KI18n.i18nc("@label which screen the floating label is on", "Screen")
                onActivated: index => FloatingSurface.setPlacement("label", { screen: index === 0 ? "" : screenControl.names[index - 1] })
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: AppMetrics.labelSpacing

            QQC2.Label {
                text: KI18n.i18nc("@label how far the floating label sits from the left of the screen", "From the left")
                color: AppColors.muted
                Layout.fillWidth: true
            }

            ValueField {
                id: leftControl

                Layout.preferredWidth: Kirigami.Units.gridUnit * 4
                label: KI18n.i18nc("@label how far the floating label sits from the left of the screen", "From the left")
                value: panel.placement.left ?? 0
                to: panel.placedOn?.width ?? 16384
                decimals: 0
                stepSize: 10
                pageStepSize: 100
                onValueEdited: number => FloatingSurface.setPlacement("label", { left: number })
            }

            QQC2.Label {
                text: KI18n.i18nc("@label the unit of a place on the screen", "px")
                color: AppColors.muted
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: AppMetrics.labelSpacing

            QQC2.Label {
                text: KI18n.i18nc("@label how far the floating label sits from the top of the screen", "From the top")
                color: AppColors.muted
                Layout.fillWidth: true
            }

            ValueField {
                id: topControl

                Layout.preferredWidth: Kirigami.Units.gridUnit * 4
                label: KI18n.i18nc("@label how far the floating label sits from the top of the screen", "From the top")
                value: panel.placement.top ?? 0
                to: panel.placedOn?.height ?? 16384
                decimals: 0
                stepSize: 10
                pageStepSize: 100
                onValueEdited: number => FloatingSurface.setPlacement("label", { top: number })
            }

            QQC2.Label {
                text: KI18n.i18nc("@label the unit of a place on the screen", "px")
                color: AppColors.muted
            }
        }
    }

    CollapsibleSection {
        id: timing

        title: KI18n.i18nc("@title:group a part of the Speech Now card", "Timing")

        TypedValueRow {
            id: holdControl

            blockId: panel.blockId
            paramId: "holdMs"
            label: KI18n.i18nc("@label how long a voice may pause and still count as speaking", "Hold")
            unit: KI18n.i18nc("@label milliseconds", "ms")
            spoken: words.milliseconds(holdControl.value)
            value: reading.params.holdMs ?? 200
            from: 100
            to: panel.longestHold
            stepSize: 10
            pageStepSize: 100
            logarithmic: true
        }

        TypedValueRow {
            id: markControl

            blockId: panel.blockId
            paramId: "quietAgainSeconds"
            label: KI18n.i18nc("@label how long Quiet again stays up", "Quiet again for")
            unit: KI18n.i18nc("@label seconds", "s")
            spoken: words.seconds(markControl.value)
            value: reading.params.quietAgainSeconds ?? 2
            from: 0
            to: panel.longestMark
            stepSize: 0.1
            pageStepSize: 1
            decimals: 1
            atLowest: KI18n.i18nc("@info Quiet again for is at nought", "Off")
        }

        TypedValueRow {
            id: shortestControl

            blockId: panel.blockId
            paramId: "shortestTalkMs"
            label: KI18n.i18nc("@label a voice shorter than this earns no Quiet again", "Shortest talk")
            unit: KI18n.i18nc("@label milliseconds", "ms")
            spoken: words.milliseconds(shortestControl.value)
            value: reading.params.shortestTalkMs ?? 300
            from: 0
            to: panel.longestShortTalk
            stepSize: 10
            pageStepSize: 100
        }
    }

    InfoLine {
        id: info

        text: KI18n.i18nc("@info what Speech Now can't know",
                          "Shows that a voice is on this lane, not who it is. A pause can look like an end, and music can look like a voice. It can be wrong")
    }
}
