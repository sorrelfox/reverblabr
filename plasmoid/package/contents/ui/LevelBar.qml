// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.kirigami as Kirigami

// **A thin level bar in the panel's own colours**: how loud a session is,
// from -60 dBFS to full scale, in the theme's colour for trouble at the top.
//
// **Read out as a progress bar with a value**, in whole decibels, brought up
// to date twice a second: a screen reader reading twenty values a second
// says nothing a person can follow. The bar itself is drawn from the live
// level.
Rectangle {
    id: bar

    /// What the bar is the level of, read out as its name.
    required property string name
    /// How loud, in dBFS, as it is now.
    property real db: -120
    /// Whether there is anything to be loud: a stopped session is idle, not
    /// silent.
    property bool running: true

    readonly property real from: -60
    readonly property real to: 0
    /// The level a screen reader is given, in whole decibels.
    property real value: bar.from
    /// How much of the bar is lit, 0 to 1.
    readonly property real fill: bar.running ? Math.max(0, Math.min(1, (bar.db - bar.from) / (bar.to - bar.from))) : 0

    implicitHeight: Math.round(Kirigami.Units.smallSpacing * 1.5)
    radius: height / 2
    color: Qt.alpha(Kirigami.Theme.textColor, 0.15)

    Accessible.role: Accessible.ProgressBar
    Accessible.name: bar.name
    Accessible.description: bar.running
        ? i18nc("@info:whatsthis the loudest channel, in decibels", "%1 dB", bar.value)
        : i18nc("@info:whatsthis nothing is running", "Idle")

    Timer {
        interval: 500
        repeat: true
        triggeredOnStart: true
        running: bar.visible && bar.running
        onTriggered: bar.value = Math.max(bar.from, Math.min(bar.to, Math.round(bar.db)))
        onRunningChanged: if (!running) {
            bar.value = bar.from;
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        radius: bar.radius
        width: parent.width * bar.fill
        color: bar.db >= -0.1 ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.positiveTextColor
        visible: bar.fill > 0
    }
}
