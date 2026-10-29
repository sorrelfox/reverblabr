// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.ki18n

// The left-hand sidebar: the blocks to drag onto the canvas, the files the
// session has recorded and what the session has to say about itself,
// in the prototype's order. Schedule joins them in a later phase.
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

    /// The tabs by position, so that the window's actions and a test name
    /// them rather than count them.
    readonly property int blocksTab: 0
    readonly property int filesTab: 1
    readonly property int infoTab: 2

    implicitWidth: Kirigami.Units.gridUnit * 14

    /// A tab, with the prototype's glyph before its name in the Reverblabr
    /// appearance: in the accent on the tab in front. The desktop's
    /// tabs have words only, and following the system keeps them so.
    component SidebarTab: QQC2.TabButton {
        id: tab

        property url glyph

        icon.source: AppMetrics.followsSystem ? "" : tab.glyph
        icon.width: AppMetrics.panelIconSize
        icon.height: AppMetrics.panelIconSize
        icon.color: tab.checked ? AppColors.accent : tab.hovered ? AppColors.text : AppColors.muted

        // **Three to a sidebar, as the prototype has them**: its tabs
        // share the row with their content centred and no padding of their
        // own, and a button's padding either side left "Blocks" elided at
        // the sidebar's usual width.
        Binding on horizontalPadding {
            when: !AppMetrics.followsSystem
            value: AppMetrics.smallSpacing
        }

        // **A longer word than "Blocks" still elides**: a third of a
        // sidebar is little room, and a translation is often a third longer.
        // The whole word is then a pointer's rest away.
        QQC2.ToolTip.text: tab.text
        QQC2.ToolTip.visible: tab.hovered && tab.implicitContentWidth > tab.availableWidth
        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        QQC2.TabBar {
            id: bar

            Layout.fillWidth: true
            // Inset by the window's margin like everything else at its edge;
            // the first tab used to start at the window's left side.
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            Layout.topMargin: AppMetrics.sidebarTabsTop
            Layout.bottomMargin: AppMetrics.sidebarTabsBottom

            SidebarTab {
                text: KI18n.i18nc("@title:tab the blocks a session can be built from", "Blocks")
                glyph: AppGlyphs.blocks
            }

            SidebarTab {
                text: KI18n.i18nc("@title:tab the files the session has recorded", "Files")
                glyph: AppGlyphs.files
            }

            SidebarTab {
                text: KI18n.i18nc("@title:tab what the session has to say about itself", "Info")
                glyph: AppGlyphs.info
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
        }
    }
}
