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

// One number that goes down freely and up only on purpose: a limit, or a
// setting with a line past which it takes a second step.
//
// **Raising is harder than lowering.** The slider and the arrows stop, a
// typed value past the stop opens the dialog filled in, and the button opens
// it empty-handed, at the value. Going down works at once by every route.
//
// **The stop is the value itself**, unless the owner gives another: a limit
// does not go up by being dragged at all. What a command does is not stopped
// here: a script is a deliberate act, and the engine holds the range.
ColumnLayout {
    id: row

    required property string blockId
    required property string paramId
    required property string typeId
    /// The name a person reads.
    required property string label
    /// What is said at the stop: which control takes it further.
    required property string stopWords
    /// The sentence over the dialog's number.
    required property string question
    /// The unit beside the number, as words.
    required property string unit
    /// A number in this setting's unit, as words.
    required property var valueText

    readonly property var descriptor: BlockCatalogue.describeParam(row.typeId, row.paramId)
    readonly property real lowest: Number(row.descriptor.minimum ?? 0)
    readonly property real highest: Number(row.descriptor.maximum ?? 0)
    /// The value as the session has it.
    readonly property real value: SessionModel.paramRevision >= 0
        ? Number(SessionModel.param(row.blockId, row.paramId) ?? row.descriptor.default ?? 0)
        : 0
    /// A stop of the owner's, or NaN for the value itself.
    property real fixedStop: NaN
    /// Whether the button is there at every value, or only at the stop.
    property bool buttonAlways: true
    property string buttonText: KI18n.i18nc("@action:button raise a limit, which asks first", "Raise…")

    /// How low the drag going on has been: a limit lowered by a drag cannot
    /// come back up in the same drag.
    property real dragLow: Infinity

    readonly property real stop: Number.isNaN(row.fixedStop) ? Math.min(row.value, row.dragLow)
                                                             : Math.max(row.fixedStop, row.value)

    readonly property alias fader: bar
    readonly property alias field: typed
    readonly property alias button: raise
    readonly property alias dialog: ask
    readonly property alias dialogField: wanted
    readonly property alias confirm: confirmAction
    readonly property alias unitLabel: reads

    function sayStopped() {
        Announcer.say(row, "stop:" + row.blockId + ":" + row.paramId, row.stopWords);
    }

    function go(number: real, live: bool) {
        SessionModel.setParam(row.blockId, row.paramId, Math.max(row.lowest, Math.min(row.highest, number)), live);
    }

    function dragged(position: real) {
        if (position > row.stop) {
            bar.value = row.stop;
            row.sayStopped();
            return;
        }
        row.dragLow = Math.min(row.dragLow, position);
        row.go(position, true);
    }

    function stepBy(by: real) {
        const number = row.value + by;
        if (number > row.stop) {
            if (row.value < row.stop) {
                row.go(row.stop, false);
            }
            row.sayStopped();
            return;
        }
        row.go(number, false);
    }

    /// Opens the dialog at \a number.
    function askFor(number: real) {
        wanted.value = Math.max(row.value, Math.min(row.highest, number));
        ask.open();
    }

    function raiseTo(number: real) {
        ask.close();
        if (number > row.value) {
            row.go(number, false);
        }
    }

    spacing: Kirigami.Units.smallSpacing

    // **Two lines**, so that a narrow card keeps the number and the button.
    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            text: row.label
            color: AppColors.text
            elide: Text.ElideRight
            wrapMode: Text.Wrap
            maximumLineCount: 2
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
        }

        ValueField {
            id: typed

            Layout.preferredWidth: Kirigami.Units.gridUnit * 3.5
            Layout.alignment: Qt.AlignVCenter
            label: row.label
            value: row.value
            from: row.lowest
            to: row.highest
            stepTop: row.stop
            stepSize: 1
            pageStepSize: 3
            decimals: Number.isInteger(row.value) ? 0 : 1
            Accessible.description: row.valueText(row.value)

            onValueEdited: number => {
                if (number > row.stop) {
                    typed.showValue();
                    row.askFor(number);
                } else {
                    row.go(number, false);
                }
            }
            onStepRefused: row.sayStopped()
        }

        QQC2.Label {
            id: reads

            text: row.unit
            color: AppColors.muted
            font: AppFonts.mono
            Layout.alignment: Qt.AlignVCenter
            Accessible.ignored: true
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        QQC2.Slider {
            id: bar

            Layout.fillWidth: true
            Layout.minimumWidth: Kirigami.Units.gridUnit * 3
            from: row.lowest
            to: row.highest
            Accessible.name: row.label

            onMoved: row.dragged(value)
            onPressedChanged: {
                if (!pressed) {
                    row.dragLow = Infinity;
                    SessionModel.commitParam(row.blockId, row.paramId);
                }
            }

            Keys.onUpPressed: row.stepBy(1)
            Keys.onRightPressed: row.stepBy(1)
            Keys.onDownPressed: row.stepBy(-1)
            Keys.onLeftPressed: row.stepBy(-1)
            Keys.onPressed: event => {
                if (event.key === Qt.Key_PageUp) {
                    row.stepBy(3);
                    event.accepted = true;
                } else if (event.key === Qt.Key_PageDown) {
                    row.stepBy(-3);
                    event.accepted = true;
                }
            }

            SessionBound {
                control: bar
                property: "value"
                value: row.value
                suspended: bar.pressed
            }

            // A mark where the slider stops, when that is not the handle.
            Rectangle {
                visible: !Number.isNaN(row.fixedStop) && row.stop < row.highest
                x: bar.leftPadding + bar.availableWidth * (row.stop - row.lowest) / Math.max(1, row.highest - row.lowest)
                y: (bar.height - height) / 2
                width: 1
                height: Kirigami.Units.gridUnit * 0.8
                color: AppColors.muted
                Accessible.ignored: true
            }
        }

        QQC2.Button {
            id: raise

            visible: row.buttonAlways || row.value >= row.stop
            enabled: row.value < row.highest
            text: row.buttonText
            Accessible.description: row.label
            Layout.alignment: Qt.AlignVCenter
            onClicked: row.askFor(row.value)
        }
    }

    AppPromptDialog {
        id: ask

        parent: QQC2.Overlay.overlay
        title: row.label
        subtitle: row.question
        standardButtons: Kirigami.Dialog.Cancel
        customFooterActions: [
            Kirigami.Action {
                id: confirmAction

                text: KI18n.i18nc("@action:button set a limit higher", "Raise")
                onTriggered: row.raiseTo(wanted.value)
            }
        ]
        onOpened: {
            // **Cancel first**: the question is real, and a person who pressed
            // Enter by habit should not go further by it.
            const cancel = ask.standardButton(Kirigami.Dialog.Cancel);
            if (cancel) {
                cancel.forceActiveFocus();
            }
        }

        ValueField {
            id: wanted

            label: row.label
            from: row.value
            to: row.highest
            stepSize: 1
            pageStepSize: 3
            decimals: 1
            onValueEdited: number => wanted.value = number
            onAccepted: {
                wanted.commit();
                row.raiseTo(wanted.value);
            }
        }
    }
}
