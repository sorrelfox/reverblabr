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

// The Simple Compressor's settings: which mode, a line saying what it
// is for, and — folded away — the numbers the mode sets.
//
// **One choice, not six controls**, which is the block's whole point. A
// person who wants to set a threshold themselves has the Dynamics block;
// what is here is read-only, for somebody who wants to know what "Heavy"
// actually does before choosing it, or to copy it into a Dynamics block.
ColumnLayout {
    id: panel

    /// The block whose settings these are, set once the panel exists.
    property string blockId: ""

    readonly property string mode: SessionModel.revision >= 0
        ? (SessionModel.param(panel.blockId, "mode") ?? CompressorModes.defaultId)
        : CompressorModes.defaultId
    /// What the chosen mode sets, from the block.
    readonly property var settings: CompressorModes.settings(panel.mode)

    /// The controls, so that a test drives what a person drives.
    readonly property alias modeBox: modeControl
    readonly property alias explanationLine: explanation
    readonly property alias detailsSection: details
    readonly property alias detailRows: detailRows
    readonly property alias words: words

    spacing: AppMetrics.panelRowGap

    CompressorWords {
        id: words
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: AppMetrics.labelSpacing

        QQC2.Label {
            text: KI18n.i18nc("@label:listbox how firmly the Simple Compressor works", "Compressor mode")
            color: AppColors.muted
            horizontalAlignment: Text.AlignRight
            // The design's column is wider than the usual label's; the words
            // are not cut short to fit the usual one.
            Layout.preferredWidth: Math.max(AppMetrics.panelLabelWidth, implicitWidth)
        }

        QQC2.ComboBox {
            id: modeControl

            Layout.fillWidth: true
            model: words.modes
            displayText: words.modeName(panel.mode)
            Accessible.name: KI18n.i18nc("@label:listbox", "Compressor mode")

            delegate: QQC2.ItemDelegate {
                required property string modelData
                required property int index

                width: modeControl.width
                text: words.modeName(modelData)
                highlighted: modeControl.highlightedIndex === index
            }

            // A mode travels as its stable id, never as its position.
            onActivated: SessionModel.setParam(panel.blockId, "mode", String(words.modes[modeControl.currentIndex]))

            SessionBound {
                control: modeControl
                property: "currentIndex"
                value: Math.max(0, words.modes.indexOf(panel.mode))
            }
        }
    }

    InfoLine {
        id: explanation

        text: words.explanation(panel.mode)
    }

    // --- what the mode sets, to read and not to change ---
    CollapsibleSection {
        id: details

        title: KI18n.i18nc("@title:group the numbers a Simple Compressor mode sets", "Mode settings")

        Repeater {
            id: detailRows

            model: panel.mode === "off" || panel.settings.ratio === undefined ? [] : [
                {
                    name: KI18n.i18nc("@label the level compression starts at", "Threshold"),
                    value: words.decibelText(panel.settings.threshold)
                },
                {
                    name: KI18n.i18nc("@label how much of what is above the threshold comes out", "Ratio"),
                    value: words.ratioText(panel.settings.ratio)
                },
                {
                    name: KI18n.i18nc("@label how wide the bend around the threshold is", "Knee"),
                    value: words.decibelText(panel.settings.knee)
                },
                {
                    name: KI18n.i18nc("@label how quickly the compressor takes hold", "Attack"),
                    value: words.millisecondsText(panel.settings.attack)
                },
                {
                    name: KI18n.i18nc("@label how quickly the compressor lets go", "Release"),
                    value: words.millisecondsText(panel.settings.release)
                },
                {
                    name: KI18n.i18nc("@label the gain added after compressing", "Makeup gain"),
                    value: words.gainText(panel.settings.makeup)
                },
                {
                    name: KI18n.i18nc("@label what level the compressor follows", "Follows"),
                    value: words.detectorName(panel.settings.detector)
                }
            ]

            delegate: RowLayout {
                id: row

                required property var modelData

                Layout.fillWidth: true
                spacing: AppMetrics.labelSpacing
                Accessible.role: Accessible.StaticText
                Accessible.name: KI18n.i18nc("@info a setting and its value, 'Threshold: -18 dB'", "%1: %2",
                                             row.modelData.name, row.modelData.value)

                QQC2.Label {
                    text: row.modelData.name
                    color: AppColors.muted
                    // A translation goes onto a second line rather than under what is
                    // beside it.
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignRight
                    Layout.preferredWidth: AppMetrics.panelLabelWidth
                    Accessible.ignored: true
                }

                QQC2.Label {
                    text: row.modelData.value
                    color: AppColors.text
                    font: AppFonts.mono
                    Layout.fillWidth: true
                    Accessible.ignored: true
                }
            }
        }

        QQC2.Label {
            visible: panel.mode === "off"
            text: KI18n.i18nc("@info the Off mode sets nothing", "Off sets nothing: the sound is left as it is.")
            color: AppColors.muted
            font: AppFonts.small
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }
}
