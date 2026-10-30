// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The VU block's face and panel offscreen, against the mock engine:
// the ids the interface spells out are the engine's, the reference reaches
// the block, and a level that arrives through the telemetry moves the
// needles to where the scale says and lights the lamps. How the needle
// itself moves is measured on the block, in blocks.VuMeterTest.

import QtQuick
import QtTest
import org.kde.kirigami as Kirigami
import io.github.reverblabr

TestCase {
    id: testCase

    name: "VuMeter"
    when: windowShown

    property string sessionId: ""

    /// A sine whose peaks reach −18 dBFS, as an amplitude.
    readonly property real referenceSine: Math.pow(10, -18 / 20)

    Component {
        id: panelComponent

        VuPanel {
            width: Kirigami.Units.gridUnit * 20
            height: 420
        }
    }

    Component {
        id: faceComponent

        VuFace {
            width: 132
            height: 70
        }
    }

    Component {
        id: settingsComponent

        SettingsPanel {
            width: 420
            height: 520
        }
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("VU");
        verify(testCase.sessionId !== "", "the mock opened a session");
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "vu", type: "meter.vu" }, index: -1 }));
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        SessionModel.setParam("vu", "reference", VuScale.defaultReference);
        SessionModel.setParam("vu", "showInTray", false);
    }

    function cleanup() {
        TestEngine.clearFakeLevels(testCase.sessionId);
        TestEngine.stop(testCase.sessionId);
        Meters.watch("");
    }

    /// Starts the session with \a peak going through the block, and waits
    /// until the telemetry says so.
    function play(peak: real) {
        Meters.watch(testCase.sessionId);
        TestEngine.start(testCase.sessionId);
        TestEngine.setFakeLevel(testCase.sessionId, "vu", peak);
        tryVerify(() => Meters.live && (Meters.levels("vu").active ?? false), 1000, "the block never went live");
    }

    function test_theBlockHasAFaceAndAPanelOfItsOwn() {
        verify(BlockPanels.panelFor("meter.vu") !== null, "the VU block is not in the panel register");
        verify(BlockFaces.faceFor("meter.vu") !== null, "the VU block has no face");
        compare(BlockCatalogue.describe("meter.vu").face, "vu");
        compare(BlockPresentation.category("meter.vu"), "meters");
    }

    function test_theIdsTheInterfaceSpellsOutAreTheOnesTheEngineOffers() {
        // The interface does not depend on the DSP layer, so the reference
        // ids are written down twice; this is what stops them drifting.
        const descriptor = BlockCatalogue.describeParam("meter.vu", "reference");
        compare(VuScale.referenceChoices, descriptor.choices);
        compare(VuScale.defaultReference, descriptor.default);
        compare(BlockCatalogue.describeParam("meter.vu", "showInTray").id, "showInTray");
    }

    function test_choosingAReferenceWritesTheStableId() {
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "vu";
        panel.locale = Qt.locale("en_GB");
        wait(50);

        compare(panel.reference, "-18");
        compare(panel.referenceBox.text, "-18 dBFS");
        compare(panel.referenceMenu.count, 3);

        const loudest = panel.referenceMenu.itemAt(VuScale.referenceChoices.indexOf("-14"));
        compare(loudest.text, "-14 dBFS");
        loudest.triggered();
        tryCompare(panel, "reference", "-14");
        compare(SessionModel.param("vu", "reference"), "-14");
        compare(panel.referenceBox.text, "-14 dBFS");
        verify(loudest.checked, "the menu does not mark the reference in force");
    }

    function test_theTraySettingIsKeptNow() {
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "vu";
        wait(50);
        compare(panel.traySwitch.checked, false);
        panel.traySwitch.toggle();
        panel.traySwitch.toggled();
        tryVerify(() => SessionModel.param("vu", "showInTray") === true, 500);
    }

    function test_withNothingRunningTheNeedlesRestOnTheirPins() {
        const face = createTemporaryObject(faceComponent, null);
        face.blockId = "vu";
        wait(50);

        compare(face.active, false);
        compare(face.detail, "Idle");
        // Straight out to the left, in the muted colour.
        compare(face.leftDial.needle.rotation, -90);
        compare(face.rightDial.needle.rotation, -90);
        verify(Qt.colorEqual(face.leftDial.needle.color, AppColors.muted), "an idle needle is not muted");
        compare(face.leftDial.lamp.visible, false, "a tile shows a lamp that is not lit");
    }

    function test_theTileIsTheDesignsTwoDials() {
        // Two dials side by side, each a band on a half circle with the red
        // from 0 VU up, drawn in the design's proportions.
        const face = createTemporaryObject(faceComponent, null);
        face.blockId = "vu";
        wait(50);
        const dial = face.leftDial;
        verify(dial.width > 30 && dial.width <= 60, "the dial is " + dial.width + " wide");
        compare(dial.arcRadius / dial.width, 19 / 46);
        compare(dial.band / dial.width, 5 / 46);
        verify(face.rightDial.x > dial.x + dial.width, "the dials are not side by side");
        compare(dial.showMarks, false, "a tile's dial is written on");
        compare(dial.redFrom, VuScale.positionOf(0));
    }

    function test_aReferenceSineReadsZeroVuAndPutsTheNeedleOnTheRed() {
        const face = createTemporaryObject(faceComponent, null);
        face.blockId = "vu";
        testCase.play(testCase.referenceSine);

        tryVerify(() => face.active, 500);
        fuzzyCompare(face.leftVu, 0, 0.01);
        fuzzyCompare(face.rightVu, 0, 0.01);
        // On the edge of the red: 0 VU is 71 % of the way round.
        fuzzyCompare(face.leftDial.needle.rotation, 180 * VuScale.positionOf(0) - 90, 0.5);
        verify(Qt.colorEqual(face.leftDial.needle.color, AppColors.text), "a live needle is not in the text colour");
        compare(face.detail, "L " + Number(0).toLocaleString(Qt.locale(), 'f', 1) + " VU, R "
                + Number(0).toLocaleString(Qt.locale(), 'f', 1) + " VU");

        // The same level against a reference 2 dB quieter reads +2 VU.
        SessionModel.setParam("vu", "reference", "-20");
        tryVerify(() => Math.abs(face.leftVu - 2) < 0.01, 500, "the reference did not move the reading");
        fuzzyCompare(face.leftDial.needle.rotation, 180 * VuScale.positionOf(2) - 90, 0.5);
    }

    function test_aSideThatReachesFullScaleLightsItsLampUntilClicked() {
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "vu";
        testCase.play(1.0);

        tryVerify(() => panel.clipped, 500, "full scale did not light anything");
        tryVerify(() => panel.leftDial !== null && panel.leftDial.clipped, 500);
        verify(Qt.colorEqual(panel.leftDial.lamp.color, AppColors.dangerMark), "the lamp is not red");
        verify(panel.sideText(0).indexOf("clipped") >= 0, "the tooltip does not say so");
        // The needle is pinned past the end of the scale, not beyond it.
        compare(panel.leftDial.needle.rotation, 90);

        TestEngine.setFakeLevel(testCase.sessionId, "vu", 0.1);
        Meters.resetClip("vu");
        tryVerify(() => !panel.clipped, 500, "clicking did not put the light out");
        verify(Qt.colorEqual(panel.leftDial.lamp.color, AppColors.well), "the lamp stayed red");
    }

    function test_thePanelsDialsCarryTheMarks() {
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "vu";
        wait(50);
        verify(panel.leftDial !== null && panel.rightDial !== null, "the panel has no dials");
        compare(panel.leftDial.showMarks, true);
        compare(panel.leftDial.label, "L");
        compare(panel.rightDial.label, "R");
        compare(VuScale.marks, [-20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3]);
        compare(panel.leftDial.markText(3), "+3");
        compare(panel.leftDial.markText(-7), "-7");
        compare(panel.formatVu(VuScale.bottomVu), "Below " + Number(-20).toLocaleString(panel.locale, 'f', 0) + " VU");
    }

    function test_theSettingsPanelShowsTheBlocksOwnPanel() {
        const card = createTemporaryObject(settingsComponent, null);
        card.open("vu");
        wait(50);
        compare(card.typeId, "meter.vu");
        const body = card.body.item;
        verify(body !== null, "the card loaded no body");
        compare(body.blockId, "vu");
        compare(card.bodyAsks("panelWidth", 0), Kirigami.Units.gridUnit * 20);
        compare(card.presets.showsPresets, false, "a meter's settings offer presets");
    }
}
