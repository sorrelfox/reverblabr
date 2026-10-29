// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Pan block's face and panel offscreen, against the mock engine.
// They are Balance's drawing (tst_balance.qml) registered with Pan's
// setting, so what is checked here is that the register points them at
// the right parameter and the right words, and that the track fills out
// from the middle.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "Pan"
    when: windowShown

    property string sessionId: ""

    function panelFor() {
        const panel = createTemporaryObject(BlockPanels.panelFor("effect.pan"), testCase, { width: 320 });
        panel.blockId = "pan1";
        return panel;
    }

    function faceFor() {
        const face = createTemporaryObject(BlockFaces.faceFor("effect.pan"), testCase, { width: 220, height: 120 });
        face.blockId = "pan1";
        return face;
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Pan");
        verify(testCase.sessionId !== "", "the mock opened a session");
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock",
                                              block: { id: "pan1", type: "effect.pan" },
                                              index: -1 }));
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        SessionModel.setParam("pan1", "pan", 0);
        SessionModel.setBlockEnabled("pan1", true);
    }

    function test_theBlockHasAFaceAndAPanelOfItsOwn() {
        verify(BlockPanels.panelFor("effect.pan") !== null, "Pan is in the panel register");
        verify(BlockFaces.faceFor("effect.pan") !== null, "Pan is in the face register");
    }

    function test_theRangeTheInterfaceOffersIsTheOneTheEngineTakes() {
        const descriptor = BlockCatalogue.describeParam("effect.pan", "pan");
        const panel = testCase.panelFor();
        const face = testCase.faceFor();
        compare(panel.slider.from, descriptor.minimum);
        compare(panel.slider.to, descriptor.maximum);
        compare(face.slider.from, descriptor.minimum);
        compare(face.slider.to, descriptor.maximum);
        compare(descriptor.default, 0);
    }

    function test_itIsCalledPanAndSaysWhereTheSoundIs() {
        const panel = testCase.panelFor();
        const face = testCase.faceFor();
        compare(panel.titleLabel.text, "Pan");
        compare(face.captionLabel.text, "pan");
        compare(face.slider.Accessible.name, "Pan");
        compare(face.positionLabel.text, "C");
        compare(panel.positionLabel.text, "Centre");

        SessionModel.setParam("pan1", "pan", -40);
        tryCompare(face.slider, "value", -40);
        compare(face.positionLabel.text, "L 40");
        compare(panel.positionLabel.text, "L 40");
    }

    function test_bothSlidersWriteThePansSetting() {
        const panel = testCase.panelFor();
        panel.slider.value = 55;
        panel.slider.moved();
        SessionModel.commitParam("pan1", "pan");
        compare(SessionModel.param("pan1", "pan"), 55);

        const face = testCase.faceFor();
        face.slider.value = -20;
        face.slider.moved();
        SessionModel.commitParam("pan1", "pan");
        compare(SessionModel.param("pan1", "pan"), -20);
        compare(face.positionLabel.text, "L 20");
    }

    function test_theTrackFillsOutFromTheMiddle() {
        // "L 40" is forty steps left of the middle, and that is what is
        // filled: from 30 % of the way along to the middle.
        const face = testCase.faceFor();
        SessionModel.setParam("pan1", "pan", -40);
        tryCompare(face.slider, "value", -40);
        const groove = face.slider.background;
        const fill = groove.children[0];
        fuzzyCompare(fill.x, 0.3 * groove.width, 0.01);
        fuzzyCompare(fill.x + fill.width, 0.5 * groove.width, 0.01);
    }
}
