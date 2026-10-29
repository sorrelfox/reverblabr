// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// **Where Tab goes, and that it can be seen going there**. The main
// window's Tab chain is walked from its start until it comes round again:
// every stop has to be showing, inside the window, and has to look
// different with the keyboard on it than without — a control that takes
// the keyboard and draws nothing to say so leaves somebody who cannot use a
// pointer not knowing where they are.
//
// **Pixels, not properties**: a control can have a focus ring that is
// never drawn (a colour equal to what is under it, a size of nothing, a
// style that ignores it), so the test looks at what is on the screen.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "FocusOrder"
    when: windowShown

    Component {
        id: mainWindow

        Main {
            width: 1400
            height: 900
        }
    }

    Component {
        id: unmarkedControl

        // A control that takes the keyboard and draws nothing for it.
        Rectangle {
            objectName: "injectedUnmarked"
            width: 60
            height: 24
            color: "gray"
            activeFocusOnTab: true
        }
    }

    // **Only the styles Reverblabr ships.** The suite also runs under Qt's
    // Basic style, which is the tests' own and draws no focus on controls
    // that leave it to the style.
    function init() {
        if (!Appearance.ownStyle) {
            skip("only under the Reverblabr control style");
        }
    }

    function cleanup() {
        SessionModel.removeBlocks(SessionModel.blockIds);
    }

    function describe(item) {
        const name = item.Accessible ? item.Accessible.name : "";
        return String(item).replace(/\(.*$/, "") + (item.objectName ? " " + item.objectName : "")
            + (name ? " \"" + name + "\"" : "");
    }

    /// The Tab chain from the first stop, once round.
    function tabChain(window) {
        const stops = [];
        const start = window.contentItem.nextItemInFocusChain(true);
        let item = start;
        for (let count = 0; item !== null && count < 400; ++count) {
            stops.push(item);
            item = item.nextItemInFocusChain(true);
            if (item === start) {
                return stops;
            }
        }
        fail("the Tab chain never came round: " + stops.length + " stops");
        return stops;
    }

    function rootOf(window) {
        return window.contentItem.parent ?? window.contentItem;
    }

    /// Whether anything in \a item's box, and a few pixels round it, is
    /// drawn differently in \a with than in \a without. **A band round the
    /// edge, every pixel, and the inside on a grid**: a focus mark is a ring
    /// or a fill, and either is found without looking at every pixel of a
    /// canvas-sized item.
    function looksDifferent(window, item, without, withFocus) {
        const box = item.mapToItem(testCase.rootOf(window), 0, 0, item.width, item.height);
        const margin = 4;
        const left = Math.max(0, Math.floor(box.x - margin));
        const top = Math.max(0, Math.floor(box.y - margin));
        const right = Math.min(without.width - 1, Math.ceil(box.x + box.width + margin));
        const bottom = Math.min(without.height - 1, Math.ceil(box.y + box.height + margin));
        const band = margin * 2 + 1;
        const differs = (x, y) => !Qt.colorEqual(without.pixel(x, y), withFocus.pixel(x, y));
        for (let y = top; y <= bottom; ++y) {
            const edgeRow = y - top < band || bottom - y < band;
            const gridRow = (y - top) % 3 === 0;
            for (let x = left; x <= right; ++x) {
                const edgeColumn = x - left < band || right - x < band;
                if (!edgeRow && !edgeColumn && !(gridRow && (x - left) % 3 === 0)) {
                    continue;
                }
                if (differs(x, y)) {
                    return true;
                }
            }
        }
        return false;
    }

    /// Every stop that cannot be seen, or cannot be seen to have the
    /// keyboard, as lines for a failure message.
    function problemsWith(window, stops) {
        const problems = [];
        const seen = [];
        for (const item of stops) {
            const what = testCase.describe(item);
            if (seen.indexOf(item) >= 0) {
                problems.push(what + ": reached twice");
                continue;
            }
            seen.push(item);
            if (!item.visible || item.width <= 0 || item.height <= 0) {
                problems.push(what + ": not showing");
                continue;
            }
            const box = item.mapToItem(testCase.rootOf(window), 0, 0, item.width, item.height);
            if (box.x + box.width <= 0 || box.y + box.height <= 0 || box.x >= window.width || box.y >= window.height) {
                problems.push(what + ": outside the window");
                continue;
            }
            // **A short wait, not waitForRendering**: taking the keyboard
            // away from a control that drew nothing for it changes nothing,
            // and waiting for a frame that never comes is five seconds.
            window.contentItem.forceActiveFocus(Qt.OtherFocusReason);
            wait(80);
            const without = grabImage(testCase.rootOf(window));
            item.forceActiveFocus(Qt.TabFocusReason);
            wait(80);
            const withFocus = grabImage(testCase.rootOf(window));
            if (!testCase.looksDifferent(window, item, without, withFocus)) {
                problems.push(what + ": nothing shows it has the keyboard");
            }
        }
        return problems;
    }

    function openWindow() {
        const window = createTemporaryObject(mainWindow, testCase);
        tryVerify(() => AppShell.hasSession && BlockCatalogue.ready, 3000, "no session to put blocks in");
        SessionModel.addBlock("source.device", Qt.point(60, 60));
        SessionModel.addBlock("effect.volume", Qt.point(360, 60));
        SessionModel.addBlock("output.device", Qt.point(660, 60));
        window.requestActivate();
        tryVerify(() => window.active, 2000, "the window never came to the front");
        waitForRendering(window.contentItem);
        return window;
    }

    // **The check sees what it is there to see**: a control that takes the
    // keyboard and draws nothing for it is reported.
    function test_theCheckFindsAControlThatDrawsNoFocus() {
        const window = testCase.openWindow();
        const injected = createTemporaryObject(unmarkedControl, window.contentItem, {x: 400, y: 400});
        waitForRendering(window.contentItem);
        const problems = testCase.problemsWith(window, [injected]);
        compare(problems.length, 1, "the unmarked control was not found: " + JSON.stringify(problems));
    }

    function test_everyTabStopIsShowingAndShowsTheKeyboard() {
        const window = testCase.openWindow();
        const stops = testCase.tabChain(window);
        verify(stops.length > 15, "only " + stops.length + " Tab stops in the whole window");
        verify(stops.some(item => item.tileActivated !== undefined), "no block on the canvas is a Tab stop");
        const problems = testCase.problemsWith(window, stops);
        compare(problems.length, 0, problems.length + " of " + stops.length + " Tab stops:\n" + problems.join("\n"));
    }

    function test_everyTabStopOfTheListIsShowingAndShowsTheKeyboard() {
        const window = testCase.openWindow();
        WindowState.listShown = true;
        tryVerify(() => window.sessionCanvas.sessionList.visible, 1000);
        waitForRendering(window.contentItem);
        const stops = testCase.tabChain(window);
        const problems = testCase.problemsWith(window, stops);
        WindowState.listShown = false;
        verify(stops.some(item => item.blockId !== undefined && item.Accessible.role === Accessible.ListItem),
               "no row of the list is a Tab stop");
        compare(problems.length, 0, problems.length + " of " + stops.length + " Tab stops:\n" + problems.join("\n"));
    }
}
