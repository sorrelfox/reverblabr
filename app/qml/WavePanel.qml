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

// The Wave block's settings: what it makes, how high, how loud and
// on which side.
//
// **A panel of its own rather than the generic one**, because the generic
// one writes the engine's ids — "saw", "both" — where a person reads words,
// and a frequency wants a logarithmic slider and a box to type an exact one
// into, both of which a test signal needs.
//
// **The frequency is for tones**, so it is greyed out for noise rather than
// hidden: the setting is kept for when the block goes back to a tone.
ColumnLayout {
    id: panel

    /// The block whose settings these are, set once the panel exists.
    property string blockId: ""

    readonly property var params: SessionModel.paramRevision >= 0 ? SessionModel.params(panel.blockId) : ({})
    readonly property string shape: panel.params.waveform ?? "sine"
    readonly property real frequency: panel.params.frequency ?? 1000
    readonly property real levelDb: panel.params.level ?? -18
    readonly property string side: panel.params.channels ?? "both"

    readonly property real lowestHz: 20
    readonly property real highestHz: 20000

    /// The controls, so that a test drives what a person drives.
    readonly property alias shapeBox: shapeControl
    readonly property alias frequencySlider: frequencyControl
    readonly property alias frequencyField: frequencyText
    readonly property alias levelSlider: levelControl
    readonly property alias sideButtons: sideFrame.row
    readonly property alias words: words

    spacing: Kirigami.Units.largeSpacing

    WaveWords {
        id: words
    }

    /// Where a frequency sits on the slider: logarithmic, as a frequency is
    /// heard, so an octave is the same distance wherever it is.
    function positionOf(hz: real): real {
        return Math.log(hz / panel.lowestHz) / Math.log(panel.highestHz / panel.lowestHz);
    }

    function frequencyAt(position: real): real {
        return panel.lowestHz * Math.pow(panel.highestHz / panel.lowestHz, position);
    }

    /// Reads what was typed as a frequency in hertz, or NaN. "1k" and "1.5
    /// kHz" are kilohertz, which is how a person says a test tone.
    function parseFrequency(typed: string): real {
        const text = typed.trim().toLowerCase().replace(",", ".");
        const kilo = /k/.test(text);
        const number = parseFloat(text);
        if (isNaN(number)) {
            return NaN;
        }
        return kilo ? number * 1000 : number;
    }

    Kirigami.FormLayout {
        Layout.fillWidth: true

        QQC2.ComboBox {
            id: shapeControl

            Kirigami.FormData.label: KI18n.i18nc("@label:listbox the shape of a Wave block's signal", "Waveform:")
            Layout.fillWidth: true
            model: words.shapes
            displayText: words.shapeName(panel.shape)
            // Named as the sliders below it are: the form only links its
            // label to the list, and the list's own name is what the help
            // page's check, and a screen reader that does not follow the
            // link, read.
            Accessible.name: KI18n.i18nc("@label:listbox", "Waveform")

            delegate: QQC2.ItemDelegate {
                required property string modelData
                required property int index

                width: shapeControl.width
                text: words.shapeName(modelData)
                highlighted: shapeControl.highlightedIndex === index
            }

            // An enum travels as its choice's stable id, never as its
            // position: a later version may reorder the choices.
            onActivated: SessionModel.setParam(panel.blockId, "waveform", String(words.shapes[shapeControl.currentIndex]))

            SessionBound {
                control: shapeControl
                property: "currentIndex"
                value: Math.max(0, words.shapes.indexOf(panel.shape))
            }
        }

        RowLayout {
            Kirigami.FormData.label: KI18n.i18nc("@label a Wave block's tone frequency", "Frequency:")
            Layout.fillWidth: true
            enabled: words.isTone(panel.shape)
            spacing: Kirigami.Units.smallSpacing

            QQC2.Slider {
                id: frequencyControl

                Layout.fillWidth: true
                from: 0
                to: 1
                Accessible.name: KI18n.i18nc("@label:slider", "Frequency")

                onMoved: SessionModel.setParam(panel.blockId, "frequency", panel.frequencyAt(value), true)
                onPressedChanged: if (!pressed) {
                    SessionModel.commitParam(panel.blockId, "frequency");
                }

                SessionBound {
                    control: frequencyControl
                    property: "value"
                    value: panel.positionOf(panel.frequency)
                    suspended: frequencyControl.pressed
                }
            }

            // **An exact number**, typed: a test tone is often a particular
            // one — 1 kHz, 440 Hz, the frequency a speaker's sheet names.
            QQC2.TextField {
                id: frequencyText

                // Room for a longer unit in another language.
                Layout.preferredWidth: Math.max(Kirigami.Units.gridUnit * 5, implicitWidth)
                horizontalAlignment: Text.AlignRight
                font.family: AppFonts.monoFamily
                Accessible.name: KI18n.i18nc("@label:textbox a frequency to type", "Frequency in hertz")

                onEditingFinished: {
                    const hz = panel.parseFrequency(text);
                    if (!isNaN(hz)) {
                        SessionModel.setParam(panel.blockId, "frequency",
                                              Math.max(panel.lowestHz, Math.min(panel.highestHz, hz)));
                    }
                    // Whatever was typed, the box goes back to saying what
                    // the block is doing.
                    text = words.frequencyText(panel.frequency);
                }

                SessionBound {
                    control: frequencyText
                    property: "text"
                    value: words.frequencyText(panel.frequency)
                    suspended: frequencyText.activeFocus
                }
            }
        }

        RowLayout {
            Kirigami.FormData.label: KI18n.i18nc("@label how loud a Wave block's signal is", "Level:")
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            QQC2.Slider {
                id: levelControl

                Layout.fillWidth: true
                from: -80
                to: 0
                Accessible.name: KI18n.i18nc("@label:slider how loud a Wave block's signal is", "Level")

                onMoved: SessionModel.setParam(panel.blockId, "level", value, true)
                onPressedChanged: if (!pressed) {
                    SessionModel.commitParam(panel.blockId, "level");
                }

                SessionBound {
                    control: levelControl
                    property: "value"
                    value: panel.levelDb
                    suspended: levelControl.pressed
                }
            }

            QQC2.Label {
                // **Loud is said in the warning colour.** A tone near full
                // scale is how a tweeter or an ear gets hurt by a slip.
                text: words.levelText(levelControl.value)
                font.family: AppFonts.monoFamily
                color: levelControl.value > -6 ? AppColors.warningText : AppColors.text
                horizontalAlignment: Text.AlignRight
                // Room for a longer unit in another language.
                Layout.preferredWidth: Math.max(Kirigami.Units.gridUnit * 4, implicitWidth)
            }
        }

        // Left, both, right: the order a person reads across a desk.
        SegmentedFrame {
            id: sideFrame

            Kirigami.FormData.label: KI18n.i18nc("@label which channels a Wave block's signal is on", "Channels:")

            QQC2.ButtonGroup {
                id: sideGroup
            }

            Repeater {
                model: words.sides

                delegate: SegmentedChoice {
                    required property string modelData

                    text: words.sideName(modelData)
                    checked: panel.side === modelData
                    QQC2.ButtonGroup.group: sideGroup
                    onClicked: SessionModel.setParam(panel.blockId, "channels", modelData)
                }
            }
        }
    }
}
