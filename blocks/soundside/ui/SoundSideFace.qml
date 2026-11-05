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

// What a Sound Side shows on the canvas: a word, the strip made small, a caption.
// The words and the mark come from SoundSideReadings, as the card's and the
// strip's do.
Item {
    id: face

    /// The block whose face this is, set once the face exists.
    property string blockId: ""

    readonly property SoundSideState reading: SoundSideState {
        blockId: face.blockId
    }
    readonly property string kind: face.reading.kind
    readonly property string headline: face.reading.word
    readonly property string caption: face.reading.caption
    /// The tile's dot: "ok", "live", "warning" or "off".
    readonly property string dot: face.reading.words.dot(face.kind)

    readonly property alias headlineLabel: headlineText
    readonly property alias captionLabel: captionText
    readonly property alias strip: track

    implicitWidth: Kirigami.Units.gridUnit * 10
    implicitHeight: Kirigami.Units.gridUnit * 5

    Accessible.role: Accessible.StaticText
    Accessible.name: face.caption === ""
        ? face.headline
        : KI18n.i18nc("@info what a Sound Side tile says, 'Left, 62 % · -31 dBFS'", "%1, %2", face.headline, face.caption)

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
            font: face.kind === "quiet" || face.kind === "stopped" || face.kind === "off" ? AppFonts.tileText : AppFonts.strong
            color: face.kind === "quiet" || face.kind === "off" ? AppColors.muted : AppColors.text
            elide: Text.ElideRight
            Layout.fillWidth: true
            Accessible.ignored: true
        }

        SoundSideStrip {
            id: track

            visible: face.kind !== "off"
            plated: false
            shown: face.reading.shown
            markWidth: 3
            implicitHeight: 12
            Layout.fillWidth: true
            Accessible.ignored: true
        }

        ElidedLabel {
            id: captionText

            visible: face.caption !== ""
            text: face.caption
            font: AppFonts.tileCaption
            color: face.kind === "same" ? AppColors.warningText : AppColors.muted
            elide: Text.ElideRight
            Layout.fillWidth: true
            Accessible.ignored: true
        }
    }
}
