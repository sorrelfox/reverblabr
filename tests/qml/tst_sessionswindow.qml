// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Sessions window: opened from the Session menu, one of it however
// often it is asked for, every saved session a row, several run and stopped
// at once, columns hidden from a menu, sorted by a header, and a session
// deleted after being asked about by name.
//
// **One mock engine serves the whole suite**, so the sessions other test
// files made are in its list too; these tests go by the ids of the
// sessions they make rather than by how many rows there are.

import QtQuick
import QtQuick.Controls as QQC2
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "SessionsWindow"
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

    /// Opens the Sessions window from the main window's action.
    function openSessions(window: var): var {
        window.sessionsWindowAction.trigger();
        tryVerify(() => window.sessionsWindow !== null && window.sessionsWindow.visible, 1000,
                  "the action opened no window");
        const sessions = window.sessionsWindow;
        tryVerify(() => sessions.overview.ready, 1000, "the table never had the list");
        return sessions;
    }

    /// Makes a session in the engine and gives back its id.
    function makeSession(name: string): string {
        const before = Sessions.open.length;
        TestEngine.createSession(name);
        tryVerify(() => Sessions.open.length === before + 1, 1000, "the session was not made");
        return Sessions.open[Sessions.open.length - 1].id;
    }

    function stateOf(sessions: var, sessionId: string): string {
        return sessions.overview.rowAt(sessions.overview.rowOf(sessionId)).state ?? "";
    }

    function cleanup() {
        Appearance.mode = SuiteAppearance;
    }

    /// The item called \a objectName in the cell of column \a key and the
    /// row of \a sessionId, the table scrolled to it first: **a TableView
    /// makes cells only for what it shows**.
    function cellItem(sessions: var, sessionId: string, key: string, objectName: string): var {
        const cell = Qt.point(sessions.overview.columns.indexOf(key), sessions.overview.rowOf(sessionId));
        if (cell.x < 0 || cell.y < 0) {
            return null;
        }
        sessions.table.positionViewAtCell(cell, TableView.Contain);
        const item = sessions.table.itemAtCell(cell);
        if (!item) {
            return null;
        }
        for (let index = 0; index < item.children.length; ++index) {
            if (item.children[index].objectName === objectName) {
                return item.children[index];
            }
        }
        return null;
    }

    // **Auto Run is a column**, set from its checkbox as an edit of
    // the session.
    function test_autoRunIsSetFromItsColumn() {
        const window = testCase.openWindow();
        const id = testCase.makeSession("Starts at login");
        const sessions = testCase.openSessions(window);
        verify(sessions.overview.columns.indexOf("autoRun") >= 0, "no Auto Run column");
        tryVerify(() => testCase.cellItem(sessions, id, "autoRun", "autoRunCheck") !== null, 1000,
                  "no checkbox for the session");
        const check = testCase.cellItem(sessions, id, "autoRun", "autoRunCheck");
        compare(check.checked, false);

        // Offscreen a click on a check box is not delivered; its signal is.
        check.checked = true;
        check.toggled();
        tryVerify(() => sessions.overview.rowAt(sessions.overview.rowOf(id)).autoRun === true, 1000,
                  "the session was not set to run at login");
        sessions.overview.setAutoRun([id], false);
        tryVerify(() => sessions.overview.rowAt(sessions.overview.rowOf(id)).autoRun === false, 1000);
        sessions.close();
    }

    function test_theSessionMenuOpensOneWindow() {
        const window = testCase.openWindow();
        compare(window.sessionsWindow, null, "the window was made before anybody asked for it");
        const sessions = testCase.openSessions(window);
        compare(sessions.title, "Sessions");
        verify(sessions.transientParent === window, "the window does not belong to the main window");

        // Asked for again, the same window.
        window.sessionsWindowAction.trigger();
        verify(window.sessionsWindow === sessions, "a second Sessions window");
        sessions.close();
        tryVerify(() => !sessions.visible, 1000);
        window.showSessionsWindow();
        verify(window.sessionsWindow === sessions, "closing it lost the window");
        tryVerify(() => sessions.visible, 1000);
        sessions.close();
    }

    function test_everySessionIsARowThatSaysWhereItStands() {
        const window = testCase.openWindow();
        const quiet = testCase.makeSession("Quiet corner");
        const busy = testCase.makeSession("Busy corner");
        TestEngine.start(busy);
        const sessions = testCase.openSessions(window);

        tryVerify(() => sessions.overview.rowOf(quiet) >= 0 && sessions.overview.rowOf(busy) >= 0, 1000,
                  "a session is missing from the table");
        tryVerify(() => sessions.overview.rowAt(sessions.overview.rowOf(busy)).running === true, 1000,
                  "the running session is not shown running");
        compare(sessions.statusText("idle", undefined), "Inactive");
        verify(sessions.statusText("running", new Date(2026, 8, 28, 9, 12)).indexOf("Started at") === 0,
               "a running session does not say when it started");
        compare(sessions.table.columns, sessions.overview.columns.length);
        TestEngine.stop(busy);
        sessions.close();
    }

    function test_severalPickedSessionsRunAndStopTogether() {
        const window = testCase.openWindow();
        const first = testCase.makeSession("Together one");
        const second = testCase.makeSession("Together two");
        const sessions = testCase.openSessions(window);
        tryVerify(() => sessions.overview.rowOf(first) >= 0 && sessions.overview.rowOf(second) >= 0, 1000);

        sessions.select(first, false);
        sessions.select(second, true);
        compare(sessions.selectedIds.length, 2);
        verify(sessions.runButton.enabled, "Run is off with two stopped sessions picked");
        compare(sessions.runButton.text, "Run 2");
        mouseClick(sessions.runButton);
        tryVerify(() => testCase.stateOf(sessions, first) === "running"
                  && testCase.stateOf(sessions, second) === "running", 1000, "they did not both start");

        tryVerify(() => sessions.stopButton.enabled, 1000);
        mouseClick(sessions.stopButton);
        tryVerify(() => testCase.stateOf(sessions, first) === "idle"
                  && testCase.stateOf(sessions, second) === "idle", 1000, "they did not both stop");
        sessions.close();
    }

    function test_aColumnIsHiddenFromTheMenuButTheNameStays() {
        const window = testCase.openWindow();
        const sessions = testCase.openSessions(window);
        const overview = sessions.overview;
        verify(overview.columns.indexOf("recordings") >= 0);

        sessions.columnsMenu.open();
        let recordings = null;
        let name = null;
        for (let i = 0; i < sessions.columnsMenu.count; ++i) {
            const item = sessions.columnsMenu.itemAt(i);
            if (item.text === "Recordings") {
                recordings = item;
            } else if (item.text === "Name") {
                name = item;
            }
        }
        verify(recordings !== null, "the menu has no Recordings entry");
        verify(name !== null && !name.enabled, "the name could be hidden");
        recordings.triggered();
        tryVerify(() => overview.columns.indexOf("recordings") < 0, 1000, "the column did not go");
        compare(sessions.table.columns, overview.columns.length);
        sessions.columnsMenu.close();

        overview.setColumnShown("recordings", true);
        sessions.close();
    }

    function test_clickingAHeaderSortsByItAndAgainTheOtherWay() {
        const window = testCase.openWindow();
        const sessions = testCase.openSessions(window);
        const overview = sessions.overview;
        tryVerify(() => sessions.columnHeads.columns > 0, 1000);
        const was = overview.sortColumn;
        overview.sortBy("name"); // start from a known place
        if (overview.sortDescending) {
            overview.sortBy("name");
        }
        compare(overview.sortColumn, "name");
        verify(!overview.sortDescending);

        const nameHead = sessions.columnHeads.itemAtCell(Qt.point(overview.columns.indexOf("name"), 0));
        verify(nameHead !== null, "no head for the name column");
        mouseClick(nameHead);
        tryVerify(() => overview.sortDescending, 1000, "a click on the sorted column did not turn it round");
        mouseClick(nameHead);
        tryVerify(() => !overview.sortDescending, 1000);
        if (was !== "name") {
            overview.sortBy(was);
        }
        sessions.close();
    }

    function test_aSessionIsDeletedOnlyOnceAskedByName() {
        const window = testCase.openWindow();
        const doomed = testCase.makeSession("Doomed");
        const sessions = testCase.openSessions(window);
        tryVerify(() => sessions.overview.rowOf(doomed) >= 0, 1000);

        sessions.select(doomed, false);
        verify(sessions.deleteButton.enabled);
        mouseClick(sessions.deleteButton);
        tryVerify(() => sessions.deleteDialog.opened, 1000, "nothing asked before deleting");
        verify(sessions.deleteDialog.subtitle.indexOf("Doomed") >= 0, "the question does not name the session");
        sessions.deleteDialog.customFooterActions[0].trigger();
        tryVerify(() => sessions.overview.rowOf(doomed) < 0, 1000, "the session is still listed");
        compare(sessions.selectedIds.indexOf(doomed), -1, "a deleted session is still picked");
        sessions.close();
    }

    function test_itIsFramedAsTheMainWindowIs() {
        const window = testCase.openWindow();
        const sessions = testCase.openSessions(window);
        const frameless = (sessions.flags & Qt.FramelessWindowHint) !== 0;
        compare(frameless, window.ownTitleBar, "the window's frame is not the main window's kind");
        compare(sessions.titleBar.visible, window.ownTitleBar);
        compare(sessions.chrome.active, window.ownTitleBar);
        sessions.close();
    }
}
