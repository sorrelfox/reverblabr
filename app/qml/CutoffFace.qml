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
import org.kde.ki18n

// What a cutoff filter shows on the canvas — the Low Pass Filter and
// the High Pass Filter: where its cutoff is, and a slider to move it.
//
// **Laid out like the Volume tile**, so the blocks with one number
// on them read alike: the cutoff first in the fixed-width face, and under
// it the cutoff's slider on a thin track in the effects colour. The slider
// is logarithmic, as the panel's is, so the two are one control for one
// number.
//
// **The slope is not on the tile.** "20.0 kHz" fills the fixed-width face
// across a tile, and a slope squeezed in beside it cut the cutoff short
// ("8.00 k…"), which is the one thing the face is for. The tooltip and the
// panel say it.
Item {
    id: face

    /// The block whose face this is, set once the face exists.
    property string blockId: ""

    readonly property var params: SessionModel.paramRevision >= 0 ? SessionModel.params(face.blockId) : ({})
    readonly property real cutoff: face.params.cutoff ?? face.defaultHz
    readonly property string slope: face.params.slope ?? "12dB"
    readonly property bool blockEnabled: SessionModel.revision >= 0 && (SessionModel.block(face.blockId).enabled ?? true)
    /// The effects colour, or grey for a block that is off.
    readonly property color tint: face.blockEnabled
        ? AppColors.categoryTint(BlockPresentation.category(face.typeId))
        : AppColors.muted

    /// The block's type: "effect.lowpass" or "effect.highpass".
    property string typeId: ""

    /// The cutoff's range and where it starts, **from the engine's own
    /// description** rather than written down here again: the two filters
    /// differ in nothing else the interface needs to know.
    readonly property var described: BlockCatalogue.ready && face.typeId !== ""
        ? BlockCatalogue.describeParam(face.typeId, "cutoff") : ({})
    readonly property real lowestHz: face.described.minimum ?? 20
    readonly property real highestHz: face.described.maximum ?? 20000
    readonly property real defaultHz: face.described.default ?? face.highestHz

    /// Everything there is to say, for the tooltip.
    readonly property string detail: KI18n.i18nc("@info:tooltip a cutoff filter's settings, 'Cutoff 8.00 kHz, 12 dB per octave'",
                                                 "Cutoff %1, %2", words.frequencyText(face.cutoff),
                                                 words.slopeDescription(face.slope))

    readonly property alias headlineLabel: headlineText
    readonly property alias slider: cutoffSlider
    readonly property alias words: words

    implicitWidth: Kirigami.Units.gridUnit * 12
    implicitHeight: Kirigami.Units.gridUnit * 6

    FilterWords {
        id: words
    }

    ColumnLayout {
        anchors {
            left: parent.left
            right: parent.right
            verticalCenter: parent.verticalCenter
            leftMargin: Kirigami.Units.smallSpacing * 3
            rightMargin: Kirigami.Units.smallSpacing * 3
        }
        spacing: Kirigami.Units.smallSpacing * 2

        ElidedLabel {
            id: headlineText

            // **The slider's own value, not the session's**, as on the
            // Volume tile: the number follows the hand through a drag.
            text: words.frequencyText(words.frequencyAt(cutoffSlider.value, face.lowestHz, face.highestHz))
            font: AppFonts.readout
            lineHeightMode: AppMetrics.followsSystem ? Text.ProportionalHeight : Text.FixedHeight
            lineHeight: AppMetrics.followsSystem ? 1.0 : AppFonts.readoutPixels
            color: face.blockEnabled ? AppColors.text : AppColors.muted
            elide: Text.ElideRight
            Layout.fillWidth: true
        }

        FaceSlider {
            id: cutoffSlider

            Layout.fillWidth: true
            Layout.topMargin: AppMetrics.followsSystem ? 0 : (AppMetrics.sliderGrooveHeight - implicitHeight) / 2
            Layout.bottomMargin: AppMetrics.followsSystem ? 0 : (AppMetrics.sliderGrooveHeight - implicitHeight) / 2
            tint: face.tint
            from: 0
            to: 1
            Accessible.name: KI18n.i18nc("@label:slider a cutoff filter's cutoff frequency", "Cutoff")

            onMoved: SessionModel.setParam(face.blockId, "cutoff",
                                           words.frequencyAt(value, face.lowestHz, face.highestHz), true)
            onPressedChanged: if (!pressed) {
                SessionModel.commitParam(face.blockId, "cutoff");
            }

            SessionBound {
                control: cutoffSlider
                property: "value"
                value: words.positionOf(face.cutoff, face.lowestHz, face.highestHz)
                suspended: cutoffSlider.pressed
            }
        }
    }
}
