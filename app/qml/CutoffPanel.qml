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

// A cutoff filter's settings — the Low Pass Filter and the High Pass
// Filter: the cutoff and the slope.
//
// **A panel of its own rather than the generic one**, because a cutoff
// wants a logarithmic slider with a box beside it to type an exact one
// into — "8k" for hiss, the number a recording's notes give — and the slope
// is two words, not the engine's ids.
ColumnLayout {
    id: panel

    /// The block whose settings these are, set once the panel exists.
    property string blockId: ""

    readonly property var params: SessionModel.revision >= 0 ? SessionModel.params(panel.blockId) : ({})
    readonly property real cutoff: panel.params.cutoff ?? panel.defaultHz
    readonly property string slope: panel.params.slope ?? "12dB"

    /// The block's type: "effect.lowpass" or "effect.highpass".
    property string typeId: ""

    /// The cutoff's range and where it starts, **from the engine's own
    /// description** rather than written down here again: the two filters
    /// differ in nothing else the interface needs to know.
    readonly property var described: BlockCatalogue.ready && panel.typeId !== ""
        ? BlockCatalogue.describeParam(panel.typeId, "cutoff") : ({})
    readonly property real lowestHz: panel.described.minimum ?? 20
    readonly property real highestHz: panel.described.maximum ?? 20000
    readonly property real defaultHz: panel.described.default ?? panel.highestHz

    /// The controls, so that a test drives what a person drives.
    readonly property alias cutoffSlider: cutoffControl
    readonly property alias cutoffField: cutoffText
    readonly property alias slopeButtons: slopeFrame.row
    readonly property alias words: words

    spacing: Kirigami.Units.largeSpacing

    FilterWords {
        id: words
    }

    Kirigami.FormLayout {
        Layout.fillWidth: true

        RowLayout {
            Kirigami.FormData.label: KI18n.i18nc("@label a cutoff filter's cutoff frequency", "Cutoff:")
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            QQC2.Slider {
                id: cutoffControl

                Layout.fillWidth: true
                from: 0
                to: 1
                Accessible.name: KI18n.i18nc("@label:slider a cutoff filter's cutoff frequency", "Cutoff")

                onMoved: SessionModel.setParam(panel.blockId, "cutoff",
                                               words.frequencyAt(value, panel.lowestHz, panel.highestHz), true)
                onPressedChanged: if (!pressed) {
                    SessionModel.commitParam(panel.blockId, "cutoff");
                }

                SessionBound {
                    control: cutoffControl
                    property: "value"
                    value: words.positionOf(panel.cutoff, panel.lowestHz, panel.highestHz)
                    suspended: cutoffControl.pressed
                }
            }

            // **An exact number**, typed, and while the slider moves the
            // number it is at.
            QQC2.TextField {
                id: cutoffText

                Layout.preferredWidth: Kirigami.Units.gridUnit * 5
                horizontalAlignment: Text.AlignRight
                font.family: AppFonts.monoFamily
                Accessible.name: KI18n.i18nc("@label:textbox a cutoff frequency to type", "Cutoff in hertz")

                onEditingFinished: {
                    const hz = words.parseFrequency(text);
                    if (!isNaN(hz)) {
                        SessionModel.setParam(panel.blockId, "cutoff",
                                              Math.max(panel.lowestHz, Math.min(panel.highestHz, hz)));
                    }
                    // Whatever was typed, the box goes back to saying what
                    // the block is doing.
                    text = words.frequencyText(panel.cutoff);
                }

                SessionBound {
                    control: cutoffText
                    property: "text"
                    value: words.frequencyText(cutoffControl.pressed
                                               ? words.frequencyAt(cutoffControl.value, panel.lowestHz, panel.highestHz)
                                               : panel.cutoff)
                    suspended: cutoffText.activeFocus
                }
            }
        }

        // Gentle, then steep: the order the numbers go up in.
        SegmentedFrame {
            id: slopeFrame

            Kirigami.FormData.label: KI18n.i18nc("@label how steeply a filter rolls off", "Slope:")

            QQC2.ButtonGroup {
                id: slopeGroup
            }

            Repeater {
                model: words.slopes

                delegate: SegmentedChoice {
                    required property string modelData

                    text: words.slopeName(modelData)
                    checked: panel.slope === modelData
                    Accessible.name: words.slopeDescription(modelData)
                    QQC2.ButtonGroup.group: slopeGroup
                    onClicked: SessionModel.setParam(panel.blockId, "slope", modelData)
                }
            }
        }
    }
}
