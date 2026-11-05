// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Low Pass Filter's face and panel: the cutoff said the way a
// filter's is, on a logarithmic slider the face and the panel share, and
// the slope in words.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "LowPassFilter"

    property string sessionId: ""

    Component {
        id: faceComponent

        CutoffFace {
            typeId: "effect.lowpass"
            width: 220
            height: 120
        }
    }

    Component {
        id: panelComponent

        CutoffPanel {
            typeId: "effect.lowpass"
            width: 320
        }
    }

    // A panel in a window of its own, for the tests that press on it: an
    // item with no window is never visible and takes no pointer events.
    Component {
        id: shownPanel

        Window {
            property alias panel: inner

            width: 400
            height: 300
            visible: true

            CutoffPanel {
                typeId: "effect.lowpass"
                id: inner

                width: 320
            }
        }
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Low Pass");
        verify(testCase.sessionId !== "", "the mock opened a session");
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "lpf", type: "effect.lowpass" }, index: -1 }));
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        SessionModel.setParam("lpf", "cutoff", 20000);
        SessionModel.setParam("lpf", "slope", "12dB");
    }

    function test_itIsAnEffectWithAFaceAndAPanelOfItsOwn() {
        verify(BlockCatalogue.types.indexOf("effect.lowpass") >= 0, "the engine does not offer the Low Pass Filter");
        compare(BlockPresentation.category("effect.lowpass"), "effects");
        compare(BlockPresentation.name("effect.lowpass"), "Low Pass Filter");
        verify(BlockFaces.faceFor("effect.lowpass") !== null, "the Low Pass Filter has no face");
        verify(BlockPanels.panelFor("effect.lowpass") !== null, "the Low Pass Filter has no panel");
    }

    function test_theIdsAndTheRangeAreTheOnesTheEngineOffers() {
        // The interface does not depend on the engine's code, so the ids and
        // the ends of the range are written down twice; this is what stops
        // them drifting apart.
        const panel = createTemporaryObject(panelComponent, null);
        compare(panel.words.slopes, BlockCatalogue.describeParam("effect.lowpass", "slope").choices);
        const cutoff = BlockCatalogue.describeParam("effect.lowpass", "cutoff");
        compare(panel.lowestHz, cutoff.minimum);
        compare(panel.highestHz, cutoff.maximum);
        for (const slope of panel.words.slopes) {
            verify(panel.words.slopeName(slope) !== slope, slope + " has no word");
        }
    }

    function test_aCutoffIsSaidToThreeFigures() {
        const panel = createTemporaryObject(panelComponent, null);
        const words = panel.words;
        compare(words.frequencyText(20000), "20.0 kHz");
        compare(words.frequencyText(8000), "8.00 kHz");
        compare(words.frequencyText(1250), "1.25 kHz");
        compare(words.frequencyText(200), "200 Hz");
        compare(words.frequencyText(456.4), "456 Hz");
        compare(words.parseFrequency("8k"), 8000);
        compare(words.parseFrequency("3.5 kHz"), 3500);
        compare(words.parseFrequency("900"), 900);
        verify(isNaN(words.parseFrequency("bright")), "a word was read as a frequency");
    }

    function test_theFaceSaysWhereTheCutoffIs() {
        const face = createTemporaryObject(faceComponent, null);
        face.blockId = "lpf";
        tryVerify(() => face.headlineLabel.text === "20.0 kHz", 1000, "the face does not start at 20.0 kHz");
        compare(face.detail, "Cutoff 20.0 kHz, 12 dB per octave");

        SessionModel.setParam("lpf", "cutoff", 4000);
        SessionModel.setParam("lpf", "slope", "24dB");
        tryVerify(() => face.headlineLabel.text === "4.00 kHz", 1000);
        compare(face.detail, "Cutoff 4.00 kHz, 24 dB per octave");
    }

    Component {
        id: tileSizedFace

        CutoffFace {
            typeId: "effect.lowpass"
            // The face as a tile holds it: the tile's width inside its
            // one-pixel border.
            width: AppMetrics.tileOuterWidth - 2
            height: 80
        }
    }

    function test_theWholeCutoffFitsOnATile() {
        // The first cut put the slope beside the cutoff, and a tile showed
        // "8.00 k…": the one number the face is for, cut short.
        const face = createTemporaryObject(tileSizedFace, null);
        face.blockId = "lpf";
        for (const hz of [20000, 8000, 1250, 200]) {
            SessionModel.setParam("lpf", "cutoff", hz);
            tryVerify(() => face.headlineLabel.text === face.words.frequencyText(hz), 1000);
            verify(!face.headlineLabel.truncated, face.headlineLabel.text + " does not fit on a tile");
        }
    }

    function test_theFacesSliderMovesTheCutoff() {
        const face = createTemporaryObject(faceComponent, null);
        face.blockId = "lpf";
        tryVerify(() => Math.abs(face.slider.value - 1) < 1e-6, 1000, "the slider does not start at the top");
        // Half way along a logarithmic slider from 200 Hz to 20 kHz is 2 kHz.
        face.slider.value = 0.5;
        face.slider.moved();
        SessionModel.commitParam("lpf", "cutoff");
        verify(Math.abs(SessionModel.param("lpf", "cutoff") - 2000) < 0.5,
               "half way along the slider set " + SessionModel.param("lpf", "cutoff"));
        compare(face.headlineLabel.text, "2.00 kHz");
    }

    function test_thePanelWritesWhatItsControlsSay() {
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "lpf";
        tryVerify(() => panel.cutoffField.text === "20.0 kHz", 1000);

        // A cutoff typed exactly, clamped to what the block allows.
        panel.cutoffField.text = "8k";
        panel.cutoffField.editingFinished();
        compare(SessionModel.param("lpf", "cutoff"), 8000);
        panel.cutoffField.text = "50";
        panel.cutoffField.editingFinished();
        compare(SessionModel.param("lpf", "cutoff"), 200);
        compare(panel.cutoffField.text, "200 Hz");

        // **The slider is logarithmic**: an octave is the same distance
        // wherever it is.
        const words = panel.words;
        const octaveLow = words.positionOf(800, 200, 20000) - words.positionOf(400, 200, 20000);
        const octaveHigh = words.positionOf(16000, 200, 20000) - words.positionOf(8000, 200, 20000);
        verify(Math.abs(octaveLow - octaveHigh) < 1e-9, "an octave is not an octave along the slider");
        panel.cutoffSlider.value = words.positionOf(1000, 200, 20000);
        panel.cutoffSlider.moved();
        SessionModel.commitParam("lpf", "cutoff");
        verify(Math.abs(SessionModel.param("lpf", "cutoff") - 1000) < 0.01, "the slider did not set 1 kHz");
    }

    function test_aSliderStaysWhereItWasLeft() {
        // A click on the track is one value, sent at once, and the
        // handle stays where the click put it.
        const window = createTemporaryObject(shownPanel, null);
        const panel = window.panel;
        panel.blockId = "lpf";
        const slider = panel.cutoffSlider;
        tryVerify(() => slider.width > 0 && slider.availableWidth > 0, 1000, "the slider was never laid out");
        mouseClick(slider, slider.leftPadding + slider.availableWidth * 0.25, slider.height / 2);
        const sent = SessionModel.param("lpf", "cutoff");
        verify(sent < 1000, "the click did not reach the session: " + sent);
        wait(100);
        verify(Math.abs(slider.value - panel.words.positionOf(sent, 200, 20000)) < 0.001,
               "the slider went back to " + slider.value + " with the session at " + sent);
    }

    function test_theSlopeIsChosenByItsButtons() {
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "lpf";
        const buttons = [];
        for (const child of panel.slopeButtons.children) {
            if (child.checkable === true) {
                buttons.push(child);
            }
        }
        compare(buttons.map(button => button.text), ["12 dB/oct", "24 dB/oct"]);
        compare(buttons.map(button => button.Accessible.name), ["12 dB per octave", "24 dB per octave"]);
        tryVerify(() => buttons[0].checked, 1000, "12 dB is not the slope shown to start with");
        buttons[1].clicked();
        compare(SessionModel.param("lpf", "slope"), "24dB");
        tryVerify(() => buttons[1].checked && !buttons[0].checked, 1000);
        // The chosen slope is the filled one.
        verify(buttons[1].highlighted && !buttons[0].highlighted, "the fill did not follow the slope");
    }
}
