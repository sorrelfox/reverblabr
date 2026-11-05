// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Dynamics Processor's panel and face offscreen, against the mock
// engine: that each of the four points moves along its one line and stops
// where it has to, by pointer and by keyboard, that nothing dragged puts the
// curve above the diagonal, that a drag says what it is setting and is one
// step to take back, and that Details opens on to every setting the block
// declares. What the curve itself draws is measured in app.DynamicsPanelTest,
// where there is a signal to read it against.

import QtQuick
import QtQuick.Controls
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "DynamicsPanel"
    when: windowShown

    property string sessionId: ""

    // **No parent**, except where a test needs key events: an item's
    // `visible` is its own value and its parent's together, and a test case
    // offscreen is not shown.
    Component {
        id: panelComponent

        DynamicsPanel {
            width: 340
        }
    }

    // **A panel in a window, and inside the scroll view the settings panel
    // really puts it in.** A drag is a gesture the window system delivers,
    // and what else is between the handle and the window decides whether it
    // arrives — which is the whole of the bug where the curve's handles did
    // not follow the pointer.
    Component {
        id: panelWindow

        Window {
            property alias panel: inner

            width: 360
            height: 700
            visible: true

            ScrollView {
                anchors.fill: parent
                contentWidth: availableWidth

                DynamicsPanel {
                    id: inner

                    width: 340
                }
            }
        }
    }

    Component {
        id: faceComponent

        DynamicsFace {
            width: 180
            height: 96
        }
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Dynamics");
        verify(testCase.sessionId !== "", "the mock opened a session");
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock",
                                              block: { id: "dyn", type: "effect.dynamics" },
                                              index: -1 }));
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        // Every test starts from the block's own defaults rather than from
        // what the one before it left.
        const params = BlockCatalogue.params("effect.dynamics");
        for (const descriptor of params) {
            SessionModel.setParam("dyn", descriptor.id, descriptor.default);
        }
    }

    function test_theBlockHasAPanelAndAFaceOfItsOwn() {
        verify(BlockRegister.panelFor("effect.dynamics") !== null, "the Dynamics block is in the panel register");
        verify(BlockRegister.faceFor("effect.dynamics") !== null, "the Dynamics block has a face of its own");
    }

    function test_theDetailsAreClosedWhenThePanelOpens() {
        // **Nine numbers are what somebody comes back for**, not what they
        // open the block to see.
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "dyn";
        wait(50);

        verify(!panel.detailsButton.checked, "Details was open when the panel opened");
        compare(panel.detailRows.count, BlockCatalogue.params("effect.dynamics").length);

        panel.detailsButton.toggle();
        wait(50);
        verify(panel.detailsButton.checked, "Details did not open");
    }

    function test_everyParameterUnderDetailsIsNamedInWords() {
        // A row headed `expanderHold` is a row written for whoever wrote the
        // block. Every parameter the block declares has a word here.
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "dyn";
        panel.detailsButton.toggle();
        wait(50);

        for (let index = 0; index < panel.detailRows.count; ++index) {
            const row = panel.detailRows.itemAt(index);
            verify(row.label !== row.paramId, "the row for " + row.paramId + " is headed with its id");
            verify(row.label.length > 0, "the row for " + row.paramId + " has no name");
        }
    }

    // --- what the tests below do with a point ---

    /// The panel in its scroll view, with these settings in the session.
    function windowWith(settings) {
        const window = createTemporaryObject(panelWindow, null);
        const panel = window.panel;
        panel.blockId = "dyn";
        for (const id in settings) {
            SessionModel.setParam("dyn", id, settings[id]);
        }
        waitForRendering(panel);
        return window;
    }

    function pointOf(panel, id) {
        const order = ["cutoff", "expanderThreshold", "threshold", "height"];
        const point = panel.handles.itemAt(order.indexOf(id));
        compare(point.modelData, id);
        return point;
    }

    function centreOf(window, point) {
        return point.mapToItem(window.contentItem, point.width / 2, point.height / 2);
    }

    /// Takes hold of a point and goes \a x, \a y away, and leaves the button
    /// down.
    function hold(window, id, x, y) {
        const from = testCase.centreOf(window, testCase.pointOf(window.panel, id));
        mousePress(window.contentItem, from.x, from.y);
        for (let step = 1; step <= 6; ++step) {
            mouseMove(window.contentItem, from.x + x * step / 6, from.y + y * step / 6);
        }
        return Qt.point(from.x + x, from.y + y);
    }

    function drag(window, id, x, y) {
        const to = testCase.hold(window, id, x, y);
        mouseRelease(window.contentItem, to.x, to.y);
    }

    /// The owner's rule, read off what is on the plot right now.
    function verifyUnderTheDiagonal(panel, when) {
        for (let inputDb = -120; inputDb <= 40; inputDb += 2) {
            verify(panel.curve.outputDb(inputDb) <= inputDb + 0.001,
                   "the curve went above the diagonal at " + inputDb + " dB " + when);
        }
    }

    function test_everyPointIsAControlThatSaysWhatItIsSetTo() {
        // The criterion: nothing is reachable by pointer alone, and a screen
        // reader is told which point it is on and where it stands.
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "dyn";
        wait(50);

        compare(panel.handles.count, 4);
        for (let index = 0; index < panel.handles.count; ++index) {
            const point = panel.handles.itemAt(index);
            verify(point.activeFocusOnTab, "a point that the tab order skips");
            verify(point.Accessible.name.length > 0, "a point with no name");
            verify(point.Accessible.name.indexOf(panel.handleValue(point.modelData)) >= 0,
                   "the name of " + point.modelData + " does not say what it is set to");
        }
    }

    function test_theFirstPointMovesAlongTheFloorAndSetsTheRatio() {
        const window = testCase.windowWith({ expanderThreshold: -60 });
        const panel = window.panel;
        const point = testCase.pointOf(panel, "cutoff");
        const floorY = panel.curve.yOfDb(-120);
        fuzzyCompare(panel.curve.cutoffDb(), -120, 0.01);

        // Up and to the right: only the right counts.
        testCase.drag(window, "cutoff", 36, -80);
        const moved = panel.curve.cutoffDb();
        verify(moved > -120, "the point did not follow the pointer along the floor");
        fuzzyCompare(moved, -120 + 36 * 160 / panel.curve.plotWidth, 1.5);
        fuzzyCompare(point.at.y, floorY, 0.01);
        // The ratio is the slope of the line from the floor to the second
        // point: R = (E + 120) / (E - b).
        fuzzyCompare(SessionModel.param("dyn", "expanderRatio"), (-60 + 120) / (-60 - moved), 0.02);
        fuzzyCompare(panel.curve.outputDb(moved), -120, 0.05);
        testCase.verifyUnderTheDiagonal(panel, "after the first point moved");

        // Right past the second point it stops at the steepest there is,
        // short of it: it can never reach it or pass it.
        testCase.drag(window, "cutoff", 400, 0);
        compare(SessionModel.param("dyn", "expanderRatio"), 50);
        verify(panel.curve.cutoffDb() < -60, "the first point reached the second");
        fuzzyCompare(point.at.y, floorY, 0.01);

        // And left it stops at the corner, where the line is the diagonal.
        testCase.drag(window, "cutoff", -400, 0);
        compare(SessionModel.param("dyn", "expanderRatio"), 1);
        fuzzyCompare(panel.curve.cutoffDb(), -120, 0.01);
        fuzzyCompare(point.at.y, floorY, 0.01);
    }

    function test_theSecondPointMovesAlongTheDiagonalAndCarriesTheFirst() {
        const window = testCase.windowWith({ expanderThreshold: -80, expanderRatio: 4 });
        const panel = window.panel;
        const point = testCase.pointOf(panel, "expanderThreshold");
        fuzzyCompare(panel.curve.cutoffDb(), -90, 0.01);

        // Straight sideways: it takes the nearest place on the diagonal.
        testCase.drag(window, "expanderThreshold", 40, 0);
        const moved = SessionModel.param("dyn", "expanderThreshold");
        verify(moved > -80, "the point did not follow the pointer up the line");
        fuzzyCompare(panel.curve.dbAtX(point.at.x), moved, 0.05);
        fuzzyCompare(panel.curve.dbAtY(point.at.y), moved, 0.05);
        // **The ratio stays, so the first point comes along.**
        compare(SessionModel.param("dyn", "expanderRatio"), 4);
        fuzzyCompare(panel.curve.cutoffDb(), moved - (moved + 120) / 4, 0.01);

        // The third point stops it.
        testCase.drag(window, "expanderThreshold", 400, -400);
        fuzzyCompare(SessionModel.param("dyn", "expanderThreshold"), panel.curve.thresholdDb(), 0.01);
        testCase.verifyUnderTheDiagonal(panel, "with the second point against the third");

        // And the corner of the graph does at the other end.
        testCase.drag(window, "expanderThreshold", -400, 400);
        compare(SessionModel.param("dyn", "expanderThreshold"), -120);
        testCase.verifyUnderTheDiagonal(panel, "with the second point in the corner");
    }

    function test_theThirdPointMovesAlongTheDiagonalAndCarriesTheFourth() {
        const window = testCase.windowWith({ expanderThreshold: -80, threshold: -30, height: 20 });
        const panel = window.panel;
        const point = testCase.pointOf(panel, "threshold");
        const endBefore = panel.curve.endDb();

        testCase.drag(window, "threshold", 30, 30);
        verify(SessionModel.param("dyn", "threshold") !== -30, "the point did not follow the pointer");
        const moved = SessionModel.param("dyn", "threshold");
        fuzzyCompare(panel.curve.dbAtX(point.at.x), moved, 0.05);
        fuzzyCompare(panel.curve.dbAtY(point.at.y), moved, 0.05);

        // The height is kept, and the fourth point has gone with it.
        testCase.drag(window, "threshold", 60, -60);
        compare(SessionModel.param("dyn", "height"), 20);
        verify(panel.curve.endDb() !== endBefore, "the fourth point stayed behind");

        // The top of its line is +20 dB.
        testCase.drag(window, "threshold", 400, -400);
        compare(SessionModel.param("dyn", "threshold"), 20);
        compare(SessionModel.param("dyn", "height"), 20);
        verify(panel.curve.endDb() < 40, "the fourth point reached the diagonal");
        testCase.verifyUnderTheDiagonal(panel, "with the third point at the top");

        // The second point is the bottom of it.
        testCase.drag(window, "threshold", -400, 400);
        compare(SessionModel.param("dyn", "threshold"), -80);
        testCase.verifyUnderTheDiagonal(panel, "with the third point on the second");
    }

    function test_theFourthPointMovesUpTheRightEdgeAndSetsTheHeight() {
        const window = testCase.windowWith({ threshold: -34, height: 10 });
        const panel = window.panel;
        const point = testCase.pointOf(panel, "height");
        const rightX = panel.curve.xOfDb(40);
        fuzzyCompare(point.at.x, rightX, 0.01);

        // Sideways does nothing but up moves it.
        const before = panel.curve.endDb();
        testCase.drag(window, "height", -60, -24);
        const moved = panel.curve.endDb();
        verify(moved > before, "the point did not follow the pointer up the edge");
        fuzzyCompare(moved, before + 24 * 160 / panel.curve.plotHeight, 1.0);
        fuzzyCompare(point.at.x, rightX, 0.01);
        fuzzyCompare(SessionModel.param("dyn", "threshold"), -34, 0.001);
        testCase.verifyUnderTheDiagonal(panel, "after the fourth point moved");

        // Down to the third and no lower: a height of nothing, a flat line.
        testCase.drag(window, "height", 0, 400);
        compare(SessionModel.param("dyn", "height"), 0);
        fuzzyCompare(panel.curve.endDb(), -34, 0.01);
        fuzzyCompare(panel.curve.outputDb(0), -34, 0.01);
        fuzzyCompare(point.at.x, rightX, 0.01);

        // Up to the most a height can be, and it stops under the diagonal.
        testCase.drag(window, "height", 0, -400);
        compare(SessionModel.param("dyn", "height"), 40);
        verify(panel.curve.endDb() < 40, "the fourth point reached the top of the graph");
        testCase.verifyUnderTheDiagonal(panel, "with the fourth point at the top");
    }

    function test_thePointOnTheEdgeCanBeTakenByItsHalfOutsideThePlot() {
        // The bottom of the first point and the right of the fourth are
        // outside the plot, and a press there is still a press on them.
        const window = testCase.windowWith({ threshold: -34, height: 10, expanderThreshold: -60, expanderRatio: 2 });
        const panel = window.panel;

        const first = testCase.pointOf(panel, "cutoff");
        const fromFirst = testCase.centreOf(window, first);
        mousePress(window.contentItem, fromFirst.x, fromFirst.y + first.height / 2 - 2);
        compare(panel.dragging, "cutoff");
        mouseRelease(window.contentItem, fromFirst.x, fromFirst.y + first.height / 2 - 2);

        const fourth = testCase.pointOf(panel, "height");
        const fromFourth = testCase.centreOf(window, fourth);
        mousePress(window.contentItem, fromFourth.x + fourth.width / 2 - 2, fromFourth.y);
        compare(panel.dragging, "height");
        mouseRelease(window.contentItem, fromFourth.x + fourth.width / 2 - 2, fromFourth.y);
    }

    function test_aPressTakesHoldAndMovesNothing() {
        const window = testCase.windowWith({ threshold: -34, height: 10 });
        const panel = window.panel;
        const point = testCase.pointOf(panel, "threshold");

        // Off to one side of the point, but within reach of it.
        const from = testCase.centreOf(window, point);
        mousePress(window.contentItem, from.x + 6, from.y - 3);
        compare(panel.dragging, "threshold");
        mouseRelease(window.contentItem, from.x + 6, from.y - 3);
        compare(SessionModel.param("dyn", "threshold"), -34);
    }

    function test_twoPointsInOnePlaceAreToldApartByWhichWayTheyGo() {
        // Each stops the other, so whichever a press picked could be the one
        // that cannot move. The drag says which is meant.
        const window = testCase.windowWith({ expanderThreshold: -40, threshold: -40, height: 10 });
        const panel = window.panel;

        const up = testCase.hold(window, "threshold", 24, -24);
        compare(panel.dragging, "threshold");
        mouseRelease(window.contentItem, up.x, up.y);
        verify(SessionModel.param("dyn", "threshold") > -40, "up did not move the third point");
        compare(SessionModel.param("dyn", "expanderThreshold"), -40);

        SessionModel.setParam("dyn", "threshold", -40);
        waitForRendering(panel);
        const down = testCase.hold(window, "threshold", -24, 24);
        compare(panel.dragging, "expanderThreshold");
        mouseRelease(window.contentItem, down.x, down.y);
        verify(SessionModel.param("dyn", "expanderThreshold") < -40, "down did not move the second point");
        compare(SessionModel.param("dyn", "threshold"), -40);
    }

    function test_whileAPointIsHeldABoxNamesWhatItSetsAndThePointStandsOut() {
        const window = testCase.windowWith({ expanderThreshold: -60, expanderRatio: 2 });
        const panel = window.panel;

        const expected = {
            "cutoff": ["expanderThreshold", "expanderRatio"],
            "expanderThreshold": ["expanderThreshold", "expanderRatio"],
            "threshold": ["threshold", "height"],
            "height": ["threshold", "height"]
        };
        for (const id in expected) {
            const point = testCase.pointOf(panel, id);
            verify(!point.held, id + " stood out before it was held");
            verify(!panel.dragReadout.visible, "the readout was up before " + id + " was held");

            const to = testCase.hold(window, id, 6, -6);
            verify(point.held, id + " was not highlighted while held");
            verify(panel.dragReadout.visible, "nothing said what " + id + " was setting");
            compare(panel.handleReadout(id), expected[id]);
            // **In words**: an id on the screen would be a word written for
            // whoever wrote the block.
            for (const paramId of expected[id]) {
                verify(panel.paramLabel(paramId) !== paramId, paramId + " has no name");
                verify(panel.paramValueText(paramId).length > 0, paramId + " has no value");
            }

            mouseRelease(window.contentItem, to.x, to.y);
            compare(panel.dragging, "", "the plot kept hold of " + id);
            verify(!point.held, id + " stayed highlighted");
            verify(!panel.dragReadout.visible, "the readout stayed up after the drag");
        }
    }

    function test_theBoxNeverCoversThePointOrLeavesThePlot() {
        // Sizes from the panel's narrowest to a wide one, and a box from the
        // size it has here to a long translation of it.
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "dyn";
        wait(50);

        for (const plotWidth of [160, 245, 400]) {
            const plotHeight = 250;
            for (const box of [Qt.size(150, 36), Qt.size(150, 56)]) {
                for (let along = 0; along <= 1; along += 0.05) {
                    const onDiagonal = Qt.point(along * plotWidth, plotHeight * (1 - along));
                    const places = [
                        ["cutoff", Qt.point(along * plotWidth, plotHeight)],
                        ["expanderThreshold", onDiagonal],
                        ["threshold", onDiagonal],
                        ["height", Qt.point(plotWidth, along * plotHeight)]
                    ];
                    for (const place of places) {
                        const at = panel.readoutPlace(place[0], place[1], box.width, box.height, plotWidth, plotHeight);
                        verify(at.x >= 0 && at.x + box.width <= Math.max(plotWidth, box.width) + 0.01,
                               place[0] + " put the box off the plot across");
                        verify(at.y >= 0 && at.y + box.height <= plotHeight + 0.01,
                               place[0] + " put the box off the plot up and down");
                        const insideX = place[1].x >= at.x - 2 && place[1].x <= at.x + box.width + 2;
                        const insideY = place[1].y >= at.y - 2 && place[1].y <= at.y + box.height + 2;
                        verify(!(insideX && insideY), "the box covers " + place[0] + " at " + along);
                    }
                }
            }
        }
    }

    function test_aDragOfAnyPointIsOneStepToTakeBack() {
        const window = testCase.windowWith({ threshold: -34, height: 10, expanderThreshold: -70, expanderRatio: 2 });
        const panel = window.panel;
        tryVerify(() => SessionModel.param("dyn", "expanderRatio") === 2, 500);
        // **Past the gesture**: an edit of one parameter that follows another
        // of it within half a second is the same step, and the settings above
        // are not what is being taken back.
        wait(600);

        const before = { "cutoff": ["expanderRatio", 2], "expanderThreshold": ["expanderThreshold", -70],
                         "threshold": ["threshold", -34], "height": ["height", 10] };
        for (const id in before) {
            const moves = id === "height" ? [0, -30] : [30, -30];
            testCase.drag(window, id, moves[0], moves[1]);
            const paramId = before[id][0];
            verify(SessionModel.param("dyn", paramId) !== before[id][1], id + " did not move");
            verify(SessionModel.canUndo, "nothing to take back after dragging " + id);

            SessionModel.undo();
            tryVerify(() => Math.abs(SessionModel.param("dyn", paramId) - before[id][1]) < 0.0001, 500,
                      "one undo did not take the whole drag of " + id + " back");
        }
    }

    function test_theKeyboardMovesEveryPointAlongItsLine() {
        // The criterion: all points are operable from the keyboard. **In a
        // window**, because a key event goes to the focused item of one.
        const panel = createTemporaryObject(panelComponent, testCase);
        panel.blockId = "dyn";
        SessionModel.setParam("dyn", "expanderThreshold", -60);
        SessionModel.setParam("dyn", "threshold", -20);
        SessionModel.setParam("dyn", "height", 10);
        wait(50);

        // The third: right and up go up the diagonal, left and down come
        // back, and Shift is the fine step, for trimming rather than aiming.
        const third = testCase.pointOf(panel, "threshold");
        third.forceActiveFocus();
        verify(third.activeFocus, "the point did not take focus");
        keyClick(Qt.Key_Right);
        compare(SessionModel.param("dyn", "threshold"), -18);
        keyClick(Qt.Key_Up);
        compare(SessionModel.param("dyn", "threshold"), -16);
        keyClick(Qt.Key_Left);
        keyClick(Qt.Key_Down);
        compare(SessionModel.param("dyn", "threshold"), -20);
        keyClick(Qt.Key_Right, Qt.ShiftModifier);
        compare(SessionModel.param("dyn", "threshold"), -19.5);
        keyClick(Qt.Key_Left, Qt.ShiftModifier);
        compare(SessionModel.param("dyn", "height"), 10);

        // The second, along the same line, with its ratio left alone.
        const second = testCase.pointOf(panel, "expanderThreshold");
        second.forceActiveFocus();
        SessionModel.setParam("dyn", "expanderRatio", 3);
        wait(20);
        keyClick(Qt.Key_Up);
        compare(SessionModel.param("dyn", "expanderThreshold"), -58);
        compare(SessionModel.param("dyn", "expanderRatio"), 3);
        keyClick(Qt.Key_Down);
        compare(SessionModel.param("dyn", "expanderThreshold"), -60);

        // The first goes along the floor, so a press is decibels of floor.
        const first = testCase.pointOf(panel, "cutoff");
        first.forceActiveFocus();
        const foot = panel.curve.cutoffDb();
        keyClick(Qt.Key_Right);
        fuzzyCompare(panel.curve.cutoffDb(), foot + 2, 0.01);
        keyClick(Qt.Key_Left);
        fuzzyCompare(panel.curve.cutoffDb(), foot, 0.01);
        fuzzyCompare(first.at.y, panel.curve.yOfDb(-120), 0.01);

        // The fourth is the height.
        const fourth = testCase.pointOf(panel, "height");
        fourth.forceActiveFocus();
        keyClick(Qt.Key_Up);
        compare(SessionModel.param("dyn", "height"), 12);
        keyClick(Qt.Key_Down);
        compare(SessionModel.param("dyn", "height"), 10);
        fuzzyCompare(fourth.at.x, panel.curve.xOfDb(40), 0.01);
    }

    function test_theKeyboardStopsWhereThePointerDoes() {
        const panel = createTemporaryObject(panelComponent, testCase);
        panel.blockId = "dyn";
        SessionModel.setParam("dyn", "expanderThreshold", -41);
        SessionModel.setParam("dyn", "threshold", -40);
        SessionModel.setParam("dyn", "height", 1);
        wait(50);

        // The second stops at the third.
        testCase.pointOf(panel, "expanderThreshold").forceActiveFocus();
        keyClick(Qt.Key_Up);
        compare(SessionModel.param("dyn", "expanderThreshold"), -40);
        // The third stops at the second.
        SessionModel.setParam("dyn", "threshold", -39);
        wait(20);
        testCase.pointOf(panel, "threshold").forceActiveFocus();
        keyClick(Qt.Key_Down);
        compare(SessionModel.param("dyn", "threshold"), -40);
        // The fourth stops at the third.
        testCase.pointOf(panel, "height").forceActiveFocus();
        keyClick(Qt.Key_Down);
        compare(SessionModel.param("dyn", "height"), 0);
        testCase.verifyUnderTheDiagonal(panel, "after the keys ran out");
    }

    function test_theThresholdsCannotBeTypedPastEachOther() {
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "dyn";
        SessionModel.setParam("dyn", "expanderThreshold", -60);
        SessionModel.setParam("dyn", "threshold", -20);
        panel.detailsButton.toggle();
        wait(50);

        for (let index = 0; index < panel.detailRows.count; ++index) {
            const row = panel.detailRows.itemAt(index);
            if (row.paramId === "threshold") {
                compare(row.minimum, -60);
                compare(row.maximum, 20);
            }
            if (row.paramId === "expanderThreshold") {
                compare(row.minimum, -120);
                compare(row.maximum, -20);
            }
        }

        // And should a session hold them the wrong way round all the same,
        // the curve is still the curve: the second point waits at the third.
        SessionModel.setParam("dyn", "expanderThreshold", -10);
        wait(50);
        fuzzyCompare(panel.curve.expanderStartDb(), -20, 0.01);
        testCase.verifyUnderTheDiagonal(panel, "with the thresholds in the wrong order");
    }

    function test_theMakeupLeavesTheGraphAlone() {
        const window = testCase.windowWith({ threshold: -30, height: 10 });
        const panel = window.panel;
        const before = panel.curve.curve;
        SessionModel.setParam("dyn", "makeup", 30);
        wait(50);
        compare(panel.curve.curve, before);
        testCase.verifyUnderTheDiagonal(panel, "with the make-up gain up");
    }

    function test_theFaceDrawsTheCurveAndSaysWhatIsBeingHeldBack() {
        const face = createTemporaryObject(faceComponent, null);
        face.blockId = "dyn";
        wait(50);

        verify(face.curve.curve.length > 1, "the face drew no curve");
        // Nothing is running, so there is nothing to report — which is not
        // the same as reporting nothing.
        compare(face.readout.text, "—");
    }

    // **The tile is the card's graph made small**: the same range and the
    // same shape. It once showed -60 to 0 dB only, so a gate below -60 and
    // a knee near the top, both on the card, were not on the tile at all.
    function test_theFaceDrawsTheSameGraphAsThePanel() {
        const window = testCase.windowWith({ expanderThreshold: -60, expanderRatio: 10, threshold: -24, height: 10 });
        const panel = window.panel;
        const face = createTemporaryObject(faceComponent, null);
        face.blockId = "dyn";
        waitForRendering(face);

        compare(face.curve.lowestDb, panel.curve.lowestDb, "the tile starts where the card does");
        compare(face.curve.highestDb, panel.curve.highestDb, "the tile ends where the card does");
        for (let db = -120; db <= 40; db += 10) {
            const onFace = face.curve.pointAt(db);
            const onPanel = panel.curve.pointAt(db);
            fuzzyCompare(onFace.x / face.curve.plotWidth, onPanel.x / panel.curve.plotWidth, 0.01,
                         "across, at " + db + " dB");
            fuzzyCompare(onFace.y / face.curve.plotHeight, onPanel.y / panel.curve.plotHeight, 0.01,
                         "up, at " + db + " dB");
        }
    }

    // **The presets footer is not this panel's any more**. It belongs
    // to the settings panel, which puts the same row under every block, and
    // is tested there — see tst_settingspanel.qml. The placeholder that
    // stood here has gone rather than being filled in twice.
}
