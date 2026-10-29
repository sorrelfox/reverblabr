// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Running a session and understanding its state, offscreen: the
// banner when the audio system goes away and the Retry that brings it back,
// what the status bar says about the connection, and the session's own tab.
//
// **The mock is driven, not mocked.** TestEngine is the same facade the
// window is bound to, so the audio system going away in a test is the same
// signal it is in life.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "SessionInfo"

    /// The session the mock last opened, so a test can hand it to the
    /// window.
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

    // Every test leaves the audio system as it found it: the singletons
    // live as long as the QML engine does, and one test's broken server is
    // not the next test's business.
    function cleanup() {
        TestEngine.setAudioSystemRunning(true);
        TestEngine.reconnectAudioSystem();
        AppShell.engineInBackground = true;
        AppShell.sessionId = "";
    }

    function openSession(): string {
        TestEngine.createSession("Info tab");
        AppShell.sessionId = testCase.openedId;
        return testCase.openedId;
    }

    function test_theSidebarsInfoTabIsFilledIn() {
        const window = createTemporaryObject(mainWindow, testCase);
        verify(window.infoPane, "the Info tab has the session's own pane in it");
        window.sidebarIndex = window.sidebar.infoTab;
        compare(window.sidebar.currentIndex, 2);
        // The Files tab, between them, has the session's recordings.
        verify(window.filesPane, "the Files tab is filled in");
        compare(window.filesPane.sessionId, SessionModel.sessionId);
        compare(window.filesPane.shown, false);
        window.sidebarIndex = window.sidebar.filesTab;
        compare(window.filesPane.shown, true);
    }

    function test_theInfoTabSaysWhatTheEngineIsRunningOn() {
        // The versions are the engine's to report; the window only lays
        // them out. What matters here is that it asked and got an answer.
        const window = createTemporaryObject(mainWindow, testCase);
        verify(AudioSystem.serverName.length > 0, "the engine said what the audio system is");
        verify(AudioSystem.serverVersion.length > 0, "and which version of it");
        verify(AudioSystem.sessionManagerName.length > 0, "and what manages it");
        compare(AudioSystem.connected, true);

        // **Nothing of ours is running, and the graph still has a clock**:
        // the Info tab names the driver and shows the graph's own
        // rate and quantum, as the audio system gives them.
        compare(AppShell.hasStatus, false, "nothing of ours is running");
        compare(window.infoPane.rateText, "48 kHz");
        // The frame count in the locale's own digits, a separator and all.
        verify(/^1.?024 frames · 21\.3 ms$/.test(window.infoPane.quantumText), window.infoPane.quantumText);
        compare(AudioSystem.driver, "Built-in Audio Analogue Stereo");
        verify(/^48 kHz · 1.?024 · 21\.3 ms$/.test(window.statusBar.rateLabel.text), window.statusBar.rateLabel.text);

        // With the audio system away there is nothing to say, and a dash
        // rather than a zero that looks like a measurement.
        TestEngine.setAudioSystemRunning(false);
        tryCompare(window.infoPane, "rateText", window.infoPane.dash);
        compare(window.infoPane.quantumText, window.infoPane.dash);
        TestEngine.setAudioSystemRunning(true);
        TestEngine.reconnectAudioSystem();
        tryCompare(window.infoPane, "rateText", "48 kHz");
    }

    function test_theSessionsOwnSettingsAreTheSessionsAndNotTheWindows() {
        const window = createTemporaryObject(mainWindow, testCase);
        openSession();

        compare(SessionModel.autoConnect, true);
        SessionModel.setAutoConnect(false);
        compare(SessionModel.autoConnect, false);
        // The menu item and the Info tab are two ways to one switch.
        const sessionMenu = window.menus.menuAt(0);
        const automatic = sessionMenu.itemAt(2);
        compare(automatic.checked, false);

        SessionModel.setLatency("low");
        compare(SessionModel.latency, "low");
    }

    // **Auto Run is the session's**: the switch sets it as an edit,
    // follows it when it changes anywhere else, and a window running an
    // engine of its own says that nothing will start the session.
    function test_autoRunIsTheSessionsAndSaysWhenNothingWillStartIt() {
        const window = createTemporaryObject(mainWindow, testCase);
        openSession();
        window.sidebarIndex = window.sidebar.infoTab;
        const pane = window.infoPane;
        const autoRun = pane.autoRunSwitch;
        tryVerify(() => autoRun.visible, 1000, "the Info tab is not showing");
        compare(SessionModel.autoRun, "off");
        compare(autoRun.checked, false);

        // Switched on as a person does; offscreen, a click on a switch is
        // not delivered, so its signal is what is sent.
        autoRun.checked = true;
        autoRun.toggled();
        compare(SessionModel.autoRun, "login");

        SessionModel.setAutoRun("off");
        tryCompare(autoRun, "checked", false, 1000, "the switch did not follow the session");

        verify(!pane.autoRunNeedsBackground.visible, "said on a window that runs on the background engine");
        AppShell.engineInBackground = false;
        tryVerify(() => pane.autoRunNeedsBackground.visible, 1000,
                  "a window running its own engine said nothing about Auto Run");
    }

    function test_theNotesBoxKeepsWhatIsBeingTypedAndSendsItWhenTheCursorLeaves() {
        const window = createTemporaryObject(mainWindow, testCase);
        openSession();
        const notes = window.infoPane.notesField;
        compare(SessionModel.notes, "", "a new session has nothing written about it");

        notes.forceActiveFocus();
        verify(notes.activeFocus, "the box took the cursor");
        const written = "Two microphones.\nMixed down on the laptop.";
        notes.insert(0, written);

        // **A patch must not rewrite the box under the cursor.** Something
        // else about the session changing is exactly what would do it.
        SessionModel.renameSession("Renamed while somebody was typing");
        compare(notes.text, written, "the box was rewritten while it was being typed in");
        compare(SessionModel.notes, "", "the note went before the edit was finished");

        // Leaving the box is what ends the edit: a text area has no
        // editingFinished to end it with.
        notes.focus = false;
        compare(SessionModel.notes, written);
        compare(notes.text, written);

        // And what the engine now holds is what the box shows, so a note
        // undone elsewhere reaches it.
        TestEngine.undo(AppShell.sessionId);
        compare(SessionModel.notes, "");
        compare(notes.text, "");
    }

    function test_theStatusBarNamesTheAudioSystemAndSaysWhereItStands() {
        const window = createTemporaryObject(mainWindow, testCase);
        compare(window.footer.serverName, AudioSystem.serverName);
        compare(window.footer.connectionText, "connected");

        TestEngine.setAudioSystemRunning(false);
        compare(window.footer.connectionText, "not running");
    }

    function test_theBannerAppearsWhenTheAudioSystemGoesAndRetryBringsItBack() {
        // The criterion, walked end to end: with the audio system stopped a
        // banner appears, and after it is back the Retry in the banner is
        // what reconnects.
        const window = createTemporaryObject(mainWindow, testCase);
        compare(window.errorBanner.visible, false);
        const name = AudioSystem.serverName;

        TestEngine.setAudioSystemRunning(false);
        compare(window.errorBanner.visible, true);
        // **It names what is missing**, using the name the audio system had
        // while it was there: a server that has gone says nothing about
        // itself, and "the audio system" is a phrase somebody has to
        // translate before they can act on it.
        verify(window.errorBanner.text.indexOf(name) !== -1, "the banner says what is not running");
        compare(window.errorBanner.actions[0].visible, true);

        // A server that is back is not a connection to it: the banner stays
        // until something asks for one.
        TestEngine.setAudioSystemRunning(true);
        compare(window.errorBanner.visible, true);

        window.errorBanner.actions[0].trigger();
        compare(window.errorBanner.visible, false);
        compare(AudioSystem.connected, true);
    }

    function test_anErrorAboutTheSessionIsASentenceAndNotACode() {
        const window = createTemporaryObject(mainWindow, testCase);
        openSession();

        TestEngine.reportError("graph.cycle", "{}");
        compare(window.errorBanner.visible, true);
        verify(window.errorBanner.text.indexOf("graph.cycle") === -1,
               "the engine's code reached a reader");
        // There is nothing to try again about a loop in the wiring.
        compare(window.errorBanner.actions[0].visible, false);

        // Putting it away is the person saying they have read it.
        ErrorSurface.dismiss();
        compare(window.errorBanner.visible, false);
    }

    function test_anErrorThatNamesABlockGoesToThatBlock() {
        // The canvas is still to come, so what is checked is what it
        // will read: the badge is published, and the banner is left alone.
        const window = createTemporaryObject(mainWindow, testCase);
        openSession();

        TestEngine.reportError("block.unknownType", '{"type":"effect.nosuch","block":"b1"}');
        compare(ErrorSurface.flaggedBlocks, ["b1"]);
        verify(ErrorSurface.badge("b1").length > 0, "the tile has something to show");
        compare(window.errorBanner.visible, false);

        ErrorSurface.clearBlock("b1");
        compare(ErrorSurface.flaggedBlocks, []);
    }
}
