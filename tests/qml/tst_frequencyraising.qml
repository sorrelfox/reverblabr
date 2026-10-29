// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Frequency Raising's face and panel, which are Frequency Lowering's
// going the other way: a Hearing block with a face and a panel of
// its own; the tile's summary and how its first move moves; three methods,
// harmonics among them, reaching the session; a destination kept above its
// source; the map's arithmetic the engine's, harmonics drawn as a line a
// harmonic; "when needed" named for low sounds; the delay its lowest move
// needs; the spectra asked for on the block's own ear.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "FrequencyRaising"
    when: windowShown

    property string sessionId: ""
    property int made: 0
    readonly property string typeId: "hearing.frequencyraising"

    Component {
        id: faceComponent

        FrequencyRaisingFace {
            width: AppMetrics.tileOuterWidth - 2
            height: AppMetrics.tileOuterHeight - AppMetrics.tileBandHeight - 2
        }
    }

    // The panel in a window, inside a Flickable taller than the window, as
    // the settings card holds it.
    Component {
        id: shownPanel

        Window {
            property alias panel: inner

            width: 460
            height: 480
            visible: true

            Flickable {
                anchors.fill: parent
                contentWidth: width
                contentHeight: inner.implicitHeight + 200

                FrequencyRaisingPanel {
                    id: inner

                    width: 420
                }
            }
        }
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Frequency Raising");
        verify(testCase.sessionId !== "", "the mock opened a session");
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function cleanup() {
        Meters.watch("");
    }

    function newBlock(): string {
        testCase.made += 1;
        const id = "raise" + testCase.made;
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: id, type: testCase.typeId }, index: -1 }));
        tryVerify(() => SessionModel.blockType(id) === testCase.typeId, 1000, "the block was not added");
        return id;
    }

    function openPanel(blockId: string): var {
        Meters.watch(testCase.sessionId);
        const window = createTemporaryObject(shownPanel, null);
        window.panel.blockId = blockId;
        tryVerify(() => window.panel.plotArea.width > 0 && window.panel.selected >= 0, 2000,
                  "the panel was never laid out with a move picked");
        return window;
    }

    function test_itIsAHearingBlockWithAFaceAndAPanelOfItsOwn() {
        compare(BlockPresentation.category(testCase.typeId), "hearing");
        compare(BlockPresentation.name(testCase.typeId), "Frequency Raising");
        verify(BlockFaces.faceFor(testCase.typeId) === BlockFaces.frequencyRaising, "no face of its own");
        verify(BlockPanels.panelFor(testCase.typeId) === BlockPanels.frequencyRaising, "no panel of its own");
        compare(BlockCatalogue.describeParam(testCase.typeId, "ear").default, "left");
    }

    function test_theMovesAreTheBlocksAsTheCatalogueHasThem() {
        const id = testCase.newBlock();
        const window = testCase.openPanel(id);
        const moves = window.panel.moves;
        verify(moves.raises, "the helper does not know it goes up");
        compare(moves.used, [0]);
        compare(BlockCatalogue.describeParam(testCase.typeId, "move1Method").choices,
                ["compress", "transpose", "harmonics"]);
        compare(BlockCatalogue.describeParam(testCase.typeId, "move1Method").default, "transpose");
        compare(BlockCatalogue.describeParam(testCase.typeId, "move1Source").default, "keep");
        compare(BlockCatalogue.describeParam(testCase.typeId, "move1SourceLow").minimum, moves.lowestHz);
        compare(BlockCatalogue.describeParam(testCase.typeId, "move1SourceLow").maximum, moves.highestHz);
        verify(window.panel.picked.active, "the first move, going up, does not act");
    }

    function test_theMapIsTheEnginesArithmetic() {
        // The numbers dsp::carriedTo() gives, pinned in
        // tests/unit/dsp/test_frequencymove.cpp.
        const moves = createTemporaryQmlObject(
            "import io.github.reverblabr; FrequencyMoves { typeId: \"hearing.frequencyraising\" }", testCase);
        const copy = { "method": "transpose", "sourceLow": 250, "sourceHigh": 500, "destinationLow": 1000,
                       "destinationHigh": 2000 };
        fuzzyCompare(moves.carriedTo(copy, 300), 1200, 1e-6);
        fuzzyCompare(moves.carriedTo(copy, 500), 2000, 1e-6);
        compare(moves.carriedTo(copy, 200), 0);
        const harmonics = { "method": "harmonics", "sourceLow": 80, "sourceHigh": 160, "destinationLow": 150,
                            "destinationHigh": 450 };
        fuzzyCompare(moves.carriedTo(harmonics, 100), 200, 1e-6);
        fuzzyCompare(moves.carriedTo(harmonics, 150), 300, 1e-6);
        fuzzyCompare(moves.carriedTo(harmonics, 80), 160, 1e-6);
        compare(moves.carriedTo(harmonics, 200), 0);
        const narrow = { "method": "harmonics", "sourceLow": 80, "sourceHigh": 160, "destinationLow": 510,
                         "destinationHigh": 590 };
        compare(moves.carriedTo(narrow, 100), 0, "no harmonic of 100 Hz in 510-590 Hz");

        // A line a harmonic on the map: the 2nd to the 5th of 80-160 Hz
        // reach into 150-450 Hz, the 6th does not.
        const branches = moves.branches(harmonics);
        compare(branches.length, 4);
        fuzzyCompare(branches[0].from, 80, 1e-6);
        fuzzyCompare(branches[0].to, 160, 1e-6);
        fuzzyCompare(branches[0].toOut, 320, 1e-6);
        fuzzyCompare(branches[1].to, 150, 1e-6);
        fuzzyCompare(branches[1].toOut, 450, 1e-6);
        compare(moves.branches(copy).length, 1);
    }

    function test_theTileSaysTheFirstMoveAndHowItMovesIt() {
        const id = testCase.newBlock();
        const face = createTemporaryObject(faceComponent, testCase, { blockId: id });
        compare(face.summaryLabel.text, "250 → 1k");
        compare(face.caption.text, "copied up");

        SessionModel.setParam(id, "move1Method", "compress");
        tryCompare(face.caption, "text", "squeezed up");
        SessionModel.setParam(id, "move1Method", "harmonics");
        tryCompare(face.caption, "text", "harmonics added");

        // Set to go down, it is not a raising.
        SessionModel.setParam(id, "move1DestinationLow", 100);
        SessionModel.setParam(id, "move1DestinationHigh", 200);
        tryCompare(face.summaryLabel, "text", "No moves");
    }

    // The segments are worked by their signals, as tst_frequencylowering's
    // are: under the offscreen platform a synthesised click does not reach
    // them.
    function test_threeMethodsAndLowSoundsReachTheSession() {
        const id = testCase.newBlock();
        const window = testCase.openPanel(id);
        const panel = window.panel;
        compare(panel.methodChoices.count, 3);
        panel.methodChoices.itemAt(2).clicked();
        tryVerify(() => panel.picked.method === "harmonics", 1000, "the method did not change");
        compare(SessionModel.param(id, "move1Method"), "harmonics");
        panel.methodChoices.itemAt(0).clicked();
        tryVerify(() => SessionModel.param(id, "move1Method") === "compress", 1000);

        compare(panel.whenChoices.itemAt(1).text, "On low sounds");
        panel.whenChoices.itemAt(1).clicked();
        tryVerify(() => SessionModel.param(id, "move1When") === "needed", 1000);
    }

    function test_thePanelSaysTheDelayItsLowestMoveNeeds() {
        // FrequencyMoveBlock::preparedShape()'s rule: the long frame while a
        // move in use, on or off, starts below 250 Hz. At 48 kHz while the
        // session says no rate: 2048 samples, or 4096.
        const id = testCase.newBlock();
        const window = testCase.openPanel(id);
        const panel = window.panel;
        compare(panel.moves.latencySamples(48000), 2048);
        compare(panel.moves.latencySamples(44100), 2048);
        compare(panel.moves.latencySamples(96000), 4096);
        compare(panel.delayLabel.text, "43 ms");

        SessionModel.setParam(id, "move1SourceLow", 125);
        tryCompare(panel.delayLabel, "text", "85 ms");
        compare(panel.moves.latencySamples(48000), 4096);
        SessionModel.setParam(id, "move1On", false);
        compare(panel.delayLabel.text, "85 ms", "a move switched off still counts");
        SessionModel.setParam(id, "move1SourceLow", 250);
        tryCompare(panel.delayLabel, "text", "43 ms");

        // The third move starts at 125 Hz: taken into use, it counts.
        SessionModel.setParam(id, "move3Used", true);
        tryCompare(panel.delayLabel, "text", "85 ms");
    }

    function test_theEndsKeepARangeAndTheDestinationAboveItsSource() {
        const id = testCase.newBlock();
        const window = testCase.openPanel(id);
        const panel = window.panel;
        compare(panel.limited(0, "DestinationLow", 100), 500, "below its source");
        compare(panel.limited(0, "SourceHigh", 3000), 1000, "above its destination");
        compare(panel.limited(0, "SourceLow", 20), 60, "below the block's range");
        compare(panel.limited(0, "DestinationHigh", 30000), 16000, "above the block's range");
        compare(panel.limited(0, "DestinationHigh", 1001), Math.round(1000 * Math.pow(2, 1 / 6)),
                "narrower than a sixth");
    }

    function test_theMapDrawsALineAHarmonic() {
        const id = testCase.newBlock();
        SessionModel.setParam(id, "move1Method", "harmonics");
        SessionModel.setParam(id, "move1SourceLow", 80);
        SessionModel.setParam(id, "move1SourceHigh", 160);
        SessionModel.setParam(id, "move1DestinationLow", 160);
        SessionModel.setParam(id, "move1DestinationHigh", 480);
        const window = testCase.openPanel(id);
        const panel = window.panel;
        panel.mapButton.clicked();
        tryVerify(() => panel.mapShape.visible, 1000);
        // The 2nd to the 5th of 80-160 Hz reach into 160-480 Hz; the 6th
        // only from 80 Hz to 80 Hz, which is no line.
        tryCompare(panel.mapShape.lines, "length", 4);
        fuzzyCompare(panel.mapShape.lines[0].from.y, panel.yOf(160), 1e-6);
        fuzzyCompare(panel.mapShape.lines[0].to.y, panel.yOf(320), 1e-6);
    }

    function test_thePanelAsksForTheSpectraOfItsOwnEar() {
        const id = testCase.newBlock();
        SessionModel.setParam(id, "ear", "right");
        const window = testCase.openPanel(id);
        tryVerify(() => TestEngine.analysed(testCase.sessionId) === id, 1000, "the panel did not ask");
        compare(TestEngine.analysedChannel(testCase.sessionId), 1, "not the right ear's");
        window.destroy();
        tryVerify(() => TestEngine.analysed(testCase.sessionId) === "", 1000, "a closed panel kept the copy going");
    }
}
