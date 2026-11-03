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

// The Spectrum block's settings: the bars at full size, how they are read and
// how they move.
//
// **Everything here is a setting of the block**, written to the session and
// undone like any other. None of it reaches the sound: the block keeps what
// was chosen and the drawing does the rest, so a change shows on the next
// frame and starts nothing again.
ColumnLayout {
    id: panel

    /// The block whose settings these are, set once the panel exists.
    property string blockId: ""

    readonly property SpectrumWords words: reading.words
    readonly property string feedState: well.scope.feedState
    /// What the cycle length lets through, in new sounds a second.
    readonly property real arrivals: well.scope.arrivalsPerSecond
    /// A refresh rate above what the cycle length delivers repeats itself.
    readonly property bool refreshOutrunsCycle: panel.arrivals > 0 && reading.refreshHz > panel.arrivals * 1.1

    /// The controls, so that a test drives what a person drives.
    readonly property alias reader: reading
    readonly property alias scope: well.scope
    readonly property alias scopeWell: well
    readonly property alias statusLabel: statusText
    readonly property alias holdButton: holdControl
    readonly property alias barsChoices: barsRow.buttons
    readonly property alias motionChoices: motionRow.buttons
    readonly property alias refreshChoices: refreshRow.buttons
    readonly property alias refreshNote: refreshNoteText
    readonly property alias scaleChoices: scaleRow.buttons
    readonly property alias fftSizeChoices: fftSizeRow.buttons
    readonly property alias fftSizeNote: fftSizeNoteText
    readonly property alias floorChoices: floorRow.buttons
    readonly property alias tiltChoices: tiltRow.buttons
    readonly property alias channelChoices: channelRow.buttons
    readonly property alias styleChoices: styleRow.buttons
    readonly property alias peaksCheck: peaksControl
    readonly property alias gridCheck: gridControl
    readonly property alias infoLine: info
    readonly property alias advancedSection: advanced

    /// Wide enough for the bars to read as bars.
    readonly property real panelWidth: Kirigami.Units.gridUnit * 24

    spacing: AppMetrics.panelRowGap

    SpectrumState {
        id: reading

        blockId: panel.blockId
    }

    SpectrumScope {
        id: well

        reading: panel.reader
        absenceFont: AppFonts.strong
        roomForLabels: true
        Layout.fillWidth: true
        Layout.preferredHeight: Kirigami.Units.gridUnit * 10
        Accessible.role: Accessible.Graphic
        Accessible.name: KI18n.i18nc("@label the bars drawn on a Spectrum card", "Spectrum")
        Accessible.description: statusText.text
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing * 2

        QQC2.Label {
            id: statusText

            text: panel.words.status(panel.feedState, reading.hold)
            color: AppColors.muted
            elide: Text.ElideRight
            Layout.fillWidth: true
        }

        QQC2.Button {
            id: holdControl

            text: KI18n.i18nc("@action:button keeps the bars shown and takes no new ones", "Hold")
            checkable: true
            Accessible.name: text
            QQC2.ToolTip.text: KI18n.i18nc("@info:tooltip", "Keeps the bars on the screen so that you can look at them")
            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            onToggled: SessionModel.setParam(panel.blockId, "hold", holdControl.checked)

            SessionBound {
                control: holdControl
                property: "checked"
                value: reading.hold
            }
        }
    }

    // --- advanced: how it is drawn and read ---
    //
    // Folded away under a heading, as on every other card: the graph
    // and Hold are what is reached for while watching, and the rest is
    // set once.
    CollapsibleSection {
        id: advanced

        title: KI18n.i18nc("@title:group the less usual settings", "Advanced")

        ScopeChoiceRow {
            id: barsRow

            blockId: panel.blockId
            paramId: "bars"
            title: KI18n.i18nc("@label how many bars a Spectrum shows", "Bars")
            choices: panel.words.barCounts
            current: reading.bars
            nameOf: panel.words.barsName
        }

        ScopeChoiceRow {
            id: motionRow

            blockId: panel.blockId
            paramId: "motion"
            title: KI18n.i18nc("@label how a Spectrum's bars move between refreshes", "Motion")
            choices: panel.words.motions
            current: reading.motion
            nameOf: panel.words.motionName
            tipOf: panel.words.motionTip
        }

        ScopeChoiceRow {
            id: refreshRow

            blockId: panel.blockId
            paramId: "refresh"
            title: KI18n.i18nc("@label how many times a second a Spectrum is worked out", "Refresh rate, per second")
            choices: panel.words.refreshRates
            current: reading.refresh
            nameOf: panel.words.refreshName
            spokenOf: choice => KI18n.i18nc("@action:button how many times a second a Spectrum is worked out", "%1 times a second", panel.words.refreshName(choice))
        }

        QQC2.Label {
            id: refreshNoteText

            visible: panel.refreshOutrunsCycle
            text: KI18n.i18nc("@info under a Spectrum's refresh rate, %1 a whole number",
                              "Sound arrives about %1 times a second at this cycle length, so a faster refresh repeats itself",
                              Math.round(panel.arrivals))
            color: AppColors.muted
            font: AppFonts.small
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        ScopeChoiceRow {
            id: scaleRow

            blockId: panel.blockId
            paramId: "scale"
            title: KI18n.i18nc("@label how a Spectrum spreads the frequencies across its width", "Frequency scale")
            choices: panel.words.scales
            current: reading.scale
            nameOf: panel.words.scaleName
        }

        ScopeChoiceRow {
            id: fftSizeRow

            blockId: panel.blockId
            paramId: "fftSize"
            title: KI18n.i18nc("@label how many samples of sound each look of a Spectrum takes in", "FFT size")
            choices: panel.words.fftSizes
            current: reading.fftSize
            nameOf: panel.words.fftSizeName
        }

        QQC2.Label {
            id: fftSizeNoteText

            visible: well.scope.frameMs > 0
            text: KI18n.i18nc("@info under a Spectrum's FFT size, %1 a duration, %2 a frequency difference; both whole numbers",
                              "Looks at %1 ms at a time, and tells apart sounds %2 Hz from one another",
                              Math.round(well.scope.frameMs), Math.round(well.scope.resolutionHz))
            color: AppColors.muted
            font: AppFonts.small
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        ScopeChoiceRow {
            id: floorRow

            blockId: panel.blockId
            paramId: "floor"
            title: KI18n.i18nc("@label the level at the bottom of a Spectrum's view", "Level range")
            choices: panel.words.floors
            current: reading.floor
            nameOf: panel.words.floorName
        }

        ScopeChoiceRow {
            id: tiltRow

            blockId: panel.blockId
            paramId: "tilt"
            title: KI18n.i18nc("@label how much a Spectrum lifts high frequencies against low ones", "Tilt")
            choices: panel.words.tilts
            current: reading.tilt
            nameOf: panel.words.tiltName
        }

        ScopeChoiceRow {
            id: channelRow

            blockId: panel.blockId
            paramId: "channels"
            title: KI18n.i18nc("@label which channels a Spectrum reads", "Channels")
            choices: panel.words.channelChoices
            current: reading.channels
            nameOf: panel.words.channelsName
        }

        ScopeChoiceRow {
            id: styleRow

            blockId: panel.blockId
            paramId: "style"
            title: KI18n.i18nc("@label how a Spectrum's bars are drawn", "Style")
            choices: panel.words.styles
            current: reading.style
            nameOf: panel.words.styleName
        }

        QQC2.CheckBox {
            id: peaksControl

            text: KI18n.i18nc("@option:check a mark over each of a Spectrum's bars at its recent peak", "Peak marks")
            onToggled: SessionModel.setParam(panel.blockId, "peaks", peaksControl.checked)

            SessionBound {
                control: peaksControl
                property: "checked"
                value: reading.peaks
            }
        }

        QQC2.CheckBox {
            id: gridControl

            text: KI18n.i18nc("@option:check lines and numbers behind a Spectrum's bars", "Grid and labels")
            onToggled: SessionModel.setParam(panel.blockId, "grid", gridControl.checked)

            SessionBound {
                control: gridControl
                property: "checked"
                value: reading.grid
            }
        }
    }


    InfoLine {
        id: info

        text: KI18n.i18nc("@info what a Spectrum does to the sound", "Passes the sound on exactly as it arrived. The block works only while it is on the screen")
    }
}
