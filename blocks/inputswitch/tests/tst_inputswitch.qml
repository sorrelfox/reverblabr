// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Input Switch's face and panel, against the mock engine: A and B
// on the tile, the one passing chosen, and a click that sends the
// crossfade time, the input and the time put back as one edit.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "InputSwitch"
    when: windowShown

    property string sessionId: ""
    property var sent: []

    Component {
        id: faceComponent

        InputSwitchFace {
            width: 180
            height: 90
        }
    }

    Component {
        id: panelComponent

        InputSwitchPanel {
            width: 320
        }
    }

    // A panel in a window of its own, for a test that presses on it.
    Component {
        id: shownPanel

        Window {
            property alias panel: inner

            width: 400
            height: 300
            visible: true

            InputSwitchPanel {
                id: inner

                width: 320
            }
        }
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.sessionPatch.connect(function (id, revision, json) {
            const command = JSON.parse(json);
            if (command.op === "setParam") {
                testCase.sent.push([command.key, command.value]);
            }
        });
        TestEngine.createSession("Input Switch");
        verify(testCase.sessionId !== "", "the mock opened a session");
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "sw", type: "advanced.inputswitch" },
                                              index: -1 }));
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        SessionModel.setParam("sw", "input", "a");
        testCase.sent = [];
    }

    function test_itIsInAdvancedWithInputsAAndB() {
        verify(BlockCatalogue.types.indexOf("advanced.inputswitch") >= 0, "the engine does not offer it");
        compare(BlockPresentation.category("advanced.inputswitch"), "advanced");
        verify(BlockFaces.faceFor("advanced.inputswitch") !== null, "it has no face");
        verify(BlockPanels.panelFor("advanced.inputswitch") !== null, "it has no panel");
        const inputs = BlockCatalogue.describe("advanced.inputswitch").inputs;
        compare(inputs.lanes, ["a", "b"]);
        compare(BlockPresentation.laneName("a"), "Input A");
        compare(BlockPresentation.laneName("b"), "Input B");
        compare(BlockCatalogue.describeParam("advanced.inputswitch", "input").choices, ["a", "b"]);
        compare(BlockCatalogue.describeParam("advanced.inputswitch", "crossfade").default, 500,
                "the panel's half second is not the block's");
    }

    function test_theFaceShowsWhichIsPassingAndSwitchesOnAClick() {
        const face = createTemporaryObject(faceComponent, null);
        face.blockId = "sw";
        wait(50);
        verify(face.aButton.checked && !face.bButton.checked);
        compare(face.captionLabel.text, "Input A passing");

        face.bButton.clicked();
        compare(testCase.sent, [["crossfade", 500], ["input", "b"], ["crossfade", 500]]);
        tryVerify(() => face.bButton.checked && !face.aButton.checked, 1000);
        compare(face.captionLabel.text, "Input B passing");

        // Clicking the one already passing sends nothing: no empty undo step.
        testCase.sent = [];
        face.bButton.clicked();
        compare(testCase.sent, []);

        // While the block is running and part way across, it says so.
        face.control.published = [0.4];
        compare(face.captionLabel.text, "Crossfading…");
        face.control.published = [1];
        compare(face.captionLabel.text, "Input B passing");
    }

    function test_shiftTakesTwoSecondsAndAClickUndoesInOneStep() {
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "sw";
        compare(panel.wantsPresets, false);
        panel.control.toggle(true);
        compare(testCase.sent, [["crossfade", 2000], ["input", "b"], ["crossfade", 500]]);
        compare(SessionModel.param("sw", "input"), "b");
        compare(panel.explanation.text, "Sound from input B goes through; input A is not heard.");

        testCase.sent = [];
        SessionModel.undo();
        tryVerify(() => testCase.sent.length === 3, 1000, "the undo was not one step");
        compare(testCase.sent[1], ["input", "a"]);
        tryCompare(panel.control, "input", "a");
    }

    function test_thePanelsSwitchTogglesOnAClick() {
        const window = createTemporaryObject(shownPanel, null);
        const panel = window.panel;
        panel.blockId = "sw";
        tryVerify(() => panel.toggle.width > 0, 1000);
        mouseClick(panel.toggle);
        compare(SessionModel.param("sw", "input"), "b");
        compare(testCase.sent[0], ["crossfade", 500]);
    }
}
