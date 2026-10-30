// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.ki18n

// The Blocks tab of the sidebar: what a session can be built out of.
//
// **Only the blocks this build has.** The presentation table covers the
// whole planned catalogue so that a session from a later version still
// shows words rather than blank rows, but a library offering a block that
// cannot be made is a library that lies. So the rows are the engine's
// catalogue, and the words come from the table.
//
// **Headings and blocks in one list.** The arrow keys, the focus ring and
// Enter come from the list for nothing, and somebody using the keyboard
// moves through the library the way it is drawn. A folded category simply
// leaves its blocks out of the list.
//
// **The plugins a format at a time.** A machine with a few hundred plugins
// installed made Plugins one list longer than every other category put
// together; under a heading of its own for each format the scan found,
// each folding like a category, a person goes to the format they want and
// folds away the ones they never use. The search still looks through all
// of them, and a format with nothing that matches is left out.
//
// **An empty category is not shown at all.** A heading with nothing under
// it is a heading that says this build is missing something, which is a
// thing to say in release notes and not in a sidebar.
Item {
    id: library

    /// What a block type travels under when it is dragged onto the canvas.
    /// **The canvas names it**, being the other half of the gesture.
    required property string mimeType

    /// A block was clicked or Enter was pressed on it. Where it goes is the
    /// canvas's business: in automatic-connections mode it follows whatever
    /// is selected.
    signal blockChosen(string typeId)

    /// The parts, for a test and for the window's Ctrl+F.
    readonly property alias searchField: search
    readonly property alias list: list
    readonly property alias formatFilter: formatFilter
    readonly property alias rescanButton: rescanButton
    readonly property alias blockedNote: blockedNote
    readonly property alias incompleteNote: incompleteNote

    /// How far in from the list's edges the search field and the tools
    /// under it sit: the prototype's search inset, less the list's own.
    readonly property real toolsMargin: AppMetrics.searchMargin - AppMetrics.libraryInset

    /// **Which format's plugins the library shows**, or every one.
    property string pluginFormat: ""

    /// The plugins the library can offer: those of a format this build
    /// has a block for, and of the format chosen. **A plugin whose format
    /// cannot be hosted yet is not offered**, for the reason an unbuilt
    /// block is not: a library entry that cannot be made is a library that
    /// lies.
    readonly property var offeredPlugins: {
        const available = BlockCatalogue.ready ? BlockCatalogue.types : [];
        return InstalledPlugins.plugins.filter(plugin => available.indexOf(plugin.type) >= 0
                                               && (library.pluginFormat === ""
                                                   || plugin.format === library.pluginFormat));
    }
    /// The formats the filter offers: those there are plugins of that can
    /// be hosted.
    readonly property var offeredFormats: {
        const available = BlockCatalogue.ready ? BlockCatalogue.types : [];
        return InstalledPlugins.formats.filter(format => available.indexOf(InstalledPlugins.blockType(format)) >= 0);
    }

    /// The categories that are folded away. Remembered between runs, so a
    /// person who works in one corner of the catalogue finds it the way
    /// they left it.
    property var collapsedCategories: WindowState.collapsedCategories()

    /// Every line the list shows, headings and blocks together. Rebuilt
    /// when the search text, the catalogue or the folding changes — which
    /// is what a person does now and again, not sixty times a second.
    ///
    /// **While something is typed, the best match comes first**: inside
    /// each category, and the categories themselves in the order of the
    /// best each holds, so the first block in the list is the one Enter
    /// adds. "Peak" added Output Device, whose description says
    /// "speakers", because Outputs comes before Meters. With nothing typed
    /// it is the catalogue in its own order, as before.
    readonly property var rows: {
        const available = BlockCatalogue.ready ? BlockCatalogue.types : [];
        const needle = search.text.toLocaleLowerCase();
        const categories = [];
        for (const [order, category] of BlockPresentation.categories.entries()) {
            const found = [];
            for (const typeId of available) {
                if (BlockPresentation.category(typeId) !== category) {
                    continue;
                }
                const rank = library.rankOf(typeId, needle);
                if (rank >= 0) {
                    found.push({ id: typeId, rank: rank, name: BlockPresentation.name(typeId) });
                }
            }
            // **Every plugin is an entry of its own** under Plugins, after
            // the blocks that are there to pick one unless it is a better
            // match than they are.
            const plugins = [];
            if (category === "plugins") {
                for (const entry of library.pluginWords) {
                    const rank = library.pluginRankOf(entry, needle);
                    if (rank >= 0) {
                        plugins.push({ plugin: entry.plugin, rank: rank });
                    }
                }
            }
            if (found.length === 0 && plugins.length === 0) {
                continue;
            }
            // **In the reader's alphabet**, not in the engine's id order:
            // "Input Device" and "source.device" sort differently, and it
            // is the name that is being read.
            found.sort((first, second) => first.rank - second.rank || first.name.localeCompare(second.name));
            categories.push({ id: category, order: order, found: found, plugins: plugins,
                              best: Math.min(library.bestOf(found), library.bestOf(plugins)) });
        }
        categories.sort((first, second) => first.best - second.best || first.order - second.order);

        const built = [];
        for (const category of categories) {
            built.push({ kind: "category", id: category.id, count: category.found.length + category.plugins.length });
            if (library.isCollapsed(category.id)) {
                continue;
            }
            // The category's own blocks, then a heading for each format;
            // **a format with a better match goes before the blocks**, so
            // the first entry under Plugins is still the best one there.
            const groups = library.byFormat(category.plugins);
            const blocksGroup = { format: "", plugins: [], best: library.bestOf(category.found) };
            const blocksAt = groups.findIndex(group => group.best >= blocksGroup.best);
            groups.splice(blocksAt < 0 ? groups.length : blocksAt, 0, blocksGroup);
            for (const group of groups) {
                if (group === blocksGroup) {
                    for (const block of category.found) {
                        built.push({ kind: "block", id: block.id, count: 0 });
                    }
                    continue;
                }
                const section = library.sectionOf(group.format);
                built.push({ kind: "section", id: section, format: group.format, count: group.plugins.length });
                if (library.isCollapsed(section)) {
                    continue;
                }
                for (const ranked of group.plugins) {
                    const plugin = ranked.plugin;
                    built.push({ kind: "plugin", id: plugin.key, count: 0, name: plugin.name, vendor: plugin.vendor,
                                 format: plugin.format, type: plugin.type, category: plugin.category,
                                 audioInputs: plugin.audioInputs, audioOutputs: plugin.audioOutputs });
                }
            }
        }
        return built;
    }

    /// The words a plugin is searched by, lower-cased once when the
    /// plugins change rather than on every key: a few hundred plugins is
    /// a few hundred names, makers and kinds to lower-case each time.
    readonly property var pluginWords: library.offeredPlugins.map(plugin => ({
        plugin: plugin,
        name: plugin.name.toLocaleLowerCase(),
        vendor: plugin.vendor.toLocaleLowerCase(),
        category: plugin.category.toLocaleLowerCase()
    }))

    /// The best rank among \a entries, or one past the worst for none.
    function bestOf(entries: var): int {
        let best = library.noRank;
        for (const entry of entries) {
            best = Math.min(best, entry.rank);
        }
        return best;
    }

    /// One past the worst rank a match can have.
    readonly property int noRank: 5

    /// What a format's heading under Plugins is folded away as. **Beside
    /// the categories' ids**, so the folding is remembered the same way;
    /// the slash keeps it from ever being taken for one.
    function sectionOf(format: string): string {
        return "plugins/" + format;
    }

    /// \a plugins, ranked as `{ plugin, rank }`, a format at a time:
    /// `[{ format, plugins, best }]`. **The best match first**, and among
    /// equals in the reader's alphabet, as the blocks are: the scan's
    /// order is the order it found the files in. With nothing typed every
    /// rank is the same, and the formats and plugins are in alphabetical
    /// order.
    function byFormat(plugins: var): var {
        const groups = [];
        for (const ranked of plugins) {
            let group = groups.find(entry => entry.format === ranked.plugin.format);
            if (group === undefined) {
                group = { format: ranked.plugin.format, plugins: [], best: library.noRank };
                groups.push(group);
            }
            group.plugins.push(ranked);
            group.best = Math.min(group.best, ranked.rank);
        }
        for (const group of groups) {
            group.plugins.sort((first, second) =>
                first.rank - second.rank || first.plugin.name.localeCompare(second.plugin.name));
        }
        groups.sort((first, second) => first.best - second.best
            || PluginWords.formatName(first.format).localeCompare(PluginWords.formatName(second.format)));
        return groups;
    }

    /// Whether \a entry is a heading: a category, or a format under Plugins.
    function isHeading(entry: var): bool {
        return entry.kind === "category" || entry.kind === "section";
    }

    /// How well \a typeId answers \a needle, best first: 0 when its name
    /// starts with it, 1 when a later word of the name does, 2 when the
    /// name has it inside a word, 3 when a word of the description starts
    /// with it, 4 when the description has it anywhere; -1 when it is not
    /// a match. **The name and the description both**, because half of
    /// what somebody types is what a block does rather than what it is
    /// called, **but the name first**: "eq" is the EQ blocks before
    /// Frequency Lowering, and both before every description that says
    /// "frequency". Lower-cased in the reader's locale, so that a Turkish
    /// dotless i matches the way a Turkish reader expects.
    function rankOf(typeId: string, needle: string): int {
        if (needle === "") {
            return 0;
        }
        const name = library.placeIn(BlockPresentation.name(typeId).toLocaleLowerCase(), needle);
        if (name >= 0) {
            return name;
        }
        return library.describedRank(library.placeIn(BlockPresentation.description(typeId).toLocaleLowerCase(),
                                                     needle));
    }

    /// The same for a plugin, from its pluginWords entry: by its name
    /// first, then by who made it or what kind of plugin it says it is.
    function pluginRankOf(entry: var, needle: string): int {
        if (needle === "") {
            return 0;
        }
        const name = library.placeIn(entry.name, needle);
        if (name >= 0) {
            return name;
        }
        const vendor = library.placeIn(entry.vendor, needle);
        const category = library.placeIn(entry.category, needle);
        return library.describedRank(vendor < 0 ? category : category < 0 ? vendor : Math.min(vendor, category));
    }

    /// The rank of a match outside the name, from where in the words it
    /// was: a word that starts with what was typed before a word that only
    /// has it inside.
    function describedRank(place: int): int {
        return place < 0 ? -1 : place < 2 ? 3 : 4;
    }

    /// Where \a needle is in \a text: 0 at the start, 1 at the start of a
    /// later word, 2 only inside a word, -1 not at all.
    function placeIn(text: string, needle: string): int {
        let at = text.indexOf(needle);
        if (at < 0) {
            return -1;
        }
        if (at === 0) {
            return 0;
        }
        for (; at >= 0; at = text.indexOf(needle, at + 1)) {
            if (!library.isWordCharacter(text[at - 1])) {
                return 1;
            }
        }
        return 2;
    }

    /// Whether \a character is part of a word: a letter of any alphabet
    /// that has capitals, or a digit. "Peak/RMS" is two words, and so is
    /// "10-Band EQ" three.
    function isWordCharacter(character: string): bool {
        return character.toLocaleUpperCase() !== character.toLocaleLowerCase() || (character >= "0" && character <= "9");
    }

    function isCollapsed(category: string): bool {
        return library.collapsedCategories.indexOf(category) >= 0;
    }

    /// Folds a category away, or opens it, and writes it down.
    function toggleCategory(category: string) {
        library.collapsedCategories = library.isCollapsed(category)
            ? library.collapsedCategories.filter(id => id !== category)
            : library.collapsedCategories.concat([category]);
        WindowState.setCollapsedCategories(library.collapsedCategories);
    }

    /// What Enter, a click and a double click all mean, whichever line they
    /// landed on.
    function activate(entry: var) {
        if (entry === undefined) {
            return;
        }
        if (library.isHeading(entry)) {
            library.toggleCategory(entry.id);
            // **The keyboard stays on the heading it folded.** The list is
            // built again, which put the current row back at the top: a
            // format's heading far down Plugins, folded with Enter, sent
            // somebody back to Sources to find their way down again.
            if (list.activeFocus) {
                list.currentIndex = library.rows.findIndex(row => row.kind === entry.kind && row.id === entry.id);
                list.positionViewAtIndex(list.currentIndex, ListView.Contain);
            }
            return;
        }
        library.blockChosen(entry.id);
    }

    /// Puts the cursor in the search field, which is what Ctrl+F does.
    function focusSearch() {
        search.forceActiveFocus();
        search.selectAll();
    }

    ColumnLayout {
        anchors.fill: parent
        // The window's margin, the same as the tab bar above it. In
        // the Reverblabr appearance the prototype's: the list six pixels in
        // from the sidebar's edges and the search field ten.
        anchors.margins: AppMetrics.libraryInset
        anchors.topMargin: AppMetrics.searchMargin
        spacing: AppMetrics.followsSystem ? Kirigami.Units.smallSpacing : AppMetrics.mediumSpacing

        Kirigami.SearchField {
            id: search

            // **Ctrl+F is the window's**, which also shows the Blocks tab.
            // The field's own Ctrl+F made the key ambiguous, and an
            // ambiguous shortcut does nothing: the cursor stayed where it
            // was and the next name was typed onto the end of the last.
            focusSequences: []
            Layout.fillWidth: true
            Layout.leftMargin: library.toolsMargin
            Layout.rightMargin: library.toolsMargin

            // The prototype's search field is a control's full 28 pixels.
            Binding on implicitHeight {
                when: !AppMetrics.followsSystem
                value: AppMetrics.controlHeight
            }
            placeholderText: KI18n.i18nc("@info:placeholder", "Search blocks")
            Accessible.name: KI18n.i18nc("@label:textbox", "Search blocks")
            // Down out of the field and into the list, so that typing a few
            // letters and pressing Down is the whole gesture.
            KeyNavigation.down: list
            // **The first block in the list**, which is the best match
            // (rows), not the first row: that is the heading the block is
            // under, and Enter folded it. Nor a format's heading under
            // Plugins, for the same reason.
            Keys.onReturnPressed: library.activate(library.rows.find(entry => !library.isHeading(entry)))
            Keys.onEnterPressed: library.activate(library.rows.find(entry => !library.isHeading(entry)))
        }

        // **The plugins' own row**: which format to show, a rescan,
        // and what could not be read. Only when there are plugins, or
        // candidates that failed.
        RowLayout {
            id: pluginTools

            Layout.fillWidth: true
            // **Lined up with the search field**, not with the list.
            Layout.leftMargin: library.toolsMargin
            Layout.rightMargin: library.toolsMargin
            visible: library.offeredFormats.length > 0 || InstalledPlugins.blocked.length > 0
                     || InstalledPlugins.incomplete.length > 0
            spacing: Kirigami.Units.smallSpacing

            QQC2.ComboBox {
                id: formatFilter

                Layout.fillWidth: true
                model: [""].concat(library.offeredFormats)
                displayText: library.pluginFormat === ""
                    ? KI18n.i18nc("@item:inlistbox show plugins of every format", "All formats")
                    : PluginWords.formatName(library.pluginFormat)
                delegate: QQC2.ItemDelegate {
                    required property string modelData

                    width: ListView.view.width
                    text: modelData === ""
                        ? KI18n.i18nc("@item:inlistbox show plugins of every format", "All formats")
                        : PluginWords.formatName(modelData)
                }
                onActivated: index => library.pluginFormat = formatFilter.model[index]
                Accessible.name: KI18n.i18nc("@label:listbox", "Plugin format")
            }

            QQC2.ToolButton {
                id: rescanButton

                icon.name: "view-refresh"
                enabled: !InstalledPlugins.scanning
                display: QQC2.AbstractButton.IconOnly
                text: InstalledPlugins.scanning
                    ? KI18n.i18nc("@action:button while a scan is under way", "Looking for plugins…")
                    : KI18n.i18nc("@action:button", "Look for plugins again")
                QQC2.ToolTip.text: rescanButton.text
                QQC2.ToolTip.visible: rescanButton.hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                onClicked: InstalledPlugins.rescan(false)
            }
        }

        QQC2.Label {
            id: blockedNote

            Layout.fillWidth: true
            Layout.leftMargin: library.toolsMargin
            Layout.rightMargin: library.toolsMargin
            visible: InstalledPlugins.blocked.length > 0
            wrapMode: Text.WordWrap
            color: AppColors.muted
            font: AppFonts.small
            textFormat: Text.StyledText
            text: KI18n.i18ncp("@info a plugin scan left some out; the link tries them again",
                               "%1 plugin could not be read. <a href=\"retry\">Try again</a>",
                               "%1 plugins could not be read. <a href=\"retry\">Try again</a>",
                               InstalledPlugins.blocked.length)
            onLinkActivated: InstalledPlugins.rescan(true)
        }

        // **A scan cut short**: a file that stopped telling of its
        // plugins part of the way. What it told is offered; the rest may be
        // missing until a scan gets through it.
        QQC2.Label {
            id: incompleteNote

            Layout.fillWidth: true
            Layout.leftMargin: library.toolsMargin
            Layout.rightMargin: library.toolsMargin
            visible: InstalledPlugins.incomplete.length > 0
            wrapMode: Text.WordWrap
            color: AppColors.muted
            font: AppFonts.small
            textFormat: Text.StyledText
            text: KI18n.i18ncp("@info a plugin scan stopped part of the way through some files; the link looks again",
                               "Some plugins in %1 file may be missing. <a href=\"again\">Look again</a>",
                               "Some plugins in %1 files may be missing. <a href=\"again\">Look again</a>",
                               InstalledPlugins.incomplete.length)
            onLinkActivated: InstalledPlugins.rescan(false)
        }

        QQC2.ScrollView {
            id: scroller

            // **The bar has a lane of its own**. A style that draws
            // its bar over the content left every row running under it, and
            // a row's drag took a press on the thumb as the start of a
            // block's drag: dragging the thumb moved nothing. While the bar
            // is needed, the rows end where it begins.
            readonly property real barLane: QQC2.ScrollBar.vertical.size < 1
                                            ? QQC2.ScrollBar.vertical.width : 0

            Layout.fillWidth: true
            Layout.fillHeight: true
            rightPadding: scroller.mirrored ? 0 : scroller.barLane
            leftPadding: scroller.mirrored ? scroller.barLane : 0
            contentWidth: availableWidth

            ListView {
                id: list

                clip: true
                // Two pixels between rows, as the prototype's list has.
                spacing: AppMetrics.followsSystem ? 0 : AppMetrics.hairSpacing
                model: library.rows
                keyNavigationEnabled: true
                activeFocusOnTab: true
                currentIndex: -1
                // **Somewhere to be when Tab arrives**: with no row
                // current, the list took the keyboard and nothing in it
                // showed that it had.
                onActiveFocusChanged: if (list.activeFocus && list.currentIndex < 0 && list.count > 0) {
                    list.currentIndex = 0;
                }

                delegate: BlockLibraryRow {
                    required property var modelData

                    width: ListView.view.width
                    entry: modelData
                    mimeType: library.mimeType
                    collapsed: library.isHeading(modelData) && library.isCollapsed(modelData.id)
                    onChosen: library.activate(modelData)
                }

                Keys.onReturnPressed: library.activate(library.rows[list.currentIndex])
                Keys.onEnterPressed: library.activate(library.rows[list.currentIndex])
            }
        }

        // **What an empty list is about.** Nothing found and nothing to
        // find are different things, and somebody who has typed three
        // letters wants to know which.
        PlaceholderMessage {
            Layout.fillWidth: true
            visible: library.rows.length === 0
            text: BlockCatalogue.ready
                ? KI18n.i18nc("@info:placeholder nothing in the library matched what was typed",
                              "No block matches what you typed")
                : KI18n.i18nc("@info:placeholder while the engine is still answering",
                              "Reading the block catalogue…")
        }
    }
}
