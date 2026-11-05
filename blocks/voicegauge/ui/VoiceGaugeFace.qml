// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.ki18n

// What a Voice Gauge shows on the canvas: a number, a line of words, a bar.
//
// **The words change and the colours do not.** The bar is one colour wherever
// the reading stands, and `dot` is never "ok" (VoiceGaugeWords.dot()).
Item {
    id: face

    /// The block whose face this is, set once the face exists.
    property string blockId: ""

    readonly property VoiceGaugeState reading: VoiceGaugeState {
        blockId: face.blockId
    }
    readonly property VoiceGaugeWords words: face.reading.words
    readonly property string kind: face.reading.kind

    readonly property string headline: face.reading.headline
    readonly property string caption: face.reading.line
    /// The tile's dot: "live", "warning" or "off".
    readonly property string dot: face.words.dot(face.kind)

    readonly property alias headlineLabel: headlineText
    readonly property alias captionLabel: captionText
    readonly property alias bar: track

    implicitWidth: Kirigami.Units.gridUnit * 10
    implicitHeight: Kirigami.Units.gridUnit * 5

    Accessible.role: Accessible.StaticText
    Accessible.name: face.caption === ""
        ? face.headline
        : KI18n.i18nc("@info what a Voice Gauge tile says, '-24 LUFS, Between marks'", "%1, %2", face.headline, face.caption)

    ColumnLayout {
        anchors {
            left: parent.left
            right: parent.right
            verticalCenter: parent.verticalCenter
            leftMargin: AppMetrics.faceMargin
            rightMargin: AppMetrics.faceMargin
        }
        spacing: AppMetrics.faceSpacing

        ElidedLabel {
            id: headlineText

            text: face.headline
            font: face.words.showsNumber(face.kind) ? AppFonts.readout : AppFonts.tileText
            color: face.kind === "off" ? AppColors.muted : AppColors.text
            elide: Text.ElideRight
            Layout.fillWidth: true
            Accessible.ignored: true
        }

        ElidedLabel {
            id: captionText

            visible: face.caption !== ""
            text: face.caption
            font: AppFonts.tileCaption
            color: face.kind === "noSound" ? AppColors.warningText : AppColors.muted
            elide: Text.ElideRight
            Layout.fillWidth: true
            Accessible.ignored: true
        }

        VoiceGaugeBar {
            id: track

            level: face.reading.level
            shown: face.words.showsNumber(face.kind)
            held: face.kind === "held"
            upper: face.reading.marks.upper
            lower: face.reading.marks.lower
            Layout.fillWidth: true
        }
    }
}
