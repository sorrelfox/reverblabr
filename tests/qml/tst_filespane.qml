// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Files tab offscreen, against the mock engine's own recordings
// index and real files in the mock's own folder. The desktop's actions are
// answered by the harness's stand-in (TestFiles), so nothing here opens a
// file manager or reaches anybody's trash; the trash itself is exercised
// for real, into a trash of the test's own, in app.FileActionsTest.

import QtQuick
import QtTest
import org.kde.kirigami as Kirigami
import io.github.reverblabr

TestCase {
    id: testCase

    name: "FilesPane"
    when: windowShown
    width: 400
    height: 600
    // **Shown**, so that what the pane shows and hides can be asked about:
    // a TestCase is an invisible item unless it says otherwise.
    visible: true

    Component {
        id: paneComponent

        FilesPane {
            width: 280
            height: 500
        }
    }

    Component {
        id: sidebarComponent

        SidebarPane {
            width: 280
            height: 500
        }
    }

    /// A pane on a session with \a names recorded, oldest first, and their
    /// outlines read.
    function paneWith(session: string, names: var): var {
        for (const name of names) {
            verify(TestEngine.addFakeRecording(session, name, 1.0) !== "", "the mock could not write " + name);
            wait(2);
        }
        TestEngine.waitForFakeRecordings();
        const pane = createTemporaryObject(paneComponent, testCase, { sessionId: session });
        tryCompare(pane.recordings, "count", names.length, 2000, "the list did not fill");
        return pane;
    }

    /// A session id of the test's own: the index lists by id, and the mock
    /// keeps one index for the whole run.
    function freshSession(): string {
        return "files-test-" + Date.now() + "-" + Math.floor(Math.random() * 1000000);
    }

    function uniqueName(stem: string): string {
        return stem + " " + Date.now() + "-" + Math.floor(Math.random() * 100000) + ".wav";
    }

    function test_aSessionsRecordingsAreListedNewestFirstWithTheirDetails() {
        const first = testCase.uniqueName("Talk");
        const second = testCase.uniqueName("Interview");
        const pane = testCase.paneWith(testCase.freshSession(), [first, second]);
        const top = pane.recordings.get(0);
        compare(top.name, second, "the newest is not first");
        compare(top.inProgress, false);
        compare(top.format, "wav");
        fuzzyCompare(top.seconds, 1.0, 0.001);
        verify(top.sizeBytes > 0, "no size");
        compare(top.peaks.length, 48);

        // The card writes it as the design does: length · format · size.
        tryVerify(() => pane.list.itemAtIndex(0) !== null, 1000);
        const card = pane.list.itemAtIndex(0);
        compare(card.detail, "0:01 · WAV · " + pane.recordings.formatSize(top.sizeBytes));
        // The outline rises and falls, as the file does: silent at the ends,
        // loud in the middle.
        const bars = card.bars(card.barCount);
        compare(bars.length, 36);
        verify(bars[18] > 0.5, "the middle of the outline is " + bars[18]);
        verify(bars[0] < bars[18] && bars[35] < bars[18], "the outline has no shape");
    }

    function test_anotherSessionsRecordingsAreNotListed() {
        const name = testCase.uniqueName("Elsewhere");
        verify(TestEngine.addFakeRecording(testCase.freshSession(), name, 0.2) !== "");
        TestEngine.waitForFakeRecordings();
        const pane = testCase.paneWith(testCase.freshSession(), [testCase.uniqueName("Here")]);
        wait(100);
        for (let row = 0; row < pane.recordings.count; ++row) {
            verify(pane.recordings.get(row).name !== name, "a recording of another session was listed");
        }
    }

    function test_aRecordingAppearsAsItStartsAndIsFilledInWhenItFinishes() {
        const session = testCase.freshSession();
        const pane = createTemporaryObject(paneComponent, testCase, { sessionId: session });
        wait(50);
        const before = pane.recordings.count;
        const name = testCase.uniqueName("Live");
        const path = TestEngine.addFakeRecording(session, name, 0.5, true);
        tryCompare(pane.recordings, "count", before + 1, 1000, "a recording that started was not listed");
        const row = pane.recordings.rowOf(path);
        compare(pane.recordings.get(row).inProgress, true);
        tryVerify(() => pane.list.itemAtIndex(row) !== null, 1000);
        const card = pane.list.itemAtIndex(row);
        // **Highlighted as recording**: the danger dot and the word.
        compare(card.detail, "Recording · WAV");

        TestEngine.finishFakeRecording(session, path);
        TestEngine.waitForFakeRecordings();
        tryVerify(() => pane.recordings.get(pane.recordings.rowOf(path)).inProgress === false, 1000,
                  "a finished recording still says it is recording");
        fuzzyCompare(pane.recordings.get(pane.recordings.rowOf(path)).seconds, 0.5, 0.001);
    }

    function test_aFileDeletedOutsideLeavesTheListWhenTheTabComesIntoView() {
        const pane = testCase.paneWith(testCase.freshSession(), [testCase.uniqueName("Deleted")]);
        const path = pane.recordings.get(0).path;
        const count = pane.recordings.count;
        // Deleted in a file manager, behind the list's back: nothing tells
        // the engine, so the list still has it…
        verify(TestFiles.removeBehindTheListsBack(path), "the file could not be removed");
        wait(100);
        compare(pane.recordings.count, count, "the list knew without being told");
        // …until the tab comes into view, which checks the disk.
        pane.shown = true;
        tryCompare(pane.recordings, "count", count - 1, 2000, "a file that went was still listed");
        verify(pane.recordings.rowOf(path) < 0);
    }

    function test_theMenusActionsReachTheDesktopForTheChosenFile() {
        const name = testCase.uniqueName("Menu");
        const pane = testCase.paneWith(testCase.freshSession(), [name]);
        const path = pane.recordings.get(0).path;
        pane.openMenu(path, pane, 10, 10);
        compare(pane.selectedPath, path);
        compare(pane.menu.path, path);
        compare(pane.menu.count, 4, "open folder, open with, a separator and the trash");
        const before = TestFiles.count;

        pane.menu.itemAt(0).triggered();
        compare(TestFiles.lastAction, "showInFolder");
        compare(TestFiles.lastPath, path);

        pane.menu.itemAt(1).triggered();
        compare(TestFiles.lastAction, "openWith");
        compare(TestFiles.lastPath, path);

        // The trash is written in the danger colour, as the design writes a
        // row that destroys something.
        verify(pane.menu.itemAt(3) instanceof DestructiveMenuItem, "Move to Trash is not the danger row");
        const count = pane.recordings.count;
        pane.menu.itemAt(3).triggered();
        compare(TestFiles.lastAction, "moveToTrash");
        compare(TestFiles.count, before + 3);
        // The file went, the index was asked to look, and it has left the list.
        tryCompare(pane.recordings, "count", count - 1, 2000, "a trashed file stayed in the list");
        compare(pane.selectedPath, "", "a trashed file is still chosen");
    }

    function test_theKeyboardChoosesOpensAndTrashes() {
        const pane = testCase.paneWith(testCase.freshSession(),
                                       [testCase.uniqueName("KeyA"), testCase.uniqueName("KeyB")]);
        let activated = "";
        pane.recordingActivated.connect(path => activated = path);
        pane.list.forceActiveFocus();
        pane.list.currentIndex = 0;
        compare(pane.selectedPath, pane.recordings.get(0).path);
        keyClick(Qt.Key_Down);
        compare(pane.selectedPath, pane.recordings.get(1).path, "Down did not choose the next");
        keyClick(Qt.Key_Return);
        compare(activated, pane.recordings.get(1).path, "Enter did not open the chosen recording");

        const count = pane.recordings.count;
        keyClick(Qt.Key_Delete);
        compare(TestFiles.lastAction, "moveToTrash");
        tryCompare(pane.recordings, "count", count - 1, 2000);
    }

    function test_aFailedActionSaysWhatKioSaid() {
        const pane = testCase.paneWith(testCase.freshSession(), [testCase.uniqueName("Fails")]);
        const count = pane.recordings.count;
        compare(pane.failureMessage.visible, false);
        TestFiles.failNext("The file could not be moved to the trash");
        pane.trash(pane.recordings.get(0).path);
        // KIO's own words, which are already translated, and nothing left
        // the list.
        tryCompare(pane.failureMessage, "visible", true, 1000);
        compare(pane.failureMessage.text, "The file could not be moved to the trash");
        wait(100);
        compare(pane.recordings.count, count);
    }

    function test_anEmptySessionSaysWhatWouldFillIt() {
        const pane = createTemporaryObject(paneComponent, testCase, { sessionId: testCase.freshSession() });
        wait(50);
        compare(pane.recordings.count, 0);
        compare(pane.emptyMessage.visible, true);
        verify(pane.emptyMessage.text.indexOf("Recorder") >= 0, pane.emptyMessage.text);
    }

    function test_theSidebarHasTheFilesTabBetweenBlocksAndInfo() {
        const sidebar = createTemporaryObject(sidebarComponent, testCase);
        compare(sidebar.blocksTab, 0);
        compare(sidebar.filesTab, 1);
        compare(sidebar.infoTab, 2);
    }
}
