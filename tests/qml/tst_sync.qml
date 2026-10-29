// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Sync block's face and panel offscreen, against the mock engine:
// the delay a person sets is the number the block gets, the tile says it
// the way the design writes a value, and while the session runs the panel
// says what the delay comes to in samples. That the block delays by exactly
// that much, and fades rather than clicks, is measured in blocks.SyncTest.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "Sync"
    when: windowShown

    property string sessionId: ""

    Component {
        id: faceComponent

        SyncFace {
            width: 132
            height: 70
        }
    }

    Component {
        id: panelComponent

        SyncPanel {
            width: 320
            height: 200
        }
    }

    Component {
        id: settingsComponent

        SettingsPanel {
            width: 360
            height: 400
        }
    }

    SyncScale {
        id: scale
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Sync");
        verify(testCase.sessionId !== "", "the mock opened a session");
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "sync", type: "advanced.sync" }, index: -1 }));
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        SessionModel.setParam("sync", "delay", 0);
    }

    function cleanup() {
        TestEngine.stop(testCase.sessionId);
        Meters.watch("");
    }

    function test_theBlockHasAFaceAndAPanelOfItsOwn() {
        verify(BlockFaces.faceFor("advanced.sync") !== null, "the Sync block has no face");
        verify(BlockPanels.panelFor("advanced.sync") !== null, "the Sync block is not in the panel register");
        compare(BlockCatalogue.describe("advanced.sync").face, "value");
        compare(BlockPresentation.category("advanced.sync"), "advanced");
    }

    function test_theScaleIsTheBlocksRangeAndGivesShortDelaysRoom() {
        // The range the interface draws is the one the engine takes.
        const descriptor = BlockCatalogue.describeParam("advanced.sync", "delay");
        compare(descriptor.maximum, scale.maximumMs);
        compare(descriptor.minimum, 0);
        compare(descriptor.default, 0);
        compare(descriptor.unit, "ms");

        compare(scale.positionOf(0), 0);
        compare(scale.positionOf(2000), 1);
        // Square law: 120 ms a quarter of the way along, not 6 %.
        fuzzyCompare(scale.positionOf(120), 0.2449, 0.0001);
        for (const ms of [0, 0.5, 1, 12.35, 120, 125.3, 1999.99, 2000]) {
            compare(scale.msAt(scale.positionOf(ms)), ms, "a round trip through the slider moved " + ms + " ms");
        }
    }

    function test_aDelayIsWrittenAsExactlyAsItIsSet() {
        const locale = Qt.locale("en_GB");
        compare(scale.text(120, locale), "120 ms");
        compare(scale.text(125.3, locale), "125.3 ms");
        compare(scale.text(12.35, locale), "12.35 ms");
        compare(scale.text(0, locale), "0 ms");
        compare(scale.number(125.3, Qt.locale("de_DE")), "125,3");
    }

    function test_theTileSaysTheDelayAndItsSliderSetsIt() {
        const face = createTemporaryObject(faceComponent, null);
        face.blockId = "sync";
        SessionModel.setParam("sync", "delay", 120);
        tryCompare(face, "delayMs", 120);
        compare(face.delayLabel.text, scale.text(120, Qt.locale()));
        compare(face.caption.text, "delay");
        fuzzyCompare(face.slider.value, scale.positionOf(120), 0.0001);

        // Moved by a person: the number the block gets is the slider's.
        face.slider.value = scale.positionOf(500);
        face.slider.moved();
        face.slider.pressedChanged();
        tryVerify(() => Math.abs(Number(SessionModel.param("sync", "delay")) - 500) < 0.01, 1000,
                  "the tile's slider did not set the delay");
    }

    function test_thePanelsBoxTakesAHundredthOfAMillisecond() {
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "sync";
        panel.locale = Qt.locale("en_GB");
        wait(50);

        // In hundredths, so a whole number for the spin box.
        compare(panel.delayBox.to, 200000);
        compare(panel.delayBox.stepSize, 100, "the arrows do not step a whole millisecond");
        compare(panel.delayBox.textFromValue(12530, Qt.locale()), "125.3");
        compare(panel.delayBox.valueFromText("125.3", Qt.locale()), 12530);
        compare(panel.delayBox.valueFromText("42.25 ms", Qt.locale()), 4225);
        compare(panel.delayBox.valueFromText("5000", Qt.locale()), 200000, "a delay past the end was not held at it");

        panel.delayBox.value = 12530;
        panel.delayBox.valueModified();
        tryVerify(() => Math.abs(Number(SessionModel.param("sync", "delay")) - 125.3) < 0.001, 1000,
                  "what was typed did not reach the block");
        fuzzyCompare(panel.slider.value, scale.positionOf(125.3), 0.0001);

        // A reader with a decimal comma types one.
        panel.locale = Qt.locale("de_DE");
        compare(panel.delayBox.valueFromText("12,5", Qt.locale()), 1250);
    }

    function test_whileRunningThePanelSaysTheDelayInSamples() {
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "sync";
        SessionModel.setParam("sync", "delay", 120);
        compare(panel.samplesLine.visible, false, "a stopped session reported a delay");

        AppShell.sessionId = testCase.sessionId;
        Meters.watch(testCase.sessionId);
        TestEngine.start(testCase.sessionId);
        tryVerify(() => panel.reporting, 1000, "the running block said nothing");
        tryCompare(panel, "delaySamples", 5760);
        tryVerify(() => panel.samplesLine.visible, 1000, "the samples line did not appear");
        verify(panel.samplesLine.text.indexOf("48 kHz") >= 0, panel.samplesLine.text);
        compare(panel.samplesText(1, 48000).indexOf("1 sample at"), 0, "one sample is not singular");
    }

    function test_theSettingsPanelShowsTheBlocksOwnPanel() {
        const card = createTemporaryObject(settingsComponent, null);
        card.open("sync");
        wait(50);
        compare(card.typeId, "advanced.sync");
        const body = card.body.item;
        verify(body !== null, "the card loaded no body");
        compare(body.blockId, "sync");
        verify(body.delayBox !== undefined, "the card is not the Sync panel");
    }
}
