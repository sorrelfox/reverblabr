// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Amplify block's face and panel offscreen, against the mock engine:
// one slider, 0 to 1000 %, writing an amplitude, the same on both.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "Amplify"
    when: windowShown

    property string sessionId: ""

    function panelFor() {
        const panel = createTemporaryObject(BlockPanels.panelFor("effect.amplify"), testCase, { width: 340 });
        panel.blockId = "amp";
        return panel;
    }

    function faceFor() {
        const face = createTemporaryObject(BlockFaces.faceFor("effect.amplify"), testCase, { width: 200, height: 100 });
        face.blockId = "amp";
        return face;
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Amplify");
        verify(testCase.sessionId !== "", "the mock opened a session");
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "amp", type: "effect.amplify" }, index: -1 }));
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        SessionModel.setParam("amp", "gain", 1.0);
    }

    function test_theBlockHasAFaceAPanelAndAHelpPageOfItsOwn() {
        verify(BlockPanels.panelFor("effect.amplify") !== null, "Amplify is in the panel register");
        verify(BlockFaces.faceFor("effect.amplify") !== null, "Amplify is in the face register");
        compare(BlockPresentation.name("effect.amplify"), "Amplify");
    }

    function test_theSliderGoesTo1000PerCentAsTheEngineDoes() {
        const panel = testCase.panelFor();
        const face = testCase.faceFor();
        const gain = BlockCatalogue.describeParam("effect.amplify", "gain");
        compare(gain.maximum * 100, panel.topPercent);
        compare(panel.slider.to, 1000);
        compare(face.slider.to, 1000);
        compare(panel.slider.from, 0);
        compare(gain.default, 1);
    }

    function test_aNewBlockIsAt100PerCent() {
        const panel = testCase.panelFor();
        const face = testCase.faceFor();
        compare(panel.percentLabel.text, "100 %");
        compare(face.percentLabel.text, "100 %");
    }

    function test_theSliderWritesAnAmplitudeAndBothShowIt() {
        const panel = testCase.panelFor();
        const face = testCase.faceFor();
        panel.slider.value = 750;
        panel.slider.moved();
        SessionModel.commitParam("amp", "gain");
        compare(SessionModel.param("amp", "gain"), 7.5);
        tryCompare(face.percentLabel, "text", "750 %");
        face.slider.value = 1000;
        face.slider.moved();
        SessionModel.commitParam("amp", "gain");
        compare(SessionModel.param("amp", "gain"), 10);
        // A thousand in the locale's own digits.
        tryCompare(panel.percentLabel, "text", "1,000 %");
    }
}
