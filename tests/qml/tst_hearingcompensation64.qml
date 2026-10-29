// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Hearing Compensation 64's face and panel: the curves drawn through
// the control points exactly as the block draws them, the "now" line with
// 64 values, a point dragged across and up, a point added where the curves
// already are and taken out again, Fit from audiogram placing points and
// its undo, and Copy from the Other Ear taking the points over.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "HearingCompensation64"
    when: windowShown

    property string sessionId: ""
    property int made: 0

    readonly property string typeId: "hearing.compensation64"

    Component {
        id: faceComponent

        HearingCompensationFace {
            typeId: "hearing.compensation64"
            width: AppMetrics.tileOuterWidth - 2
            height: AppMetrics.tileOuterHeight - AppMetrics.tileBandHeight - 2
        }
    }

    Component {
        id: shownPanel

        Window {
            property alias panel: inner
            property alias flickable: scroller

            width: 500
            height: 420
            visible: true

            Flickable {
                id: scroller

                anchors.fill: parent
                contentWidth: width
                contentHeight: inner.implicitHeight + 200

                HearingCompensationPanel {
                    id: inner

                    typeId: "hearing.compensation64"
                    width: 460
                }
            }
        }
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Hearing Compensation 64");
        verify(testCase.sessionId !== "", "the mock opened a session");
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function cleanup() {
        Meters.watch("");
    }

    function newBlock(ear: string): string {
        testCase.made += 1;
        const id = "hc64_" + testCase.made;
        const block = { id: id, type: testCase.typeId };
        if (ear !== "") {
            block.params = { ear: ear };
        }
        TestEngine.applyEdit(testCase.sessionId, JSON.stringify({ op: "addBlock", block: block, index: -1 }));
        tryVerify(() => SessionModel.blockType(id) === testCase.typeId, 1000, "the block was not added");
        return id;
    }

    function openPanel(blockId: string): var {
        Meters.watch(testCase.sessionId);
        const window = createTemporaryObject(shownPanel, null);
        window.panel.blockId = blockId;
        tryVerify(() => window.panel.plotArea.width > 0 && window.panel.averageLine.path.length === 64, 2000,
                  "the plot was never laid out");
        return window;
    }

    function clearSession() {
        const ids = SessionModel.blockIds.slice();
        if (ids.length > 0) {
            SessionModel.removeBlocks(ids);
            tryVerify(() => SessionModel.blockIds.length === 0, 1000, "the session was not cleared");
        }
    }

    function test_itIsAHearingBlockWithTheSameFaceAndPanelSetThroughPoints() {
        verify(BlockCatalogue.types.indexOf(testCase.typeId) >= 0, "the engine does not offer it");
        compare(BlockPresentation.category(testCase.typeId), "hearing");
        compare(BlockPresentation.name(testCase.typeId), "Hearing Compensation 64");
        verify(BlockFaces.faceFor(testCase.typeId) !== null, "it has no face");
        verify(BlockPanels.panelFor(testCase.typeId) !== null, "it has no panel");
        const hz = BlockCatalogue.describeParam(testCase.typeId, "point0Hz");
        compare(hz.minimum, 100);
        compare(hz.maximum, 16000);
        compare(hz.default, 250);
        compare(BlockCatalogue.describeParam(testCase.typeId, "point5On").default, true);
        compare(BlockCatalogue.describeParam(testCase.typeId, "point6On").default, false);
        compare(BlockCatalogue.describeParam(testCase.typeId, "point23CeilingDb").default, 100);
    }

    function test_thePanelsCurveIsTheBlocksCurve() {
        // The same points and readings dsp.HearingPointCurve.ThePanelsCurveIs
        // TheBlocksToTheLastDecimal checks the block's curve at.
        const id = testCase.newBlock("");
        const window = testCase.openPanel(id);
        const bands = window.panel.bands;
        const curve = bands.curveThrough([Math.log(250), Math.log(1000), Math.log(4000)], [0, 20, 5]);
        verify(Math.abs(curve(Math.log(500)) - 12.5) < 1e-4, "at 500 Hz: " + curve(Math.log(500)));
        verify(Math.abs(curve(Math.log(2000)) - 14.375) < 1e-4, "at 2 kHz: " + curve(Math.log(2000)));
        verify(Math.abs(curve(Math.log(3000)) - 8.6247) < 1e-4, "at 3 kHz: " + curve(Math.log(3000)));
        // Level beyond the ends.
        compare(curve(Math.log(100)), 0);
        compare(curve(Math.log(16000)), 5);
    }

    function test_theFaceDrawsTheCurvesThroughThePoints() {
        const id = testCase.newBlock("");
        const face = createTemporaryObject(faceComponent, testCase, { blockId: id });
        tryVerify(() => face.averageLine.path.length === 64, 1000, "the face does not draw 64 bands");
        const flat = face.averageLine.path.map(point => point.y);
        // The 1 kHz point up 20 dB: the bands around it rise, the ends stay.
        SessionModel.setParams(id, [["point2AverageGainDb", 20], ["point2SoftGainDb", 30]]);
        tryVerify(() => {
            const bands = face.bands;
            const near = bands.centresHz.findIndex(hz => hz >= 1000);
            return face.averageLine.path[near].y < flat[near] - 5;
        }, 1000, "the lift at 1 kHz is not drawn");
        compare(face.averageLine.path[0].y, flat[0], "the lowest band moved");
        compare(face.averageLine.path[63].y, flat[63], "the highest band moved");
    }

    function test_theNowLineHasSixtyFourValues() {
        const id = testCase.newBlock("");
        const window = testCase.openPanel(id);
        const panel = window.panel;
        TestEngine.start(testCase.sessionId);
        for (const band of [0, 20, 41, 63]) {
            const values = [];
            for (let index = 0; index < 64; ++index) {
                values.push(index === band ? 20 : 0);
            }
            for (let index = 0; index < 64; ++index) {
                values.push(-40);
            }
            TestEngine.setFakeBandValues(testCase.sessionId, id, values);
            tryVerify(() => panel.nowLine.path.length === 64, 1000, "no 64-value now line while the session runs");
            tryVerify(() => Math.abs(panel.nowLine.path[band].y - panel.yOfGain(20)) < 1e-6, 1000,
                      "the now line does not peak at band " + band);
            verify(Math.abs(panel.nowLine.path[band].x - panel.xOfHz(panel.bands.centresHz[band])) < 1e-6);
        }
        TestEngine.stop(testCase.sessionId);
        tryVerify(() => panel.nowLine.path.length === 0, 1000, "the now line stayed after the session stopped");
    }

    function test_draggingAPointMovesItAcrossAndUpTheCurveBeingEdited() {
        const id = testCase.newBlock("");
        const window = testCase.openPanel(id);
        const panel = window.panel;
        panel.editing = "Average";
        tryVerify(() => panel.handles.count === 6, 1000, "a handle for each of the six points");
        const point = panel.handlePoint(2);
        const from = panel.plotArea.mapToItem(window.contentItem, point.x, point.y);
        mousePress(window.contentItem, from.x, from.y);
        for (let step = 1; step <= 5; ++step) {
            mouseMove(window.contentItem, from.x + step * 6, from.y - step * 8, 10, Qt.LeftButton);
        }
        mouseRelease(window.contentItem, from.x + 30, from.y - 40);
        compare(window.flickable.contentY, 0, "the scroll view took the drag");
        const gain = Math.round(panel.gainAtY(panel.yOfGain(0) - 40) * 10) / 10;
        const hz = panel.pointHzAtX(point.x + 30);
        tryVerify(() => Math.abs((SessionModel.param(id, "point2AverageGainDb") ?? 0) - gain) < 0.11, 1000,
                  "point 2's average gain is " + SessionModel.param(id, "point2AverageGainDb") + ", not " + gain);
        tryVerify(() => Math.abs((SessionModel.param(id, "point2Hz") ?? 1000) - hz) <= 1, 1000,
                  "point 2 is at " + SessionModel.param(id, "point2Hz") + " Hz, not " + hz);
        verify(hz > 1000, "a drag to the right moved the point down in frequency");
        compare(SessionModel.param(id, "point2SoftGainDb"), undefined, "the drag moved another curve");
        compare(panel.selected, 2, "the point dragged is the one inspected");
    }

    function test_aPointIsAddedWhereTheCurvesAreAndCanBeTakenOut() {
        const id = testCase.newBlock("");
        SessionModel.setParams(id, [["point2AverageGainDb", 20], ["point3AverageGainDb", 10]]);
        tryVerify(() => SessionModel.param(id, "point3AverageGainDb") === 10, 1000);
        const window = testCase.openPanel(id);
        const panel = window.panel;
        tryVerify(() => panel.handles.count === 6, 1000);
        const before = panel.averageLine.path.map(point => point.y);

        // A double click in the open at 1.5 kHz, between the 1 and 2 kHz points.
        const x = panel.xOfHz(1500);
        const at = panel.plotArea.mapToItem(window.contentItem, x, panel.yOfGain(-15));
        mouseDoubleClickSequence(window.contentItem, at.x, at.y);
        tryVerify(() => SessionModel.param(id, "point6On") === true, 1000, "no point was added");
        const hz = SessionModel.param(id, "point6Hz");
        verify(Math.abs(hz - 1500) / 1500 < 0.02, "the point is at " + hz + " Hz");
        // On the curve through the six points: nothing changes until it is moved.
        const hzs = [250, 500, 1000, 2000, 4000, 8000];
        const onCurve = panel.bands.curveThrough(hzs.map(f => Math.log(f)), [0, 0, 20, 10, 0, 0])(Math.log(hz));
        verify(Math.abs(SessionModel.param(id, "point6AverageGainDb") - onCurve) <= 0.05,
               "the new point asks for " + SessionModel.param(id, "point6AverageGainDb") + ", the curve is at " + onCurve);
        compare(panel.handles.count, 7);
        compare(panel.selected, 6, "the new point is the one inspected");
        for (let band = 0; band < 64; ++band) {
            verify(Math.abs(panel.averageLine.path[band].y - before[band]) < 1.0, "band " + band + " moved");
        }
        // One edit.
        SessionModel.undo();
        tryVerify(() => SessionModel.param(id, "point6On") === undefined, 1000, "one undo did not take it all back");
        SessionModel.redo();
        tryVerify(() => SessionModel.param(id, "point6On") === true, 1000);

        panel.selected = 6;
        tryVerify(() => panel.removeButton.visible, 1000, "no Remove Point for the chosen point");
        mouseClick(panel.removeButton);
        tryVerify(() => SessionModel.param(id, "point6On") === false, 1000, "the point was not taken out");
        tryVerify(() => panel.handles.count === 6, 1000);
    }

    function test_theInspectorShowsTheChosenPointAndItsCurve() {
        const id = testCase.newBlock("");
        SessionModel.setParams(id, [["point3SoftGainDb", 30], ["point3AverageGainDb", 20], ["point3LoudGainDb", 10]]);
        tryVerify(() => SessionModel.param(id, "point3LoudGainDb") === 10, 1000);
        const window = testCase.openPanel(id);
        const panel = window.panel;
        panel.selected = 3;
        tryVerify(() => panel.bandCurve.path.length > 80, 1000, "the point's curve was not drawn");
        const area = panel.bandCurveArea;
        const at = panel.bandCurve.path.find(point => Math.abs(point.x - area.xOf(-40)) < 1e-6);
        verify(at !== undefined, "no point at -40 dBFS");
        verify(Math.abs(at.y - area.yOf(-20)) < 1e-6, "the curve at -40 dBFS is not 20 dB up");
    }

    function test_fitFromAudiogramPlacesPointsAndOneUndoTakesItBack() {
        const id = testCase.newBlock("");
        const window = testCase.openPanel(id);
        const panel = window.panel;
        SessionModel.setParams(id, [
            ["audiogram500", "heard"], ["audiogram500Db", 30],
            ["audiogram2000", "heard"], ["audiogram2000Db", 50],
            ["audiogram8000", "noResponse"], ["audiogram8000Db", 90]
        ]);
        tryVerify(() => SessionModel.param(id, "audiogram8000") === "noResponse", 1000);
        const before = SessionModel.params(id);

        panel.audiogramSection.expanded = true;
        tryVerify(() => panel.fitButton.visible && panel.fitButton.width > 0, 1000, "the audiogram did not open");
        mouseClick(panel.fitButton);
        tryVerify(() => SessionModel.param(id, "point2Hz") === 8000, 1000, "the fit placed no point at 8 kHz");
        compare(SessionModel.param(id, "point0Hz"), 500);
        compare(SessionModel.param(id, "point1Hz"), 2000);
        for (let point = 0; point < 24; ++point) {
            compare(SessionModel.param(id, "point" + point + "On"), point < 3, "point " + point);
        }
        tryVerify(() => panel.handles.count === 3, 1000, "a handle for each measured frequency");
        compare(panel.unhelped.length, 1);
        compare(panel.unhelped[0], 8000, "the mark is not where the fit could not help");
        verify(panel.fitMessage.visible, "nothing points to Frequency Lowering");

        SessionModel.undo();
        tryVerify(() => SessionModel.param(id, "point2Hz") === undefined, 1000, "one undo left the fit");
        compare(JSON.stringify(SessionModel.params(id)), JSON.stringify(before));
    }

    function test_theOtherEarsPointsAreDrawnFaintlyAndCanBeCopied() {
        testCase.clearSession();
        const left = testCase.newBlock("left");
        const right = testCase.newBlock("right");
        SessionModel.setParams(left, [["point1AverageGainDb", 18], ["point8On", true], ["point8Hz", 3000],
                                      ["point8LoudGainDb", -6], ["point0On", false]]);
        tryVerify(() => SessionModel.param(left, "point8Hz") === 3000, 1000);
        const window = testCase.openPanel(right);
        const panel = window.panel;
        compare(panel.otherBlock, left, "the left ear's block was not found");
        tryVerify(() => panel.otherLine.path.length === 64, 1000, "the other ear's curve is not drawn");
        verify(panel.copyButton.enabled);

        mouseClick(panel.copyButton);
        tryVerify(() => SessionModel.param(right, "point8On") === true, 1000, "nothing was copied");
        compare(SessionModel.param(right, "point8Hz"), 3000);
        compare(SessionModel.param(right, "point8LoudGainDb"), -6);
        compare(SessionModel.param(right, "point1AverageGainDb"), 18);
        compare(SessionModel.param(right, "point0On"), false);
        compare(SessionModel.param(right, "ear"), "right", "copying moved the block to the other ear");
        tryVerify(() => panel.handles.count === 6, 1000, "the copied points are not the handles");
        SessionModel.undo();
        tryVerify(() => SessionModel.param(right, "point8On") === undefined, 1000, "one undo left the copy");
    }
}
