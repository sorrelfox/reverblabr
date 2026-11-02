// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// What a Voice Compressor shows on the canvas: what it takes off now, in the
// fixed-width face, a word, and its settings. While nothing plays the number
// is 0 dB and the settings line says what it is set to.
//
// **Read on request, never announced**: the number moves all the time, and a
// tile that spoke it would talk over the voice it works on.
Item {
    id: face

    /// The block whose face this is, set once the face exists.
    property string blockId: ""

    readonly property VoiceCompressorState reading: VoiceCompressorState {
        blockId: face.blockId
    }
    readonly property VoiceCompressorWords words: face.reading.words

    readonly property string kind: face.reading.kind
    readonly property string dot: face.words.dotOf(face.kind)
    readonly property string headline: face.words.reductionText(face.reading.shownTotalDb)
    readonly property string caption: face.words.captionOf(face.kind)
    readonly property string settingsLine: face.words.settingsLine(face.reading.compression, face.reading.ceiling)
    readonly property string tileLine: face.words.tileLine(face.reading.compression, face.reading.ceiling)
    /// Everything there is to say, for the tooltip.
    readonly property string detail: face.settingsLine + ", " + face.words.gateLine(face.reading.gate)

    readonly property alias headlineLabel: headlineText
    readonly property alias captionLabel: captionText
    readonly property alias settingsLabel: settingsText

    implicitWidth: Kirigami.Units.gridUnit * 12
    implicitHeight: Kirigami.Units.gridUnit * 6

    Accessible.role: Accessible.StaticText
    Accessible.name: [face.headline, face.caption, face.settingsLine].join(", ")

    ColumnLayout {
        anchors {
            left: parent.left
            right: parent.right
            verticalCenter: parent.verticalCenter
            leftMargin: Kirigami.Units.smallSpacing * 3
            rightMargin: Kirigami.Units.smallSpacing * 3
        }
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            id: headlineText

            Layout.fillWidth: true
            text: face.headline
            font: AppFonts.readout
            lineHeightMode: AppMetrics.followsSystem ? Text.ProportionalHeight : Text.FixedHeight
            lineHeight: AppMetrics.followsSystem ? 1.0 : AppFonts.readoutPixels
            color: face.reading.blockEnabled ? AppColors.text : AppColors.muted
            Accessible.ignored: true
        }

        QQC2.Label {
            id: captionText

            Layout.fillWidth: true
            text: face.caption
            font: AppFonts.tileCaption
            color: AppColors.muted
            elide: Text.ElideRight
            Accessible.ignored: true
        }

        QQC2.Label {
            id: settingsText

            Layout.fillWidth: true
            text: face.tileLine
            font: AppFonts.tileCaption
            color: AppColors.muted
            elide: Text.ElideRight
            Accessible.ignored: true
        }
    }
}
