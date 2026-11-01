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

// What a Speech Now shows on the canvas: a word and, under it, how it knows
// or why it can't. The header carries its name, so "Call" and "You" read as
// two talkers.
//
// **The dot says the block is working** (`dot`), never that a level is fine.
Item {
    id: face

    /// The block whose face this is, set once the face exists.
    property string blockId: ""

    readonly property SpeechNowState reading: SpeechNowState {
        blockId: face.blockId
    }
    readonly property SpeechNowWords words: face.reading.words
    readonly property string kind: face.reading.kind

    readonly property string headline: face.words.word(face.kind)
    readonly property string caption: {
        switch (face.kind) {
        case "off":
            return "";
        case "notWatching":
            return face.words.reason(face.reading.watch);
        case "again":
            return KI18n.i18nc("@info what a Speech Now tile says just after a voice stops", "Voice stopped");
        }
        return face.words.detectorName(face.reading.detector);
    }
    /// The tile's dot: "ok", "live", "warning" or "off".
    readonly property string dot: face.words.dot(face.kind)

    readonly property alias headlineLabel: headlineText
    readonly property alias captionLabel: captionText

    implicitWidth: Kirigami.Units.gridUnit * 8
    implicitHeight: Kirigami.Units.gridUnit * 5

    Accessible.role: Accessible.StaticText
    Accessible.name: face.caption === ""
        ? face.headline
        : KI18n.i18nc("@info what a Speech Now tile says, 'Speaking, Voice model'", "%1, %2", face.headline, face.caption)

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
            color: face.kind === "speaking" || face.kind === "again" ? AppColors.liveText : AppColors.muted
            elide: Text.ElideRight
            Layout.fillWidth: true
            Accessible.ignored: true
        }
    }
}
