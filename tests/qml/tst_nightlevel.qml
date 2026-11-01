// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Night Level block's tile and card offscreen, against the mock engine:
// every state and every word, the curve against the block's table, and a
// program's own word for music. What the block does is in blocks.NightLevel.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "NightLevel"
    when: windowShown
    visible: true

    property string sessionId: ""

    readonly property var words: NightLevelWords {}

    /// The state number: tracking, ceiling acting, detector, seconds.
    function state(tracking, acting = false, detector = 0, seconds = 0) {
        return tracking + (acting ? 10 : 0) + detector * 100 + seconds * 1000;
    }

    readonly property int noSpeech: 0
    readonly property int found: 1
    readonly property int typed: 2

    function panelFor() {
        const panel = createTemporaryObject(BlockPanels.panelFor("effect.nightlevel"), testCase, { width: 380 });
        panel.blockId = "night";
        return panel;
    }

    function faceFor() {
        const face = createTemporaryObject(BlockFaces.faceFor("effect.nightlevel"), testCase, { width: 200, height: 90 });
        face.blockId = "night";
        return face;
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Night level");
        verify(testCase.sessionId !== "", "the mock opened a session");
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "film", type: "source.application" }, index: -1 }));
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "night", type: "effect.nightlevel" }, index: -1 }));
        TestEngine.applyEdit(testCase.sessionId, JSON.stringify({
            op: "connect", wire: { fromBlock: "film", fromPort: 0, toBlock: "night", toPort: 0 }, index: -1
        }));
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        SessionModel.setParams("night", [["cut", 100], ["boost", 50], ["pivotFrom", "found"], ["typedPivot", -28]]);
        SessionModel.setBlockEnabled("night", true);
        Announcer.reset();
    }

    function cleanup() {
        TestEngine.setFakeOverload(testCase.sessionId, false);
        TestEngine.setFakeFrozen(testCase.sessionId, false);
        TestEngine.setFakeBlockValues(testCase.sessionId, "night", []);
        TestEngine.getInventory();
        SessionModel.removeBlocks(SessionModel.blockIds.filter(id => id === "player"));
        TestEngine.stop(testCase.sessionId);
        Meters.watch("");
    }

    function play(values) {
        Meters.watch(testCase.sessionId);
        TestEngine.start(testCase.sessionId);
        TestEngine.setFakeBlockValues(testCase.sessionId, "night", values);
        tryVerify(() => Meters.live, 1000, "the session never went live");
    }

    // --- the numbers written down twice ---

    function test_theNumbersAreTheEngines() {
        const typedPivot = BlockCatalogue.describeParam("effect.nightlevel", "typedPivot");
        compare(typedPivot.minimum, testCase.words.lowestPivot);
        compare(typedPivot.maximum, testCase.words.highestPivot);
        compare(typedPivot.default, testCase.words.defaultTyped);
        compare(BlockCatalogue.describeParam("effect.nightlevel", "cut").default, 0);
        compare(BlockCatalogue.describeParam("effect.nightlevel", "boost").default, 0);
        compare(BlockCatalogue.describeParam("effect.nightlevel", "pivotFrom").default, "found");
    }

    function test_theCurveIsTheTable_data() {
        return [
            { tag: "light", cut: 50, boost: 0, out: [21.9, 14.9, 7.9, 0, -10, -20, -30] },
            { tag: "night", cut: 100, boost: 50, out: [13.8, 9.8, 5.8, 0, -9, -17, -25] },
            { tag: "strong", cut: 100, boost: 100, out: [13.8, 9.8, 5.8, 0, -8, -14, -20] }
        ];
    }

    function test_theCurveIsTheTable(row) {
        [30, 20, 10, 0, -10, -20, -30].forEach((lu, at) => {
            fuzzyCompare(lu + testCase.words.changeFor(lu, row.cut, row.boost), row.out[at], 0.001);
        });
        compare(testCase.words.changeFor(-31, row.cut, row.boost), 0);
    }

    function test_theCeilingIsTheBlocks() {
        compare(testCase.words.ceilingFor(-27, 100, 50), -7);
        compare(testCase.words.ceilingFor(-27, 50, 0), -1);
        compare(testCase.words.ceilingFor(-45, 100, 0), -25);
        compare(testCase.words.ceilingFor(-120, 100, 100), -1);
        compare(testCase.words.ceilingFor(-45, 0, 100), -1);
        verify(testCase.words.ceilingFor(-45, 0, 0) > 0, "set to nothing it is out of reach");
    }

    function test_theRatiosFollowTheAmounts() {
        compare(testCase.words.ratioText(0.6, 100), "2.5:1");
        compare(testCase.words.ratioText(0.4, 100), "1.67:1");
        compare(testCase.words.ratioText(0.4, 50), "1.25:1");
    }

    // --- the tile: every row of the table ---

    function test_notSet() {
        SessionModel.setParams("night", [["cut", 0], ["boost", 0]]);
        const face = testCase.faceFor();
        testCase.play([0, -120, -30, testCase.state(testCase.noSpeech)]);
        tryCompare(face, "caption", "set an amount");
        compare(face.headline, "0 dB");
        compare(face.levelLine, "");
        compare(face.dot, "ok");
    }

    function test_noSpeechYet() {
        const face = testCase.faceFor();
        testCase.play([0, -120, -30, testCase.state(testCase.noSpeech)]);
        tryCompare(face, "caption", "passing unchanged");
        compare(face.headline, "0 dB");
        compare(face.levelLine, "No speech yet");
        compare(face.dot, "ok");
    }

    function test_nearSpeech() {
        const face = testCase.faceFor();
        testCase.play([0.2, -27, -27, testCase.state(testCase.found)]);
        tryCompare(face, "caption", "left alone");
        compare(face.headline, "0 dB");
        compare(face.levelLine, "Speech -27 LUFS");
        compare(face.dot, "ok");
    }

    function test_loudSound() {
        const face = testCase.faceFor();
        testCase.play([-7, -27, -12, testCase.state(testCase.found)]);
        tryCompare(face, "caption", "turning down");
        compare(face.headline, "-7 dB");
        compare(face.dot, "live");
    }

    function test_aHeldLevel() {
        const face = testCase.faceFor();
        testCase.play([-7, -27, -12, testCase.state(testCase.found, false, 0, 45)]);
        tryCompare(face, "levelLine", "-27 LUFS held");
    }

    function test_quietScene() {
        const face = testCase.faceFor();
        testCase.play([3, -27, -40, testCase.state(testCase.found)]);
        tryCompare(face, "caption", "lifting");
        compare(face.headline, "+3 dB");
        compare(face.dot, "live");
    }

    function test_aTypedLevel() {
        SessionModel.setParams("night", [["pivotFrom", "typed"], ["typedPivot", -24]]);
        const face = testCase.faceFor();
        testCase.play([3, -120, -40, testCase.state(testCase.typed)]);
        tryCompare(face, "levelLine", "Typed -24 LUFS");
    }

    function test_ceilingActing() {
        const face = testCase.faceFor();
        testCase.play([-12, -27, -5, testCase.state(testCase.found, true)]);
        tryCompare(face, "caption", "peaks held");
        compare(face.headline, "-12 dB");
        compare(face.dot, "live");
    }

    function test_stoppedAndOff() {
        const face = testCase.faceFor();
        compare(face.caption, "Not playing");
        compare(face.headline, "On");
        compare(face.dot, "off");
        SessionModel.setBlockEnabled("night", false);
        tryCompare(face, "headline", "Off");
        compare(face.caption, "");
    }

    function test_aFrozenEngineIsNotPlaying() {
        const face = testCase.faceFor();
        testCase.play([-7, -27, -12, testCase.state(testCase.found)]);
        tryCompare(face, "caption", "turning down");
        TestEngine.setFakeFrozen(testCase.sessionId, true);
        tryCompare(face, "caption", "Not playing", 2000);
    }

    function test_setToNothingTheHeadlineSaysOff() {
        SessionModel.setParams("night", [["cut", 0], ["boost", 0]]);
        const face = testCase.faceFor();
        compare(face.headline, "Off");
        verify(face.detail.indexOf("passes the sound through") >= 0, face.detail);
    }

    function test_nothingIsGreenWhileTheSoundIsChanged_data() {
        return [
            { tag: "held with no level", values: [-3, -120, -5, 10], caption: "peaks held", status: "Peaks held, down 3 dB" },
            { tag: "gliding back after a lapse", values: [-12, -120, -12, 0], caption: "turning down",
              status: "Turning down 12 dB" },
            { tag: "a lift letting go", values: [4, -120, -40, 0], caption: "lifting", status: "Lifting 4 dB" }
        ];
    }

    function test_nothingIsGreenWhileTheSoundIsChanged(row) {
        const face = testCase.faceFor();
        const panel = testCase.panelFor();
        testCase.play(row.values);
        tryCompare(face, "caption", row.caption);
        compare(face.dot, "live");
        compare(face.levelLine, "No speech yet");
        tryCompare(panel.statusLabel, "text", row.status);
    }

    function test_setToNothingAndStillGlidingIsNotNotSet() {
        SessionModel.setParams("night", [["cut", 0], ["boost", 0]]);
        const face = testCase.faceFor();
        testCase.play([-9, -120, -12, 0]);
        tryCompare(face, "caption", "turning down");
        compare(face.dot, "live");
    }

    function test_pressingFoundSaysNothing() {
        SessionModel.setParam("night", "pivotFrom", "typed");
        const face = testCase.faceFor();
        testCase.play([0, -27, -27, testCase.state(testCase.typed)]);
        tryVerify(() => face.reading.tracking === testCase.typed);
        SessionModel.setParam("night", "pivotFrom", "found");
        TestEngine.setFakeBlockValues(testCase.sessionId, "night", [0, -27, -27, testCase.state(testCase.found)]);
        tryVerify(() => face.reading.tracking === testCase.found);
        wait(100);
        compare(Announcer.last, "");
        SessionModel.setParam("night", "pivotFrom", "typed");
        TestEngine.setFakeBlockValues(testCase.sessionId, "night", [0, -120, -27, testCase.state(testCase.typed)]);
        tryVerify(() => face.reading.tracking === testCase.typed);
        SessionModel.setParam("night", "pivotFrom", "found");
        TestEngine.setFakeBlockValues(testCase.sessionId, "night", [0, -120, -27, testCase.state(testCase.noSpeech)]);
        tryVerify(() => face.reading.tracking === testCase.noSpeech);
        wait(100);
        compare(Announcer.last, "");
    }

    function test_aBlockComingBackSaysNothing() {
        const face = testCase.faceFor();
        testCase.play([0, -27, -27, testCase.state(testCase.found)]);
        tryVerify(() => face.reading.tracking === testCase.found);
        Announcer.reset();
        SessionModel.setBlockEnabled("night", false);
        tryVerify(() => !face.reading.current);
        SessionModel.setBlockEnabled("night", true);
        tryVerify(() => face.reading.tracking === testCase.found);
        wait(100);
        compare(Announcer.last, "");
    }

    function test_aFoundLevelAndALapseAreSaid() {
        const face = testCase.faceFor();
        testCase.play([0, -120, -30, testCase.state(testCase.noSpeech)]);
        tryCompare(face, "caption", "passing unchanged");
        TestEngine.setFakeBlockValues(testCase.sessionId, "night", [0, -27, -27, testCase.state(testCase.found)]);
        tryCompare(Announcer, "last", "Speech at -27 LUFS", 1000);
        Announcer.reset();
        TestEngine.setFakeBlockValues(testCase.sessionId, "night", [0, -120, -30, testCase.state(testCase.noSpeech)]);
        tryCompare(Announcer, "last", "No speech yet", 1000);
    }

    // --- the card ---

    function test_theCardsStatus_data() {
        return [
            { tag: "no speech", values: [0, -120, -30, 0], words: "No speech yet", level: "No speech yet",
              note: "Nothing changes until it hears speech" },
            { tag: "left", values: [0, -27, -27, 1], words: "Left alone", level: "-27 LUFS",
              note: "Found on this lane. From a voice detector, it can be wrong." },
            { tag: "down", values: [-7, -27, -12, 1], words: "Turning down 7 dB", level: "-27 LUFS",
              note: "Found on this lane. From a voice detector, it can be wrong." },
            { tag: "lifting", values: [3, -27, -40, 1], words: "Lifting 3 dB", level: "-27 LUFS",
              note: "Found on this lane. From a voice detector, it can be wrong." },
            { tag: "ceiling", values: [-12, -27, -5, 11], words: "Peaks held, down 12 dB", level: "-27 LUFS",
              note: "Found on this lane. From a voice detector, it can be wrong." },
            { tag: "held", values: [-4, -27, -12, 1 + 240 * 1000], words: "Turning down 4 dB", level: "-27 LUFS",
              note: "Held. No voice for 4 min" }
        ];
    }

    function test_theCardsStatus(row) {
        const panel = testCase.panelFor();
        testCase.play(row.values);
        tryCompare(panel.statusLabel, "text", row.words);
        compare(panel.levelLabel.text, row.level);
        compare(panel.levelNoteLabel.text, row.note);
    }

    function test_theCardsOtherStates() {
        const panel = testCase.panelFor();
        compare(panel.statusLabel.text, "Not playing");
        SessionModel.setParams("night", [["cut", 0], ["boost", 0]]);
        testCase.play([0, -120, -30, 0]);
        tryCompare(panel.statusLabel, "text", "Set an amount below");
        verify(!panel.peakLabel.visible, "set to nothing there is no ceiling to speak of");
        TestEngine.stop(testCase.sessionId);
        SessionModel.setBlockEnabled("night", false);
        tryCompare(panel.statusLabel, "text", "Switched off");
    }

    function test_presetsWriteBothAmountsAndASliderLeavesThem() {
        const panel = testCase.panelFor();
        compare(panel.chosenPreset, "night");
        panel.choosePreset("light");
        tryCompare(panel.reading, "cut", 50);
        compare(panel.reading.boost, 0);
        compare(panel.chosenPreset, "light");
        panel.choosePreset("strong");
        tryCompare(panel.reading, "boost", 100);
        compare(panel.chosenPreset, "strong");
        SessionModel.setParam("night", "boost", 90);
        tryCompare(panel, "chosenPreset", "");
    }

    function test_typedStartsFromTheFoundLevel() {
        const panel = testCase.panelFor();
        testCase.play([0, -26.6, -27, testCase.state(testCase.found)]);
        tryCompare(panel.reading, "foundLufs", -26.6, 1000, "fuzzy");
        verify(!panel.typedRow.visible);
        panel.choosePivotFrom("typed");
        tryCompare(panel.reading, "pivotFrom", "typed");
        compare(panel.reading.typedPivot, -27);
        verify(panel.typedRow.visible);
        compare(panel.levelLabel.text, "-27 LUFS");
        compare(panel.levelNoteLabel.text, "Typed. The block goes by this and never lets it go");
    }

    function test_typedStartsAtTheUsualLevelWithNone() {
        SessionModel.setParam("night", "typedPivot", -30);
        const panel = testCase.panelFor();
        panel.choosePivotFrom("typed");
        tryCompare(panel.reading, "pivotFrom", "typed");
        compare(panel.reading.typedPivot, -28);
    }

    function test_thePeakLine() {
        const panel = testCase.panelFor();
        testCase.play([0, -27, -27, testCase.state(testCase.found)]);
        tryCompare(panel.peakLabel, "text", "Peaks stop at -7 dBFS. A digital limit, not a level at your ears");
        verify(panel.peakLabel.visible);
        verify(panel.graphView.showsPeakLine);
    }

    function test_theDetectorIsNamed() {
        const panel = testCase.panelFor();
        testCase.play([0, -120, -30, testCase.state(testCase.noSpeech, false, 0)]);
        tryVerify(() => panel.reading.current);
        verify(!panel.detectorLabel.visible, "the model says nothing of itself");
        TestEngine.setFakeBlockValues(testCase.sessionId, "night",
                                      [0, -120, -30, testCase.state(testCase.noSpeech, false, 2)]);
        tryVerify(() => panel.detectorLabel.visible);
        verify(panel.detectorLabel.text.startsWith("Level and rhythm is listening."), panel.detectorLabel.text);
    }

    function test_theGraphAndTheTable() {
        const panel = testCase.panelFor();
        verify(panel.graphSection.expanded, "the graph is open");
        compare(panel.graphView.cutRatio.text, "2.5:1");
        compare(panel.graphView.liftRatio.text, "1.25:1");
        verify(!panel.graphView.dot.visible, "no dot while nothing plays");
        compare(panel.tableCellTexts.slice(0, 3), ["+30", "+13.8", "-16.2"]);
        testCase.play([-4, -27, -12, testCase.state(testCase.found)]);
        tryVerify(() => panel.graphView.dot.visible);
        fuzzyCompare(panel.graphView.inLu, 15, 0.001);
        fuzzyCompare(panel.graphView.outLu, 11, 0.001);
        tryCompare(panel, "liveWords", "In -12 LUFS · Out -16 LUFS · Change -4 dB");
        tryVerify(() => panel.tableCellTexts.indexOf("Now +15.0") >= 0);
    }

    function test_theInfoLine() {
        const panel = testCase.panelFor();
        compare(panel.explanationLine.text,
                "Keeps speech where it is and brings louder and quieter sounds towards it. For films and shows. It presses music too, so turn it off for music. The speech level comes from a voice detector, and it can be wrong");
    }

    function test_aProgramThatDeclaresMusicAndOneThatDeclaresAFilm() {
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "player", type: "source.application",
                                                                       params: { binary: "player" } }, index: -1 }));
        SessionModel.connectLanes("player", "night", 2);
        const panel = testCase.panelFor();
        const face = testCase.faceFor();
        const inventoryWith = role => ({ applications: [{ key: "binary:player", name: "Player", streamCount: 1,
                                                          streams: [{ node: "p", role: role }] }] });
        testCase.play([-4, -27, -12, testCase.state(testCase.found)]);
        TestEngine.inventoryChanged(JSON.stringify(inventoryWith("Movie")));
        wait(50);
        verify(!panel.reading.declaredMusic, "a film is what this block is for");
        TestEngine.inventoryChanged(JSON.stringify(inventoryWith("Music")));
        tryCompare(panel.reading, "declaredMusic", true);
        verify(panel.musicLine.visible);
        tryCompare(face.captionLabel, "text", "program says music");
        TestEngine.inventoryChanged(JSON.stringify({ applications: [] }));
        tryCompare(panel.reading, "declaredMusic", false);
    }
}
