// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Recording Inspector offscreen, against the mock's recordings
// index and real files in the mock's own folder: the outline from the
// index's peaks, listening and seeking through the preview, renaming the
// file on the disk, writing its tags into it, and both showing in the
// Files tab. The desktop's actions go to the harness's stand-in.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "RecordingInspector"
    when: windowShown

    Component {
        id: inspectorComponent

        RecordingInspectorWindow {}
    }

    Component {
        id: mainWindow

        Main {}
    }

    Component {
        id: listComponent

        RecordingList {}
    }

    function freshSession(): string {
        return "inspector-test-" + Date.now() + "-" + Math.floor(Math.random() * 1000000);
    }

    /// A two-second recording of a fresh session, its outline read, and an
    /// inspector open on it.
    function inspectOne(): var {
        const session = testCase.freshSession();
        const name = "Take " + Date.now() + "-" + Math.floor(Math.random() * 100000);
        const path = TestEngine.addFakeRecording(session, name + ".wav", 2.0);
        verify(path !== "", "the mock could not write a recording");
        TestEngine.waitForFakeRecordings();
        const window = createTemporaryObject(inspectorComponent, testCase, { path: path });
        window.show();
        tryCompare(window.inspector, "loaded", true, 2000, "the engine never said what the file is");
        return { window: window, path: path, session: session, name: name };
    }

    function cleanup() {
        RecordingPreview.stop();
    }

    function test_itShowsWhatTheFileIsAndItsOutline() {
        const made = testCase.inspectOne();
        const window = made.window;
        compare(window.inspector.baseName, made.name);
        compare(window.inspector.extension, "wav");
        compare(window.title, made.name + ".wav");
        fuzzyCompare(window.inspector.seconds, 2.0, 0.001);
        compare(window.timeLabel.text, "0:00 / 0:02");
        verify(window.formatLabel.text.startsWith("WAV · 48 kHz · Stereo · "), window.formatLabel.text);

        // **The outline is the file's**: 512 peaks from the index, drawn as
        // as many bars as there is room for, and they rise and fall as the
        // mock's tone does — quiet at the ends, loud in the middle.
        compare(window.inspector.peaks.length, 512);
        const bars = window.waveform.bars();
        verify(bars.length > 50, "only " + bars.length + " bars");
        compare(bars.length, window.waveform.barCount);
        const middle = bars[Math.floor(bars.length / 2)];
        verify(middle > 0.7, "the middle of the outline is " + middle);
        verify(bars[0] < 0.1 && bars[bars.length - 1] < 0.1, "the ends are " + bars[0] + " and " + bars[bars.length - 1]);
    }

    function test_itListensAndAClickOnTheOutlineSeeksThere() {
        const made = testCase.inspectOne();
        const window = made.window;
        window.playButton.clicked();
        tryCompare(RecordingPreview, "playing", true, 1000);
        compare(RecordingPreview.path, made.path);
        compare(window.playButton.text, "Pause");
        tryVerify(() => window.waveform.progress > 0, 2000, "the outline did not show the listening");

        // A click at a quarter of the outline listens from half a second in.
        mouseClick(window.waveform, window.waveform.width * 0.25, window.waveform.height / 2);
        tryVerify(() => Math.abs(RecordingPreview.seconds - 0.5) < 0.1, 1000,
                  "the preview is at " + RecordingPreview.seconds + " s, not 0.5");
        compare(window.waveform.fractionAt(window.waveform.width * 0.25), 0.25);

        window.playButton.clicked();
        tryCompare(RecordingPreview, "paused", true, 1000);
    }

    function test_theNameRenamesTheFileAndTheFilesTabShowsIt() {
        const made = testCase.inspectOne();
        const window = made.window;
        const list = createTemporaryObject(listComponent, testCase, { sessionId: made.session });
        tryCompare(list, "count", 1, 2000);

        const renamed = made.name + " renamed";
        window.nameField.text = renamed;
        window.nameField.editingFinished();
        tryCompare(window.inspector, "baseName", renamed, 2000, "the file was not renamed");
        compare(window.inspector.extension, "wav", "the extension was not kept");
        verify(window.inspector.path.endsWith("/" + renamed + ".wav"), window.inspector.path);
        compare(window.title, renamed + ".wav");
        tryCompare(window.inspector, "loaded", true, 1000);

        // The Files tab's list follows it.
        tryVerify(() => list.count === 1 && list.get(0).name === renamed + ".wav", 2000,
                  "the Files tab still has the old name");
    }

    function test_aNameAnotherFileHasIsRefusedAndSaysSo() {
        const first = testCase.inspectOne();
        const otherName = "Other " + Date.now();
        const other = TestEngine.addFakeRecording(first.session, otherName + ".wav", 0.5);
        TestEngine.waitForFakeRecordings();
        verify(other !== "");

        first.window.nameField.text = otherName;
        first.window.nameField.editingFinished();
        tryVerify(() => first.window.inspector.refusal !== "", 2000, "nothing said the rename was refused");
        verify(first.window.refusedMessage.text.indexOf("another file already has that name") >= 0,
               first.window.refusedMessage.text);
        compare(first.window.inspector.path, first.path, "the file moved anyway");
    }

    function test_tagsAreSavedIntoTheFileAndShowInTheFilesTab() {
        const made = testCase.inspectOne();
        const window = made.window;
        const list = createTemporaryObject(listComponent, testCase, { sessionId: made.session });
        tryCompare(list, "count", 1, 2000);
        compare(window.saveTagsButton.enabled, false, "nothing edited, and Save Tags is offered");

        window.setTag("title", "Friday show — ep. 41");
        window.setTag("artist", "Reverblabr FM");
        window.setTag("year", "2026");
        compare(window.saveTagsButton.enabled, true);
        window.saveTagsButton.clicked();

        // What the file holds now, read back by the engine.
        tryCompare(window.inspector.tags, "title", "Friday show — ep. 41", 2000);
        compare(window.inspector.tags.artist, "Reverblabr FM");
        compare(window.inspector.tags.year, "2026");
        compare(window.saveTagsButton.enabled, false, "saved tags still read as edited");
        tryVerify(() => (list.get(0).tags ?? {}).title === "Friday show — ep. 41", 2000,
                  "the Files tab does not have the tags");

        // And a second inspector on the same file reads them from the file.
        const again = createTemporaryObject(inspectorComponent, testCase, { path: window.inspector.path });
        tryCompare(again.inspector, "loaded", true, 2000);
        compare(again.inspector.tags.artist, "Reverblabr FM");
    }

    function test_theActionsReachTheDesktopAndATrashedFileSaysSo() {
        const made = testCase.inspectOne();
        const window = made.window;
        const before = TestFiles.count;
        FileActions.showInFolder(window.inspector.path, window);
        compare(TestFiles.lastAction, "showInFolder");
        compare(TestFiles.lastPath, made.path);

        FileActions.moveToTrash(window.inspector.path, window);
        compare(TestFiles.lastAction, "moveToTrash");
        tryCompare(window.inspector, "gone", true, 2000, "the window did not notice its file had gone");
        compare(window.goneMessage.visible, true);
        compare(window.playButton.enabled, false);
        compare(TestFiles.count, before + 2);
    }

    function test_theMainWindowOpensOneInspectorAFile() {
        const session = testCase.freshSession();
        const first = TestEngine.addFakeRecording(session, "A " + Date.now() + ".wav", 0.5);
        const second = TestEngine.addFakeRecording(session, "B " + Date.now() + ".wav", 0.5);
        TestEngine.waitForFakeRecordings();
        const main = createTemporaryObject(mainWindow, testCase);

        const opened = main.openInspector(first);
        verify(opened !== null);
        compare(main.openInspector(first), opened, "a second window for the same file");
        const other = main.openInspector(second);
        verify(other !== opened, "one window for two files");
        compare(main.inspectors.length, 2);

        opened.close();
        tryCompare(main.inspectors, "length", 1, 1000);
        other.close();
        tryCompare(main.inspectors, "length", 0, 1000);
    }
}
