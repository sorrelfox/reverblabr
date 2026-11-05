// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.


// The Declick block's face and panel offscreen, against the mock
// engine: the threshold a person sets is the number the block gets, both
// say it in decibels, and while the session runs the panel says how many
// clicks were taken out. That clicks are found and mended, and clean sound
// left alone, is measured in blocks.DeclickTest.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "Declick"
    when: windowShown

    property string sessionId: ""

    function panelFor() {
        const panel = createTemporaryObject(BlockPanels.panelFor("advanced.declick"), testCase, { width: 340 });
        panel.blockId = "dc";
        return panel;
    }

    function faceFor() {
        const face = createTemporaryObject(BlockFaces.faceFor("advanced.declick"), testCase, { width: 180, height: 90 });
        face.blockId = "dc";
        return face;
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Declick");
        verify(testCase.sessionId !== "", "the mock opened a session");
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "dc", type: "advanced.declick" }, index: -1 }));
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        SessionModel.setParam("dc", "threshold", 3);
    }

    function cleanup() {
        TestEngine.stop(testCase.sessionId);
        Meters.watch("");
    }

    function test_theBlockHasAFaceAndAPanelOfItsOwn() {
        verify(BlockPanels.panelFor("advanced.declick") !== null, "Declick is in the panel register");
        verify(BlockFaces.faceFor("advanced.declick") !== null, "Declick is in the face register");
    }

    function test_theRangeIsTheEnginesOwn() {
        const threshold = BlockCatalogue.describeParam("advanced.declick", "threshold");
        const panel = testCase.panelFor();
        const face = testCase.faceFor();
        compare(panel.highestThreshold, threshold.maximum);
        compare(panel.defaultThreshold, threshold.default);
        compare(face.highestThreshold, threshold.maximum);
        compare(face.defaultThreshold, threshold.default);
        compare(panel.slider.to, threshold.maximum);
    }

    function test_theFaceSaysTheThreshold() {
        const face = testCase.faceFor();
        compare(face.thresholdLabel.text, "3.0 dB");
        compare(face.caption.text, "threshold");
        face.slider.value = 12.5;
        face.slider.moved();
        SessionModel.commitParam("dc", "threshold");
        compare(SessionModel.param("dc", "threshold"), 12.5);
        tryCompare(face.thresholdLabel, "text", "12.5 dB");
    }

    function test_thePanelSetsTheThreshold() {
        const panel = testCase.panelFor();
        compare(panel.valueLabel.text, "3.0 dB");
        panel.slider.value = 0;
        panel.slider.moved();
        SessionModel.commitParam("dc", "threshold");
        compare(SessionModel.param("dc", "threshold"), 0);
        compare(panel.valueLabel.text, "0.0 dB");
        SessionModel.setParam("dc", "threshold", 20);
        tryCompare(panel.slider, "value", 20);
    }

    function test_runningThePanelCountsTheClicks() {
        const panel = testCase.panelFor();
        verify(!panel.live, "a count is shown with nothing running");
        compare(panel.clicksLabel.text, "");
        Meters.watch(testCase.sessionId);
        TestEngine.start(testCase.sessionId);
        tryVerify(() => panel.live, 1000, "the count never showed while running");
        compare(panel.clicksLabel.text, "No clicks taken out yet");
    }

    function test_theCountIsSaidInWords() {
        const words = createTemporaryQmlObject("import io.github.reverblabr; DeclickWords {}", testCase);
        compare(words.clicks(1), "1 click taken out");
        compare(words.clicks(42), "42 clicks taken out");
    }
}
