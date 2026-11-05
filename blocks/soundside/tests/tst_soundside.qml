// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Sound Side block's tile, card and strip offscreen, against the mock
// engine: every state, every word, and one reading for all of them. The word's
// timing is in SoundSideReading's test, what it publishes in blocks.SoundSide.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "SoundSide"
    when: windowShown
    visible: true

    property string sessionId: ""

    readonly property var words: SoundSideWords {}
    readonly property int quiet: 0
    readonly property int sounding: 1
    readonly property int alike: 2

    Component {
        id: overlayContent

        SoundSideOverlay {}
    }

    Component {
        id: plainWindow

        FloatingWindow {}
    }

    function panelFor() {
        const panel = createTemporaryObject(BlockRegister.panelFor("meter.soundside"), testCase, { width: 340 });
        panel.blockId = "sides";
        return panel;
    }

    function faceFor() {
        const face = createTemporaryObject(BlockRegister.faceFor("meter.soundside"), testCase, { width: 200, height: 90 });
        face.blockId = "sides";
        return face;
    }

    function edit(command) {
        TestEngine.applyEdit(testCase.sessionId, JSON.stringify(command));
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Sound side");
        verify(testCase.sessionId !== "", "the mock opened a session");
        testCase.edit({ op: "addBlock", block: { id: "game", type: "source.system" }, index: -1 });
        testCase.edit({ op: "addBlock", block: { id: "sides", type: "meter.soundside" }, index: -1 });
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        SessionModel.setParams("sides", [["showStrip", false], ["band", "all"], ["gateDb", -50],
                                         ["fromHz", 1000], ["toHz", 6000]]);
        SessionModel.setBlockEnabled("sides", true);
    }

    function cleanup() {
        TestEngine.setFakeOverload(testCase.sessionId, false);
        TestEngine.setFakeFrozen(testCase.sessionId, false);
        TestEngine.setFakeBlockValues(testCase.sessionId, "sides", []);
        TestEngine.stop(testCase.sessionId);
        SessionModel.setParams("sides", [["showStrip", false]]);
        FloatingWindows.endPlacing(false);
        FloatingWindows.sweeping = false;
        FloatingWindows.sweepTimer.stop();
        FloatingSurface.setPlacement("strip", { left: 50, top: 4, size: "medium", screen: "", show: "mark" });
    }

    function play(pan, level, width, state) {
        TestEngine.start(testCase.sessionId);
        TestEngine.setFakeBlockValues(testCase.sessionId, "sides", [pan, level, width, state]);
    }

    // --- the tile: every row of the table ---

    function test_quiet() {
        const face = testCase.faceFor();
        testCase.play(0, -64, 0, testCase.quiet);
        tryCompare(face, "kind", "quiet");
        compare(face.headline, "Quiet");
        compare(face.caption, "-64 dBFS");
        compare(face.dot, "ok");
        verify(!face.strip.mark.visible, "silence is never a centred mark");
    }

    function test_noSoundAtAll() {
        const face = testCase.faceFor();
        testCase.play(0, -120, 0, testCase.quiet);
        tryCompare(face, "caption", "No sound");
    }

    function test_left() {
        const face = testCase.faceFor();
        testCase.play(-0.62, -31, 0.2, testCase.sounding);
        tryCompare(face, "headline", "Left");
        compare(face.caption, "62 % · -31 dBFS");
        compare(face.dot, "live");
        verify(face.strip.mark.visible);
        verify(face.strip.mark.x + face.strip.mark.width / 2 < face.strip.track.width / 2);
        compare(face.Accessible.name, "Left, 62 % · -31 dBFS");
    }

    function test_right() {
        const face = testCase.faceFor();
        testCase.play(0.8, -20, 1, testCase.sounding);
        tryCompare(face, "headline", "Right");
        verify(face.strip.mark.x > face.strip.track.width / 2);
    }

    function test_centre() {
        const face = testCase.faceFor();
        testCase.play(0.03, -28, 0.9, testCase.sounding);
        tryCompare(face, "headline", "Centre");
        compare(face.caption, "3 % · -28 dBFS");
        compare(face.dot, "live");
        verify(face.strip.mark.width < face.strip.track.width / 4, "one place is narrow");
    }

    function test_both() {
        const face = testCase.faceFor();
        testCase.play(0, -24, 0.1, testCase.sounding);
        tryCompare(face, "headline", "Both");
        compare(face.caption, "wide · -24 dBFS");
        verify(face.strip.pale);
        fuzzyCompare(face.strip.mark.width, face.strip.track.width * 0.45, 2, "no one place is wide");
    }

    function test_sidesAlike() {
        const face = testCase.faceFor();
        testCase.play(0, -30, 1, testCase.alike);
        tryCompare(face, "headline", "Same");
        compare(face.caption, "Sides identical");
        compare(face.dot, "warning");
        verify(!face.strip.mark.visible);
    }

    function test_stopped() {
        const face = testCase.faceFor();
        tryCompare(face, "headline", "Not watching");
        compare(face.caption, "Session stopped");
        compare(face.dot, "off");
        verify(!face.strip.mark.visible);
    }

    function test_overloadedAndDead() {
        const face = testCase.faceFor();
        testCase.play(-0.8, -20, 1, testCase.sounding);
        tryCompare(face, "headline", "Left");
        TestEngine.setFakeOverload(testCase.sessionId, true);
        tryCompare(face, "headline", "Not watching");
        compare(face.caption, "Session overloaded");
        verify(!face.strip.mark.visible, "a plate stays and the track empties");
        TestEngine.setFakeOverload(testCase.sessionId, false);
        tryCompare(face, "headline", "Left");
        TestEngine.setFakeFrozen(testCase.sessionId, true);
        tryCompare(face, "headline", "Not watching", 2000);
        compare(face.caption, "Engine not answering");
    }

    function test_blockOff() {
        const face = testCase.faceFor();
        testCase.play(-0.8, -20, 1, testCase.sounding);
        tryCompare(face, "headline", "Left");
        SessionModel.setBlockEnabled("sides", false);
        tryCompare(face, "headline", "Off");
        compare(face.caption, "");
        compare(face.dot, "off");
        verify(!face.strip.visible, "hidden");
    }

    function test_aMarkFadesAndNeverGoesToTheMiddle() {
        const face = testCase.faceFor();
        testCase.play(-0.8, -20, 1, testCase.sounding);
        tryCompare(face, "headline", "Left");
        const x = face.strip.mark.x;
        TestEngine.setFakeBlockValues(testCase.sessionId, "sides", [0, -80, 0, testCase.quiet]);
        tryVerify(() => face.strip.markStrength < 1 && face.strip.markStrength > 0, 1000, "it never faded");
        compare(face.strip.mark.x, x, "it fades where it stood");
        tryCompare(face, "headline", "Quiet", 1000);
        verify(!face.strip.mark.visible);
    }

    // --- one reading for every surface ---

    function test_tileCardAndStripSayTheSame() {
        const face = testCase.faceFor();
        const panel = testCase.panelFor();
        const overlay = createTemporaryObject(overlayContent, testCase);
        SessionModel.setParams("sides", [["showStrip", true]]);
        tryCompare(FloatingSurface.strip, "blockId", "sides");
        testCase.play(-0.5, -25, 0.8, testCase.sounding);
        tryCompare(face, "headline", "Left");
        compare(panel.sideLabel.text, "Left 50 %");
        tryCompare(overlay.strip, "word", "Left");
        compare(overlay.strip.markStrength, panel.sideStrip.markStrength);
        compare(overlay.strip.pan, panel.sideStrip.pan);
    }

    // --- the card ---

    function test_theCardsStates() {
        const panel = testCase.panelFor();
        tryCompare(panel.statusLabel, "text", "Not watching");
        compare(panel.sideLabel.text, "Not watching", "the tile's word, and no other");
        compare(panel.levelLabel.text, "");
        testCase.play(0, -64, 0, testCase.quiet);
        tryCompare(panel.statusLabel, "text", "Quiet");
        compare(panel.levelLabel.text, "-64 dBFS");
        testCase.play(0.3, -30, 1, testCase.sounding);
        tryCompare(panel.statusLabel, "text", "Sounding");
        tryCompare(panel.sideLabel, "text", "Right 30 %");
        compare(panel.sideStrip.Accessible.description, "right 30 percent");
        compare(panel.sideStrip.Accessible.name, "Side now");
        testCase.play(0, -30, 1, testCase.alike);
        tryCompare(panel.statusLabel, "text", "Sides alike");
        verify(panel.alikeLine.visible);
        verify(panel.alikeLine.text.indexOf("before One Ear Mix") > 0);
    }

    function test_sidesIdenticalIsSaidOnce() {
        const panel = testCase.panelFor();
        Announcer.reset();
        testCase.play(0, -30, 1, testCase.alike);
        tryCompare(Announcer, "last", "Sides identical");
        verify(panel.alikeLine.visible);
    }

    function test_directionIsNeverAnnounced() {
        const panel = testCase.panelFor();
        Announcer.reset();
        testCase.play(-0.5, -30, 1, testCase.sounding);
        tryCompare(panel.statusLabel, "text", "Sounding");
        wait(200);
        compare(Announcer.last, "");
    }

    function test_theBandShowsFromAndTo() {
        const panel = testCase.panelFor();
        verify(!panel.fromRow.visible);
        panel.chooseBand("range");
        tryVerify(() => panel.fromRow.visible && panel.toRow.visible, 1000);
    }

    function test_fromAndToAreWrittenTogetherAnOctaveApart() {
        const panel = testCase.panelFor();
        panel.moveEdge(false, 4000, false);
        tryCompare(panel, "from", 4000);
        tryCompare(panel, "to", 8000);
        panel.moveEdge(true, 1000, false);
        tryCompare(panel, "to", 1000);
        tryCompare(panel, "from", 500);
    }

    function test_theRowsAreTheCataloguesRanges() {
        const panel = testCase.panelFor();
        const range = id => BlockCatalogue.describeParam("meter.soundside", id);
        compare(panel.lowestGate, range("gateDb").minimum);
        compare(panel.highestGate, range("gateDb").maximum);
        compare(panel.lowestFrom, range("fromHz").minimum);
        compare(panel.highestFrom, range("fromHz").maximum);
        compare(panel.lowestTo, range("toHz").minimum);
        compare(panel.highestTo, range("toHz").maximum);
    }

    function test_theGateLineStandsAtTheGate() {
        const panel = testCase.panelFor();
        compare(panel.levelMeter.from, -70);
        SessionModel.setParam("sides", "gateDb", -35);
        tryCompare(panel, "gate", -35);
        fuzzyCompare(panel.gateLine.x, panel.levelMeter.width * 0.5, 1);
    }

    function test_oneSideOnlyIsNotSaidTooSoon() {
        const panel = testCase.panelFor();
        testCase.play(-1, -30, 0, testCase.sounding);
        tryCompare(panel.statusLabel, "text", "Sounding");
        verify(!panel.oneSideLine.visible);
    }

    function test_theCardSaysTheStripMayNotStayOnTop() {
        const panel = testCase.panelFor();
        compare(panel.onTopLine.visible, !FloatingWindows.keepsOnTop);
        compare(panel.onTopLine.text, "May not stay on top here");
    }

    function test_aSecondBlockIsToldTheStripIsTaken() {
        testCase.edit({ op: "addBlock", block: { id: "first", type: "meter.soundside", params: { showStrip: true } }, index: 0 });
        tryCompare(FloatingSurface.strip, "blockId", "first");
        const panel = testCase.panelFor();
        verify(!panel.takenLine.visible);
        SessionModel.setParams("sides", [["showStrip", true]]);
        tryVerify(() => panel.takenLine.visible, 1000);
        testCase.edit({ op: "removeBlock", blockId: "first" });
        tryVerify(() => !panel.takenLine.visible, 1000);
    }

    // --- Move Before One Ear Mix ---

    function test_oneEarMixBeforeTheBlockIsNamedAndMoved() {
        const panel = testCase.panelFor();
        verify(!panel.moveButton.visible);
        testCase.edit({ op: "addBlock", block: { id: "fold", type: "hearing.oneearmix" }, index: -1 });
        testCase.edit({ op: "addBlock", block: { id: "out", type: "output.device" }, index: -1 });
        verify(SessionModel.connectLanes("game", "fold", 2));
        verify(SessionModel.connectLanes("fold", "sides", 2));
        verify(SessionModel.connectLanes("sides", "out", 2));
        tryCompare(panel, "foldBefore", "fold");
        verify(panel.moveButton.visible);
        compare(panel.moveButton.text, "Move Before One Ear Mix");
        compare(panel.moveLine.text, "One Ear Mix comes before this block, so the sides may be gone by here");

        panel.moveButton.clicked();
        tryCompare(panel, "foldBefore", "");
        verify(!panel.moveButton.visible);
        verify(SessionModel.hasWire("game", 0, "sides", 0));
        verify(SessionModel.hasWire("sides", 1, "fold", 1));
        verify(SessionModel.hasWire("fold", 0, "out", 0));
        verify(!SessionModel.hasWire("fold", 0, "sides", 0));

        SessionModel.undo();
        tryCompare(panel, "foldBefore", "fold");
        verify(SessionModel.hasWire("fold", 0, "sides", 0), "one step of undo");
        SessionModel.removeBlocks(["fold", "out"]);
        SessionModel.disconnectLanes("game", "sides");
    }

    // --- the strip ---

    function test_theStripsSizesAreTheTable() {
        const small = AppMetrics.stripSize("small");
        const large = AppMetrics.stripSize("large");
        compare([small.width, small.height, small.word, small.mark], [360, 24, 12, 5]);
        const medium = AppMetrics.stripSize("medium");
        compare([medium.width, medium.height, medium.word, medium.mark], [540, 36, 16, 7]);
        compare([large.width, large.height, large.word, large.mark], [810, 54, 24, 10]);
    }

    function test_theOverlayFollowsThePlacement() {
        const overlay = createTemporaryObject(overlayContent, testCase);
        compare(overlay.implicitWidth, 540);
        compare(overlay.implicitHeight, 36);
        compare(overlay.strip.show, "mark");
        FloatingSurface.setPlacement("strip", { size: "large", show: "bars" });
        compare(overlay.implicitWidth, 810);
        compare(overlay.strip.show, "bars");
        compare(overlay.strip.wordSize, 24);
    }

    function test_thePlateKeepsItsLookInEveryState() {
        const overlay = createTemporaryObject(overlayContent, testCase);
        const plate = overlay.strip.color;
        for (const kind of ["quiet", "left", "same", "stopped"]) {
            overlay.strip.shown = { kind: kind, strength: kind === "left" ? 1 : 0 };
            compare(overlay.strip.color, plate, "nothing flashes");
            compare(overlay.strip.opacity, 1);
        }
        verify(Qt.colorEqual(AppColors.overlayPlate, Qt.rgba(0.07, 0.08, 0.09, 0.85)));
    }

    function test_theStripSaysItsOwnWords() {
        const expected = { quiet: "Quiet", left: "Left", centre: "Centre", right: "Right", both: "Both",
                           same: "Same", stopped: "Stopped", off: "Stopped" };
        for (const kind in expected) {
            compare(testCase.words.stripWord(kind), expected[kind]);
        }
    }

    function test_noForbiddenWordCanBeShown() {
        const kinds = ["quiet", "left", "right", "centre", "both", "same", "stopped", "off"];
        for (const kind of kinds) {
            const spoken = [testCase.words.tileWord(kind), testCase.words.stripWord(kind), testCase.words.status(kind),
                            testCase.words.sideNow(kind, 50), testCase.words.sideSpoken(kind, 50)].join(" ");
            verify(!/enem|safe|protect|restor|direction|hearing/i.test(spoken), spoken);
        }
    }

    function test_barsShowEachSidesLevelFromTheGate() {
        // -30 dBFS, on the left alone: left is 3 dB up, right has nothing.
        fuzzyCompare(testCase.words.barShare(-30, -1, -50, -1), 23.01 / 50, 0.01);
        compare(testCase.words.barShare(-30, -1, -50, 1), 0);
        // Centred, each side reads the level.
        fuzzyCompare(testCase.words.barShare(-25, 0, -50, -1), 0.5, 0.01);
        fuzzyCompare(testCase.words.barShare(-25, 0, -50, 1), 0.5, 0.01);
        // It stops at the ends.
        compare(testCase.words.barShare(0, -1, -50, -1), 1);

        const overlay = createTemporaryObject(overlayContent, testCase);
        overlay.strip.show = "bars";
        overlay.strip.gate = -50;
        overlay.strip.shown = { kind: "left", pan: -0.5, strength: 1, markLevel: -20, ghosts: [] };
        verify(overlay.strip.leftBar.visible && overlay.strip.rightBar.visible);
        verify(!overlay.strip.mark.visible);
        verify(overlay.strip.leftBar.width > overlay.strip.rightBar.width);
        verify(Qt.colorEqual(overlay.strip.leftBar.color, AppColors.overlayMark), "the louder one is live");
        verify(Qt.colorEqual(overlay.strip.rightBar.color, AppColors.overlayMuted));
    }

    function test_testStripSweepsOnTheStripAlone() {
        const face = testCase.faceFor();
        const overlay = createTemporaryObject(overlayContent, testCase);
        verify(!FloatingWindows.stripInUse);
        const panel = testCase.panelFor();
        panel.testButton.clicked();
        verify(FloatingWindows.sweeping);
        verify(FloatingWindows.stripInUse, "the strip comes up for it though no block asked");
        compare(overlay.strip.word, "Left");
        verify(overlay.strip.pan < 0);
        compare(face.headline, "Not watching", "the tile does not move");
        tryCompare(overlay.strip, "word", "Centre", 1500);
        tryCompare(overlay.strip, "word", "Right", 1500);
        tryVerify(() => !FloatingWindows.sweeping, 1500, "three seconds and it stops");
    }

    // --- placing it ---

    function stripWindow() {
        const surface = createTemporaryObject(plainWindow, testCase, {
            surfaceName: FloatingWindows.strip, contentComponent: overlayContent
        });
        surface.visible = true;
        return surface;
    }

    function test_stripIsInPercentOfTheRoomLeft() {
        const surface = testCase.stripWindow();
        verify(surface.inPercent);
        verify(surface.clickThrough);
        compare(surface.edge(50, 1920, 540), 690, "50 is the middle at any size");
        compare(surface.edge(100, 1920, 540), 1380, "100 is the far edge");
        compare(surface.edge(0, 1920, 540), 0);
    }

    function test_placingTakesTheKeysAndEnterKeeps() {
        const surface = testCase.stripWindow();
        const across = surface.placedOn.width - surface.width;
        const panel = testCase.panelFor();
        panel.placeButton.clicked();
        verify(FloatingWindows.placing);
        verify(surface.placing);
        verify(!surface.clickThrough, "it takes clicks while it is placed");
        verify(FloatingWindows.stripInUse);
        verify(surface.placingKeys.press(Qt.Key_Right));
        verify(surface.placingKeys.press(Qt.Key_End));
        const placement = FloatingSurface.placement("strip");
        fuzzyCompare(placement.left, 50 + 60 * 100 / across, 0.001);
        verify(!surface.placingKeys.press(Qt.Key_A), "other keys mean nothing");
        verify(surface.placingKeys.press(Qt.Key_Return));
        verify(!FloatingWindows.placing);
        verify(surface.clickThrough);
        fuzzyCompare(FloatingSurface.placement("strip").left, placement.left, 0.001);
    }

    function test_escPutsItBack() {
        const surface = testCase.stripWindow();
        FloatingWindows.startPlacing();
        surface.placingKeys.press(Qt.Key_Down);
        surface.placingKeys.press(Qt.Key_PageDown);
        verify(FloatingSurface.placement("strip").top > 4);
        surface.placingKeys.press(Qt.Key_Escape);
        compare(FloatingSurface.placement("strip").top, 4);
        compare(FloatingSurface.placement("strip").left, 50);
    }

    function test_aSecondPressKeepsThePlaceEscGoesBackTo() {
        const surface = testCase.stripWindow();
        const panel = testCase.panelFor();
        verify(panel.placeButton.enabled);
        panel.placeButton.clicked();
        verify(!panel.placeButton.enabled, "there is nothing to start twice");
        surface.placingKeys.press(Qt.Key_End);
        FloatingWindows.startPlacing();
        surface.placingKeys.press(Qt.Key_Escape);
        compare(FloatingSurface.placement("strip").left, 50);
        verify(panel.placeButton.enabled);
    }

    function test_theWordsShowWhilePlacing() {
        const overlay = createTemporaryObject(overlayContent, testCase);
        FloatingWindows.startPlacing();
        verify(overlay.placingWords.visible);
        compare(overlay.placingWords.text, "Arrows move · Enter keeps · Esc puts it back");
        verify(overlay.implicitHeight > 36, "the words have a line of their own");
        FloatingWindows.endPlacing(false);
        compare(overlay.implicitHeight, 36);
    }

    function test_aWindowThatGoesEndsThePlacing() {
        const surface = testCase.stripWindow();
        FloatingWindows.startPlacing();
        surface.destroy();
        tryVerify(() => !FloatingWindows.placing, 1000);
    }

    function test_thePlacementSectionIsInPercent() {
        const panel = testCase.panelFor();
        const place = panel.placeSection;
        compare(place.surfaceName, "strip");
        compare(place.note, "50 %, 4 %");
        compare(place.leftField.to, 100);
        compare(place.leftField.stepSize, 1);
        compare(place.leftField.pageStepSize, 5);
        place.leftField.valueEdited(20);
        compare(FloatingSurface.placement("strip").left, 20);
        compare(place.note, "20 %, 4 %");
    }
}
