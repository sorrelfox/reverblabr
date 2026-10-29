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

// The Speech Denoise block's settings, as the design lays them out:
// whether it hears speech, and mono processing; with the strength the block
// has besides — how far it turns anything down — and a line on the model.
ColumnLayout {
    id: panel

    /// The block whose settings these are, set once the panel exists.
    property string blockId: ""

    readonly property var params: SessionModel.revision >= 0 ? SessionModel.params(panel.blockId) : ({})

    /// The controls, so that a test drives what a person drives.
    readonly property alias reading: reading
    readonly property alias statusLabel: statusText
    readonly property alias reductionSlider: reductionControl
    readonly property alias reductionLabel: reductionText
    readonly property alias monoBox: monoControl

    /// The block's range and defaults. The interface layer does not depend
    /// on the engine's, so they are written down twice;
    /// tst_speechdenoise.qml holds them against the catalogue.
    readonly property real highestReduction: 60
    readonly property real defaultReduction: 40
    readonly property bool defaultMono: false

    spacing: AppMetrics.panelRowGap

    SpeechDenoiseState {
        id: reading

        blockId: panel.blockId
    }

    // --- whether it hears speech ---
    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing * 2

        Rectangle {
            implicitWidth: AppMetrics.stateDotSize
            implicitHeight: AppMetrics.stateDotSize
            radius: width / 2
            color: reading.atWrongRate ? AppColors.warningMark : (reading.hearsSpeech ? AppColors.okMark : AppColors.muted)
            Layout.alignment: Qt.AlignVCenter
            Accessible.ignored: true
        }

        QQC2.Label {
            id: statusText

            text: reading.status
            color: reading.atWrongRate ? AppColors.warningText : (reading.hearsSpeech ? AppColors.text : AppColors.muted)
            Layout.fillWidth: true
        }
    }

    // --- how far it turns anything down ---
    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: AppMetrics.panelRowGap
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            text: KI18n.i18nc("@label:slider the most a Speech Denoise block turns anything down", "Noise reduction")
            color: AppColors.text
            Layout.fillWidth: true
        }

        QQC2.Label {
            id: reductionText

            text: KI18n.i18nc("@info a level in decibels, '40.0 dB'", "%1 dB",
                              Number(reductionControl.value).toLocaleString(Qt.locale(), 'f', 1))
            color: AppColors.accentText
            font.family: AppFonts.monoFamily
        }
    }

    QQC2.Slider {
        id: reductionControl

        Layout.fillWidth: true
        Layout.topMargin: AppMetrics.pullIntoRow
        from: 0
        to: panel.highestReduction
        stepSize: 1
        snapMode: QQC2.Slider.SnapAlways
        Accessible.name: KI18n.i18nc("@label:slider", "Noise reduction")
        Accessible.description: reductionText.text

        onMoved: SessionModel.setParam(panel.blockId, "reduction", value, true)
        onPressedChanged: if (!pressed) {
            SessionModel.commitParam(panel.blockId, "reduction");
        }

        SessionBound {
            control: reductionControl
            property: "value"
            value: panel.params.reduction ?? panel.defaultReduction
            suspended: reductionControl.pressed
        }
    }

    // --- mono ---
    QQC2.CheckBox {
        id: monoControl

        Layout.topMargin: AppMetrics.panelRowGap
        text: KI18n.i18nc("@option:check a Speech Denoise block folds stereo to one before the model",
                          "Mono signal processing")
        onToggled: SessionModel.setParam(panel.blockId, "mono", monoControl.checked)

        SessionBound {
            control: monoControl
            property: "checked"
            value: panel.params.mono ?? panel.defaultMono
        }
    }

    InfoLine {
        text: KI18n.i18nc("@info what mono does on a Speech Denoise block",
                          "Mono processing uses half the CPU by folding stereo input to one before the model.")
    }

    InfoLine {
        text: KI18n.i18nc("@info the model a Speech Denoise block runs, and what it costs",
                          "RNNoise, a model trained on voices, needs nothing learned first. It works at 48 kHz and makes the sound 30 ms late.")
    }
}
