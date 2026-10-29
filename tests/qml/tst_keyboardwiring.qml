// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Wires drawn and taken out from the keyboard, offscreen against the
// mock engine: C on a block aims a wire at each input that would take it in
// turn, the arrows move it, Enter draws it; D goes through the wires on the
// block and Enter takes one out; Esc leaves the session as it was. **No
// pointer anywhere below**: the focus is handed to a tile the way Tab hands
// it over, and every step after that is a key.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "KeyboardWiring"
    when: windowShown

    property string sessionId: ""

    Component {
        id: shownCanvas

        Window {
            property alias canvas: inner

            width: 1400
            height: 600
            visible: true

            SessionCanvas {
                id: inner

                anchors.fill: parent
            }
        }
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Keyboard wiring");
        verify(testCase.sessionId !== "", "the mock opened a session");
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        SessionModel.sessionId = testCase.sessionId;
        SessionModel.setAutoConnect(false);
    }

    function cleanup() {
        SessionModel.removeBlocks(SessionModel.blockIds);
        compare(SessionModel.blockIds.length, 0);
        SessionModel.setAutoConnect(true);
    }

    function tileFor(canvas, blockId) {
        for (let row = 0; row < canvas.tiles.count; ++row) {
            const tile = canvas.tiles.itemAt(row);
            if (tile !== null && tile.blockId === blockId) {
                return tile;
            }
        }
        return null;
    }

    /// A canvas with a source, a volume and an output in a row, none of them
    /// wired, the window in front and the source's tile holding the focus.
    function threeInARow() {
        const window = createTemporaryObject(shownCanvas, null);
        const canvas = window.canvas;
        const source = canvas.addBlockAt("source.device", 100, 200);
        const volume = canvas.addBlockAt("effect.volume", 500, 200);
        const output = canvas.addBlockAt("output.device", 900, 200);
        canvas.clearSelection();
        waitForRendering(canvas);
        window.requestActivate();
        tryVerify(() => window.active, 2000, "the window never came to the front");
        testCase.tileFor(canvas, source).forceActiveFocus(Qt.TabFocusReason);
        return {window: window, canvas: canvas, source: source, volume: volume, output: output};
    }

    function test_cAimsAtEachInputInTurnAndEnterDrawsTheWire() {
        const s = testCase.threeInARow();
        const canvas = s.canvas;

        keyClick(Qt.Key_C);
        verify(canvas.keyWiring, "C did not start a wire");
        verify(canvas.wiring, "no wire is being drawn");
        compare(canvas.wiringFromBlock, s.source);
        // **Every input that would take it, left to right** — the volume,
        // then the output — and nothing the rules refuse, such as the
        // source's own.
        compare(canvas.keyTargets.length, 2);
        compare(canvas.wiringOverBlock, s.volume, "the loose end did not start at the first input");
        compare(canvas.lastAnnouncement, "Connect to Volume, 1 of 2");

        keyClick(Qt.Key_Right);
        compare(canvas.wiringOverBlock, s.output, "Right did not move the loose end on");
        keyClick(Qt.Key_Right);
        compare(canvas.wiringOverBlock, s.volume, "Right at the end did not go round to the first");
        keyClick(Qt.Key_Left);
        compare(canvas.wiringOverBlock, s.output, "Left at the start did not go round to the last");
        keyClick(Qt.Key_Up);
        compare(canvas.wiringOverBlock, s.volume);

        keyClick(Qt.Key_Return);
        verify(!canvas.keyWiring, "Enter left the wire half drawn");
        verify(!canvas.wiring);
        // **Both channels, as a Ctrl-drag would draw them.**
        verify(SessionModel.hasWire(s.source, 0, s.volume, 0), "Enter drew no wire");
        verify(SessionModel.hasWire(s.source, 1, s.volume, 1), "Enter drew only one channel");
        compare(canvas.wires.count, 1);
        verify(testCase.tileFor(canvas, s.source).activeFocus, "the focus did not go back to the block");
        compare(canvas.lastAnnouncement, "Connected Input Device to Volume");
    }

    function test_escapeLeavesTheSessionAsItWas() {
        const s = testCase.threeInARow();
        const canvas = s.canvas;

        keyClick(Qt.Key_C);
        keyClick(Qt.Key_Right);
        keyClick(Qt.Key_Escape);
        verify(!canvas.keyWiring, "Esc did not end the wire");
        verify(!canvas.wiring);
        compare(canvas.wires.count, 0, "Esc drew a wire");
        verify(testCase.tileFor(canvas, s.source).activeFocus, "the focus did not go back to the block");
    }

    function test_theArrowsDoNotMoveTheLooseEndOntoAnInputThatWouldRefuseIt() {
        const s = testCase.threeInARow();
        const canvas = s.canvas;
        // Volume into the source's own block cannot be; neither can a second
        // link that is already there, nor a loop back to the volume.
        verify(SessionModel.connectLanes(s.source, s.volume, 2, 0));
        testCase.tileFor(canvas, s.volume).forceActiveFocus(Qt.TabFocusReason);

        keyClick(Qt.Key_C);
        compare(canvas.keyTargets.length, 1, "an input that would refuse the wire was offered");
        compare(canvas.wiringOverBlock, s.output);
        keyClick(Qt.Key_Escape);
    }

    function test_aBlockNothingCanBeWiredFromSaysSoAndStartsNoWire() {
        const s = testCase.threeInARow();
        const canvas = s.canvas;
        testCase.tileFor(canvas, s.output).forceActiveFocus(Qt.TabFocusReason);

        keyClick(Qt.Key_C);
        verify(!canvas.keyWiring, "a wire was started from an output");
        compare(canvas.lastAnnouncement, "Nothing can be connected to from Output Device");
        verify(testCase.tileFor(canvas, s.output).activeFocus, "the focus left the block");
    }

    function test_dGoesThroughTheWiresAndEnterTakesTheOneInHandOut() {
        const s = testCase.threeInARow();
        const canvas = s.canvas;
        verify(SessionModel.connectLanes(s.source, s.volume, 2, 0));
        verify(SessionModel.connectLanes(s.volume, s.output, 2, 0));
        testCase.tileFor(canvas, s.volume).forceActiveFocus(Qt.TabFocusReason);

        keyClick(Qt.Key_D);
        verify(canvas.keyUnwiring, "D did not start going through the wires");
        compare(canvas.keyWires.length, 2, "not every wire on the block was offered");
        const first = canvas.selectedWire;
        verify(first !== null, "no wire is in hand");
        keyClick(Qt.Key_Down);
        const second = canvas.selectedWire;
        verify(second.toBlock !== first.toBlock, "Down did not move to the other wire");
        verify(canvas.lastAnnouncement.startsWith("Wire "), "the wire in hand was not said");

        keyClick(Qt.Key_Return);
        verify(!canvas.keyUnwiring, "Enter left the walk going");
        compare(canvas.wires.count, 1, "Enter did not take exactly one wire out");
        verify(!SessionModel.hasWire(second.fromBlock, 0, second.toBlock, 0), "the wrong wire went");
        verify(testCase.tileFor(canvas, s.volume).activeFocus, "the focus did not go back to the block");
    }

    function test_deleteTakesTheWireOutAndEscapeLeavesIt() {
        const s = testCase.threeInARow();
        const canvas = s.canvas;
        verify(SessionModel.connectLanes(s.source, s.volume, 2, 0));
        testCase.tileFor(canvas, s.volume).forceActiveFocus(Qt.TabFocusReason);

        keyClick(Qt.Key_D);
        keyClick(Qt.Key_Escape);
        verify(!canvas.keyUnwiring);
        compare(canvas.selectedWire, null, "Esc left the wire selected");
        compare(canvas.wires.count, 1, "Esc took the wire out");

        keyClick(Qt.Key_D);
        keyClick(Qt.Key_Delete);
        compare(canvas.wires.count, 0, "Del did not take the wire out");
    }

    function test_tabbingAwayAbandonsTheWire() {
        const s = testCase.threeInARow();
        const canvas = s.canvas;

        keyClick(Qt.Key_C);
        verify(canvas.keyWiring);
        testCase.tileFor(canvas, s.volume).forceActiveFocus(Qt.TabFocusReason);
        verify(!canvas.keyWiring, "the wire stayed aimed after the focus left the canvas");
        verify(!canvas.wiring);
        compare(canvas.wires.count, 0);
    }

    function test_undoTakesAKeyboardWireBackInOneStep() {
        const s = testCase.threeInARow();
        const canvas = s.canvas;

        keyClick(Qt.Key_C);
        keyClick(Qt.Key_Return);
        compare(canvas.wires.count, 1);
        SessionModel.undo();
        tryCompare(canvas.wires, "count", 0, 1000, "one undo did not take the whole wire back");
    }
}
