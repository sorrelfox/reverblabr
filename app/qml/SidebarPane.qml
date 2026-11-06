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

// The left-hand sidebar: the blocks to drag onto the canvas, the files the
// session has recorded, what the session has to say about itself, in the
// prototype's order, and the timers that start it, which the prototype did
// not have.
//
// The panes themselves belong to their own tasks; what is here is the frame
// they hang in, and the placeholder somebody sees until they exist.
Item {
    id: sidebar

    /// Which tab is showing, by index. The window keeps this so that the
    /// menu's Ctrl+1/Ctrl+2 and the tab bar are the same thing.
    property alias currentIndex: bar.currentIndex

    /// Where the block library goes.
    property alias blocksContent: blocksSlot.data
    /// Where the session's recordings go.
    property alias filesContent: filesSlot.data
    /// Where the session's own information goes.
    property alias infoContent: infoSlot.data
    /// Where the session's timers go.
    property alias scheduleContent: scheduleSlot.data

    /// The tabs by position, so that the window's actions and a test name
    /// them rather than count them.
    readonly property int blocksTab: 0
    readonly property int filesTab: 1
    readonly property int infoTab: 2
    readonly property int scheduleTab: 3

    /// The row of tabs, for a test to measure.
    readonly property alias tabBar: bar

    implicitWidth: Kirigami.Units.gridUnit * 14

    /// A tab, with the prototype's glyph before its name in the Reverblabr
    /// appearance when the row has room for every glyph: in the accent on the
    /// tab in front. The desktop's tabs have words only, and following the
    /// system keeps them so.
    component SidebarTab: QQC2.TabButton {
        id: tab

        property url glyph

        /// The width the tab's word needs, with the least room either side
        /// of it. **Measured in the heavier weight of the tab in front**, so
        /// that no tab moves when another is picked. The desktop's style
        /// draws the word itself, inside margins of its own, which a large
        /// spacing either side leaves room for.
        readonly property real wordWidth: Math.ceil(word.advanceWidth)
                                          + (AppMetrics.followsSystem ? Kirigami.Units.largeSpacing * 2
                                                                      : tab.leftPadding + tab.rightPadding)
        /// What the glyph and the gap after it add to that.
        readonly property real glyphWidth: AppMetrics.followsSystem ? 0 : tab.icon.width + tab.spacing
        /// What the tab needs for what it shows.
        readonly property real neededWidth: tab.wordWidth + (bar.glyphsFit ? tab.glyphWidth : 0)

        width: bar.widthFor(tab.neededWidth)
        display: bar.glyphsFit ? QQC2.AbstractButton.TextBesideIcon : QQC2.AbstractButton.TextOnly

        icon.source: AppMetrics.followsSystem ? "" : tab.glyph
        icon.width: AppMetrics.panelIconSize
        icon.height: AppMetrics.panelIconSize
        icon.color: tab.checked ? AppColors.accent : tab.hovered ? AppColors.text : AppColors.muted

        // The prototype's tabs have no padding of their own, their content
        // centred in the room the row gives them. This is the least room
        // either side of a word, before the row shares out what it has over.
        Binding on horizontalPadding {
            when: !AppMetrics.followsSystem
            value: AppMetrics.smallSpacing
        }

        // **A translation can still be too long for the row**, and then
        // its word is cut short. The whole word is a pointer's rest away.
        QQC2.ToolTip.text: tab.text
        QQC2.ToolTip.visible: tab.hovered && tab.width < tab.neededWidth
        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

        TextMetrics {
            id: word

            font: AppFonts.strong
            text: tab.text
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        QQC2.TabBar {
            id: bar

            /// The tabs, for what they need between them.
            readonly property list<SidebarTab> tabs: [blocksButton, filesButton, infoButton, scheduleButton]
            /// The room between the tabs.
            readonly property real gaps: Math.max(0, bar.tabs.length - 1) * bar.spacing
            /// The width the words need, gaps included, and what the glyphs
            /// add to it.
            readonly property real wordsWidth: {
                let width = bar.gaps;
                for (const tab of bar.tabs) {
                    width += tab.wordWidth;
                }
                return width;
            }
            readonly property real glyphsWidth: {
                let width = 0;
                for (const tab of bar.tabs) {
                    width += tab.glyphWidth;
                }
                return width;
            }

            /// **The words before the glyphs.** The prototype has three tabs
            /// to the sidebar, and four with a glyph and a word each need some
            /// 40 pixels more than the row has at the sidebar's usual width. A
            /// tab's word says what it is, so the glyphs give way, and come
            /// back when the sidebar is wide enough for them all.
            readonly property bool glyphsFit: bar.wordsWidth + bar.glyphsWidth <= bar.availableWidth
            /// What the tabs need for what they show.
            readonly property real neededWidth: bar.wordsWidth + (bar.glyphsFit ? bar.glyphsWidth : 0)

            /// **A tab is as wide as its word, with an equal share of the
            /// room left over**, so that "Info" gives "Schedule" what it does
            /// not need and every word has the same room round it. Shared out
            /// equally, a long word was cut while a short one had room to
            /// spare. In a row too narrow for the words, every tab gives up the
            /// same part of its width.
            function widthFor(needed: real): real {
                const room = Math.max(0, bar.availableWidth - bar.gaps);
                const wanted = bar.neededWidth - bar.gaps;
                return wanted <= room ? needed + (room - wanted) / bar.tabs.length : needed * room / wanted;
            }

            // **The row's own width is what the tabs need, not what they
            // were given.** Left to itself it adds up the tabs' widths, which
            // are worked out from the row's, and the layout went round in a
            // loop.
            contentWidth: bar.wordsWidth + bar.glyphsWidth

            Layout.fillWidth: true
            // Inset by the window's margin like everything else at its edge;
            // the first tab used to start at the window's left side.
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            Layout.topMargin: AppMetrics.sidebarTabsTop
            Layout.bottomMargin: AppMetrics.sidebarTabsBottom

            SidebarTab {
                id: blocksButton

                text: KI18n.i18nc("@title:tab the blocks a session can be built from", "Blocks")
                glyph: AppGlyphs.blocks
            }

            SidebarTab {
                id: filesButton

                text: KI18n.i18nc("@title:tab the files the session has recorded", "Files")
                glyph: AppGlyphs.files
            }

            SidebarTab {
                id: infoButton

                text: KI18n.i18nc("@title:tab what the session has to say about itself", "Info")
                glyph: AppGlyphs.info
            }

            SidebarTab {
                id: scheduleButton

                text: KI18n.i18nc("@title:tab the timers that start the session at set times", "Schedule")
                glyph: AppGlyphs.schedule
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: bar.currentIndex

            Item {
                id: blocksSlot

                PlaceholderMessage {
                    anchors.centerIn: parent
                    width: parent.width - Kirigami.Units.largeSpacing * 2
                    visible: blocksSlot.children.length === 1
                    text: KI18n.i18nc("@info:placeholder", "The block library goes here")
                }
            }

            Item {
                id: filesSlot

                PlaceholderMessage {
                    anchors.centerIn: parent
                    width: parent.width - Kirigami.Units.largeSpacing * 2
                    visible: filesSlot.children.length === 1
                    text: KI18n.i18nc("@info:placeholder", "The session's recordings go here")
                }
            }

            Item {
                id: infoSlot

                PlaceholderMessage {
                    anchors.centerIn: parent
                    width: parent.width - Kirigami.Units.largeSpacing * 2
                    visible: infoSlot.children.length === 1
                    text: KI18n.i18nc("@info:placeholder", "What this session is and how it is running goes here")
                }
            }

            Item {
                id: scheduleSlot

                PlaceholderMessage {
                    anchors.centerIn: parent
                    width: parent.width - Kirigami.Units.largeSpacing * 2
                    visible: scheduleSlot.children.length === 1
                    text: KI18n.i18nc("@info:placeholder", "The timers that start this session go here")
                }
            }
        }
    }
}
