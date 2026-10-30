// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The installed plugins in the block library, against the mock
// engine: every plugin an entry of its own under Plugins with its name, who
// made it and its format; only formats this build can host; a filter by
// format; a heading for each format, folding and in the reader's alphabet;
// choosing one making a plugin block set to it; and a rescan, with a retry
// for what could not be read.

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

    function init() {
        WindowState.setCollapsedCategories([]);
    }

    function cleanup() {
        TestEngine.clearFakePlugins();
        WindowState.setCollapsedCategories([]);
    }

    /// Two LADSPA plugins, added out of alphabetical order, beside the
    /// mock's two built-in ones: two formats, each with more than one.
    function addTwoLadspaPlugins() {
        TestEngine.addFakePlugin("ladspa", "zeta.so:zeta", "Zeta Delay", "Somebody");
        TestEngine.addFakePlugin("ladspa", "amp.so:amp_stereo", "amplifier", "Somebody Else");
    }

    /// The rows from the Plugins heading on, as `kind:id` for headings and
    /// the name for a plugin: what a person reads down the list.
    function pluginsPart(library: var): var {
        const start = library.rows.findIndex(row => row.kind === "category" && row.id === "plugins");
        return library.rows.slice(start + 1)
            .filter(row => row.kind === "section" || row.kind === "plugin")
            .map(row => row.kind === "section" ? "section:" + row.format + ":" + row.count : row.name);
    }

    /// The delegate at \a index, scrolled into view.
    function rowAt(library: var, index: int): var {
        library.list.positionViewAtIndex(index, ListView.Beginning);
        library.list.forceLayout();
        tryVerify(() => library.list.itemAtIndex(index) !== null, 2000, "row " + index + " was built");
        return library.list.itemAtIndex(index);
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
        // A plugin of a format no build hosts as well: it is found, and not
        // offered.
        TestEngine.addFakePlugin("dssi", "org.example.eq", "An EQ", "Somebody");
        const window = createTemporaryObject(libraryWindow, testCase);
        const rows = pluginRows(window.library);
        compare(rows.map(row => row.name), ["Test gain", "Test gain, slow to load"]);
        compare(rows[0].vendor, "Reverblabr");
        compare(rows[0].format, "builtin");
        compare(rows[0].type, "plugin.builtin");
        compare(rows[0].id, "plugin.builtin?plugin=test.gain");
        verify(InstalledPlugins.plugin("dssi", "org.example.eq").name === "An EQ", "the plugin was listed");

        // Under the Plugins heading, which counts them: the blocks that
        // pick a plugin (Test Plugin, LV2, LADSPA, and CLAP where the build
        // has it) and the two plugins.
        const heading = window.library.rows.find(row => row.kind === "category" && row.id === "plugins");
        verify(heading !== undefined, "no Plugins heading");
        const pluginBlocks = BlockCatalogue.types.filter(type => type.startsWith("plugin."));
        verify(pluginBlocks.indexOf("plugin.lv2") >= 0, "no LV2 block");
        compare(heading.count, pluginBlocks.length + 2);

        // The row says so: the name, who made it, and the format in its
        // tooltip.
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

    function test_aVst3PluginIsOfferedWhereTheBuildCanHostIt() {
        if (BlockCatalogue.types.indexOf("plugin.vst3") < 0) {
            skip("this build has no VST3 SDK, so no VST3 block");
        }
        TestEngine.addFakePlugin("vst3", "5242565354334741494E53544552454F", "A Gain", "Somebody");
        const window = createTemporaryObject(libraryWindow, testCase);
        const row = pluginRows(window.library).find(row => row.format === "vst3");
        verify(row !== undefined, "the VST3 plugin was not offered");
        compare(row.name, "A Gain");
        compare(row.type, "plugin.vst3");
        verify(window.library.offeredFormats.indexOf("vst3") >= 0, "VST3 is not in the format filter");
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

    function test_thePluginsAreUnderAHeadingForEachFormatInTheReadersAlphabet() {
        // **A heading for each format the scan found**, in the alphabet of
        // the words a person reads, and the plugins under each by name,
        // whatever order the scan found them in and whatever their case.
        testCase.addTwoLadspaPlugins();
        const window = createTemporaryObject(libraryWindow, testCase);
        const library = window.library;
        compare(testCase.pluginsPart(library),
                ["section:builtin:2", "Test gain", "Test gain, slow to load",
                 "section:ladspa:2", "amplifier", "Zeta Delay"]);
        // Each heading straight after the blocks that pick a plugin, which
        // stay where they were, under Plugins itself.
        const first = library.rows.findIndex(row => row.kind === "section");
        compare(library.rows[first - 1].kind, "block");
        compare(library.rows[first - 1].id.startsWith("plugin."), true);
        // The category still counts every plugin, the headings their own.
        const heading = library.rows.find(row => row.kind === "category" && row.id === "plugins");
        const pluginBlocks = BlockCatalogue.types.filter(type => type.startsWith("plugin."));
        compare(heading.count, pluginBlocks.length + 4);

        // **What a person reads and what a screen reader says**: the
        // format's own name and how many, as a plural; a heading, folded
        // or open.
        const ladspaIndex = library.rows.findIndex(row => row.kind === "section" && row.format === "ladspa");
        const row = testCase.rowAt(library, ladspaIndex);
        compare(row.label, "LADSPA");
        compare(row.isHeading, true);
        compare(row.Accessible.role, Accessible.Grouping);
        compare(row.Accessible.name, "LADSPA, 2 plugins");
        compare(row.Accessible.description, "Open");
        // Set in from its category's heading, so the two levels read as two.
        const category = testCase.rowAt(library, library.rows.findIndex(entry => entry.kind === "category"
                                                                                 && entry.id === "plugins"));
        const sectionRow = testCase.rowAt(library, ladspaIndex);
        verify(sectionRow.contentItem.children[0].mapToItem(library, 0, 0).x
               > category.contentItem.children[0].mapToItem(library, 0, 0).x,
               "a format's heading is not set in from its category's");

        library.searchField.text = "zeta";
        const only = library.rows.find(entry => entry.kind === "section");
        compare(only.count, 1);
        const one = testCase.rowAt(library, library.rows.indexOf(only));
        compare(one.Accessible.name, "LADSPA, 1 plugin");
        library.searchField.text = "";
    }

    function test_aFormatsHeadingFoldsItsPluginsAndStaysFolded() {
        testCase.addTwoLadspaPlugins();
        const window = createTemporaryObject(libraryWindow, testCase);
        const library = window.library;
        const ladspa = library.sectionOf("ladspa");

        // Enter on the heading, as on a category's: the list's one key.
        library.activate(library.rows.find(row => row.kind === "section" && row.format === "ladspa"));
        verify(library.isCollapsed(ladspa), "Enter on a format's heading did not fold it");
        compare(testCase.pluginsPart(library),
                ["section:builtin:2", "Test gain", "Test gain, slow to load", "section:ladspa:2"]);
        const index = library.rows.findIndex(row => row.kind === "section" && row.format === "ladspa");
        const row = testCase.rowAt(library, index);
        compare(row.collapsed, true);
        compare(row.Accessible.description, "Folded away");

        // Remembered as the categories are, and the category's own folding
        // is a thing apart.
        const again = createTemporaryObject(libraryWindow, testCase).library;
        verify(again.isCollapsed(ladspa), "the format's heading was open again next time");
        verify(!again.isCollapsed("plugins"), "folding a format folded the category");
        again.toggleCategory("plugins");
        compare(testCase.pluginsPart(again), []);
        again.toggleCategory("plugins");
        again.activate(again.rows.find(entry => entry.kind === "section" && entry.format === "ladspa"));
        compare(testCase.pluginsPart(again).length, 6);
    }

    function test_theSearchLooksUnderEveryFormat() {
        // **Across every heading**: what matches under each, and a format
        // with nothing that matches is not shown.
        testCase.addTwoLadspaPlugins();
        // A window of its own, drawn, for the keys to reach.
        const window = createTemporaryObject(libraryWindow, null);
        const library = window.library;
        waitForRendering(library);
        library.searchField.text = "e";
        compare(testCase.pluginsPart(library),
                ["section:builtin:2", "Test gain", "Test gain, slow to load",
                 "section:ladspa:2", "amplifier", "Zeta Delay"]);
        library.searchField.text = "somebody else";
        compare(testCase.pluginsPart(library), ["section:ladspa:1", "amplifier"]);
        library.searchField.text = "slow";
        compare(testCase.pluginsPart(library), ["section:builtin:1", "Test gain, slow to load"]);

        // Enter in the search adds the first plugin it found, not the
        // heading it is under.
        let chosen = "";
        library.blockChosen.connect(typeId => chosen = typeId);
        library.searchField.forceActiveFocus();
        library.searchField.text = "zeta";
        keyClick(Qt.Key_Return);
        compare(chosen, InstalledPlugins.entryKey("ladspa", "zeta.so:zeta"));
        verify(!library.isCollapsed(library.sectionOf("ladspa")), "Enter in the search folded the heading");
        library.searchField.text = "";
    }

    function test_theKeyboardGoesThroughAFormatsHeadingIntoItsPlugins() {
        testCase.addTwoLadspaPlugins();
        // A window of its own, drawn, for the keys to reach.
        const window = createTemporaryObject(libraryWindow, null);
        const library = window.library;
        waitForRendering(library);
        const index = library.rows.findIndex(row => row.kind === "section" && row.format === "ladspa");
        library.list.forceActiveFocus();
        library.list.currentIndex = index - 1;
        keyClick(Qt.Key_Down);
        compare(library.list.currentIndex, index, "Down did not reach the format's heading");
        keyClick(Qt.Key_Down);
        compare(library.rows[library.list.currentIndex].name, "amplifier");
        keyClick(Qt.Key_Up);
        keyClick(Qt.Key_Return);
        verify(library.isCollapsed(library.sectionOf("ladspa")), "Enter on the heading did not fold it");
        compare(library.rows[library.list.currentIndex].kind, "section", "the current row moved off the heading");
        keyClick(Qt.Key_Return);
        verify(!library.isCollapsed(library.sectionOf("ladspa")), "and Enter again did not open it");
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
