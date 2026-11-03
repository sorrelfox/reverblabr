// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma ComponentBehavior: Bound

import QtQuick
import org.kde.kirigami as Kirigami

// The spectrum drawn in its well, on a tile or a card: a SpectrumItem with the
// application's colours, the scales beside it when there is room for them, and
// a line of words where there are no bars.
//
// **The colours are the ones a Waveform uses**, so that the two read as one
// family: the accent for what is being measured, and the live colour for the
// mark above it.
Rectangle {
    id: well

    /// The block drawn, and what it is set to.
    required property SpectrumState reading
    /// Where the words go when a tile has room for few.
    property var absenceFont: AppFonts.tileCaption
    /// Whether this surface has room for the scales' numbers: a card has, a
    /// tile has not.
    property bool roomForLabels: false

    readonly property alias scope: spectrumItem
    readonly property alias absenceLabel: absence
    readonly property bool showsLabels: well.roomForLabels && well.reading.grid
    readonly property alias frequencyLabels: frequencyRepeater
    readonly property alias levelLabels: levelRepeater

    /// The strips the numbers go in.
    readonly property real labelWidth: Kirigami.Units.gridUnit * 2
    readonly property real labelHeight: Kirigami.Units.gridUnit

    color: AppColors.well
    radius: AppMetrics.controlRadius
    clip: true

    SpectrumItem {
        id: spectrumItem

        anchors {
            fill: parent
            leftMargin: well.showsLabels ? well.labelWidth : 0
            bottomMargin: well.showsLabels ? well.labelHeight : 0
        }
        hub: Meters.scopes
        blockId: well.reading.blockId
        refreshHz: well.reading.refreshHz
        motion: well.reading.motion
        bars: well.reading.words.barCountOf(well.reading.bars)
        frequencyScale: well.reading.scale
        frameSize: well.reading.words.fftSizeOf(well.reading.fftSize)
        floorDb: well.reading.words.floorDbOf(well.reading.floor)
        tiltDb: well.reading.words.tiltDbOf(well.reading.tilt)
        peakMarks: well.reading.peaks
        channels: well.reading.channels
        hold: well.reading.hold
        grid: well.reading.grid
        barStyle: well.reading.style
        barColor: AppColors.accent
        peakColor: AppColors.live
        gridColor: AppColors.scopeGrid
    }

    Repeater {
        id: frequencyRepeater

        model: well.showsLabels ? spectrumItem.frequencyMarks.filter(mark => mark.labelled) : []

        delegate: ElidedLabel {
            required property var modelData

            x: spectrumItem.x + modelData.position * spectrumItem.width - width / 2
            y: well.height - well.labelHeight
            height: well.labelHeight
            text: well.reading.words.frequencyText(modelData.hz)
            font: AppFonts.small
            color: AppColors.muted
            verticalAlignment: Text.AlignVCenter
            Accessible.ignored: true
        }
    }

    Repeater {
        id: levelRepeater

        model: well.showsLabels ? spectrumItem.levelMarks : []

        delegate: ElidedLabel {
            required property var modelData

            x: 0
            width: well.labelWidth - Kirigami.Units.smallSpacing
            y: spectrumItem.height * (1 - modelData.position) - height / 2
            text: well.reading.words.levelText(modelData.db)
            font: AppFonts.small
            color: AppColors.muted
            horizontalAlignment: Text.AlignRight
            Accessible.ignored: true
        }
    }

    ElidedLabel {
        id: absence

        anchors.fill: spectrumItem
        anchors.margins: Kirigami.Units.smallSpacing
        visible: !spectrumItem.showing
        text: well.reading.hold ? well.reading.words.status("", true) : well.reading.words.absence(spectrumItem.feedState)
        font: well.absenceFont
        color: AppColors.muted
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        wrapMode: Text.WordWrap
        Accessible.ignored: true
    }
}
