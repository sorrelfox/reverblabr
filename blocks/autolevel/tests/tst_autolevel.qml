// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Auto Level's face and panel offscreen, against the mock engine: the
// switches and the advanced settings write the block's parameters, the ranges
// are the engine's, the table agrees with the engine's rule, every word of the
// tile is reached, and a program's own word for music reaches the card.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "AutoLevel"
    when: windowShown

    property string sessionId: ""

    function panelFor() {
        const panel = createTemporaryObject(BlockPanels.panelFor("effect.autolevel"), testCase, { width: 320 });
        panel.blockId = "al";
        return panel;
    }

    function faceFor() {
        const face = createTemporaryObject(BlockFaces.faceFor("effect.autolevel"), testCase,
                                           { width: 220, height: 120 });
        face.blockId = "al";
        return face;
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Auto Level");
        verify(testCase.sessionId !== "", "the mock opened a session");
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "al", type: "effect.autolevel" },
                                              index: -1 }));
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        SessionModel.setParam("al", "boost", true);
        SessionModel.setParam("al", "cut", false);
        SessionModel.setParam("al", "target", -20);
        SessionModel.setParam("al", "maximumBoost", 18);
        SessionModel.setParam("al", "maximumCut", 18);
        SessionModel.setParam("al", "band", 0);
        SessionModel.setBlockEnabled("al", true);
        AutoLevelHistory.forget("al");
        Announcer.reset();
    }

    function cleanup() {
        TestEngine.setFakeBlockValues(testCase.sessionId, "al", []);
        TestEngine.clearFakeLevels(testCase.sessionId);
        TestEngine.stop(testCase.sessionId);
        Meters.watch("");
        TestEngine.getInventory();
        SessionModel.removeBlocks(SessionModel.blockIds.filter(id => id !== "al"));
    }

    /// Runs the session with a steady signal of \a peak through the block.
    function play(peak: real) {
        Meters.watch(testCase.sessionId);
        TestEngine.start(testCase.sessionId);
        TestEngine.setFakeLevel(testCase.sessionId, "al", peak);
        tryVerify(() => Meters.live && (Meters.levels("al").active ?? false), 1000, "the block never went live");
    }

    /// The peak of a sine that reads \a lufs.
    function peakOf(lufs: real): real {
        return Math.pow(10, lufs / 20) * Math.SQRT2;
    }

    /// Runs the block with values no steady level gives.
    function playValues(values: var) {
        testCase.play(testCase.peakOf(-30));
        TestEngine.setFakeBlockValues(testCase.sessionId, "al", values);
    }

    function test_theBlockHasAFaceAndAPanelOfItsOwn() {
        verify(BlockPanels.panelFor("effect.autolevel") !== null, "Auto Level is in the panel register");
        verify(BlockFaces.faceFor("effect.autolevel") !== null, "Auto Level is in the face register");
    }

    function test_theRangesAreTheEnginesOwn() {
        // Written down on both sides of the layer boundary; this keeps them
        // together.
        const panel = testCase.panelFor();
        const words = panel.words;
        const target = BlockCatalogue.describeParam("effect.autolevel", "target");
        const maximum = BlockCatalogue.describeParam("effect.autolevel", "maximumBoost");
        const cutLimit = BlockCatalogue.describeParam("effect.autolevel", "maximumCut");
        const band = BlockCatalogue.describeParam("effect.autolevel", "band");
        compare(words.defaultTarget, target.default);
        compare(words.lowestTarget, target.minimum);
        compare(words.highestTarget, target.maximum);
        compare(words.defaultMaximum, maximum.default);
        compare(words.highestMaximum, maximum.maximum);
        compare(maximum.minimum, 0);
        compare(words.defaultMaximumCut, cutLimit.default);
        compare(words.highestMaximumCut, cutLimit.maximum);
        compare(cutLimit.minimum, 0);
        compare(band.default, 0);
        compare(band.minimum, 0);
        compare(words.highestBand, band.maximum);
        compare(BlockCatalogue.describeParam("effect.autolevel", "boost").default, true);
        compare(BlockCatalogue.describeParam("effect.autolevel", "cut").default, false);
        compare(panel.targetRow.slider.from, target.minimum);
        compare(panel.targetRow.slider.to, target.maximum);
        compare(panel.maximumRow.slider.to, maximum.maximum);
        compare(panel.maximumCutRow.slider.to, cutLimit.maximum);
        compare(panel.bandRow.slider.to, band.maximum);
    }

    function test_eachSwitchWritesAndGreysItsOwn() {
        const panel = testCase.panelFor();
        verify(panel.boostSwitch.checked, "a new Auto Level lifts");
        verify(!panel.cutSwitch.checked, "a new Auto Level brings nothing down");
        verify(panel.targetRow.enabled);
        verify(panel.maximumRow.enabled);
        verify(!panel.maximumCutRow.enabled, "the cut's maximum works with the cut off");

        panel.cutSwitch.toggle();
        panel.cutSwitch.toggled();
        compare(SessionModel.param("al", "cut"), true);
        verify(panel.maximumCutRow.enabled);

        panel.boostSwitch.toggle();
        panel.boostSwitch.toggled();
        compare(SessionModel.param("al", "boost"), false);
        verify(panel.targetRow.enabled, "the target follows either switch");
        verify(panel.bandRow.enabled, "the band follows either switch");
        verify(!panel.maximumRow.enabled, "the boost's maximum works with the boost off");

        panel.cutSwitch.toggle();
        panel.cutSwitch.toggled();
        verify(!panel.targetRow.enabled, "the target can be moved with both off");
        verify(!panel.bandRow.enabled);
    }

    function test_theAdvancedSettingsAreFoldedAwayAndWriteTheirNumbers() {
        const panel = testCase.panelFor();
        verify(!panel.advancedSection.expanded, "the advanced settings are open to begin with");
        compare(panel.advancedSection.note, "-20 LUFS · 18 · 18 · ±0 LU");
        panel.advancedSection.expanded = true;

        for (const [row, id, number] of [[panel.targetRow, "target", -16], [panel.maximumRow, "maximumBoost", 9],
                                         [panel.maximumCutRow, "maximumCut", 12], [panel.bandRow, "band", 2]]) {
            row.slider.value = number;
            row.slider.moved();
            SessionModel.commitParam("al", id);
            compare(SessionModel.param("al", id), number);
        }
        compare(panel.advancedSection.note, "-16 LUFS · 9 · 12 · ±2 LU");
        compare(panel.targetRow.spoken, "-16 LUFS");

        // Typed, and held to the range.
        panel.bandRow.field.text = "4";
        panel.bandRow.field.editingFinished();
        compare(SessionModel.param("al", "band"), 4);
        panel.maximumCutRow.field.text = "40";
        panel.maximumCutRow.field.editingFinished();
        compare(SessionModel.param("al", "maximumCut"), 24);
    }

    function test_theLowestValuesSayWhatTheyMean() {
        const panel = testCase.panelFor();
        panel.advancedSection.expanded = true;
        compare(panel.bandRow.lowestNote.text, "At 0 every level is moved to the target");
        verify(panel.bandRow.noteShown);
        SessionModel.setParam("al", "band", 1);
        verify(!panel.bandRow.noteShown);
        verify(!panel.maximumCutRow.noteShown);
        SessionModel.setParam("al", "maximumCut", 0);
        compare(panel.maximumCutRow.lowestNote.text, "At 0 dB nothing is turned down");
        verify(panel.maximumCutRow.noteShown);
    }

    function test_thePanelSaysWhatTheBlockDoes() {
        compare(testCase.panelFor().explanationLine.text,
                "Brings quiet passages up and loud ones down towards a steady loudness, and leaves what is near it alone. For voices on a call. On music or a film it takes the loud and soft out: turn it off, or give them a lane of their own.");
    }

    function test_theGraphIsOpenOnlyOnceTheBlockCanComeDown() {
        verify(!testCase.panelFor().graphSection.expanded, "a plain Auto Level's card is not short");
        SessionModel.setParam("al", "cut", true);
        verify(testCase.panelFor().graphSection.expanded);
    }

    function test_theCurveBendsWhereTheSettingsSay() {
        const panel = testCase.panelFor();
        SessionModel.setParam("al", "cut", true);
        SessionModel.setParam("al", "target", -23);
        SessionModel.setParam("al", "band", 1);
        const curve = panel.graphView.curve;
        const at = lufs => curve.find(point => Math.abs(point.in - lufs) < 1e-6)?.out;
        compare(at(-60), -60);
        compare(at(-50), -32);
        compare(at(-42), -24);
        compare(at(-24), -24);
        compare(at(-22), -22);
        compare(at(-4), -22);
        compare(at(0), -18);
    }

    function test_theTableAgreesWithTheEnginesRuleAtEveryRow() {
        SessionModel.setParam("al", "cut", true);
        SessionModel.setParam("al", "target", -23);
        SessionModel.setParam("al", "band", 1);
        const panel = testCase.panelFor();
        const face = testCase.faceFor();
        const levels = panel.words.tableLevels(-23);
        compare(levels, [-55, -45, -30, -23, -15, -2]);
        compare(panel.tableRows.map(row => row.changeText), ["0 dB", "+18 dB", "+6 dB", "0 dB", "-7 dB", "-18 dB"]);
        compare(panel.tableRows.map(row => row.outText),
                ["-55 LUFS", "-27 LUFS", "-24 LUFS", "-23 LUFS", "-22 LUFS", "-20 LUFS"]);
        testCase.play(testCase.peakOf(-30));
        for (const lufs of levels) {
            TestEngine.setFakeLevel(testCase.sessionId, "al", testCase.peakOf(lufs));
            const wanted = panel.words.gainFor(lufs, panel.settings);
            tryVerify(() => Math.abs(panel.reading.gainDb - wanted) < 0.05, 2000,
                      "the engine says another gain at " + lufs + " LUFS");
        }
        verify(face !== null);
    }

    function test_theTableHasARowForNowWhileItRuns() {
        const panel = testCase.panelFor();
        compare(panel.tableCellTexts.length, 18);
        testCase.play(testCase.peakOf(-30));
        tryCompare(panel.tableCellTexts, "length", 21);
        tryCompare(panel.tableRows[6], "inText", "Now -30 LUFS");
        tryCompare(panel.tableRows[6], "outText", "-20 LUFS");
        tryCompare(panel.tableRows[6], "changeText", "+10 dB");
    }

    function test_theDotAndTheLiveLineAreTheSound() {
        const panel = testCase.panelFor();
        panel.graphSection.expanded = true;
        verify(!panel.reading.live, "live numbers with nothing playing");
        testCase.play(testCase.peakOf(-30));
        tryCompare(panel.liveLabel, "text", "In -30 LUFS · Out -20 LUFS · Change +10 dB");
        verify(panel.graphView.live);
        fuzzyCompare(panel.graphView.inLufs, -30, 0.1);
        fuzzyCompare(panel.graphView.outLufs, -20, 0.1);
    }

    function test_whileNothingPlaysTheFaceSaysTheSetting() {
        const face = testCase.faceFor();
        const panel = testCase.panelFor();
        compare(face.headlineLabel.text, "On");
        compare(face.captionLabel.text, "Not playing");
        compare(face.dot, "off");
        compare(panel.statusLabel.text, "Not playing");
        SessionModel.setParam("al", "boost", false);
        tryCompare(face.headlineLabel, "text", "Off");
        verify(Qt.colorEqual(face.tint, AppColors.muted), "a switched-off Auto Level is not grey");
        SessionModel.setParam("al", "cut", true);
        tryCompare(face.headlineLabel, "text", "On");
    }

    function test_aBlockSwitchedOffSaysOffAndNothingMore() {
        const face = testCase.faceFor();
        const panel = testCase.panelFor();
        testCase.play(testCase.peakOf(-30));
        SessionModel.setBlockEnabled("al", false);
        tryCompare(face.headlineLabel, "text", "Off");
        compare(face.captionLabel.text, "");
        compare(face.dot, "off");
        compare(panel.statusLabel.text, "Switched off");
    }

    function test_lifting() {
        const face = testCase.faceFor();
        const panel = testCase.panelFor();
        testCase.play(testCase.peakOf(-30));
        tryCompare(face.headlineLabel, "text", "+10 dB");
        compare(face.captionLabel.text, "lifting");
        compare(face.dot, "live");
        tryCompare(panel.statusLabel, "text", "Lifting 10 dB");
        fuzzyCompare(face.fill, 10 / 18, 0.01);
        compare(face.zeroAt, 0);
    }

    function test_turningDown() {
        SessionModel.setParam("al", "cut", true);
        const face = testCase.faceFor();
        const panel = testCase.panelFor();
        testCase.play(testCase.peakOf(-16));
        tryCompare(face.headlineLabel, "text", "-4 dB");
        compare(face.captionLabel.text, "turning down");
        compare(face.dot, "live");
        tryCompare(panel.statusLabel, "text", "Turning down 4 dB");
        fuzzyCompare(face.zeroAt, 0.5, 0.001);
        fuzzyCompare(face.fill, 4 / 36, 0.01);
    }

    function test_atTheMaximum() {
        const face = testCase.faceFor();
        const panel = testCase.panelFor();
        SessionModel.setParam("al", "maximumBoost", 6);
        testCase.play(testCase.peakOf(-40));
        tryCompare(face.headlineLabel, "text", "+6 dB");
        compare(face.captionLabel.text, "at the maximum");
        compare(face.dot, "live");
        tryCompare(panel.statusLabel, "text", "Lifting 6 dB, the most it may");
        compare(face.fill, 1);

        SessionModel.setParam("al", "cut", true);
        SessionModel.setParam("al", "maximumCut", 6);
        TestEngine.setFakeLevel(testCase.sessionId, "al", testCase.peakOf(-6));
        tryCompare(face.headlineLabel, "text", "-6 dB");
        compare(face.captionLabel.text, "at the maximum");
        tryCompare(panel.statusLabel, "text", "Turning down 6 dB, the most it may");
    }

    function test_pulledBack() {
        const face = testCase.faceFor();
        const panel = testCase.panelFor();
        testCase.playValues([-9, -20, 9]);
        tryCompare(face.captionLabel, "text", "pulled back");
        compare(face.headlineLabel.text, "-9 dB");
        compare(face.dot, "live");
        tryCompare(panel.statusLabel, "text", "Pulling back 9 dB");
    }

    function test_leftAlone() {
        SessionModel.setParam("al", "band", 2);
        const face = testCase.faceFor();
        const panel = testCase.panelFor();
        testCase.play(testCase.peakOf(-21));
        tryCompare(face.captionLabel, "text", "left alone");
        compare(face.headlineLabel.text, "0 dB");
        compare(face.dot, "ok");
        tryCompare(panel.statusLabel, "text", "Leaving the sound as it is");
    }

    function test_tooQuietToRead() {
        const face = testCase.faceFor();
        const panel = testCase.panelFor();
        testCase.play(testCase.peakOf(-60));
        tryCompare(face.captionLabel, "text", "too quiet to read");
        compare(face.headlineLabel.text, "0 dB");
        compare(face.dot, "ok");
        tryCompare(panel.statusLabel, "text", "Too quiet to read");
    }

    function test_bothSwitchesOff() {
        SessionModel.setParam("al", "boost", false);
        const face = testCase.faceFor();
        const panel = testCase.panelFor();
        testCase.play(testCase.peakOf(-30));
        tryCompare(face.captionLabel, "text", "both switches off");
        compare(face.headlineLabel.text, "0 dB");
        compare(face.dot, "ok");
        tryCompare(panel.statusLabel, "text", "Both switches off");
    }

    function test_theWordsChangeAtMostFourTimesASecond() {
        const face = testCase.faceFor();
        testCase.play(testCase.peakOf(-30));
        tryCompare(face.headlineLabel, "text", "+10 dB");
        const seen = [];
        const connection = () => seen.push(face.headlineLabel.text);
        face.headlineLabel.textChanged.connect(connection);
        for (let step = 0; step < 40; ++step) {
            TestEngine.setFakeLevel(testCase.sessionId, "al", testCase.peakOf(-30 - step % 2));
            wait(10);
        }
        verify(seen.length <= 2, "the tile changed " + seen.length + " times in 0.4 s");
    }

    function test_theRangeLineIsTheLastMinuteAndNothingElse() {
        const panel = testCase.panelFor();
        const face = testCase.faceFor();
        compare(panel.rangeWords, "", "a range with nothing heard");
        const now = 1000000000;
        AutoLevelHistory.note("al", -9, now);
        AutoLevelHistory.note("al", 6, now + 2000);
        const range = AutoLevelHistory.rangeOf("al", now + 3000);
        compare(range.low, -9);
        compare(range.high, 6);
        compare(panel.words.rangeText(range.low, range.high), "Moved -9 to +6 dB in the last minute");
        compare(panel.words.rangeText(4, 4), "Stayed at +4 dB in the last minute");
        compare(AutoLevelHistory.rangeOf("al", now + 2000 + 61000), null);
        AutoLevelHistory.forget("al");

        // And from the tile.
        testCase.play(testCase.peakOf(-30));
        tryCompare(panel.rangeLabel, "text", "Stayed at +10 dB in the last minute");
        verify(face !== null);
    }

    function test_theRangeLineEmptiesOnACardLeftOpenWithNothingPlaying() {
        const panel = testCase.panelFor();
        // All but gone from the minute, and nothing notes anything after it.
        AutoLevelHistory.note("al", 6, Date.now() - 58500);
        panel.nowMs = Date.now();
        compare(panel.rangeWords, "Stayed at +6 dB in the last minute");
        tryCompare(panel, "rangeWords", "", 4000, "a stopped card kept an old range");
    }

    function test_aProgramThatDeclaresMusicIsSaidOnce() {
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "src", type: "source.application",
                                                                       params: { binary: "player" } }, index: -1 }));
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "mid", type: "effect.volume" }, index: -1 }));
        for (const [from, to] of [["src", "mid"], ["mid", "al"]]) {
            SessionModel.connectLanes(from, to, 2);
        }
        const face = testCase.faceFor();
        const panel = testCase.panelFor();
        verify(!panel.reading.declaredMusic, "nothing is known of the program yet");

        const inventoryWith = role => ({ applications: [{ key: "binary:player", name: "Player", streamCount: 1,
                                                          streams: [{ node: "p", role: role }] }] });
        TestEngine.inventoryChanged(JSON.stringify(inventoryWith("")));
        verify(!panel.reading.declaredMusic, "saying nothing was read as music");

        TestEngine.inventoryChanged(JSON.stringify(inventoryWith("Music")));
        tryCompare(panel.reading, "declaredMusic", true);
        compare(panel.musicLine.text,
                "This lane's program says it is playing music. This block takes the loud and soft out of it. Turn it off, or move the program to a lane of its own.");
        compare(Announcer.last, panel.musicLine.text);

        testCase.play(testCase.peakOf(-16));
        tryCompare(face.captionLabel, "text", "program says music");
        verify(Qt.colorEqual(face.captionLabel.color, AppColors.warningText));

        TestEngine.inventoryChanged(JSON.stringify(inventoryWith("Movie")));
        verify(panel.reading.declaredMusic, "a film is declared too");
        Announcer.reset();
        TestEngine.inventoryChanged(JSON.stringify(inventoryWith("Movie")));
        compare(Announcer.last, "", "said again with nothing new");

        TestEngine.inventoryChanged(JSON.stringify(inventoryWith("Phone")));
        tryCompare(panel.reading, "declaredMusic", false);
    }
}
