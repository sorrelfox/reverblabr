// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import io.github.reverblabr

// Sets up the scenes bench_canvas.cpp measures, one after another: the
// canvas with nothing on it, then the tiles and their wires with the view
// panning, then the same with every meter moving and every wire's dash
// running. The view moves a pixel each frame throughout, so that nothing is
// drawn once and left alone.
Window {
    id: bench

    property string sessionId: ""
    property var added: []
    property int stage: 0

    /// Seven tiles to a row, 200 apart, at the zoom the dash runs at: the
    /// window is made big enough for all of them, because a tile Qt culls
    /// costs nothing and would flatter the figure.
    readonly property int columns: 7

    width: columns * 200 + 80
    height: Math.ceil(TileCount / columns) * 120 + 80
    visible: true

    SessionCanvas {
        id: canvas

        anchors.fill: parent
    }

    FrameAnimation {
        running: true
        onTriggered: canvas.originX += (currentFrame % 2 === 0) ? 1 : -1
    }

    Connections {
        function onMeasured(name: string) {
            bench.next();
        }

        target: Bench
    }

    // The engine and the model answer on the event loop, so each scene is
    // waited for rather than assumed.
    Timer {
        id: waiter

        property var ready: null
        property var then: null
        property int tries: 0

        interval: 20
        repeat: true
        onTriggered: {
            if (ready()) {
                stop();
                then();
            } else if (++tries > 500) {
                stop();
                Bench.fail("a scene was never ready: stage " + bench.stage + ", " + canvas.blocks.count + " tiles, "
                           + canvas.wires.count + " wires");
            }
        }
    }

    function waitFor(ready: var, then: var) {
        waiter.ready = ready;
        waiter.then = then;
        waiter.tries = 0;
        waiter.start();
    }

    function next() {
        ++stage;
        if (stage === 1) {
            Bench.measure(bench, "empty", FrameCount, "the window with nothing on the canvas");
        } else if (stage === 2) {
            // Peak/RMS meters and volumes, which is most of what a session
            // is, each wired to the next on both channels.
            for (let i = 0; i < TileCount; ++i) {
                const typeId = i % 3 === 0 ? "meter.peakrms" : "effect.volume";
                added.push(SessionModel.addBlock(typeId, Qt.point((i % columns) * 200 + 40,
                                                                  Math.floor(i / columns) * 120 + 40)));
            }
            waitFor(() => canvas.blocks.count === TileCount, function () {
                for (let i = 0; i + 1 < added.length; ++i) {
                    SessionModel.connectBlocks(added[i], 0, added[i + 1], 0);
                    SessionModel.connectBlocks(added[i], 1, added[i + 1], 1);
                }
                // A stereo pair of links is drawn as one wire.
                waitFor(() => canvas.wires.count >= TileCount - 1, function () {
                    canvas.zoom = 1.0;
                    Bench.measure(bench, TileCount + "-tiles", FrameCount,
                                  TileCount + " tiles and " + canvas.wires.count + " wires, the view panning");
                });
            });
        } else if (stage === 3) {
            // **Alive**: a running session with a level on every block, so
            // the meters move and the wires carry their dash.
            Meters.watch(sessionId);
            BenchEngine.start(sessionId);
            for (const block of added) {
                BenchEngine.setFakeLevel(sessionId, block, 0.5);
            }
            bench.requestActivate();
            waitFor(() => Meters.live && canvas.wireIsLive(added[0]), function () {
                const detail = TileCount + " tiles with their meters moving and "
                    + canvas.wires.count + " live wires, " + (canvas.flowing ? "the dash running"
                                                                              : "the dash stopped (the window is not active)");
                Bench.measure(bench, TileCount + "-tiles-live", FrameCount, detail);
            });
        } else {
            BenchEngine.stop(sessionId);
            Meters.watch("");
            Bench.finish();
        }
    }

    Component.onCompleted: {
        BenchEngine.sessionOpened.connect(function (id) {
            bench.sessionId = id;
        });
        BenchEngine.createSession("Bench");
        SessionModel.sessionId = bench.sessionId;
        waitFor(() => BlockCatalogue.ready && bench.sessionId !== "", () => bench.next());
    }
}
