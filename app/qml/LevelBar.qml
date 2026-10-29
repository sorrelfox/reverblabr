// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.kirigami as Kirigami
import org.kde.ki18n
import io.github.reverblabr

// **A thin level bar, as the tray's popover draws them**: how loud
// something is, from -60 dBFS to full scale, in the danger colour at the top.
//
// **Read out as a progress bar with a value**, as a block's meter is:
// `value` between `from` and `to`, in whole decibels, brought up to
// date twice a second, because a screen reader reading sixty values a second
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

    implicitHeight: Kirigami.Units.smallSpacing
    radius: AppMetrics.barRadius
    color: AppColors.well

    Accessible.role: Accessible.ProgressBar
    Accessible.name: bar.name
    Accessible.description: bar.running
        ? KI18n.i18nc("@info:whatsthis the loudest channel, in decibels", "%1 dB", bar.value)
        : KI18n.i18nc("@info:whatsthis nothing is running", "Idle")

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
        radius: AppMetrics.barRadius
        width: parent.width * Math.max(0, Math.min(1, (bar.db - bar.from) / (bar.to - bar.from)))
        color: bar.db >= -0.1 ? AppColors.dangerMark : AppColors.liveMark
        visible: bar.running
    }
}
