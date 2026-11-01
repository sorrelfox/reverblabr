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

// Sudden Sounds' settings: the ear, four words for how much, and what it has
// been doing.
//
// **The count is there so somebody can see the block touching speech:** 14
// times in a minute with nothing dropping says so. It counts what the block
// did and says nothing about the person.
ColumnLayout {
    id: panel

    /// The block whose settings these are, set once the panel exists.
    property string blockId: ""

    readonly property SuddenSoundsState reading: SuddenSoundsState {
        blockId: panel.blockId
    }
    readonly property SuddenSoundsWords words: panel.reading.words

    /// The controls, so that a test drives what a person drives.
    readonly property alias earChoice: earControl
    readonly property alias strengthChoices: strengthRepeater
    readonly property alias statusLabel: statusText
    readonly property alias statusDot: dot
    readonly property alias softenedBar: softenedMeter
    readonly property alias softenedLabel: softenedText
    readonly property alias countLabel: countText
    readonly property alias infoLine: musicLine
    readonly property alias placingLabel: placingText

    spacing: AppMetrics.panelRowGap

    QQC2.Label {
        Layout.fillWidth: true
        text: panel.words.description()
        color: AppColors.muted
        wrapMode: Text.WordWrap
    }

    EarChoice {
        id: earControl

        Layout.fillWidth: true
        blockId: panel.blockId
        typeId: "hearing.suddensounds"
    }

    // --- how much ---
    RowLayout {
        Layout.fillWidth: true
        spacing: AppMetrics.labelSpacing

        Accessible.role: Accessible.Grouping
        Accessible.name: KI18n.i18nc("@label how much Sudden Sounds softens", "Strength")

        QQC2.Label {
            text: KI18n.i18nc("@label how much Sudden Sounds softens", "Strength:")
            color: AppColors.muted
        }

        SegmentedFrame {
            QQC2.ButtonGroup {
                id: strengthGroup
            }

            Repeater {
                id: strengthRepeater

                model: panel.words.strengths

                delegate: SegmentedChoice {
                    id: strengthButton

                    required property string modelData

                    text: panel.words.strengthName(strengthButton.modelData)
                    Accessible.name: text
                    QQC2.ButtonGroup.group: strengthGroup
                    onClicked: if (panel.reading.strength !== strengthButton.modelData) {
                        SessionModel.setParam(panel.blockId, "strength", strengthButton.modelData);
                    }

                    SessionBound {
                        control: strengthButton
                        property: "checked"
                        value: panel.reading.strength === strengthButton.modelData
                    }
                }
            }
        }

        Item {
            Layout.fillWidth: true
        }
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
                : dot.word === "ok" ? AppColors.okMark : AppColors.liveMark
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

    // --- softened now ---
    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            Layout.fillWidth: true
            text: KI18n.i18nc("@label how much Sudden Sounds is taking off", "Softened now")
            color: AppColors.text
        }

        QQC2.Label {
            id: softenedText

            text: panel.words.softenedText(panel.reading.current, panel.reading.softenedDb)
            font: AppFonts.small
            color: AppColors.muted
            Accessible.role: Accessible.StaticText
            Accessible.name: softenedText.text
        }
    }

    LevelBar {
        id: softenedMeter

        Layout.fillWidth: true
        name: KI18n.i18nc("@label how much Sudden Sounds is taking off", "Softened now")
        from: 0
        to: panel.words.mostCut
        db: panel.reading.softenedDb
        running: panel.reading.current
        warnsAtTop: false
    }

    // The bar's numbers, in dB of change.
    Item {
        Layout.fillWidth: true
        Layout.topMargin: AppMetrics.pullIntoRow
        implicitHeight: markFont.implicitHeight

        QQC2.Label {
            id: markFont

            visible: false
            text: "0"
            font: AppFonts.small
        }

        Repeater {
            model: [0, 5, 10, 15]

            delegate: QQC2.Label {
                id: mark

                required property int modelData

                x: Math.max(0, Math.min(parent.width - width, parent.width * mark.modelData / panel.words.mostCut - width / 2))
                text: panel.words.markText(mark.modelData)
                font: AppFonts.small
                color: AppColors.muted
                Accessible.ignored: true
            }
        }
    }

    QQC2.Label {
        id: countText

        Layout.fillWidth: true
        visible: panel.reading.current
        text: panel.words.countText(panel.reading.count)
        color: AppColors.text
        wrapMode: Text.WordWrap
        Accessible.role: Accessible.StaticText
        Accessible.name: countText.text
    }

    InfoLine {
        id: musicLine

        text: KI18n.i18nc("@info where Sudden Sounds does harm", "Leave this off for music: it softens drums and plucked notes too")
    }

    QQC2.Label {
        id: placingText

        Layout.fillWidth: true
        text: KI18n.i18nc("@info where Sudden Sounds goes in a chain", "Put this before any block that moves or lifts sound. It delays the sound by less than a thousandth of a second")
        font: AppFonts.small
        color: AppColors.muted
        wrapMode: Text.WordWrap
    }
}
