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

// What a Night Level shows on the canvas: the change it makes now, in the
// fixed-width face, a word, and the level it goes by. While nothing plays the
// number is the setting: "On" or "Off".
//
// **This tile speaks twice and no more:** when a level is found and when it
// goes. It lives as long as the block, and the gain as it moves would talk over
// the film.
Item {
    id: face

    /// The block whose face this is, set once the face exists.
    property string blockId: ""

    readonly property NightLevelState reading: NightLevelState {
        blockId: face.blockId
        keepsHistory: true
    }
    readonly property NightLevelWords words: face.reading.words

    readonly property string kind: face.reading.kind
    readonly property string dot: face.words.dotOf(face.kind)
    readonly property bool showsMusic: face.reading.running && face.reading.declaredMusic

    readonly property string headline: face.reading.running
        ? face.words.common.boostText(face.reading.shownGainDb)
        : (face.kind !== "off" && face.reading.anyAmount ? KI18n.i18nc("@info Night Level is switched on", "On")
                                                         : KI18n.i18nc("@info Night Level is switched off", "Off"))
    readonly property string caption: face.showsMusic
        ? KI18n.i18nc("@label what a Night Level tile says when the program declares music", "program says music")
        : face.words.captionOf(face.kind)
    readonly property string levelLine: face.reading.levelLine
    /// Everything there is to say, for the tooltip.
    readonly property string detail: face.reading.anyAmount
        ? KI18n.i18nc("@info:tooltip what Night Level is set to, 'Loud sounds 100 %, quiet sounds 50 %'",
                      "Loud sounds %1 %, quiet sounds %2 %", Math.round(face.reading.cut), Math.round(face.reading.boost))
        : KI18n.i18nc("@info:tooltip", "Set to nothing: passes the sound through")

    readonly property alias headlineLabel: headlineText
    readonly property alias captionLabel: captionText
    readonly property alias levelLabel: levelText

    implicitWidth: Kirigami.Units.gridUnit * 12
    implicitHeight: Kirigami.Units.gridUnit * 6

    Accessible.role: Accessible.StaticText
    Accessible.name: [face.headline, face.caption, face.levelLine].filter(part => part !== "").join(", ")

    Connections {
        target: face.reading

        function onLevelFound(lufs: real) {
            Announcer.say(face, "nightLevel:" + face.blockId,
                          KI18n.i18nc("@info:status spoken, Night Level found a level, 'Speech at -27 LUFS'",
                                      "Speech at %1", face.words.common.loudnessText(lufs)));
        }

        function onLevelLost() {
            Announcer.say(face, "nightLevel:" + face.blockId, KI18n.i18nc("@info:status Night Level", "No speech yet"));
        }
    }

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
            color: face.reading.blockEnabled && face.reading.anyAmount ? AppColors.text : AppColors.muted
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

        QQC2.Label {
            id: levelText

            Layout.fillWidth: true
            visible: face.levelLine !== ""
            text: face.levelLine
            font: AppFonts.tileCaption
            color: AppColors.muted
            elide: Text.ElideRight
            Accessible.ignored: true
        }
    }
}
