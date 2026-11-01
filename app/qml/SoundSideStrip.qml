// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

// The strip, on the tile, on the card and on screen: **one drawing, so that what
// is seen in the card is what is seen above a game.**
//
// **Position does the work.** The word confirms it, and colour only makes the
// mark easy to find. Nothing here flashes: the plate keeps its brightness, the
// mark moves and then fades, and with no sound there is no mark at all. A
// mark in the middle would read as a sound straight ahead.
Rectangle {
    id: strip

    /// What SoundSideReadings shows, and the gate the bars start from.
    property var shown: ({})
    property real gate: -50
    /// "mark" or "bars".
    property string show: "mark"
    /// The word in its box, or empty for none.
    property string word: ""
    /// A dark plate with the ends named, for over a picture nobody chose.
    property bool plated: true
    property real markWidth: 7
    property int wordSize: 16
    /// No mark: nothing to show, or nothing heard.
    readonly property bool empty: strip.markStrength <= 0

    readonly property SoundSideWords words: SoundSideWords {}
    readonly property real markStrength: strip.shown.strength ?? 0
    readonly property real pan: strip.shown.pan ?? 0
    readonly property real opening: strip.shown.opening ?? 0
    /// Wide says no one place, narrow one place, and shape alone carries it.
    readonly property bool pale: (strip.shown.kind ?? "") === "both"
    readonly property color markColor: strip.plated ? AppColors.overlayMark : AppColors.liveMark
    readonly property color quietColor: strip.plated ? AppColors.overlayMuted : AppColors.muted

    /// Where the track starts inside the strip, for a scale under it.
    readonly property real trackLeft: row.x + trackItem.x

    /// The parts, for a test.
    readonly property alias mark: markItem
    readonly property alias track: trackItem
    readonly property alias wordLabel: wordText
    readonly property alias ghosts: ghostRepeater
    readonly property alias leftBar: leftBarItem
    readonly property alias rightBar: rightBarItem

    color: strip.plated ? AppColors.overlayPlate : "transparent"
    radius: strip.plated ? AppMetrics.barRadius : 0
    implicitHeight: 12

    RowLayout {
        id: row

        anchors.fill: parent
        anchors.leftMargin: strip.plated ? strip.height / 3 : 0
        anchors.rightMargin: strip.plated ? strip.height / 3 : 0
        anchors.topMargin: strip.plated ? strip.height / 6 : 0
        anchors.bottomMargin: strip.plated ? strip.height / 6 : 0
        spacing: strip.plated ? strip.height / 3 : 0

        Text {
            visible: strip.plated
            text: strip.words.leftEnd
            color: AppColors.overlayText
            font: AppFonts.stripWord(strip.wordSize)
            Accessible.ignored: true
        }

        Item {
            id: trackItem

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            /// Where a pan lands for something \a across wide.
            function xOf(pan: real, across: real): real {
                return Math.max(0, Math.min(trackItem.width - across, (pan + 1) / 2 * trackItem.width - across / 2));
            }

            Rectangle {
                anchors.fill: parent
                radius: AppMetrics.barRadius
                color: strip.plated ? AppColors.overlayTrack : AppColors.well
            }

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 1
                height: parent.height
                color: strip.quietColor
                opacity: 0.5
            }

            Repeater {
                id: ghostRepeater

                model: 3

                delegate: Rectangle {
                    id: ghost

                    required property int index
                    readonly property var past: (strip.shown.ghosts ?? [])[ghost.index] ?? ({})

                    visible: strip.show === "mark" && ghost.opacity > 0
                    x: trackItem.xOf(ghost.past.pan ?? 0, width)
                    width: strip.markWidth
                    height: trackItem.height
                    color: strip.markColor
                    opacity: ghost.past.strength ?? 0
                }
            }

            Rectangle {
                id: markItem

                visible: strip.show === "mark" && !strip.empty
                x: trackItem.xOf(strip.pan, width)
                width: Math.max(strip.markWidth, strip.opening * trackItem.width / 2)
                height: trackItem.height
                color: strip.markColor
                opacity: strip.markStrength * (strip.pale ? 0.55 : 1)
            }

            Rectangle {
                id: leftBarItem

                readonly property real share: strip.words.barShare(strip.shown.markLevel ?? -120, strip.pan, strip.gate, -1)

                visible: strip.show === "bars" && !strip.empty
                anchors.right: parent.horizontalCenter
                width: leftBarItem.share * trackItem.width / 2
                height: trackItem.height
                color: strip.pan <= 0 ? strip.markColor : strip.quietColor
                opacity: strip.markStrength
            }

            Rectangle {
                id: rightBarItem

                readonly property real share: strip.words.barShare(strip.shown.markLevel ?? -120, strip.pan, strip.gate, 1)

                visible: strip.show === "bars" && !strip.empty
                anchors.left: parent.horizontalCenter
                width: rightBarItem.share * trackItem.width / 2
                height: trackItem.height
                color: strip.pan > 0 ? strip.markColor : strip.quietColor
                opacity: strip.markStrength
            }
        }

        Text {
            visible: strip.plated
            text: strip.words.rightEnd
            color: AppColors.overlayText
            font: AppFonts.stripWord(strip.wordSize)
            Accessible.ignored: true
        }

        // A fixed box: the word never moves, so the eye knows where it is.
        Text {
            id: wordText

            visible: strip.plated && strip.word !== ""
            text: strip.word
            color: AppColors.overlayText
            font: AppFonts.stripWord(strip.wordSize)
            horizontalAlignment: Text.AlignHCenter
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 8
            Layout.preferredWidth: strip.wordSize * 4.5
            Layout.fillHeight: true
            verticalAlignment: Text.AlignVCenter
            Accessible.ignored: true
        }
    }
}
