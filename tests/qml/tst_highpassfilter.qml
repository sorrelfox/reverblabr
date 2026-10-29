// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The High Pass Filter's face and panel: the Low Pass Filter's
// (tst_lowpassfilter.qml tests the words and the controls), with the range
// and the start turned round — which they read from the engine, and which
// is what this holds them to.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "HighPassFilter"

    property string sessionId: ""

    Component {
        id: faceComponent

        CutoffFace {
            typeId: "effect.highpass"
            width: 220
            height: 120
        }
    }

    Component {
        id: tileSizedFace

        CutoffFace {
            typeId: "effect.highpass"
            width: AppMetrics.tileOuterWidth - 2
            height: 80
        }
    }

    Component {
        id: panelComponent

        CutoffPanel {
            typeId: "effect.highpass"
            width: 320
        }
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("High Pass");
        verify(testCase.sessionId !== "", "the mock opened a session");
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "hpf", type: "effect.highpass" }, index: -1 }));
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        SessionModel.setParam("hpf", "cutoff", 20);
        SessionModel.setParam("hpf", "slope", "12dB");
    }

    function test_itIsAnEffectWithAFaceAndAPanelOfItsOwn() {
        verify(BlockCatalogue.types.indexOf("effect.highpass") >= 0, "the engine does not offer the High Pass Filter");
        compare(BlockPresentation.category("effect.highpass"), "effects");
        compare(BlockPresentation.name("effect.highpass"), "High Pass Filter");
        verify(BlockFaces.faceFor("effect.highpass") !== null, "the High Pass Filter has no face");
        verify(BlockPanels.panelFor("effect.highpass") !== null, "the High Pass Filter has no panel");
    }

    function test_theRangeAndTheStartAreTheEnginesOwn() {
        const panel = createTemporaryObject(panelComponent, null);
        compare(panel.lowestHz, 20);
        compare(panel.highestHz, 2000);
        compare(panel.defaultHz, 20);
        const face = createTemporaryObject(faceComponent, null);
        compare(face.lowestHz, 20);
        compare(face.highestHz, 2000);
    }

    function test_theFaceStartsAtTheBottomAndSaysWhereItIs() {
        const face = createTemporaryObject(faceComponent, null);
        face.blockId = "hpf";
        tryVerify(() => face.headlineLabel.text === "20 Hz", 1000, "the face does not start at 20 Hz");
        tryVerify(() => Math.abs(face.slider.value) < 1e-6, 1000, "the slider does not start at the bottom");
        compare(face.detail, "Cutoff 20 Hz, 12 dB per octave");

        // Half way along a logarithmic slider from 20 Hz to 2 kHz is 200 Hz.
        face.slider.value = 0.5;
        face.slider.moved();
        SessionModel.commitParam("hpf", "cutoff");
        verify(Math.abs(SessionModel.param("hpf", "cutoff") - 200) < 0.05,
               "half way along the slider set " + SessionModel.param("hpf", "cutoff"));
        compare(face.headlineLabel.text, "200 Hz");
    }

    function test_theWholeCutoffFitsOnATile() {
        const face = createTemporaryObject(tileSizedFace, null);
        face.blockId = "hpf";
        for (const hz of [20, 85.5, 1250, 2000]) {
            SessionModel.setParam("hpf", "cutoff", hz);
            tryVerify(() => face.headlineLabel.text === face.words.frequencyText(hz), 1000);
            verify(!face.headlineLabel.truncated, face.headlineLabel.text + " does not fit on a tile");
        }
    }

    function test_aTypedCutoffIsHeldInsideTheRange() {
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "hpf";
        panel.cutoffField.text = "80";
        panel.cutoffField.editingFinished();
        compare(SessionModel.param("hpf", "cutoff"), 80);
        panel.cutoffField.text = "8k";
        panel.cutoffField.editingFinished();
        compare(SessionModel.param("hpf", "cutoff"), 2000);
        compare(panel.cutoffField.text, "2.00 kHz");
        panel.cutoffField.text = "5";
        panel.cutoffField.editingFinished();
        compare(SessionModel.param("hpf", "cutoff"), 20);
    }

    function test_theSlopeIsChosenByItsButtons() {
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "hpf";
        const buttons = [];
        for (const child of panel.slopeButtons.children) {
            if (child.checkable === true) {
                buttons.push(child);
            }
        }
        compare(buttons.map(button => button.text), ["12 dB/oct", "24 dB/oct"]);
        buttons[1].clicked();
        compare(SessionModel.param("hpf", "slope"), "24dB");
        tryVerify(() => buttons[1].checked && !buttons[0].checked, 1000);
    }
}
