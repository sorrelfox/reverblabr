// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Schedule tab, offscreen: a timer made, switched off, and deleted,
// each as an edit of the session; a timer that would never run cannot be
// saved; and quitting the captured applications is asked about first.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "Schedule"

    property string openedId: ""

    Connections {
        target: TestEngine

        function onSessionOpened(sessionId) {
            testCase.openedId = sessionId;
        }
    }

    Component {
        id: mainWindow

        Main {}
    }

    function cleanup() {
        AppShell.engineInBackground = true;
        AppShell.sessionId = "";
    }

    function openWindow(): var {
        const window = createTemporaryObject(mainWindow, testCase);
        TestEngine.createSession("Schedule tab");
        AppShell.sessionId = testCase.openedId;
        tryCompare(SessionModel, "sessionId", testCase.openedId);
        window.sidebarIndex = window.sidebar.scheduleTab;
        waitForRendering(window.contentItem);
        return window;
    }

    function test_theScheduleTabIsTheSidebarsFourth() {
        const window = createTemporaryObject(mainWindow, testCase);
        verify(window.schedulePane, "the Schedule tab has the session's timers in it");
        compare(window.sidebar.scheduleTab, 3);
        window.sidebarIndex = window.sidebar.scheduleTab;
        compare(window.sidebar.currentIndex, 3);
    }

    function test_aTimerIsMadeSwitchedOffAndDeletedAsEditsOfTheSession() {
        const window = openWindow();
        const pane = window.schedulePane;
        verify(pane.emptyMessage.visible, "an empty list says what would fill it");
        compare(SessionModel.timers.length, 0);

        pane.addTimer();
        const dialog = pane.dialog;
        tryVerify(() => dialog.opened);
        dialog.repeatBox.currentIndex = 2; // Monday to Friday
        dialog.hourBox.value = 7;
        dialog.minuteBox.value = 30;
        dialog.hoursBox.value = 1;
        dialog.minutesBox.value = 30;
        compare(dialog.draft.repeat, "weekdays");
        verify(dialog.saveAction.enabled);
        dialog.saveAction.trigger();
        tryVerify(() => SessionModel.timers.length === 1);
        const timer = SessionModel.timers[0];
        compare(timer.repeat, "weekdays");
        compare(timer.time, "07:30");
        compare(timer.duration, 5400);
        compare(timer.enabled, true);
        verify(timer.nextRun !== undefined, "it says when it next runs");
        verify(!pane.emptyMessage.visible);
        verify(SessionModel.canUndo, "one edit, which undo takes back");

        SessionModel.setTimerEnabled(timer.id, false);
        tryVerify(() => SessionModel.timers[0].enabled === false);
        compare(SessionModel.timers[0].time, "07:30", "everything else is kept");

        SessionModel.removeTimer(timer.id);
        tryVerify(() => SessionModel.timers.length === 0);
        SessionModel.undo();
        tryVerify(() => SessionModel.timers.length === 1);
    }

    // **Save waits for a timer that can run**: chosen days with none
    // chosen is no timer, nor is a date that is not one.
    function test_aTimerThatWouldNeverRunCannotBeSaved() {
        const window = openWindow();
        const dialog = window.schedulePane.dialog;
        window.schedulePane.addTimer();
        tryVerify(() => dialog.opened);

        dialog.repeatBox.currentIndex = 3; // on these days
        verify(!dialog.saveAction.enabled, "no day chosen");
        dialog.toggleDay("thu", true);
        verify(dialog.saveAction.enabled);
        compare(SessionModel.timerCalendar(dialog.draft).indexOf("Thu "), 0);

        dialog.repeatBox.currentIndex = 0; // once
        dialog.dateField.text = "2026-02-30";
        verify(!dialog.saveAction.enabled, "not a date");
        dialog.dateField.text = "2026-11-03";
        verify(dialog.saveAction.enabled);

        dialog.hoursBox.value = 0;
        dialog.minutesBox.value = 0;
        verify(!dialog.saveAction.enabled, "a timer that runs for no time");
        dialog.close();
    }

    // **Asked first**: quitting somebody's applications is said before it
    // is set, and Cancel leaves it unset.
    function test_quittingTheCapturedApplicationsIsAskedFirst() {
        const window = openWindow();
        const dialog = window.schedulePane.dialog;
        window.schedulePane.addTimer();
        tryVerify(() => dialog.opened);

        dialog.quitBox.click();
        tryVerify(() => dialog.quitPrompt.opened);
        verify(!dialog.quitBox.checked, "not until the question is answered");
        dialog.quitPrompt.close();
        verify(!dialog.quitBox.checked);

        dialog.quitBox.click();
        tryVerify(() => dialog.quitPrompt.opened);
        dialog.quitPrompt.quitAction.trigger();
        verify(dialog.quitBox.checked);
        compare(dialog.draft.quitApplications, true);
        dialog.close();
    }

    function test_aWindowWithAnEngineOfItsOwnSaysTimersNeedTheBackgroundOne() {
        const window = openWindow();
        verify(!window.schedulePane.needsBackground.visible);
        AppShell.engineInBackground = false;
        verify(window.schedulePane.needsBackground.visible);
    }
}
