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

// What a Sync block shows on the canvas: the delay, "120 ms", with
// "delay" beside it and a slider under it, as the design draws a value
// tile — the same layout as the Volume tile's.
//
// **The slider is for getting near; the panel is for getting exact.** A
// tile's slider is a few dozen pixels across two seconds, so it moves in
// steps no finer than its pixels; the panel's box takes a hundredth of a
// millisecond.
Item {
    id: face

    /// The block whose face this is. **Not a required property**: a face is
    /// made from a Component in the register, which cannot see the context
    /// it is loaded into, so it is set once the face exists.
    property string blockId: ""

    readonly property var params: SessionModel.revision >= 0 ? SessionModel.params(face.blockId) : ({})
    readonly property real delayMs: face.params.delay ?? 0
    readonly property bool blockEnabled: SessionModel.revision >= 0 && (SessionModel.block(face.blockId).enabled ?? true)
    /// The Advanced colour, or grey for a block that is off.
    readonly property color tint: face.blockEnabled
        ? AppColors.categoryTint(BlockPresentation.category("advanced.sync"))
        : AppColors.muted

    /// The controls themselves, so that a test can drive what a person
    /// drives rather than the function behind it.
    readonly property alias slider: faceSlider
    readonly property alias delayLabel: delayText
    readonly property alias caption: captionText
    readonly property alias delayScale: delayScale

    implicitWidth: Kirigami.Units.gridUnit * 12
    implicitHeight: Kirigami.Units.gridUnit * 6

    SyncScale {
        id: delayScale
    }

    ColumnLayout {
        anchors {
            left: parent.left
            right: parent.right
            verticalCenter: parent.verticalCenter
            // The design's 12 pixels either side, less than the tile's own
            // edge takes, as on the Volume tile.
            leftMargin: Kirigami.Units.smallSpacing * 3
            rightMargin: Kirigami.Units.smallSpacing * 3
        }
        spacing: Kirigami.Units.smallSpacing * 2

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            ElidedLabel {
                id: delayText

                // **The slider's own value while it moves**, so the number
                // follows a drag rather than jumping when it ends.
                text: delayScale.text(faceSlider.pressed ? delayScale.msAt(faceSlider.value) : face.delayMs, Qt.locale())
                font: AppFonts.readout
                lineHeightMode: AppMetrics.followsSystem ? Text.ProportionalHeight : Text.FixedHeight
                lineHeight: AppMetrics.followsSystem ? 1.0 : AppFonts.readoutPixels
                color: face.blockEnabled ? AppColors.text : AppColors.muted
                elide: Text.ElideRight
                Layout.alignment: Qt.AlignBaseline
                Layout.maximumWidth: face.width
            }

            Item {
                Layout.fillWidth: true
            }

            QQC2.Label {
                id: captionText

                text: KI18n.i18nc("@label what the number on a Sync tile is", "delay")
                font: AppFonts.tileCaption
                color: AppColors.muted
                Layout.alignment: Qt.AlignBaseline
                Accessible.ignored: true
            }
        }

        FaceSlider {
            id: faceSlider

            Layout.fillWidth: true
            Layout.topMargin: AppMetrics.followsSystem ? 0 : (AppMetrics.sliderGrooveHeight - implicitHeight) / 2
            Layout.bottomMargin: AppMetrics.followsSystem ? 0 : (AppMetrics.sliderGrooveHeight - implicitHeight) / 2
            tint: face.tint
            from: 0
            to: 1

            Accessible.name: KI18n.i18nc("@label:slider how long a Sync block holds the sound back", "Delay")

            // While it moves the value is live: coalesced and sent sixty
            // times a second. The block fades to each one it hears.
            onMoved: SessionModel.setParam(face.blockId, "delay", delayScale.msAt(value), true)
            onPressedChanged: if (!pressed) {
                SessionModel.commitParam(face.blockId, "delay");
            }

            SessionBound {
                control: faceSlider
                property: "value"
                value: delayScale.positionOf(face.delayMs)
                suspended: faceSlider.pressed
            }
        }
    }
}
