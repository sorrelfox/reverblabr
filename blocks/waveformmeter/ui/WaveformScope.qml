// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma ComponentBehavior: Bound

import QtQuick
import org.kde.kirigami as Kirigami

// The sound drawn in its well, on a tile or a card: a ScopeItem with the
// application's colours, and a line of words where there is no trace.
//
// **The colours are the ones already meaning something here**: the accent for
// the left, or the one channel shown, and the live colour for the right. They
// are told apart in both themes, and in a colour scheme of the person's own.
Rectangle {
    id: well

    /// The block drawn, and what it is set to.
    required property WaveformState reading
    /// Where the words go when a tile has room for few.
    property var absenceFont: AppFonts.tileCaption

    readonly property alias scope: scopeItem
    readonly property alias absenceLabel: absence

    color: AppColors.well
    radius: AppMetrics.controlRadius
    clip: true

    ScopeItem {
        id: scopeItem

        anchors.fill: parent
        hub: Meters.scopes
        blockId: well.reading.blockId
        refreshHz: well.reading.refreshHz
        motion: well.reading.motion
        style: well.reading.style
        spanMs: well.reading.spanMs
        gainDb: well.reading.gainDb
        autoFit: well.reading.autoFit
        channels: well.reading.channels
        trigger: well.reading.trigger
        hold: well.reading.hold
        grid: well.reading.grid
        heightScale: well.reading.scale
        firstColor: AppColors.accent
        secondColor: AppColors.live
        gridColor: AppColors.scopeGrid
    }

    ElidedLabel {
        id: absence

        anchors.fill: parent
        anchors.margins: Kirigami.Units.smallSpacing
        visible: !scopeItem.showing
        text: well.reading.hold ? well.reading.words.status("", true) : well.reading.words.absence(scopeItem.feedState)
        font: well.absenceFont
        color: AppColors.muted
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        wrapMode: Text.WordWrap
        Accessible.ignored: true
    }
}
