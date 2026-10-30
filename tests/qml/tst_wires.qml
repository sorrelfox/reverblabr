// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The wires between the tiles, offscreen against the mock engine:
// that a Ctrl-drag from an output to an input connects, that a drop nothing
// can be done with leaves the session exactly as it was, that five blocks
// added in automatic mode come out as a chain, and that taking one out of
// the middle of that chain closes the gap behind it.
//
// **The mock is driven, not mocked.** TestEngine is the same facade the
// window is bound to, so a wire drawn here is the wire the real engine would
// be asked for — the cycle rule and the port limits included.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "SessionWires"
    when: windowShown

    property string sessionId: ""

    Component {
        id: canvasComponent

        SessionCanvas {
            width: 900
            height: 600
        }
    }

    // A canvas in a window of its own, for the tests that press a button and
    // move a pointer: an item with no window is an item no synthetic mouse
    // event can reach.
    Component {
        id: shownCanvas

        Window {
            property alias canvas: inner

            width: 900
            height: 600
            visible: true

            SessionCanvas {
                id: inner

                anchors.fill: parent
            }
        }
    }

    Component {
        id: wireComponent

        CanvasWire {}
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Wires");
        verify(testCase.sessionId !== "", "the mock opened a session");
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        SessionModel.sessionId = testCase.sessionId;
    }

    // One test's session is not the next test's: the singletons live as long
    // as the QML engine does. Removing every block takes every wire with it.
    function cleanup() {
        TestEngine.setAnswersLater(false);
        SessionModel.removeBlocks(SessionModel.blockIds);
        compare(SessionModel.blockIds.length, 0);
        compare(SessionModel.wireCount, 0);
    }

    /// Where a connector is in the canvas's own coordinates, which is where
    /// a synthetic press has to land.
    function viewPoint(canvas, blockId, lane, output) {
        const at = canvas.lanePoint(blockId, lane, output);
        return Qt.point(at.x * canvas.zoom + canvas.originX, at.y * canvas.zoom + canvas.originY);
    }

    /// What a person does: hold Ctrl, press on a connector, drag to another
    /// and let go.
    function ctrlDrag(canvas, fromBlock, fromLane, fromOutput, toBlock, toLane) {
        const from = testCase.viewPoint(canvas, fromBlock, fromLane, fromOutput);
        const to = testCase.viewPoint(canvas, toBlock, toLane, false);
        mousePress(canvas, from.x, from.y, Qt.LeftButton, Qt.ControlModifier);
        mouseMove(canvas, (from.x + to.x) / 2, (from.y + to.y) / 2);
        mouseMove(canvas, to.x, to.y);
        mouseRelease(canvas, to.x, to.y, Qt.LeftButton, Qt.ControlModifier);
    }

    function test_ctrlDraggingFromAnOutputToAnInputConnectsThroughTheEngine() {
        const window = createTemporaryObject(shownCanvas, null);
        const canvas = window.canvas;
        const source = canvas.addBlockAt("source.device", 120, 200);
        const volume = canvas.addBlockAt("effect.volume", 500, 200);
        waitForRendering(canvas);

        testCase.ctrlDrag(canvas, source, 0, true, volume, 0);

        // **One drag, one link, both channels**: the session holds a
        // wire a channel, and the canvas draws the one thing that was drawn.
        verify(SessionModel.hasWire(source, 0, volume, 0), "the drag drew no wire");
        verify(SessionModel.hasWire(source, 1, volume, 1), "the drag drew only one channel");
        compare(SessionModel.wireCount, 2);
        compare(canvas.wires.count, 1, "two channels were drawn as two wires");
        compare(canvas.wireShapes.count, 1);
        compare(canvas.wires.at(0).channels, 2);
        // And it survives being read back out of the engine's own copy.
        compare(SessionModel.wires()[0].fromBlock, source);
        compare(SessionModel.wires()[0].toBlock, volume);
    }

    function test_aWireIsSelectedByClickingItAndDeleteTakesItOut() {
        const window = createTemporaryObject(shownCanvas, null);
        const canvas = window.canvas;
        const source = canvas.addBlockAt("source.device", 120, 200);
        const volume = canvas.addBlockAt("effect.volume", 500, 320);
        verify(SessionModel.connectLanes(source, volume, 2), "the link was refused");
        compare(SessionModel.wireCount, 2);
        waitForRendering(canvas);

        // Halfway along the curve, which is nowhere near either tile.
        const from = canvas.lanePoint(source, 0, true);
        const to = canvas.lanePoint(volume, 0, false);
        const middle = canvas.wirePoint(from, to, canvas.curvature(from.x, to.x), 0.5);
        mouseClick(canvas, middle.x * canvas.zoom + canvas.originX, middle.y * canvas.zoom + canvas.originY);

        verify(canvas.selectedWire !== null, "the click landed on nothing");
        verify(canvas.isWireSelected(source, volume));
        compare(canvas.selectedIds.length, 0, "a wire and a tile are never selected together");
        compare(canvas.wireShapes.itemAt(0).selected, true);

        keyClick(Qt.Key_Delete);
        compare(SessionModel.wireCount, 0);
        compare(canvas.selectedWire, null);
    }

    function test_aDropThatWouldCloseALoopIsRefusedAndChangesNothing() {
        const window = createTemporaryObject(shownCanvas, null);
        const canvas = window.canvas;
        const first = canvas.addBlockAt("effect.volume", 150, 200);
        const second = canvas.addBlockAt("effect.volume", 550, 200);
        verify(SessionModel.connectLanes(first, second, 2));
        waitForRendering(canvas);

        // Back the way it came: the graph compiler could not schedule it,
        // so the session refuses it while the pointer is still on the port.
        compare(canvas.laneRefusal(second, first), "wireWouldCycle");
        testCase.ctrlDrag(canvas, second, 0, true, first, 0);

        compare(SessionModel.wireCount, 2, "the link that was there is two channels, and no more");
        verify(!SessionModel.hasWire(second, 0, first, 0), "the loop was drawn anyway");
        compare(canvas.wiring, false, "the drag was left half finished");
    }

    function test_ALaneNoBlockOfThatTypeHasIsRefused() {
        const canvas = createTemporaryObject(canvasComponent, null);
        const volume = canvas.addBlockAt("effect.volume", 150, 200);
        const meter = canvas.addBlockAt("meter.peakrms", 550, 200);
        const source = canvas.addBlockAt("source.device", 150, 400);

        // Both stereo blocks have a lane on each edge, so a link between
        // them is drawn. The shape is the catalogue's, which is the
        // engine's own block registry.
        compare(canvas.laneRefusal(volume, meter), "");
        // An edge the audio system owns is not a lane a link can reach, and
        // a link that named one is refused rather than drawn into nothing.
        compare(canvas.laneRefusal(volume, source), "wireIntoSource");
    }

    function test_nothingIsWiredIntoASourceOrOutOfAnOutput() {
        // A capture block's inputs are filled by the audio system with the
        // application or device it follows, and an output block's outputs
        // belong to the device. Neither is a port on the canvas, so neither
        // ever lights up as somewhere a wire can be dropped.
        const canvas = createTemporaryObject(canvasComponent, null);
        const source = canvas.addBlockAt("source.device", 150, 200);
        const volume = canvas.addBlockAt("effect.volume", 400, 200);
        const output = canvas.addBlockAt("output.device", 650, 200);
        compare(SessionModel.wireCount, 0, "a block carried to a spot is placed, not wired");

        compare(canvas.laneRefusal(volume, source), "wireIntoSource");
        compare(canvas.laneRefusal(output, volume), "wireOutOfOutput");
        compare(canvas.laneRefusal(source, volume), "");
        compare(canvas.laneRefusal(volume, output), "");
    }

    function test_aPortNoWireCanReachIsNotDrawn() {
        // **A dot that looks like somewhere a wire goes and never is.**
        // A capture block's inputs are filled by what it follows and an
        // output block's outputs belong to the device, so neither edge has
        // anything to show. The block says which those are, not the
        // sidebar it is listed under.
        const canvas = createTemporaryObject(canvasComponent, null);
        SessionModel.setAutoConnect(false);
        const source = canvas.addBlockAt("source.device", 150, 200);
        const volume = canvas.addBlockAt("effect.volume", 400, 200);
        const output = canvas.addBlockAt("output.device", 650, 200);

        compare(canvas.laneCountOf(source, false), 0, "a capture block drew its captured inputs");
        compare(canvas.laneCountOf(output, true), 0, "an output block drew the device's outputs");
        // The edges that *are* wiring show one connector each, whatever
        // they carry: a link carries a stereo signal.
        compare(canvas.laneCountOf(source, true), 1);
        compare(canvas.laneCountOf(output, false), 1);
        compare(canvas.laneCountOf(volume, false), 1);
        compare(canvas.laneCountOf(volume, true), 1);
        // The channels are still there underneath, which is what a link is
        // carried by and what the session holds a wire for.
        compare(canvas.channelCountOf(volume, false), 2);
        compare(canvas.channelCountOf(volume, true), 2);

        const sourceTile = testCase.tileFor(canvas, source);
        const outputTile = testCase.tileFor(canvas, output);
        compare(sourceTile.inputPorts.count, 0, "the tile drew a dot nothing can be wired to");
        compare(sourceTile.outputPorts.count, 1);
        compare(outputTile.outputPorts.count, 0, "the tile drew a dot nothing can be wired to");
        compare(outputTile.inputPorts.count, 1);

        // **And the counting did not go wrong with it**, which was the risk:
        // where a connector sits is worked out from how many that edge has,
        // so an edge that stopped being drawn must not move the other one.
        // The one that is left sits in the middle of its edge, and inside
        // the tile.
        verify(SessionModel.connectLanes(source, output, 2));
        const arrives = canvas.lanePoint(output, 0, false);
        const leaves = canvas.lanePoint(source, 0, true);
        compare(arrives.y, outputTile.y + outputTile.height / 2, "the connector is not on the middle of its edge");
        compare(arrives.x, outputTile.x, "the input is not on the left edge");
        for (const at of [arrives, leaves]) {
            verify(at.y >= outputTile.y && at.y <= outputTile.y + outputTile.height * 2,
                   "a connector landed outside the tiles");
        }

        SessionModel.setAutoConnect(true);
    }

    function test_fiveBlocksAddedInAutomaticModeFormAChain() {
        const canvas = createTemporaryObject(canvasComponent, null);
        verify(SessionModel.autoConnect, "a new session wires itself up by default");

        const added = [];
        added.push(canvas.addBlockAfterSelection("source.device"));
        added.push(canvas.addBlockAfterSelection("effect.volume"));
        added.push(canvas.addBlockAfterSelection("meter.peakrms"));
        added.push(canvas.addBlockAfterSelection("effect.volume"));
        added.push(canvas.addBlockAfterSelection("output.device"));
        for (const id of added) {
            verify(id !== "", "the engine gave every block an id");
        }

        // Four joins, both channels of each: a chain wired only down the
        // left would go quietly mono with nothing saying why.
        for (let step = 0; step + 1 < added.length; ++step) {
            verify(SessionModel.hasWire(added[step], 0, added[step + 1], 0),
                   "no wire from " + added[step] + " to " + added[step + 1]);
            verify(SessionModel.hasWire(added[step], 1, added[step + 1], 1),
                   "only one channel from " + added[step] + " to " + added[step + 1]);
        }
        compare(SessionModel.wireCount, 8);

        // And every tile is somewhere of its own, which is the other half of
        // the rule: to the right, moving down if that spot is taken.
        for (let one = 0; one < added.length; ++one) {
            for (let other = one + 1; other < added.length; ++other) {
                verify(SessionModel.block(added[one]).position !== SessionModel.block(added[other]).position,
                       "two tiles were put in the same place");
            }
        }
    }

    function test_withAutomaticConnectionsOffNothingIsWiredByItself() {
        const canvas = createTemporaryObject(canvasComponent, null);
        SessionModel.setAutoConnect(false);
        verify(!SessionModel.autoConnect);

        canvas.addBlockAfterSelection("source.device");
        canvas.addBlockAfterSelection("effect.volume");
        compare(SessionModel.wireCount, 0);

        SessionModel.setAutoConnect(true);
    }

    function test_takingABlockOutOfTheMiddleOfALaneClosesTheGap() {
        const canvas = createTemporaryObject(canvasComponent, null);
        const source = canvas.addBlockAfterSelection("source.device");
        const middle = canvas.addBlockAfterSelection("effect.volume");
        const output = canvas.addBlockAfterSelection("output.device");
        compare(SessionModel.wireCount, 4);

        canvas.selectOnly(middle);
        canvas.removeSelected();

        compare(SessionModel.blockIds.length, 2);
        verify(SessionModel.hasWire(source, 0, output, 0), "the lane was left in two halves");
        verify(SessionModel.hasWire(source, 1, output, 1));
        compare(SessionModel.wireCount, 2);
    }

    // **The gap closes with an engine that answers later**, as the one in
    // reverblabrd does over the bus. Whether the block had gone was asked
    // straight after the removal was sent, when it had not gone yet, so
    // the lane was left in two halves in the installed application.
    function test_theGapClosesWhenTheEngineAnswersLater() {
        const canvas = createTemporaryObject(canvasComponent, null);
        const source = canvas.addBlockAfterSelection("source.device");
        const first = canvas.addBlockAfterSelection("effect.volume");
        const second = canvas.addBlockAfterSelection("effect.volume");
        const output = canvas.addBlockAfterSelection("output.device");
        compare(SessionModel.wireCount, 6);
        TestEngine.setAnswersLater(true);

        canvas.selectOnly(first);
        canvas.removeSelected();
        tryVerify(() => SessionModel.blockIds.length === 3, 1000, "the block was not taken out");
        tryVerify(() => SessionModel.hasWire(source, 0, second, 0) && SessionModel.hasWire(source, 1, second, 1), 1000,
                  "the lane was left in two halves");
        compare(SessionModel.wireCount, 4);

        // **One step to take back**, the block and the wire that replaced it.
        SessionModel.undo();
        tryVerify(() => SessionModel.blockIds.length === 4, 1000, "one undo did not put the block back");
        tryVerify(() => SessionModel.wireCount === 6, 1000, "one undo did not put the lane back as it was");
        verify(!SessionModel.hasWire(source, 0, second, 0), "the wire that closed the gap outlived the undo");

        // **Two at once**, each closing the gap the other left, though
        // neither removal has been answered when the next is worked out.
        SessionModel.removeBlocks([first, second]);
        tryVerify(() => SessionModel.blockIds.length === 2, 1000, "the two blocks were not taken out");
        tryVerify(() => SessionModel.hasWire(source, 0, output, 0) && SessionModel.hasWire(source, 1, output, 1), 1000,
                  "taking two blocks out left a hole in the lane");
        compare(SessionModel.wireCount, 2);
    }

    // **Two blocks added before the engine has answered the first** are
    // two blocks. Each was given the session's next free id, which is the
    // same id until the engine answers, so the second was refused; added
    // from the library, it was also wired to a block that never came.
    function test_twoBlocksAddedBeforeTheEngineAnswersAreTwoBlocks() {
        const canvas = createTemporaryObject(canvasComponent, null);
        TestEngine.setAnswersLater(true);
        const first = SessionModel.addBlock("effect.volume", Qt.point(0, 0));
        const second = SessionModel.addBlock("effect.volume", Qt.point(300, 0));
        verify(first !== second, "both blocks were given " + first);
        tryVerify(() => SessionModel.blockIds.length === 2, 1000, "the second block was not added");

        canvas.selectOnly(second);
        const third = canvas.addBlockAfterSelection("effect.volume");
        const fourth = canvas.addBlockAfterSelection("output.device");
        verify(third !== fourth, "both blocks from the library were given " + third);
        tryVerify(() => SessionModel.blockIds.length === 4, 1000, "the blocks from the library were not both added");
        tryVerify(() => SessionModel.hasWire(second, 0, third, 0) && SessionModel.hasWire(third, 0, fourth, 0), 1000,
                  "the blocks from the library were not wired one after the other");

        // **A removed block's id is free again** once the engine has said so.
        SessionModel.removeBlock(first);
        tryVerify(() => SessionModel.blockIds.indexOf(first) < 0, 1000, "the block was not taken out");
        compare(SessionModel.addBlock("effect.volume", Qt.point(0, 300)), first);
        tryVerify(() => SessionModel.blockIds.length === 4, 1000, "the block was not added again");
    }

    function test_aLaneSeveralLinksArriveAtIsMarkedAsSumming() {
        const canvas = createTemporaryObject(canvasComponent, null);
        SessionModel.setAutoConnect(false);
        const first = canvas.addBlockAt("source.device", 120, 120);
        const second = canvas.addBlockAt("source.device", 120, 360);
        const output = canvas.addBlockAt("output.device", 600, 240);

        verify(SessionModel.connectLanes(first, output, 2));
        compare(canvas.wires.at(0).merging, false,
                "the two channels of one link are not two links arriving");

        // A second link into the same lane is an implicit sum in the engine,
        // and both links have to say so — not only the newcomer.
        verify(SessionModel.connectLanes(second, output, 2));
        compare(SessionModel.arrivingLinks(output), 2);
        compare(canvas.wires.at(0).merging, true);
        compare(canvas.wires.at(1).merging, true);

        SessionModel.setAutoConnect(true);
    }

    function test_aLinkIsDraggedOffOneBlockAndOntoAnother() {
        const window = createTemporaryObject(shownCanvas, null);
        const canvas = window.canvas;
        SessionModel.setAutoConnect(false);
        const source = canvas.addBlockAt("source.device", 120, 200);
        const volume = canvas.addBlockAt("effect.volume", 560, 200);
        const meter = canvas.addBlockAt("meter.peakrms", 560, 420);
        verify(SessionModel.connectLanes(source, volume, 2));
        waitForRendering(canvas);

        // Grabbing the end that is on the input carries the link; letting go
        // on another block's input moves the whole of it there.
        testCase.ctrlDrag(canvas, volume, 0, false, meter, 0);

        verify(!SessionModel.hasWire(source, 0, volume, 0), "the link stayed where it was");
        verify(SessionModel.hasWire(source, 0, meter, 0), "the link was lost rather than moved");
        verify(SessionModel.hasWire(source, 1, meter, 1), "only one channel was moved");
        compare(SessionModel.wireCount, 2);

        SessionModel.setAutoConnect(true);
    }

    function test_aWireDraggedNowhereIsPutBackWhereItWas() {
        const window = createTemporaryObject(shownCanvas, null);
        const canvas = window.canvas;
        SessionModel.setAutoConnect(false);
        const source = canvas.addBlockAt("source.device", 120, 200);
        const volume = canvas.addBlockAt("effect.volume", 560, 200);
        verify(SessionModel.connectLanes(source, volume, 2));
        waitForRendering(canvas);

        const grab = testCase.viewPoint(canvas, volume, 0, false);
        mousePress(canvas, grab.x, grab.y, Qt.LeftButton, Qt.ControlModifier);
        mouseMove(canvas, 40, 560);
        mouseRelease(canvas, 40, 560, Qt.LeftButton, Qt.ControlModifier);

        verify(SessionModel.hasWire(source, 0, volume, 0), "a link dropped on nothing was lost");
        verify(SessionModel.hasWire(source, 1, volume, 1), "half the link was lost");
        compare(SessionModel.wireCount, 2);

        SessionModel.setAutoConnect(true);
    }

    function test_onlyTheConnectorsThatWouldTakeTheLinkLightUp() {
        const canvas = createTemporaryObject(canvasComponent, null);
        SessionModel.setAutoConnect(false);
        const source = canvas.addBlockAt("source.device", 120, 200);
        const volume = canvas.addBlockAt("effect.volume", 560, 200);

        compare(canvas.isWireCandidate(volume, 0, false), false, "nothing lights up when nothing is being drawn");

        canvas.beginWire(source, 0, true);
        verify(canvas.wiring);
        compare(canvas.isWireCandidate(volume, 0, false), true);
        compare(canvas.isWireCandidate(volume, 0, true), false, "an output is never a place to drop a wire");
        compare(canvas.isWireCandidate(source, 0, false), false, "a source never takes a wire");
        canvas.cancelWire();
        compare(canvas.isWireCandidate(volume, 0, false), false);

        SessionModel.setAutoConnect(true);
    }

    function test_aWireIsReallyDrawnBetweenTheTwoConnectors() {
        // **A Shape is not clipped but it is culled.** Its own size is the
        // bounding box the scene graph uses, so a wire whose box did not
        // cover its curve would be there at one zoom and gone at another —
        // and nothing else in this file would notice.
        const window = createTemporaryObject(shownCanvas, null);
        const canvas = window.canvas;
        SessionModel.setAutoConnect(false);
        const source = canvas.addBlockAt("source.device", 150, 180);
        const volume = canvas.addBlockAt("effect.volume", 620, 400);
        verify(SessionModel.connectLanes(source, volume, 2));
        waitForRendering(canvas);

        const shape = canvas.wireShapes.itemAt(0);
        verify(shape !== null, "no curve was built for the link");
        const from = canvas.lanePoint(source, 0, true);
        const to = canvas.lanePoint(volume, 0, false);
        for (const end of [from, to]) {
            verify(end.x >= shape.x && end.x <= shape.x + shape.width,
                   "the curve's own bounds do not reach its end at " + end.x);
            verify(end.y >= shape.y && end.y <= shape.y + shape.height,
                   "the curve's own bounds do not reach its end at " + end.y);
        }
        // The control points reach sideways, so the box has room for them.
        verify(shape.width >= Math.abs(to.x - from.x) + canvas.curvature(from.x, to.x),
               "the curve's bounds are too narrow for the bulge in it");

        SessionModel.setAutoConnect(true);
    }

    function test_aWireCarryingAudioIsDrawnInTheLiveColour() {
        // **The colour is a binding, and this is the binding.** What makes a
        // wire live is the telemetry the engine publishes for the block it
        // leaves, which no offscreen test has; what can be held to here is
        // that live and idle are two colours and which is which.
        const wire = createTemporaryObject(wireComponent, null);
        wire.live = false;
        compare(wire.strokeColor, AppColors.wireMark);
        wire.live = true;
        compare(wire.strokeColor, AppColors.liveMark);
        verify(AppColors.liveMark !== AppColors.wireMark, "live and idle are the same colour");

        // Selected beats both: a wire somebody picked out has to be findable
        // whatever it is carrying.
        wire.selected = true;
        compare(wire.strokeColor, AppColors.accentMark);
    }

    function test_aWireGoesLiveWithinAHundredMillisecondsOfTheSignal() {
        // **Live colouring, finally checked rather than reasoned about.**
        // The colour follows telemetry the engine publishes, which nothing
        // offscreen could put there until a test could drive it; the mock
        // now publishes made-up levels for the blocks of a running session,
        // so the whole path — snapshot, hub, `Meters.levels().active`, the
        // binding — is what this drives, not a property set by hand.
        const canvas = createTemporaryObject(canvasComponent, null);
        const source = canvas.addBlockAfterSelection("source.device");
        canvas.addBlockAfterSelection("effect.volume");
        Meters.watch(testCase.sessionId);

        TestEngine.start(testCase.sessionId);
        tryVerify(() => Meters.live, 1000, "a running session published nothing");
        compare(canvas.wireIsLive(source), false, "a running session with no signal had a live wire");

        TestEngine.setFakeLevel(testCase.sessionId, source, 0.5);
        tryVerify(() => canvas.wireIsLive(source), 100,
                  "the wire did not go live within 100 ms of the signal");
        tryVerify(() => canvas.wireShapes.itemAt(0).strokeColor === AppColors.liveMark, 100,
                  "the wire was live and not drawn in the live colour");

        // And back: silence takes the colour off again, which is the half
        // that a wire stuck on would pass without.
        TestEngine.clearFakeLevels(testCase.sessionId);
        tryVerify(() => !canvas.wireIsLive(source), 100,
                  "the wire stayed live after the signal stopped");

        // Stopping takes every wire down, whatever was last published.
        TestEngine.setFakeLevel(testCase.sessionId, source, 0.5);
        tryVerify(() => canvas.wireIsLive(source), 100, "the signal did not come back");
        TestEngine.stop(testCase.sessionId);
        tryVerify(() => !canvas.wireIsLive(source), 200,
                  "a stopped session still had a live wire");

        Meters.watch("");
    }

    function test_withNothingRunningNoWireIsLiveAndNothingIsAnimated() {
        const canvas = createTemporaryObject(canvasComponent, null);
        const source = canvas.addBlockAfterSelection("source.device");
        canvas.addBlockAfterSelection("effect.volume");

        compare(canvas.wireIsLive(source), false, "a session that is not running is not a session with signal");
        compare(canvas.flowing, false, "the dash was animating with nothing to show");
        compare(canvas.wireShapes.itemAt(0).live, false);
        compare(canvas.wireShapes.itemAt(0).strokeColor, AppColors.wireMark);
    }

    function test_aTileSaysWhatItIsWiredTo() {
        // The gap the canvas once had: a tile could say what it was and
        // what it was doing, but not what it was joined to, because the
        // session model had no wires in it to read.
        const canvas = createTemporaryObject(canvasComponent, null);
        const source = canvas.addBlockAfterSelection("source.device");
        const volume = canvas.addBlockAfterSelection("effect.volume");
        SessionModel.renameBlock(source, "Desk mic");

        const tile = testCase.tileFor(canvas, volume);
        verify(tile !== null, "there is a tile to read");
        verify(tile.connectionText.indexOf("Desk mic") >= 0,
               "the tile says '" + tile.connectionText + "' rather than what feeds it");
        verify(tile.Accessible.description.indexOf("Desk mic") >= 0,
               "the description a screen reader gets says nothing about the wires");

        const alone = canvas.addBlockAt("effect.volume", 700, 500);
        compare(testCase.tileFor(canvas, alone).connectionText, "Not connected");
    }

    /// The tile showing \a blockId, or null.
    function tileFor(canvas, blockId) {
        for (let row = 0; row < canvas.tiles.count; ++row) {
            const tile = canvas.tiles.itemAt(row);
            if (tile !== null && tile.blockId === blockId) {
                return tile;
            }
        }
        return null;
    }

    // --- a block that takes more than one signal ---

    function test_aBlockWithTwoLanesHasAConnectorForEachNamedForWhatItIsFor() {
        const canvas = createTemporaryObject(canvasComponent, null);
        const ducker = canvas.addBlockAt("advanced.ducking", 500, 200);
        compare(canvas.laneCountOf(ducker, false), 2);
        compare(canvas.laneCountOf(ducker, true), 1, "the output is one signal");
        compare(canvas.laneChannelsOf(ducker, false), 2);
        compare(canvas.firstPortOf(ducker, 1, false), 2);
        compare(canvas.laneIdOf(ducker, 0, false), "main");
        compare(canvas.laneIdOf(ducker, 1, false), "background");
        verify(canvas.lanePoint(ducker, 1, false).y > canvas.lanePoint(ducker, 0, false).y,
               "the two connectors are on top of each other");

        const tile = testCase.tileFor(canvas, ducker);
        verify(tile !== null, "the ducker has no tile");
        compare(tile.inputPorts.count, 2);
        compare(tile.inputPorts.itemAt(0).name, "Main");
        compare(tile.inputPorts.itemAt(1).name, "Background");
        compare(tile.outputPorts.itemAt(0).name, "", "a single output has nothing to tell apart");
        // A block with one lane is exactly what it was.
        const volume = canvas.addBlockAt("effect.volume", 500, 420);
        compare(canvas.laneCountOf(volume, false), 1);
        compare(testCase.tileFor(canvas, volume).inputPorts.itemAt(0).name, "");
    }

    function test_aLinkLetGoOverTheBackgroundArrivesAtTheBackgroundsPorts() {
        const window = createTemporaryObject(shownCanvas, null);
        const canvas = window.canvas;
        SessionModel.setAutoConnect(false);
        const voice = canvas.addBlockAt("source.device", 100, 120);
        const music = canvas.addBlockAt("source.system", 100, 360);
        const ducker = canvas.addBlockAt("advanced.ducking", 520, 240);
        waitForRendering(canvas);

        testCase.ctrlDrag(canvas, voice, 0, true, ducker, 0);
        testCase.ctrlDrag(canvas, music, 0, true, ducker, 1);

        verify(SessionModel.hasWire(voice, 0, ducker, 0) && SessionModel.hasWire(voice, 1, ducker, 1),
               "the voice is not on the main lane");
        verify(SessionModel.hasWire(music, 0, ducker, 2) && SessionModel.hasWire(music, 1, ducker, 3),
               "the music is not on the background lane");
        compare(SessionModel.wireCount, 4);
        compare(canvas.wires.count, 2);
        // Each curve ends at the connector it was let go over.
        for (let row = 0; row < canvas.wires.count; ++row) {
            const link = canvas.wires.at(row);
            compare(link.toLane, link.fromBlock === music ? 1 : 0, link.fromBlock);
            const curve = canvas.wireShapes.itemAt(row);
            const end = canvas.lanePoint(ducker, link.toLane, false);
            compare(Qt.point(curve.toX, curve.toY), end);
        }
        // Two different sources into two different lanes is not a sum.
        compare(canvas.wires.at(0).merging, false);
        compare(canvas.wires.at(1).merging, false);
        SessionModel.setAutoConnect(true);
    }

    function test_oneSourceIntoBothLanesIsTwoLinksAndOneComesOutAlone() {
        const window = createTemporaryObject(shownCanvas, null);
        const canvas = window.canvas;
        SessionModel.setAutoConnect(false);
        const source = canvas.addBlockAt("source.device", 100, 240);
        const ducker = canvas.addBlockAt("advanced.ducking", 560, 240);
        verify(SessionModel.connectLanes(source, ducker, 2, 0));
        compare(canvas.laneRefusal(source, ducker, 0), "wireAlreadyExists");
        compare(canvas.laneRefusal(source, ducker, 1), "", "the other lane is refused for the first one's link");
        verify(SessionModel.connectLanes(source, ducker, 2, 2));
        compare(canvas.wires.count, 2);
        waitForRendering(canvas);

        // Click the background's curve, halfway along, and delete it.
        const row = canvas.wires.at(0).toLane === 1 ? 0 : 1;
        const from = canvas.lanePoint(source, 0, true);
        const to = canvas.lanePoint(ducker, 1, false);
        const middle = canvas.wirePoint(from, to, canvas.curvature(from.x, to.x), 0.5);
        canvas.selectWire(canvas.wires.at(row));
        verify(canvas.isWireSelected(source, ducker, 1));
        verify(!canvas.isWireSelected(source, ducker, 0), "both links are selected at once");
        compare(canvas.wireShapes.itemAt(row).selected, true);
        compare(canvas.wireShapes.itemAt(1 - row).selected, false);
        canvas.removeSelectedWire();

        verify(SessionModel.hasWire(source, 0, ducker, 0), "the main lane's link went with it");
        verify(!SessionModel.hasWire(source, 0, ducker, 2), "the background's link is still there");
        compare(SessionModel.wireCount, 2);
        compare(canvas.wires.count, 1);
        SessionModel.setAutoConnect(true);
    }

    function test_aLinkIsMovedFromOneLaneToTheOther() {
        const window = createTemporaryObject(shownCanvas, null);
        const canvas = window.canvas;
        SessionModel.setAutoConnect(false);
        const source = canvas.addBlockAt("source.device", 100, 240);
        const ducker = canvas.addBlockAt("advanced.ducking", 560, 240);
        verify(SessionModel.connectLanes(source, ducker, 2, 0));
        waitForRendering(canvas);

        testCase.ctrlDrag(canvas, ducker, 0, false, ducker, 1);

        verify(!SessionModel.hasWire(source, 0, ducker, 0), "the link stayed on the main lane");
        verify(SessionModel.hasWire(source, 0, ducker, 2) && SessionModel.hasWire(source, 1, ducker, 3),
               "the link did not arrive at the background");
        compare(SessionModel.wireCount, 2);
        compare(canvas.wires.at(0).toLane, 1);
        SessionModel.setAutoConnect(true);
    }

    function test_onlyTheLaneTwoLinksArriveAtSums() {
        const canvas = createTemporaryObject(canvasComponent, null);
        SessionModel.setAutoConnect(false);
        const voice = canvas.addBlockAt("source.device", 100, 100);
        const first = canvas.addBlockAt("source.system", 100, 300);
        const second = canvas.addBlockAt("source.application", 100, 500);
        const ducker = canvas.addBlockAt("advanced.ducking", 560, 300);
        verify(SessionModel.connectLanes(voice, ducker, 2, 0));
        verify(SessionModel.connectLanes(first, ducker, 2, 2));
        verify(SessionModel.connectLanes(second, ducker, 2, 2));

        compare(SessionModel.arrivingLinks(ducker, 0, 2), 1);
        compare(SessionModel.arrivingLinks(ducker, 2, 2), 2);
        for (let row = 0; row < canvas.wires.count; ++row) {
            const link = canvas.wires.at(row);
            compare(link.merging, link.toLane === 1, link.fromBlock + " into lane " + link.toLane);
        }
        const tile = testCase.tileFor(canvas, ducker);
        compare(tile.inputPorts.itemAt(0).merging, false, "the voice's lane is marked as summing");
        compare(tile.inputPorts.itemAt(1).merging, true);
        SessionModel.setAutoConnect(true);
    }

    function test_aBlockAddedAfterADuckerIsFedFromItAndOneAddedBeforeGoesToItsMain() {
        const canvas = createTemporaryObject(canvasComponent, null);
        const source = canvas.addBlockAt("source.device", 100, 240);
        const ducker = canvas.addBlockAt("advanced.ducking", 400, 240);
        verify(canvas.autoConnect(ducker, source));
        verify(SessionModel.hasWire(source, 0, ducker, 0) && SessionModel.hasWire(source, 1, ducker, 1));
        verify(!SessionModel.hasWire(source, 0, ducker, 2), "an automatic link went to the background");
    }
}
