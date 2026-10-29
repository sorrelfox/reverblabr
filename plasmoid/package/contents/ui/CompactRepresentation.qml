// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.kirigami as Kirigami
import io.github.reverblabr.plasmoid
import "status.js" as Status

// **What sits in the panel**: Reverblabr's icon, a dot in the theme's
// colour for good news while any session runs, and a red one while any
// records. A click opens the list.
//
// **Drawn from what the engine says, never polled**: the icon changes when
// a session starts or stops, and costs nothing in between.
MouseArea {
    id: compact

    required property SessionList sessions
    /// Whether the popup is open, for the icon's pressed look.
    property bool expanded: false

    readonly property bool running: (compact.sessions?.runningCount ?? 0) > 0
    readonly property alias runningMark: runningMark
    readonly property alias recordingMark: recordingMark

    /// The popup should open or close.
    signal toggled()

    hoverEnabled: true
    acceptedButtons: Qt.LeftButton
    onClicked: compact.toggled()

    Accessible.role: Accessible.Button
    Accessible.name: i18nc("@title the panel widget's name", "Reverblabr Sessions")
    Accessible.description: Status.summary(compact.sessions)
    Accessible.onPressAction: compact.toggled()

    Kirigami.Icon {
        id: icon

        anchors.fill: parent
        source: "io.github.reverblabr.Reverblabr"
        fallback: "audio-card"
        active: compact.containsMouse || compact.expanded
    }

    // **Running**: a dot in the bottom corner, in the theme's colour for
    // something that is well, so it follows a light theme and a dark one.
    Rectangle {
        id: runningMark

        anchors.right: icon.right
        anchors.bottom: icon.bottom
        width: Math.max(Kirigami.Units.smallSpacing * 2, Math.round(Math.min(icon.width, icon.height) / 3))
        height: width
        radius: width / 2
        color: Kirigami.Theme.positiveTextColor
        border.color: Kirigami.Theme.backgroundColor
        border.width: 1
        visible: compact.running
    }

    // **Recording**: a red dot in the top corner.
    Rectangle {
        id: recordingMark

        anchors.right: icon.right
        anchors.top: icon.top
        width: runningMark.width
        height: width
        radius: width / 2
        color: Kirigami.Theme.negativeTextColor
        border.color: Kirigami.Theme.backgroundColor
        border.width: 1
        visible: compact.sessions?.recording ?? false
    }
}
