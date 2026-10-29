// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Balance block's face and panel offscreen, against the mock
// engine: that the setting is said as a side and how far, that the slider
// writes the number the block stores, and that the range the interface
// offers is the one the engine takes.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "Balance"
    when: windowShown

    property string sessionId: ""

    function panelFor(width) {
        const panel = createTemporaryObject(BlockPanels.panelFor("effect.balance"), testCase, { width: width });
        panel.blockId = "bal";
        return panel;
    }

    function faceFor() {
        const face = createTemporaryObject(BlockFaces.faceFor("effect.balance"), testCase,
                                           { width: 220, height: 120 });
        face.blockId = "bal";
        return face;
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Balance");
        verify(testCase.sessionId !== "", "the mock opened a session");
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock",
                                              block: { id: "bal", type: "effect.balance" },
                                              index: -1 }));
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        SessionModel.setParam("bal", "balance", 0);
        SessionModel.setBlockEnabled("bal", true);
    }

    function test_theBlockHasAFaceAndAPanelOfItsOwn() {
        verify(BlockPanels.panelFor("effect.balance") !== null, "Balance is in the panel register");
        verify(BlockFaces.faceFor("effect.balance") !== null, "Balance is in the face register");
    }

    function test_theRangeTheInterfaceOffersIsTheOneTheEngineTakes() {
        // **The one thing neither side can check by reading itself**: the
        // interface does not depend on the DSP layer, so the ends are
        // written down twice and this is what keeps them together.
        const descriptor = BlockCatalogue.describeParam("effect.balance", "balance");
        const panel = testCase.panelFor(320);
        const face = testCase.faceFor();
        compare(panel.slider.from, descriptor.minimum);
        compare(panel.slider.to, descriptor.maximum);
        compare(face.slider.from, descriptor.minimum);
        compare(face.slider.to, descriptor.maximum);
        compare(descriptor.default, 0);
    }

    function test_theSettingIsSaidAsASideAndHowFar_data() {
        return [
            { tag: "centre", position: 0, face: "C", panel: "Centre" },
            { tag: "left", position: -30, face: "L 30", panel: "L 30" },
            { tag: "right", position: 45, face: "R 45", panel: "R 45" },
            { tag: "all the way left", position: -100, face: "L 100", panel: "L 100" },
            { tag: "all the way right", position: 100, face: "R 100", panel: "R 100" },
        ];
    }

    function test_theSettingIsSaidAsASideAndHowFar(data) {
        const panel = testCase.panelFor(320);
        const face = testCase.faceFor();
        SessionModel.setParam("bal", "balance", data.position);
        tryCompare(panel.slider, "value", data.position);
        tryCompare(face.slider, "value", data.position);
        compare(face.positionLabel.text, data.face);
        compare(panel.positionLabel.text, data.panel);
        compare(face.captionLabel.text, "balance");
    }

    function test_thePanelsSliderWritesTheNumberTheBlockStores() {
        const panel = testCase.panelFor(320);
        panel.slider.value = -30;
        panel.slider.moved();
        SessionModel.commitParam("bal", "balance");
        compare(SessionModel.param("bal", "balance"), -30);
        compare(panel.positionLabel.text, "L 30");

        panel.slider.value = 72;
        panel.slider.moved();
        SessionModel.commitParam("bal", "balance");
        compare(SessionModel.param("bal", "balance"), 72);
    }

    function test_theFacesSliderWritesItToo() {
        const face = testCase.faceFor();
        face.slider.value = 25;
        face.slider.moved();
        SessionModel.commitParam("bal", "balance");
        compare(SessionModel.param("bal", "balance"), 25);
        compare(face.positionLabel.text, "R 25");
    }

    function test_theKeyboardMovesItOneStepAtATime() {
        const panel = testCase.panelFor(320);
        panel.slider.forceActiveFocus();
        keyClick(Qt.Key_Left);
        keyClick(Qt.Key_Left);
        SessionModel.commitParam("bal", "balance");
        compare(SessionModel.param("bal", "balance"), -2);
        compare(panel.positionLabel.text, "L 2");
    }

    function test_thePanelIsLaidOutAsTheDesignDrawsIt() {
        // The name and the setting on one line with the setting in the
        // accent, the slider under them at the panel's width, and "L" and
        // "R" under its two ends.
        const panel = testCase.panelFor(320);
        wait(50);
        compare(panel.titleLabel.text, "Balance");
        const number = panel.positionLabel.mapToItem(panel, 0, 0);
        const slider = panel.slider.mapToItem(panel, 0, 0);
        verify(number.y + panel.positionLabel.height <= slider.y + 1, "the setting is not above the slider");
        verify(Qt.colorEqual(panel.positionLabel.color, AppColors.accentText), "the setting is not in the accent");
        verify(panel.slider.width >= panel.width - 1, "the slider does not take the panel's width");

        const left = panel.leftEnd.mapToItem(panel, 0, 0);
        const right = panel.rightEnd.mapToItem(panel, panel.rightEnd.width, 0);
        compare(panel.leftEnd.text, "L");
        compare(panel.rightEnd.text, "R");
        verify(left.y >= slider.y + panel.slider.height - 1, "the ends are not under the slider");
        verify(left.x <= slider.x + 1, "L is not at the slider's left end");
        verify(right.x >= slider.x + panel.slider.width - 1, "R is not at the slider's right end");
    }

    function test_aBlockThatIsOffHasAGreyFace() {
        const face = testCase.faceFor();
        verify(!Qt.colorEqual(face.tint, AppColors.muted), "a block that is on is in its category's colour");
        SessionModel.setBlockEnabled("bal", false);
        tryVerify(() => Qt.colorEqual(face.tint, AppColors.muted), 1000, "a block that is off is grey");
    }
}
