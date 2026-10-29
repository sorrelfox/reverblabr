// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The system tray's popover and what the tray asks of the window.
// **No icon here**: TrayIcon is made only by the application's main(), so a
// test run never puts one in a desktop's tray. The clicks it would pass on
// are made through TrayState, which is what the icon calls.
//
// **One mock engine serves the whole suite**, so the tests find their own
// sessions by id rather than by how many rows there are.

import QtQuick
import QtQuick.Controls as QQC2
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "TrayPopover"
    when: windowShown

    Component {
        id: mainWindow

        Main {}
    }

    function openWindow(): var {
        const window = createTemporaryObject(mainWindow, testCase);
        verify(window, "the main window is created");
        tryVerify(() => AppShell.hasSession, 1000, "the window came up with a session");
        return window;
    }

    /// A click on the icon, as TrayIcon passes it on.
    function openPopover(window: var): var {
        TrayState.requestPopover();
        tryVerify(() => window.trayPopover !== null && window.trayPopover.visible, 1000, "no popover opened");
        const popover = window.trayPopover;
        tryVerify(() => popover.overview.ready, 1000, "the popover never had the list");
        // **Laid out only once it has drawn**: before that every part of it
        // is none tall, and a list that is none tall makes no rows.
        waitForRendering(popover.contentItem);
        return popover;
    }

    function makeSession(name: string): string {
        const before = Sessions.open.length;
        TestEngine.createSession(name);
        tryVerify(() => Sessions.open.length === before + 1, 1000, "the session was not made");
        return Sessions.open[Sessions.open.length - 1].id;
    }

    function stateOf(popover: var, sessionId: string): string {
        return popover.overview.rowAt(popover.overview.rowOf(sessionId)).state ?? "";
    }

    /// The Run/Stop button of \a sessionId's row in the popover.
    function runButtonOf(popover: var, sessionId: string): var {
        const index = popover.overview.rowOf(sessionId);
        popover.sessionList.positionViewAtIndex(index, ListView.Contain);
        // A list makes its rows at its next layout, not at once.
        tryVerify(() => popover.sessionList.itemAtIndex(index) !== null, 1000,
                  "the session has no row in the popover: row " + index + " of " + popover.sessionList.count);
        const row = popover.sessionList.itemAtIndex(index);
        return row.children[row.children.length - 1];
    }

    function cleanup() {
        TrayState.available = false;
    }

    function test_aClickOnTheIconOpensThePopoverAndAnotherClosesIt() {
        const window = testCase.openWindow();
        compare(window.trayPopover, null, "the popover was made before the icon was clicked");
        const popover = testCase.openPopover(window);
        verify(popover.transientParent === null, "the popover belongs to the main window, and hides with it");
        compare(popover.title, "Reverblabr");

        TrayState.requestPopover();
        tryVerify(() => !popover.visible, 1000, "a second click did not close the popover");
    }

    function test_sessionsAreRunAndStoppedFromThePopover() {
        const window = testCase.openWindow();
        const id = testCase.makeSession("Tray run");
        const popover = testCase.openPopover(window);
        tryVerify(() => popover.overview.rowOf(id) >= 0, 1000, "the session is missing from the popover");

        mouseClick(testCase.runButtonOf(popover, id));
        tryVerify(() => testCase.stateOf(popover, id) === "running", 1000, "Run in the popover did not start it");
        compare(TrayState.runningCount >= 1, true, "the tray does not know it runs");

        mouseClick(testCase.runButtonOf(popover, id));
        tryVerify(() => testCase.stateOf(popover, id) === "idle", 1000, "Stop in the popover did not stop it");
        popover.hide();
    }

    function test_theMetersMarkedForTheTrayAreShownAndBringTheirSessionForward() {
        const window = testCase.openWindow();
        const id = testCase.makeSession("Tray meters");
        TestEngine.applyEdit(id, JSON.stringify({
            op: "addBlock",
            block: { id: "trayMeter", type: "meter.peakrms", params: { showInTray: true } },
            index: -1
        }));
        const other = testCase.makeSession("Somewhere else");
        compare(Sessions.currentId, other);

        const popover = testCase.openPopover(window);
        // **Found by its block and session**: other tests leave meters of
        // their own marked for the tray.
        const find = () => TrayState.meters.findIndex(m => m.sessionId === id && m.blockId === "trayMeter");
        tryVerify(() => find() >= 0, 1000, "the meter marked for the tray is not listed");
        popover.meterList.positionViewAtIndex(find(), ListView.Contain);
        tryVerify(() => popover.meterList.itemAtIndex(find()) !== null, 1000, "the meter marked for the tray is not shown");
        const meter = popover.meterList.itemAtIndex(find());
        compare(meter.meterName, "Peak/RMS");
        // **Read out with its value**, as a block's meter is.
        compare(meter.levelBar.Accessible.role, Accessible.ProgressBar);
        compare(meter.levelBar.Accessible.name, "Peak/RMS");
        compare([meter.levelBar.from, meter.levelBar.to], [-60, 0]);
        compare(meter.modelData.sessionName, "Tray meters");
        verify(TrayState.watching, "the levels are not read while the popover shows them");

        mouseClick(meter);
        tryVerify(() => Sessions.currentId === id, 1000, "the meter did not bring its session forward");
        tryVerify(() => !popover.visible, 1000, "the popover stayed open over the window");
        verify(!TrayState.watching, "the levels are still read with nobody to see them");
    }

    function test_whatWentWrongIsShownAndCountsAsSeenOnceClosed() {
        const window = testCase.openWindow();
        TestEngine.reportError("plugin.crashed", JSON.stringify({ sessionId: Sessions.currentId, plugin: "Reverb" }));
        tryVerify(() => TrayState.attention, 1000, "a crashed plugin did not ask for attention");

        const popover = testCase.openPopover(window);
        verify(popover.attentionMessage.visible, "the popover does not say what went wrong");
        compare(popover.attentionMessage.text, "Plugin stopped");
        verify(TrayState.attention, "opening the popover counted as seen before it could be read");

        popover.hide();
        tryVerify(() => !TrayState.attention, 1000, "closing the popover did not count as seen");
    }

    function test_withATrayClosingHidesAndATrayGoneBringsTheWindowBack() {
        const window = testCase.openWindow();
        window.showSessionsWindow();
        tryVerify(() => window.sessionsWindow !== null && window.sessionsWindow.visible, 1000);

        TrayState.available = true;
        // A middle click hides it, and the windows that belong to it go too.
        TrayState.requestWindowToggle();
        tryVerify(() => !window.visible, 1000, "a middle click did not hide the window");
        tryVerify(() => !window.sessionsWindow.visible, 1000, "the Sessions window stayed open without its window");

        // Another brings it back.
        TrayState.requestWindowToggle();
        tryVerify(() => window.visible, 1000, "a second middle click did not show the window");

        // **Hidden, and the tray goes**: the window comes back rather than
        // being left out of reach.
        TrayState.requestWindowToggle();
        tryVerify(() => !window.visible, 1000);
        TrayState.available = false;
        tryVerify(() => window.visible, 1000, "the tray went and the hidden window stayed hidden");
    }

    function test_theIconsMenuOpensTheSessionsWindow() {
        const window = testCase.openWindow();
        TrayState.requestSessionsWindow();
        tryVerify(() => window.sessionsWindow !== null && window.sessionsWindow.visible, 1000,
                  "All Sessions… in the tray's menu opened nothing");
        window.sessionsWindow.close();
    }
}
