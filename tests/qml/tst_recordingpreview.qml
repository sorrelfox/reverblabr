// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Listening to a recording from the Files tab, offscreen against
// the mock's pretend preview: the button, Space and a click on the outline
// ask the engine to play, pause and seek, and the card shows how far it
// has got. That the engine's preview plays to the default output and
// leaves a running session alone is measured in pw.preview.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "RecordingPreview"
    when: windowShown
    width: 400
    height: 600
    visible: true

    Component {
        id: paneComponent

        FilesPane {
            width: 280
            height: 500
        }
    }

    function freshSession(): string {
        return "preview-test-" + Date.now() + "-" + Math.floor(Math.random() * 1000000);
    }

    /// A pane with one two-second recording, chosen, and its card.
    function paneWithOne(): var {
        const session = testCase.freshSession();
        const path = TestEngine.addFakeRecording(session, "Take " + Date.now() + ".wav", 2.0);
        verify(path !== "", "the mock could not write a recording");
        TestEngine.waitForFakeRecordings();
        const pane = createTemporaryObject(paneComponent, testCase, { sessionId: session });
        tryCompare(pane.recordings, "count", 1, 2000);
        pane.select(path);
        tryVerify(() => pane.list.itemAtIndex(0) !== null, 1000);
        return { pane: pane, card: pane.list.itemAtIndex(0), path: path };
    }

    function cleanup() {
        RecordingPreview.stop();
    }

    function test_thePlayButtonListensAndPauses() {
        const made = testCase.paneWithOne();
        const card = made.card;
        verify(card.playButton.visible, "the chosen card has no play button");
        compare(card.playButton.text, "Play");

        card.playButton.clicked();
        tryCompare(RecordingPreview, "playing", true, 1000, "the button did not start the preview");
        compare(RecordingPreview.path, made.path);
        fuzzyCompare(RecordingPreview.duration, 2.0, 0.001);
        compare(card.previewing, true);
        compare(card.playButton.text, "Pause");
        tryVerify(() => RecordingPreview.seconds > 0.2, 2000, "the preview did not move");
        verify(card.played > 0, "the card does not show how far it has got");

        card.playButton.clicked();
        tryCompare(RecordingPreview, "paused", true, 1000, "the button did not pause");
        const held = RecordingPreview.seconds;
        wait(300);
        compare(RecordingPreview.seconds, held, "a paused preview moved");
        compare(card.playButton.text, "Play");
        verify(card.Accessible.description.indexOf("paused") >= 0, card.Accessible.description);
    }

    function test_spaceListensToTheChosenRecording() {
        const made = testCase.paneWithOne();
        made.pane.list.forceActiveFocus();
        keyClick(Qt.Key_Space);
        tryCompare(RecordingPreview, "playing", true, 1000, "Space did not start the preview");
        compare(RecordingPreview.path, made.path);
        keyClick(Qt.Key_Space);
        tryCompare(RecordingPreview, "paused", true, 1000, "Space did not pause it");
    }

    function test_aClickOnTheOutlineListensFromThere() {
        const made = testCase.paneWithOne();
        const outline = made.card.outline;
        // Three quarters of the way along a file that is not playing yet:
        // it starts, and moves there.
        mouseClick(outline, outline.width * 0.75, outline.height / 2);
        tryCompare(RecordingPreview, "playing", true, 1000);
        tryVerify(() => Math.abs(RecordingPreview.seconds - 1.5) < 0.2, 1000,
                  "the preview is at " + RecordingPreview.seconds + " s, not 1.5");
        // And back to a quarter.
        mouseClick(outline, outline.width * 0.25, outline.height / 2);
        tryVerify(() => Math.abs(RecordingPreview.seconds - 0.5) < 0.2, 1000);
        // The played part of the outline is drawn in the accent.
        verify(made.card.played > 0.2 && made.card.played < 0.4, "played " + made.card.played);
    }

    function test_aPreviewStopsAtTheEndOfItsFile() {
        const made = testCase.paneWithOne();
        RecordingPreview.seekTo(made.path, 0.9);
        tryCompare(RecordingPreview, "playing", true, 1000);
        tryCompare(RecordingPreview, "playing", false, 2000, "the preview ran past its end");
        compare(RecordingPreview.path, "", "a finished preview still names its file");
        compare(made.card.previewing, false);
    }

    function test_aRecordingBeingListenedToIsLetGoOfBeforeItIsTrashed() {
        const made = testCase.paneWithOne();
        RecordingPreview.play(made.path);
        tryCompare(RecordingPreview, "playing", true, 1000);
        made.pane.trash(made.path);
        tryCompare(RecordingPreview, "path", "", 1000, "the preview went on playing a trashed file");
        tryCompare(made.pane.recordings, "count", 0, 2000);
    }

    function test_aRecordingStillBeingWrittenHasNothingToPlay() {
        const session = testCase.freshSession();
        verify(TestEngine.addFakeRecording(session, "Live " + Date.now() + ".wav", 0.5, true) !== "");
        const pane = createTemporaryObject(paneComponent, testCase, { sessionId: session });
        tryCompare(pane.recordings, "count", 1, 2000);
        pane.select(pane.recordings.get(0).path);
        tryVerify(() => pane.list.itemAtIndex(0) !== null, 1000);
        compare(pane.list.itemAtIndex(0).playButton.visible, false);
    }
}
