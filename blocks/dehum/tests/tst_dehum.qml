// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Dehum face and panel: the base and the amount on the tile, the
// mains frequency one press away, the amount said in decibels, and every
// control writing the setting it names. And Auto: a switch, and what
// it is following, or that it is holding, said on the panel and the tile.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "Dehum"

    property string sessionId: ""

    Component {
        id: tileSizedFace

        DehumFace {
            width: AppMetrics.tileOuterWidth - 2
            height: AppMetrics.tileOuterHeight - AppMetrics.tileBandHeight - 2
        }
    }

    Component {
        id: panelComponent

        DehumPanel {
            width: 340
        }
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Dehum");
        verify(testCase.sessionId !== "", "the mock opened a session");
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "hum", type: "advanced.dehum" }, index: -1 }));
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        SessionModel.setParam("hum", "base", 50);
        SessionModel.setParam("hum", "amount", 5);
        SessionModel.setParam("hum", "harmonics", 10);
        SessionModel.setParam("hum", "auto", false);
    }

    function cleanup() {
        TestEngine.stop(testCase.sessionId);
        Meters.watch("");
    }

    function test_itIsAnAdvancedBlockWithAFaceAndAPanelOfItsOwn() {
        verify(BlockCatalogue.types.indexOf("advanced.dehum") >= 0, "the engine does not offer Dehum");
        compare(BlockPresentation.category("advanced.dehum"), "advanced");
        verify(BlockFaces.faceFor("advanced.dehum") !== null, "Dehum has no face");
        verify(BlockPanels.panelFor("advanced.dehum") !== null, "Dehum has no panel");
    }

    function test_theRangesAreTheOnesTheEngineOffers() {
        const panel = createTemporaryObject(panelComponent, null);
        const base = BlockCatalogue.describeParam("advanced.dehum", "base");
        compare(panel.baseSlider.from, base.minimum);
        compare(panel.baseSlider.to, base.maximum);
        compare(panel.amountSlider.to, BlockCatalogue.describeParam("advanced.dehum", "amount").maximum);
        compare(panel.harmonicsBox.to, BlockCatalogue.describeParam("advanced.dehum", "harmonics").maximum);
    }

    function test_theFaceShowsTheBaseAndTheAmountAndFitsATile() {
        const face = createTemporaryObject(tileSizedFace, null);
        face.blockId = "hum";
        tryVerify(() => face.baseLabel.text === "50.0 Hz" && face.amountLabel.text === "5.0", 1000);
        compare(face.detail, "Hum at 50.0 Hz, 30 dB down");
        SessionModel.setParam("hum", "base", 59.4);
        SessionModel.setParam("hum", "amount", 10);
        tryVerify(() => face.baseLabel.text === "59.4 Hz", 1000);
        compare(face.amountLabel.text, "10.0");
        compare(face.detail, "Hum at 59.4 Hz, 60 dB down");
        verify(!face.baseLabel.truncated && !face.amountLabel.truncated, "a setting does not fit on a tile");
        const bottom = face.amountSlider.mapToItem(face, 0, face.amountSlider.height).y;
        verify(bottom <= face.height + 0.5, "the amount slider ends at " + bottom + " in a face " + face.height + " high");

        face.amountSlider.value = 2.3;
        face.amountSlider.moved();
        SessionModel.commitParam("hum", "amount");
        compare(SessionModel.param("hum", "amount"), 2.3);
    }

    function test_theMainsIsOnePressAway() {
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "hum";
        const buttons = [];
        for (const child of panel.mainsButtons.children) {
            if (child.checkable === true) {
                buttons.push(child);
            }
        }
        compare(buttons.map(button => button.text), ["50 Hz", "60 Hz"]);
        tryVerify(() => buttons[0].checked, 1000);
        buttons[1].clicked();
        compare(SessionModel.param("hum", "base"), 60);
        tryVerify(() => buttons[1].checked && !buttons[0].checked, 1000);
        // Tuned off the mains, neither is lit.
        SessionModel.setParam("hum", "base", 58.7);
        tryVerify(() => !buttons[0].checked && !buttons[1].checked, 1000, "a tuned base still lights a mains button");
    }

    function test_theAmountIsSaidInDecibels() {
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "hum";
        tryVerify(() => panel.amountReadout.text === "30 dB down", 1000, panel.amountReadout.text);
        panel.amountSlider.value = 0;
        panel.amountSlider.moved();
        SessionModel.commitParam("hum", "amount");
        compare(SessionModel.param("hum", "amount"), 0);
        compare(panel.amountReadout.text, "Off");
    }

    function test_theSlidersAndTheHarmonicsWriteTheirSettings() {
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "hum";
        panel.baseSlider.value = 51.3;
        panel.baseSlider.moved();
        SessionModel.commitParam("hum", "base");
        compare(SessionModel.param("hum", "base"), 51.3);
        panel.harmonicsBox.value = 4;
        panel.harmonicsBox.valueModified();
        compare(SessionModel.param("hum", "harmonics"), 4);
    }

    function test_autoIsASwitchThatSaysWhatItWillDo() {
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "hum";
        verify(!panel.autoSwitch.checked);
        verify(!panel.autoStatus.visible, "a word about Auto with Auto off");
        panel.autoSwitch.toggle();
        panel.autoSwitch.toggled();
        compare(SessionModel.param("hum", "auto"), true);
        tryVerify(() => panel.autoStatus.visible, 1000);
        compare(panel.autoStatus.text, "While it runs, it follows the hum, starting from Base.");
    }

    function test_runningWithAutoThePanelAndTheTileSayItIsHolding() {
        // The mock has no hum in its made-up levels, as a real session with
        // none has not: the notches hold on Base, and both say so.
        SessionModel.setParam("hum", "auto", true);
        SessionModel.setParam("hum", "base", 50.4);
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "hum";
        const face = createTemporaryObject(tileSizedFace, null);
        face.blockId = "hum";
        Meters.watch(testCase.sessionId);
        TestEngine.start(testCase.sessionId);
        tryVerify(() => panel.live, 1000, "nothing was published while running");
        tryCompare(panel.autoStatus, "text", "No hum to follow: holding at 50.40 Hz");
        tryVerify(() => face.tracking, 1000);
        compare(face.baseLabel.text, "50.40 Hz");
        compare(face.detail, "Hum followed from 50.40 Hz, 30 dB down");
        verify(!face.baseLabel.truncated, "the tracked base does not fit on a tile");
    }

    function test_whatAutoFollowsIsSaidToAHundredth() {
        const words = createTemporaryQmlObject("import io.github.reverblabr; DehumWords {}", testCase);
        compare(words.autoStatus(true, true, 50.034), "Following the hum at 50.03 Hz");
        compare(words.autoStatus(true, false, 59.9), "No hum to follow: holding at 59.90 Hz");
        compare(words.trackedText(49.5), "49.50 Hz");
    }

    function test_withAutoTheTileSaysSo() {
        const face = createTemporaryObject(tileSizedFace, null);
        face.blockId = "hum";
        SessionModel.setParam("hum", "auto", true);
        tryVerify(() => face.automatic, 1000);
        // Not running: the base it starts from.
        compare(face.baseLabel.text, "50.0 Hz");
        compare(face.detail, "Hum followed from 50.0 Hz, 30 dB down");
    }
}
