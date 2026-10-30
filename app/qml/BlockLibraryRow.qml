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

// One line of the block library: either a category heading or a block.
//
// **Both shapes in one delegate, in one list.** The arrow keys, the focus
// ring and Enter come from the list for nothing, and a person tabbing
// through the library moves through headings and blocks in the order they
// are drawn rather than in the order two separate lists happen to be built.
//
// **A block is dragged out of here as mime data, not as a pointer to this
// row.** The canvas is the other half of the gesture and has to be able to
// say what it accepts; a type id under an agreed type is something either
// side can be written against on its own.
QQC2.ItemDelegate {
    id: row

    /// What this line is: `{ kind: "category" | "section" | "block" |
    /// "plugin", id, count }`; for a format's heading under Plugins its
    /// format, and for a plugin its name, vendor, format and the block type
    /// it becomes, with the id its library key.
    required property var entry
    /// What a block type travels under when it is dragged to the canvas.
    required property string mimeType
    /// Whether this heading is folded away. Meaningless on a block.
    required property bool collapsed

    readonly property bool isCategory: row.entry.kind === "category"
    /// A format's heading under Plugins: folded and read out as a category
    /// is, and set in from it, so the two levels read as two.
    readonly property bool isSection: row.entry.kind === "section"
    /// Either kind of heading: a line that folds what is under it.
    readonly property bool isHeading: row.isCategory || row.isSection
    readonly property bool isPlugin: row.entry.kind === "plugin"
    /// What travels to the canvas: a block's type id, or a plugin's key.
    readonly property string typeId: row.isHeading ? "" : row.entry.id
    /// The block type this line makes, which is what its icon and words are.
    readonly property string blockType: row.isPlugin ? row.entry.type : row.typeId

    readonly property string label: row.isCategory
        ? BlockPresentation.categoryName(row.entry.id)
        : row.isSection ? PluginWords.formatName(row.entry.format)
        : row.isPlugin ? row.entry.name : BlockPresentation.name(row.typeId)
    /// The second line: what a block does, or who made a plugin.
    readonly property string detail: row.isHeading
        ? ""
        : row.isPlugin ? row.entry.vendor : BlockPresentation.description(row.typeId)

    /// The heading is folded, or the block is added: the library decides
    /// which, because it is the one holding both.
    signal chosen()

    // A heading is a line of text; a block is two, and wants the room.
    // **In the Reverblabr appearance, what the lines need**: the
    // prototype's heading stands ten pixels under the row before it and
    // four over its first block, and a block is its 28-pixel icon and
    // five either side.
    implicitHeight: AppMetrics.followsSystem
        ? (row.isHeading ? Math.round(Kirigami.Units.gridUnit * 1.8) : Math.round(Kirigami.Units.gridUnit * 2.6))
        : implicitContentHeight + topPadding + bottomPadding

    // A format's heading sits closer to what is above it, being a part
    // of the category over it rather than one of its own.
    Binding on topPadding {
        when: !AppMetrics.followsSystem && row.isHeading
        value: row.isSection ? AppMetrics.smallSpacing : AppMetrics.rowSpacing
    }
    Binding on bottomPadding {
        when: !AppMetrics.followsSystem && row.isHeading
        value: AppMetrics.smallSpacing
    }

    highlighted: row.ListView.isCurrentItem

    Accessible.role: row.isHeading ? Accessible.Grouping : Accessible.ListItem
    Accessible.name: row.isCategory
        // **A count is a plural**: "Outputs, 1 blocks" is what the
        // one-form sentence read out for a heading with one block in it.
        ? KI18n.i18ncp("@info:whatsthis a library heading and how many blocks are under it, 'Outputs, 1 block'",
                       "%2, %1 block", "%2, %1 blocks", row.entry.count, row.label)
        : row.isSection
            ? KI18n.i18ncp("@info:whatsthis a plugin format's library heading and how many plugins are under it, 'LV2, 1 plugin'",
                           "%2, %1 plugin", "%2, %1 plugins", row.entry.count, row.label)
            : row.label
    Accessible.description: row.isHeading
        ? (row.collapsed
            ? KI18n.i18nc("@info:whatsthis", "Folded away")
            : KI18n.i18nc("@info:whatsthis", "Open"))
        : row.isPlugin
            ? KI18n.i18nc("@info:whatsthis a plugin in the library: its format and who made it", "%1, %2",
                          PluginWords.formatName(row.entry.format), row.detail)
            : row.detail

    /// Whether the press that is under way turned into a drag.
    ///
    /// **A drag and a click are one gesture, and must not count as two.**
    /// The row is a button and a drag source at once, and the canvas adds
    /// the block where it was dropped; a release that also reached the
    /// button would have the library add a second one beside it, which is
    /// how one drop once made two tiles.
    ///
    /// Whether the button sees that release at all is the window system's
    /// business and not ours: an automatic drag runs an event loop of its
    /// own, and what it hands back afterwards differs between platforms —
    /// offscreen the press is simply taken away, a real compositor gives
    /// the release back and the click follows it. So the row remembers
    /// that it was dragged rather than trusting the press to have been
    /// taken from it.
    property bool dragged: false

    // A new press starts a new gesture, unless the drag is still the one
    // that is under way.
    onPressedChanged: if (row.pressed && !dragHandler.active) {
        row.dragged = false;
    }

    onClicked: if (!row.dragged) {
        row.chosen();
    }
    // **A double click does what a click does**, rather than nothing or
    // something else: somebody who double-clicks a list expects the thing
    // to happen, and two blocks appearing where one was wanted is worse
    // than the second click being ignored.
    onDoubleClicked: {}

    /// The longer form, where there is room for it. Named here rather than
    /// only on the attached tooltip so that a test can read what a person
    /// would read.
    readonly property string toolTipText: row.isHeading
        // A heading has no longer form; its own words, when they don't fit.
        ? (titleLabel.truncated ? row.label : "")
        : row.isPlugin ? PluginWords.pluginTooltip(row.entry) : BlockPresentation.tooltip(row.typeId)

    QQC2.ToolTip.text: row.toolTipText
    QQC2.ToolTip.visible: row.hovered && row.toolTipText !== ""
    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

    contentItem: RowLayout {
        spacing: AppMetrics.followsSystem ? Kirigami.Units.smallSpacing
                                          : row.isHeading ? AppMetrics.mediumSpacing : AppMetrics.rowSpacing

        // --- a heading ---

        Kirigami.Icon {
            id: chevron

            visible: row.isHeading
            source: row.collapsed ? "arrow-right" : "arrow-down"
            // **A format's heading is set in** under its category's words,
            // so the two levels read as two.
            Layout.leftMargin: row.isSection ? AppMetrics.sectionChevronSize + (chevron.parent as RowLayout).spacing : 0
            implicitWidth: AppMetrics.sectionChevronSize
            implicitHeight: AppMetrics.sectionChevronSize
            Accessible.ignored: true

            // Muted with the heading's words in the prototype.
            Binding on color {
                when: !AppMetrics.followsSystem
                value: AppColors.muted
            }
        }

        // --- or a block ---
        //
        // **In a box of its own in the Reverblabr appearance**, as the
        // prototype draws a library block: the icon on a raised square with
        // a hairline and the tile's corner. While following the
        // system the box is nothing, and the icon is as it was.
        Rectangle {
            visible: !row.isHeading
            implicitWidth: AppMetrics.followsSystem ? Kirigami.Units.iconSizes.smallMedium : AppMetrics.controlHeight
            implicitHeight: implicitWidth
            radius: AppMetrics.tileRadius
            color: AppMetrics.followsSystem ? "transparent" : AppColors.surface
            border.width: AppMetrics.followsSystem ? 0 : 1
            border.color: AppColors.border

            Kirigami.Icon {
                anchors.centerIn: parent
                source: BlockPresentation.icon(row.blockType)
                // The one thing that says which kind of block this is without
                // being read, which is why the category's colour goes here.
                color: AppColors.categoryTint(row.isCategory ? row.entry.id : BlockPresentation.category(row.blockType))
                isMask: true
                implicitWidth: AppMetrics.followsSystem ? Kirigami.Units.iconSizes.smallMedium : AppMetrics.libraryIconSize
                implicitHeight: implicitWidth
                Accessible.ignored: true
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0

            QQC2.Label {
                id: titleLabel

                Layout.fillWidth: true
                text: row.label
                color: row.isHeading && !AppMetrics.followsSystem ? AppColors.muted : AppColors.text
                // A format's heading is a level under its category's, so
                // in the weight of a block's name rather than the heading's.
                font: row.isCategory ? AppFonts.libraryHeading : AppFonts.libraryTitle
                // The prototype sets a block's two lines close, at 1.2 and
                // 1.25 of their size, so the pair is as tall as the icon
                // beside it.
                lineHeightMode: AppMetrics.followsSystem || row.isHeading ? Text.ProportionalHeight : Text.FixedHeight
                lineHeight: AppMetrics.followsSystem || row.isHeading ? 1.0 : AppMetrics.libraryTitleLine
                elide: Text.ElideRight
                Accessible.ignored: true
            }

            QQC2.Label {
                visible: !row.isHeading
                Layout.fillWidth: true
                text: row.detail
                color: AppColors.muted
                font: AppFonts.libraryDetail
                lineHeightMode: AppMetrics.followsSystem ? Text.ProportionalHeight : Text.FixedHeight
                lineHeight: AppMetrics.followsSystem ? 1.0 : AppMetrics.libraryDetailLine
                elide: Text.ElideRight
                Accessible.ignored: true
            }
        }

        // How many blocks are under a heading, so that a folded category
        // still says whether there is anything in it.
        QQC2.Label {
            visible: row.isHeading
            text: KI18n.i18ncp("@info how many blocks a library category holds", "%1", "%1", row.entry.count)
            color: AppColors.muted
            font: AppFonts.small
            Accessible.ignored: true
        }
    }

    // **The drag carries the type id under the agreed type.** Automatic
    // rather than internal, because the canvas asks the drop what it is
    // carrying rather than reaching back into whatever started it — which
    // is what lets either half be written and tested without the other.
    Drag.active: !row.isHeading && dragHandler.active
    Drag.dragType: Drag.Automatic
    Drag.supportedActions: Qt.CopyAction
    Drag.mimeData: row.dragData()
    Drag.hotSpot.x: row.width / 2
    Drag.hotSpot.y: row.height / 2

    /// What the drag carries. Built rather than written as a literal,
    /// because the key is the canvas's to name and not ours.
    function dragData(): var {
        const data = {};
        data[row.mimeType] = row.typeId;
        return data;
    }

    DragHandler {
        id: dragHandler

        enabled: !row.isHeading
        target: null

        onActiveChanged: if (dragHandler.active) {
            row.dragged = true;
        }
    }
}
