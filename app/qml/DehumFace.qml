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

// What a Dehum block shows on the canvas: the base it is taking out,
// and how much, each with a slider — the two things a person reaches for
// while listening to the hum go.
//
// Laid out as the Bass & Treble face is: each setting's name and value on a
// line over a thin track in the advanced blocks' colour. With Auto on
// the base row says so, and while the session runs it shows the base the
// notches are following; its slider still sets where Auto starts.
Item {
    id: face

    /// The block whose face this is, set once the face exists.
    property string blockId: ""

    readonly property var params: SessionModel.paramRevision >= 0 ? SessionModel.params(face.blockId) : ({})
    readonly property real base: face.params.base ?? 50
    readonly property real amount: face.params.amount ?? 5
    readonly property bool automatic: face.params.auto ?? false
    readonly property var values: Meters.frame >= 0 ? (Meters.levels(face.blockId).values ?? []) : []
    /// Auto on and the session running: the base the notches are on.
    readonly property bool tracking: face.automatic && Meters.live && face.values.length > 1
    readonly property bool blockEnabled: SessionModel.revision >= 0 && (SessionModel.block(face.blockId).enabled ?? true)
    readonly property color tint: face.blockEnabled
        ? AppColors.categoryTint(BlockPresentation.category("advanced.dehum"))
        : AppColors.muted

    /// Everything there is to say, for the tooltip.
    readonly property string detail: face.automatic
        ? KI18n.i18nc("@info:tooltip a Dehum block's settings with Auto on, 'Hum followed from 50.0 Hz, 30 dB down'",
                      "Hum followed from %1, %2", face.tracking ? words.trackedText(face.values[0]) : words.baseText(face.base), words.depthText(face.amount))
        : KI18n.i18nc("@info:tooltip a Dehum block's settings, 'Hum at 50.0 Hz, 30 dB down'",
                      "Hum at %1, %2", words.baseText(face.base), words.depthText(face.amount))

    readonly property alias baseSlider: baseRow.slider
    readonly property alias amountSlider: amountRow.slider
    readonly property alias baseLabel: baseRow.valueLabel
    readonly property alias amountLabel: amountRow.valueLabel
    readonly property alias words: words

    implicitWidth: Kirigami.Units.gridUnit * 12
    implicitHeight: Kirigami.Units.gridUnit * 6

    DehumWords {
        id: words
    }

    /// One setting: its name and value on a line, and its slider under them.
    component SettingRow: ColumnLayout {
        id: row

        required property string paramId
        required property string name
        required property real value
        required property real lowest
        required property real highest
        /// What the value is said as.
        required property var say
        /// What to say instead of the slider's value, when there is
        /// something truer to say (what Auto is following); undefined for
        /// the slider's.
        property var shown: undefined

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

                text: row.shown !== undefined ? row.shown : row.say(rowSlider.value)
                font: AppFonts.tileCaption
                color: face.blockEnabled ? AppColors.text : AppColors.muted
                Accessible.ignored: true
            }
        }

        FaceSlider {
            id: rowSlider

            Layout.fillWidth: true
            tint: face.tint
            from: row.lowest
            to: row.highest
            stepSize: 0.1
            snapMode: QQC2.Slider.SnapAlways
            Accessible.name: row.name

            onMoved: SessionModel.setParam(face.blockId, row.paramId, Math.round(value * 10) / 10, true)
            onPressedChanged: if (!pressed) {
                SessionModel.commitParam(face.blockId, row.paramId);
            }

            SessionBound {
                control: rowSlider
                property: "value"
                value: row.value
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

        SettingRow {
            id: baseRow

            paramId: "base"
            name: face.automatic ? KI18n.i18nc("@label:slider a Dehum block's base frequency, followed automatically, short", "Base (auto)")
                                 : KI18n.i18nc("@label:slider a Dehum block's base frequency, short", "Base")
            value: face.base
            lowest: 48
            highest: 62
            say: hz => words.baseText(hz)
            shown: face.tracking ? words.trackedText(face.values[0]) : undefined
        }

        SettingRow {
            id: amountRow

            paramId: "amount"
            name: KI18n.i18nc("@label:slider how much a Dehum block takes out", "Amount")
            value: face.amount
            lowest: 0
            highest: 10
            say: amount => words.amountText(amount)
        }
    }
}
