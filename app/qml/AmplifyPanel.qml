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

// The Amplify block's settings: one slider, 0 to 1000 %, laid out as
// Volume's is. **A percentage over an amplitude**, as there: what is
// written is the number the sound is multiplied by.
ColumnLayout {
    id: panel

    /// The block whose settings these are, set once the panel exists.
    property string blockId: ""

    readonly property var params: SessionModel.paramRevision >= 0 ? SessionModel.params(panel.blockId) : ({})
    readonly property real gain: panel.params.gain ?? 1.0
    /// The block's top, written down twice as the interface layer does not
    /// depend on the engine; tst_amplify.qml holds it to the catalogue.
    readonly property real topPercent: 1000

    readonly property alias slider: gainSlider
    readonly property alias percentLabel: percentText

    spacing: AppMetrics.panelRowGap

    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            text: KI18n.i18nc("@label:slider how much an Amplify block multiplies the sound by", "Gain")
            color: AppColors.text
            Layout.fillWidth: true
        }

        QQC2.Label {
            id: percentText

            // The slider's own value, so the number is where the handle is
            // during a drag.
            text: KI18n.i18nc("@info a gain as a percentage of the signal that arrived", "%1 %",
                              Math.round(gainSlider.value))
            color: AppColors.accentText
            font.family: AppFonts.monoFamily
            horizontalAlignment: Text.AlignRight
        }
    }

    QQC2.Slider {
        id: gainSlider

        Layout.fillWidth: true
        Layout.topMargin: AppMetrics.pullIntoRow
        from: 0
        to: panel.topPercent
        Accessible.name: KI18n.i18nc("@label:slider how much an Amplify block multiplies the sound by", "Gain")

        onMoved: SessionModel.setParam(panel.blockId, "gain", VolumeScale.amplitudeOf(value), true)
        onPressedChanged: if (!pressed) {
            SessionModel.commitParam(panel.blockId, "gain");
        }

        SessionBound {
            control: gainSlider
            property: "value"
            value: VolumeScale.percentOf(panel.gain)
            suspended: gainSlider.pressed
        }
    }

    InfoLine {
        text: KI18n.i18nc("@info what an Amplify block does",
                          "Multiplies the sound by the gain and does nothing else: nothing stops a loud sound from clipping.")
    }
}
