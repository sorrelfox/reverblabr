// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The One Ear Mix block's face and panel offscreen, against the mock engine:
// every state of the tile and every word the card can show. What reaches
// each ear, the glide and the reading itself are in blocks.OneEarMix.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "OneEarMix"
    when: windowShown

    property string sessionId: ""

    readonly property int settled: 0
    readonly property int arriving: 1
    readonly property int notRead: 0
    readonly property int readingNow: 1
    readonly property int read: 2

    function panelFor() {
        const panel = createTemporaryObject(BlockPanels.panelFor("hearing.oneearmix"), testCase, { width: 340 });
        panel.blockId = "mix";
        return panel;
    }

    function faceFor() {
        const face = createTemporaryObject(BlockFaces.faceFor("hearing.oneearmix"), testCase, { width: 180, height: 90 });
        face.blockId = "mix";
        return face;
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("One ear");
        verify(testCase.sessionId !== "", "the mock opened a session");
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "mix", type: "hearing.oneearmix" }, index: -1 }));
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        SessionModel.setParam("mix", "sendTo", "none");
        SessionModel.setParam("mix", "hear", "fold");
        SessionModel.setParam("mix", "lowerDb", 0);
        SessionModel.setBlockEnabled("mix", true);
        Announcer.reset();
    }

    function cleanup() {
        TestEngine.setFakeBlockValues(testCase.sessionId, "mix", []);
        TestEngine.clearFakeLevels(testCase.sessionId);
        TestEngine.stop(testCase.sessionId);
        Meters.watch("");
    }

    function play() {
        Meters.watch(testCase.sessionId);
        TestEngine.start(testCase.sessionId);
        TestEngine.setFakeLevel(testCase.sessionId, "mix", 0.5);
        tryVerify(() => Meters.live && (Meters.levels("mix").values ?? []).length > 3, 1000, "the block never went live");
    }

    function publish(state: int, fold: real, mono: real, stage: int) {
        TestEngine.setFakeBlockValues(testCase.sessionId, "mix", [state, fold, mono, stage]);
    }

    function test_theBlockHasAFaceAndAPanelOfItsOwn() {
        verify(BlockPanels.panelFor("hearing.oneearmix") !== null);
        verify(BlockFaces.faceFor("hearing.oneearmix") !== null);
    }

    function test_theRangesAreTheEnginesOwn() {
        const panel = testCase.panelFor();
        const lower = BlockCatalogue.describeParam("hearing.oneearmix", "lowerDb");
        compare(panel.mostLowered, lower.maximum);
        compare(panel.lowerSlider.to, lower.maximum);
        compare(BlockCatalogue.describeParam("hearing.oneearmix", "sendTo").default, "none");
        compare(BlockCatalogue.describeParam("hearing.oneearmix", "hear").default, "fold");
        compare(panel.sendChoices.count, BlockCatalogue.describeParam("hearing.oneearmix", "sendTo").choices.length);
        compare(panel.hearChoices.count, BlockCatalogue.describeParam("hearing.oneearmix", "hear").choices.length);
    }

    // --- the tile ---

    function test_aNewBlockSaysPickWhereItGoes() {
        const face = testCase.faceFor();
        compare(face.headlineLabel.text, "No change");
        compare(face.captionLabel.text, "Pick where it goes");
        compare(face.dot, "ok");
        testCase.play();
        tryCompare(face.headlineLabel, "text", "No change");
        compare(face.dot, "ok");
    }

    function test_aRunningTileStatesTheRoute_data() {
        return [
            { tag: "left", send: "left", headline: "Left ear", caption: "L+R one mix to L" },
            { tag: "right", send: "right", headline: "Right ear", caption: "L+R one mix to R" },
            { tag: "both", send: "both", headline: "Both ears", caption: "L+R one mix to both" }
        ];
    }

    function test_aRunningTileStatesTheRoute(data) {
        const face = testCase.faceFor();
        SessionModel.setParam("mix", "sendTo", data.send);
        testCase.play();
        tryCompare(face.headlineLabel, "text", data.headline);
        tryCompare(face.captionLabel, "text", data.caption);
        compare(face.dot, "ok");
    }

    function test_aSideArriving_data() {
        return [
            { tag: "left", send: "left", caption: "Bringing in R" },
            { tag: "right", send: "right", caption: "Bringing in L" },
            { tag: "both", send: "both", caption: "Bringing in both sides" }
        ];
    }

    function test_aSideArriving(data) {
        const face = testCase.faceFor();
        SessionModel.setParam("mix", "sendTo", data.send);
        testCase.play();
        testCase.publish(testCase.arriving, -120, -120, testCase.notRead);
        tryCompare(face.captionLabel, "text", data.caption);
        compare(face.dot, "live");
        compare(face.captionLabel.color, AppColors.liveText);
    }

    function test_plainMono_data() {
        return [
            { tag: "left", send: "left", caption: "Plain Mono to L" },
            { tag: "right", send: "right", caption: "Plain Mono to R" },
            { tag: "both", send: "both", caption: "Plain Mono to both" }
        ];
    }

    function test_plainMono(data) {
        const face = testCase.faceFor();
        SessionModel.setParam("mix", "sendTo", data.send);
        SessionModel.setParam("mix", "hear", "mono");
        SessionModel.setParam("mix", "lowerDb", 1.4);
        testCase.play();
        tryCompare(face.captionLabel, "text", data.caption);
        compare(face.dot, "ok");
    }

    function test_lowered_data() {
        return [
            { tag: "left", send: "left", caption: "One mix −1.4 dB to L" },
            { tag: "right", send: "right", caption: "One mix −1.4 dB to R" },
            { tag: "both", send: "both", caption: "One mix −1.4 dB to both" }
        ];
    }

    function test_lowered(data) {
        const face = testCase.faceFor();
        SessionModel.setParam("mix", "sendTo", data.send);
        SessionModel.setParam("mix", "lowerDb", 1.4);
        testCase.play();
        tryCompare(face.captionLabel, "text", data.caption);
        compare(face.dot, "warning");
    }

    function test_stopped() {
        const face = testCase.faceFor();
        SessionModel.setParam("mix", "sendTo", "left");
        compare(face.headlineLabel.text, "Not running");
        compare(face.captionLabel.text, "Session stopped");
        compare(face.dot, "off");
    }

    function test_switchedOff() {
        const face = testCase.faceFor();
        SessionModel.setParam("mix", "sendTo", "left");
        SessionModel.setBlockEnabled("mix", false);
        testCase.play();
        tryCompare(face, "headline", "Off");
        compare(face.captionLabel.text, "Stereo, as it came");
        compare(face.dot, "off");
        compare(face.headlineLabel.color, AppColors.muted);
    }

    function test_theTileSaysAllOfItToAScreenReader() {
        const face = testCase.faceFor();
        compare(face.Accessible.name, "No change, Pick where it goes");
    }

    // --- the card ---

    function test_theStatusAndExplanationFollowTheChoice_data() {
        return [
            { tag: "none", send: "none", status: "No change. Both sides pass as they came.",
                line: "Both sides pass as they came. Pick where the sound goes." },
            { tag: "left", send: "left", status: "Left ear. The right side is silent.",
                line: "Puts both sides into the left ear. Sound panned to one side comes through 3 dB lower. The right side is silent." },
            { tag: "right", send: "right", status: "Right ear. The left side is silent.",
                line: "Puts both sides into the right ear. Sound panned to one side comes through 3 dB lower. The left side is silent." },
            { tag: "both", send: "both", status: "Both ears. One mix on both sides.",
                line: "Puts both sides into one mix, the same on the left and on the right, for one earbud. Sound panned to one side comes through 3 dB lower." }
        ];
    }

    function test_theStatusAndExplanationFollowTheChoice(data) {
        const panel = testCase.panelFor();
        SessionModel.setParam("mix", "sendTo", data.send);
        compare(panel.statusLabel.text, data.status);
        compare(panel.explanationLine.label.text, data.line);
    }

    function test_choosingAnEarSetsItAndSaysSoOnce() {
        const panel = testCase.panelFor();
        panel.sendChoices.itemAt(1).click();
        compare(SessionModel.param("mix", "sendTo"), "left");
        compare(Announcer.last, "Left ear. The right side is silent.");
        verify(panel.sendChoices.itemAt(1).checked);
        Announcer.reset();
        panel.chooseSend("left");
        compare(Announcer.last, "", "the same choice again says nothing");
    }

    function test_everyChoiceHasANameForAScreenReader() {
        const panel = testCase.panelFor();
        const names = [];
        for (let index = 0; index < panel.sendChoices.count; ++index) {
            names.push(panel.sendChoices.itemAt(index).Accessible.name);
        }
        compare(names, ["No change", "Left ear", "Right ear", "Both ears"]);
        compare(panel.hearChoices.itemAt(0).Accessible.name, "One Ear Mix");
        compare(panel.hearChoices.itemAt(1).Accessible.name, "Plain Mono");
        compare(panel.lowerSlider.Accessible.name, "Lower this mix by");
        compare(panel.lowerField.label, "Lower this mix by");
    }

    function test_theTableIsInCells() {
        const panel = testCase.panelFor();
        const cells = [];
        for (let index = 0; index < panel.tableRows.count; ++index) {
            cells.push(panel.tableRows.itemAt(index).text);
        }
        compare(cells, ["Centred", "0 dB", "About 0 dB",
                        "One side only", "−6 dB", "−3 dB",
                        "Out of phase", "Cancels", "About 0 dB",
                        "Unrelated sides", "−3 dB", "0 dB",
                        "Peaks", "No rise", "Can rise"]);
        compare(panel.tableRows.itemAt(4).Accessible.name, "One side only, Plain Mono: −6 dB");
    }

    function test_theComparisonWaitsForAnEar() {
        const panel = testCase.panelFor();
        verify(!panel.compareSection.enabled);
        compare(panel.pickFirstLabel.text, "Pick an ear first");
        SessionModel.setParam("mix", "sendTo", "right");
        verify(panel.compareSection.enabled);
    }

    function test_theInfoLineSaysWhatStaysLost() {
        const panel = testCase.panelFor();
        verify(panel.infoLine.text.indexOf("Direction doesn't come back") === 0);
        verify(panel.infoLine.text.toLowerCase().indexOf("cancel") < 0, "it must not promise that nothing cancels");
        verify(!panel.figuresButton.checked);
        panel.figuresButton.toggle();
        verify(panel.figuresButton.checked);
        verify(panel.figuresLabel.text.indexOf("30 dB lower") > 0);
    }

    function test_hearSwitchesTheMix() {
        const panel = testCase.panelFor();
        SessionModel.setParam("mix", "sendTo", "left");
        panel.hearChoices.itemAt(1).click();
        compare(SessionModel.param("mix", "hear"), "mono");
    }

    function test_theSliderAndTheFieldSetTheSameValue() {
        const panel = testCase.panelFor();
        SessionModel.setParam("mix", "sendTo", "left");
        panel.lowerField.valueEdited(1.4);
        compare(SessionModel.param("mix", "lowerDb"), 1.4);
        tryCompare(panel.lowerSlider, "value", 1.4, 500);
        compare(panel.lowerField.stepSize, 0.5);
        compare(panel.lowerField.pageStepSize, 1);
    }

    // --- Match Levels ---

    function match(panel: var, fold: real, mono: real) {
        panel.matchButton.click();
        compare(panel.matchButton.text, "Reading, 3 s");
        verify(!panel.matchButton.enabled);
        testCase.publish(testCase.settled, -30, -30, testCase.readingNow);
        tryCompare(panel, "sawReading", true);
        testCase.publish(testCase.settled, fold, mono, testCase.read);
        tryCompare(panel, "matching", false);
    }

    function test_theButtonNeedsARunningSession() {
        const panel = testCase.panelFor();
        SessionModel.setParam("mix", "sendTo", "left");
        verify(!panel.matchButton.enabled);
        testCase.play();
        tryVerify(() => panel.matchButton.enabled);
        compare(panel.matchButton.text, "Match Levels");
    }

    function test_aReadingFillsInTheGapAndSaysSo() {
        const panel = testCase.panelFor();
        SessionModel.setParam("mix", "sendTo", "left");
        testCase.play();
        tryVerify(() => panel.matchButton.enabled);
        testCase.match(panel, -21.6, -23.04);
        compare(SessionModel.param("mix", "lowerDb"), 1.4);
        compare(panel.matchLabel.text,
                "One Ear Mix was 1.4 dB louder here, so it is lowered by 1.4 dB. Set it back to 0 when you are done.");
        compare(Announcer.last, panel.matchLabel.text);
        verify(panel.matchButton.enabled);
    }

    function test_aReadingThatMakesNoSenseWritesNothing_data() {
        return [
            { tag: "silence", fold: -120, mono: -120, words: "Nothing to measure yet. Play something first." },
            { tag: "plain mono louder", fold: -24, mono: -23, words: "Plain Mono was louder here, so nothing is lowered." },
            { tag: "the same", fold: -23, mono: -23.01, words: "The two mixes read the same here, so nothing is lowered." },
            { tag: "too far", fold: -20, mono: -27, words: "The two mixes are more than 6 dB apart here, so nothing is lowered." },
            { tag: "plain mono silent", fold: -20, mono: -120, words: "The two mixes are more than 6 dB apart here, so nothing is lowered." }
        ];
    }

    function test_aReadingThatMakesNoSenseWritesNothing(data) {
        const panel = testCase.panelFor();
        SessionModel.setParam("mix", "sendTo", "left");
        SessionModel.setParam("mix", "lowerDb", 2);
        testCase.play();
        tryVerify(() => panel.matchButton.enabled);
        testCase.match(panel, data.fold, data.mono);
        compare(SessionModel.param("mix", "lowerDb"), 2);
        compare(panel.matchLabel.text, data.words);
    }

    function test_aReadingLeftOverFromLastTimeIsNotTakenForThisOne() {
        const panel = testCase.panelFor();
        SessionModel.setParam("mix", "sendTo", "left");
        testCase.play();
        testCase.publish(testCase.settled, -20, -21, testCase.read);
        tryVerify(() => panel.reader.stage === testCase.read);
        panel.matchButton.click();
        verify(panel.matching, "still waiting for this reading to start");
        testCase.publish(testCase.settled, -20, -21.5, testCase.readingNow);
        tryCompare(panel, "sawReading", true);
        compare(SessionModel.param("mix", "lowerDb"), 0);
    }

    function test_aStoppedSessionEndsAReading() {
        const panel = testCase.panelFor();
        SessionModel.setParam("mix", "sendTo", "left");
        testCase.play();
        tryVerify(() => panel.matchButton.enabled);
        panel.matchButton.click();
        TestEngine.stop(testCase.sessionId);
        tryCompare(panel, "matching", false);
        compare(panel.matchLabel.text, "The session stopped, so nothing was measured.");
        compare(SessionModel.param("mix", "lowerDb"), 0);
    }

    function test_aReadingNoOneAnswersIsGivenUp() {
        const panel = testCase.panelFor();
        SessionModel.setParam("mix", "sendTo", "left");
        testCase.play();
        tryVerify(() => panel.matchButton.enabled);
        panel.startMatching();
        panel.giveUpMatching("No reading came back, so nothing was changed.");
        verify(!panel.matching);
        compare(panel.matchLabel.text, "No reading came back, so nothing was changed.");
    }

    function test_aPressLeavesNoStepToUndo() {
        const panel = testCase.panelFor();
        SessionModel.setParam("mix", "sendTo", "left");
        const undoable = SessionModel.undoText ?? "";
        testCase.play();
        tryVerify(() => panel.matchButton.enabled);
        panel.matchButton.click();
        compare(SessionModel.undoText ?? "", undoable);
    }
}
