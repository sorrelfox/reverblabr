// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The 10-Band EQ's face and panel: ten bars that are the shape of the
// setting, ten sliders that write their own bands, and Flatten as one step
// to take back.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "Equalizer10"

    property string sessionId: ""

    Component {
        id: faceComponent

        EqualizerFace {
            width: AppMetrics.tileOuterWidth - 2
            height: AppMetrics.tileOuterHeight - AppMetrics.tileBandHeight - 2
        }
    }

    Component {
        id: panelComponent

        EqualizerPanel {
            width: 400
        }
    }

    // A panel in a window of its own, so its layouts are laid out.
    Component {
        id: shownPanel

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

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("EQ");
        verify(testCase.sessionId !== "", "the mock opened a session");
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "eq", type: "effect.equalizer10" }, index: -1 }));
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        const flat = { preamp: 0, keepLevel: false };
        for (const id of ["gain31", "gain63", "gain125", "gain250", "gain500", "gain1k", "gain2k", "gain4k", "gain8k",
                          "gain16k"]) {
            flat[id] = 0;
        }
        for (const id in flat) {
            SessionModel.setParam("eq", id, flat[id]);
        }
    }

    function test_itIsAnEffectWithAFaceAndAPanelOfItsOwn() {
        verify(BlockCatalogue.types.indexOf("effect.equalizer10") >= 0, "the engine does not offer the 10-Band EQ");
        compare(BlockPresentation.category("effect.equalizer10"), "effects");
        compare(BlockPresentation.name("effect.equalizer10"), "10-Band EQ");
        verify(BlockFaces.faceFor("effect.equalizer10") !== null, "the 10-Band EQ has no face");
        verify(BlockPanels.panelFor("effect.equalizer10") !== null, "the 10-Band EQ has no panel");
    }

    function test_theBandsAreTheOnesTheEngineOffers() {
        // The ids and the range are written down twice; this is what stops
        // them drifting apart.
        const panel = createTemporaryObject(panelComponent, null);
        for (const id of panel.bands.ids) {
            const described = BlockCatalogue.describeParam("effect.equalizer10", id);
            verify(described !== undefined && described.type === "float", id + " is not one of the engine's bands");
            compare(described.minimum, -panel.bands.rangeDb, id);
            compare(described.maximum, panel.bands.rangeDb, id);
        }
        compare(BlockCatalogue.describeParam("effect.equalizer10", "preamp").maximum, panel.bands.rangeDb);
        compare(panel.bands.frequencies.length, panel.bands.ids.length);
    }

    function test_bandsAreLabelledShort() {
        const words = createTemporaryObject(panelComponent, null).words;
        compare([31.5, 63, 125, 1000, 16000].map(hz => words.bandLabel(hz)), ["31", "63", "125", "1k", "16k"]);
        compare([31.5, 1000, 16000].map(hz => words.bandText(hz)), ["31 Hz", "1 kHz", "16 kHz"]);
    }

    function test_theBarsAreTheShapeOfTheSetting() {
        const face = createTemporaryObject(faceComponent, null);
        face.blockId = "eq";
        tryVerify(() => face.flat, 1000);
        const heights = () => {
            const all = [];
            for (let index = 0; index < face.bars.children.length; ++index) {
                const child = face.bars.children[index];
                if (child.width === 6) {
                    all.push(child.height);
                }
            }
            return all;
        };
        compare(heights().length, 10);
        // Flat is a level row, half way up.
        verify(heights().every(height => height === heights()[0]), "a flat EQ is not a level row: " + heights());
        compare(face.detail, "Flat");

        SessionModel.setParam("eq", "gain31", 12);
        SessionModel.setParam("eq", "gain16k", -12);
        tryVerify(() => heights()[0] === face.tallest, 1000, "a band at +12 dB is not a full bar: " + heights());
        compare(heights()[9], face.shortest, "a band at -12 dB is not the stub");
        verify(heights()[0] > heights()[4] && heights()[4] > heights()[9], "the bars are not in the order of the gains");
        compare(face.detail, "31 Hz +12.0 dB, 16 kHz -12.0 dB");
        // And the bars stay inside the face a tile gives them.
        verify(face.bars.y >= 0 && face.bars.y + face.bars.height <= face.height + 0.5,
               "the bars stand out of the face: " + face.bars.y + " + " + face.bars.height + " in " + face.height);
        verify(face.bars.width <= face.width, "ten bars are wider than a tile");
    }

    function test_eachSliderWritesItsOwnBand() {
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "eq";
        compare(panel.bandSliders.count, 10);
        for (let index = 0; index < 10; ++index) {
            const slider = panel.bandSliders.itemAt(index).slider;
            slider.value = index - 4.5;
            slider.moved();
            SessionModel.commitParam("eq", panel.bands.ids[index]);
        }
        for (let index = 0; index < 10; ++index) {
            compare(SessionModel.param("eq", panel.bands.ids[index]), index - 4.5, panel.bands.ids[index]);
        }
        compare(panel.bandSliders.itemAt(0).readout.text, "-4.5");
        compare(panel.bandSliders.itemAt(5).readout.text, "+0.5");
        panel.preampSlider.value = -6;
        panel.preampSlider.moved();
        SessionModel.commitParam("eq", "preamp");
        compare(SessionModel.param("eq", "preamp"), -6);
        panel.keepLevelBox.toggle();
        panel.keepLevelBox.toggled();
        compare(SessionModel.param("eq", "keepLevel"), true);
    }

    function test_flattenPutsEveryBandBackAsOneStep() {
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "eq";
        tryVerify(() => !panel.flattenButton.enabled, 1000, "Flatten is offered on a flat EQ");
        SessionModel.setParam("eq", "gain63", 6);
        SessionModel.setParam("eq", "gain4k", -3);
        SessionModel.setParam("eq", "preamp", -2);
        tryVerify(() => panel.flattenButton.enabled, 1000);

        panel.flattenButton.clicked();
        tryVerify(() => SessionModel.param("eq", "gain63") === 0, 2000, "Flatten did not reach the session");
        compare(SessionModel.param("eq", "gain4k"), 0);
        compare(SessionModel.param("eq", "preamp"), 0);
        tryVerify(() => !panel.flattenButton.enabled, 1000);

        // **One step back** brings the whole shape back, not one band of it.
        SessionModel.undo();
        tryVerify(() => SessionModel.param("eq", "gain63") === 6, 2000, "undo did not bring the bands back");
        compare(SessionModel.param("eq", "gain4k"), -3);
        compare(SessionModel.param("eq", "preamp"), -2);
    }

    function test_theScaleIsLevelWithTheSliders() {
        // +12 beside a handle at +12, 0 beside one at 0 and -12 beside one
        // at -12, so a setting can be read off the scale.
        const window = createTemporaryObject(shownPanel, null);
        const panel = window.panel;
        panel.blockId = "eq";
        const slider = panel.bandSliders.itemAt(0).slider;
        tryVerify(() => slider.availableHeight > 100, 1000, "the sliders have no travel to speak of");
        const handleAt = value => {
            slider.value = value;
            return slider.handle.mapToItem(panel, 0, slider.handle.height / 2).y;
        };
        const labelAt = index => {
            const label = panel.scaleLabels.itemAt(index);
            return label.mapToItem(panel, 0, label.height / 2).y;
        };
        const marks = [12, 6, 0, -6, -12];
        for (let index = 0; index < marks.length; ++index) {
            const handle = handleAt(marks[index]);
            verify(Math.abs(labelAt(index) - handle) <= 2,
                   "the scale's " + marks[index] + " is at " + labelAt(index) + ", a handle there at " + handle);
        }
    }
}
