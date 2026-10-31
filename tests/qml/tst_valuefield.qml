// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The typed number: it shows the owner's value, hands on what is typed inside
// the range, puts back what is no number, and follows arrows and pages.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "ValueField"
    when: windowShown
    width: 300
    height: 100
    visible: true

    Component {
        id: fieldComponent

        ValueField {
            property var edits: []

            label: "Ceiling"
            value: -6
            from: -60
            to: 0
            decimals: 1
            stepSize: 0.5
            pageStepSize: 6
            onValueEdited: value => edits.push(value)
        }
    }

    function fieldFor() {
        const field = createTemporaryObject(fieldComponent, testCase, { width: 120 });
        field.forceActiveFocus();
        return field;
    }

    function typeInto(field, number) {
        field.text = Number(number).toLocaleString(Qt.locale(), 'f', 2);
        keyClick(Qt.Key_Return);
    }

    function test_showsTheValueInTheLocale() {
        const field = fieldFor();
        compare(field.text, Number(-6).toLocaleString(Qt.locale(), 'f', 1));
        compare(field.Accessible.name, "Ceiling");
    }

    function test_typingHandsItOn() {
        const field = fieldFor();
        typeInto(field, -12.5);
        compare(field.edits, [-12.5]);
    }

    function test_keepsToTheRange() {
        const field = fieldFor();
        typeInto(field, -90);
        compare(field.edits, [-60]);
        compare(field.text, Number(-60).toLocaleString(Qt.locale(), 'f', 1));
    }

    function test_whatIsNoNumberPutsBackTheValue() {
        const field = fieldFor();
        field.text = "loud";
        keyClick(Qt.Key_Return);
        compare(field.edits, []);
        compare(field.text, Number(-6).toLocaleString(Qt.locale(), 'f', 1));
    }

    function test_arrowsAndPagesStep() {
        const field = fieldFor();
        keyClick(Qt.Key_Up);
        compare(field.edits, [-5.5]);
        keyClick(Qt.Key_PageDown);
        compare(field.edits, [-5.5, -12]);
    }

    function test_theOwnerHasTheLastWord() {
        const field = fieldFor();
        typeInto(field, -12);
        // The owner refused it: the value never moved. Focus leaving shows so.
        testCase.forceActiveFocus();
        tryVerify(() => !field.activeFocus);
        compare(field.text, Number(-6).toLocaleString(Qt.locale(), 'f', 1));
        compare(field.edits.length, 1, "leaving does not send the refused edit again");
        field.value = -3;
        compare(field.text, Number(-3).toLocaleString(Qt.locale(), 'f', 1));
    }

    function test_passingThroughChangesNothing() {
        const field = fieldFor();
        field.value = -1.25;
        testCase.forceActiveFocus();
        field.forceActiveFocus();
        testCase.forceActiveFocus();
        tryVerify(() => !field.activeFocus);
        compare(field.edits, []);
    }

    function test_anArrowStepsFromTheValueAndNotItsRounding() {
        const field = fieldFor();
        field.value = -1.25;
        keyClick(Qt.Key_Up);
        compare(field.edits, [-0.75]);
    }

    function test_aValueThatMovesWhileFocusedIsNotUndone() {
        const field = fieldFor();
        field.value = -2;
        compare(field.text, Number(-2).toLocaleString(Qt.locale(), 'f', 1));
        testCase.forceActiveFocus();
        tryVerify(() => !field.activeFocus);
        compare(field.edits, []);
    }

    function test_aValueThatMovesDoesNotTakeWhatIsBeingTyped() {
        const field = fieldFor();
        field.text = "-4";
        field.value = -2;
        compare(field.text, "-4");
    }
}
