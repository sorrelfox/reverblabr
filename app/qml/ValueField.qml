// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2

// A number with a fraction, typed: what a SpinBox is for a whole one.
//
// **It reads and writes in the person's own locale**, and the value it hands
// on is a plain number. It keeps to the range, and what cannot be read as a
// number puts back what was there.
//
// **It holds no value of its own.** `value` is bound by the owner and only
// `valueEdited` goes out, so the session has the last word: an edit it clamps
// or refuses shows as soon as the field stops being typed in. Nothing is sent
// on a keystroke, since a number half typed is not a number.
//
// **Only typing is an edit.** The text is the value rounded, so reading it back
// would change a setting for anyone who merely passed through the field. What
// the field wrote itself is remembered, and nothing goes out until the text
// differs from it.
QQC2.TextField {
    id: field

    /// What the owner says the value is.
    property real value: 0
    property real from: 0
    property real to: 1
    /// How many places after the point are shown.
    property int decimals: 1
    /// What an arrow key moves it by, and what Page Up and Page Down do.
    property real stepSize: 1
    property real pageStepSize: 10
    /// How far the arrows go. **Apart from `to`**, for an owner that lets a
    /// value be typed higher than it can be stepped to and asks about it.
    property real stepTop: field.to
    /// What it says at `from`, when that value means something other than
    /// a number. Passing through sends nothing, as for any text it wrote.
    property string lowestText: ""
    /// What a screen reader calls it. The owner's words.
    required property string label

    /// The text as the field last wrote it. Anything else is somebody's edit.
    property string writtenText: ""

    /// Somebody typed or stepped to \a value, already inside the range.
    signal valueEdited(real value)
    /// An arrow was pressed at `stepTop`, and nothing moved.
    signal stepRefused

    function shown(number) {
        if (field.lowestText !== "" && number <= field.from) {
            return field.lowestText;
        }
        return Number(number).toLocaleString(Qt.locale(), 'f', field.decimals);
    }

    function write(number) {
        field.writtenText = field.shown(number);
        field.text = field.writtenText;
    }

    function showValue() {
        field.write(field.value);
    }

    function hasUnsentEdit() {
        return field.text !== field.writtenText;
    }

    function offer(number) {
        const kept = Math.min(field.to, Math.max(field.from, number));
        field.write(kept);
        if (kept !== field.value) {
            field.valueEdited(kept);
        }
    }

    /// What the field shows now as a number, or NaN.
    function typed() {
        try {
            return Number.fromLocaleString(Qt.locale(), field.text.trim());
        } catch (error) {
            return NaN;
        }
    }

    function commit() {
        if (!field.hasUnsentEdit()) {
            return;
        }
        const number = field.typed();
        if (Number.isFinite(number)) {
            field.offer(number);
        } else {
            field.showValue();
        }
    }

    /// One edit, from what was typed if anything was, else from the value
    /// itself and not its rounding.
    function step(by) {
        const number = field.hasUnsentEdit() ? field.typed() : field.value;
        const wanted = (Number.isFinite(number) ? number : field.value) + by;
        if (by > 0 && wanted > field.stepTop && number >= field.stepTop) {
            field.stepRefused();
        }
        field.offer(Math.min(wanted, Math.max(field.stepTop, number)));
    }

    horizontalAlignment: TextInput.AlignRight
    inputMethodHints: Qt.ImhFormattedNumbersOnly
    font: AppFonts.mono
    Accessible.name: field.label

    onEditingFinished: field.commit()
    onValueChanged: {
        if (!field.activeFocus || !field.hasUnsentEdit()) {
            field.showValue();
        }
    }
    onDecimalsChanged: field.showValue()
    onActiveFocusChanged: {
        if (!field.activeFocus) {
            field.showValue();
        }
    }
    Component.onCompleted: field.showValue()

    Keys.onUpPressed: field.step(field.stepSize)
    Keys.onDownPressed: field.step(-field.stepSize)
    Keys.onPressed: event => {
        if (event.key === Qt.Key_PageUp) {
            field.step(field.pageStepSize);
            event.accepted = true;
        } else if (event.key === Qt.Key_PageDown) {
            field.step(-field.pageStepSize);
            event.accepted = true;
        } else if (event.key === Qt.Key_Escape && field.hasUnsentEdit()) {
            // Left for a dialog round the field when there is nothing to
            // take back.
            field.showValue();
            event.accepted = true;
        }
    }
}
