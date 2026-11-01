// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick

// Speech Now's mark, on the card, in the tray and on the floating label.
//
// **It changes shape as well as colour:** filled for Speaking, a ring for
// Quiet again, a small grey dot otherwise, so that the two that matter look
// nothing alike at a glance. Nothing pulses with the level, so as not to pull
// the eye away from a face.
Item {
    id: mark

    /// SpeechNowWords.kindOf()'s word.
    property string kind: "quiet"
    property real size: AppMetrics.stateDotSize

    implicitWidth: mark.size
    implicitHeight: mark.size
    Accessible.ignored: true

    Rectangle {
        readonly property bool small: mark.kind !== "speaking" && mark.kind !== "again"

        anchors.centerIn: parent
        width: small ? mark.size / 2 : mark.size
        height: width
        radius: width / 2
        color: mark.kind === "speaking" ? AppColors.liveMark : mark.kind === "again" ? "transparent" : AppColors.muted
        border.width: mark.kind === "again" ? Math.max(1, mark.size / 5) : 0
        border.color: AppColors.okMark
    }
}
