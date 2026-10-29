// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Wave block's face and panel: words where the engine has ids,
// the frequency said the way a person says it, and every control writing
// the setting it names.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "Wave"

    property string sessionId: ""

    Component {
        id: faceComponent

        WaveFace {
            width: 220
            height: 120
        }
    }

    Component {
        id: panelComponent

        WavePanel {
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
            height: 400
            visible: true

            WavePanel {
                id: inner

                width: 320
            }
        }
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Wave");
        verify(testCase.sessionId !== "", "the mock opened a session");
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "wav", type: "output.wave" }, index: -1 }));
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        SessionModel.setParam("wav", "waveform", "sine");
        SessionModel.setParam("wav", "frequency", 1000);
        SessionModel.setParam("wav", "level", -18);
        SessionModel.setParam("wav", "channels", "both");
    }

    function test_itIsInTheOutputsWithAFaceAndAPanelOfItsOwn() {
        verify(BlockCatalogue.types.indexOf("output.wave") >= 0, "the engine does not offer Wave");
        compare(BlockPresentation.category("output.wave"), "outputs");
        compare(BlockPresentation.name("output.wave"), "Wave");
        verify(BlockFaces.faceFor("output.wave") !== null, "Wave has no face");
        verify(BlockPanels.panelFor("output.wave") !== null, "Wave has no panel");
    }

    function test_theIdsTheInterfaceSpellsOutAreTheOnesTheEngineOffers() {
        // The interface does not depend on the engine's code, so the ids are
        // written down twice; this is what stops them drifting apart.
        const panel = createTemporaryObject(panelComponent, null);
        compare(panel.words.shapes, BlockCatalogue.describeParam("output.wave", "waveform").choices);
        const sides = BlockCatalogue.describeParam("output.wave", "channels").choices;
        compare(panel.words.sides.slice().sort(), sides.slice().sort());
        // Every one has a word, not its id.
        for (const shape of panel.words.shapes) {
            verify(panel.words.shapeName(shape) !== shape, shape + " has no word");
        }
    }

    function test_theFaceSaysWhatItIsMaking() {
        const face = createTemporaryObject(faceComponent, null);
        face.blockId = "wav";
        wait(50);
        compare(face.headlineLabel.text, "1 kHz");
        compare(face.captionLabel.text, "sine");
        compare(face.detail, "Sine, 1 kHz, -18.0 dB, both");

        SessionModel.setParam("wav", "frequency", 440);
        SessionModel.setParam("wav", "channels", "left");
        tryVerify(() => face.headlineLabel.text === "440 Hz", 1000);
        compare(face.captionLabel.text, "sine · left");

        // Noise has no frequency to lead with.
        SessionModel.setParam("wav", "waveform", "pink");
        tryVerify(() => face.headlineLabel.text === "Noise", 1000);
        compare(face.captionLabel.text, "pink · left");
    }

    function test_frequenciesAreSaidTheWayAPersonSaysThem() {
        const panel = createTemporaryObject(panelComponent, null);
        const words = panel.words;
        compare(words.frequencyText(440), "440 Hz");
        compare(words.frequencyText(1000), "1 kHz");
        compare(words.frequencyText(12500), "12.5 kHz");
        compare(words.frequencyText(1234), "1.23 kHz");
        // And typed the same way.
        compare(panel.parseFrequency("440"), 440);
        compare(panel.parseFrequency("1k"), 1000);
        compare(panel.parseFrequency("1.5 kHz"), 1500);
        verify(isNaN(panel.parseFrequency("loud")), "a word was read as a frequency");
    }

    function test_thePanelWritesWhatItsControlsSay() {
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "wav";
        wait(50);

        // The waveform, by its stable id.
        panel.shapeBox.currentIndex = panel.words.shapes.indexOf("triangle");
        panel.shapeBox.activated(panel.shapeBox.currentIndex);
        compare(SessionModel.param("wav", "waveform"), "triangle");

        // A frequency typed exactly, clamped to what the block can make.
        panel.frequencyField.text = "2.5k";
        panel.frequencyField.editingFinished();
        compare(SessionModel.param("wav", "frequency"), 2500);
        panel.frequencyField.text = "90000";
        panel.frequencyField.editingFinished();
        compare(SessionModel.param("wav", "frequency"), 20000);

        // **The slider is logarithmic**: an octave is the same distance
        // wherever it is.
        const octaveLow = panel.positionOf(200) - panel.positionOf(100);
        const octaveHigh = panel.positionOf(8000) - panel.positionOf(4000);
        verify(Math.abs(octaveLow - octaveHigh) < 1e-9, "an octave is not an octave along the slider");
        panel.frequencySlider.value = panel.positionOf(100);
        panel.frequencySlider.moved();
        SessionModel.commitParam("wav", "frequency");
        verify(Math.abs(SessionModel.param("wav", "frequency") - 100) < 0.01, "the slider did not set 100 Hz");

        // The level.
        panel.levelSlider.value = -6;
        panel.levelSlider.moved();
        SessionModel.commitParam("wav", "level");
        compare(SessionModel.param("wav", "level"), -6);
    }

    function test_aSliderStaysWhereItWasLeft() {
        // A click on the track is one value, sent at once, so nothing
        // is left to send when the button comes up. The handle stayed only
        // until the session said what it thought, which was the old value.
        const window = createTemporaryObject(shownPanel, null);
        const panel = window.panel;
        panel.blockId = "wav";
        waitForRendering(panel);

        const level = panel.levelSlider;
        mouseClick(level, level.leftPadding + level.availableWidth * 0.25, level.height / 2);
        const levelSent = SessionModel.param("wav", "level");
        verify(levelSent < -40, "the click did not reach the session: " + levelSent);
        wait(100);
        verify(Math.abs(level.value - levelSent) < 0.01,
               "the level slider went back to " + level.value + " with the session at " + levelSent);

        const frequency = panel.frequencySlider;
        mouseClick(frequency, frequency.leftPadding + frequency.availableWidth * 0.8, frequency.height / 2);
        const frequencySent = SessionModel.param("wav", "frequency");
        verify(frequencySent > 3000, "the click did not reach the session: " + frequencySent);
        wait(100);
        verify(Math.abs(frequency.value - panel.positionOf(frequencySent)) < 0.001,
               "the frequency slider went back to " + frequency.value);
    }

    function test_theSideIsChosenByItsButtons() {
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "wav";
        wait(50);
        const buttons = [];
        for (const child of panel.sideButtons.children) {
            if (child.checkable === true) {
                buttons.push(child);
            }
        }
        compare(buttons.map(button => button.text), ["Left", "Both", "Right"]);
        verify(buttons[1].checked, "both is not the side shown to start with");
        // The chosen side is filled, in the desktop's look too.
        verify(buttons[1].highlighted && !buttons[0].highlighted, "the chosen side is not the filled one");
        buttons[2].clicked();
        compare(SessionModel.param("wav", "channels"), "right");
        tryVerify(() => buttons[2].checked && !buttons[1].checked, 1000);
        verify(buttons[2].highlighted && !buttons[1].highlighted, "the fill did not follow the side");
    }

    function test_theFrequencyIsForTonesOnly() {
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "wav";
        SessionModel.setParam("wav", "waveform", "white");
        tryVerify(() => !panel.frequencySlider.parent.enabled, 1000, "the frequency is offered for noise");
        SessionModel.setParam("wav", "waveform", "square");
        tryVerify(() => panel.frequencySlider.parent.enabled, 1000);
    }
}
