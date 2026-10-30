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

// One parameter: what it is called, the control for it, and what it says.
//
// **The control is chosen from the descriptor**, never from the block: the
// type says what kind of thing it is, the unit says how to read it, and the
// taper says how a slider should be spread across the range. A block that
// describes itself needs no interface code of its own.
//
// **A read-only parameter is a readout.** It is something the block says
// about itself — a gain reduction, a stream count — not a setting, and
// offering a control for it would be a lie.
RowLayout {
    id: row

    /// The block this parameter belongs to.
    required property string blockId
    /// The descriptor, as the catalogue gives it.
    required property var descriptor
    /// What it is at the moment. `undefined` means the session does not
    /// mention it, which means the default.
    required property var currentValue

    readonly property string paramId: descriptor.id ?? ""
    readonly property string paramType: descriptor.type ?? ""
    readonly property string unit: descriptor.unit ?? "none"
    readonly property bool readOnly: descriptor.readOnly === true
    readonly property real minimum: descriptor.minimum ?? 0
    readonly property real maximum: descriptor.maximum ?? 1
    readonly property bool logarithmic: (descriptor.taper ?? "linear") === "log" && row.minimum > 0
    /// Whether the value is a number, and so is written in the reader's
    /// locale with its unit after it.
    readonly property bool numeric: row.paramType === "float" || row.paramType === "int"

    /// The control this row put up for the parameter, whichever kind it
    /// turned out to be. A test asks it what it is showing; nothing in the
    /// interface reaches through it.
    readonly property alias activeControl: control.item

    /// What the value is now, falling back to the descriptor's default.
    readonly property var effectiveValue: currentValue === undefined ? descriptor.default : currentValue

    /// The locale a number is written in. The user's, unless a test says
    /// otherwise — which is how "a decimal comma in de_DE" is checked on a
    /// machine that has no German locale installed.

    /// The name a person reads. **Not from the engine**: the catalogue holds
    /// ids and the words are the interface's, translated and keyed by them.
    /// A panel that has words for its own parameters — a block's own panel
    /// does — sets this; until the catalogue content task writes the table
    /// for the rest, the id is shown, which is honest and tells a translator
    /// what is missing.
    property string label: row.paramId

    property var locale: Qt.locale()

    spacing: Kirigami.Units.largeSpacing

    // **The same words as anywhere else the value appears.** A tile's face
    // and this row show one parameter, and two tables of units would be two
    // readings of it (ParamFormat).
    function unitSuffix(): string {
        return ParamFormat.unitSuffix(row.unit);
    }

    function formatted(value: real): string {
        return ParamFormat.formatted(value, row.paramType, row.unit, row.locale);
    }

    // A logarithmic taper spreads equal multiples over equal distances, so
    // that 2:1 sits as far from 4:1 as 4:1 does from 8:1.
    function toPosition(value: real): real {
        if (!row.logarithmic) {
            return value;
        }
        const lo = Math.log(row.minimum);
        return (Math.log(Math.max(value, row.minimum)) - lo) / (Math.log(row.maximum) - lo);
    }

    function fromPosition(position: real): real {
        if (!row.logarithmic) {
            return position;
        }
        const lo = Math.log(row.minimum);
        return Math.exp(lo + position * (Math.log(row.maximum) - lo));
    }

    QQC2.Label {
        text: row.label
        color: AppColors.text
        elide: Text.ElideRight
        // A label column is fixed; a longer word goes onto a second line
        // rather than out of sight.
        wrapMode: Text.Wrap
        maximumLineCount: 2
        Layout.preferredWidth: Kirigami.Units.gridUnit * 7
        Layout.alignment: Qt.AlignVCenter
    }

    Loader {
        id: control

        Layout.fillWidth: true
        Layout.alignment: Qt.AlignVCenter
        sourceComponent: {
            if (row.readOnly) {
                return readout;
            }
            switch (row.paramType) {
            case "bool": return toggle;
            case "enum": return choice;
            case "int": return counter;
            case "float": return slider;
            case "string":
            case "deviceRef": return line;
            case "stringList": return listSummary;
            }
            return readout;
        }
    }

    Component {
        id: readout

        QQC2.Label {
            text: row.numeric ? row.formatted(Number(row.effectiveValue)) : String(row.effectiveValue ?? "")
            color: AppColors.muted
            font: AppFonts.mono
            horizontalAlignment: Text.AlignRight
            Accessible.name: row.label
            Accessible.description: text
        }
    }

    Component {
        id: toggle

        QQC2.Switch {
            id: toggleControl

            Accessible.name: row.label
            onToggled: SessionModel.setParam(row.blockId, row.paramId, toggleControl.checked)

            SessionBound {
                control: toggleControl
                property: "checked"
                value: row.effectiveValue === true
            }
        }
    }

    Component {
        id: choice

        QQC2.ComboBox {
            id: choiceControl

            // **A plugin's choices come with the plugin's words**; a
            // built-in block's are ChoiceWords', keyed by their ids.
            model: row.descriptor.choiceLabels ?? ChoiceWords.labels(SessionModel.blockType(row.blockId), row.descriptor)
            Accessible.name: row.label
            onActivated: SessionModel.setParam(row.blockId, row.paramId,
                                               String((row.descriptor.choices ?? [])[choiceControl.currentIndex]))

            SessionBound {
                control: choiceControl
                property: "currentIndex"
                value: Math.max(0, (row.descriptor.choices ?? []).indexOf(String(row.effectiveValue)))
            }
        }
    }

    Component {
        id: counter

        QQC2.SpinBox {
            id: counterControl

            from: Math.round(row.minimum)
            to: Math.round(row.maximum)
            editable: true
            Accessible.name: row.label
            onValueModified: SessionModel.setParam(row.blockId, row.paramId, counterControl.value)

            SessionBound {
                control: counterControl
                property: "value"
                value: Math.round(Number(row.effectiveValue))
            }
        }
    }

    Component {
        id: line

        QQC2.TextField {
            id: lineControl

            Accessible.name: row.label
            // On editing finished, not on every keystroke: a name half
            // typed is not a name, and every letter would be an edit.
            onEditingFinished: SessionModel.setParam(row.blockId, row.paramId, lineControl.text)

            SessionBound {
                control: lineControl
                property: "text"
                value: String(row.effectiveValue ?? "")
                // Left alone while somebody is typing: a field that rewrote
                // itself under the cursor could not be typed into.
                suspended: lineControl.activeFocus
            }
        }
    }

    Component {
        id: listSummary

        // **A list is shown, not edited, here.** The lists a block carries
        // are sinks and applications, and choosing one means the picker the
        // block's own panel has; a comma-separated text field would make a
        // name with a comma in it impossible to type.
        QQC2.Label {
            readonly property var entries: row.effectiveValue ?? []

            text: entries.length === 0
                ? KI18n.i18nc("@info:placeholder an empty list of names", "None")
                : entries.join(KI18n.i18nc("@item:intext separator between names in a list", ", "))
            color: AppColors.muted
            elide: Text.ElideRight
            Accessible.name: row.label
            Accessible.description: text
        }
    }

    Component {
        id: slider

        RowLayout {
            /// The slider itself, for a test that asks where the handle is.
            readonly property alias fader: bar

            spacing: Kirigami.Units.smallSpacing

            QQC2.Slider {
                id: bar

                Layout.fillWidth: true
                from: row.logarithmic ? 0 : row.minimum
                to: row.logarithmic ? 1 : row.maximum
                Accessible.name: row.label

                // **While it moves, the value is live**: coalesced and sent
                // sixty times a second, which is what the parameter can take
                // and what a smoothed one will glide to.
                onMoved: SessionModel.setParam(row.blockId, row.paramId, row.fromPosition(value), true)
                // **And the last one always arrives**, whatever the timer
                // was doing when the button came up.
                onPressedChanged: if (!pressed) {
                    SessionModel.commitParam(row.blockId, row.paramId);
                }

                SessionBound {
                    control: bar
                    property: "value"
                    value: row.toPosition(Number(row.effectiveValue))
                    // A drag is the one time the pointer, not the session,
                    // says where the handle is.
                    suspended: bar.pressed
                }
            }

            QQC2.Label {
                text: row.formatted(row.fromPosition(bar.value))
                color: AppColors.muted
                font: AppFonts.mono
                horizontalAlignment: Text.AlignRight
                // Room for a longer unit in another language.
                Layout.preferredWidth: Math.max(Kirigami.Units.gridUnit * 4, implicitWidth)
            }
        }
    }
}
