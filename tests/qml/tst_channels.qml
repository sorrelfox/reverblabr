// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Channels block's face and panel offscreen, against the mock
// engine: that the actions are the engine's, each named in words, and that
// choosing one writes its id.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "Channels"
    when: windowShown

    property string sessionId: ""

    function panelFor() {
        const panel = createTemporaryObject(BlockPanels.panelFor("effect.channels"), testCase, { width: 320 });
        panel.blockId = "ch";
        return panel;
    }

    function faceFor() {
        const face = createTemporaryObject(BlockFaces.faceFor("effect.channels"), testCase,
                                           { width: 180, height: 90 });
        face.blockId = "ch";
        return face;
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Channels");
        verify(testCase.sessionId !== "", "the mock opened a session");
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock",
                                              block: { id: "ch", type: "effect.channels" },
                                              index: -1 }));
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        SessionModel.setParam("ch", "action", "none");
        SessionModel.setBlockEnabled("ch", true);
    }

    function test_theBlockHasAFaceAndAPanelOfItsOwn() {
        verify(BlockPanels.panelFor("effect.channels") !== null, "Channels is in the panel register");
        verify(BlockFaces.faceFor("effect.channels") !== null, "Channels is in the face register");
    }

    function test_theActionsAreTheEnginesEachWithWords() {
        // The interface does not depend on the DSP layer, so the ids are
        // written down twice; this keeps them together, in the same order.
        const descriptor = BlockCatalogue.describeParam("effect.channels", "action");
        const panel = testCase.panelFor();
        compare(panel.words.actions, descriptor.choices);
        compare(descriptor.default, "none");
        for (const id of descriptor.choices) {
            verify(panel.words.actionName(id) !== id, id + " has no name");
            verify(panel.words.routing(id) !== "", id + " has no routing");
            verify(panel.words.explanation(id) !== "", id + " has no explanation");
        }
    }

    function test_theFaceSaysTheActionAndWhereEachSideComesFrom_data() {
        return [
            { tag: "none", action: "none", headline: "No change", caption: "L→L · R→R" },
            { tag: "swap", action: "swap", headline: "Swap sides", caption: "R→L · L→R" },
            { tag: "mono", action: "mono", headline: "Mono", caption: "L+R → both" },
            { tag: "invert right", action: "invertRight", headline: "Invert right", caption: "L→L · −R→R" },
        ];
    }

    function test_theFaceSaysTheActionAndWhereEachSideComesFrom(data) {
        const face = testCase.faceFor();
        SessionModel.setParam("ch", "action", data.action);
        tryCompare(face.headlineLabel, "text", data.headline);
        compare(face.captionLabel.text, data.caption);
        verify(face.detail.length > 0, "the tooltip has nothing to say");
    }

    function test_theListWritesTheActionByItsId() {
        const panel = testCase.panelFor();
        const index = panel.words.actions.indexOf("leftToBoth");
        panel.actionBox.currentIndex = index;
        panel.actionBox.activated(index);
        compare(SessionModel.param("ch", "action"), "leftToBoth");
        tryCompare(panel.actionBox, "displayText", "Left to both");
        compare(panel.explanationLine.text, panel.words.explanation("leftToBoth"));
    }

    function test_theListFollowsTheSession() {
        const panel = testCase.panelFor();
        SessionModel.setParam("ch", "action", "difference");
        tryCompare(panel.actionBox, "currentIndex", panel.words.actions.indexOf("difference"));
        compare(panel.actionBox.displayText, "Difference");
    }

    function test_theListIsChosenFromWithTheKeyboard() {
        const panel = testCase.panelFor();
        panel.actionBox.forceActiveFocus();
        keyClick(Qt.Key_Down);
        compare(SessionModel.param("ch", "action"), "swap");
    }
}
