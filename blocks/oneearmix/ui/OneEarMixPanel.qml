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

// The One Ear Mix block's settings: where the sound goes, what each mix does
// to sound, and a comparison with Plain Mono.
//
// **Not EarChoice.** That row offers Left and Right and says the other side
// passes through, which is false here.
//
// **Match Levels is a press and not a setting**: the block reads both mixes
// for a few seconds and publishes the two figures, and this panel takes the
// difference once. It only ever lowers the fold, and it writes nothing when
// the difference makes no sense.
ColumnLayout {
    id: panel

    /// The block whose settings these are, set once the panel exists.
    property string blockId: ""

    /// The block's range; tst_oneearmix.qml holds it against the catalogue.
    readonly property real mostLowered: 6
    /// Under this a reading is silence (GatedLoudness::kAbsoluteGateLufs).
    readonly property real audibleLufs: -70
    /// A reading takes about three seconds; this gives up on one lost.
    readonly property int readingPatienceMs: 6000

    /// The reading under way, and what the last one came to.
    property bool matching: false
    property bool sawReading: false
    property string matchMessage: ""

    /// The controls, so that a test drives what a person drives.
    readonly property alias statusLabel: statusText
    readonly property alias explanationLine: explanation
    readonly property alias sendChoices: sendRepeater
    readonly property alias hearChoices: hearRepeater
    readonly property alias tableRows: tableRepeater
    readonly property alias compareSection: compare
    readonly property alias pickFirstLabel: pickFirst
    readonly property alias matchButton: matchControl
    readonly property alias matchLabel: matchText
    readonly property alias lowerSlider: lowerControl
    readonly property alias lowerField: lowerTyped
    readonly property alias infoLine: info
    readonly property alias figuresButton: figuresControl
    readonly property alias figuresLabel: figuresText
    readonly property alias reader: reading

    function chooseSend(id: string) {
        if (id === reading.sendTo) {
            return;
        }
        SessionModel.setParam(panel.blockId, "sendTo", id);
        Announcer.say(panel, "oneEarMix:" + panel.blockId, words.status(id));
    }

    function chooseHear(id: string) {
        if (id !== reading.hear) {
            SessionModel.setParam(panel.blockId, "hear", id);
        }
    }

    function startMatching() {
        panel.matchMessage = "";
        panel.sawReading = false;
        panel.matching = true;
        patience.restart();
        SessionModel.setParam(panel.blockId, "matchLevels", true);
    }

    function followReading() {
        if (!panel.matching) {
            return;
        }
        if (reading.stage === reading.measuring) {
            panel.sawReading = true;
        } else if (reading.stage === reading.measured && panel.sawReading) {
            panel.finishMatching();
        }
    }

    function finishMatching() {
        panel.matching = false;
        patience.stop();
        // From the values themselves: the stage changes with them, and what
        // is worked out from them may not have caught up by now.
        const result = words.reading(reading.values[reading.foldValue], reading.values[reading.monoValue],
                                     panel.audibleLufs, panel.mostLowered);
        if (result.lower >= 0) {
            SessionModel.setParam(panel.blockId, "lowerDb", result.lower);
        }
        panel.tellMatch(result.message);
    }

    function tellMatch(message: string) {
        panel.matchMessage = message;
        Announcer.say(panel, "oneEarMixMatch:" + panel.blockId, message);
    }

    function giveUpMatching(message: string) {
        if (panel.matching) {
            panel.matching = false;
            patience.stop();
            panel.tellMatch(message);
        }
    }

    spacing: AppMetrics.panelRowGap

    OneEarMixWords {
        id: words
    }

    OneEarMixState {
        id: reading

        blockId: panel.blockId
        onStageChanged: panel.followReading()
        onLiveChanged: {
            if (!reading.live) {
                panel.giveUpMatching(KI18n.i18nc("@info a One Ear Mix's reading was cut short", "The session stopped, so nothing was measured."));
            }
        }
    }

    Timer {
        id: patience

        interval: panel.readingPatienceMs
        onTriggered: panel.giveUpMatching(KI18n.i18nc("@info a One Ear Mix's reading never ended", "No reading came back, so nothing was changed."))
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing * 2

        Rectangle {
            implicitWidth: AppMetrics.stateDotSize
            implicitHeight: AppMetrics.stateDotSize
            radius: width / 2
            color: !reading.live && reading.acting ? AppColors.muted
                                                   : (reading.arriving ? AppColors.liveMark
                                                                       : (reading.lowered ? AppColors.warningMark : AppColors.okMark))
            Layout.alignment: Qt.AlignVCenter
            Accessible.ignored: true
        }

        QQC2.Label {
            id: statusText

            text: words.status(reading.sendTo)
            color: AppColors.text
            font: AppFonts.strong
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }

    QQC2.Label {
        text: KI18n.i18nc("@label which ear a One Ear Mix sends the sound to", "Send to")
        color: AppColors.muted
    }

    SegmentedFrame {
        shrinkable: true
        Layout.fillWidth: true

        QQC2.ButtonGroup {
            id: sendGroup
        }

        Repeater {
            id: sendRepeater

            model: ["none", "left", "right", "both"]

            delegate: SegmentedChoice {
                id: sendButton

                required property string modelData

                text: words.sendName(sendButton.modelData)
                Accessible.name: text
                QQC2.ButtonGroup.group: sendGroup
                onClicked: panel.chooseSend(sendButton.modelData)

                SessionBound {
                    control: sendButton
                    property: "checked"
                    value: reading.sendTo === sendButton.modelData
                }
            }
        }
    }

    InfoLine {
        id: explanation

        text: words.explanation(reading.sendTo)
    }

    QQC2.Label {
        text: KI18n.i18nc("@title a table on the One Ear Mix card", "What each mix does to sound")
        color: AppColors.muted
    }

    GridLayout {
        Layout.fillWidth: true
        columns: 3
        columnSpacing: Kirigami.Units.smallSpacing * 2
        rowSpacing: 0

        ElidedLabel {
            text: KI18n.i18nc("@title:column a kind of sound", "Sound in the mix")
            color: AppColors.muted
            font: AppFonts.small
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
        QQC2.Label {
            text: words.plainMonoName
            color: AppColors.muted
            font: AppFonts.small
        }
        QQC2.Label {
            text: KI18n.i18nc("@title:column the One Ear Mix fold, in a table beside Plain Mono", "This mix")
            color: AppColors.muted
            font: AppFonts.small
        }

        Repeater {
            id: tableRepeater

            // Three real cells a row, each saying its row and column to a
            // screen reader, since one read alone means nothing.
            model: words.tableCells()

            delegate: QQC2.Label {
                required property var modelData

                text: modelData.text
                color: modelData.strong ? AppColors.text : AppColors.muted
                font: AppFonts.small
                Accessible.name: modelData.name
                Layout.fillWidth: modelData.strong
            }
        }
    }

    CollapsibleSection {
        id: compare

        title: KI18n.i18nc("@title:group a part of the One Ear Mix card", "Compare with Plain Mono")
        enabled: reading.acting

        RowLayout {
            Layout.fillWidth: true
            spacing: AppMetrics.labelSpacing

            QQC2.Label {
                text: KI18n.i18nc("@label which of two mixes to listen to", "Hear")
                color: AppColors.muted
            }

            SegmentedFrame {
                shrinkable: true
                Layout.fillWidth: true

                QQC2.ButtonGroup {
                    id: hearGroup
                }

                Repeater {
                    id: hearRepeater

                    model: ["fold", "mono"]

                    delegate: SegmentedChoice {
                        id: hearButton

                        required property string modelData

                        text: words.hearName(hearButton.modelData)
                        Accessible.name: text
                        QQC2.ButtonGroup.group: hearGroup
                        onClicked: panel.chooseHear(hearButton.modelData)

                        SessionBound {
                            control: hearButton
                            property: "checked"
                            value: reading.hear === hearButton.modelData
                        }
                    }
                }
            }
        }

        QQC2.Button {
            id: matchControl

            text: panel.matching ? KI18n.i18nc("@action:button a One Ear Mix is reading the two levels", "Reading, 3 s")
                                 : KI18n.i18nc("@action:button read both mixes and lower the louder", "Match Levels")
            enabled: reading.live && !panel.matching
            QQC2.ToolTip.text: reading.live ? "" : KI18n.i18nc("@info:tooltip", "Run the session and play something to compare the two")
            QQC2.ToolTip.visible: hovered && QQC2.ToolTip.text !== ""
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            onClicked: panel.startMatching()
        }

        QQC2.Label {
            id: matchText

            visible: text !== ""
            text: panel.matchMessage
            color: AppColors.text
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            QQC2.Label {
                text: KI18n.i18nc("@label:slider how far a One Ear Mix turns its mix down", "Lower this mix by")
                color: AppColors.text
                Layout.fillWidth: true
            }

            ValueField {
                id: lowerTyped

                Layout.preferredWidth: Kirigami.Units.gridUnit * 3.5
                label: KI18n.i18nc("@label:slider how far a One Ear Mix turns its mix down", "Lower this mix by")
                value: reading.lowerDb
                from: 0
                to: panel.mostLowered
                stepSize: 0.5
                pageStepSize: 1
                decimals: 1
                Accessible.description: words.decibels(reading.lowerDb)
                onValueEdited: number => SessionModel.setParam(panel.blockId, "lowerDb", Math.round(number * 10) / 10)
            }

            QQC2.Label {
                text: KI18n.i18nc("@label the unit of Lower this mix by", "dB")
                color: AppColors.muted
            }
        }

        QQC2.Slider {
            id: lowerControl

            Layout.fillWidth: true
            from: 0
            to: panel.mostLowered
            stepSize: 0.1
            snapMode: QQC2.Slider.SnapAlways
            // The field beside it is what the keys step.
            activeFocusOnTab: false
            Accessible.name: KI18n.i18nc("@label:slider how far a One Ear Mix turns its mix down", "Lower this mix by")
            Accessible.description: words.decibels(lowerControl.value)

            onMoved: SessionModel.setParam(panel.blockId, "lowerDb", Math.round(value * 10) / 10, true)
            onPressedChanged: if (!pressed) {
                SessionModel.commitParam(panel.blockId, "lowerDb");
            }

            SessionBound {
                control: lowerControl
                property: "value"
                value: reading.lowerDb
                suspended: lowerControl.pressed
            }
        }
    }

    QQC2.Label {
        id: pickFirst

        visible: !reading.acting
        text: KI18n.i18nc("@info why the comparison is not open to use yet", "Pick an ear first")
        color: AppColors.muted
        font: AppFonts.small
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        InfoLine {
            id: info

            text: KI18n.i18nc("@info what a One Ear Mix cannot do",
                              "Direction doesn't come back: a mix in one ear can't tell you where a sound is. A sound a quarter of a cycle apart between the sides can get quieter. Peaks can rise by several dB, so keep a limit after this block, as Volume has.")
        }

        QQC2.ToolButton {
            id: figuresControl

            checkable: true
            icon.name: "help-about-symbolic"
            display: QQC2.AbstractButton.IconOnly
            text: KI18n.i18nc("@action:button show the figures behind the line beside it", "Figures")
            Accessible.name: text
            Layout.alignment: Qt.AlignTop
            QQC2.ToolTip.text: text
            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
        }
    }

    QQC2.Label {
        id: figuresText

        visible: figuresControl.checked
        text: KI18n.i18nc("@info the figures behind the quarter of a cycle",
                          "At 1 kHz and a 48 kHz rate, sound where the right side leads the left by a quarter of a cycle comes through 30 dB lower, and sound where it lags comes through 3 dB higher. Plain Mono puts both 3 dB lower.")
        color: AppColors.muted
        font: AppFonts.small
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }
}
