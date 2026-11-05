// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Sudden Sounds block's tile and card offscreen, against the mock engine:
// every state and every word. What the block does is in blocks.SuddenSounds.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "SuddenSounds"
    when: windowShown
    visible: true
    width: 400
    height: 700

    property string sessionId: ""

    readonly property var words: SuddenSoundsWords {}

    function panelFor() {
        const panel = createTemporaryObject(BlockRegister.panelFor("hearing.suddensounds"), testCase, { width: 380 });
        panel.blockId = "sudden";
        return panel;
    }

    function faceFor() {
        const face = createTemporaryObject(BlockRegister.faceFor("hearing.suddensounds"), testCase, { width: 200, height: 90 });
        face.blockId = "sudden";
        return face;
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Sudden sounds");
        verify(testCase.sessionId !== "", "the mock opened a session");
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "call", type: "source.application" }, index: -1 }));
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "sudden", type: "hearing.suddensounds" }, index: -1 }));
        TestEngine.applyEdit(testCase.sessionId, JSON.stringify({
            op: "connect", wire: { fromBlock: "call", fromPort: 0, toBlock: "sudden", toPort: 0 }, index: -1
        }));
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        SessionModel.setParams("sudden", [["strength", "medium"], ["ear", "left"]]);
        SessionModel.setBlockEnabled("sudden", true);
    }

    function cleanup() {
        TestEngine.setFakeBlockValues(testCase.sessionId, "sudden", []);
        TestEngine.stop(testCase.sessionId);
        Meters.watch("");
    }

    function play(values) {
        Meters.watch(testCase.sessionId);
        TestEngine.start(testCase.sessionId);
        TestEngine.setFakeBlockValues(testCase.sessionId, "sudden", values);
        tryVerify(() => Meters.live, 1000, "the session never went live");
    }

    function test_theNumbersAreTheEngines() {
        const strength = BlockCatalogue.describeParam("hearing.suddensounds", "strength");
        compare(strength.choices, testCase.words.strengths);
        compare(strength.default, "off");
        compare(BlockCatalogue.describeParam("hearing.suddensounds", "ear").default, "left");
    }

    function test_theLibraryWords() {
        compare(BlockPresentation.name("hearing.suddensounds"), "Sudden Sounds");
        compare(BlockPresentation.description("hearing.suddensounds"),
                "Softens sudden sharp sounds such as clicks and knocks");
        compare(BlockPresentation.category("hearing.suddensounds"), "hearing");
    }

    function test_runningTile_data() {
        return [
            { tag: "just added", strength: "off", softened: 0, kind: "notSet", dot: "off", headline: "Off", caption: "L" },
            { tag: "ready", strength: "medium", softened: 0, kind: "ready", dot: "ok", headline: "0 dB", caption: "L · Medium" },
            { tag: "gentle", strength: "gentle", softened: 0, kind: "ready", dot: "ok", headline: "0 dB", caption: "L · Gentle" },
            { tag: "softening", strength: "medium", softened: 11.2, kind: "softening", dot: "live", headline: "-11 dB", caption: "L · Medium" },
            { tag: "strong", strength: "strong", softened: 15, kind: "softening", dot: "live", headline: "-15 dB", caption: "L · Strong" }
        ];
    }

    function test_runningTile(row) {
        SessionModel.setParam("sudden", "strength", row.strength);
        const face = testCase.faceFor();
        testCase.play([row.softened, 0, 0, 0]);
        tryCompare(face, "kind", row.kind, 1000);
        compare(face.dot, row.dot);
        compare(face.headlineLabel.text, row.headline);
        compare(face.captionLabel.text, row.caption);
    }

    function test_theCaptionFollowsTheEar() {
        SessionModel.setParam("sudden", "ear", "right");
        const face = testCase.faceFor();
        testCase.play([0, 0, 0, 0]);
        tryCompare(face.captionLabel, "text", "R · Medium", 1000);
    }

    function test_softeningIsInTheLiveColour() {
        const face = testCase.faceFor();
        testCase.play([9, 0, 0, 0]);
        tryCompare(face, "kind", "softening", 1000);
        compare(face.captionLabel.color, AppColors.liveText);
    }

    function test_stoppedSaysNotLive() {
        const face = testCase.faceFor();
        compare(face.kind, "stopped");
        compare(face.dot, "off");
        compare(face.headlineLabel.text, "Not live");
        verify(!face.captionLabel.visible, "a stopped tile shows a caption");
    }

    function test_strengthOffOnAStoppedSessionIsOff() {
        SessionModel.setParam("sudden", "strength", "off");
        const face = testCase.faceFor();
        compare(face.kind, "notSet");
        compare(face.headlineLabel.text, "Off");
        compare(face.captionLabel.text, "L");
    }

    function test_blockSwitchedOffSaysOffAndGoesMuted() {
        SessionModel.setBlockEnabled("sudden", false);
        const face = testCase.faceFor();
        testCase.play([9, 3, 0, 0]);
        tryCompare(face, "kind", "off", 1000);
        compare(face.dot, "off");
        compare(face.headlineLabel.text, "Off");
        verify(!face.captionLabel.visible, "a block that is off shows a caption");
        compare(face.tint, AppColors.muted);
    }

    function test_theCardStartsWithItsEar() {
        const panel = testCase.panelFor();
        verify(panel.earChoice.visible);
        compare(panel.earChoice.Accessible.name, "Ear");
        compare(panel.earChoice.choices.count, 2);
    }

    function test_strengthIsFourChoices() {
        const panel = testCase.panelFor();
        compare(panel.strengthChoices.count, 4);
        compare(panel.strengthChoices.itemAt(0).text, "Off");
        compare(panel.strengthChoices.itemAt(1).text, "Gentle");
        compare(panel.strengthChoices.itemAt(2).text, "Medium");
        compare(panel.strengthChoices.itemAt(3).text, "Strong");
        tryVerify(() => panel.strengthChoices.itemAt(2).checked, 1000, "Medium is not shown as chosen");
        tryVerify(() => panel.strengthChoices.itemAt(3).width > 0, 1000);
        mouseClick(panel.strengthChoices.itemAt(3));
        tryCompare(panel.reading, "strength", "strong", 1000);
        compare(SessionModel.param("sudden", "strength"), "strong");
        tryVerify(() => panel.strengthChoices.itemAt(3).checked, 1000);
    }

    function test_cardStatus_data() {
        return [
            { tag: "ready", values: [0, 0, 0, 0], status: "Ready", softened: "0 dB", count: "Softened in the last minute: 0 times" },
            { tag: "softening", values: [11, 14, 0, 0], status: "Softening now", softened: "11 dB", count: "Softened in the last minute: 14 times" },
            { tag: "once", values: [6, 1, 0, 0], status: "Softening now", softened: "6 dB", count: "Softened in the last minute: 1 time" }
        ];
    }

    function test_cardStatus(row) {
        const panel = testCase.panelFor();
        testCase.play(row.values);
        tryCompare(panel.statusLabel, "text", row.status, 1000);
        compare(panel.softenedLabel.text, row.softened);
        verify(panel.countLabel.visible);
        compare(panel.countLabel.text, row.count);
        compare(panel.softenedBar.to, testCase.words.mostCut);
        compare(panel.softenedBar.db, row.values[0]);
        compare(panel.softenedBar.Accessible.name, "Softened now");
    }

    function test_notLiveOnTheCard() {
        const panel = testCase.panelFor();
        compare(panel.statusLabel.text, "Not live");
        compare(panel.softenedLabel.text, "Not live");
        verify(!panel.countLabel.visible, "a stopped card shows a count");
    }

    function test_offOnTheCard() {
        SessionModel.setParam("sudden", "strength", "off");
        const panel = testCase.panelFor();
        compare(panel.statusLabel.text, "Off");
    }

    function test_theStandingLines() {
        const panel = testCase.panelFor();
        compare(panel.infoLine.text, "Leave this off for music: it softens drums and plucked notes too");
        compare(panel.placingLabel.text,
                "Put this before any block that moves or lifts sound. It delays the sound by less than a thousandth of a second");
    }

    function test_itIsNeverCalledALimitOrASlam() {
        const panel = testCase.panelFor();
        const seen = [BlockPresentation.description("hearing.suddensounds"), BlockPresentation.tooltip("hearing.suddensounds"),
                      panel.infoLine.text, panel.placingLabel.text].join(" ").toLowerCase();
        verify(seen.indexOf("limit") < 0, seen);
        verify(seen.indexOf("slam") < 0, seen);
    }
}
