// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The canvas's frame budget ([ADR-0009]): thirty tiles, their
// faces, the wires between them and the dot grid, with the view moving every
// frame so that nothing is drawn once and left alone.
//
// **It cannot be measured headless and says so.** The offscreen platform has
// no compositor and no vertical blank, so the intervals it reports measure a
// timer rather than a frame; this skips there rather than reporting a number
// that means nothing. To run it for real, from a Plasma Wayland session:
//
//     build/debug/bin/reverblabr-qml-test -input tests/qml/tst_canvasframes.qml
//
// (with QT_QPA_PLATFORM unset, so the real platform plugin is used).
//
// **The empty canvas is the ruler.** What QML can time is how far apart
// frames arrived, and that is set by the compositor: a window that is not in
// front, a screen at 50 Hz and a system under load all change it, and none
// of them is anything to do with this code. So the same window is measured
// with nothing on it and then with thirty tiles on it, and what is asserted
// is that the tiles cost nothing on top of what the window costs anyway.
// A canvas that really took longer than a frame would push the intervals
// above the cadence, which is exactly what this would catch.
//
// **The wires are measured with the tiles, not instead of them.** A wire is
// drawn between two tiles that both move, so every one of them is processed
// again on every frame of a pan — which is why the view is moved here.
//
// **The moving dash is measured too**, which it once could not be:
// what makes a wire live is telemetry from a running session, and the mock
// now publishes made-up levels for one. It is the expensive thing the canvas
// draws — ADR-0009 measured 0.7 ms a frame for forty live wires — and the
// canvas stops it when the window is not in front, when the view is zoomed
// out past 75 % and when nothing is playing, so the test has to hold all
// three open before the figure means anything.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "SessionCanvasFrames"
    when: windowShown

    /// How many tiles the budget is stated for, and how many wires join
    /// them up. Every tile after the first is wired from the one before it
    /// on both channels, which is the shape a session of this size takes.
    readonly property int tileCount: 30
    readonly property int wireCount: (30 - 1) * 2
    /// How many frames each run is measured over. Two seconds or so at
    /// 60 Hz, which is long enough for the first frames' warm-up to stop
    /// mattering.
    readonly property int frameCount: 120
    /// How much slower a canvas full of tiles and wires may be than the same
    /// window with nothing on it. Fifteen per cent of a frame is more than
    /// the spike's whole scene cost and less than a dropped frame.
    readonly property real allowedOverhead: 1.15
    /// And how much slower again the same scene may be with every wire
    /// live and its dash moving. A quarter of a frame on top: ADR-0009 put
    /// forty live wires at 0.7 ms, and there are more wires than that here.
    readonly property real allowedDashOverhead: 1.25

    property string sessionId: ""

    Component {
        id: frameWindow

        Window {
            property alias canvas: canvas

            width: 1200
            height: 800
            visible: true

            SessionCanvas {
                id: canvas

                anchors.fill: parent
            }
        }
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Frames");
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function cleanupTestCase() {
        SessionModel.removeBlocks(SessionModel.blockIds);
    }

    /// How long the frames of \a canvas took, in milliseconds, sorted. The
    /// view moves a pixel each frame so that every tile, the grid and the
    /// faces are processed again each time: a scene that is drawn once and
    /// left alone measures nothing.
    ///
    /// With \a dragging the tiles are moved instead of the view, which is a
    /// harder thing to ask: panning moves one item and every wire keeps the
    /// ends it had, while dragging tiles makes each wire work out both of
    /// its ends again on every frame.
    function frameTimes(canvas, dragging) {
        const taken = [];
        const ticker = Qt.createQmlObject(
            "import QtQuick; FrameAnimation { running: true }", canvas, "frameTicker");
        ticker.triggered.connect(function () {
            if (dragging === true) {
                canvas.dragBy((taken.length % 2 === 0) ? 1 : -1, 1);
            } else {
                canvas.originX += (taken.length % 2 === 0) ? 1 : -1;
            }
            // The first few frames are the window arriving, not the canvas
            // drawing.
            if (ticker.currentFrame > 5) {
                taken.push(ticker.frameTime * 1000);
            }
        });
        tryVerify(() => taken.length >= testCase.frameCount, 10000, "the window kept producing frames");
        ticker.running = false;
        ticker.destroy();
        return taken.slice(0, testCase.frameCount).sort((a, b) => a - b);
    }

    function median(sorted) {
        return sorted[Math.floor(sorted.length / 2)];
    }

    function test_thirtyTilesAndTheirWiresCostNoMoreThanAnEmptyCanvas() {
        if (Qt.platform.pluginName === "offscreen" || Qt.platform.pluginName === "minimal") {
            skip("There is no compositor to measure against; run this from a Plasma Wayland session.");
        }

        const window = createTemporaryObject(frameWindow, null);
        verify(window, "there is a window to draw in");
        const canvas = window.canvas;
        waitForRendering(canvas);

        // The same window, with nothing on it: the cadence everything else
        // is measured against.
        const empty = testCase.median(testCase.frameTimes(canvas));

        // Six rows of five, which is the shape a session of this size takes
        // and which puts every tile inside the window rather than where Qt
        // would cull it.
        const added = [];
        for (let i = 0; i < testCase.tileCount; ++i) {
            const typeId = i % 3 === 0 ? "meter.peakrms" : "effect.volume";
            added.push(SessionModel.addBlock(typeId, Qt.point((i % 5) * 200 + 40, Math.floor(i / 5) * 120 + 40)));
        }
        tryVerify(() => canvas.blocks.count === testCase.tileCount, 2000, "every tile is on the canvas");

        // **Wired across the rows, not down them**, so that most of the
        // wires run the width of the window rather than between two tiles
        // sitting next to each other: a long curve is what costs something
        // to draw, and a session laid out in rows is full of them.
        for (let i = 0; i + 1 < added.length; ++i) {
            SessionModel.connectBlocks(added[i], 0, added[i + 1], 0);
            SessionModel.connectBlocks(added[i], 1, added[i + 1], 1);
        }
        tryVerify(() => canvas.wires.count === testCase.wireCount, 2000, "every wire is on the canvas");
        canvas.fitToView();
        waitForRendering(canvas);

        const loaded = testCase.median(testCase.frameTimes(canvas));
        const allowed = empty * testCase.allowedOverhead;
        console.info("canvas frames: empty " + empty.toFixed(2) + " ms, " + testCase.tileCount
                     + " tiles and " + testCase.wireCount + " wires " + loaded.toFixed(2)
                     + " ms (allowed " + allowed.toFixed(2) + " ms)");

        verify(loaded <= allowed,
               testCase.tileCount + " tiles and " + testCase.wireCount + " wires took "
               + loaded.toFixed(2) + " ms a frame against " + empty.toFixed(2)
               + " ms for an empty canvas");

        // **And the same scene with the tiles moving**, which is the harder
        // half of drawing wires: a wire is drawn between two tiles that both
        // move, so every frame of a drag is every wire worked out again
        // from both of its ends.
        canvas.selectedIds = SessionModel.blockIds;
        const dragged = testCase.median(testCase.frameTimes(canvas, true));
        canvas.cancelDrag();
        canvas.clearSelection();
        console.info("canvas frames: every tile dragged " + dragged.toFixed(2) + " ms (allowed "
                     + allowed.toFixed(2) + " ms)");

        verify(dragged <= allowed,
               "dragging every tile with " + testCase.wireCount + " wires on them took "
               + dragged.toFixed(2) + " ms a frame against " + empty.toFixed(2)
               + " ms for an empty canvas");

        // **And the same scene alive**, which is the one expensive thing
        // the canvas draws: every wire dashed and every dash moving.
        // Nothing here is a different scene — the same tiles, the same
        // wires, the same pan — so what the figures differ by is the dash.
        canvas.zoom = 1.0;
        Meters.watch(testCase.sessionId);
        TestEngine.start(testCase.sessionId);
        tryVerify(() => Meters.live, 2000, "the session published nothing to be live from");
        for (const block of added) {
            TestEngine.setFakeLevel(testCase.sessionId, block, 0.5);
        }
        tryVerify(() => canvas.wireIsLive(added[0]), 1000, "no wire went live");
        // The dash only moves with the window in front and the view in
        // close; a figure measured with it stopped would be the figure
        // above under another name.
        tryVerify(() => canvas.flowing, 2000, "the window never came to the front, so no dash moved");
        compare(canvas.wireShapes.itemAt(0).live, true);
        waitForRendering(canvas);

        const live = testCase.median(testCase.frameTimes(canvas));
        const allowedLive = empty * testCase.allowedDashOverhead;
        console.info("canvas frames: " + testCase.wireCount + " live wires with the dash moving "
                     + live.toFixed(2) + " ms (allowed " + allowedLive.toFixed(2) + " ms)");

        TestEngine.stop(testCase.sessionId);
        Meters.watch("");

        verify(live <= allowedLive,
               testCase.wireCount + " live wires with the dash moving took " + live.toFixed(2)
               + " ms a frame against " + empty.toFixed(2) + " ms for an empty canvas");
    }
}
