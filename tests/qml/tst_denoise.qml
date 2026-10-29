// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Denoise block's face and panel offscreen, against the mock
// engine: what they say with no noise, a saved noise and a noise learned in
// this run; the Learn and Forget button; and the two settings.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "Denoise"
    when: windowShown

    property string sessionId: ""

    /// A saved profile's text, as the block writes one: only its header is
    /// read here.
    readonly property string savedProfile: "v1;rate=48000;size=2048;seconds=3.0;db=" + new Array(1025).fill("-60.0").join(",")

    function panelFor() {
        const panel = createTemporaryObject(BlockPanels.panelFor("advanced.denoise"), testCase, { width: 340 });
        panel.blockId = "dn";
        return panel;
    }

    function faceFor() {
        const face = createTemporaryObject(BlockFaces.faceFor("advanced.denoise"), testCase, { width: 180, height: 90 });
        face.blockId = "dn";
        return face;
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Denoise");
        verify(testCase.sessionId !== "", "the mock opened a session");
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "dn", type: "advanced.denoise" }, index: -1 }));
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        SessionModel.setParam("dn", "profile", "");
        SessionModel.setParam("dn", "learn", 0);
        SessionModel.setParam("dn", "forget", 0);
        SessionModel.setParam("dn", "reduction", 20);
        SessionModel.setParam("dn", "threshold", 3);
    }

    function cleanup() {
        TestEngine.clearFakeLevels(testCase.sessionId);
        TestEngine.stop(testCase.sessionId);
        Meters.watch("");
    }

    function run() {
        Meters.watch(testCase.sessionId);
        TestEngine.start(testCase.sessionId);
        tryVerify(() => Meters.live, 1000, "the session never went live");
    }

    function test_theBlockHasAFaceAndAPanelOfItsOwn() {
        verify(BlockPanels.panelFor("advanced.denoise") !== null, "Denoise is in the panel register");
        verify(BlockFaces.faceFor("advanced.denoise") !== null, "Denoise is in the face register");
    }

    function test_theRangesAreTheEnginesOwn() {
        const panel = testCase.panelFor();
        const reduction = BlockCatalogue.describeParam("advanced.denoise", "reduction");
        const threshold = BlockCatalogue.describeParam("advanced.denoise", "threshold");
        compare(panel.highestReduction, reduction.maximum);
        compare(panel.defaultReduction, reduction.default);
        compare(panel.highestThreshold, threshold.maximum);
        compare(panel.defaultThreshold, threshold.default);
        compare(panel.reductionSlider.to, reduction.maximum);
    }

    function test_withNothingLearnedItSaysSo() {
        const face = testCase.faceFor();
        const panel = testCase.panelFor();
        compare(face.headlineLabel.text, "Inactive");
        compare(face.captionLabel.text, "No noise learned");
        compare(panel.actionButton.text, "Learn Noise");
        verify(!panel.actionButton.enabled, "Learn can be pressed with nothing running to listen to");
    }

    function test_aSavedNoiseIsActiveWithItsSeconds() {
        SessionModel.setParam("dn", "profile", testCase.savedProfile);
        const face = testCase.faceFor();
        const panel = testCase.panelFor();
        tryCompare(face.headlineLabel, "text", "Active");
        compare(face.captionLabel.text, "3.0 s noise learned");
        compare(panel.actionButton.text, "Forget Noise");
        verify(panel.actionButton.enabled, "a saved noise cannot be forgotten while stopped");
    }

    function test_learningWhileRunningCountsAPress() {
        const panel = testCase.panelFor();
        const face = testCase.faceFor();
        testCase.run();
        tryVerify(() => panel.actionButton.enabled, 1000, "Learn could not be pressed while running");
        panel.actionButton.clicked();
        compare(SessionModel.param("dn", "learn"), 1);
        tryCompare(face.headlineLabel, "text", "Active");
        compare(face.captionLabel.text, "3.0 s noise learned");
    }

    function test_forgettingDropsTheSavedAndTheRunningNoise() {
        SessionModel.setParam("dn", "profile", testCase.savedProfile);
        const panel = testCase.panelFor();
        tryCompare(panel.actionButton, "text", "Forget Noise");
        panel.actionButton.clicked();
        verify(SessionModel.param("dn", "profile") === "", "the saved noise is still there");
        compare(SessionModel.param("dn", "forget"), 1);
        tryCompare(panel.stateLabel, "text", "Inactive");
    }

    function test_theSlidersWriteTheirDecibels() {
        const panel = testCase.panelFor();
        panel.reductionSlider.value = 25.5;
        panel.reductionSlider.moved();
        SessionModel.commitParam("dn", "reduction");
        compare(SessionModel.param("dn", "reduction"), 25.5);
        compare(panel.reductionLabel.text, "25.5 dB");
        panel.thresholdSlider.value = 6;
        panel.thresholdSlider.moved();
        SessionModel.commitParam("dn", "threshold");
        compare(SessionModel.param("dn", "threshold"), 6);
        compare(panel.thresholdLabel.text, "6.0 dB");
    }
}
