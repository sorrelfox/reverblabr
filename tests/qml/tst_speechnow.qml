// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Speech Now block's tile, card and floating label offscreen, against the
// mock engine: every state and every word. The states themselves are in
// blocks.SpeechNow.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "SpeechNow"
    when: windowShown

    property string sessionId: ""

    readonly property int quiet: 0
    readonly property int speaking: 1
    readonly property int quietAgain: 2

    Component {
        id: plainWindow

        FloatingWindow {}
    }

    Component {
        id: mainWindow

        Main {}
    }

    Component {
        id: labelContent

        FloatingLabel {}
    }

    function panelFor() {
        const panel = createTemporaryObject(BlockPanels.panelFor("meter.speechnow"), testCase, { width: 340 });
        panel.blockId = "now";
        return panel;
    }

    function faceFor() {
        const face = createTemporaryObject(BlockFaces.faceFor("meter.speechnow"), testCase, { width: 180, height: 90 });
        face.blockId = "now";
        return face;
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Speech now");
        verify(testCase.sessionId !== "", "the mock opened a session");
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "now", type: "meter.speechnow" }, index: -1 }));
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        SessionModel.setParams("now", [["showFloating", false], ["showInTray", false], ["detector", "model"]]);
        SessionModel.setBlockEnabled("now", true);
        SessionModel.renameBlock("now", "");
    }

    function cleanup() {
        TestEngine.setFakeOverload(testCase.sessionId, false);
        TestEngine.setFakeFrozen(testCase.sessionId, false);
        TestEngine.setFakeBlockValues(testCase.sessionId, "now", []);
        TestEngine.stop(testCase.sessionId);
        Meters.watch("");
        FloatingSurface.setPlacement("label", { left: 40, top: 40, size: "small", screen: "" });
    }

    function play(values) {
        Meters.watch(testCase.sessionId);
        TestEngine.start(testCase.sessionId);
        TestEngine.setFakeBlockValues(testCase.sessionId, "now", values);
        tryVerify(() => Meters.live, 1000, "the session never went live");
    }

    // --- the tile ---

    function test_theTileSaysNotWatchingOfAStoppedSession() {
        const face = testCase.faceFor();
        compare(face.headline, "Not watching");
        compare(face.caption, "Session stopped");
        compare(face.dot, "off");
    }

    function test_theTileSaysSpeaking() {
        const face = testCase.faceFor();
        testCase.play([testCase.speaking, 0.9, 0]);
        tryCompare(face, "headline", "Speaking");
        compare(face.caption, "Voice model");
        compare(face.dot, "live");
        compare(face.Accessible.name, "Speaking, Voice model");
    }

    function test_theTileSaysQuietAgain() {
        const face = testCase.faceFor();
        testCase.play([testCase.quietAgain, 0, 0]);
        tryCompare(face, "headline", "Quiet again");
        compare(face.caption, "Voice stopped");
        compare(face.dot, "ok");
    }

    function test_theTileSaysQuietAndNamesItsDetector() {
        const face = testCase.faceFor();
        testCase.play([testCase.quiet, 0, 1]);
        tryCompare(face, "headline", "Quiet");
        compare(face.caption, "Level and rhythm");
        compare(face.dot, "ok");
    }

    function test_theTileOffIsOff() {
        const face = testCase.faceFor();
        SessionModel.setBlockEnabled("now", false);
        testCase.play([testCase.speaking, 0, 0]);
        tryCompare(face, "headline", "Off");
        compare(face.caption, "");
        compare(face.dot, "off");
    }

    function test_overloadIsNotWatching() {
        const face = testCase.faceFor();
        testCase.play([testCase.speaking, 0.9, 0]);
        tryCompare(face, "headline", "Speaking");
        TestEngine.setFakeOverload(testCase.sessionId, true);
        tryCompare(face, "headline", "Not watching");
        compare(face.caption, "Session overloaded");
        TestEngine.setFakeOverload(testCase.sessionId, false);
        tryCompare(face, "headline", "Speaking");
    }

    function test_anEngineThatGoesSilentIsNotWatchingWithinHalfASecond() {
        const face = testCase.faceFor();
        testCase.play([testCase.speaking, 0.9, 0]);
        tryCompare(face, "headline", "Speaking");
        TestEngine.setFakeFrozen(testCase.sessionId, true);
        verify(Meters.live, "the memory still says it runs");
        tryCompare(face, "headline", "Not watching", 900);
        compare(face.caption, "Engine not answering");
        compare(face.dot, "off");
    }

    // --- the card ---

    function test_theCardsWordIsTheTiles() {
        const panel = testCase.panelFor();
        compare(panel.statusLabel.text, "Not watching");
        compare(panel.voiceLabel.text, "Not live");
        testCase.play([testCase.speaking, 0.84, 0]);
        tryCompare(panel.statusLabel, "text", "Speaking");
        tryCompare(panel.voiceLabel, "text", "84 %");
        compare(panel.statusRow.Accessible.description, "Speaking");
    }

    function test_theVoiceModelIsGreyedWithItsReason() {
        const panel = testCase.panelFor();
        testCase.play([testCase.quiet, 0, 2]);
        tryCompare(panel.detectorReason, "text", "Not in this build");
        verify(!panel.detectorChoices.itemAt(0).enabled);
        verify(panel.detectorChoices.itemAt(1).checked, "what runs is what shows");
        TestEngine.setFakeBlockValues(testCase.sessionId, "now", [testCase.quiet, 0, 3]);
        tryCompare(panel.detectorReason, "text", "Needs 48 kHz");
        TestEngine.setFakeBlockValues(testCase.sessionId, "now", [testCase.quiet, 0, 0]);
        tryVerify(() => panel.detectorChoices.itemAt(0).enabled, 1000);
        verify(!panel.detectorReason.visible);
    }

    function test_choosingADetectorIsAnEdit() {
        const panel = testCase.panelFor();
        panel.detectorChoices.itemAt(1).click();
        compare(SessionModel.param("now", "detector"), "levelAndRhythm");
    }

    function test_theNameIsTheBlocksNickname() {
        const panel = testCase.panelFor();
        compare(panel.nameField.placeholderText, "For example Call or You");
        compare(panel.nameField.maximumLength, 24);
        panel.nameField.forceActiveFocus();
        keyClick(Qt.Key_C);
        keyClick(Qt.Key_Return);
        compare(SessionModel.block("now").nickname, "c");
    }

    function test_bothSurfacesStartOffAndAToggleIsAnEditTheyHear() {
        const panel = testCase.panelFor();
        verify(!panel.floatingSwitch.checked);
        verify(!panel.traySwitch.checked);
        verify(!FloatingSurface.wanted, "a window must not appear unasked");

        panel.traySwitch.click();
        tryVerify(() => TrayState.meters.some(m => m.blockId === "now"), 1000, "the tray did not hear of it");

        panel.floatingSwitch.click();
        tryVerify(() => FloatingSurface.wanted, 1000, "the surface did not hear of it");
        panel.floatingSwitch.click();
        tryVerify(() => !FloatingSurface.wanted, 1000);
    }

    function test_theCardSaysTheDesktopMayNotKeepItOnTop() {
        const panel = testCase.panelFor();
        verify(FloatingWindows.layer === null, "the layer shell was loaded offscreen");
        verify(!FloatingWindows.keepsOnTop);
        compare(FloatingWindows.window, FloatingWindows.plain);
        compare(panel.onTopLine.text, "This desktop may not keep it on top");
    }

    function test_thePlaceIsWrittenWhereTheLabelReadsIt() {
        const panel = testCase.panelFor();
        panel.placeSection.expanded = true;
        waitForRendering(panel);
        compare(panel.screenChoice.displayText, "The window's");
        panel.leftField.valueEdited(1480);
        compare(FloatingSurface.placement("label").left, 1480);
        panel.topField.valueEdited(90);
        compare(FloatingSurface.placement("label").top, 90);
        panel.sizeChoices.itemAt(1).click();
        compare(FloatingSurface.placement("label").size, "large");
    }

    function test_theTimingRowsWriteTheirParameters() {
        const panel = testCase.panelFor();
        panel.timingSection.expanded = true;
        waitForRendering(panel);
        compare(panel.holdRow.value, 200);
        panel.holdRow.field.valueEdited(600);
        compare(SessionModel.param("now", "holdMs"), 600);
        panel.markRow.field.valueEdited(0);
        compare(SessionModel.param("now", "quietAgainSeconds"), 0);
        verify(panel.markRow.noteShown);
        compare(panel.markRow.lowestNote.text, "Off");
        panel.markRow.field.valueEdited(2.5);
        compare(SessionModel.param("now", "quietAgainSeconds"), 2.5);
        panel.shortestRow.field.valueEdited(500);
        compare(SessionModel.param("now", "shortestTalkMs"), 500);
        SessionModel.resetParams("now", ["holdMs", "quietAgainSeconds", "shortestTalkMs"]);
    }

    function test_theRangesAreTheCatalogues() {
        const panel = testCase.panelFor();
        compare(BlockCatalogue.describeParam("meter.speechnow", "holdMs").maximum, panel.longestHold);
        compare(BlockCatalogue.describeParam("meter.speechnow", "quietAgainSeconds").maximum, panel.longestMark);
        compare(BlockCatalogue.describeParam("meter.speechnow", "shortestTalkMs").maximum, panel.longestShortTalk);
    }

    function test_theInfoLineStaysAndClaimsNothing() {
        const panel = testCase.panelFor();
        verify(panel.infoLine.text.indexOf("not who it is") >= 0);
    }

    function test_noWordIsAClaimAboutAConversation() {
        const words = createTemporaryObject(wordsComponent, testCase);
        for (const kind of ["speaking", "again", "quiet", "notWatching", "off"]) {
            const said = (words.word(kind) + " " + words.spokenPart("Call", kind)).toLowerCase();
            for (const claim of ["finished", "your turn", "go ahead", "you can talk"]) {
                verify(said.indexOf(claim) < 0, said);
            }
        }
    }

    Component {
        id: wordsComponent

        SpeechNowWords {}
    }

    // --- the floating label ---

    function test_thePlainWindowTakesNoFocusAndAsksToStayOnTop() {
        const window = createTemporaryObject(plainWindow, testCase);
        verify(window.flags & Qt.WindowStaysOnTopHint);
        verify(window.flags & Qt.WindowDoesNotAcceptFocus);
        verify(!(window.flags & Qt.WindowTransparentForInput));
        verify(!window.movedByMargins);
    }

    function test_theStripPassesClicksAndMovesInPercent() {
        const window = createTemporaryObject(plainWindow, testCase, { surfaceName: FloatingWindows.strip });
        verify(window.flags & Qt.WindowTransparentForInput);
        verify(window.inPercent);
        FloatingSurface.setPlacement("strip", { left: 50, top: 0 });
        compare(window.edge(50, 1000, 100), 450, "of the room left, so 50 is the middle");
        compare(window.edge(100, 1000, 100), 900, "kept on the screen");
        window.moveBy((window.placedOn.width - window.width) / 10, (window.placedOn.height - window.height) / 20);
        fuzzyCompare(FloatingSurface.placement("strip").left, 60, 0.01);
        fuzzyCompare(FloatingSurface.placement("strip").top, 5, 0.01);
        FloatingSurface.setPlacement("strip", {});
    }

    function test_theLabelMovesInPixelsAndStaysOnTheScreen() {
        const window = createTemporaryObject(plainWindow, testCase);
        verify(!window.inPercent);
        compare(window.edge(1480, 1920, 168), 1480);
        compare(window.edge(5000, 1920, 168), 1752);
        window.moveBy(10, -5);
        compare(FloatingSurface.placement("label").left, 50);
        compare(FloatingSurface.placement("label").top, 35);
        FloatingSurface.setPlacement("label", {});
    }

    function test_withNoTrayClosingTheWindowTakesTheLabelToo() {
        SessionModel.setParams("now", [["showFloating", true]]);
        tryVerify(() => FloatingSurface.wanted, 1000);
        const hadTray = TrayState.available;
        TrayState.available = false;
        const window = createTemporaryObject(mainWindow, testCase);
        tryVerify(() => window.floatingLabel.item !== null, 1000, "the label did not open");
        verify(window.floatingLabel.item.visible);
        window.close();
        verify(!window.floatingLabel.active, "a floating window kept the application running");
        TrayState.available = hadTray;
    }

    function test_withATrayTheLabelOutlastsTheWindow() {
        SessionModel.setParams("now", [["showFloating", true]]);
        tryVerify(() => FloatingSurface.wanted, 1000);
        const hadTray = TrayState.available;
        TrayState.available = true;
        const window = createTemporaryObject(mainWindow, testCase);
        tryVerify(() => window.floatingLabel.item !== null, 1000, "the label did not open");
        window.close();
        verify(window.floatingLabel.active);
        compare(window.floatingLabel.item.transientParent, null);
        TrayState.available = hadTray;
    }

    function test_theLabelShowsARowForEachBlockAndItsWord() {
        SessionModel.setParams("now", [["showFloating", true]]);
        SessionModel.renameBlock("now", "Call");
        tryVerify(() => FloatingSurface.rows.length === 1 && FloatingSurface.rows[0].nickname === "Call", 1000);
        const window = createTemporaryObject(plainWindow, testCase, { contentComponent: labelContent });
        testCase.play([testCase.speaking, 0.9, 0]);
        tryVerify(() => FloatingSurface.rows[0].state === testCase.speaking, 1000);
        compare(window.width, 168);
        compare(window.height, 28);
        FloatingSurface.setPlacement("label", { size: "large" });
        tryCompare(window, "width", 264);
        compare(window.height, 44);
    }

    function test_theLabelSaysNotWatchingWhenTheSessionStops() {
        const content = createTemporaryObject(labelContent, testCase);
        SessionModel.setParams("now", [["showFloating", true]]);
        testCase.play([testCase.speaking, 0.9, 0]);
        tryVerify(() => content.rows.count === 1 && content.rows.itemAt(0).shownWord === "Speaking", 1000);
        TestEngine.stop(testCase.sessionId);
        tryVerify(() => content.rows.itemAt(0)?.shownWord === "Not watching", 1000);
    }
}
