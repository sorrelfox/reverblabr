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

// What a One Ear Mix shows on the canvas: where the sound goes and, under
// it, the route in words. A choice that sets the sound is no curve to draw.
//
// **The dot is the block's own** (`dot`), and says the block is working, not
// that a level is fine.
Item {
    id: face

    /// The block whose face this is, set once the face exists.
    property string blockId: ""

    /// "No change" with no ear picked is true of a stopped session too.
    readonly property bool stopped: reading.acting && !reading.live
    readonly property bool off: !reading.blockEnabled

    readonly property string headline: face.off
        ? KI18n.i18nc("@info a One Ear Mix that is switched off", "Off")
        : face.stopped ? KI18n.i18nc("@info a One Ear Mix whose session is not running", "Not running")
                       : words.sendName(reading.sendTo)
    readonly property string caption: {
        if (face.off) {
            return KI18n.i18nc("@info what comes out of a One Ear Mix that is switched off", "Stereo, as it came");
        }
        if (face.stopped) {
            return KI18n.i18nc("@info a One Ear Mix whose session is not running", "Session stopped");
        }
        if (reading.arriving) {
            return words.arriving(reading.sendTo);
        }
        if (reading.acting && reading.hear === "mono") {
            return words.plainMono(reading.sendTo);
        }
        return reading.lowered ? words.lowered(reading.sendTo, reading.lowerDb) : words.route(reading.sendTo);
    }
    /// The tile's dot: "ok", "live", "warning" or "off".
    readonly property string dot: face.off || face.stopped ? "off"
        : reading.arriving ? "live"
        : reading.lowered ? "warning"
        : "ok"
    /// Everything there is to say, for the tooltip.
    readonly property string detail: face.off ? "" : words.explanation(reading.sendTo)

    readonly property alias headlineLabel: headlineText
    readonly property alias captionLabel: captionText

    implicitWidth: Kirigami.Units.gridUnit * 8
    implicitHeight: Kirigami.Units.gridUnit * 5

    Accessible.role: Accessible.StaticText
    Accessible.name: KI18n.i18nc("@info what a One Ear Mix tile says, 'Left ear, L+R one mix to L'", "%1, %2", face.headline, face.caption)
    Accessible.description: face.detail

    OneEarMixWords {
        id: words
    }

    OneEarMixState {
        id: reading

        blockId: face.blockId
    }

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
            font: AppFonts.tileText
            color: face.off ? AppColors.muted : AppColors.text
            elide: Text.ElideRight
            Layout.fillWidth: true
            Accessible.ignored: true
        }

        ElidedLabel {
            id: captionText

            text: face.caption
            font: AppFonts.tileCaption
            color: reading.arriving && !face.off ? AppColors.liveText : AppColors.muted
            elide: Text.ElideRight
            Layout.fillWidth: true
            Accessible.ignored: true
        }
    }
}
