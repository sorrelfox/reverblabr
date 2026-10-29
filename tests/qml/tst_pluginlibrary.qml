// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The installed plugins in the block library, against the mock
// engine: every plugin an entry of its own under Plugins with its name, who
// made it and its format; only formats this build can host; a filter by
// format; choosing one making a plugin block set to it; and a rescan, with
// a retry for what could not be read.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "PluginLibrary"
    when: windowShown

    property string sessionId: ""

    Component {
        id: libraryWindow

        Window {
            property alias library: inner

            width: 320
            height: 900
            visible: true

            BlockLibrary {
                id: inner

                anchors.fill: parent
                mimeType: "application/x-reverblabr-blocktype"
            }
        }
    }

    Component {
        id: faceComponent

        PluginFace {
            width: 220
            height: 80
        }
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Plugin library");
        verify(testCase.sessionId !== "", "the mock opened a session");
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
        tryVerify(() => InstalledPlugins.ready, 2000, "the engine answered with its plugins");
    }

    function cleanup() {
        TestEngine.clearFakePlugins();
    }

    function pluginRows(library: var): var {
        return library.rows.filter(row => row.kind === "plugin");
    }

    /// **The plugins' row lines up with the search field**: the
    /// format box and the rescan button share its left and right edges,
    /// rather than sitting nearer the sidebar's edge as the list does.
    function test_thePluginToolsLineUpWithTheSearchField() {
        const window = createTemporaryObject(libraryWindow, testCase);
        const library = window.library;
        waitForRendering(library);
        verify(library.formatFilter.visible, "there are plugins, so the format box is shown");
        const search = library.searchField;
        const searchLeft = search.mapToItem(library, 0, 0).x;
        const searchRight = search.mapToItem(library, search.width, 0).x;
        const boxLeft = library.formatFilter.mapToItem(library, 0, 0).x;
        const buttonRight = library.rescanButton.mapToItem(library, library.rescanButton.width, 0).x;
        fuzzyCompare(boxLeft, searchLeft, 0.5, "the format box's left edge against the search field's");
        fuzzyCompare(buttonRight, searchRight, 0.5, "the rescan button's right edge against the search field's");
    }

    function test_everyPluginIsAnEntryWithItsNameVendorAndFormat() {
        // A VST3 plugin as well, which no build can host yet: it is found,
        // and not offered.
        TestEngine.addFakePlugin("vst3", "org.example.eq", "An EQ", "Somebody");
        const window = createTemporaryObject(libraryWindow, testCase);
        const rows = pluginRows(window.library);
        compare(rows.map(row => row.name), ["Test gain", "Test gain, slow to load"]);
        compare(rows[0].vendor, "Reverblabr");
        compare(rows[0].format, "builtin");
        compare(rows[0].type, "plugin.builtin");
        compare(rows[0].id, "plugin.builtin?plugin=test.gain");
        verify(InstalledPlugins.plugin("vst3", "org.example.eq").name === "An EQ", "the VST3 plugin was listed");

        // Under the Plugins heading, which counts them: the blocks that
        // pick a plugin (Test Plugin, LV2, LADSPA, and CLAP where the build
        // has it) and the two plugins.
        const heading = window.library.rows.find(row => row.kind === "category" && row.id === "plugins");
        verify(heading !== undefined, "no Plugins heading");
        const pluginBlocks = BlockCatalogue.types.filter(type => type.startsWith("plugin."));
        verify(pluginBlocks.indexOf("plugin.lv2") >= 0, "no LV2 block");
        compare(heading.count, pluginBlocks.length + 2);

        // The row says so: the name, who made it, and a badge.
        const index = window.library.rows.findIndex(row => row.kind === "plugin");
        window.library.list.positionViewAtIndex(index, 0);
        wait(50);
        const row = window.library.list.itemAtIndex(index);
        verify(row !== null, "the plugin's row was not built");
        compare(row.label, "Test gain");
        compare(row.detail, "Reverblabr");
        compare(row.blockType, "plugin.builtin");
        verify(row.toolTipText.indexOf("Built-in") >= 0, row.toolTipText);
    }

    function test_aClapPluginIsOfferedWhereTheBuildCanHostIt() {
        if (BlockCatalogue.types.indexOf("plugin.clap") < 0) {
            skip("this build has no CLAP SDK, so no CLAP block");
        }
        TestEngine.addFakePlugin("clap", "org.example.comp", "A Compressor", "Somebody");
        const window = createTemporaryObject(libraryWindow, testCase);
        const row = pluginRows(window.library).find(row => row.format === "clap");
        verify(row !== undefined, "the CLAP plugin was not offered");
        compare(row.name, "A Compressor");
        compare(row.type, "plugin.clap");
        verify(window.library.offeredFormats.indexOf("clap") >= 0, "CLAP is not in the format filter");
    }

    function test_theSearchAndTheFormatFilterNarrowThePlugins() {
        const window = createTemporaryObject(libraryWindow, testCase);
        window.library.searchField.text = "slow";
        compare(pluginRows(window.library).map(row => row.name), ["Test gain, slow to load"]);
        window.library.searchField.text = "";

        compare(window.library.offeredFormats, ["builtin"], "no LV2 plugin is installed in the mock");
        verify(window.library.formatFilter.visible, "no format filter");
        window.library.pluginFormat = "lv2";
        compare(pluginRows(window.library).length, 0);
        window.library.pluginFormat = "builtin";
        compare(pluginRows(window.library).length, 2);
        window.library.pluginFormat = "";
    }

    function test_choosingAPluginMakesABlockSetToIt() {
        const before = SessionModel.blockIds.length;
        const added = SessionModel.addBlock(InstalledPlugins.entryKey("builtin", "test.slowgain"), Qt.point(0, 0));
        verify(added !== "", "nothing was added");
        compare(SessionModel.blockIds.length, before + 1);
        compare(SessionModel.block(added).type, "plugin.builtin");
        compare(SessionModel.param(added, "plugin"), "test.slowgain");

        // And the face says which plugin, by the name the scan found.
        const face = createTemporaryObject(faceComponent, null);
        face.blockId = added;
        compare(face.titleLabel.text, "Test gain, slow to load");
        SessionModel.removeBlock(added);
    }

    function test_aLadspaPluginIsOfferedAndChoosingItMakesALadspaBlock() {
        // Its id is the library's file name and the plugin's label.
        TestEngine.addFakePlugin("ladspa", "amp.so:amp_stereo", "Stereo Amplifier", "Richard Furse", 2, 2,
                                 JSON.stringify([{ id: "gain", name: "Gain", kind: "float", min: 0, max: 2,
                                                   default: 1, log: false }]));
        const window = createTemporaryObject(libraryWindow, testCase);
        compare(window.library.offeredFormats, ["builtin", "ladspa"]);
        window.library.pluginFormat = "ladspa";
        const rows = pluginRows(window.library);
        compare(rows.map(row => row.name), ["Stereo Amplifier"]);
        compare(rows[0].type, "plugin.ladspa");
        window.library.pluginFormat = "";

        const added = SessionModel.addBlock(InstalledPlugins.entryKey("ladspa", "amp.so:amp_stereo"), Qt.point(0, 0));
        verify(added !== "", "nothing was added");
        compare(SessionModel.block(added).type, "plugin.ladspa");
        compare(SessionModel.param(added, "plugin"), "amp.so:amp_stereo");
        const face = createTemporaryObject(faceComponent, null);
        face.blockId = added;
        compare(face.titleLabel.text, "Stereo Amplifier");
        SessionModel.removeBlock(added);
    }

    // A file a scan got only part of the way through is said to be
    // missing plugins, and the link looks again (not a retry of the
    // blocklisted).
    function test_aScanCutShortIsSaidAndLooksAgain() {
        const window = createTemporaryObject(libraryWindow, testCase);
        verify(!window.library.incompleteNote.visible, "a note with nothing cut short");
        TestEngine.addFakeIncompletePlugins("clap", "/somewhere/many.clap");
        tryVerify(() => window.library.incompleteNote.visible, 1000, "a scan cut short is not said");
        verify(window.library.incompleteNote.text.indexOf("Some plugins in 1 file may be missing") === 0,
               window.library.incompleteNote.text);
        verify(!window.library.blockedNote.visible, "a file cut short is not one that could not be read");
        const rescans = TestEngine.fakeRescans();
        window.library.incompleteNote.linkActivated("again");
        compare(TestEngine.fakeRescans(), rescans + 1);
        compare(TestEngine.fakeLastRescanRetried(), false);
        tryVerify(() => !InstalledPlugins.scanning, 1000, "the rescan never ended");
    }

    function test_aRescanAndARetryReachTheEngine() {
        TestEngine.addFakeBlockedPlugin("lv2", "/somewhere/broken.lv2", "crashed");
        const window = createTemporaryObject(libraryWindow, testCase);
        verify(window.library.blockedNote.visible, "what could not be read is not said");
        verify(window.library.blockedNote.text.indexOf("1 plugin could not be read") === 0,
               window.library.blockedNote.text);

        const rescans = TestEngine.fakeRescans();
        window.library.rescanButton.clicked();
        compare(TestEngine.fakeRescans(), rescans + 1);
        compare(TestEngine.fakeLastRescanRetried(), false);
        window.library.blockedNote.linkActivated("retry");
        compare(TestEngine.fakeRescans(), rescans + 2);
        compare(TestEngine.fakeLastRescanRetried(), true);
        tryVerify(() => !InstalledPlugins.scanning, 1000, "the rescan never ended");
    }
}
