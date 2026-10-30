// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The session as a list, offscreen against the mock engine: lanes
// are runs of blocks each feeding only the next; a row says what its tile
// says; one Tab stop, the arrows, Home and End go from row to row across the
// lanes; Enter opens the settings beside the row, C draws a wire from it and
// Delete takes the block out. **No pointer**: every step is a key.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "SessionList"
    when: windowShown

    property string sessionId: ""

    Component {
        id: shownCanvas

        Window {
            property alias canvas: inner

            width: 1400
            height: 700
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
        TestEngine.createSession("Session as a list");
        verify(testCase.sessionId !== "", "the mock opened a session");
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        SessionModel.sessionId = testCase.sessionId;
        SessionModel.setAutoConnect(false);
        WindowState.listShown = true;
    }

    function cleanup() {
        WindowState.listShown = false;
        SessionModel.removeBlocks(SessionModel.blockIds);
        compare(SessionModel.blockIds.length, 0);
        SessionModel.setAutoConnect(true);
    }

    /// Two sources into a volume, the volume into an output: the volume is
    /// fed by two, so the lanes are the first source, the volume and the
    /// output, and the second source.
    function twoIntoOne() {
        const window = createTemporaryObject(shownCanvas, null);
        const canvas = window.canvas;
        const first = canvas.addBlockAt("source.device", 100, 100);
        const volume = canvas.addBlockAt("effect.volume", 500, 200);
        const output = canvas.addBlockAt("output.device", 900, 200);
        const second = canvas.addBlockAt("source.device", 100, 300);
        verify(canvas.commitWire(first, "", 0, volume, 0));
        verify(canvas.commitWire(second, "", 0, volume, 0));
        verify(canvas.commitWire(volume, "", 0, output, 0));
        canvas.clearSelection();
        waitForRendering(canvas);
        window.requestActivate();
        tryVerify(() => window.active, 2000, "the window never came to the front");
        return {window: window, canvas: canvas, first: first, volume: volume, output: output, second: second};
    }

    function test_theListFollowsTheChoiceAndTheTilesStepAside() {
        const s = testCase.twoIntoOne();
        verify(s.canvas.listShown);
        verify(s.canvas.sessionList.visible, "the list is not showing");
        verify(!s.canvas.tileFor(s.volume).visible, "a tile is still drawn under the list");
        WindowState.listShown = false;
        verify(!s.canvas.sessionList.visible, "the list stayed after the canvas was chosen");
        verify(s.canvas.tileFor(s.volume).visible, "the tiles did not come back");
    }

    function test_lanesAreRunsOfBlocksEachFeedingOnlyTheNext() {
        const s = testCase.twoIntoOne();
        compare(s.canvas.sessionList.lanes, [[s.first], [s.volume, s.output], [s.second]]);
        // **A plain chain is one lane**, in the order the sound goes.
        SessionModel.removeBlocks([s.first, s.second]);
        compare(s.canvas.sessionList.lanes, [[s.volume, s.output]]);
    }

    function test_aRowSaysWhatItsTileSays() {
        const s = testCase.twoIntoOne();
        const list = s.canvas.sessionList;
        for (const blockId of [s.first, s.volume, s.output]) {
            const row = list.rowFor(blockId);
            const tile = s.canvas.tileFor(blockId);
            verify(row !== null, "no row for a block");
            compare(row.Accessible.role, Accessible.ListItem);
            compare(row.Accessible.name, tile.Accessible.name);
            compare(row.Accessible.description, tile.Accessible.description);
        }
        verify(list.rowFor(s.volume).Accessible.description.indexOf("Fed by") >= 0,
               "the row does not say what feeds it: " + list.rowFor(s.volume).Accessible.description);
    }

    function test_oneTabStopAndTheArrowsGoAcrossTheLanes() {
        const s = testCase.twoIntoOne();
        const list = s.canvas.sessionList;
        const stops = list.allRows().filter(row => row.activeFocusOnTab);
        compare(stops.length, 1, "the list is more than one Tab stop");
        list.focusRow(s.first);
        verify(list.rowFor(s.first).activeFocus);
        keyClick(Qt.Key_Down);
        verify(list.rowFor(s.volume).activeFocus, "Down did not go on to the next lane");
        verify(s.canvas.isSelected(s.volume), "the row in hand is not the one selected");
        keyClick(Qt.Key_Down);
        verify(list.rowFor(s.output).activeFocus);
        keyClick(Qt.Key_Up);
        verify(list.rowFor(s.volume).activeFocus, "Up did not go back");
        keyClick(Qt.Key_End);
        verify(list.rowFor(s.second).activeFocus, "End did not go to the last row");
        keyClick(Qt.Key_Home);
        verify(list.rowFor(s.first).activeFocus, "Home did not go to the first row");
        compare(list.allRows().filter(row => row.activeFocusOnTab).map(row => row.blockId), [s.first],
                "the Tab stop did not follow the keyboard");
    }

    function test_enterOpensTheSettingsBesideTheRow() {
        const s = testCase.twoIntoOne();
        const list = s.canvas.sessionList;
        list.focusRow(s.volume);
        keyClick(Qt.Key_Return);
        tryVerify(() => s.canvas.settings.blockId === s.volume && s.canvas.settings.visible, 1000,
                  "Enter did not open the block's settings");
        const row = list.rowRect(s.volume);
        compare(s.canvas.settingsAnchor, row, "the panel is not placed beside the row");
        verify(s.canvas.settings.x >= row.x + row.width || s.canvas.settings.x + s.canvas.settings.width <= row.x,
               "the panel is over the row it belongs to");
        keyClick(Qt.Key_Escape);
        tryVerify(() => s.canvas.settings.blockId === "", 1000);
    }

    function test_cOnARowDrawsAWireAndGivesTheRowBack() {
        const s = testCase.twoIntoOne();
        const list = s.canvas.sessionList;
        list.focusRow(s.first);
        keyClick(Qt.Key_C);
        verify(s.canvas.keyWiring, "C on a row started no wire");
        compare(s.canvas.wiringFromBlock, s.first);
        for (let step = 0; step < 10 && s.canvas.wiringOverBlock !== s.output; ++step) {
            keyClick(Qt.Key_Right);
        }
        compare(s.canvas.wiringOverBlock, s.output, "the output was never offered");
        verify(list.rowFor(s.output).aimedAt, "the row the wire is aimed at does not say so");
        keyClick(Qt.Key_Return);
        verify(SessionModel.hasWire(s.first, 0, s.output, 0), "Enter drew no wire");
        tryVerify(() => list.rowFor(s.first) !== null && list.rowFor(s.first).activeFocus, 1000,
                  "the keyboard did not come back to the row");
    }

    function test_deleteTakesTheBlockOutAndTheKeyboardGoesOn() {
        const s = testCase.twoIntoOne();
        const list = s.canvas.sessionList;
        list.focusRow(s.volume);
        keyClick(Qt.Key_Delete);
        tryVerify(() => SessionModel.block(s.volume).type === undefined, 1000, "Delete left the block");
        tryVerify(() => list.rowFor(s.output) !== null && list.rowFor(s.output).activeFocus, 1000,
                  "the keyboard did not go on to the next row");
    }

    // **A change made elsewhere does not lose the keyboard's place**: new
    // lanes are new rows, and the row in hand is found again in them.
    function test_theKeyboardKeepsItsPlaceWhenTheLanesChange() {
        const s = testCase.twoIntoOne();
        const list = s.canvas.sessionList;
        list.focusRow(s.output);
        const before = JSON.stringify(list.lanes);
        verify(s.canvas.commitWire(s.second, "", 0, s.output, 0), "the wire was refused");
        verify(JSON.stringify(list.lanes) !== before, "the lanes did not change");
        tryVerify(() => list.rowFor(s.output) !== null && list.rowFor(s.output).activeFocus, 1000,
                  "the keyboard lost its row when the lanes changed");
    }

    function test_theMenuKeyOpensTheBlocksMenu() {
        const s = testCase.twoIntoOne();
        s.canvas.sessionList.focusRow(s.output);
        keyClick(Qt.Key_Menu);
        tryVerify(() => s.canvas.tileMenu.opened, 1000, "the Menu key opened nothing");
        verify(s.canvas.isSelected(s.output), "the menu is not about the row in hand");
        s.canvas.tileMenu.close();
    }
}
