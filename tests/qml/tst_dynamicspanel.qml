// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Dynamics Processor's panel and face offscreen, against the mock
// engine: that the four handles are controls a keyboard can reach and move,
// that a drag writes the parameter the handle is named after, that Details
// opens on to every setting the block declares, and that the presets footer
// is drawn without pretending to work. What the curve itself draws is
// measured in app.DynamicsPanelTest, where there is a signal to read it
// against.

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
        verify(BlockPanels.panelFor("effect.dynamics") !== null, "the Dynamics block is in the panel register");
        verify(BlockFaces.faceFor("effect.dynamics") !== null, "the Dynamics block has a face of its own");
    }

    function test_theDetailsAreClosedWhenThePanelOpens() {
        // **Twelve numbers are what somebody comes back for**, not what they
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

    function test_everyHandleIsAControlThatSaysWhatItIsSetTo() {
        // The criterion: nothing is reachable by pointer alone, and a screen
        // reader is told which handle it is on and where it stands.
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "dyn";
        wait(50);

        compare(panel.handles.count, 4);
        for (let index = 0; index < panel.handles.count; ++index) {
            const handle = panel.handles.itemAt(index);
            verify(handle.activeFocusOnTab, "a handle that the tab order skips");
            verify(handle.Accessible.name.length > 0, "a handle with no name");
            verify(handle.Accessible.name.indexOf(panel.handleValue(handle.modelData)) >= 0,
                   "the name of " + handle.modelData + " does not say what it is set to");
        }
    }

    function test_theArrowKeysMoveAHandle() {
        // The criterion: all handles are operable from the keyboard. **In a
        // window**, because a key event goes to the focused item of one.
        const panel = createTemporaryObject(panelComponent, testCase);
        panel.blockId = "dyn";
        wait(50);

        const threshold = panel.handles.itemAt(1);
        compare(threshold.modelData, "threshold");
        threshold.forceActiveFocus();
        verify(threshold.activeFocus, "the handle did not take focus");

        keyClick(Qt.Key_Right);
        compare(SessionModel.param("dyn", "threshold"), -18);
        keyClick(Qt.Key_Left);
        compare(SessionModel.param("dyn", "threshold"), -20);

        // Shift is the fine step, for trimming rather than aiming.
        keyClick(Qt.Key_Right, Qt.ShiftModifier);
        compare(SessionModel.param("dyn", "threshold"), -19.5);

        // And the ratio is a multiplication, so up is a multiple of what it
        // was rather than an amount added to it.
        const ratio = panel.handles.itemAt(3);
        compare(ratio.modelData, "ratio");
        SessionModel.setParam("dyn", "ratio", 4);
        ratio.forceActiveFocus();
        keyClick(Qt.Key_Up);
        verify(SessionModel.param("dyn", "ratio") > 4, "up did not steepen the ratio");
        keyClick(Qt.Key_Down);
        fuzzyCompare(SessionModel.param("dyn", "ratio"), 4, 0.001);
    }

    function test_aHandleDraggedAcrossThePlotWritesItsParameter() {
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "dyn";
        wait(50);

        // Where −30 dBFS in lands on the plot, which is where somebody would
        // let go of the compressor's threshold to put it there.
        const at = panel.curve.pointAt(-30);
        panel.dragTo("threshold", at.x, at.y);
        panel.commit("threshold");
        fuzzyCompare(SessionModel.param("dyn", "threshold"), -30, 0.5);
        // The edit goes to the engine and comes back as a patch, and the
        // panel reads the session rather than what it just sent.
        // **The panel draws what it has sent.** A live edit is not news the
        // session tells anybody about until the gesture ends, and a handle
        // that waited for that would lag the pointer by the whole drag.
        fuzzyCompare(panel.value("threshold"), -30, 0.5);

        // The knee handle stands half a knee above the threshold, so
        // dragging it to 6 dB above the threshold makes a knee 12 dB wide.
        const kneeAt = panel.curve.pointAt(-24);
        panel.dragTo("knee", kneeAt.x, kneeAt.y);
        panel.commit("knee");
        fuzzyCompare(SessionModel.param("dyn", "knee"), 12, 0.5);

        // And nothing may be dragged past what the block allows: the
        // pointer may leave the plot, the parameter may not leave its range.
        const belowTheEnd = panel.curve.pointAt(-130);
        panel.dragTo("expanderThreshold", belowTheEnd.x, belowTheEnd.y);
        panel.commit("expanderThreshold");
        compare(SessionModel.param("dyn", "expanderThreshold"),
                BlockCatalogue.describeParam("effect.dynamics", "expanderThreshold").minimum);
    }

    // **The pointer, not the function behind it**. The test beside
    // this one calls dragTo() straight, which says the arithmetic is right
    // and nothing at all about whether a drag reaches it. It did not: the
    // handles took focus and stayed exactly where they were.
    function test_aHandleFollowsThePointer() {
        const window = createTemporaryObject(panelWindow, null);
        const panel = window.panel;
        panel.blockId = "dyn";
        waitForRendering(panel);

        const threshold = panel.handles.itemAt(1);
        compare(threshold.modelData, "threshold");
        compare(SessionModel.param("dyn", "threshold"), -20);

        // Across: a threshold is where the bend starts, so dragging left
        // makes the block start working sooner.
        const from = threshold.mapToItem(window.contentItem, threshold.width / 2, threshold.height / 2);
        mousePress(window.contentItem, from.x, from.y);
        mouseMove(window.contentItem, from.x - 12, from.y);
        mouseMove(window.contentItem, from.x - 30, from.y);
        mouseMove(window.contentItem, from.x - 48, from.y);
        mouseRelease(window.contentItem, from.x - 48, from.y);

        verify(SessionModel.param("dyn", "threshold") < -20,
               "the handle did not follow the pointer across the plot");
        // **And it was the handle that was grabbed.** The knee sits a few
        // pixels from the threshold and used to take the press instead,
        // which is what made the curve look unmovable: the wrong handle was
        // being dragged, straight into its own limit.
        compare(SessionModel.param("dyn", "knee"), 12, "the press landed on the knee handle");
    }

    function test_theRatioHandleFollowsThePointerUpAndDown() {
        const window = createTemporaryObject(panelWindow, null);
        const panel = window.panel;
        panel.blockId = "dyn";
        SessionModel.setParam("dyn", "ratio", 4);
        waitForRendering(panel);

        const ratio = panel.handles.itemAt(3);
        compare(ratio.modelData, "ratio");

        // Down steepens: less of what goes over the threshold comes out.
        const from = ratio.mapToItem(window.contentItem, ratio.width / 2, ratio.height / 2);
        mousePress(window.contentItem, from.x, from.y);
        mouseMove(window.contentItem, from.x, from.y + 12);
        mouseMove(window.contentItem, from.x, from.y + 30);
        mouseMove(window.contentItem, from.x, from.y + 48);
        mouseRelease(window.contentItem, from.x, from.y + 48);

        verify(SessionModel.param("dyn", "ratio") > 4,
               "the handle did not follow the pointer down the plot");
    }

    function test_aHandleWritesBothOfItsAxes() {
        // **A handle is a place on the curve, and a place has two numbers.**
        // Each handle used to answer only one of them, so half of every drag
        // did nothing.
        const window = createTemporaryObject(panelWindow, null);
        const panel = window.panel;
        panel.blockId = "dyn";
        waitForRendering(panel);

        compare(panel.handleParamList("threshold"), ["threshold", "makeup"]);
        // The lower end of the curve: across is where the expander starts,
        // down is how steeply it pushes what is under it away.
        compare(panel.handleParamList("expanderThreshold"), ["expanderThreshold", "expanderRange", "expanderRatio"]);
        // The upper end: across drags the bend with it, up and down is how
        // steep the line past the bend is.
        compare(panel.handleParamList("ratio"), ["threshold", "ratio"]);
        // A knee is a width, and means one thing.
        compare(panel.handleParamList("knee"), ["knee"]);

        const threshold = panel.handles.itemAt(1);
        const from = threshold.mapToItem(window.contentItem, threshold.width / 2, threshold.height / 2);
        mousePress(window.contentItem, from.x, from.y);
        mouseMove(window.contentItem, from.x - 20, from.y - 20);
        mouseMove(window.contentItem, from.x - 40, from.y - 40);

        verify(panel.dragging === "threshold",
               "the plot lost hold of the handle: the scroll view took the drag away");
        // **Both numbers are readable while the hand is on it**, so that
        // somebody aiming at one of them does not have to let go and look
        // somewhere else.
        verify(panel.dragReadout.visible, "nothing said what the drag was setting");

        mouseRelease(window.contentItem, from.x - 40, from.y - 40);
        verify(SessionModel.param("dyn", "threshold") < -20, "across did not write the threshold");
        verify(SessionModel.param("dyn", "makeup") > 0, "up did not write the make-up gain");
        compare(panel.dragging, "", "the plot kept hold of the handle after the button came up");
        verify(!panel.dragReadout.visible, "the readout stayed up after the drag");
    }

    function test_draggingTheExpanderHandleAcrossLeavesItsRatioAlone() {
        // **A ratio is not a place on the plot.** The expander's handle
        // rides the line where nothing is changed, whatever its ratio, so
        // reading the ratio off the pointer would set it to nothing the
        // moment the handle was grabbed — and a drag straight across would
        // flatten the curve for no reason. It moves by how far the pointer
        // has come instead.
        const window = createTemporaryObject(panelWindow, null);
        const panel = window.panel;
        panel.blockId = "dyn";
        SessionModel.setParam("dyn", "expanderThreshold", -70);
        SessionModel.setParam("dyn", "expanderRatio", 4);
        waitForRendering(panel);

        const expander = panel.handles.itemAt(0);
        compare(expander.modelData, "expanderThreshold");

        const from = expander.mapToItem(window.contentItem, expander.width / 2, expander.height / 2);
        mousePress(window.contentItem, from.x, from.y);
        mouseMove(window.contentItem, from.x + 14, from.y);
        mouseMove(window.contentItem, from.x + 28, from.y);
        mouseRelease(window.contentItem, from.x + 28, from.y);

        verify(SessionModel.param("dyn", "expanderThreshold") > -70,
               "the expander's threshold did not follow the pointer across");
        fuzzyCompare(SessionModel.param("dyn", "expanderRatio"), 4, 0.01,
                     "a drag straight across changed the expander's ratio");
        compare(SessionModel.param("dyn", "expanderRange"), 0,
                "a drag straight across opened the expander's range");
    }

    function test_draggingTheExpanderHandleDownSteepensIt() {
        const window = createTemporaryObject(panelWindow, null);
        const panel = window.panel;
        panel.blockId = "dyn";
        SessionModel.setParam("dyn", "expanderThreshold", -70);
        SessionModel.setParam("dyn", "expanderRatio", 4);
        waitForRendering(panel);

        const expander = panel.handles.itemAt(0);
        const from = expander.mapToItem(window.contentItem, expander.width / 2, expander.height / 2);
        mousePress(window.contentItem, from.x, from.y);
        mouseMove(window.contentItem, from.x, from.y + 14);
        mouseMove(window.contentItem, from.x, from.y + 28);
        mouseRelease(window.contentItem, from.x, from.y + 28);

        verify(SessionModel.param("dyn", "expanderRatio") > 4,
               "pulling the expander's handle down did not steepen it");
        // **And it opened the range**, or the curve would not have moved at
        // all: the range is the most the expander may take off and it starts
        // at nought.
        verify(SessionModel.param("dyn", "expanderRange") > 0,
               "pulling the expander's handle down bent nothing, because its range stayed shut");
    }

    function test_theKeyboardReachesBothOfAHandlesAxes() {
        const panel = createTemporaryObject(panelComponent, testCase);
        panel.blockId = "dyn";
        wait(50);

        const threshold = panel.handles.itemAt(1);
        threshold.forceActiveFocus();
        compare(SessionModel.param("dyn", "makeup"), 0);

        // Up is the handle's other parameter, so nothing on the curve is
        // reachable by pointer alone.
        keyClick(Qt.Key_Up);
        compare(SessionModel.param("dyn", "makeup"), 2);
        keyClick(Qt.Key_Down);
        compare(SessionModel.param("dyn", "makeup"), 0);
        // And across is still the one it is named after.
        keyClick(Qt.Key_Right);
        compare(SessionModel.param("dyn", "threshold"), -18);
    }

    function test_puttingANumberOnTheRatioTurnsLimitingOff() {
        // The infinite ratio is the click past the end of the slider; a
        // handle that answered nothing while it was on would read as a
        // handle that is broken.
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "dyn";
        SessionModel.setParam("dyn", "infiniteRatio", true);
        wait(50);

        panel.nudge("ratio", 0, -1, false);
        compare(SessionModel.param("dyn", "infiniteRatio"), false);
        verify(SessionModel.param("dyn", "ratio") < 20, "the ratio did not come down");
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

    // **The presets footer is not this panel's any more**. It belongs
    // to the settings panel, which puts the same row under every block, and
    // is tested there — see tst_settingspanel.qml. The placeholder that
    // stood here has gone rather than being filled in twice.
}
