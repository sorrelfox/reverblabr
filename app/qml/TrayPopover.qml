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

// **What a click on the tray icon opens**: every saved session with
// Run and Stop and how loud it is, and the meters somebody marked "Show in
// system tray", each of which brings its session forward when clicked.
//
// **A small window of its own, not a menu.** A tray icon on Wayland is told
// nothing about where on the screen it sits, so a popover cannot be pinned
// to it; the compositor places this one, and it goes away as soon as it
// loses the focus, the way a popover does.
//
// **What went wrong is shown here and counts as seen** once the popover
// closes (TrayState.acknowledge()): the icon stops asking for attention then,
// not the moment it is opened, before anybody could read it.
Window {
    id: root

    flags: Qt.Tool | Qt.FramelessWindowHint
    color: AppColors.window
    title: KI18n.i18nc("@title:window the system tray's popover", "Reverblabr")

    width: Kirigami.Units.gridUnit * 22
    height: Math.min(content.implicitHeight + AppMetrics.panelPadding * 2, Kirigami.Units.gridUnit * 32)

    /// The parts, for the tests.
    readonly property alias overview: overview
    readonly property alias sessionList: sessionList
    readonly property alias meterList: meterList
    readonly property alias attentionMessage: attentionMessage
    readonly property alias openButton: openButton
    readonly property alias allSessionsButton: allSessionsButton
    readonly property alias placeholder: placeholder

    /// Show Reverblabr's main window.
    signal openWindowRequested()
    /// Show the Sessions window.
    signal allSessionsRequested()

    /// Opens the popover, or closes it if it is open: what a click on the
    /// icon does.
    function toggle() {
        if (root.visible) {
            root.hide();
        } else {
            root.show();
            root.raise();
            root.requestActivate();
        }
    }

    /// Whether it has had the focus since it was shown. **It closes when it
    /// loses the focus, not before it ever had it**, or a compositor that
    /// takes a moment to activate a new window would close it at once.
    property bool hadFocus: false

    onActiveChanged: {
        if (root.active) {
            root.hadFocus = true;
        } else if (root.hadFocus) {
            root.hide();
        }
    }
    onVisibleChanged: {
        if (!root.visible) {
            root.hadFocus = false;
            TrayState.acknowledge();
        }
    }

    // The levels are read only while there is somebody to read them.
    Binding {
        target: TrayState
        property: "watching"
        value: root.visible
    }

    SessionOverview {
        id: overview

        watching: root.visible
    }

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.color: AppColors.border
        border.width: 1
        radius: AppMetrics.panelRadius
    }

    ColumnLayout {
        id: content

        anchors.fill: parent
        anchors.margins: AppMetrics.panelPadding
        spacing: AppMetrics.mediumSpacing

        RowLayout {
            Layout.fillWidth: true
            spacing: AppMetrics.mediumSpacing

            Kirigami.Heading {
                Layout.fillWidth: true
                level: 4
                text: KI18n.i18nc("@title the sessions listed in the system tray's popover", "Sessions")
                elide: Text.ElideRight
            }

            QQC2.ToolButton {
                id: allSessionsButton

                text: KI18n.i18nc("@action:button open the window listing every saved session", "All Sessions…")
                icon.name: "view-list-details"
                display: QQC2.AbstractButton.IconOnly
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.text: text
                onClicked: {
                    root.hide();
                    root.allSessionsRequested();
                }
            }

            QQC2.ToolButton {
                id: openButton

                text: KI18n.i18nc("@action:button show the application's main window", "Open Reverblabr")
                icon.name: "window-new"
                display: QQC2.AbstractButton.IconOnly
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.text: text
                onClicked: {
                    root.hide();
                    root.openWindowRequested();
                }
            }
        }

        Kirigami.InlineMessage {
            id: attentionMessage

            Layout.fillWidth: true
            type: Kirigami.MessageType.Warning
            visible: TrayState.attention
            text: TrayState.attentionText
        }

        ListView {
            id: sessionList

            /// One row's height. **The list is as tall as its rows**, up to a
            /// limit: sized from its own content, a list with nothing drawn
            /// yet is none tall, and so never draws anything.
            readonly property real rowHeight: Kirigami.Units.gridUnit * 2

            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(sessionList.count * sessionList.rowHeight, Kirigami.Units.gridUnit * 14)
            // Never squeezed away by a long list of meters below it.
            Layout.minimumHeight: Math.min(sessionList.count, 3) * sessionList.rowHeight
            clip: true
            model: overview
            boundsBehavior: Flickable.StopAtBounds
            QQC2.ScrollBar.vertical: QQC2.ScrollBar {}

            delegate: RowLayout {
                id: row

                required property string sessionId
                required property string name
                required property string sessionState
                required property bool running
                required property real levelDb

                readonly property bool stops: row.running || row.sessionState === "starting"

                width: ListView.view.width
                height: sessionList.rowHeight
                spacing: AppMetrics.mediumSpacing

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    ElidedLabel {
                        Layout.fillWidth: true
                        text: row.name
                    }

                    // How loud it is, from -60 dBFS to full scale, while it runs.
                    LevelBar {
                        Layout.fillWidth: true
                        name: KI18n.i18nc("@info:tooltip %1 is a session", "Level of %1", row.name)
                        db: row.levelDb
                        running: row.running
                    }
                }

                QQC2.Button {
                    text: row.stops ? KI18n.i18nc("@action:button stop the session", "Stop")
                                    : KI18n.i18nc("@action:button start the session", "Run")
                    icon.name: Appearance.followsSystem ? (row.stops ? "media-playback-stop" : "media-playback-start") : ""
                    icon.source: Appearance.followsSystem ? "" : (row.stops ? AppGlyphs.stop : AppGlyphs.run)
                    display: QQC2.AbstractButton.IconOnly
                    enabled: row.sessionState !== "stopping"
                    Accessible.name: row.stops ? KI18n.i18nc("@action:button %1 is a session", "Stop %1", row.name)
                                               : KI18n.i18nc("@action:button %1 is a session", "Run %1", row.name)
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.text: Accessible.name
                    onClicked: row.stops ? overview.stop([row.sessionId]) : overview.run([row.sessionId])
                }
            }
        }

        QQC2.Label {
            id: placeholder

            Layout.fillWidth: true
            visible: overview.ready && overview.count === 0
            text: KI18n.i18nc("@info in the system tray's popover", "No saved sessions yet")
            color: AppColors.muted
            wrapMode: Text.Wrap
        }

        // --- the meters marked "Show in system tray" ---
        Kirigami.Heading {
            Layout.fillWidth: true
            Layout.topMargin: AppMetrics.mediumSpacing
            level: 4
            visible: meterList.count > 0
            text: KI18n.i18nc("@title the meters shown in the system tray's popover", "Meters")
            elide: Text.ElideRight
        }

        ListView {
            id: meterList

            /// One meter's height: its name above its bar.
            readonly property real rowHeight: Kirigami.Units.gridUnit * 2.5

            Layout.fillWidth: true
            // **Bounded like the sessions**: somebody who marks twenty meters
            // for the tray still sees the sessions above them.
            Layout.preferredHeight: Math.min(meterList.count * meterList.rowHeight, Kirigami.Units.gridUnit * 10)
            Layout.minimumHeight: Math.min(meterList.count, 2) * meterList.rowHeight
            visible: meterList.count > 0
            clip: true
            model: TrayState.meters
            boundsBehavior: Flickable.StopAtBounds
            QQC2.ScrollBar.vertical: QQC2.ScrollBar {}

            delegate: QQC2.ItemDelegate {
                id: meter

                required property var modelData

                width: ListView.view.width
                height: meterList.rowHeight

                readonly property string meterName: meter.modelData.nickname !== ""
                    ? meter.modelData.nickname : BlockPresentation.name(meter.modelData.type)
                /// Read again at every tick of the tray's levels.
                readonly property var level: TrayState.frame >= 0
                    ? TrayState.meterLevel(meter.modelData.sessionId, meter.modelData.blockId) : ({})
                readonly property real peakDb: meter.level.peakDb ?? -120
                readonly property alias levelBar: meterBar

                Accessible.name: KI18n.i18nc("@action:button %1 is a meter, %2 its session", "%1 in %2",
                                             meter.meterName, meter.modelData.sessionName)
                Accessible.description: KI18n.i18nc("@info:tooltip", "Brings its session forward")
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.text: Accessible.description
                onClicked: {
                    root.hide();
                    TrayState.bringForward(meter.modelData.sessionId, meter.modelData.blockId);
                }

                contentItem: ColumnLayout {
                    spacing: 0

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: AppMetrics.mediumSpacing

                        ElidedLabel {
                            Layout.fillWidth: true
                            text: meter.meterName
                        }

                        ElidedLabel {
                            Layout.maximumWidth: meter.width / 2
                            text: meter.modelData.sessionName
                            color: AppColors.muted
                            font: AppFonts.small
                        }
                    }

                    LevelBar {
                        id: meterBar

                        Layout.fillWidth: true
                        implicitHeight: Kirigami.Units.smallSpacing * 2
                        name: meter.meterName
                        db: meter.peakDb
                        running: meter.peakDb > -120
                    }
                }
            }
        }
    }
}
