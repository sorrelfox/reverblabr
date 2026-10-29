// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// **A whole session from the keyboard alone** (what the accessibility pass
// left open), in the real window against the mock engine: three blocks
// added from the library's search, wired with C, a setting changed in a
// panel reached with Tab, and the session run and stopped with Space. Not
// one pointer event: every step is a key, pressed where the focus is.

import QtQuick
import QtQuick.Controls as QQC2
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "KeyboardSession"
    when: windowShown

    Component {
        id: mainWindow

        Main {
            width: 1400
            height: 900
        }
    }

    function cleanup() {
        if (AppShell.running) {
            AppShell.toggleRun();
            tryVerify(() => !AppShell.running, 1000);
        }
        SessionModel.removeBlocks(SessionModel.blockIds);
        SessionModel.setAutoConnect(true);
    }

    /// What has the keyboard, in words, for a failure message.
    function describe(item) {
        if (item === null || item === undefined) {
            return "nothing";
        }
        const name = item.Accessible ? item.Accessible.name : "";
        return String(item).replace(/\(.*$/, "") + (item.objectName ? " " + item.objectName : "")
            + (name ? " \"" + name + "\"" : "");
    }

    /// Presses Tab until \a found holds for what has the keyboard, or gives
    /// up after \a limit presses. Answers what it went through, for a
    /// failure message.
    function tabUntil(window, found, limit) {
        const path = [];
        for (let press = 0; press < limit; ++press) {
            if (found(window.activeFocusItem)) {
                return path;
            }
            keyClick(Qt.Key_Tab);
            path.push(testCase.describe(window.activeFocusItem));
        }
        verify(found(window.activeFocusItem), "Tab never got there:\n" + path.join("\n"));
        return path;
    }

    /// Adds a block by typing part of its name into the library's search.
    function addByName(window, typed) {
        const before = SessionModel.blockIds.length;
        keySequence(StandardKey.Find);
        verify(window.blockLibrary.searchField.activeFocus, "Ctrl+F did not put the cursor in the search");
        compare(window.blockLibrary.searchField.selectedText, window.blockLibrary.searchField.text,
                "Ctrl+F did not select what was there, so typing would add to it");
        for (const letter of typed) {
            keyClick(letter);
        }
        keyClick(Qt.Key_Return);
        tryVerify(() => SessionModel.blockIds.length === before + 1, 1000, "Enter in the search added nothing for " + typed);
        return SessionModel.blockIds[SessionModel.blockIds.length - 1];
    }

    /// Whether \a item is somewhere inside \a outer.
    function inside(item, outer) {
        for (let at = item; at !== null && at !== undefined; at = at.parent) {
            if (at === outer) {
                return true;
            }
        }
        return false;
    }

    function isTile(item) {
        return item !== null && item.blockId !== undefined && item.tileActivated !== undefined;
    }

    function test_aWholeSessionFromTheKeyboardAlone() {
        const window = createTemporaryObject(mainWindow, testCase);
        tryVerify(() => AppShell.hasSession && BlockCatalogue.ready, 3000, "no session to build in");
        SessionModel.removeBlocks(SessionModel.blockIds);
        SessionModel.setAutoConnect(false);
        window.requestActivate();
        tryVerify(() => window.active, 2000, "the window never came to the front");
        waitForRendering(window.contentItem);

        // **Three blocks, by name.**
        const input = testCase.addByName(window, "input dev");
        compare(SessionModel.block(input).type, "source.device");
        const volume = testCase.addByName(window, "volume");
        compare(SessionModel.block(volume).type, "effect.volume");
        const output = testCase.addByName(window, "output dev");
        compare(SessionModel.block(output).type, "output.device");

        // **Onto the canvas with Tab**, to the input's tile.
        testCase.tabUntil(window, item => testCase.isTile(item) && item.blockId === input, 60);

        // **Wired with C**: the input into the volume, the volume into the
        // output, each by aiming with the arrows and drawing with Enter.
        const canvas = window.sessionCanvas;
        testCase.wire(canvas, volume);
        verify(SessionModel.hasWire(input, 0, volume, 0), "C and Enter did not wire the input into the volume");
        testCase.tabUntil(window, item => testCase.isTile(item) && item.blockId === volume, 10);
        testCase.wire(canvas, output);
        verify(SessionModel.hasWire(volume, 0, output, 0), "C and Enter did not wire the volume into the output");

        // **A setting, changed in the panel Enter opens**, reached with Tab.
        keyClick(Qt.Key_Return);
        tryVerify(() => window.settingsPanel.blockId === volume && window.settingsPanel.visible, 1000,
                  "Enter on the volume did not open its settings");
        const before = SessionModel.param(volume, "gain");
        testCase.tabUntil(window, item => item instanceof QQC2.Slider && testCase.inside(item, window.settingsPanel), 40);
        // Down from the top: a new Volume starts at the top of its range.
        keyClick(Qt.Key_Left);
        keyClick(Qt.Key_Left);
        tryVerify(() => SessionModel.param(volume, "gain") !== before, 1000,
                  "the arrows on the panel's slider did not change the gain");
        keyClick(Qt.Key_Escape);

        // **Run and stop with Space**, wherever the keyboard is.
        keyClick(Qt.Key_Space);
        tryVerify(() => AppShell.running, 1000, "Space did not run the session");
        keyClick(Qt.Key_Space);
        tryVerify(() => !AppShell.running, 1000, "Space did not stop the session");
    }

    /// C on the block the keyboard is on, then the arrows until the loose
    /// end is on \a target, then Enter.
    function wire(canvas, target) {
        keyClick(Qt.Key_C);
        verify(canvas.keyWiring, "C did not start a wire");
        for (let step = 0; step < 10 && canvas.wiringOverBlock !== target; ++step) {
            keyClick(Qt.Key_Right);
        }
        compare(canvas.wiringOverBlock, target, "the arrows never reached the block to wire to");
        keyClick(Qt.Key_Return);
        verify(!canvas.keyWiring, "Enter left the wire half drawn");
    }
}
