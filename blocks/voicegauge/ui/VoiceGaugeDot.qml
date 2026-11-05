// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick

// Voice Gauge's mark, on the card, in the tray and on the floating bar.
//
// **Never green.** Orange and full size means a voice is being counted, and a
// small grey dot means it is not. A mark in the warning colour is for nothing
// arriving, which is a fact about a signal. Nothing pulses.
Item {
    id: mark

    /// VoiceGaugeWords.dot()'s word.
    property string dot: "off"
    property real size: AppMetrics.stateDotSize

    readonly property color shownColor: mark.dot === "live" ? AppColors.liveMark
        : mark.dot === "warning" ? AppColors.warningMark : AppColors.muted

    implicitWidth: mark.size
    implicitHeight: mark.size
    Accessible.ignored: true

    Rectangle {
        anchors.centerIn: parent
        width: mark.dot === "off" ? mark.size / 2 : mark.size
        height: width
        radius: width / 2
        color: mark.shownColor
    }
}
