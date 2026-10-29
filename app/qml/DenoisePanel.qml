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

// The Denoise block's settings, as the design lays them out: where it
// stands and the button that changes it, how far it turns the noise down,
// how far above the noise a sound has to be to pass, and a line saying how
// to use it.
//
// **Learning needs the session running**: it listens to what is playing.
// While it is stopped the button says so rather than doing nothing.
ColumnLayout {
    id: panel

    /// The block whose settings these are, set once the panel exists.
    property string blockId: ""

    readonly property var params: SessionModel.revision >= 0 ? SessionModel.params(panel.blockId) : ({})

    /// The controls, so that a test drives what a person drives.
    readonly property alias reading: reading
    readonly property alias stateLabel: stateText
    readonly property alias detailLabel: detailText
    readonly property alias actionButton: action
    readonly property alias reductionSlider: reductionControl
    readonly property alias reductionLabel: reductionText
    readonly property alias thresholdSlider: thresholdControl
    readonly property alias thresholdLabel: thresholdText

    /// The block's ranges. The interface layer does not depend on the
    /// engine's, so they are written down twice; tst_denoise.qml holds them
    /// against the catalogue.
    readonly property real highestReduction: 30
    readonly property real defaultReduction: 20
    readonly property real highestThreshold: 20
    readonly property real defaultThreshold: 3

    spacing: AppMetrics.panelRowGap

    DenoiseState {
        id: reading

        blockId: panel.blockId
    }

    function decibels(db: real): string {
        return KI18n.i18nc("@info a level in decibels, '20.0 dB'", "%1 dB", Number(db).toLocaleString(Qt.locale(), 'f', 1));
    }

    // --- where it stands, and the button ---
    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing * 2

        Rectangle {
            implicitWidth: AppMetrics.stateDotSize
            implicitHeight: AppMetrics.stateDotSize
            radius: width / 2
            color: reading.isLearning ? AppColors.liveMark : (reading.isActive ? AppColors.okMark : AppColors.muted)
            Layout.alignment: Qt.AlignVCenter
            Accessible.ignored: true
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0

            QQC2.Label {
                id: stateText

                text: reading.headline
                color: AppColors.text
                font: AppFonts.strong
            }

            QQC2.Label {
                id: detailText

                text: reading.detail
                color: AppColors.muted
                font: AppFonts.small
            }
        }

        QQC2.Button {
            id: action

            text: reading.isActive ? KI18n.i18nc("@action:button drop the learned noise", "Forget Noise")
                                   : KI18n.i18nc("@action:button start listening to the noise", "Learn Noise")
            // Forgetting needs nothing running; learning needs something to
            // listen to.
            enabled: reading.isActive || (Meters.live && !reading.isLearning)
            QQC2.ToolTip.text: reading.isActive || Meters.live
                ? ""
                : KI18n.i18nc("@info:tooltip", "Run the session, with only the noise playing, to learn it")
            QQC2.ToolTip.visible: hovered && QQC2.ToolTip.text !== ""
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            onClicked: reading.isActive ? reading.forget() : reading.learn()
        }
    }

    // --- how far it turns the noise down ---
    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: AppMetrics.panelRowGap
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            text: KI18n.i18nc("@label:slider how far a Denoise block turns the noise down", "Noise reduction")
            color: AppColors.text
            Layout.fillWidth: true
        }

        QQC2.Label {
            id: reductionText

            text: panel.decibels(reductionControl.value)
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
        stepSize: 0.5
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

    // --- how far above the noise a sound passes ---
    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: AppMetrics.panelRowGap
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            text: KI18n.i18nc("@label:slider how far above the learned noise a sound has to be to pass untouched",
                              "Keep sound above noise by")
            color: AppColors.text
            // A translation goes onto a second line rather than under what is
            // beside it.
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
            Layout.fillWidth: true
        }

        QQC2.Label {
            id: thresholdText

            text: panel.decibels(thresholdControl.value)
            color: AppColors.accentText
            font.family: AppFonts.monoFamily
        }
    }

    QQC2.Slider {
        id: thresholdControl

        Layout.fillWidth: true
        Layout.topMargin: AppMetrics.pullIntoRow
        from: 0
        to: panel.highestThreshold
        stepSize: 0.5
        snapMode: QQC2.Slider.SnapAlways
        Accessible.name: KI18n.i18nc("@label:slider", "Keep sound above noise by")
        Accessible.description: thresholdText.text

        onMoved: SessionModel.setParam(panel.blockId, "threshold", value, true)
        onPressedChanged: if (!pressed) {
            SessionModel.commitParam(panel.blockId, "threshold");
        }

        SessionBound {
            control: thresholdControl
            property: "value"
            value: panel.params.threshold ?? panel.defaultThreshold
            suspended: thresholdControl.pressed
        }
    }

    InfoLine {
        text: KI18n.i18nc("@info how to use the Denoise block",
                          "Play the noise on its own and press Learn Noise: three seconds of it are listened to. Then everything that follows has that noise taken out.")
    }
}
