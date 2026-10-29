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

// The open sessions, one tab each, in the row under the title bar.
//
// **A tab per open session, not per saved one.** What is on disk is a list
// somebody goes looking through (Open Session…); what is in the row is what
// they are working on now.
//
// **Each tab carries its own live dot.** Several sessions can run at once —
// which is the point of having tabs at all — so a tab cannot ask AppShell,
// which knows only about the session in front. Sessions puts each one's
// state in its row.
//
// **Not a QQC2.TabBar.** A TabBar owns `currentIndex` and moves it when its
// model changes, which fights a tab bar whose order is dragged about and
// whose front tab is decided by something else. A ListView draws the same
// thing and leaves both to Sessions.
ListView {
    id: tabs

    /// What the `+` at the end of the strip does: the window's own New
    /// Session action, so the two cannot disagree about what it is called
    /// or whether it can be pressed.
    property QQC2.Action newTabAction: null

    /// The `+`, for a test.
    readonly property alias newTabButton: tabs.footerItem

    /// The tab the pointer is dragging, or -1. **Held here rather than in a
    /// delegate**, because a delegate cannot see its siblings and the drag
    /// has to know when it has passed one.
    property int draggingIndex: -1

    /// How far a tab must be dragged before it counts as a drag rather than
    /// a click that moved a little.
    readonly property int dragThreshold: Kirigami.Units.gridUnit

    /// The tab showing \a sessionId, for a test.
    ///
    /// **Found by asking the model, not the delegate.** A delegate's own
    /// properties are not something the row can be sure of from out here,
    /// and the tabs are in the model's order anyway.
    function tabFor(sessionId: string): Item {
        for (let row = 0; row < tabs.count; ++row) {
            if (Sessions.open[row]?.id === sessionId) {
                return tabs.itemAtIndex(row);
            }
        }
        return null;
    }

    model: Sessions.open
    orientation: ListView.Horizontal
    spacing: AppMetrics.followsSystem ? Kirigami.Units.smallSpacing : AppMetrics.hairSpacing
    clip: true
    // The tabs are as wide as they need to be and no wider; a row of them
    // that overflows scrolls rather than squeezing every name to nothing.
    // The header row, less its rule, in the Reverblabr appearance.
    implicitHeight: AppMetrics.followsSystem ? Kirigami.Units.gridUnit * 2 : AppMetrics.headerHeight - 1
    boundsBehavior: Flickable.StopAtBounds

    // **A new session is started from where the sessions are**,
    // after the last of them, and not only from the Session menu. It
    // triggers the menu's own action, so the two are one thing. It is not
    // the button's `action`: Qt 6.10 then shows the action's icon whenever
    // the button's own name is empty, over the prototype's glyph.
    footer: QQC2.ToolButton {
        // A control's square in the prototype, in the row's middle.
        height: AppMetrics.followsSystem ? tabs.height : AppMetrics.controlHeight
        y: (tabs.height - height) / 2
        width: height
        enabled: tabs.newTabAction?.enabled ?? false
        onClicked: tabs.newTabAction?.trigger(this)
        display: QQC2.AbstractButton.IconOnly
        // The icon theme's "+", or the prototype's own.
        icon.name: Appearance.followsSystem ? "list-add" : ""
        icon.source: Appearance.followsSystem ? "" : AppGlyphs.add
        Accessible.name: tabs.newTabAction?.text.replace("&", "") ?? ""
        QQC2.ToolTip.text: Accessible.name
        QQC2.ToolTip.visible: hovered
        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
    }

    // A tab sliding out of the way when another is dragged past it is what
    // says the order is being changed rather than something being dropped
    // on something.
    displaced: Transition {
        NumberAnimation {
            properties: "x"
            duration: Kirigami.Units.shortDuration
            easing.type: Easing.OutCubic
        }
    }

    delegate: QQC2.ItemDelegate {
        id: tab

        required property int index
        required property var modelData

        readonly property string sessionId: tab.modelData.id
        readonly property bool current: tab.sessionId === Sessions.currentId
        /// Whether this session is running, which is its own business and
        /// not the front session's.
        readonly property bool live: tab.modelData.running === true
        /// The close button, for a test.
        readonly property alias closeButton: closeTab
        readonly property alias liveDot: dot
        readonly property alias nameLabel: label

        height: tabs.height
        // **As wide as the name needs**, up to a limit so that a long one
        // does not take the row. No minimum beyond the padding: a short name
        // in a wide box leaves gaps that read as missing tabs.
        implicitWidth: Math.min(Kirigami.Units.gridUnit * 12,
                                tab.implicitContentWidth + tab.leftPadding + tab.rightPadding)
        leftPadding: AppMetrics.followsSystem ? Kirigami.Units.smallSpacing * 2 : AppMetrics.groupSpacing
        rightPadding: AppMetrics.followsSystem ? Kirigami.Units.smallSpacing : AppMetrics.groupSpacing
        // **Drawn by hand, because a tab has to look like a tab.** An
        // ItemDelegate draws nothing at all when it is neither hovered nor
        // highlighted, which left the tabs that were not in front as loose
        // text with a stray close button beside them.
        //
        // **Underlined, not filled**. A tab filled with the accent
        // colour read as the selected row of a list. The one in front is
        // marked the way a tab is: a rule under it in the accent, and its
        // name in the text colour and a heavier weight while the others
        // are muted. A hovered tab lifts a step, so the strip still says
        // which parts of it can be pressed.
        background: Rectangle {
            radius: AppMetrics.controlRadius
            color: tab.hovered && !tab.current ? AppColors.surface2 : "transparent"

            Rectangle {
                id: underline

                visible: tab.current
                anchors {
                    left: parent.left
                    right: parent.right
                    bottom: parent.bottom
                }
                height: 2
                color: AppColors.accentMark
            }
        }
        readonly property alias underline: underline
        // **The name is not the accessible name.** A screen reader is told
        // which session it is and what it is doing, because the dot that
        // says so to an eye says nothing to it.
        Accessible.name: tab.live
            ? KI18n.i18nc("@info:whatsthis a running session's tab", "%1, running", tab.modelData.name)
            : tab.modelData.name
        Accessible.description: KI18n.i18nc("@info:whatsthis", "Session tab")
        onClicked: Sessions.currentId = tab.sessionId

        // **A layout, not arithmetic.** Working the label's width out by
        // subtracting the paddings from the tab's is a sum that is wrong the
        // moment any of them changes — and it was: every name came out
        // elided with room to spare.
        contentItem: RowLayout {
            spacing: AppMetrics.followsSystem ? Kirigami.Units.smallSpacing : AppMetrics.largeSpacing

            Rectangle {
                id: dot

                Layout.alignment: Qt.AlignVCenter
                implicitWidth: AppMetrics.followsSystem ? Kirigami.Units.smallSpacing * 2 : AppMetrics.stateDotSize
                implicitHeight: implicitWidth
                radius: width / 2
                // **Shown only while it is running.** A dot that was there
                // in two colours would be a dot somebody has to learn; one
                // that appears is one they cannot miss.
                visible: tab.live
                color: AppColors.okMark
            }

            QQC2.Label {
                id: label

                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                text: tab.modelData.name
                elide: Text.ElideRight
                font.weight: tab.current ? Font.DemiBold : Font.Normal
                color: tab.current ? AppColors.text : AppColors.muted
            }

            QQC2.ToolButton {
                id: closeTab

                Layout.alignment: Qt.AlignVCenter
                icon.name: "window-close"
                icon.color: tab.current ? AppColors.text : AppColors.muted
                display: QQC2.AbstractButton.IconOnly
                implicitWidth: Kirigami.Units.gridUnit * 1.5
                implicitHeight: implicitWidth
                // **Not offered for the last one.** A window with no session
                // has nothing to show and no way back to one, so the button
                // that cannot work is not there at all.
                visible: Sessions.open.length > 1
                Accessible.name: KI18n.i18nc("@action:button close this session", "Close %1", tab.modelData.name)
                QQC2.ToolTip.text: Accessible.name
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                onClicked: Sessions.closeSession(tab.sessionId)
            }
        }

        // **Dragged sideways to reorder**, and nothing else: a tab dragged
        // out of the row does not become a window, and a drag that is really
        // a click that moved three pixels is a click.
        DragHandler {
            id: drag

            target: null
            yAxis.enabled: false
            cursorShape: active ? Qt.ClosedHandCursor : Qt.ArrowCursor

            onActiveChanged: {
                if (active) {
                    tabs.draggingIndex = tab.index;
                    return;
                }
                tabs.draggingIndex = -1;
            }

            onTranslationChanged: {
                if (!drag.active || Math.abs(drag.translation.x) < tabs.dragThreshold) {
                    return;
                }
                // One place at a time, so that the row keeps up with the
                // pointer rather than jumping to wherever it ended up.
                const step = drag.translation.x > 0 ? 1 : -1;
                const to = tab.index + step;
                if (to < 0 || to >= Sessions.open.length) {
                    return;
                }
                Sessions.moveTab(tab.index, to);
            }
        }
    }
}
