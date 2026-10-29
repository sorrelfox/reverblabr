// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Pitch Shift's face and panel: the ranges are the engine's, the
// face says the whole shift with its sign, the sliders set what they say,
// and the numbers are written with their units.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "PitchShift"
    when: windowShown

    property string sessionId: ""

    function panelFor() {
        const panel = createTemporaryObject(BlockPanels.panelFor("effect.pitchshift"), testCase, { width: 340 });
        panel.blockId = "ps";
        return panel;
    }

    function faceFor() {
        const face = createTemporaryObject(BlockFaces.faceFor("effect.pitchshift"), testCase, { width: 180, height: 90 });
        face.blockId = "ps";
        return face;
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Pitch Shift");
        verify(testCase.sessionId !== "", "the mock opened a session");
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "ps", type: "effect.pitchshift" }, index: -1 }));
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        SessionModel.setParam("ps", "semitones", 0);
        SessionModel.setParam("ps", "cents", 0);
    }

    function test_theBlockHasAFaceAndAPanelOfItsOwn() {
        verify(BlockPanels.panelFor("effect.pitchshift") !== null, "Pitch Shift is in the panel register");
        verify(BlockFaces.faceFor("effect.pitchshift") !== null, "Pitch Shift is in the face register");
    }

    function test_theRangesAreTheEnginesOwn() {
        const semitones = BlockCatalogue.describeParam("effect.pitchshift", "semitones");
        const cents = BlockCatalogue.describeParam("effect.pitchshift", "cents");
        const panel = testCase.panelFor();
        const face = testCase.faceFor();
        compare(panel.widestSemitones, semitones.maximum);
        compare(-panel.widestSemitones, semitones.minimum);
        compare(panel.widestCents, cents.maximum);
        compare(-panel.widestCents, cents.minimum);
        compare(face.widestSemitones, semitones.maximum);
        compare(panel.semitonesSlider.to, semitones.maximum);
        compare(panel.centsSlider.from, cents.minimum);
        compare(face.slider.from, semitones.minimum);
    }

    function test_theFaceSaysTheWholeShift() {
        const face = testCase.faceFor();
        compare(face.shiftLabel.text, "0 st");
        compare(face.caption.text, "pitch");
        face.slider.value = 3;
        face.slider.moved();
        SessionModel.commitParam("ps", "semitones");
        compare(SessionModel.param("ps", "semitones"), 3);
        tryCompare(face.shiftLabel, "text", "+3 st");
        // The cents are in the number, though not on the slider.
        SessionModel.setParam("ps", "cents", 25);
        tryCompare(face.shiftLabel, "text", "+3.25 st");
        SessionModel.setParam("ps", "semitones", -5);
        SessionModel.setParam("ps", "cents", 40);
        tryCompare(face.shiftLabel, "text", "−4.60 st");
    }

    function test_thePanelSetsSemitonesAndCents() {
        const panel = testCase.panelFor();
        compare(panel.semitonesLabel.text, "0 st");
        compare(panel.centsLabel.text, "0 ct");
        panel.semitonesSlider.value = -7;
        panel.semitonesSlider.moved();
        SessionModel.commitParam("ps", "semitones");
        compare(SessionModel.param("ps", "semitones"), -7);
        compare(panel.semitonesLabel.text, "−7 st");
        panel.centsSlider.value = 50;
        panel.centsSlider.moved();
        SessionModel.commitParam("ps", "cents");
        compare(SessionModel.param("ps", "cents"), 50);
        compare(panel.centsLabel.text, "+50 ct");
        tryCompare(panel.shiftLabel, "text", "Moved by −6.50 st in all");
        SessionModel.setParam("ps", "semitones", 12);
        tryCompare(panel.semitonesSlider, "value", 12);
    }

    function test_theNumbersAreWrittenWithTheirSign() {
        const words = createTemporaryQmlObject("import io.github.reverblabr; PitchShiftWords {}", testCase);
        compare(words.semitones(3), "+3 st");
        compare(words.semitones(-12), "−12 st");
        compare(words.semitones(0), "0 st");
        compare(words.cents(-37.5), "−37.5 ct");
        compare(words.cents(100), "+100 ct");
        compare(words.shift(0.25), "+0.25 st");
    }
}
