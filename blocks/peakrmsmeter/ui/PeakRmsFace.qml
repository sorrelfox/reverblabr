// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma ComponentBehavior: Bound

import QtQuick
import org.kde.kirigami as Kirigami
import org.kde.ki18n

// What a Peak/RMS block shows on the canvas: the two sides, four numbers to
// read them against, and a dot each for when they went over.
//
// **Bar, scale, bar**, as the design draws it: a narrow bar a side,
// the numbers between them, the RMS in the live colour and the tip up to the
// peak paler above it. Nothing is written under the bars. What they read is
// in the tooltip and said to a screen reader, and whether the block is live
// is the header's state dot.
//
// **Four marks, not twelve.** A tile is glanced at; −3, −9, −18 and −36 are
// enough to tell "near the top", "about right" and "nearly nothing" apart,
// and any more of them on something this size is a smear of digits.
//
// **Idle is not silence.** A session that is not running and one playing
// nothing are told apart by the header's state dot, which is green when the
// block is on and orange when sound is going through it, and by the reading
// in the tooltip.
//
// **A face only draws**. It does not tell the meter hub what this
// block's `peakHold` is, and neither does its panel: a hold the hub learns
// from something on the screen is a hold that depends on what has been
// drawn or opened, which is the defect. BlockModel tells the hub, for every
// meter block in the session, whether it has a tile yet or not.
Item {
    id: face

    /// The block whose face this is. **Not a required property**: a face is
    /// made from a Component in the register, and a Loader cannot hand a
    /// Component its properties, so it is set once the face exists.
    property string blockId: ""

    /// Whether the tip up to the peak is drawn: the block's own setting.
    readonly property bool showPeak: SessionModel.revision >= 0
                                     && SessionModel.params(face.blockId).showPeak !== false
    readonly property var levels: Meters.frame >= 0 ? Meters.levels(face.blockId) : ({})
    readonly property bool active: Meters.live && (face.levels.active ?? false)
    /// The loudest side, which is what a face has room to say.
    readonly property real peakDb: {
        const held = face.levels.holdDb ?? [];
        return held.length === 0 ? MeterScale.floorDb : Math.max(...held);
    }

    /// What the bars read, in words: the loudest side, or that nothing is
    /// running. The tile shows it as the face's tooltip.
    readonly property string detail: face.active
        ? KI18n.i18nc("@info the loudest side, in decibels below full scale", "%1 dB",
                      Number(face.peakDb).toLocaleString(Qt.locale(), 'f', 1))
        : KI18n.i18nc("@info nothing is running", "Idle")

    /// The parts themselves, so that a test can read what a person reads.
    readonly property alias meter: faceMeter

    implicitWidth: Kirigami.Units.gridUnit * 6
    implicitHeight: Kirigami.Units.gridUnit * 8

    Accessible.role: Accessible.StaticText
    Accessible.name: face.detail

    LevelMeter {
        id: faceMeter

        blockId: face.blockId
        scaleBetween: true
        showScale: true
        // The tile's four marks: what showScale decides is whether the
        // numbers are written, not how many of them there are.
        ticks: MeterScale.compactTicks
        channelClips: true
        showPeak: face.showPeak
        rmsColor: AppColors.live
        // **A paler live for the tip up to the peak**, the band the design
        // draws above the bar: the same hue, so it reads as more of the same
        // sound rather than as something else.
        peakColor: AppColors.meterHot
        holdColor: faceMeter.peakColor
        // Most of the face's height, as the design's bars are 52 of its 70.
        anchors.centerIn: parent
        height: Math.max(0, parent.height - Kirigami.Units.gridUnit)
        width: parent.width
    }
}
