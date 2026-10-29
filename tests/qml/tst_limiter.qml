// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Limiter's face and panel offscreen, against the mock engine:
// the settings write the block's parameters, the ranges are the engine's,
// and the face says the ceiling while stopped and the reduction the block
// publishes while it runs.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "Limiter"
    when: windowShown

    property string sessionId: ""

    function panelFor() {
        const panel = createTemporaryObject(BlockPanels.panelFor("effect.limiter"), testCase, { width: 320 });
        panel.blockId = "lim";
        return panel;
    }

    function faceFor() {
        const face = createTemporaryObject(BlockFaces.faceFor("effect.limiter"), testCase,
                                           { width: 220, height: 120 });
        face.blockId = "lim";
        return face;
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Limiter");
        verify(testCase.sessionId !== "", "the mock opened a session");
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "lim", type: "effect.limiter" },
                                              index: -1 }));
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        SessionModel.setParam("lim", "ceiling", -1);
        SessionModel.setParam("lim", "release", 100);
        SessionModel.setParam("lim", "truePeak", true);
        SessionModel.setBlockEnabled("lim", true);
    }

    function cleanup() {
        TestEngine.clearFakeLevels(testCase.sessionId);
        TestEngine.stop(testCase.sessionId);
        Meters.watch("");
    }

    function play(peak: real) {
        Meters.watch(testCase.sessionId);
        TestEngine.start(testCase.sessionId);
        TestEngine.setFakeLevel(testCase.sessionId, "lim", peak);
        tryVerify(() => Meters.live && (Meters.levels("lim").active ?? false), 1000, "the block never went live");
    }

    function test_theBlockHasAFaceAndAPanelOfItsOwn() {
        verify(BlockPanels.panelFor("effect.limiter") !== null, "the Limiter is in the panel register");
        verify(BlockFaces.faceFor("effect.limiter") !== null, "the Limiter is in the face register");
    }

    function test_theRangesAreTheEnginesOwn() {
        const panel = testCase.panelFor();
        const ceiling = BlockCatalogue.describeParam("effect.limiter", "ceiling");
        const release = BlockCatalogue.describeParam("effect.limiter", "release");
        compare(panel.words.defaultCeiling, ceiling.default);
        compare(panel.words.lowestCeiling, ceiling.minimum);
        compare(ceiling.maximum, 0);
        compare(panel.words.defaultRelease, release.default);
        compare(panel.words.shortestRelease, release.minimum);
        compare(panel.words.longestRelease, release.maximum);
        compare(release.taper, "log");
        compare(BlockCatalogue.describeParam("effect.limiter", "truePeak").default, true);
        compare(panel.ceilingSlider.from, ceiling.minimum);
        compare(panel.ceilingSlider.to, ceiling.maximum);
    }

    function test_theCeilingIsSaidAsATrueOrASamplePeak() {
        const panel = testCase.panelFor();
        const face = testCase.faceFor();
        compare(panel.ceilingLabel.text, "-1.0 dBTP");
        compare(face.headlineLabel.text, "-1.0 dB");
        compare(face.captionLabel.text, "TP ceiling");
        panel.truePeakSwitch.toggle();
        panel.truePeakSwitch.toggled();
        compare(SessionModel.param("lim", "truePeak"), false);
        tryCompare(panel.ceilingLabel, "text", "-1.0 dBFS");
        tryCompare(face.captionLabel, "text", "ceiling");
        verify(face.detail.indexOf("-1.0 dBFS") >= 0, "the tooltip does not say the ceiling");
    }

    function test_theSlidersWriteTheirNumbers() {
        const panel = testCase.panelFor();
        panel.ceilingSlider.value = -6.3;
        panel.ceilingSlider.moved();
        SessionModel.commitParam("lim", "ceiling");
        fuzzyCompare(SessionModel.param("lim", "ceiling"), -6.3, 1e-6);

        // Logarithmic: the middle of 10 ms to 1 s is 100 ms.
        panel.releaseSlider.value = 0.5;
        panel.releaseSlider.moved();
        SessionModel.commitParam("lim", "release");
        compare(SessionModel.param("lim", "release"), 100);
        compare(panel.releaseLabel.text, "100 ms");
        panel.releaseSlider.value = 1;
        panel.releaseSlider.moved();
        SessionModel.commitParam("lim", "release");
        compare(panel.releaseLabel.text, "1.00 s");
    }

    function test_whileItRunsTheFaceAndThePanelSayTheReduction() {
        // A level of -2 dBFS under a -6 dB ceiling: the block takes 4 dB off.
        SessionModel.setParam("lim", "ceiling", -6);
        const face = testCase.faceFor();
        const panel = testCase.panelFor();
        compare(panel.reductionLabel.text, "—");
        testCase.play(Math.pow(10, -2 / 20));
        tryCompare(face.headlineLabel, "text", "-4.0 dB");
        compare(face.captionLabel.text, "reduction");
        compare(panel.reductionLabel.text, "-4.0 dB");
        fuzzyCompare(face.fill, 4 / 12, 0.01);
    }

    function test_underTheCeilingThereIsNothingToTakeOff() {
        const face = testCase.faceFor();
        testCase.play(0.5);
        tryCompare(face.headlineLabel, "text", "0.0 dB");
        compare(face.fill, 0);
    }
}
