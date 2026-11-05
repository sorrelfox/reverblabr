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

// What a Wave block shows on the canvas: what it is making, and a
// slider for how loud.
//
// **Laid out like the Volume tile**, so two blocks with a level on
// them read alike: the number first in the fixed-width face, what it is
// beside it, and the level under them on a thin track in the block's
// category colour. The number is the frequency, or "Noise" for noise; beside
// it the waveform, and the side when it is only one.
Item {
    id: face

    /// The block whose face this is, set once the face exists.
    property string blockId: ""

    readonly property var params: SessionModel.paramRevision >= 0 ? SessionModel.params(face.blockId) : ({})
    readonly property string shape: face.params.waveform ?? "sine"
    readonly property real frequency: face.params.frequency ?? 1000
    readonly property real levelDb: face.params.level ?? -18
    readonly property string side: face.params.channels ?? "both"
    readonly property bool blockEnabled: SessionModel.revision >= 0 && (SessionModel.block(face.blockId).enabled ?? true)
    /// The outputs' colour, or grey for a block that is off.
    readonly property color tint: face.blockEnabled
        ? AppColors.categoryTint(BlockPresentation.category("output.wave"))
        : AppColors.muted

    /// The number the tile leads with.
    readonly property string headline: words.isTone(face.shape)
        ? words.frequencyText(face.frequency)
        : KI18n.i18nc("@info a Wave block making noise rather than a tone", "Noise")
    /// What is beside it: the waveform, and the side when there is one.
    readonly property string caption: {
        const what = words.isTone(face.shape) ? words.shapeName(face.shape).toLowerCase()
                                              : words.shapeName(face.shape).split(" ")[0].toLowerCase();
        return face.side === "both" ? what
                                    : KI18n.i18nc("@info a waveform and the one side it is on, 'sine · left'",
                                                  "%1 · %2", what, words.sideName(face.side).toLowerCase());
    }
    /// Everything there is to say, for the tooltip.
    readonly property string detail: words.isTone(face.shape)
        ? KI18n.i18nc("@info:tooltip what a Wave block is making, 'Sine, 1 kHz, −18.0 dB, both'",
                      "%1, %2, %3, %4", words.shapeName(face.shape), words.frequencyText(face.frequency),
                      words.levelText(face.levelDb), words.sideName(face.side).toLowerCase())
        : KI18n.i18nc("@info:tooltip what a Wave block is making, 'White noise, −18.0 dB, both'",
                      "%1, %2, %3", words.shapeName(face.shape), words.levelText(face.levelDb),
                      words.sideName(face.side).toLowerCase())

    readonly property alias headlineLabel: headlineText
    readonly property alias captionLabel: captionText
    readonly property alias slider: levelSlider

    implicitWidth: Kirigami.Units.gridUnit * 12
    implicitHeight: Kirigami.Units.gridUnit * 6

    WaveWords {
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

            ElidedLabel {
                id: headlineText

                text: face.headline
                font: AppFonts.readout
                color: face.blockEnabled ? AppColors.text : AppColors.muted
                elide: Text.ElideRight
                Layout.alignment: Qt.AlignBaseline
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

        // The level, from the floor to full scale, as the panel's slider.
        FaceSlider {
            id: levelSlider

            Layout.fillWidth: true
            tint: face.tint
            from: -80
            to: 0
            Accessible.name: KI18n.i18nc("@label:slider how loud a Wave block's signal is", "Level")

            onMoved: SessionModel.setParam(face.blockId, "level", value, true)
            onPressedChanged: if (!pressed) {
                SessionModel.commitParam(face.blockId, "level");
            }

            SessionBound {
                control: levelSlider
                property: "value"
                value: face.levelDb
                suspended: levelSlider.pressed
            }
        }
    }
}
