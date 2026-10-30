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

// What an Auto Level shows on the canvas: how much it is lifting the
// sound right now.
//
// **A value face, as the design draws this block**: the number in the
// fixed-width face with a small "boost" beside it, and under them a track
// in the effects colour filled as far as the boost has gone towards the
// most it may add. While nothing plays there is no boost to show, so the
// number is the setting instead: "On" or "Off".
//
// **The track is not a slider.** The boost is the block's to decide, moment
// by moment; what a person sets is on or off, in the panel.
Item {
    id: face

    /// The block whose face this is, set once the face exists.
    property string blockId: ""

    readonly property var params: SessionModel.paramRevision >= 0 ? SessionModel.params(face.blockId) : ({})
    readonly property bool boostOn: face.params.boost ?? true
    readonly property real maximum: face.params.maximumBoost ?? words.defaultMaximum
    readonly property bool blockEnabled: SessionModel.revision >= 0 && (SessionModel.block(face.blockId).enabled ?? true)

    readonly property var levels: Meters.frame >= 0 ? Meters.levels(face.blockId) : ({})
    /// Whether the block is running and has something to say.
    readonly property bool live: Meters.live && face.blockEnabled && (face.levels.values ?? []).length > words.boostValue
    readonly property real boost: face.live ? (face.levels.values[words.boostValue] ?? 0) : 0

    /// The effects colour, or grey for a block that is off.
    readonly property color tint: face.blockEnabled && face.boostOn
        ? AppColors.categoryTint(BlockPresentation.category("effect.autolevel"))
        : AppColors.muted

    readonly property string headline: face.live && face.boostOn
        ? words.boostText(face.boost)
        : (face.boostOn ? KI18n.i18nc("@info Auto Level is lifting quiet passages", "On")
                        : KI18n.i18nc("@info Auto Level is not lifting anything", "Off"))
    /// Everything there is to say, for the tooltip.
    readonly property string detail: face.boostOn
        ? KI18n.i18nc("@info:tooltip what Auto Level is set to, 'Lifts towards -20 LUFS, by up to 18 dB'",
                      "Lifts towards %1, by up to %2",
                      words.loudnessText(face.params.target ?? words.defaultTarget),
                      words.decibelText(face.maximum))
        : KI18n.i18nc("@info:tooltip", "Switched off: passes the sound through")

    readonly property alias headlineLabel: headlineText
    readonly property alias captionLabel: captionText
    /// How much of the track is filled, from 0 to 1.
    readonly property real fill: face.maximum > 0 ? Math.max(0, Math.min(1, face.boost / face.maximum)) : 0

    implicitWidth: Kirigami.Units.gridUnit * 12
    implicitHeight: Kirigami.Units.gridUnit * 6

    Accessible.role: Accessible.StaticText
    Accessible.name: KI18n.i18nc("@info what an Auto Level tile says, 'Boost +6.2 dB'", "Boost %1", face.headline)

    AutoLevelWords {
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
                color: face.blockEnabled && face.boostOn ? AppColors.text : AppColors.muted
                Layout.alignment: Qt.AlignBaseline
                Accessible.ignored: true
            }

            Item {
                Layout.fillWidth: true
            }

            QQC2.Label {
                id: captionText

                text: KI18n.i18nc("@label what the number on an Auto Level tile is", "boost")
                font: AppFonts.tileCaption
                color: AppColors.muted
                Layout.alignment: Qt.AlignBaseline
                Accessible.ignored: true
            }
        }

        // The design's thin track, filled as far as the boost has gone.
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
