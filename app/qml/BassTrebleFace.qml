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

// What a Bass & Treble block shows on the canvas: both settings, and
// a slider for each.
//
// **Two small rows rather than one big number.** The block's setting is
// two numbers that mean nothing apart — a warm source is bass up *and*
// treble down — so the face shows the pair: each end's name and its lift
// or cut, over a thin track in the effects colour. The sliders are the
// panel's, from −12 to +12 dB in half decibels.
Item {
    id: face

    /// The block whose face this is, set once the face exists.
    property string blockId: ""

    readonly property var params: SessionModel.revision >= 0 ? SessionModel.params(face.blockId) : ({})
    readonly property real bassDb: face.params.bass ?? 0
    readonly property real trebleDb: face.params.treble ?? 0
    readonly property bool blockEnabled: SessionModel.revision >= 0 && (SessionModel.block(face.blockId).enabled ?? true)
    /// The effects colour, or grey for a block that is off.
    readonly property color tint: face.blockEnabled
        ? AppColors.categoryTint(BlockPresentation.category("effect.basstreble"))
        : AppColors.muted

    readonly property real rangeDb: 12

    /// Everything there is to say, for the tooltip.
    readonly property string detail: KI18n.i18nc("@info:tooltip a Bass & Treble block's settings, 'Bass +3.0 dB, treble -2.0 dB'",
                                                 "Bass %1, treble %2", words.gainText(face.bassDb),
                                                 words.gainText(face.trebleDb))

    readonly property alias bassSlider: bassRow.slider
    readonly property alias trebleSlider: trebleRow.slider
    readonly property alias bassLabel: bassRow.valueLabel
    readonly property alias trebleLabel: trebleRow.valueLabel
    readonly property alias words: words

    implicitWidth: Kirigami.Units.gridUnit * 12
    implicitHeight: Kirigami.Units.gridUnit * 6

    FilterWords {
        id: words
    }

    /// One end: its name and setting on a line, and its slider under them.
    component ToneRow: ColumnLayout {
        id: row

        required property string paramId
        required property string name
        required property real db

        readonly property alias slider: rowSlider
        readonly property alias valueLabel: valueText

        Layout.fillWidth: true
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            QQC2.Label {
                text: row.name
                font: AppFonts.tileCaption
                color: AppColors.muted
                Accessible.ignored: true
            }

            Item {
                Layout.fillWidth: true
            }

            QQC2.Label {
                id: valueText

                // **The slider's own value, not the session's**, as on the
                // Volume tile: the number follows the hand through a drag.
                text: words.gainText(rowSlider.value)
                font: AppFonts.tileCaption
                color: face.blockEnabled ? AppColors.text : AppColors.muted
                Accessible.ignored: true
            }
        }

        FaceSlider {
            id: rowSlider

            Layout.fillWidth: true
            tint: face.tint
            from: -face.rangeDb
            to: face.rangeDb
            stepSize: 0.5
            snapMode: QQC2.Slider.SnapAlways
            Accessible.name: row.name

            onMoved: SessionModel.setParam(face.blockId, row.paramId, value, true)
            onPressedChanged: if (!pressed) {
                SessionModel.commitParam(face.blockId, row.paramId);
            }

            SessionBound {
                control: rowSlider
                property: "value"
                value: row.db
                suspended: rowSlider.pressed
            }
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

        ToneRow {
            id: bassRow

            paramId: "bass"
            name: KI18n.i18nc("@label:slider the low end on a Bass & Treble block", "Bass")
            db: face.bassDb
        }

        ToneRow {
            id: trebleRow

            paramId: "treble"
            name: KI18n.i18nc("@label:slider the high end on a Bass & Treble block", "Treble")
            db: face.trebleDb
        }
    }
}
