// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Auto Level's face and panel offscreen, against the mock engine:
// the switch and the two advanced settings write the block's parameters,
// the ranges are the engine's, and the face says the boost the block
// publishes while it runs and the setting while it does not.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "AutoLevel"
    when: windowShown

    property string sessionId: ""

    function panelFor() {
        const panel = createTemporaryObject(BlockPanels.panelFor("effect.autolevel"), testCase, { width: 320 });
        panel.blockId = "al";
        return panel;
    }

    function faceFor() {
        const face = createTemporaryObject(BlockFaces.faceFor("effect.autolevel"), testCase,
                                           { width: 220, height: 120 });
        face.blockId = "al";
        return face;
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Auto Level");
        verify(testCase.sessionId !== "", "the mock opened a session");
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "al", type: "effect.autolevel" },
                                              index: -1 }));
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        SessionModel.setParam("al", "boost", true);
        SessionModel.setParam("al", "target", -20);
        SessionModel.setParam("al", "maximumBoost", 18);
        SessionModel.setBlockEnabled("al", true);
    }

    function cleanup() {
        TestEngine.clearFakeLevels(testCase.sessionId);
        TestEngine.stop(testCase.sessionId);
        Meters.watch("");
    }

    /// Runs the session with a steady signal of \a peak through the block.
    function play(peak: real) {
        Meters.watch(testCase.sessionId);
        TestEngine.start(testCase.sessionId);
        TestEngine.setFakeLevel(testCase.sessionId, "al", peak);
        tryVerify(() => Meters.live && (Meters.levels("al").active ?? false), 1000, "the block never went live");
    }

    function test_theBlockHasAFaceAndAPanelOfItsOwn() {
        verify(BlockPanels.panelFor("effect.autolevel") !== null, "Auto Level is in the panel register");
        verify(BlockFaces.faceFor("effect.autolevel") !== null, "Auto Level is in the face register");
    }

    function test_theRangesAreTheEnginesOwn() {
        // Written down on both sides of the layer boundary; this keeps them
        // together.
        const panel = testCase.panelFor();
        const target = BlockCatalogue.describeParam("effect.autolevel", "target");
        const maximum = BlockCatalogue.describeParam("effect.autolevel", "maximumBoost");
        compare(panel.words.defaultTarget, target.default);
        compare(panel.words.lowestTarget, target.minimum);
        compare(panel.words.highestTarget, target.maximum);
        compare(panel.words.defaultMaximum, maximum.default);
        compare(panel.words.highestMaximum, maximum.maximum);
        compare(maximum.minimum, 0);
        compare(BlockCatalogue.describeParam("effect.autolevel", "boost").default, true);
        compare(panel.targetSlider.from, target.minimum);
        compare(panel.targetSlider.to, target.maximum);
        compare(panel.maximumSlider.to, maximum.maximum);
    }

    function test_theSwitchTurnsTheBoostOnAndOff() {
        const panel = testCase.panelFor();
        verify(panel.boostSwitch.checked, "a new Auto Level is on");
        panel.boostSwitch.toggle();
        panel.boostSwitch.toggled();
        compare(SessionModel.param("al", "boost"), false);
        verify(!panel.targetSlider.enabled, "the target can still be moved with the boost off");
        panel.boostSwitch.toggle();
        panel.boostSwitch.toggled();
        compare(SessionModel.param("al", "boost"), true);
    }

    function test_theAdvancedSettingsAreFoldedAwayAndWriteTheirNumbers() {
        const panel = testCase.panelFor();
        verify(!panel.advancedSection.expanded, "the advanced settings are open to begin with");
        panel.advancedSection.expanded = true;

        panel.targetSlider.value = -16;
        panel.targetSlider.moved();
        SessionModel.commitParam("al", "target");
        compare(SessionModel.param("al", "target"), -16);
        compare(panel.targetLabel.text, "-16 LUFS");

        panel.maximumSlider.value = 9;
        panel.maximumSlider.moved();
        SessionModel.commitParam("al", "maximumBoost");
        compare(SessionModel.param("al", "maximumBoost"), 9);
        compare(panel.maximumLabel.text, "9 dB");
    }

    function test_whileNothingPlaysTheFaceSaysTheSetting() {
        const face = testCase.faceFor();
        compare(face.headlineLabel.text, "On");
        compare(face.captionLabel.text, "boost");
        SessionModel.setParam("al", "boost", false);
        tryCompare(face.headlineLabel, "text", "Off");
        verify(Qt.colorEqual(face.tint, AppColors.muted), "a switched-off Auto Level is not grey");
    }

    function test_whileItRunsTheFaceSaysTheBoost() {
        // A steady sine whose RMS is −30 dBFS, which is its loudness to a
        // fraction of a decibel: ten under the target, so it is lifted ten.
        const face = testCase.faceFor();
        testCase.play(Math.pow(10, -30 / 20) * Math.SQRT2);
        tryCompare(face.headlineLabel, "text", "+10.0 dB");
        fuzzyCompare(face.fill, 10 / 18, 0.01);
    }

    function test_theBoostStopsAtTheMaximumAndBelowTheGate() {
        const face = testCase.faceFor();
        SessionModel.setParam("al", "maximumBoost", 6);
        testCase.play(Math.pow(10, -40 / 20) * Math.SQRT2);
        tryCompare(face.headlineLabel, "text", "+6.0 dB");
        compare(face.fill, 1);

        // Under the gate: nothing to lift.
        TestEngine.setFakeLevel(testCase.sessionId, "al", Math.pow(10, -60 / 20) * Math.SQRT2);
        tryCompare(face.headlineLabel, "text", "0.0 dB");
    }
}
