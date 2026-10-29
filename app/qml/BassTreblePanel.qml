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

// The Bass & Treble settings: a slider for each end, in decibels.
//
// **A panel of its own rather than the generic one**, so each slider says
// its lift or cut with a sign — "+3.0 dB", and "0 dB" for untouched —
// rather than a bare number, and says which part of the sound it moves.
// A double-click on a slider puts that end back to 0 dB.
ColumnLayout {
    id: panel

    /// The block whose settings these are, set once the panel exists.
    property string blockId: ""

    readonly property var params: SessionModel.revision >= 0 ? SessionModel.params(panel.blockId) : ({})
    readonly property real bassDb: panel.params.bass ?? 0
    readonly property real trebleDb: panel.params.treble ?? 0
    readonly property real rangeDb: 12

    /// The controls, so that a test drives what a person drives.
    readonly property alias bassSlider: bassRow.slider
    readonly property alias trebleSlider: trebleRow.slider
    readonly property alias bassReadout: bassRow.readout
    readonly property alias trebleReadout: trebleRow.readout
    readonly property alias words: words

    spacing: Kirigami.Units.largeSpacing

    FilterWords {
        id: words
    }

    component ToneRow: RowLayout {
        id: row

        required property string paramId
        required property string name
        /// What part of the sound it moves, for the tooltip.
        required property string reach
        required property real db

        readonly property alias slider: rowSlider
        readonly property alias readout: readoutText

        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        QQC2.Slider {
            id: rowSlider

            Layout.fillWidth: true
            from: -panel.rangeDb
            to: panel.rangeDb
            stepSize: 0.5
            snapMode: QQC2.Slider.SnapAlways
            Accessible.name: row.name
            Accessible.description: row.reach
            QQC2.ToolTip.text: row.reach
            QQC2.ToolTip.visible: hovered && !pressed
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

            /// When the last press began, and whether this one is the
            /// second of a double-click.
            property double lastPress: 0
            property bool secondPress: false

            onMoved: SessionModel.setParam(panel.blockId, row.paramId, value, true)
            // **Back to untouched on a double-click**, the way a tone
            // control is usually reset: nobody can drag a slider to exactly
            // 0. Told from the presses themselves rather than by a tap
            // handler, which the slider's own grab of the press cancels.
            onPressedChanged: {
                if (pressed) {
                    const now = Date.now();
                    secondPress = now - lastPress < Application.styleHints.mouseDoubleClickInterval;
                    lastPress = now;
                    return;
                }
                SessionModel.commitParam(panel.blockId, row.paramId);
                if (secondPress) {
                    secondPress = false;
                    lastPress = 0;
                    SessionModel.setParam(panel.blockId, row.paramId, 0);
                }
            }

            SessionBound {
                control: rowSlider
                property: "value"
                value: row.db
                suspended: rowSlider.pressed
            }
        }

        QQC2.Label {
            id: readoutText

            text: words.gainText(rowSlider.value)
            font.family: AppFonts.monoFamily
            horizontalAlignment: Text.AlignRight
            Layout.preferredWidth: Kirigami.Units.gridUnit * 4
        }
    }

    Kirigami.FormLayout {
        Layout.fillWidth: true

        ToneRow {
            id: bassRow

            Kirigami.FormData.label: KI18n.i18nc("@label:slider the low end on a Bass & Treble block", "Bass:")
            paramId: "bass"
            name: KI18n.i18nc("@label:slider the low end on a Bass & Treble block", "Bass")
            reach: KI18n.i18nc("@info:tooltip", "Lifts or cuts everything below about 100 Hz")
            db: panel.bassDb
        }

        ToneRow {
            id: trebleRow

            Kirigami.FormData.label: KI18n.i18nc("@label:slider the high end on a Bass & Treble block", "Treble:")
            paramId: "treble"
            name: KI18n.i18nc("@label:slider the high end on a Bass & Treble block", "Treble")
            reach: KI18n.i18nc("@info:tooltip", "Lifts or cuts everything above about 10 kHz")
            db: panel.trebleDb
        }
    }
}
