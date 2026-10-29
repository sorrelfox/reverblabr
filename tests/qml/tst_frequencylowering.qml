// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Frequency Lowering's face and panel: a Hearing block with a face
// and a panel of its own; the tile's summary; the moves as chips, added and
// taken away; the picked move's settings reaching the session; a
// destination never dragged or slid above its source; the map's arithmetic
// the engine's; the spectra asked for on the block's own ear, drawn while
// the session runs, and let go when the panel closes.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "FrequencyLowering"
    when: windowShown

    property string sessionId: ""
    property int made: 0
    readonly property string typeId: "hearing.frequencylowering"

    Component {
        id: faceComponent

        FrequencyLoweringFace {
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

                FrequencyLoweringPanel {
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
        TestEngine.createSession("Frequency Lowering");
        verify(testCase.sessionId !== "", "the mock opened a session");
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function cleanup() {
        Meters.watch("");
    }

    function newBlock(): string {
        testCase.made += 1;
        const id = "lower" + testCase.made;
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
        compare(BlockPresentation.name(testCase.typeId), "Frequency Lowering");
        verify(BlockFaces.faceFor(testCase.typeId) === BlockFaces.frequencyLowering, "no face of its own");
        verify(BlockPanels.panelFor(testCase.typeId) === BlockPanels.frequencyLowering, "no panel of its own");
        compare(BlockCatalogue.describeParam(testCase.typeId, "ear").default, "left");
    }

    function test_theMovesAreTheBlocksAsTheCatalogueHasThem() {
        // The helper's ids and choices are the engine's, so nothing the
        // panel sends is refused.
        const id = testCase.newBlock();
        const window = testCase.openPanel(id);
        const moves = window.panel.moves;
        compare(moves.count, 4);
        compare(moves.used, [0]);
        for (const which of ["Used", "On", "Method", "SourceLow", "SourceHigh", "DestinationLow", "DestinationHigh",
                             "Level", "Source", "When"]) {
            const described = BlockCatalogue.describeParam(testCase.typeId, moves.paramId(3, which));
            verify(described.id !== undefined && described.id !== "", "no such parameter: " + moves.paramId(3, which));
        }
        compare(BlockCatalogue.describeParam(testCase.typeId, "move1Method").choices, ["compress", "transpose"]);
        compare(BlockCatalogue.describeParam(testCase.typeId, "move1Source").choices, ["keep", "lower", "remove"]);
        compare(BlockCatalogue.describeParam(testCase.typeId, "move1When").choices, ["always", "needed"]);
        compare(BlockCatalogue.describeParam(testCase.typeId, "move1SourceLow").minimum, moves.lowestHz);
        compare(BlockCatalogue.describeParam(testCase.typeId, "move1SourceLow").maximum, moves.highestHz);
    }

    function test_theMapIsTheEnginesArithmetic() {
        // The numbers dsp::carriedTo() gives, pinned in
        // tests/unit/dsp/test_frequencymove.cpp.
        const moves = createTemporaryQmlObject("import io.github.reverblabr; FrequencyMoves {}", testCase);
        const squeeze = { "method": "compress", "sourceLow": 4000, "sourceHigh": 8000, "destinationLow": 2500,
                          "destinationHigh": 4000 };
        fuzzyCompare(moves.carriedTo(squeeze, 4000), 2500, 1e-6);
        fuzzyCompare(moves.carriedTo(squeeze, 8000), 4000, 1e-6);
        fuzzyCompare(moves.carriedTo(squeeze, 4000 * Math.SQRT2), Math.sqrt(2500 * 4000), 1e-6);
        compare(moves.carriedTo(squeeze, 3999), 0);
        const copy = { "method": "transpose", "sourceLow": 4000, "sourceHigh": 8000, "destinationLow": 2000,
                       "destinationHigh": 3500 };
        fuzzyCompare(moves.carriedTo(copy, 6000), 3000, 1e-6);
        compare(moves.carriedTo(copy, 7500), 0, "3750 Hz is above the destination");
        compare(moves.carriedUpTo(copy), 7000);
    }

    function test_theTileSaysTheFirstMoveAndHowManyMore() {
        const id = testCase.newBlock();
        const face = createTemporaryObject(faceComponent, testCase, { blockId: id });
        compare(face.summaryLabel.text, "4k → 2.5k");
        compare(face.caption.text, "squeezed down");

        SessionModel.setParam(id, "move2Used", true);
        tryCompare(face.summaryLabel, "text", "4k → 2.5k (+1)");
        SessionModel.setParam(id, "move1On", false);
        tryCompare(face.summaryLabel, "text", "2k → 1.3k");
        SessionModel.setParam(id, "move2Used", false);
        tryCompare(face.summaryLabel, "text", "No moves");
    }

    function test_theChipsAreTheMovesAndAMoveIsAddedAndTakenAway() {
        const id = testCase.newBlock();
        const window = testCase.openPanel(id);
        const panel = window.panel;
        compare(panel.chips.count, 1);
        compare(panel.selected, 0);

        mouseClick(panel.addButton);
        tryCompare(panel.chips, "count", 2);
        compare(SessionModel.param(id, "move2Used"), true);
        compare(panel.selected, 1, "the added move is the one picked");

        mouseClick(panel.addButton);
        mouseClick(panel.addButton);
        tryCompare(panel.chips, "count", 4);
        verify(!panel.addButton.enabled, "four moves are all there is room for");

        mouseClick(panel.deleteButton);
        tryCompare(panel.chips, "count", 3);
        compare(SessionModel.param(id, "move4Used"), false);
        verify(panel.moves.used.indexOf(panel.selected) >= 0, "the pick went to a move not in use");
    }

    // The segments and switches are worked as tst_dehum and
    // tst_parametriceq work their own, by their signals: under the
    // offscreen platform a synthesised click does not reach them.
    function test_thePickedMovesSettingsReachTheSession() {
        const id = testCase.newBlock();
        const window = testCase.openPanel(id);
        const panel = window.panel;

        panel.methodChoices.itemAt(1).clicked();
        tryVerify(() => panel.picked.method === "transpose", 1000, "the method did not change");
        compare(SessionModel.param(id, "move1Method"), "transpose");

        panel.sourceChoices.itemAt(0).clicked();
        tryVerify(() => SessionModel.param(id, "move1Source") === "keep", 1000);
        panel.whenChoices.itemAt(1).clicked();
        tryVerify(() => SessionModel.param(id, "move1When") === "needed", 1000);

        panel.onSwitch.toggle();
        panel.onSwitch.toggled();
        tryVerify(() => SessionModel.param(id, "move1On") === false, 1000);

        panel.movedOnlySwitch.toggle();
        panel.movedOnlySwitch.toggled();
        tryVerify(() => SessionModel.param(id, "movedOnly") === true, 1000);
    }

    function test_aDestinationDraggedUpStopsAtItsSource() {
        const id = testCase.newBlock();
        const window = testCase.openPanel(id);
        const panel = window.panel;
        const area = panel.plotArea;
        // The destination's top, 4 kHz, dragged to 10 kHz: it stops at the
        // source's bottom, 4 kHz, which it already touches.
        // 4 kHz is also the source's bottom; the source's end is the nearer
        // one on the same spot, so first move the source up out of the way.
        SessionModel.setParam(id, "move1SourceLow", 5000);
        tryVerify(() => panel.picked.sourceLow === 5000, 1000);
        const start = area.mapToItem(window.contentItem, panel.xOf(4000), area.height * 0.8);
        const to = area.mapToItem(window.contentItem, panel.xOf(10000), area.height * 0.8);
        mousePress(window.contentItem, start.x, start.y);
        compare(panel.dragging, "DestinationHigh");
        mouseMove(window.contentItem, (start.x + to.x) / 2, to.y);
        mouseMove(window.contentItem, to.x, to.y);
        mouseRelease(window.contentItem, to.x, to.y);
        tryVerify(() => SessionModel.param(id, "move1DestinationHigh") === 5000, 1000,
                  "the destination went above its source: " + SessionModel.param(id, "move1DestinationHigh"));
        compare(panel.dragging, "");
    }

    function test_theEndsKeepARangeAndTheDestinationBelowItsSource() {
        const id = testCase.newBlock();
        const window = testCase.openPanel(id);
        const panel = window.panel;
        compare(panel.limited(0, "DestinationHigh", 9000), 4000, "above its source");
        compare(panel.limited(0, "SourceLow", 1000), 4000, "below its destination");
        compare(panel.limited(0, "SourceHigh", 4001), Math.round(4000 * Math.pow(2, 1 / 6)), "narrower than a sixth");
        compare(panel.limited(0, "DestinationLow", 50), 125, "below the block's range");
        compare(panel.limited(0, "SourceHigh", 30000), 16000, "above the block's range");
    }

    function test_theMapShowsAMoveAsALineFromWhereItsSourceStarts() {
        const id = testCase.newBlock();
        const window = testCase.openPanel(id);
        const panel = window.panel;
        panel.mapButton.clicked();
        tryVerify(() => panel.mapShape.visible, 1000);
        const lines = panel.mapShape.lines;
        compare(lines.length, 1);
        fuzzyCompare(lines[0].from.x, panel.xOf(4000), 1e-6);
        fuzzyCompare(lines[0].from.y, panel.yOf(2500), 1e-6);
        fuzzyCompare(lines[0].to.x, panel.xOf(8000), 1e-6);
        fuzzyCompare(lines[0].to.y, panel.yOf(4000), 1e-6);

        SessionModel.setParam(id, "move1On", false);
        tryVerify(() => panel.mapShape.lines.length === 0, 1000, "a move switched off is still drawn");
    }

    function test_thePanelAsksForTheSpectraOfItsOwnEarWhileItIsShown() {
        const id = testCase.newBlock();
        compare(TestEngine.analysed(testCase.sessionId), "");
        const window = testCase.openPanel(id);
        tryVerify(() => TestEngine.analysed(testCase.sessionId) === id, 1000, "the panel did not ask");
        compare(TestEngine.analysedChannel(testCase.sessionId), 0, "not the left ear's");

        SessionModel.setParam(id, "ear", "right");
        tryVerify(() => TestEngine.analysedChannel(testCase.sessionId) === 1, 1000, "the right ear was not asked for");

        window.panel.visible = false;
        tryVerify(() => TestEngine.analysed(testCase.sessionId) === "", 1000, "a hidden panel still asked");
        window.panel.visible = true;
        tryVerify(() => TestEngine.analysed(testCase.sessionId) === id, 1000);
        window.destroy();
        tryVerify(() => TestEngine.analysed(testCase.sessionId) === "", 1000, "a closed panel kept the copy going");
    }

    function test_theSoundBeforeAndAfterIsDrawnWhileTheSessionRuns() {
        const id = testCase.newBlock();
        const window = testCase.openPanel(id);
        const panel = window.panel;
        const bands = [];
        for (let band = 0; band < 96; ++band) {
            bands.push(-60);
        }
        TestEngine.setFakeSpectrum(testCase.sessionId, id, bands);
        TestEngine.setFakeInputSpectrum(testCase.sessionId, id, bands);
        TestEngine.setFakeBandValues(testCase.sessionId, id, [1, 0, 0, 0]);

        wait(50);
        verify(!panel.outputSpectrumShape.visible, "drawn for a session that is not running");
        verify(!panel.inputSpectrumShape.visible, "drawn for a session that is not running");

        TestEngine.start(testCase.sessionId);
        tryVerify(() => panel.outputSpectrumShape.visible && panel.inputSpectrumShape.visible, 1000,
                  "the spectra were never drawn");
        tryVerify(() => panel.movingNow.length === 4 && panel.movingNow[0] === 1, 1000,
                  "which moves are moving never arrived");

        panel.mapButton.clicked();
        tryVerify(() => !panel.outputSpectrumShape.visible, 1000, "the map drew the spectrum");
        TestEngine.stop(testCase.sessionId);
    }
}
