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

// What a Limiter shows on the canvas: how hard it is holding back.
//
// **A value face**, laid out like Auto Level's: while the session runs, the
// gain it is taking off ("−3.2 dB") with a small "reduction" beside it and a
// track filled as far as that has gone; while it does not, the ceiling it
// holds to ("−1.0 dBTP", "ceiling"), which is what somebody placing one
// wants to see. The track is not a slider: the reduction is the block's.
Item {
    id: face

    /// The block whose face this is, set once the face exists.
    property string blockId: ""

    readonly property var params: SessionModel.paramRevision >= 0 ? SessionModel.params(face.blockId) : ({})
    readonly property real ceiling: face.params.ceiling ?? words.defaultCeiling
    readonly property bool truePeak: face.params.truePeak ?? true
    readonly property real release: face.params.release ?? words.defaultRelease
    readonly property bool blockEnabled: SessionModel.revision >= 0 && (SessionModel.block(face.blockId).enabled ?? true)

    readonly property var levels: Meters.frame >= 0 ? Meters.levels(face.blockId) : ({})
    /// Whether the block is running and has something to say.
    readonly property bool live: Meters.live && face.blockEnabled
                                 && (face.levels.values ?? []).length > words.reductionValue
    readonly property real reduction: face.live ? (face.levels.values[words.reductionValue] ?? 0) : 0

    /// The effects colour, or grey for a block that is off.
    readonly property color tint: face.blockEnabled
        ? AppColors.categoryTint(BlockPresentation.category("effect.limiter"))
        : AppColors.muted

    // **The unit's letters go in the caption**, "TP ceiling", not after the
    // number: "-1.0 dBTP" in the fixed-width face left no room beside it.
    readonly property string headline: face.live ? words.reductionText(face.reduction)
                                                 : words.decibelText(face.ceiling)
    readonly property string caption: face.live
        ? KI18n.i18nc("@label what the number on a running Limiter tile is", "reduction")
        : (face.truePeak ? KI18n.i18nc("@label a Limiter's ceiling, as a true peak, on its tile", "TP ceiling")
                         : KI18n.i18nc("@label a Limiter's ceiling, as a sample peak, on its tile", "ceiling"))
    /// Everything there is to say, for the tooltip.
    readonly property string detail: KI18n.i18nc("@info:tooltip a Limiter's settings, 'Ceiling -1.0 dBTP, release 100 ms'",
                                                 "Ceiling %1, release %2", words.ceilingText(face.ceiling, face.truePeak),
                                                 words.releaseText(face.release))

    readonly property alias headlineLabel: headlineText
    readonly property alias captionLabel: captionText
    /// How much of the track is filled, from 0 to 1.
    readonly property real fill: Math.max(0, Math.min(1, face.reduction / words.fullReduction))

    implicitWidth: Kirigami.Units.gridUnit * 12
    implicitHeight: Kirigami.Units.gridUnit * 6

    Accessible.role: Accessible.StaticText
    Accessible.name: KI18n.i18nc("@info what a Limiter tile says, 'reduction -3.2 dB'", "%1 %2", face.caption, face.headline)

    LimiterWords {
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
                Accessible.ignored: true
            }

            Item {
                Layout.fillWidth: true
            }

            QQC2.Label {
                id: captionText

                text: face.caption
                font: AppFonts.tileCaption
                color: AppColors.muted
                Layout.alignment: Qt.AlignBaseline
                Accessible.ignored: true
            }
        }

        // The design's thin track, filled as far as the reduction has gone.
        Rectangle {
            id: track

            Layout.fillWidth: true
            implicitHeight: AppMetrics.sliderGrooveHeight
            radius: AppMetrics.troughRadius
            color: AppColors.well

            Rectangle {
                width: track.width * face.fill
                height: track.height
                radius: track.radius
                color: face.tint
            }
        }
    }
}
