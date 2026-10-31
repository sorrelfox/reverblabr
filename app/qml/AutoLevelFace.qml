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

// What an Auto Level shows on the canvas: what it is doing to the sound
// right now.
//
// **A value face, as the design draws this block**: a number in whole dB in
// the fixed-width face, a word under it, and under them a track from Maximum
// cut to Maximum boost with a mark at zero. With the cut off the track starts
// at zero, so it reads as a boost alone. While nothing plays there is nothing
// to show, so the number is the setting instead: "On" or "Off".
//
// **The track is not a slider.** The gain is the block's to decide, moment
// by moment; what a person sets is in the panel.
Item {
    id: face

    /// The block whose face this is, set once the face exists.
    property string blockId: ""

    readonly property AutoLevelState reading: AutoLevelState {
        blockId: face.blockId
        keepsHistory: true
    }
    readonly property AutoLevelWords words: face.reading.words
    readonly property var settings: face.reading.settings

    readonly property string stateWord: face.reading.state
    readonly property bool running: face.stateWord !== "stopped" && face.stateWord !== "off"
    readonly property real boost: face.reading.shownGainDb

    /// The state dot (BlockTile.dotColor()).
    readonly property string dot: {
        switch (face.stateWord) {
        case "lifting":
        case "down":
        case "limit":
        case "pulled":
            return "live";
        case "left":
        case "quiet":
        case "bothOff":
            return "ok";
        }
        return "off";
    }

    /// The effects colour, or grey for a block that is off.
    readonly property color tint: face.reading.blockEnabled && face.reading.anyOn
        ? AppColors.categoryTint(BlockPresentation.category("effect.autolevel"))
        : AppColors.muted

    readonly property string headline: face.running
        ? face.words.boostText(face.boost)
        : (face.stateWord !== "off" && face.reading.anyOn
            ? KI18n.i18nc("@info Auto Level is switched on", "On")
            : KI18n.i18nc("@info Auto Level is switched off", "Off"))
    /// The program's own word takes the place of the block's while it runs.
    readonly property bool showsMusic: face.running && face.reading.declaredMusic
    readonly property string caption: face.showsMusic
        ? KI18n.i18nc("@label what an Auto Level tile says when the program declares music", "program says music")
        : face.words.captionOf(face.stateWord)
    /// Everything there is to say, for the tooltip.
    readonly property string detail: {
        const target = face.words.loudnessText(face.settings.target);
        const boostLimit = face.words.decibelText(face.settings.maximumBoost);
        const cutLimit = face.words.decibelText(face.settings.maximumCut);
        if (face.settings.boostOn && face.settings.cutOn) {
            return KI18n.i18nc("@info:tooltip what Auto Level is set to, 'Towards -23 LUFS, up by at most 18 dB and down by at most 18 dB'",
                               "Towards %1, up by at most %2 and down by at most %3", target, boostLimit, cutLimit);
        }
        if (face.settings.boostOn) {
            return KI18n.i18nc("@info:tooltip what Auto Level is set to, 'Lifts towards -20 LUFS, by up to 18 dB'",
                               "Lifts towards %1, by up to %2", target, boostLimit);
        }
        if (face.settings.cutOn) {
            return KI18n.i18nc("@info:tooltip what Auto Level is set to, 'Turns down towards -20 LUFS, by up to 18 dB'",
                               "Turns down towards %1, by up to %2", target, cutLimit);
        }
        return KI18n.i18nc("@info:tooltip", "Switched off: passes the sound through");
    }

    readonly property alias headlineLabel: headlineText
    readonly property alias captionLabel: captionText

    /// The track runs from the most it may take off to the most it may add.
    readonly property real span: (face.settings.boostOn ? face.settings.maximumBoost : 0)
                                 + (face.settings.cutOn ? face.settings.maximumCut : 0)
    /// Where zero is on the track, from 0 to 1.
    readonly property real zeroAt: face.span > 0 && face.settings.cutOn ? face.settings.maximumCut / face.span : 0
    /// How much of the track is filled, from 0 to 1, and which side of zero.
    readonly property real fill: face.span > 0 ? Math.min(Math.abs(face.boost) / face.span,
                                                          face.boost < 0 ? face.zeroAt : 1 - face.zeroAt) : 0

    implicitWidth: Kirigami.Units.gridUnit * 12
    implicitHeight: Kirigami.Units.gridUnit * 6

    Accessible.role: Accessible.StaticText
    Accessible.name: face.caption === ""
        ? face.headline
        : KI18n.i18nc("@info what an Auto Level tile says, '+6 dB, lifting'", "%1, %2", face.headline, face.caption)

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
            color: face.reading.blockEnabled && face.reading.anyOn ? AppColors.text : AppColors.muted
            Accessible.ignored: true
        }

        QQC2.Label {
            id: captionText

            Layout.fillWidth: true
            text: face.caption
            font: AppFonts.tileCaption
            color: face.showsMusic ? AppColors.warningText : AppColors.muted
            elide: Text.ElideRight
            Accessible.ignored: true
        }

        // The design's thin track, filled from zero as far as the gain has
        // gone, either way.
        Rectangle {
            id: track

            Layout.fillWidth: true
            implicitHeight: AppMetrics.sliderGrooveHeight
            radius: AppMetrics.troughRadius
            color: AppColors.well

            Rectangle {
                x: track.width * (face.boost < 0 ? face.zeroAt - face.fill : face.zeroAt)
                width: track.width * face.fill
                height: track.height
                radius: track.radius
                color: face.tint
            }

            // The mark at zero. With the cut off zero is the track's end.
            Rectangle {
                visible: face.settings.cutOn
                x: track.width * face.zeroAt - width / 2
                y: -height / 2 + track.height / 2
                width: 1
                height: track.height * 2
                color: AppColors.text
            }
        }
    }
}
