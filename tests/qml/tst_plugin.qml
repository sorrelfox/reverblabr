// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// A plugin block's face and panel: what its host is doing, in red
// when it crashed or stopped responding, a Restart that reaches the engine,
// and the plugin's settings still there while it is down. The badge the
// engine's error puts on its tile is in tests/unit/app/test_errorsurface.cpp.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "Plugin"

    property string sessionId: ""
    property var errors: []

    Connections {
        target: TestEngine

        function onError(code, argsJson) {
            testCase.errors.push({ code: code, args: JSON.parse(argsJson) });
        }
    }

    Component {
        id: faceComponent

        PluginFace {
            width: 220
            height: 80
        }
    }

    Component {
        id: panelComponent

        PluginPanel {
            width: 340
        }
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Plugins");
        verify(testCase.sessionId !== "", "the mock opened a session");
        TestEngine.applyEdit(testCase.sessionId,
                             JSON.stringify({ op: "addBlock", block: { id: "p", type: "plugin.builtin" }, index: -1 }));
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        testCase.errors = [];
    }

    function test_itIsUnderPluginsWithAFaceAndAPanel() {
        verify(BlockCatalogue.types.indexOf("plugin.builtin") >= 0, "the engine does not offer it");
        compare(BlockPresentation.category("plugin.builtin"), "plugins");
        verify(BlockFaces.faceFor("plugin.builtin") !== null, "it has no face");
        verify(BlockPanels.panelFor("plugin.builtin") !== null, "it has no panel");
    }

    function test_theFaceSaysWhichPluginAndWhatItsHostIsDoing() {
        const face = createTemporaryObject(faceComponent, null);
        face.blockId = "p";
        // The name the scan found for it, not its id.
        compare(face.titleLabel.text, "Test gain");
        face.published = [2];
        compare(face.statusLabel.text, "Running");
        compare(face.statusLabel.color, AppColors.muted);
        face.published = [3];
        compare(face.statusLabel.text, "Plugin crashed");
        compare(face.statusLabel.color, AppColors.dangerText);
        face.published = [4];
        compare(face.statusLabel.text, "Plugin stopped responding");
    }

    function test_aPluginThatIsDownOffersARestartThatReachesTheEngine() {
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "p";
        panel.published = [2];
        compare(panel.restartButton.visible, false);
        compare(panel.statusLabel.text, "Running");

        panel.published = [3];
        compare(panel.restartButton.visible, true);
        verify(panel.statusLabel.text.indexOf("Plugin crashed") === 0, panel.statusLabel.text);
        compare(panel.statusLabel.color, AppColors.dangerText);

        // The mock is not running the session, so it answers the way the
        // engine would: refused, because it is not running. What matters is
        // that the click reached it, for this block.
        panel.restartButton.clicked();
        tryVerify(() => testCase.errors.length > 0, 1000, "the restart never reached the engine");
        compare(testCase.errors[0].code, "plugin.restartRefused");
        compare(testCase.errors[0].args.block, "p");
        compare(testCase.errors[0].args.reason, "NotRunning");

        panel.published = [4];
        verify(panel.statusLabel.text.indexOf("Plugin stopped responding") === 0, panel.statusLabel.text);
    }

    // --- LV2 ---

    readonly property string lv2Params: JSON.stringify([
        { id: "g_out", name: "Output gain", kind: "float", min: 0, max: 10, default: 1, log: true },
        { id: "on", name: "Enabled", kind: "toggle", min: 0, max: 1, default: 1 },
        { id: "scm", name: "Sidechain mode", kind: "choice", min: 0, max: 2, default: 1,
          choices: [{ value: 0, label: "Peak" }, { value: 1, label: "RMS" }, { value: 2, label: "LPF" }] }
    ])

    function addLv2(id: string, uri: string): string {
        TestEngine.applyEdit(testCase.sessionId, JSON.stringify({
            op: "addBlock", block: { id: id, type: "plugin.lv2", params: { plugin: uri } }, index: -1 }));
        return id;
    }

    function test_anLv2PluginsPanelHasItsSettingsByItsOwnNames() {
        TestEngine.addFakePlugin("lv2", "urn:test:comp", "A Compressor", "Somebody", 2, 2, testCase.lv2Params);
        const blockId = addLv2("lv2comp", "urn:test:comp");
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = blockId;
        compare(panel.format, "lv2");
        tryVerify(() => panel.paramRows.count === 3, 2000, "the plugin's settings never came");
        compare(panel.paramRows.itemAt(0).label, "Output gain");
        compare(panel.paramRows.itemAt(1).paramType, "bool");
        const choice = panel.paramRows.itemAt(2);
        compare(choice.label, "Sidechain mode");
        compare(choice.descriptor.choiceLabels, ["Peak", "RMS", "LPF"]);
        compare(choice.activeControl.currentIndex, 1, "the plugin's default, RMS");
        compare(choice.activeControl.currentText, "RMS");

        // Choosing sets the choice by the value the plugin block keeps.
        choice.activeControl.currentIndex = 2;
        choice.activeControl.activated(2);
        compare(SessionModel.param(blockId, "scm"), "2");
        compare(panel.monoChooser.visible, false, "a stereo plugin has no mono question");
        TestEngine.clearFakePlugins();
    }

    function test_aPluginIsChosenInThePanelOfABlockWithNone() {
        TestEngine.addFakePlugin("lv2", "urn:test:a", "Plugin A", "Somebody", 2, 2, testCase.lv2Params);
        TestEngine.addFakePlugin("lv2", "urn:test:mono", "A Mono One", "Somebody", 1, 1, testCase.lv2Params);
        const blockId = addLv2("lv2none", "");
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = blockId;
        const face = createTemporaryObject(faceComponent, null);
        face.blockId = blockId;
        compare(face.titleLabel.text, "Choose a plugin");
        verify(panel.pluginChooser.visible, "no chooser");
        compare(panel.pluginChooser.displayText, "Choose a plugin…");
        compare(panel.pluginChooser.model, ["A Mono One", "Plugin A"]);

        panel.pluginChooser.activated(0);
        compare(SessionModel.param(blockId, "plugin"), "urn:test:mono");
        compare(face.titleLabel.text, "A Mono One");
        // A mono plugin asks what it does on a stereo lane.
        verify(panel.monoChooser.visible, "no mono question for a mono plugin");
        panel.monoChooser.activated(1);
        compare(SessionModel.param(blockId, "monoMode"), "downmix");
        TestEngine.clearFakePlugins();
    }

    function test_thePluginsOwnWindowAndPresetsFollowTheEnginesAnswers() {
        TestEngine.addFakePlugin("lv2", "urn:test:own", "Own Side", "Somebody", 2, 2, testCase.lv2Params);
        const blockId = addLv2("lv2own", "urn:test:own");
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = blockId;
        panel.published = [2];
        verify(panel.windowButton.visible, "an LV2 plugin offers its window");
        verify(panel.windowButton.enabled, "a running one can show it");

        // Not running: the engine says so, and the button stays up.
        panel.windowButton.clicked();
        tryVerify(() => SessionModel.pluginWindowReason(blockId) === "notRunning", 1000, "no answer");
        compare(panel.windowButton.checked, false);

        TestEngine.start(testCase.sessionId);
        panel.windowButton.clicked();
        tryVerify(() => panel.windowButton.checked, 1000, "the window the engine said was showing");
        panel.windowButton.clicked();
        tryVerify(() => !panel.windowButton.checked, 1000, "the window the engine said had closed");
        compare(panel.windowNote.visible, false, "a window closed by a person needs no words");

        // The plugin's own presets: one saved is listed, and shown.
        SessionModel.savePluginPreset(blockId, "Gentle");
        tryVerify(() => panel.ownPresets.length === 1, 1000, "the saved preset is not listed");
        compare(panel.pluginPresetChooser.currentText, "Gentle");
        verify(panel.savePluginPresetButton.visible, "an LV2 plugin's presets can be saved here");
        TestEngine.stop(testCase.sessionId);
        TestEngine.clearFakePlugins();
    }

    // --- CLAP ---

    function test_aClapPluginsBlockIsThePluginPanelAndFaceWithItsSettings() {
        if (BlockCatalogue.types.indexOf("plugin.clap") < 0) {
            skip("this build has no CLAP SDK, so no CLAP block");
        }
        verify(BlockPanels.panelFor("plugin.clap") === BlockPanels.panelFor("plugin.lv2"),
               "a CLAP block has the plugin panel");
        verify(BlockFaces.faceFor("plugin.clap") === BlockFaces.faceFor("plugin.lv2"),
               "a CLAP block has the plugin face");
        // A CLAP plugin's settings are named by the plugin's own numbers.
        const clapParams = JSON.stringify([
            { id: "3630838676", name: "Output gain", kind: "float", min: -80, max: 60, default: 0 },
            { id: "3810080779", name: "Sidechain mode", kind: "choice", min: 0, max: 1, default: 0.333,
              choices: [{ value: 0, label: "Peak" }, { value: 0.5, label: "RMS" }, { value: 1, label: "SMA" }] }
        ]);
        TestEngine.addFakePlugin("clap", "org.example.comp", "A Compressor", "Somebody", 2, 2, clapParams);
        TestEngine.applyEdit(testCase.sessionId, JSON.stringify({
            op: "addBlock", block: { id: "clapcomp", type: "plugin.clap", params: { plugin: "org.example.comp" } },
            index: -1 }));
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "clapcomp";
        compare(panel.format, "clap");
        tryVerify(() => panel.paramRows.count === 2, 2000, "the plugin's settings never came");
        compare(panel.paramRows.itemAt(0).label, "Output gain");
        const choice = panel.paramRows.itemAt(1);
        compare(choice.descriptor.choiceLabels, ["Peak", "RMS", "SMA"]);
        compare(choice.activeControl.currentText, "RMS", "the nearest to the plugin's default");
        choice.activeControl.currentIndex = 2;
        choice.activeControl.activated(2);
        compare(SessionModel.param("clapcomp", "3810080779"), "1");
        TestEngine.clearFakePlugins();
    }

    // --- VST3 ---

    function test_aVst3PluginsBlockIsThePluginPanelWithItsWindowAndPresetFiles() {
        if (BlockCatalogue.types.indexOf("plugin.vst3") < 0) {
            skip("this build has no VST3 SDK, so no VST3 block");
        }
        verify(BlockPanels.panelFor("plugin.vst3") === BlockPanels.panelFor("plugin.lv2"),
               "a VST3 block has the plugin panel");
        verify(BlockFaces.faceFor("plugin.vst3") === BlockFaces.faceFor("plugin.lv2"),
               "a VST3 block has the plugin face");
        // Named by the plugin's own numbers; a value is the plain one.
        const vst3Params = JSON.stringify([
            { id: "1", name: "Gain", kind: "float", min: 0, max: 4, default: 1 },
            { id: "2", name: "Colour", kind: "choice", min: 0, max: 2, default: 0,
              choices: [{ value: 0, label: "Clean" }, { value: 1, label: "Warm" }, { value: 2, label: "Hot" }] }
        ]);
        const classId = "5242565354334741494E53544552454F";
        TestEngine.addFakePlugin("vst3", classId, "A Gain", "Somebody", 2, 2, vst3Params);
        TestEngine.applyEdit(testCase.sessionId, JSON.stringify({
            op: "addBlock", block: { id: "vst3gain", type: "plugin.vst3", params: { plugin: classId } },
            index: -1 }));
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "vst3gain";
        panel.published = [2];
        compare(panel.format, "vst3");
        tryVerify(() => panel.paramRows.count === 2, 2000, "the plugin's settings never came");
        compare(panel.paramRows.itemAt(0).label, "Gain");
        compare(panel.paramRows.itemAt(1).descriptor.choiceLabels, ["Clean", "Warm", "Hot"]);
        verify(panel.windowButton.visible, "a VST3 plugin offers its view");
        verify(panel.savesOwnPresets, "a VST3 plugin's presets are files other programs read");
        TestEngine.clearFakePlugins();
    }

    function test_theSettingsStayWhileThePluginIsDown() {
        const panel = createTemporaryObject(panelComponent, null);
        panel.blockId = "p";
        panel.published = [3];
        verify(BlockCatalogue.params("plugin.builtin").some(p => p.id === "gain"), "the plugin's gain is a setting");
        SessionModel.setParam("p", "gain", 2.5);
        compare(SessionModel.param("p", "gain"), 2.5);
    }
}
