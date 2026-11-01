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

// What the floating strip shows (ADR-0030): the plate, the mark and the word, for
// the block FloatingSurface.strip names.
//
// **Takes no focus and has no accessible role**: the card is the route to it,
// and a stream of "left, right, left" would bury everything else being said.
// It holds a frame and the keyboard only while Place Strip lasts.
//
// **A test or a sweep draws over the block's reading, on the strip alone**, as a
// person checking that it can be seen must not change what the card says.
Item {
    id: overlay

    readonly property var placement: FloatingSurface.placementRevision >= 0 ? FloatingSurface.placement("strip") : ({})
    readonly property var size: AppMetrics.stripSize(overlay.placement.size ?? "medium")
    readonly property alias strip: drawn
    readonly property alias placingWords: hint

    /// While a sweep runs, what it shows, as SoundSideReadings would.
    readonly property var sweepShown: ({
        kind: FloatingWindows.sweepPan < 0 ? "left" : FloatingWindows.sweepPan > 0 ? "right" : "centre",
        pan: FloatingWindows.sweepPan,
        strength: 1,
        opening: 0,
        ghosts: [],
        markLevel: -12
    })
    readonly property var shownNow: FloatingWindows.sweeping ? overlay.sweepShown : reading.shown

    implicitWidth: overlay.size.width
    implicitHeight: overlay.size.height + (FloatingWindows.placing ? hint.implicitHeight + Kirigami.Units.smallSpacing * 2 : 0)
    Accessible.ignored: true

    SoundSideState {
        id: reading

        sessionId: FloatingSurface.strip.sessionId ?? ""
        blockId: FloatingSurface.strip.blockId ?? ""
    }

    SoundSideStrip {
        id: drawn

        width: overlay.size.width
        height: overlay.size.height
        shown: overlay.shownNow
        gate: reading.gate
        show: overlay.placement.show ?? "mark"
        markWidth: overlay.size.mark
        wordSize: overlay.size.word
        word: reading.words.stripWord(overlay.shownNow.kind ?? "stopped")
    }

    Rectangle {
        anchors.fill: drawn
        visible: FloatingWindows.placing
        color: "transparent"
        border.color: AppColors.overlayMark
        border.width: 2
        radius: AppMetrics.barRadius
    }

    Rectangle {
        visible: FloatingWindows.placing
        anchors.top: drawn.bottom
        width: overlay.width
        height: overlay.height - drawn.height
        color: AppColors.overlayPlate

        Text {
            id: hint

            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.smallSpacing * 2
            text: KI18n.i18nc("@info while the strip is being placed", "Arrows move · Enter keeps · Esc puts it back")
            color: AppColors.overlayText
            font: AppFonts.stripWord(overlay.size.word > 12 ? 12 : overlay.size.word)
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            Accessible.ignored: true
        }
    }
}
