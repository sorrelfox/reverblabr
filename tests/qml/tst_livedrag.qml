// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What a setting changes on screen changes while its control is still being
// dragged, not when the button comes up: a curve, a tile's bars, a band on a
// plot. Every drag here holds the button down while it looks, which is the
// whole point; a test that let go first would pass whether or not anything
// followed. And however many ticks a drag lasts, it is one step to undo.

import QtQuick
import QtQuick.Controls
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "LiveDrag"
    when: windowShown

    property string sessionId: ""

    Component {
        id: equalizerWindow

        Window {
            property alias panel: inner

            width: 440
            height: 420
            visible: true

            EqualizerPanel {
                id: inner

                width: 400
            }
        }
    }

    Component {
        id: equalizerFace

        EqualizerFace {
            width: AppMetrics.tileOuterWidth - 2
            height: AppMetrics.tileOuterHeight - AppMetrics.tileBandHeight - 2
        }
    }

    // Tall enough that the rows under Details are in the window, so the
    // pointer can reach them without a scroll view in between.
    Component {
        id: dynamicsWindow

        Window {
            property alias panel: inner

            width: 380
            height: 1400
            visible: true

            DynamicsPanel {
                id: inner

                width: 360
            }
        }
    }

    Component {
        id: dynamicsFace

        DynamicsFace {
            width: 180
            height: 96
        }
    }

    Component {
        id: parametricWindow

        Window {
            property alias panel: inner

            width: 460
            height: 700
            visible: true

            ParametricEqPanel {
                id: inner

                width: 420
            }
        }
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Live drag");
        verify(testCase.sessionId !== "", "the mock opened a session");
        for (const block of [{ id: "eq", type: "effect.equalizer10" }, { id: "dyn", type: "effect.dynamics" },
                             { id: "peq", type: "advanced.parametriceq" }]) {
            TestEngine.applyEdit(testCase.sessionId, JSON.stringify({ op: "addBlock", block: block, index: -1 }));
        }
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
        tryVerify(() => SessionModel.blockType("peq") === "advanced.parametriceq", 1000, "the blocks were not added");
    }

    function init() {
        for (const typeId of ["effect.equalizer10", "effect.dynamics"]) {
            const blockId = typeId === "effect.dynamics" ? "dyn" : "eq";
            for (const descriptor of BlockCatalogue.params(typeId)) {
                SessionModel.setParam(blockId, descriptor.id, descriptor.default);
            }
        }
    }

    /// The middle of \a control's handle, in \a window's coordinates.
    function handleCentre(window: var, control: var): point {
        const handle = control.handle;
        return handle.mapToItem(window.contentItem, handle.width / 2, handle.height / 2);
    }

    /// Moves the pointer from \a from by (\a dx, \a dy) in \a steps, **with
    /// the button held**, as a hand does.
    function dragBy(window: var, from: point, dx: real, dy: real, steps: int) {
        for (let step = 1; step <= steps; ++step) {
            mouseMove(window.contentItem, from.x + dx * step / steps, from.y + dy * step / steps, 10, Qt.LeftButton);
        }
    }

    function test_theTilesBarsFollowABandWhileItIsDragged() {
        const window = createTemporaryObject(equalizerWindow, null);
        window.panel.blockId = "eq";
        const face = createTemporaryObject(equalizerFace, null);
        face.blockId = "eq";
        waitForRendering(window.panel);
        compare(face.detail, "Flat");

        const slider = window.panel.bandSliders.itemAt(5).slider;
        const from = testCase.handleCentre(window, slider);
        mousePress(window.contentItem, from.x, from.y);
        testCase.dragBy(window, from, 0, -60, 6);

        verify(slider.pressed, "the drag let go of the slider");
        verify(slider.value > 2, "the slider did not follow the pointer: " + slider.value);
        // The tile reads the session, not the slider, and it has heard.
        tryVerify(() => face.gains[5] > 2, 500,
                  "the tile's bar waited for the button to come up: " + face.gains[5]);
        tryVerify(() => Math.abs(face.gains[5] - slider.value) < 0.01, 500,
                  "the tile was a tick behind the handle and stayed there");

        mouseRelease(window.contentItem, from.x, from.y - 60);
        tryVerify(() => Math.abs(SessionModel.param("eq", "gain1k") - slider.value) < 0.01, 500);
    }

    function test_aCurveFollowsARowUnderDetailsWhileItIsDragged() {
        // **The shared row**, which every generic panel and the Details of
        // the dynamics blocks are made of.
        const window = createTemporaryObject(dynamicsWindow, null);
        const panel = window.panel;
        panel.blockId = "dyn";
        panel.detailsButton.toggle();
        const face = createTemporaryObject(dynamicsFace, null);
        face.blockId = "dyn";
        waitForRendering(panel);

        let row = null;
        for (let index = 0; index < panel.detailRows.count; ++index) {
            if (panel.detailRows.itemAt(index).paramId === "threshold") {
                row = panel.detailRows.itemAt(index);
            }
        }
        verify(row !== null, "Details has no row for the threshold");
        const slider = row.activeControl.fader;
        const before = face.params.threshold ?? -20;

        const from = testCase.handleCentre(window, slider);
        mousePress(window.contentItem, from.x, from.y);
        testCase.dragBy(window, from, 60, 0, 6);
        verify(slider.pressed, "the drag let go of the slider");

        tryVerify(() => (face.params.threshold ?? -20) > before + 1, 500,
                  "the tile's curve waited for the button to come up");
        tryVerify(() => (panel.curve.params.threshold ?? -20) > before + 1, 500,
                  "the panel's own curve waited for the button to come up");

        mouseRelease(window.contentItem, from.x + 60, from.y);
    }

    function test_aBandOnThePlotFollowsThePointerWhileItIsDragged() {
        const window = createTemporaryObject(parametricWindow, null);
        const panel = window.panel;
        panel.blockId = "peq";
        tryVerify(() => panel.plotArea.width > 0 && panel.plot.curve.length > 0, 2000, "the plot was never laid out");

        const point = panel.handlePoint(1);
        const from = panel.plotArea.mapToItem(window.contentItem, point.x, point.y);
        const drawn = panel.plot.tessellations;
        mousePress(window.contentItem, from.x, from.y);
        testCase.dragBy(window, from, 0, -40, 5);

        tryVerify(() => (panel.plot.params.band2Gain ?? 0) > 3, 500,
                  "the band's curve waited for the button to come up");
        verify(panel.plot.tessellations > drawn, "the line was not drawn again");

        mouseRelease(window.contentItem, from.x, from.y - 40);
    }

    function test_oneDragIsOneStepToUndo() {
        SessionModel.setParam("eq", "gain1k", 3);
        // Another kind of edit after it, so that the drag cannot join it.
        SessionModel.setParam("eq", "keepLevel", true);

        const window = createTemporaryObject(equalizerWindow, null);
        window.panel.blockId = "eq";
        waitForRendering(window.panel);
        const slider = window.panel.bandSliders.itemAt(5).slider;
        tryCompare(slider, "value", 3);

        const from = testCase.handleCentre(window, slider);
        mousePress(window.contentItem, from.x, from.y);
        // Long enough for several ticks to go to the engine.
        for (let step = 1; step <= 8; ++step) {
            mouseMove(window.contentItem, from.x, from.y - step * 8, 0, Qt.LeftButton);
            wait(20);
        }
        mouseRelease(window.contentItem, from.x, from.y - 64);
        tryVerify(() => SessionModel.param("eq", "gain1k") > 6, 500, "the drag did not land");

        SessionModel.undo();
        tryVerify(() => SessionModel.param("eq", "gain1k") === 3, 500,
                  "undo took back only part of the drag: " + SessionModel.param("eq", "gain1k"));
        compare(SessionModel.param("eq", "keepLevel"), true, "the edit before the drag went with it");
    }
}
