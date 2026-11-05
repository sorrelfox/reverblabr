// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Voice Forward block's tile and card offscreen, against the mock engine:
// every state and every word, the slider on the tile, and Hold for before, by
// the button, by B and by Enter. What the block does is in blocks.VoiceForward.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "VoiceForward"
    when: windowShown
    visible: true
    width: 400
    height: 700

    property string sessionId: ""

    readonly property var words: VoiceForwardWords {}

    function panelFor() {
        const panel = createTemporaryObject(BlockRegister.panelFor("effect.voiceforward"), testCase, { width: 380 });
        panel.blockId = "forward";
        return panel;
    }

    function faceFor() {
        const face = createTemporaryObject(BlockRegister.faceFor("effect.voiceforward"), testCase, { width: 200, height: 90 });
        face.blockId = "forward";
        return face;
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Voice forward");
        verify(testCase.sessionId !== "", "the mock opened a session");
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "film", type: "source.application" }, index: -1 }));
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "forward", type: "effect.voiceforward" }, index: -1 }));
        TestEngine.applyEdit(testCase.sessionId, JSON.stringify({
            op: "connect", wire: { fromBlock: "film", fromPort: 0, toBlock: "forward", toPort: 0 }, index: -1
        }));
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        SessionModel.setParams("forward", [["background", -12], ["betweenVoices", -3], ["holdLevel", true]]);
        SessionModel.setBlockEnabled("forward", true);
    }

    function cleanup() {
        TestEngine.setFakeBlockValues(testCase.sessionId, "forward", []);
        TestEngine.stop(testCase.sessionId);
        Meters.watch("");
    }

    function play(values) {
        Meters.watch(testCase.sessionId);
        TestEngine.start(testCase.sessionId);
        TestEngine.setFakeBlockValues(testCase.sessionId, "forward", values);
        tryVerify(() => Meters.live, 1000, "the session never went live");
    }

    function test_theNumbersAreTheEngines() {
        const background = BlockCatalogue.describeParam("effect.voiceforward", "background");
        compare(background.minimum, testCase.words.lowestCut);
        compare(background.maximum, 0);
        compare(background.default, 0);
        const between = BlockCatalogue.describeParam("effect.voiceforward", "betweenVoices");
        compare(between.minimum, testCase.words.lowestCut);
        compare(between.default, testCase.words.defaultBetween);
        compare(BlockCatalogue.describeParam("effect.voiceforward", "holdLevel").default, true);
    }

    function test_tileStates_data() {
        const w = testCase.words;
        return [
            { tag: "not set", background: 0, found: w.wide, kind: "notSet", dot: "ok", headline: "0 dB", caption: "background" },
            { tag: "centre", background: -12, found: w.centre, kind: "centre", dot: "live", headline: "-12 dB", caption: "background" },
            { tag: "between", background: -12, found: w.wide, kind: "between", dot: "ok", headline: "-12 dB", caption: "background · -3 dB now" },
            { tag: "alike", background: -12, found: w.alike, kind: "alike", dot: "warning", headline: "-12 dB", caption: "both sides alike" },
            { tag: "quiet", background: -12, found: w.quiet, kind: "quiet", dot: "ok", headline: "-12 dB", caption: "background" }
        ];
    }

    function test_tileStates(row) {
        SessionModel.setParam("forward", "background", row.background);
        const face = testCase.faceFor();
        testCase.play([row.background === 0 ? 0 : -4, row.found, 0, 0]);
        tryCompare(face, "kind", row.kind, 1000);
        compare(face.dot, row.dot);
        compare(face.headlineLabel.text, row.headline);
        compare(face.captionLabel.text, row.caption);
        compare(face.slider.value, row.background);
    }

    function test_stoppedShowsTheSettingAndNothingLive() {
        const face = testCase.faceFor();
        compare(face.kind, "stopped");
        compare(face.dot, "off");
        compare(face.headlineLabel.text, "-12 dB");
        compare(face.captionLabel.text, "background");
    }

    function test_switchedOffSaysOff() {
        SessionModel.setBlockEnabled("forward", false);
        const face = testCase.faceFor();
        tryCompare(face, "kind", "off", 1000);
        compare(face.headlineLabel.text, "Off");
        verify(!face.captionLabel.visible);
        compare(face.tint, AppColors.muted);
    }

    function test_theCutShownIsBetweenVoicesStoppedAtBackground() {
        SessionModel.setParams("forward", [["background", -6], ["betweenVoices", -18]]);
        const face = testCase.faceFor();
        testCase.play([0, testCase.words.wide, 0, 0]);
        tryCompare(face.captionLabel, "text", "background · -6 dB now", 1000);
    }

    function test_theTileSliderIsBackground() {
        const face = testCase.faceFor();
        face.slider.forceActiveFocus();
        keyClick(Qt.Key_Right);
        tryCompare(face.reading, "background", -11, 1000);
        keyClick(Qt.Key_PageDown);
        tryCompare(face.reading, "background", -14, 1000);
        keyClick(Qt.Key_PageUp);
        tryCompare(face.reading, "background", -11, 1000);
        verify(face.slider.Accessible.name !== "");
    }

    function test_statusWords_data() {
        const w = testCase.words;
        return [
            { tag: "not set", background: 0, found: w.wide, want: "Not set. Sound passes unchanged" },
            { tag: "working", background: -12, found: w.centre, want: "Working" },
            { tag: "between", background: -12, found: w.wide, want: "Working" },
            { tag: "alike", background: -12, found: w.alike, want: "Nothing to move: both sides alike" }
        ];
    }

    function test_statusWords(row) {
        SessionModel.setParam("forward", "background", row.background);
        const panel = testCase.panelFor();
        testCase.play([0, row.found, 0, 0]);
        tryCompare(panel.statusLabel, "text", row.want, 1000);
    }

    function test_stoppedIsNotLive() {
        const panel = testCase.panelFor();
        compare(panel.statusLabel.text, "Not running");
        compare(panel.takenOutLabel.text, "Not live");
        compare(panel.foundLabel.text, "Not live");
        verify(!panel.holdLabel.visible);
        verify(!panel.delayLabel.visible);
    }

    function test_foundWords_data() {
        const w = testCase.words;
        return [
            { tag: "centre", found: w.centre, want: "Centre sound" },
            { tag: "wide", found: w.wide, want: "Wide sound" },
            { tag: "alike", found: w.alike, want: "Both sides alike" },
            { tag: "quiet", found: w.quiet, want: "Quiet" }
        ];
    }

    function test_foundWords(row) {
        const panel = testCase.panelFor();
        testCase.play([0, row.found, 0, 0]);
        tryCompare(panel.foundLabel, "text", row.want, 1000);
    }

    // Nothing taken out is an empty bar, not a full one.
    function test_takenOutFillsAsMoreIsTakenOut() {
        const panel = testCase.panelFor();
        SessionModel.setParam("forward", "background", -6);
        testCase.play([0, testCase.words.centre, 0, 0]);
        tryCompare(panel.takenOutBar, "filled", 0, 1000);
        fuzzyCompare(panel.backgroundMark.x + panel.backgroundMark.width / 2, panel.takenOutBar.width / 4, 0.51);
        testCase.play([-12, testCase.words.centre, 0, 0]);
        tryCompare(panel.takenOutBar, "filled", 0.5, 1000);
    }

    function test_takenOutIsShownAndItsLineStandsAtBackground() {
        const panel = testCase.panelFor();
        testCase.play([-7.2, testCase.words.centre, 0, 0]);
        tryCompare(panel.takenOutLabel, "text", "-7 dB", 1000);
        compare(panel.takenOutBar.from, -24);
        verify(panel.backgroundMark.visible);
        fuzzyCompare(panel.backgroundMark.x + panel.backgroundMark.width / 2, panel.takenOutBar.width / 2, 0.51);
        SessionModel.setParam("forward", "background", 0);
        tryVerify(() => !panel.backgroundMark.visible, 1000);
    }

    function test_holdLevelLines() {
        const panel = testCase.panelFor();
        testCase.play([-6, testCase.words.centre, 3, 0]);
        tryCompare(panel.holdLabel, "text", "Lifting +3 dB", 1000);
        verify(panel.holdBox.checked);
        verify(!panel.beforeLabel.visible);

        TestEngine.setFakeBlockValues(testCase.sessionId, "forward", [-6, testCase.words.centre, 3, 1.5]);
        tryCompare(panel.holdLabel, "text", "Lifting +3 dB. Limit acting", 1000);

        panel.holdBox.toggle();
        panel.holdBox.toggled();
        tryCompare(panel.reading, "holdLevel", false, 1000);
        verify(!panel.holdLabel.visible);
        tryCompare(panel.beforeLabel, "text", "Before is 6 dB louder than this", 1000);
    }

    function test_betweenVoicesFoldsWithItsValueOnTheRow() {
        const panel = testCase.panelFor();
        verify(!panel.betweenSection.expanded);
        compare(panel.betweenSection.note, "-3 dB");
        verify(panel.explanationLine.text.indexOf("can't tell a voice") >= 0);
    }

    function test_theDelayIsShown() {
        const words = testCase.words;
        compare(words.delayLine(22.83), "Adds 22.83 ms to this lane");
    }

    function test_holdForBeforeByTheButton() {
        const panel = testCase.panelFor();
        testCase.play([0, testCase.words.centre, 0, 0]);
        const button = panel.beforeButton;
        verify(!panel.beforeHeldLabel.visible);
        mousePress(button);
        tryVerify(() => TestEngine.heldOff(testCase.sessionId, "forward"), 1000);
        verify(panel.beforeHeldLabel.visible);
        compare(panel.beforeHeldLabel.text, "Hearing the sound before this block");
        mouseRelease(button);
        tryVerify(() => !TestEngine.heldOff(testCase.sessionId, "forward"), 1000);
        verify(!panel.beforeHeldLabel.visible);
    }

    function test_holdForBeforeByTheBKeyAnywhereInTheCard() {
        const panel = testCase.panelFor();
        testCase.play([0, testCase.words.centre, 0, 0]);
        panel.holdBox.forceActiveFocus();
        keyPress(Qt.Key_B);
        tryVerify(() => TestEngine.heldOff(testCase.sessionId, "forward"), 1000);
        keyRelease(Qt.Key_B);
        tryVerify(() => !TestEngine.heldOff(testCase.sessionId, "forward"), 1000);
    }

    function test_theKeyDoesNotOutliveTheFocus() {
        const panel = testCase.panelFor();
        testCase.play([0, testCase.words.centre, 0, 0]);
        panel.holdBox.forceActiveFocus();
        keyPress(Qt.Key_B);
        tryVerify(() => TestEngine.heldOff(testCase.sessionId, "forward"), 1000);
        panel.beforeButton.forceActiveFocus();
        tryVerify(() => !TestEngine.heldOff(testCase.sessionId, "forward"), 500, "the release went elsewhere");
        keyRelease(Qt.Key_B);
    }

    function test_enterKeepsItOnUntilTheNextEnter() {
        const panel = testCase.panelFor();
        testCase.play([0, testCase.words.centre, 0, 0]);
        panel.beforeButton.forceActiveFocus();
        keyClick(Qt.Key_Return);
        tryVerify(() => TestEngine.heldOff(testCase.sessionId, "forward"), 1000);
        verify(panel.beforeButton.kept);
        keyClick(Qt.Key_Return);
        tryVerify(() => !TestEngine.heldOff(testCase.sessionId, "forward"), 1000);
    }

    function test_aClosedCardLetsGo() {
        const panel = testCase.panelFor();
        testCase.play([0, testCase.words.centre, 0, 0]);
        panel.beforeButton.forceActiveFocus();
        keyClick(Qt.Key_Return);
        tryVerify(() => TestEngine.heldOff(testCase.sessionId, "forward"), 1000);
        panel.destroy();
        tryVerify(() => !TestEngine.heldOff(testCase.sessionId, "forward"), 1000);
    }

    function test_nothingShownSaysVoice() {
        for (const found of [0, 1, 2, 3]) {
            verify(testCase.words.foundName(found).toLowerCase().indexOf("voice") < 0);
        }
    }

    function test_stillChangingTheSoundIsNotNotSet_data() {
        return [
            { tag: "lift left", values: [0, testCase.words.wide, 5, 0] },
            { tag: "cut coming back", values: [-3, testCase.words.wide, 0, 0] }
        ];
    }

    function test_stillChangingTheSoundIsNotNotSet(row) {
        SessionModel.setParam("forward", "background", 0);
        const panel = testCase.panelFor();
        const face = testCase.faceFor();
        testCase.play(row.values);
        tryCompare(face, "kind", "settling", 1000);
        compare(face.dot, "live");
        compare(face.captionLabel.text, "background");
        compare(panel.statusLabel.text, "Working");

        TestEngine.setFakeBlockValues(testCase.sessionId, "forward", [0, testCase.words.wide, 0, 0]);
        tryCompare(face, "kind", "notSet", 1000);
        compare(panel.statusLabel.text, "Not set. Sound passes unchanged");
    }

    function test_aCardHandedAnotherBlockLetsGoOfTheFirst() {
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "other", type: "effect.voiceforward" }, index: -1 }));
        tryVerify(() => SessionModel.blockIds.indexOf("other") >= 0, 1000, "the block did not arrive");
        const panel = testCase.panelFor();
        testCase.play([0, testCase.words.centre, 0, 0]);
        panel.beforeButton.forceActiveFocus();
        keyClick(Qt.Key_Return);
        tryVerify(() => TestEngine.heldOff(testCase.sessionId, "forward"), 1000);

        panel.blockId = "other";
        tryVerify(() => !TestEngine.heldOff(testCase.sessionId, "forward"), 1000, "the first block stayed held");
        verify(!TestEngine.heldOff(testCase.sessionId, "other"));
        verify(!panel.beforeHeldLabel.visible);
        verify(!panel.beforeButton.kept);
        SessionModel.removeBlock("other");
    }
}
