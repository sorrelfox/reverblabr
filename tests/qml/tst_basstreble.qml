// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Bass & Treble face and panel: both ends on the tile, each lift
// or cut said with its sign, and every slider writing the end it names.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "BassTreble"

    property string sessionId: ""

    Component {
        id: faceComponent

        BassTrebleFace {
            width: 220
            height: 120
        }
    }

    Component {
        id: tileSizedFace

        BassTrebleFace {
            // The face as a tile holds it: inside the tile's border, under
            // its header.
            width: AppMetrics.tileOuterWidth - 2
            height: AppMetrics.tileOuterHeight - AppMetrics.tileBandHeight - 2
        }
    }

    Component {
        id: panelComponent

        BassTreblePanel {
            width: 320
        }
    }

    // A panel in a window of its own, for the tests that press on it.
    Component {
        id: shownPanel

        Window {
            property alias panel: inner

            width: 400
            height: 300
            visible: true

            BassTreblePanel {
                id: inner

                width: 320
            }
        }
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Bass & Treble");
        verify(testCase.sessionId !== "", "the mock opened a session");
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "bt", type: "effect.basstreble" }, index: -1 }));
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        SessionModel.setParam("bt", "bass", 0);
        SessionModel.setParam("bt", "treble", 0);
    }

    function test_itIsAnEffectWithAFaceAndAPanelOfItsOwn() {
        verify(BlockCatalogue.types.indexOf("effect.basstreble") >= 0, "the engine does not offer Bass & Treble");
        compare(BlockPresentation.category("effect.basstreble"), "effects");
        compare(BlockPresentation.name("effect.basstreble"), "Bass & Treble");
        verify(BlockFaces.faceFor("effect.basstreble") !== null, "Bass & Treble has no face");
        verify(BlockPanels.panelFor("effect.basstreble") !== null, "Bass & Treble has no panel");
    }

    function test_theRangeIsTheOneTheEngineOffers() {
        const panel = createTemporaryObject(panelComponent, null);
        for (const id of ["bass", "treble"]) {
            const described = BlockCatalogue.describeParam("effect.basstreble", id);
            compare(-panel.rangeDb, described.minimum, id);
            compare(panel.rangeDb, described.maximum, id);
        }
    }

    function test_aLiftOrACutIsSaidWithItsSign() {
        const words = createTemporaryObject(panelComponent, null).words;
        compare(words.gainText(0), "0 dB");
        compare(words.gainText(0.04), "0 dB");
        compare(words.gainText(3), "+3.0 dB");
        compare(words.gainText(-4.5), "-4.5 dB");
        compare(words.gainText(12), "+12.0 dB");
    }

    function test_theFaceShowsBothEnds() {
        const face = createTemporaryObject(faceComponent, null);
        face.blockId = "bt";
        tryVerify(() => face.bassLabel.text === "0 dB" && face.trebleLabel.text === "0 dB", 1000);
        compare(face.detail, "Bass 0 dB, treble 0 dB");

        SessionModel.setParam("bt", "bass", 4.5);
        SessionModel.setParam("bt", "treble", -3);
        tryVerify(() => face.bassLabel.text === "+4.5 dB", 1000);
        compare(face.trebleLabel.text, "-3.0 dB");
        compare(face.detail, "Bass +4.5 dB, treble -3.0 dB");
    }

    function test_bothRowsFitOnATile() {
        // Two rows of name, number and slider in the face a tile has: every
        // line whole, and nothing hanging out of the bottom.
        const face = createTemporaryObject(tileSizedFace, null);
        face.blockId = "bt";
        SessionModel.setParam("bt", "bass", -12);
        SessionModel.setParam("bt", "treble", -12);
        tryVerify(() => face.bassLabel.text === "-12.0 dB" && face.trebleLabel.text === "-12.0 dB", 1000);
        verify(!face.bassLabel.truncated && !face.trebleLabel.truncated, "a setting does not fit on a tile");
        const bottom = face.trebleSlider.mapToItem(face, 0, face.trebleSlider.height).y;
        verify(bottom <= face.height + 0.5, "the treble slider ends at " + bottom + " in a face " + face.height + " high");
        const top = face.bassLabel.mapToItem(face, 0, 0).y;
        verify(top >= -0.5, "the bass row starts above the face, at " + top);
    }

    function test_theFacesSlidersWriteTheirEnds() {
        const face = createTemporaryObject(faceComponent, null);
        face.blockId = "bt";
        face.bassSlider.value = 6;
        face.bassSlider.moved();
        SessionModel.commitParam("bt", "bass");
        compare(SessionModel.param("bt", "bass"), 6);
        face.trebleSlider.value = -2.5;
        face.trebleSlider.moved();
        SessionModel.commitParam("bt", "treble");
        compare(SessionModel.param("bt", "treble"), -2.5);
        compare(SessionModel.param("bt", "bass"), 6, "the treble slider moved the bass");
    }

    function test_thePanelWritesWhatItsSlidersSay() {
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "bt";
        panel.bassSlider.value = -7.5;
        panel.bassSlider.moved();
        SessionModel.commitParam("bt", "bass");
        compare(SessionModel.param("bt", "bass"), -7.5);
        tryVerify(() => panel.bassReadout.text === "-7.5 dB", 1000);
        panel.trebleSlider.value = 12;
        panel.trebleSlider.moved();
        SessionModel.commitParam("bt", "treble");
        compare(SessionModel.param("bt", "treble"), 12);
        compare(panel.trebleReadout.text, "+12.0 dB");
    }

    function test_aDoubleClickPutsAnEndBackToNothing() {
        SessionModel.setParam("bt", "treble", 5);
        const window = createTemporaryObject(shownPanel, null);
        const panel = window.panel;
        panel.blockId = "bt";
        const slider = panel.trebleSlider;
        tryVerify(() => slider.availableWidth > 0 && Math.abs(slider.value - 5) < 0.01, 1000);
        mouseDoubleClickSequence(slider, slider.leftPadding + slider.visualPosition * slider.availableWidth,
                                 slider.height / 2);
        tryVerify(() => SessionModel.param("bt", "treble") === 0, 1000,
                  "the treble is at " + SessionModel.param("bt", "treble") + " after a double-click");
        tryVerify(() => Math.abs(slider.value) < 0.01, 1000, "the slider did not follow");
    }
}
