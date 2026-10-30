// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Peak/RMS block's face and panel offscreen, against the mock
// engine: that the settings reach the block, that the ids the interface
// spells out are the ids the engine offers, and that the numbers are
// written the way a person reads them. What the levels themselves come to
// is measured in app.PeakRmsPanelTest, where there is telemetry to read.

import QtQuick
import QtTest
import org.kde.kirigami as Kirigami
import io.github.reverblabr

TestCase {
    id: testCase

    name: "PeakRmsPanel"
    when: windowShown

    property string sessionId: ""

    Component {
        id: panelComponent

        PeakRmsPanel {
            width: 320
            height: 520
        }
    }

    Component {
        id: faceComponent

        PeakRmsFace {
            width: 120
            height: 180
        }
    }

    Component {
        id: settingsComponent

        SettingsPanel {
            width: 320
            height: 520
        }
    }

    // **What the window has, and what tells the hub the holds**.
    // Nothing else here does: a panel writes the choice to the session and
    // a face only draws, so what the hub knows does not depend on what has
    // been opened or drawn.
    BlockModel {
        id: blocks

        session: SessionModel
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Meter");
        verify(testCase.sessionId !== "", "the mock opened a session");
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock",
                                              block: { id: "met", type: "meter.peakrms" },
                                              index: -1 }));
        // A second meter, because a session with one of them is the case
        // where a hold for the whole window looks right.
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock",
                                              block: { id: "met2", type: "meter.peakrms" },
                                              index: -1 }));
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        SessionModel.setParam("met", "peakHold", PeakHold.defaultChoice);
        SessionModel.setParam("met", "showInTray", false);
        SessionModel.setParam("met", "showPeak", true);
        SessionModel.setParam("met2", "peakHold", PeakHold.defaultChoice);
    }

    function test_theBlockHasAPanelOfItsOwn() {
        verify(BlockPanels.panelFor("meter.peakrms") !== null, "the Peak/RMS block is in the register");
        verify(BlockFaces.faceFor("meter.peakrms") !== null, "the Peak/RMS block has a face");
    }

    function test_theIdsTheInterfaceSpellsOutAreTheOnesTheEngineOffers() {
        // **The one thing that cannot be checked by reading either side.**
        // The interface layer does not depend on the DSP one, so the hold
        // ids are written down twice; this is what stops them drifting.
        const descriptor = BlockCatalogue.describeParam("meter.peakrms", "peakHold");
        compare(PeakHold.choices, descriptor.choices);
        compare(PeakHold.defaultChoice, descriptor.default);
        verify(BlockCatalogue.describeParam("meter.peakrms", "showInTray").id === "showInTray",
               "the block keeps a tray setting");
    }

    function test_choosingAHoldWritesTheStableIdAndTellsTheMeters() {
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "met";
        wait(50);

        compare(panel.holdChoice, "2s");
        // The value short, as the design writes it at the end of its row;
        // the menu offers each by its full name.
        compare(panel.holdBox.text, "2 s");
        compare(panel.holdLabel("2s"), "2 seconds");

        // Chosen from the menu the value opens. (A panel with no window
        // cannot open a popup, so the item is pressed directly.) An enum
        // travels as its choice's stable id, never as its position: a later
        // version may reorder the choices.
        compare(panel.holdMenu.count, PeakHold.choices.length);
        const five = panel.holdMenu.itemAt(PeakHold.choices.indexOf("5s"));
        compare(five.text, "5 seconds");
        five.triggered();
        wait(50);
        compare(SessionModel.param("met", "peakHold"), "5s");
        compare(panel.holdChoice, "5s");
        compare(panel.holdBox.text, "5 s");
        verify(five.checked, "the menu does not mark the choice that is in force");
        // What the panel then tells the hub, which is where wall-clock time
        // lives. That it waits that long is measured in app.PeakRmsPanelTest.
        compare(PeakHold.holdMs("5s"), 5000);

        panel.holdMenu.itemAt(PeakHold.choices.indexOf("forever")).triggered();
        wait(50);
        compare(SessionModel.param("met", "peakHold"), "forever");
        compare(panel.holdBox.text, "Until reset");
        // "Forever" is a line that never falls rather than a very long wait.
        compare(PeakHold.decayDbPerSecond("forever"), 0);
    }

    function test_aMeterHoldsForItsOwnSettingWithNoPanelOpen() {
        // The hold reaches the hub because the block is in the
        // session, not because somebody opened its panel — so this test
        // makes no panel and no face at all. Before the fix both meters
        // sat on the hub's default of 1200 ms.
        SessionModel.setParam("met", "peakHold", "5s");
        SessionModel.setParam("met2", "peakHold", "1s");
        wait(50);

        compare(Meters.holdMs("met"), 5000);
        compare(Meters.holdMs("met2"), 1000);
        verify(blocks.count >= 2, "the model is following the session's blocks");
    }

    function test_aMeterAddedToARunningSessionGetsItsHold() {
        // One that arrives while the window is open, with a hold already in
        // it: it has never had a tile, let alone a panel.
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock",
                                              block: { id: "late",
                                                       type: "meter.peakrms",
                                                       params: { peakHold: "5s" } },
                                              index: -1 }));
        tryVerify(() => Meters.holdMs("late") === 5000, 2000,
                  "a meter added to a running session kept the hub's default");

        // And it goes away again, so the tests after this one see the
        // session they were written against.
        SessionModel.removeBlocks(["late"]);
        wait(50);
    }

    function test_twoMetersInOneSessionEachHoldForTheirOwnTime() {
        // **Which block each hold was meant for** is the one thing neither
        // side shows on its own: the session knows the parameter and the
        // hub knows what it was told, and the two once did not meet.
        // That each then waits that long is measured in app.PeakRmsPanelTest.
        SessionModel.setParam("met", "peakHold", "1s");
        SessionModel.setParam("met2", "peakHold", "5s");

        const first = createTemporaryObject(panelComponent, null);
        first.blockId = "met";
        const second = createTemporaryObject(panelComponent, null);
        second.blockId = "met2";
        wait(50);

        compare(first.holdChoice, "1s");
        compare(second.holdChoice, "5s");
        compare(Meters.holdMs("met"), 1000);
        compare(Meters.holdMs("met2"), 5000);

        // And the second panel did not reach across to the first.
        second.holdMenu.itemAt(PeakHold.choices.indexOf("forever")).triggered();
        wait(50);
        compare(Meters.decayDbPerSecond("met2"), 0);
        compare(Meters.holdMs("met"), 1000);
        verify(Meters.decayDbPerSecond("met") > 0, "one meter held until reset froze the other");
    }

    function test_theTraySettingIsKeptNow() {
        // The tray itself is its own milestone; a setting that could not be
        // made until then would be a session that had to be gone over again
        // when it arrived.
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "met";
        wait(50);

        compare(panel.traySwitch.checked, false);
        panel.traySwitch.toggle();
        panel.traySwitch.toggled();
        wait(50);
        compare(SessionModel.param("met", "showInTray"), true);
    }

    function test_thePeakBarIsABlockSettingKeptWithTheSession() {
        // On out of the box, which is how the meter has always looked, and
        // a choice written to the session like every other control, so the
        // block keeps it when the session is saved and opened again.
        const descriptor = BlockCatalogue.describeParam("meter.peakrms", "showPeak");
        compare(descriptor.id, "showPeak");
        compare(descriptor.default, true);

        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "met";
        wait(50);
        compare(panel.peakSwitch.text, "Show peak bar");
        compare(panel.peakSwitch.checked, true);
        compare(panel.meter.showPeak, true);

        panel.peakSwitch.toggle();
        panel.peakSwitch.toggled();
        wait(50);
        compare(SessionModel.param("met", "showPeak"), false);
        compare(panel.meter.showPeak, false);

        // **Set anywhere, shown here**: undo, a second window, a session
        // opened from disk all arrive as the session's value.
        SessionModel.setParam("met", "showPeak", true);
        wait(50);
        compare(panel.peakSwitch.checked, true);
        compare(panel.meter.showPeak, true);

        // It sits at the bottom with the other two, directly above the tray.
        const peakY = panel.peakSwitch.mapToItem(panel, 0, 0).y;
        const trayY = panel.traySwitch.mapToItem(panel, 0, 0).y;
        verify(peakY > panel.meter.mapToItem(panel, 0, 0).y + panel.meter.height, "it is not below the meter");
        verify(peakY < trayY, "it is not above the tray's checkbox");
    }

    function test_withThePeakBarOffTheMetersDrawTheRmsAlone() {
        // A running session and a level, so there is a peak to draw. The
        // tile and the panel are both the block's meter; with the setting
        // off neither draws the tip, and both still draw the RMS under it.
        Meters.watch(testCase.sessionId);
        TestEngine.start(testCase.sessionId);
        tryVerify(() => Meters.live, 2000, "the session published nothing to be live from");
        TestEngine.setFakeLevel(testCase.sessionId, "met", 0.5);

        const face = createTemporaryObject(faceComponent, null);
        face.blockId = "met";
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "met";
        const bars = () => [face.meter.sideBars[0].bar, face.meter.sideBars[1].bar,
                            panel.meter.sideBars[0].bar, panel.meter.sideBars[1].bar];
        tryVerify(() => bars().every(bar => bar.peakDb.length === 1 && bar.peakDb[0] > -7), 2000,
                  "the peak bar was not drawn with the setting on");

        SessionModel.setParam("met", "showPeak", false);
        tryVerify(() => bars().every(bar => bar.peakDb.length === 0), 2000,
                  "a meter still drew the peak bar with the setting off");
        for (const bar of bars()) {
            compare(bar.rmsDb.length, 1, "the RMS bar went with the peak");
            verify(bar.rmsDb[0] > -12, "the RMS bar is not drawn at the level");
        }

        SessionModel.setParam("met", "showPeak", true);
        tryVerify(() => bars().every(bar => bar.peakDb.length === 1), 2000,
                  "turning the setting back on did not bring the peak bar back");

        TestEngine.clearFakeLevels(testCase.sessionId);
        TestEngine.stop(testCase.sessionId);
        Meters.watch("");
    }

    function test_theNumbersAreWrittenToATenthOfADecibelInTheReadersLocale() {
        // The criterion's other half: a −18 dBFS sine reads −18.0 and
        // −21.0, and what makes those two numbers out of two amplitudes is
        // measured in app.PeakRmsPanelTest. This is how they are written.
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "met";
        wait(50);

        panel.locale = Qt.locale("en_GB");
        compare(panel.formatDb(-18), "-18.0 dB");
        compare(panel.formatDb(-21), "-21.0 dB");
        compare(panel.formatDb(-3.25), "-3.3 dB");
        // A decimal comma in de_DE is what a German reader expects; what
        // travels to the engine never goes through here at all.
        panel.locale = Qt.locale("de_DE");
        compare(panel.formatDb(-18), "-18,0 dB");

        // Below the floor there is no number worth writing.
        compare(panel.formatDb(MeterScale.floorDb), "—");
        compare(panel.formatDb(MeterScale.floorDb - 40), "—");
    }

    function test_withNothingRunningTheMetersAreIdleAndNotQuiet() {
        // **Idle is not silence.** A meter at the bottom says "this is
        // playing nothing"; a face that says Idle says "nothing is
        // running", and a person glancing at the window has to be able to
        // tell which.
        const face = createTemporaryObject(faceComponent, null);
        face.blockId = "met";
        wait(50);

        compare(face.active, false);
        compare(face.detail, "Idle");
        // The tile's four marks: −3, −9, −18 and −36, which is what there
        // is room to read on something this size.
        compare(face.meter.ticks, MeterScale.compactTicks);
        compare(face.meter.ticks, [-3, -9, -18, -36]);
        // A dot a side rather than one strip across the meter.
        compare(face.meter.channelClips, true);
    }

    function test_thePanelsMetersCarryTheWholeScale() {
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "met";
        wait(50);

        // From full scale down to the floor, as the design writes it: a
        // panel has the room a tile does not.
        compare(panel.meter.showScale, true);
        compare(panel.meter.ticks, [0, -3, -6, -9, -12, -18, -24, -30, -36, -48, -60]);
        compare(MeterScale.topDb, 0);
        compare(MeterScale.floorDb, -60);

        // Nothing has clipped, so there is nothing to put out.
        compare(panel.clipped, false);
    }

    function test_theSettingsPanelShowsTheBlocksOwnPanel() {
        const panel = createTemporaryObject(settingsComponent, null);
        panel.open("met");
        wait(50);

        compare(panel.typeId, "meter.peakrms");
        const body = panel.body.item;
        verify(body !== null, "the panel loaded a body");
        compare(body.blockId, "met");
    }

    // --- the tile as the design draws it ---

    function test_theTileIsBarScaleBarInTheLiveColours() {
        const face = createTemporaryObject(faceComponent, null);
        face.blockId = "met";
        wait(50);
        const meter = face.meter;

        // A bar a side with the scale between them, not bars side by side.
        compare(meter.scaleBetween, true);
        compare(meter.split, true, "a stereo meter did not split its bars around the scale");
        verify(meter.barWidth > 0 && meter.barWidth < Kirigami.Units.gridUnit, "the bars are not narrow");
        // The RMS in the live colour and the tip up to the peak paler, as the
        // design draws them.
        compare(Qt.colorEqual(meter.rmsColor, AppColors.live), true);
        verify(!Qt.colorEqual(meter.peakColor, meter.rmsColor), "the tip is the same colour as the bar");
        // Nothing is written under the bars; the reading is the tooltip.
        verify(face.detail.length > 0, "there is no reading to hover for");
    }

    function test_eachScaleMarkSitsAtTheLevelItNames() {
        // **The numbers are placed by the bars' own curve**, inside the part
        // of the bar below the clip light, so a mark sits where a bar at
        // that level ends.
        const face = createTemporaryObject(faceComponent, null);
        face.blockId = "met";
        wait(50);
        const height = 54;
        const clip = Math.min(height * 0.04, 6);
        let previous = -1;
        for (const db of MeterScale.compactTicks) {
            const y = face.meter.yOf(db, height);
            const expected = clip + 1 + (height - clip - 1) * (1 - MeterScale.positionOf(db));
            compare(y, expected, db + " dB is not where a bar at that level ends");
            verify(y > previous, "the marks are not in order down the bar");
            previous = y;
        }
    }

    function test_aMonoMeterShowsItsOneChannelOnBothBars() {
        const face = createTemporaryObject(faceComponent, null);
        face.blockId = "met";
        compare(face.meter.channelOf([-12], 0), [-12]);
        compare(face.meter.channelOf([-12], 1), [-12], "the right bar of a mono meter is empty");
        compare(face.meter.channelOf([-12, -20], 1), [-20]);
        compare(face.meter.channelOf([], 1), []);
    }

    // --- the settings as the design draws them ---

    function test_theSettingsAreBarScaleBarWithClipBoxesAndChannels() {
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "met";
        wait(50);
        const meter = panel.meter;

        compare(meter.scaleBetween, true);
        compare(meter.split, true);
        compare(meter.clipBoxes, true, "the clip lights are not boxes above the bars");
        compare(meter.channelLabels, true);
        compare(meter.channelName(0), "L");
        compare(meter.channelName(1), "R");
        // The live colours, a hot band from −6 dBFS and a white hold line.
        compare(Qt.colorEqual(meter.rmsColor, AppColors.live), true);
        compare(meter.hotDb, -6);
        compare(Qt.colorEqual(meter.hotColor, AppColors.danger), true);
        compare(Qt.colorEqual(meter.holdColor, AppColors.text), true);
        // The bars' own MeterItems carry the hot level.
        const left = meter.sideBars[0];
        verify(left !== null, "there is no left side");
        compare(left.bar.hotDb, -6);
    }

    function test_theNumbersAreEachSidesToEachSide() {
        // The table is gone; what it said is each side's tooltip and what a
        // screen reader reads of it.
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "met";
        wait(50);
        compare(panel.sideText(0), "Peak —, RMS —");
        compare(panel.traySwitch.text, "Show in system tray");
    }

    function test_theCardIsNarrowAndHasNoPresetsRow() {
        const card = createTemporaryObject(settingsComponent, null);
        card.open("met");
        wait(50);
        compare(card.bodyAsks("panelWidth", 0), Kirigami.Units.gridUnit * 15);
        compare(card.implicitWidth, Kirigami.Units.gridUnit * 15);
        verify(card.implicitWidth < Kirigami.Units.gridUnit * 18, "the card is no narrower than the standard one");
        compare(card.presets.visible, false, "a meter's settings still have a presets row");
    }
}
