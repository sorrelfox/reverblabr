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

// What a Ducking block shows on the canvas: where it starts working,
// how far down it takes the background, and — while it is doing it — how
// far down the background is now.
//
// **Laid out like the Volume tile**: the number first, what it is beside
// it, and under them a slider for the one setting worth reaching for, the
// ducked volume, in the block's category colour. While the block is ducking
// the caption says so in the live colour, with the amount.
Item {
    id: face

    /// The block whose face this is, set once the face exists.
    property string blockId: ""

    readonly property var params: SessionModel.paramRevision >= 0 ? SessionModel.params(face.blockId) : ({})
    readonly property real thresholdDb: face.params.threshold ?? -30
    readonly property real duckedVolume: face.params.duckedVolume ?? 0.4
    readonly property bool blockEnabled: SessionModel.revision >= 0 && (SessionModel.block(face.blockId).enabled ?? true)
    readonly property color tint: face.blockEnabled
        ? AppColors.categoryTint(BlockPresentation.category("advanced.ducking"))
        : AppColors.muted

    /// What the block published last: how far down the background is and
    /// whether it is ducking. **Settable**, so a test can say what a running
    /// block would have.
    property var published: Meters.frame >= 0 && Meters.live ? (Meters.levels(face.blockId).values ?? []) : []
    readonly property bool ducking: face.blockEnabled && (face.published[words.activeValue] ?? 0) > 0.5
    readonly property real duckingDb: face.published[words.duckingValue] ?? 0

    readonly property string headline: words.decibelText(face.thresholdDb)
    readonly property string caption: face.ducking
        ? KI18n.i18nc("@info the background is turned down by this much now, 'ducking -8.0 dB'", "ducking %1",
                      words.decibelText(face.duckingDb))
        : KI18n.i18nc("@info the background is turned down to this while the main source is heard, 'duck to 40 %'",
                      "duck to %1", words.percentText(face.duckedVolume))
    /// Everything there is to say, for the tooltip.
    readonly property string detail: KI18n.i18nc(
        "@info:tooltip what a Ducking block does, 'Above -30.0 dB on the main input, the background goes to 40 %'",
        "Above %1 on the main input, the background goes to %2", words.decibelText(face.thresholdDb),
        words.percentText(face.duckedVolume))

    readonly property alias headlineLabel: headlineText
    readonly property alias captionLabel: captionText
    readonly property alias slider: faceSlider
    readonly property alias words: words

    implicitWidth: Kirigami.Units.gridUnit * 12
    implicitHeight: Kirigami.Units.gridUnit * 6

    DuckingWords {
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

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            QQC2.Label {
                id: headlineText

                text: face.headline
                font: AppFonts.readout
                lineHeightMode: AppMetrics.followsSystem ? Text.ProportionalHeight : Text.FixedHeight
                lineHeight: AppMetrics.followsSystem ? 1.0 : AppFonts.readoutPixels
                color: face.blockEnabled ? AppColors.text : AppColors.muted
                Layout.alignment: Qt.AlignBaseline
            }

            Item {
                Layout.fillWidth: true
            }

            ElidedLabel {
                id: captionText

                text: face.caption
                font: AppFonts.tileCaption
                color: face.ducking ? AppColors.liveText : AppColors.muted
                elide: Text.ElideRight
                Layout.alignment: Qt.AlignBaseline
                Accessible.ignored: true
            }
        }

        // The ducked volume, from silence to untouched, as the panel's
        // slider.
        FaceSlider {
            id: faceSlider

            Layout.fillWidth: true
            Layout.topMargin: AppMetrics.followsSystem ? 0 : (AppMetrics.sliderGrooveHeight - implicitHeight) / 2
            Layout.bottomMargin: AppMetrics.followsSystem ? 0 : (AppMetrics.sliderGrooveHeight - implicitHeight) / 2
            tint: face.tint
            from: 0
            to: 100
            Accessible.name: KI18n.i18nc("@label:slider how far the Ducking block turns the background down",
                                         "Ducked volume")

            onMoved: SessionModel.setParam(face.blockId, "duckedVolume", value / 100, true)
            onPressedChanged: if (!pressed) {
                SessionModel.commitParam(face.blockId, "duckedVolume");
            }

            SessionBound {
                control: faceSlider
                property: "value"
                value: face.duckedVolume * 100
                suspended: faceSlider.pressed
            }
        }
    }
}
