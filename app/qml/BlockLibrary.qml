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
    readonly property var rows: {
        const available = BlockCatalogue.ready ? BlockCatalogue.types : [];
        const needle = search.text.toLocaleLowerCase();
        const built = [];
        for (const category of BlockPresentation.categories) {
            const found = [];
            for (const typeId of available) {
                if (BlockPresentation.category(typeId) === category && library.matches(typeId, needle)) {
                    found.push(typeId);
                }
            }
            // **Every plugin is an entry of its own** under Plugins, after
            // the blocks that are there to pick one.
            const plugins = category === "plugins"
                ? library.offeredPlugins.filter(plugin => library.pluginMatches(plugin, needle))
                : [];
            if (found.length === 0 && plugins.length === 0) {
                continue;
            }
            // **In the reader's alphabet**, not in the engine's id order:
            // "Input Device" and "source.device" sort differently, and it
            // is the name that is being read.
            found.sort((first, second) =>
                BlockPresentation.name(first).localeCompare(BlockPresentation.name(second)));
            built.push({ kind: "category", id: category, count: found.length + plugins.length });
            if (library.isCollapsed(category)) {
                continue;
            }
            for (const typeId of found) {
                built.push({ kind: "block", id: typeId, count: 0 });
            }
            for (const group of library.byFormat(plugins)) {
                const section = library.sectionOf(group.format);
                built.push({ kind: "section", id: section, format: group.format, count: group.plugins.length });
                if (library.isCollapsed(section)) {
                    continue;
                }
                for (const plugin of group.plugins) {
                    built.push({ kind: "plugin", id: plugin.key, count: 0, name: plugin.name, vendor: plugin.vendor,
                                 format: plugin.format, type: plugin.type, category: plugin.category,
                                 audioInputs: plugin.audioInputs, audioOutputs: plugin.audioOutputs });
                }
            }
        }
        return built;
    }

    /// What a format's heading under Plugins is folded away as. **Beside
    /// the categories' ids**, so the folding is remembered the same way;
    /// the slash keeps it from ever being taken for one.
    function sectionOf(format: string): string {
        return "plugins/" + format;
    }

    /// \a plugins a format at a time: `[{ format, plugins }]`, the formats
    /// and the plugins under each in the reader's alphabet. **By the name
    /// a person reads**, as the blocks are: the scan's order is the order
    /// it found the files in.
    function byFormat(plugins: var): var {
        const groups = [];
        for (const plugin of plugins) {
            let group = groups.find(entry => entry.format === plugin.format);
            if (group === undefined) {
                group = { format: plugin.format, plugins: [] };
                groups.push(group);
            }
            group.plugins.push(plugin);
        }
        for (const group of groups) {
            group.plugins.sort((first, second) => first.name.localeCompare(second.name));
        }
        groups.sort((first, second) =>
            PluginWords.formatName(first.format).localeCompare(PluginWords.formatName(second.format)));
        return groups;
    }

    /// Whether \a entry is a heading: a category, or a format under Plugins.
    function isHeading(entry: var): bool {
        return entry.kind === "category" || entry.kind === "section";
    }

    /// Whether \a typeId is one of the blocks \a needle is looking for.
    /// **The name and the description both**, because half of what somebody
    /// types is what a block does rather than what it is called — and
    /// lower-cased in the reader's locale, so that a Turkish dotless i
    /// matches the way a Turkish reader expects.
    function matches(typeId: string, needle: string): bool {
        if (needle === "") {
            return true;
        }
        return BlockPresentation.name(typeId).toLocaleLowerCase().includes(needle)
            || BlockPresentation.description(typeId).toLocaleLowerCase().includes(needle);
    }

    /// Whether a plugin is one \a needle is looking for: by its name, who
    /// made it, or what kind of plugin it says it is.
    function pluginMatches(plugin: var, needle: string): bool {
        if (needle === "") {
            return true;
        }
        return plugin.name.toLocaleLowerCase().includes(needle)
            || plugin.vendor.toLocaleLowerCase().includes(needle)
            || plugin.category.toLocaleLowerCase().includes(needle);
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
            // **The first block it found**, not the first row: that is the
            // heading the block is under, and Enter folded it. Nor a
            // format's heading under Plugins, for the same reason.
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
