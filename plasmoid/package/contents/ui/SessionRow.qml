// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents3

// **One session in the widget's list**: its name, whether it records, how
// loud it is, and Run or Stop.
RowLayout {
    id: row

    required property string sessionId
    required property string name
    required property string sessionState
    required property bool running
    required property real levelDb
    required property bool recording
    /// Whether the levels are read: the popup is open.
    property bool live: true

    /// Run or Stop, by what the session does now: one that is starting
    /// can be stopped already.
    readonly property bool stops: row.running || row.sessionState === "starting"
    readonly property alias runButton: runButton
    readonly property alias levelBar: levelBar
    readonly property alias recordingMark: recordingMark

    /// Run or Stop was pressed.
    signal runRequested()
    signal stopRequested()

    spacing: Kirigami.Units.smallSpacing

    ColumnLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing / 2

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            PlasmaComponents3.Label {
                Layout.fillWidth: true
                text: row.name
                elide: Text.ElideRight
                textFormat: Text.PlainText
            }

            // **A red dot while it records**, as the window's tray icon
            // shows it.
            Rectangle {
                id: recordingMark

                Layout.alignment: Qt.AlignVCenter
                implicitWidth: Kirigami.Units.smallSpacing * 2
                implicitHeight: implicitWidth
                radius: width / 2
                color: Kirigami.Theme.negativeTextColor
                visible: row.recording
                Accessible.role: Accessible.Indicator
                Accessible.name: i18nc("@info:status a session is writing a recording", "Recording")
            }
        }

        LevelBar {
            id: levelBar

            Layout.fillWidth: true
            name: i18nc("@info:tooltip %1 is a session", "Level of %1", row.name)
            db: row.levelDb
            running: row.running && row.live
        }
    }

    PlasmaComponents3.ToolButton {
        id: runButton

        text: row.stops ? i18nc("@action:button stop the session", "Stop")
                        : i18nc("@action:button start the session", "Run")
        icon.name: row.stops ? "media-playback-stop" : "media-playback-start"
        display: PlasmaComponents3.AbstractButton.IconOnly
        enabled: row.sessionState !== "stopping"
        Accessible.name: row.stops ? i18nc("@action:button %1 is a session", "Stop %1", row.name)
                                   : i18nc("@action:button %1 is a session", "Run %1", row.name)
        onClicked: row.stops ? row.stopRequested() : row.runRequested()

        PlasmaComponents3.ToolTip {
            text: runButton.Accessible.name
        }
    }
}
