// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The settings panel offscreen, against the mock engine: that it opens
// on a block, that the generic panel offers a control for every kind of
// parameter, and that renaming and the on/off switch reach the engine.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "SettingsPanel"

    /// How long a change the panel notices may take to show.
    readonly property int noticeMs: 500
    when: windowShown

    property string sessionId: ""

    // **No parent.** An item's `visible` is its own value and its parent's
    // together, and a test case offscreen is not shown — so a panel parented
    // to it would read as hidden however it was opened.
    Component {
        id: panelComponent

        SettingsPanel {
            width: 320
            height: 480
        }
    }

    // **A panel in a window that is shown**, for what only a window has:
    // whether the footer is seen, and the overlay a question opens in.
    Component {
        id: shownPanel

        Window {
            property alias panel: inner

            width: 480
            height: 640
            visible: true

            SettingsPanel {
                id: inner

                width: 360
                height: 600
            }
        }
    }

    // The generic panel on its own. **What most of these tests are about**:
    // a block that declares its parameters and gets a panel for nothing.
    // The Volume block now has a panel of its own as well, and the
    // generic one stays reachable behind it as the accessible fallback.
    Component {
        id: genericComponent

        GenericParamPanel {
            width: 320
        }
    }

    function initTestCase() {
        // A session of our own with one block in it, made through the
        // facade exactly as the window would make it.
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Panel");
        verify(testCase.sessionId !== "", "the mock opened a session");
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock",
                                              block: { id: "b1", type: "effect.volume" },
                                              index: -1 }));
        // A second block, so that a parameter with a unit, a range and a
        // logarithmic taper is on the screen as well as a plain number.
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock",
                                              block: { id: "b2", type: "effect.dynamics" },
                                              index: -1 }));
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        // **A binding loop is a failure here, not a line in the log**.
        // A panel is a stack of bindings over the session and the presets,
        // and a loop among them re-evaluates whatever it touches until QML
        // gives up on it — silently, as far as any other test can see.
        failOnWarning(/Binding loop detected/);
    }

    /// Every parameter row in a panel, however deeply the layout nests
    /// them. The panel is a layout, not a list, so there is no model to ask.
    function paramRowsOf(item) {
        const rows = [];
        for (let i = 0; i < item.children.length; ++i) {
            const child = item.children[i];
            if (child.paramId !== undefined && child.paramId !== "") {
                rows.push(child);
            }
            rows.push(...paramRowsOf(child));
        }
        return rows;
    }

    function test_theCatalogueDescribesTheBlockTheSessionHolds() {
        verify(BlockCatalogue.types.indexOf("effect.volume") >= 0, "the catalogue has the Volume block");
        const params = BlockCatalogue.params("effect.volume");
        compare(params.length, 3);
        compare(params.map(p => p.id), ["gain", "overdrive", "muted"]);
    }

    function test_openingAndClosingIsAChangeOfOneString() {
        // **The panel is opened by naming a block**, not created for one,
        // so the canvas owns nothing and tidies nothing up, and moving from
        // one tile to the next is a change of one string.
        //
        // Whether it is *shown* is checked in tst_mainwindow, where there
        // is a window: an item with no window is never effectively visible,
        // whatever its own property says.
        const panel = createTemporaryObject(panelComponent, null);
        compare(panel.blockId, "");
        panel.open("b1");
        compare(panel.blockId, "b1");
        compare(panel.typeId, "effect.volume");

        let closedCount = 0;
        panel.closed.connect(() => { closedCount += 1; });
        panel.close();
        compare(panel.blockId, "");
        compare(closedCount, 1);
    }

    function test_everyParameterOfTheBlockGetsAControl() {
        // The criterion: every parameter can be edited through the generic
        // panel. A block that describes itself needs no interface code, and
        // that is only true if nothing is quietly left out.
        const panel = createTemporaryObject(genericComponent, null, { blockId: "b1" });
        wait(50);

        const rows = paramRowsOf(panel);

        compare(rows.map(r => r.paramId).sort(), ["gain", "muted", "overdrive"]);
        for (const row of rows) {
            verify(row.label.length > 0, row.paramId + " has nothing to call it");
        }
    }

    function test_aValueSetInThePanelReachesTheEngine() {
        const panel = createTemporaryObject(panelComponent, null);
        panel.open("b1");
        wait(50);

        SessionModel.setParam("b1", "gain", 2.5);
        compare(SessionModel.param("b1", "gain"), 2.5);

        // An enum travels as its choice's stable id, never as its position:
        // a later version may reorder the choices.
        SessionModel.setParam("b1", "overdrive", "2x");
        compare(SessionModel.param("b1", "overdrive"), "2x");

        SessionModel.setParam("b1", "muted", true);
        compare(SessionModel.param("b1", "muted"), true);
    }

    function test_renamingAndTheOnOffSwitchGoThroughTheEngine() {
        const panel = createTemporaryObject(panelComponent, null);
        panel.open("b1");
        wait(50);

        compare(panel.displayName, "Volume");
        SessionModel.renameBlock("b1", "Voice");
        wait(50);
        compare(SessionModel.block("b1").nickname, "Voice");
        compare(panel.displayName, "Voice");

        SessionModel.setBlockEnabled("b1", false);
        wait(50);
        compare(SessionModel.block("b1").enabled, false);
        SessionModel.setBlockEnabled("b1", true);
        wait(50);
        compare(SessionModel.block("b1").enabled, true);
    }

    // **A panel is headed with the block's name, not the engine's id**.
    // It used to fall back to the type id, so an unnamed block's
    // panel read `effect.volume` while its own tile read "Volume" — the
    // presentation table was written and the panel was never brought over.
    function test_anUnnamedBlockIsHeadedWithItsBlocksName() {
        const panel = createTemporaryObject(panelComponent, null);
        panel.open("b1");
        wait(50);

        // **Said rather than assumed.** Qt Quick Test runs these in
        // alphabetical order, so what the block is called when this starts
        // is whatever a test earlier in the alphabet left behind.
        SessionModel.renameBlock("b1", "");
        wait(50);
        compare(SessionModel.block("b1").nickname, "", "the block has no name of its own");
        compare(panel.displayName, BlockPresentation.name("effect.volume"),
                "the panel is headed with the type id rather than the block's name");
        verify(panel.displayName !== "effect.volume", "the panel is headed with the engine's id");

        // Clearing the name goes back to the block's own, and the field
        // says so before it is cleared.
        SessionModel.renameBlock("b1", "Voice");
        wait(50);
        compare(panel.displayName, "Voice");
        SessionModel.renameBlock("b1", "");
        wait(50);
        compare(panel.displayName, BlockPresentation.name("effect.volume"));
    }

    // A block of a type this build has never heard of still gets a header:
    // the table hands back the id, which is better than nothing at all.
    function test_aBlockOfAnUnknownTypeStillGetsAHeader() {
        compare(BlockPresentation.name("effect.nothing-like-this"), "effect.nothing-like-this");
    }

    function test_thePanelFollowsAChangeItDidNotMake() {
        // **The generic panel is where this trap would be walked into
        // again.** Every block that describes itself gets these controls,
        // so each kind of them is asked here whether it still follows the
        // session after somebody has used it.
        const panel = createTemporaryObject(genericComponent, null, { blockId: "b1" });
        wait(50);

        const rows = {};
        for (const row of paramRowsOf(panel)) {
            rows[row.paramId] = row;
        }

        // Each control is started from a state this test named, because
        // the block is the one the rest of the file has been editing.
        SessionModel.setParam("b1", "muted", false);
        SessionModel.setParam("b1", "overdrive", "off");

        const toggle = rows["muted"].activeControl;
        tryVerify(() => !toggle.checked, 250, "the switch did not start where the session was");
        toggle.toggle();
        toggle.toggled();
        compare(SessionModel.param("b1", "muted"), true);
        SessionModel.setParam("b1", "muted", false);
        tryVerify(() => !toggle.checked, 250,
                  "the switch stopped following the session after it was clicked");

        const combo = rows["overdrive"].activeControl;
        combo.currentIndex = 1;
        combo.activated(1);
        compare(SessionModel.param("b1", "overdrive"), "2x");
        SessionModel.setParam("b1", "overdrive", "4x");
        tryVerify(() => combo.currentIndex === 3, 250,
                  "the choice stopped following the session after it was used");

        const slider = rows["gain"].activeControl.fader;
        slider.value = slider.to;
        slider.moved();
        SessionModel.commitParam("b1", "gain");
        SessionModel.setParam("b1", "gain", 1.0);
        tryVerify(() => slider.value === rows["gain"].toPosition(1.0), 250,
                  "the slider stopped following the session after it was moved");
    }

    function test_aValueIsWrittenInTheReadersLocaleAndStoredInNobodys() {
        // The criterion: a decimal comma in de_DE, and a value in the
        // session that has no locale at all. **A number on the screen is
        // for a person; a number in a file is for a machine**, and the two
        // are not the same number written twice.
        const panel = createTemporaryObject(genericComponent, null, { blockId: "b1" });
        wait(50);

        const rows = paramRowsOf(panel);
        const gain = rows.find(r => r.paramId === "gain");
        verify(gain, "the gain has a row");

        gain.locale = Qt.locale("en_GB");
        compare(gain.formatted(2.5), "2.50");
        gain.locale = Qt.locale("de_DE");
        compare(gain.formatted(2.5), "2,50");

        // And what travels is a JSON number, which has no locale.
        SessionModel.setParam("b1", "gain", 2.5);
        compare(SessionModel.param("b1", "gain"), 2.5);
        compare(typeof SessionModel.param("b1", "gain"), "number");
    }

    function test_aUnitIsWrittenAfterTheNumber() {
        const panel = createTemporaryObject(panelComponent, null);
        panel.open("b2");
        wait(50);

        const rows = paramRowsOf(panel);
        const threshold = rows.find(r => r.paramId === "threshold");
        verify(threshold, "the dynamics threshold has a row");
        threshold.locale = Qt.locale("en_GB");
        compare(threshold.unitSuffix(), " dB");
        // How many digits the number deserves, not how wide its range is.
        compare(threshold.formatted(-20), "-20.0 dB");
        compare(threshold.formatted(-120), "-120 dB");
        compare(threshold.formatted(-6.5), "-6.50 dB");

        const attack = rows.find(r => r.paramId === "attack");
        compare(attack.unitSuffix(), " ms");
        // **A ratio is not linear.** Equal distances along its slider are
        // equal multiples, which is what the taper says and what the panel
        // has to do about it.
        const ratio = rows.find(r => r.paramId === "ratio");
        compare(ratio.logarithmic, true);
        compare(Math.round(ratio.fromPosition(0)), 1);
        compare(Math.round(ratio.fromPosition(1)), 20);
        // Halfway along is the geometric middle, not the arithmetic one.
        verify(ratio.fromPosition(0.5) < 10, "halfway up a ratio slider was the arithmetic middle");

        // And the Volume block's gain is an amplitude with no unit at all,
        // which the panel shows as a plain number.
        const volume = createTemporaryObject(genericComponent, null, { blockId: "b1" });
        wait(50);
        compare(paramRowsOf(volume).find(r => r.paramId === "gain").unitSuffix(), "");
    }

    function test_thePresetsFooterOffersTheShippedOnesAndManual() {
        const panel = createTemporaryObject(panelComponent, null);
        panel.open("b1");
        wait(50);

        const footer = panel.presets;
        verify(footer, "the settings panel has no presets row");
        tryVerify(() => footer.presets.length > 0, 1000, "no presets were offered for a block that ships three");

        // **"Manual" is an entry, not a blank.** A box that was empty when
        // the values matched nothing would look like one that had not
        // loaded yet.
        compare(footer.chooser.model[0].id, "");
        compare(footer.chooser.model[0].name, footer.manualText);
        // The shipped ones come first, and are named by the interface
        // rather than by the engine.
        verify(footer.presets[0].factory, "a shipped preset is not first");
        verify(footer.presets[0].name.length > 0, "a shipped preset has no words");
        verify(footer.presets[0].name !== footer.presets[0].id,
               "a shipped preset is showing its id instead of its name");
    }

    function test_theFirstPanelOfAKindOpensWithoutABindingLoop() {
        // The first panel of each kind of block asks for that kind's
        // presets, and an engine in the same process answers at once — from
        // inside the footer's binding that asked, which then changed what it
        // was reading. **A kind no other test here opens**, so that this is
        // the first time it is asked whatever order the tests run in; the
        // guard in init() is what fails.
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "meter", type: "meter.peakrms" }, index: -1 }));
        const answered = Presets.revision;
        const panel = createTemporaryObject(panelComponent, null);
        panel.open("meter");
        verify(panel.presets, "the settings panel has no presets row");
        // And the answer still arrives: asking later is not asking never.
        // (This kind ships no presets, so the list it brings is empty.)
        tryVerify(() => Presets.revision > answered, 1000, "the engine's answer never reached the footer");
    }

    function test_theFooterTakesTheBlockOffTheCanvas() {
        // Every panel in the design ends with a bin at the left of
        // the presets row.
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "gone", type: "effect.volume" }, index: -1 }));
        const panel = createTemporaryObject(panelComponent, null);
        panel.open("gone");
        wait(50);
        const footer = panel.presets;
        verify(footer.removeButton, "the footer has no remove button");
        compare(footer.removeButton.icon.name, "trash-empty-symbolic");
        footer.removeButton.clicked();
        tryVerify(() => SessionModel.blockIds.indexOf("gone") < 0, 1000, "the block is still there");
    }

    function test_applyingAPresetSetsTheBlockAndUndoPutsItBack() {
        // The criterion: **applying a preset can be undone**, in one step.
        const panel = createTemporaryObject(panelComponent, null);
        panel.open("b1");
        wait(50);
        const footer = panel.presets;
        tryVerify(() => footer.presets.length > 0, 1000, "no presets to apply");

        SessionModel.setParam("b1", "gain", 3.0);
        SessionModel.commitParam("b1", "gain");
        tryVerify(() => footer.shownId === "", testCase.noticeMs, "a hand-set block is showing a preset");

        const half = footer.presets.find(preset => preset.id === "halfVolume");
        verify(half, "the Volume block does not ship a half-volume preset");
        Presets.apply(testCase.sessionId, "b1", half.id);
        tryVerify(() => SessionModel.param("b1", "gain") === 0.5, testCase.noticeMs,
                  "applying the preset did not set the block");
        tryVerify(() => footer.shownId === half.id, testCase.noticeMs, "the box does not say which preset it is");

        TestEngine.undo(testCase.sessionId);
        tryVerify(() => SessionModel.param("b1", "gain") === 3.0, testCase.noticeMs,
                  "one undo did not put the block back");
        tryVerify(() => footer.shownId === "", testCase.noticeMs, "undo left the box showing a preset");
    }

    function test_manualShowsAfterAnyHandEdit() {
        // The criterion, and the reason "Manual" is worked out from the
        // values rather than remembered: nothing clears the marker a block
        // carries when somebody turns a knob, nor should it, because undo
        // has to be able to put it back.
        const panel = createTemporaryObject(panelComponent, null);
        panel.open("b1");
        wait(50);
        const footer = panel.presets;
        tryVerify(() => footer.presets.length > 0, 1000, "no presets to apply");

        const half = footer.presets.find(preset => preset.id === "halfVolume");
        Presets.apply(testCase.sessionId, "b1", half.id);
        tryVerify(() => footer.shownId === half.id, testCase.noticeMs, "the preset was not applied");

        // One knob, by hand.
        SessionModel.setParam("b1", "gain", 0.9);
        SessionModel.commitParam("b1", "gain");
        tryVerify(() => footer.shownId === "", testCase.noticeMs,
                  "a block one knob away from a preset still says it is showing it");
        compare(footer.chooser.currentIndex, 0, "the box is not on Manual");

        // **And back again**: a block put back to exactly what a preset says
        // is showing that preset, whatever it was last set from.
        SessionModel.setParam("b1", "gain", 0.5);
        SessionModel.commitParam("b1", "gain");
        tryVerify(() => footer.shownId === half.id, testCase.noticeMs,
                  "a block set back to a preset's values does not say so");
    }

    function test_aShippedPresetCannotBeRenamedOrDeleted() {
        const panel = createTemporaryObject(panelComponent, null);
        panel.open("b1");
        wait(50);
        const footer = panel.presets;
        tryVerify(() => footer.presets.length > 0, 1000, "no presets");

        const half = footer.presets.find(preset => preset.id === "halfVolume");
        Presets.apply(testCase.sessionId, "b1", half.id);
        tryVerify(() => footer.isFactory, testCase.noticeMs, "a shipped preset does not read as one");
        // It has no file either, so there is nothing to show in a file
        // manager.
        compare(String(Presets.fileOf(footer.typeId, half.id)), "");
    }

    function test_resetPutsTheDefaultsBackAfterAskingAndUndoesInOneStep() {
        // The criterion: **"Reset to Default Settings" asks first**, puts
        // every setting back to what a new block of the kind has, and one
        // undo takes the whole of it back.
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "reset", type: "effect.volume" }, index: -1 }));
        const window = createTemporaryObject(shownPanel, null);
        const panel = window.panel;
        waitForRendering(panel);
        panel.open("reset");
        wait(50);
        const footer = panel.presets;
        verify(!footer.resetAction.enabled, "a block at its defaults offers to reset them");

        SessionModel.setParams("reset", [["gain", 1.5], ["overdrive", 2], ["muted", true]]);
        tryVerify(() => SessionModel.param("reset", "muted") === true, testCase.noticeMs, "the settings did not change");
        tryVerify(() => footer.resetAction.enabled, testCase.noticeMs, "a changed block offers no reset");
        compare(footer.resettable.slice().sort(), ["gain", "muted", "overdrive"]);

        // **It asks**, and asking changes nothing.
        footer.resetAction.trigger();
        tryVerify(() => footer.resetDialog.opened, 1000, "the reset did not ask first");
        compare(SessionModel.param("reset", "gain"), 1.5, "the reset happened before it was confirmed");
        // Cancelling leaves everything as it was.
        footer.resetDialog.reject();
        tryVerify(() => !footer.resetDialog.visible, 1000, "the question did not go away");
        compare(SessionModel.param("reset", "gain"), 1.5, "cancelling the reset reset the block");

        footer.resetAction.trigger();
        tryVerify(() => footer.resetDialog.opened, 1000, "the reset did not ask the second time");
        footer.resetDialog.customFooterActions[0].trigger();
        tryVerify(() => SessionModel.param("reset", "gain") === undefined, testCase.noticeMs,
                  "the volume was not put back to its default");
        compare(SessionModel.param("reset", "overdrive"), undefined);
        compare(SessionModel.param("reset", "muted"), undefined);
        tryVerify(() => !footer.resetAction.enabled, testCase.noticeMs, "a reset block still offers a reset");
        // What the panel shows is the descriptor's default.
        for (const descriptor of BlockCatalogue.params("effect.volume")) {
            compare(footer.effectiveParams[descriptor.id], descriptor.default, descriptor.id + " is not at its default");
        }
        // The Edit menu names it for what it was.
        tryCompare(SessionModel, "undoText", "Undo Reset Settings");

        // **One step back**, all three at once.
        SessionModel.undo();
        tryVerify(() => SessionModel.param("reset", "gain") === 1.5, testCase.noticeMs, "one undo did not put the volume back");
        compare(SessionModel.param("reset", "overdrive"), 2);
        compare(SessionModel.param("reset", "muted"), true);
    }

    function test_aResetLeavesWhatAPresetLeaves() {
        // **Which ear a hearing block is for is not a setting to reset**:
        // it says which of a pair the block is, and a preset leaves it alone
        // for the same reason.
        verify(BlockCatalogue.describeParam("hearing.compensation16", "ear").inPresets === false,
               "the catalogue does not say the ear is kept out of presets");
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock",
                                              block: { id: "ear", type: "hearing.compensation16",
                                                       params: { ear: "right", band1SoftGainDb: 6 } },
                                              index: -1 }));
        const panel = createTemporaryObject(panelComponent, null);
        panel.open("ear");
        wait(50);
        const footer = panel.presets;
        verify(footer.resettable.indexOf("ear") < 0, "a reset would change which ear the block is for");
        SessionModel.resetParams("ear", footer.resettable);
        tryVerify(() => SessionModel.param("ear", "band1SoftGainDb") === undefined, testCase.noticeMs, "the band was not reset");
        compare(SessionModel.param("ear", "ear"), "right");
    }

    function test_aMeterKeepsTheRowAndItsResetButNotThePresets() {
        // A meter has no use for presets, and still has settings to put
        // back: the row stays, without the box, with the reset alone in its
        // menu.
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "vuReset", type: "meter.vu" }, index: -1 }));
        const window = createTemporaryObject(shownPanel, null);
        const panel = window.panel;
        waitForRendering(panel);
        panel.open("vuReset");
        wait(50);
        const footer = panel.presets;
        verify(footer.visible, "a meter's settings have no footer");
        compare(footer.showsPresets, false);
        compare(footer.chooser.visible, false, "a meter's footer offers presets");
        verify(footer.menuButton.visible, "a meter's footer has no menu");
        verify(footer.resettable.length > 0, "a meter has nothing to reset");
        compare(footer.settingsMenu.count, 1);
        compare(footer.settingsMenu.itemAt(0).action, footer.resetAction);
    }

    function test_aPluginBlockOffersNoReset() {
        // A plugin's settings are more than its parameters (its state), and
        // only the plugin can make that new again.
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "plug", type: "plugin.lv2" }, index: -1 }));
        const panel = createTemporaryObject(panelComponent, null);
        panel.open("plug");
        wait(50);
        const footer = panel.presets;
        compare(footer.resettable.length, 0, "a plugin block offers a reset");
        for (let index = 0; index < footer.presetMenu.count; ++index) {
            verify(footer.presetMenu.itemAt(index).action !== footer.resetAction, "the plugin's menu has the reset in it");
        }
    }

    function test_aBlockWithNoPanelOfItsOwnGetsTheGenericOne() {
        // A block with no entry is not a lesser block: the generic panel is
        // right for anything whose settings are a list of settings, which
        // is most of them. The Dynamics block, the Echo, the Reverb and the
        // Pitch Shift all have panels of their own now, so the block named
        // here is one this build does not have yet.
        compare(BlockPanels.panelFor("advanced.transcribe"), null);
        compare(BlockPanels.panelFor("nosuch.block"), null);

        const panel = createTemporaryObject(panelComponent, null);
        panel.open("b2");
        wait(50);
        verify(paramRowsOf(panel).length > 0, "the block with no panel of its own got the generic one");
    }
}
