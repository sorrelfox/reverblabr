// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma ComponentBehavior: Bound

import QtCore
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs as QtDialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.ki18n

// The FIR Filter block's settings, as the design lays them out: the
// pre-gain and the delay the block adds, then the impulse response — the
// file, what the block made of it, and how it is prepared: its loudness
// matched and its silence trimmed.
//
// **The file is read by the engine, not here**: what the panel knows of it
// beyond its name is what the running block publishes.
ColumnLayout {
    id: panel

    /// The block whose settings these are, set once the panel exists.
    property string blockId: ""

    readonly property var params: SessionModel.paramRevision >= 0 ? SessionModel.params(panel.blockId) : ({})

    /// The controls, so that a test drives what a person drives.
    readonly property alias reading: reading
    readonly property alias preGainSlider: preGainControl
    readonly property alias preGainLabel: preGainText
    readonly property alias delayLabel: delayText
    readonly property alias fileLabel: fileText
    readonly property alias descriptionLabel: descriptionText
    readonly property alias importButton: importAction
    readonly property alias removeButton: removeAction
    readonly property alias normaliseBox: normaliseControl
    readonly property alias trimBox: trimControl
    readonly property alias fileDialog: fileDialog

    /// The block's ranges and defaults. The interface layer does not depend
    /// on the engine's, so they are written down twice; tst_firfilter.qml
    /// holds them against the catalogue.
    readonly property real lowestPreGain: -24
    readonly property bool defaultNormalise: true
    readonly property bool defaultTrim: false

    spacing: AppMetrics.panelRowGap

    FirFilterState {
        id: reading

        blockId: panel.blockId
    }

    // --- the pre-gain ---
    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            text: KI18n.i18nc("@label:slider how much a FIR Filter turns its input down first", "Pre-gain")
            color: AppColors.text
            Layout.fillWidth: true
        }

        QQC2.Label {
            id: preGainText

            text: KI18n.i18nc("@info a level in decibels, '-6.0 dB'", "%1 dB",
                              Number(preGainControl.value).toLocaleString(Qt.locale(), 'f', 1))
            color: AppColors.accentText
            font.family: AppFonts.monoFamily
        }
    }

    QQC2.Slider {
        id: preGainControl

        Layout.fillWidth: true
        Layout.topMargin: AppMetrics.pullIntoRow
        from: panel.lowestPreGain
        to: 0
        stepSize: 0.5
        snapMode: QQC2.Slider.SnapAlways
        Accessible.name: KI18n.i18nc("@label:slider", "Pre-gain")
        Accessible.description: preGainText.text

        onMoved: SessionModel.setParam(panel.blockId, "preGainDb", value, true)
        onPressedChanged: if (!pressed) {
            SessionModel.commitParam(panel.blockId, "preGainDb");
        }

        SessionBound {
            control: preGainControl
            property: "value"
            value: panel.params.preGainDb ?? 0
            suspended: preGainControl.pressed
        }
    }

    // --- the delay it adds ---
    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            text: KI18n.i18nc("@label how late a FIR Filter's output is", "Delay")
            color: AppColors.text
            Layout.fillWidth: true
        }

        QQC2.Label {
            id: delayText

            // The same number of samples at every rate; in milliseconds
            // once the running block says at which.
            text: reading.live
                ? KI18n.i18nc("@info a delay in milliseconds and in samples, '5.3 ms (256 samples)'",
                              "%1 ms (%2 samples)", Number(reading.delayMs).toLocaleString(Qt.locale(), 'f', 1),
                              reading.latencySamples)
                : KI18n.i18nc("@info a delay in samples", "%1 samples", reading.latencySamples)
            color: AppColors.muted
            font.family: AppFonts.monoFamily
        }
    }

    // --- the impulse response ---
    CollapsibleSection {
        title: KI18n.i18nc("@title:group the file a FIR Filter applies", "Impulse Response")
        // What the block is for: open from the start.
        expanded: true

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            QQC2.Label {
                id: fileText

                text: reading.file !== "" ? reading.fileName
                                          : KI18n.i18nc("@info a FIR Filter with no file chosen", "No file chosen")
                color: reading.file !== "" && !reading.unusable ? AppColors.text : AppColors.muted
                elide: Text.ElideMiddle
                Layout.fillWidth: true
                QQC2.ToolTip.text: reading.file
                QQC2.ToolTip.visible: fileHover.hovered && reading.file !== ""
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

                HoverHandler {
                    id: fileHover
                }
            }

            QQC2.Button {
                id: importAction

                text: KI18n.i18nc("@action:button choose an impulse response file", "Import File…")
                onClicked: fileDialog.open()
            }

            QQC2.Button {
                id: removeAction

                visible: reading.file !== ""
                icon.name: "edit-delete-remove"
                display: QQC2.AbstractButton.IconOnly
                text: KI18n.i18nc("@action:button stop using the impulse response file", "Remove File")
                QQC2.ToolTip.text: removeAction.text
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                onClicked: reading.remove()
            }
        }

        QQC2.Label {
            id: descriptionText

            visible: text !== ""
            text: reading.description
            color: reading.unusable ? AppColors.warningText : AppColors.muted
            font: AppFonts.small
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        QQC2.CheckBox {
            id: normaliseControl

            text: KI18n.i18nc("@option:check scale the impulse response so noise comes out as loud as it went in",
                              "Match the input's loudness")
            onToggled: SessionModel.setParam(panel.blockId, "normalise", normaliseControl.checked)

            SessionBound {
                control: normaliseControl
                property: "checked"
                value: panel.params.normalise ?? panel.defaultNormalise
            }
        }

        QQC2.CheckBox {
            id: trimControl

            text: KI18n.i18nc("@option:check take the silence off the start and the end of the impulse response",
                              "Trim silence at both ends")
            onToggled: SessionModel.setParam(panel.blockId, "trim", trimControl.checked)

            SessionBound {
                control: trimControl
                property: "checked"
                value: panel.params.trim ?? panel.defaultTrim
            }
        }

        InfoLine {
            text: KI18n.i18nc("@info what a FIR Filter does with a file at another rate",
                              "WAV, FLAC or AIFF, with one, two or four channels. A file at another sample rate than the session's is resampled.")
        }
    }

    QtDialogs.FileDialog {
        id: fileDialog

        title: KI18n.i18nc("@title:window", "Choose an Impulse Response")
        fileMode: QtDialogs.FileDialog.OpenFile
        // Where the last one was, or the music folder.
        currentFolder: reading.file !== ""
            ? RecordingStatus.folderUrl(reading.file.substring(0, reading.file.lastIndexOf("/")))
            : StandardPaths.standardLocations(StandardPaths.MusicLocation)[0]
        nameFilters: [
            KI18n.i18nc("@item:inlistbox a kind of file", "Audio files (*.wav *.flac *.aif *.aiff *.caf *.w64)"),
            KI18n.i18nc("@item:inlistbox a kind of file", "All files (*)")
        ]
        onAccepted: reading.choose(fileDialog.selectedFile)
    }
}
