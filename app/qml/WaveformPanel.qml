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

// The Waveform block's settings: the sound at full size, how it is drawn and
// how it moves.
//
// **Everything here is a setting of the block**, written to the session and
// undone like any other. None of it reaches the sound: the block keeps what
// was chosen and the drawing does the rest, so a change shows on the next
// frame and starts nothing again.
ColumnLayout {
    id: panel

    /// The block whose settings these are, set once the panel exists.
    property string blockId: ""

    /// The block's ranges; tst_waveform.qml holds them against the catalogue.
    readonly property real shortestSpan: 10
    readonly property real longestSpan: 10000
    readonly property real lowestGain: -24
    readonly property real highestGain: 48

    readonly property WaveformWords words: reading.words
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
    readonly property alias styleChoices: styleRepeater
    readonly property alias motionChoices: motionRepeater
    readonly property alias refreshChoices: refreshRepeater
    readonly property alias refreshNote: refreshNoteText
    readonly property alias spanRow: spanControl
    readonly property alias gainRow: gainControl
    readonly property alias autoFitCheck: autoFitControl
    readonly property alias channelChoices: channelRepeater
    readonly property alias triggerCheck: triggerControl
    readonly property alias triggerNote: triggerNoteText
    readonly property alias gridCheck: gridControl
    readonly property alias scaleChoices: scaleRepeater
    readonly property alias infoLine: info

    /// Wide enough for a trace to read as one.
    readonly property real panelWidth: Kirigami.Units.gridUnit * 24

    function choose(paramId: string, id: string) {
        SessionModel.setParam(panel.blockId, paramId, id);
    }

    function spanSpoken(ms: real): string {
        return panel.words.spanText(ms);
    }

    spacing: AppMetrics.panelRowGap

    WaveformState {
        id: reading

        blockId: panel.blockId
    }

    WaveformScope {
        id: well

        reading: panel.reader
        absenceFont: AppFonts.strong
        Layout.fillWidth: true
        Layout.preferredHeight: Kirigami.Units.gridUnit * 10
        Accessible.role: Accessible.Graphic
        Accessible.name: KI18n.i18nc("@label the sound drawn on a Waveform card", "Waveform")
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

            text: KI18n.i18nc("@action:button keeps the trace shown and takes no new one", "Hold")
            checkable: true
            Accessible.name: text
            QQC2.ToolTip.text: KI18n.i18nc("@info:tooltip", "Keeps the trace on the screen so that you can look at it")
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

    QQC2.Label {
        text: KI18n.i18nc("@label how a Waveform is drawn", "Style")
        color: AppColors.muted
    }

    SegmentedFrame {
        shrinkable: true
        Layout.fillWidth: true

        QQC2.ButtonGroup {
            id: styleGroup
        }

        Repeater {
            id: styleRepeater

            model: panel.words.styles

            delegate: SegmentedChoice {
                id: styleButton

                required property string modelData

                text: panel.words.styleName(styleButton.modelData)
                Accessible.name: text
                QQC2.ButtonGroup.group: styleGroup
                onClicked: panel.choose("style", styleButton.modelData)

                SessionBound {
                    control: styleButton
                    property: "checked"
                    value: reading.style === styleButton.modelData
                }
            }
        }
    }

    QQC2.Label {
        text: KI18n.i18nc("@label how a Waveform moves between refreshes", "Motion")
        color: AppColors.muted
    }

    SegmentedFrame {
        shrinkable: true
        Layout.fillWidth: true

        QQC2.ButtonGroup {
            id: motionGroup
        }

        Repeater {
            id: motionRepeater

            model: panel.words.motions

            delegate: SegmentedChoice {
                id: motionButton

                required property string modelData

                text: panel.words.motionName(motionButton.modelData)
                Accessible.name: text
                QQC2.ToolTip.text: panel.words.motionTip(motionButton.modelData)
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                QQC2.ButtonGroup.group: motionGroup
                onClicked: panel.choose("motion", motionButton.modelData)

                SessionBound {
                    control: motionButton
                    property: "checked"
                    value: reading.motion === motionButton.modelData
                }
            }
        }
    }

    QQC2.Label {
        text: KI18n.i18nc("@label how many times a second a Waveform is drawn", "Refresh rate, per second")
        color: AppColors.muted
    }

    SegmentedFrame {
        shrinkable: true
        Layout.fillWidth: true
        Accessible.name: KI18n.i18nc("@label how many times a second a Waveform is drawn", "Refresh rate, per second")

        QQC2.ButtonGroup {
            id: refreshGroup
        }

        Repeater {
            id: refreshRepeater

            model: panel.words.refreshRates

            delegate: SegmentedChoice {
                id: refreshButton

                required property string modelData

                text: panel.words.refreshName(refreshButton.modelData)
                Accessible.name: KI18n.i18nc("@action:button how many times a second a Waveform is drawn", "%1 times a second", text)
                QQC2.ButtonGroup.group: refreshGroup
                onClicked: panel.choose("refresh", refreshButton.modelData)

                SessionBound {
                    control: refreshButton
                    property: "checked"
                    value: reading.refresh === refreshButton.modelData
                }
            }
        }
    }

    QQC2.Label {
        id: refreshNoteText

        visible: panel.refreshOutrunsCycle
        text: KI18n.i18nc("@info under a Waveform's refresh rate, %1 a whole number",
                          "Sound arrives about %1 times a second at this cycle length, so a faster refresh repeats itself",
                          Math.round(panel.arrivals))
        color: AppColors.muted
        font: AppFonts.small
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }

    TypedValueRow {
        id: spanControl

        blockId: panel.blockId
        paramId: "spanMs"
        label: KI18n.i18nc("@label how much time a Waveform shows across its width", "Time shown")
        unit: KI18n.i18nc("@label milliseconds", "ms")
        spoken: panel.spanSpoken(spanControl.value)
        value: reading.spanMs
        from: panel.shortestSpan
        to: panel.longestSpan
        pageStepSize: 50
        logarithmic: true
    }

    TypedValueRow {
        id: gainControl

        blockId: panel.blockId
        paramId: "gainDb"
        label: KI18n.i18nc("@label how much a Waveform's trace is enlarged", "Gain")
        unit: KI18n.i18nc("@label decibels", "dB")
        spoken: panel.words.gainText(gainControl.value)
        value: reading.gainDb
        from: panel.lowestGain
        to: panel.highestGain
        enabled: !reading.autoFit
    }

    QQC2.CheckBox {
        id: autoFitControl

        text: KI18n.i18nc("@option:check a Waveform enlarges its trace to fill the height", "Auto fit")
        onToggled: SessionModel.setParam(panel.blockId, "autoFit", autoFitControl.checked)

        SessionBound {
            control: autoFitControl
            property: "checked"
            value: reading.autoFit
        }
    }

    QQC2.Label {
        text: KI18n.i18nc("@label which channels a Waveform draws", "Channels")
        color: AppColors.muted
    }

    SegmentedFrame {
        shrinkable: true
        Layout.fillWidth: true

        QQC2.ButtonGroup {
            id: channelGroup
        }

        Repeater {
            id: channelRepeater

            model: panel.words.channelChoices

            delegate: SegmentedChoice {
                id: channelButton

                required property string modelData

                text: panel.words.channelsName(channelButton.modelData)
                Accessible.name: text
                QQC2.ButtonGroup.group: channelGroup
                onClicked: panel.choose("channels", channelButton.modelData)

                SessionBound {
                    control: channelButton
                    property: "checked"
                    value: reading.channels === channelButton.modelData
                }
            }
        }
    }

    QQC2.CheckBox {
        id: triggerControl

        text: KI18n.i18nc("@option:check a Waveform starts each trace at the same point of the wave", "Start on a rising zero crossing")
        onToggled: SessionModel.setParam(panel.blockId, "trigger", triggerControl.checked)

        SessionBound {
            control: triggerControl
            property: "checked"
            value: reading.trigger
        }
    }

    QQC2.Label {
        id: triggerNoteText

        visible: reading.trigger && well.scope.showing && !well.scope.crossed
        text: KI18n.i18nc("@info under a Waveform's trigger", "No crossing found: the newest sound is shown as it comes")
        color: AppColors.muted
        font: AppFonts.small
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }

    QQC2.CheckBox {
        id: gridControl

        text: KI18n.i18nc("@option:check lines behind a Waveform's trace", "Grid")
        onToggled: SessionModel.setParam(panel.blockId, "grid", gridControl.checked)

        SessionBound {
            control: gridControl
            property: "checked"
            value: reading.grid
        }
    }

    QQC2.Label {
        text: KI18n.i18nc("@label what the height of a Waveform means", "Scale")
        color: AppColors.muted
    }

    SegmentedFrame {
        shrinkable: true
        Layout.fillWidth: true

        QQC2.ButtonGroup {
            id: scaleGroup
        }

        Repeater {
            id: scaleRepeater

            model: panel.words.scales

            delegate: SegmentedChoice {
                id: scaleButton

                required property string modelData

                text: panel.words.scaleName(scaleButton.modelData)
                Accessible.name: text
                QQC2.ButtonGroup.group: scaleGroup
                onClicked: panel.choose("scale", scaleButton.modelData)

                SessionBound {
                    control: scaleButton
                    property: "checked"
                    value: reading.scale === scaleButton.modelData
                }
            }
        }
    }

    InfoLine {
        id: info

        text: KI18n.i18nc("@info what a Waveform does to the sound", "Passes the sound on exactly as it arrived. The block works only while it is on the screen")
    }
}
