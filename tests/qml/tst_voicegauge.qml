// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Voice Gauge block's tile, card, bar and floating row offscreen, against
// the mock engine: every state and every word, and that no colour depends on
// where the reading stands. What it publishes is in blocks.VoiceGauge.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "VoiceGauge"
    when: windowShown
    visible: true

    property string sessionId: ""

    readonly property var words: VoiceGaugeWords {}

    Component {
        id: labelContent

        FloatingLabel {}
    }

    Component {
        id: plainWindow

        FloatingWindow {}
    }

    /// The state number: sound, voice now, position, seconds, detector.
    function state(sound, voiceNow, position, seconds = 0, detector = 0) {
        return sound + (voiceNow ? 10 : 0) + position * 100 + seconds * 1000 + detector * 100000;
    }

    function marks(upper, lower) {
        return (upper + 61) * 100 + (lower + 61);
    }

    readonly property int noSound: 0
    readonly property int room: 1
    readonly property int voice: 2

    function panelFor() {
        const panel = createTemporaryObject(BlockPanels.panelFor("meter.voicegauge"), testCase, { width: 340 });
        panel.blockId = "gauge";
        return panel;
    }

    function faceFor() {
        const face = createTemporaryObject(BlockFaces.faceFor("meter.voicegauge"), testCase, { width: 200, height: 90 });
        face.blockId = "gauge";
        return face;
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Voice gauge");
        verify(testCase.sessionId !== "", "the mock opened a session");
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "mic", type: "source.device" }, index: -1 }));
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "gauge", type: "meter.voicegauge" }, index: -1 }));
        TestEngine.applyEdit(testCase.sessionId, JSON.stringify({
            op: "connect", wire: { fromBlock: "mic", fromPort: 0, toBlock: "gauge", toPort: 0 }, index: -1
        }));
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        SessionModel.setParams("gauge", [["showFloating", false], ["showInTray", false], ["detector", "model"],
                                         ["upperMark", -61], ["lowerMark", -61], ["marksPlacedOn", ""],
                                         ["roomEachSide", 3]]);
        SessionModel.setBlockEnabled("gauge", true);
        SessionModel.renameBlock("gauge", "");
    }

    function cleanup() {
        TestEngine.setFakeOverload(testCase.sessionId, false);
        TestEngine.setFakeFrozen(testCase.sessionId, false);
        TestEngine.setFakeBlockValues(testCase.sessionId, "gauge", []);
        TestEngine.stop(testCase.sessionId);
        Meters.watch("");
        FloatingSurface.setPlacement("label", { left: 40, top: 40, size: "small", screen: "" });
    }

    function play(values) {
        Meters.watch(testCase.sessionId);
        TestEngine.start(testCase.sessionId);
        TestEngine.setFakeBlockValues(testCase.sessionId, "gauge", values);
        tryVerify(() => Meters.live, 1000, "the session never went live");
    }

    // --- the tile: every row of the table ---

    function test_aVoiceBetweenMarks() {
        const face = testCase.faceFor();
        testCase.play([testCase.state(testCase.voice, true, 1), -24, -120, testCase.marks(-21, -27)]);
        tryCompare(face, "headline", "-24 LUFS");
        compare(face.caption, "Between marks");
        compare(face.dot, "live");
        compare(face.bar.fillStrength, 1);
        verify(face.bar.fillShare > 0);
        compare(face.Accessible.name, "-24 LUFS, Between marks");
    }

    function test_aPauseIsHeldAtHalfStrength() {
        const face = testCase.faceFor();
        testCase.play([testCase.state(testCase.room, false, 1), -24, -120, testCase.marks(-21, -27)]);
        tryCompare(face, "headline", "-24 LUFS");
        compare(face.caption, "Held · between marks");
        compare(face.dot, "off");
        compare(face.bar.fillStrength, 0.5);
    }

    function test_everyPositionWord_data() {
        return [
            { tag: "above upper", position: 2, word: "Above upper mark" },
            { tag: "below upper", position: 3, word: "Below upper mark" },
            { tag: "above lower", position: 4, word: "Above lower mark" },
            { tag: "below lower", position: 5, word: "Below lower mark" },
            { tag: "no marks", position: 0, word: "No marks set" }
        ];
    }

    function test_everyPositionWord(data) {
        const face = testCase.faceFor();
        testCase.play([testCase.state(testCase.voice, true, data.position), -17, -120, 0]);
        tryCompare(face, "caption", data.word);
        compare(face.dot, "live");
    }

    function test_noNumberYet() {
        const face = testCase.faceFor();
        testCase.play([testCase.state(testCase.room, false, 0), -120, -120, 0]);
        tryCompare(face, "headline", "No reading yet");
        compare(face.caption, "Sound, no voice");
        compare(face.dot, "off");
        compare(face.bar.fillShare, 0);
        TestEngine.setFakeBlockValues(testCase.sessionId, "gauge", [testCase.state(testCase.voice, true, 0), -120, -120, 0]);
        tryCompare(face, "caption", "Talk to start");
    }

    function test_nothingArrives() {
        const face = testCase.faceFor();
        testCase.play([testCase.state(testCase.noSound, false, 1), -24, -120, testCase.marks(-21, -27)]);
        tryCompare(face, "headline", "No sound here");
        compare(face.caption, "At this point");
        compare(face.dot, "warning");
        compare(face.captionLabel.color, AppColors.warningText);
        compare(face.bar.fillShare, 0, "a held number here would say sound arrives");
    }

    function test_sessionStopped() {
        const face = testCase.faceFor();
        compare(face.headline, "Not watching");
        compare(face.caption, "Session stopped");
        compare(face.dot, "off");
        compare(face.bar.fillShare, 0);
    }

    function test_blockOff() {
        const face = testCase.faceFor();
        SessionModel.setBlockEnabled("gauge", false);
        testCase.play([testCase.state(testCase.voice, true, 1), -24, -120, 0]);
        tryCompare(face, "headline", "Off");
        compare(face.caption, "");
        compare(face.dot, "off");
    }

    function test_overloadAndADeadEngineAreNotWatching() {
        const face = testCase.faceFor();
        testCase.play([testCase.state(testCase.voice, true, 1), -24, -120, 0]);
        tryCompare(face, "headline", "-24 LUFS");
        TestEngine.setFakeOverload(testCase.sessionId, true);
        tryCompare(face, "headline", "Not watching");
        compare(face.caption, "Session overloaded");
        TestEngine.setFakeOverload(testCase.sessionId, false);
        tryCompare(face, "headline", "-24 LUFS");
        TestEngine.setFakeFrozen(testCase.sessionId, true);
        tryCompare(face, "headline", "Not watching", 900);
        compare(face.caption, "Engine not answering");
    }

    // --- what must never change with the position ---

    function test_theTileNeverReturnsOk() {
        for (const sound of [testCase.noSound, testCase.room, testCase.voice]) {
            for (const position of [0, 1, 2, 3, 4, 5]) {
                for (const reading of [-120, -24]) {
                    const kind = testCase.words.kindOf("watching", true, testCase.state(sound, sound === testCase.voice, position), reading);
                    verify(testCase.words.dot(kind) !== "ok", kind);
                }
            }
        }
        for (const kind of ["voice", "held", "noReading", "noSound", "notWatching", "off"]) {
            verify(["live", "warning", "off"].indexOf(testCase.words.dot(kind)) >= 0, kind);
        }
    }

    function test_noColourDependsOnThePosition() {
        const face = testCase.faceFor();
        testCase.play([testCase.state(testCase.voice, true, 1), -24, -120, testCase.marks(-21, -27)]);
        tryCompare(face, "caption", "Between marks");
        const fill = face.bar.fill.color;
        const caption = face.captionLabel.color;
        const headline = face.headlineLabel.color;
        for (const [position, word] of [[2, "Above upper mark"], [5, "Below lower mark"], [1, "Between marks"]]) {
            TestEngine.setFakeBlockValues(testCase.sessionId, "gauge",
                                          [testCase.state(testCase.voice, true, position), -24, -120, testCase.marks(-21, -27)]);
            tryCompare(face, "caption", word);
            compare(face.bar.fill.color, fill, word);
            compare(face.bar.fillStrength, 1, word);
            compare(face.captionLabel.color, caption, word);
            compare(face.headlineLabel.color, headline, word);
            compare(face.dot, "live", word);
        }
    }

    function test_theBarIsNoAccentAndNoGreen() {
        const bar = createTemporaryObject(barComponent, testCase);
        compare(bar.fillColor, AppColors.voiceBar);
        compare(String(AppColors.voiceBar), String(AppColors.designPalette.accent));
        for (const palette of [AppColors.darkPalette, AppColors.lightPalette]) {
            const colour = Qt.color(palette.accent);
            verify(colour.b > colour.g && colour.b > colour.r, "blue is the largest part: " + colour);
        }
    }

    Component {
        id: barComponent

        VoiceGaugeBar {
            width: 100
        }
    }

    // --- the scale ---

    function test_twoMarksKeepOnePlaceOnTheTile() {
        const bar = createTemporaryObject(barComponent, testCase, { upper: -21, lower: -27 });
        fuzzyCompare(bar.place(-27), 1 / 3, 0.001);
        fuzzyCompare(bar.place(-21), 2 / 3, 0.001);
        const other = createTemporaryObject(barComponent, testCase, { upper: -30, lower: -32 });
        fuzzyCompare(other.place(-32), 1 / 3, 0.001);
        fuzzyCompare(other.place(-30), 2 / 3, 0.001);
    }

    function test_oneMarkSitsInTheMiddleAndNoneRunsTheWholeScale() {
        const one = createTemporaryObject(barComponent, testCase, { upper: -20 });
        fuzzyCompare(one.place(-20), 0.5, 0.001);
        fuzzyCompare(one.place(-32), 0, 0.001);
        const none = createTemporaryObject(barComponent, testCase);
        fuzzyCompare(none.place(-30), 0.5, 0.001);
    }

    function test_theCardKeepsTheWholeScale() {
        const bar = createTemporaryObject(barComponent, testCase, { upper: -21, lower: -27, wholeScale: true });
        fuzzyCompare(bar.place(-30), 0.5, 0.001);
    }

    // --- the card ---

    function test_theCardsWords_data() {
        return [
            { tag: "voice", values: [2 + 10 + 100, -24, -120, 0], status: "Voice at this point", meter: "-24 LUFS", position: "Between marks" },
            { tag: "held", values: [1 + 100, -24, -120, 0], status: "Held", meter: "-24 LUFS", position: "Between marks" },
            { tag: "room", values: [1, -120, -120, 0], status: "Sound at this point, no voice", meter: "No reading yet", position: "" },
            { tag: "counting", values: [2 + 10, -120, -120, 0], status: "No reading yet", meter: "No reading yet", position: "" },
            { tag: "silent", values: [0, -120, -120, 0], status: "No sound at this point", meter: "No sound here", position: "" }
        ];
    }

    function test_theCardsWords(data) {
        const panel = testCase.panelFor();
        testCase.play(data.values);
        tryCompare(panel.statusLabel, "text", data.status);
        compare(panel.meterLabel.text, data.meter);
        compare(panel.positionLabel.visible, data.position !== "");
        if (data.position !== "") {
            compare(panel.positionLabel.text, data.position);
        }
    }

    function test_aStoppedCardSaysNotWatchingAsTheTileDoes() {
        const panel = testCase.panelFor();
        compare(panel.statusLabel.text, "Not watching");
        compare(panel.meterLabel.text, "Not watching");
        compare(panel.statusRow.Accessible.description, "Not watching");
    }

    function test_theCardShowsOnlyTheWordsItMay() {
        const panel = testCase.panelFor();
        for (const values of [[0, -120, -120, 0], [1, -120, -120, 0], [12, -120, -120, 0], [1 + 100, -24, -120, 0]]) {
            testCase.play(values);
            tryVerify(() => Math.round(panel.reader.state) === values[0], 1000);
            verify(panel.meterLabel.text.toLowerCase().indexOf("live") < 0, panel.meterLabel.text);
            verify(panel.statusLabel.text.toLowerCase().indexOf("live") < 0, panel.statusLabel.text);
        }
        SessionModel.setBlockEnabled("gauge", false);
        tryCompare(panel.meterLabel, "text", "Off");
    }

    // A block switched off in a running session is not run, and its slots go
    // on saying what they said last while the frame moves.
    function offWithOldSlots(panel) {
        SessionModel.setParams("gauge", [["upperMark", -20], ["lowerMark", -30]]);
        testCase.play([testCase.state(testCase.voice, true, 1, 20), -24, -24, testCase.marks(-20, -30)]);
        tryVerify(() => panel.useButton.enabled, 1000);
        SessionModel.setBlockEnabled("gauge", false);
        tryVerify(() => !panel.useButton.enabled, 1000);
    }

    function test_aBlockOffCountsNothingAndOffersNoLevel() {
        const panel = testCase.panelFor();
        testCase.offWithOldSlots(panel);
        compare(panel.reader.secondsCounted, 0);
        compare(panel.useButton.text, "Use This Level (0 of 20 s)");
    }

    function test_aBlockOffShowsTheMarksTheSessionHolds() {
        const panel = testCase.panelFor();
        testCase.offWithOldSlots(panel);
        SessionModel.setParams("gauge", [["upperMark", -18], ["lowerMark", -28]]);
        tryCompare(panel.upperRow, "value", -18);
        compare(panel.lowerRow.value, -28);
        panel.clearMarks();
        tryCompare(panel.upperRow, "value", -61);
        verify(!panel.clearButton.enabled);
    }

    function test_aMarkMovedWhileOffKeepsTheOtherAsTheSessionHasIt() {
        const panel = testCase.panelFor();
        testCase.offWithOldSlots(panel);
        SessionModel.setParams("gauge", [["upperMark", -18], ["lowerMark", -28]]);
        tryCompare(panel.reader.marks, "lower", -28);
        panel.moveMark(false, -15, false);
        compare(SessionModel.param("gauge", "upperMark"), -15);
        compare(SessionModel.param("gauge", "lowerMark"), -28);
    }

    function test_aNewCardSuggestsNothing() {
        const panel = testCase.panelFor();
        compare(panel.upperRow.field.text, "Not set");
        compare(panel.lowerRow.field.text, "Not set");
        verify(panel.firstUseLine.visible);
        compare(panel.firstUseLine.text, "Talk as you do on a call. After 20 s of voice, Use This Level places two marks around that level");
        verify(!panel.useButton.enabled);
        verify(!panel.clearButton.enabled);
        verify(!panel.placedOnLine.visible);
    }

    function test_theButtonCountsSecondsOfVoice() {
        const panel = testCase.panelFor();
        testCase.play([testCase.state(testCase.voice, true, 0, 12), -24, -120, 0]);
        tryCompare(panel.useButton, "text", "Use This Level (12 of 20 s)");
        verify(!panel.useButton.enabled);
    }

    function test_useThisLevelPlacesWholeMarksAndNamesTheMicrophone() {
        const panel = testCase.panelFor();
        testCase.play([testCase.state(testCase.voice, true, 0, 20), -24.4, -24.4, 0]);
        tryVerify(() => panel.useButton.enabled, 1000);
        compare(panel.useButton.text, "Use This Level");
        panel.useButton.clicked();
        compare(SessionModel.param("gauge", "upperMark"), -21);
        compare(SessionModel.param("gauge", "lowerMark"), -27);
        const name = SessionModel.param("gauge", "marksPlacedOn");
        compare(name, panel.deviceName());
        if (name !== "") {
            verify(panel.placedOnLine.visible);
            compare(panel.placedOnLine.text, "Marks placed on: " + name);
        }
    }

    function test_theNoteNamesWhatIsStored() {
        const panel = testCase.panelFor();
        SessionModel.setParams("gauge", [["marksPlacedOn", "Headset"]]);
        tryVerify(() => panel.placedOnLine.visible, 1000);
        compare(panel.placedOnLine.text, "Marks placed on: Headset");
    }

    function test_roomEachSideSetsTheGap() {
        const panel = testCase.panelFor();
        SessionModel.setParams("gauge", [["roomEachSide", 5]]);
        testCase.play([testCase.state(testCase.voice, true, 0, 20), -24, -24, 0]);
        tryVerify(() => panel.useButton.enabled, 1000);
        panel.useButton.clicked();
        compare(SessionModel.param("gauge", "upperMark"), -19);
        compare(SessionModel.param("gauge", "lowerMark"), -29);
    }

    function test_clearMarksTakesBothAwayAndAsksNothing() {
        const panel = testCase.panelFor();
        SessionModel.setParams("gauge", [["upperMark", -20], ["lowerMark", -30], ["marksPlacedOn", "Headset"]]);
        tryVerify(() => panel.clearButton.enabled, 1000);
        panel.clearButton.clicked();
        compare(SessionModel.param("gauge", "upperMark"), -61);
        compare(SessionModel.param("gauge", "lowerMark"), -61);
        compare(SessionModel.param("gauge", "marksPlacedOn"), "");
    }

    function test_aMarkMovedPastTheOtherWritesBoth() {
        const panel = testCase.panelFor();
        SessionModel.setParams("gauge", [["upperMark", -20], ["lowerMark", -30]]);
        panel.moveMark(false, -35, false);
        compare(SessionModel.param("gauge", "upperMark"), -35);
        compare(SessionModel.param("gauge", "lowerMark"), -36);
        panel.moveMark(true, -10, false);
        compare(SessionModel.param("gauge", "lowerMark"), -10);
        compare(SessionModel.param("gauge", "upperMark"), -9);
    }

    function test_typingAMarkGoesThroughTheSameRule() {
        const panel = testCase.panelFor();
        SessionModel.setParams("gauge", [["upperMark", -20], ["lowerMark", -30]]);
        tryCompare(panel.upperRow.field, "text", "-20");
        panel.upperRow.field.valueEdited(-40);
        compare(SessionModel.param("gauge", "lowerMark"), -41);
    }

    function test_oneUndoTakesBackBothMarks() {
        const panel = testCase.panelFor();
        SessionModel.setParams("gauge", [["upperMark", -20], ["lowerMark", -30]]);
        panel.moveMark(false, -35, false);
        SessionModel.undo();
        tryVerify(() => SessionModel.param("gauge", "upperMark") === -20 && SessionModel.param("gauge", "lowerMark") === -30, 1000);
    }

    function test_theWordsOfTheRanges() {
        const panel = testCase.panelFor();
        const param = id => BlockCatalogue.params("meter.voicegauge").find(entry => entry.id === id);
        compare(param("upperMark").minimum, -61);
        compare(param("upperMark").maximum, -6);
        compare(param("lowerMark").maximum, -6);
        compare(param("readingSeconds").maximum, panel.longestReading);
        compare(param("forgetSeconds").maximum, panel.longestForget);
        compare(param("roomEachSide").maximum, panel.mostRoom);
        compare(panel.readingRow.spoken, "3 s of voice");
        compare(panel.forgetRow.spoken, "60 s");
        compare(panel.roomRow.spoken, "±3 LU");
    }

    function test_theDetectorIsGreyedWithItsReason() {
        const panel = testCase.panelFor();
        testCase.play([testCase.state(testCase.room, false, 0, 0, 3), -120, -120, 0]);
        tryVerify(() => panel.detectorReason.visible, 1000);
        compare(panel.detectorReason.text, "Needs 48 kHz");
        verify(!panel.detectorChoices.itemAt(0).enabled);
    }

    function test_theInfoLineIsAlwaysThere() {
        const panel = testCase.panelFor();
        verify(panel.infoLine.visible);
        compare(panel.infoLine.text, "A level at this point, before the call program. It can't see a mute inside that program, or what the other side hears. A voice from a loudspeaker counts too");
    }

    function test_theMeterIsAProgressBarWithANameAndAValue() {
        const panel = testCase.panelFor();
        compare(panel.meter.Accessible.role, Accessible.ProgressBar);
        compare(panel.meter.Accessible.name, "Voice level");
        compare(panel.meter.Accessible.description, "Not watching");
    }

    function test_theFloatingCheckBoxReachesTheSurface() {
        const panel = testCase.panelFor();
        panel.floatingSwitch.toggle();
        panel.floatingSwitch.toggled();
        tryVerify(() => FloatingSurface.rows.some(row => row.blockId === "gauge"), 1000);
        compare(panel.floatingSwitch.text, "Floating bar");
    }

    // --- the floating row, and the spoken and tray lines ---

    function test_aRowOnTheFloatingBarIsDrawnAsSpecified() {
        SessionModel.renameBlock("gauge", "You");
        SessionModel.setParams("gauge", [["showFloating", true]]);
        testCase.play([testCase.state(testCase.voice, true, 1), -24, -120, testCase.marks(-21, -27)]);
        const content = createTemporaryObject(labelContent, testCase);
        // Found by its block: other tests leave rows of their own marked.
        const find = () => {
            for (let index = 0; index < content.rows.count; ++index) {
                const item = content.rows.itemAt(index);
                if (item?.modelData.blockId === "gauge") {
                    return item.gauge;
                }
            }
            return null;
        };
        tryVerify(() => (find()?.number ?? "") === "-24 LUFS", 1000);
        const row = find();
        compare(row.name, "You");
        compare(row.shownWord, "Between marks");
        fuzzyCompare(row.bar.place(-27), 1 / 3, 0.001);
        compare(row.bar.fillColor, AppColors.voiceBar);
        compare(content.implicitWidth, 264);
        compare(row.height, 44);
        FloatingSurface.setPlacement("label", { size: "large" });
        tryCompare(content, "implicitWidth", 396);
        compare(row.height, 66);
        TestEngine.stop(testCase.sessionId);
        tryCompare(row, "number", "Not watching", 1000);
    }

    function test_aRowSwitchedOffDrawsNoOldMarks() {
        SessionModel.setParams("gauge", [["showFloating", true]]);
        testCase.play([testCase.state(testCase.voice, true, 1), -24, -120, testCase.marks(-21, -27)]);
        SessionModel.setBlockEnabled("gauge", false);
        const content = createTemporaryObject(labelContent, testCase);
        tryVerify(() => {
            for (let index = 0; index < content.rows.count; ++index) {
                const item = content.rows.itemAt(index);
                if (item?.modelData.blockId === "gauge" && item.gauge !== null) {
                    return item.gauge.number === "Off" && item.gauge.marks.upper === -61;
                }
            }
            return false;
        }, 1000);
    }

    function test_theFloatingWindowIsHiddenFromAScreenReader() {
        const content = createTemporaryObject(labelContent, testCase);
        verify(content.Accessible.ignored);
    }

    function test_theSpokenSentence() {
        const words = testCase.words;
        const between = testCase.state(testCase.voice, true, 1);
        compare(words.spoken("voice", between, -24), "Minus 24 LUFS, between marks");
        compare(words.spoken("noSound", 0, -24), "No sound here");
        compare(words.spoken("notWatching", 0, -24), "Not watching");
        compare(words.trayLine("voice", between, -24), "-24 LUFS, between marks");
        compare(words.trayLine("noSound", 0, -24), "No sound here");
    }

    function test_noForbiddenWordCanBeShown() {
        const words = testCase.words;
        let shown = [];
        for (let position = 0; position <= 5; ++position) {
            shown.push(words.position(position), words.positionInSentence(position));
        }
        for (const kind of ["voice", "held", "noReading", "noSound", "notWatching", "off"]) {
            for (const state of [0, 1, 2, 12]) {
                shown.push(words.headline(kind, -24), words.status(kind, state), words.line(kind, state, "stopped"));
            }
        }
        const all = shown.join(" | ").toLowerCase();
        for (const word of ["too loud", "too quiet", "speak up", "good", "normal", "your voice", "keep ", "live", "mic on", "on air", "muted"]) {
            verify(all.indexOf(word) < 0, word);
        }
    }

    // --- a plain window's place ---

    function test_aPlainWindowMovedByTheDesktopWritesItsPlaceOnce() {
        const window = createTemporaryObject(plainWindow, testCase, { contentComponent: labelContent, readsItsPlace: true });
        const on = window.screen;
        window.x = on.virtualX + 210;
        window.y = on.virtualY + 120;
        window.noteWhereItIs();
        compare(FloatingSurface.placement("label").left, 210);
        compare(FloatingSurface.placement("label").top, 120);
    }

    function test_aPlaceTheCardSetIsNotWrittenBack() {
        const window = createTemporaryObject(plainWindow, testCase, { contentComponent: labelContent, readsItsPlace: true });
        const revision = FloatingSurface.placementRevision;
        window.noteWhereItIs();
        compare(FloatingSurface.placementRevision, revision, "it sits where it was asked to");
    }

    function test_aWindowThatIsNotToldWritesNothing() {
        const window = createTemporaryObject(plainWindow, testCase, { contentComponent: labelContent, readsItsPlace: false });
        const revision = FloatingSurface.placementRevision;
        window.x = 300;
        window.noteWhereItIs();
        compare(FloatingSurface.placementRevision, revision);
        compare(FloatingSurface.placement("label").left, 40);
    }

    function test_moveEndsAreWaitedFor() {
        const window = createTemporaryObject(plainWindow, testCase, { contentComponent: labelContent, readsItsPlace: true });
        window.x = window.screen.virtualX + 333;
        tryCompare(FloatingSurface.placement("label"), "left", 40, 100);
        tryVerify(() => FloatingSurface.placement("label").left === 333, 1000);
    }
}
