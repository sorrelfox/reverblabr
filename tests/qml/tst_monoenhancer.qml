// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Mono Enhancer's face and panel offscreen, against the mock
// engine: the crossover said in hertz, a logarithmic slider over the range
// the engine takes, and a whole number of hertz written.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "MonoEnhancer"
    when: windowShown

    property string sessionId: ""

    function panelFor() {
        const panel = createTemporaryObject(BlockPanels.panelFor("effect.monoenhancer"), testCase, { width: 320 });
        panel.blockId = "me";
        return panel;
    }

    function faceFor() {
        const face = createTemporaryObject(BlockFaces.faceFor("effect.monoenhancer"), testCase,
                                           { width: 220, height: 120 });
        face.blockId = "me";
        return face;
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Mono Enhancer");
        verify(testCase.sessionId !== "", "the mock opened a session");
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock",
                                              block: { id: "me", type: "effect.monoenhancer" },
                                              index: -1 }));
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        SessionModel.setParam("me", "crossover", 500);
        SessionModel.setBlockEnabled("me", true);
    }

    function test_theBlockHasAFaceAndAPanelOfItsOwn() {
        verify(BlockPanels.panelFor("effect.monoenhancer") !== null, "the Mono Enhancer is in the panel register");
        verify(BlockFaces.faceFor("effect.monoenhancer") !== null, "the Mono Enhancer is in the face register");
    }

    function test_theRangeIsTheOneTheEngineTakes() {
        // The ends are written down on both sides of the layer boundary;
        // this keeps them together.
        const descriptor = BlockCatalogue.describeParam("effect.monoenhancer", "crossover");
        const panel = testCase.panelFor();
        const face = testCase.faceFor();
        compare(panel.lowestHz, descriptor.minimum);
        compare(panel.highestHz, descriptor.maximum);
        compare(panel.defaultHz, descriptor.default);
        compare(face.lowestHz, descriptor.minimum);
        compare(face.highestHz, descriptor.maximum);
        compare(face.defaultHz, descriptor.default);
        compare(descriptor.taper, "log");
    }

    function test_theCrossoverIsSaidInHertz_data() {
        return [
            { tag: "the default", hz: 500, text: "500 Hz" },
            { tag: "the bottom", hz: 100, text: "100 Hz" },
            { tag: "kilohertz", hz: 1250, text: "1.25 kHz" },
            { tag: "the top", hz: 2000, text: "2.00 kHz" },
        ];
    }

    function test_theCrossoverIsSaidInHertz(data) {
        const panel = testCase.panelFor();
        const face = testCase.faceFor();
        SessionModel.setParam("me", "crossover", data.hz);
        tryCompare(face.headlineLabel, "text", data.text);
        compare(panel.valueLabel.text, data.text);
        compare(face.captionLabel.text, "crossover");
    }

    function test_theSliderIsLogarithmic() {
        // An octave is the same distance wherever it is: 100 → 200 Hz takes
        // as much of the slider as 1 → 2 kHz, and the middle is the
        // geometric mean, 447 Hz.
        const panel = testCase.panelFor();
        panel.slider.value = 0.5;
        panel.slider.moved();
        SessionModel.commitParam("me", "crossover");
        compare(SessionModel.param("me", "crossover"), 447);
        compare(panel.valueLabel.text, "447 Hz");
    }

    function test_theFaceWritesAWholeNumberOfHertz() {
        const face = testCase.faceFor();
        face.slider.value = 0.25;
        face.slider.moved();
        SessionModel.commitParam("me", "crossover");
        const written = SessionModel.param("me", "crossover");
        compare(written, Math.round(written), "a fraction of a hertz was written");
        compare(written, 211);
    }

    function test_thePanelSaysWhatTheBlockIsFor() {
        const panel = testCase.panelFor();
        compare(panel.titleLabel.text, "Crossover frequency");
        verify(panel.explanationLine.text.indexOf("mono") >= 0, "the line does not say it is for mono");
    }
}
