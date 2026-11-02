// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs as QtDialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.ki18n

// The Model block's settings: that it is a beta, what it is doing and why
// it is silent, the folder and what is in it, and what it may take.
//
// **What a folder holds is read by the model host**, through ModelFolderReader,
// and what the block is doing is what the running block publishes.
ColumnLayout {
    id: panel

    /// The block whose settings these are, set once the panel exists.
    property string blockId: ""

    readonly property var params: SessionModel.paramRevision >= 0 ? SessionModel.params(panel.blockId) : ({})
    readonly property bool blockEnabled: SessionModel.revision >= 0 && (SessionModel.block(panel.blockId).enabled ?? true)

    /// The controls, so that a test drives what a person drives.
    readonly property alias reading: reading
    readonly property alias folderReader: folderReader
    readonly property alias betaLine: betaNote
    readonly property alias stateLabel: stateText
    readonly property alias silenceLabel: silenceText
    readonly property alias latencyLabel: latencyText
    readonly property alias loadLabel: loadText
    readonly property alias folderLabel: folderText
    readonly property alias chooseButton: chooseAction
    readonly property alias removeButton: removeAction
    readonly property alias tryAgainButton: tryAgainAction
    readonly property alias descriptionLabel: descriptionText
    readonly property alias refusalLabel: refusalText
    readonly property alias licenceLabel: licenceText
    readonly property alias openLicenceButton: openLicenceAction
    readonly property alias folderDialog: folderDialog

    /// How many times Try Again was pressed: a press is a new number.
    property int presses: 0

    spacing: AppMetrics.panelRowGap

    ModelState {
        id: reading

        blockId: panel.blockId
        blockEnabled: panel.blockEnabled
    }

    ModelFolderReader {
        id: folderReader

        folder: reading.folder
    }

    BetaNote {
        id: betaNote

        typeId: "effect.model"
    }

    // --- what it is doing ---
    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            text: KI18n.i18nc("@label what a Model block is doing", "State")
            color: AppColors.text
            Layout.fillWidth: true
        }

        QQC2.Label {
            id: stateText

            text: reading.stateWord
            color: reading.troubled ? AppColors.warningText : AppColors.accentText
            font.family: AppFonts.monoFamily
        }
    }

    QQC2.Label {
        id: silenceText

        visible: text !== ""
        text: reading.silence
        color: reading.troubled ? AppColors.warningText : AppColors.muted
        font: AppFonts.small
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }

    QQC2.Button {
        id: tryAgainAction

        visible: panel.blockEnabled && reading.state === reading.failed && reading.folder !== ""
        text: KI18n.i18nc("@action:button start a Model block's model again after it failed", "Try Again")
        onClicked: {
            panel.presses += 1;
            folderReader.refresh();
            SessionModel.setParam(panel.blockId, "retry", panel.presses);
        }
    }

    RowLayout {
        Layout.fillWidth: true
        visible: reading.live && reading.latencyMs > 0
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            text: KI18n.i18nc("@label how late a Model block's output is", "Delay")
            color: AppColors.text
            Layout.fillWidth: true
        }

        QQC2.Label {
            id: latencyText

            text: KI18n.i18nc("@info a delay in milliseconds, '63.0 ms'", "%1 ms",
                              Number(reading.latencyMs).toLocaleString(Qt.locale(), 'f', 1))
            color: AppColors.muted
            font.family: AppFonts.monoFamily
        }
    }

    RowLayout {
        Layout.fillWidth: true
        visible: reading.state === reading.running || reading.state === reading.cannotKeepUp
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            text: KI18n.i18nc("@label how much of the available time a Model block's model uses", "Time used")
            color: AppColors.text
            Layout.fillWidth: true
        }

        QQC2.Label {
            id: loadText

            text: KI18n.i18nc("@info a share of the time a model has, '34 %'", "%1 %", Math.round(reading.load))
            color: AppColors.muted
            font.family: AppFonts.monoFamily
        }
    }

    // --- the model ---
    CollapsibleSection {
        title: KI18n.i18nc("@title:group the folder a Model block runs", "Model Folder")
        // What the block is for: open from the start.
        expanded: true

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            QQC2.Label {
                id: folderText

                text: reading.folder !== "" ? reading.folderName
                                            : KI18n.i18nc("@info a Model block with no folder chosen", "No folder chosen")
                color: reading.folder !== "" ? AppColors.text : AppColors.muted
                elide: reading.folder !== "" ? Text.ElideMiddle : Text.ElideNone
                wrapMode: reading.folder !== "" ? Text.NoWrap : Text.WordWrap
                Layout.fillWidth: true
                QQC2.ToolTip.text: reading.folder
                QQC2.ToolTip.visible: folderHover.hovered && reading.folder !== ""
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

                HoverHandler {
                    id: folderHover
                }
            }

            QQC2.Button {
                id: chooseAction

                text: KI18n.i18nc("@action:button choose the folder that holds a model", "Choose Folder…")
                onClicked: folderDialog.open()
            }

            QQC2.Button {
                id: removeAction

                visible: reading.folder !== ""
                icon.name: "edit-delete-remove"
                display: QQC2.AbstractButton.IconOnly
                text: KI18n.i18nc("@action:button stop using the model folder", "Remove Folder")
                QQC2.ToolTip.text: removeAction.text
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                onClicked: reading.remove()
            }
        }

        QQC2.Label {
            id: descriptionText

            visible: text !== ""
            text: {
                if (reading.folder === "" || folderReader.busy || folderReader.sampleRate === 0) {
                    return reading.familyWords(folderReader.family);
                }
                return KI18n.i18nc("@info what a Model block's folder holds: the kind of model, its file, its sample rate and channels, 'DeepFilterNet noise cleanup, model.onnx, 48000 Hz, 1 channel'",
                                   "%1, %2, %3 Hz, %4", reading.familyWords(folderReader.family),
                                   folderReader.modelFile, folderReader.sampleRate,
                                   KI18n.i18ncp("@info how many channels a model takes", "%1 channel", "%1 channels",
                                                folderReader.channels));
            }
            color: AppColors.muted
            font: AppFonts.small
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        QQC2.Label {
            id: refusalText

            visible: reading.folder !== "" && !folderReader.busy && folderReader.refusal !== 0
            text: reading.refusalWords(folderReader.refusal)
            color: AppColors.warningText
            font: AppFonts.small
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true
            visible: reading.folder !== "" && !folderReader.busy
            spacing: Kirigami.Units.smallSpacing

            QQC2.Label {
                id: licenceText

                text: folderReader.licenceFile !== ""
                    ? KI18n.i18nc("@info the file that says on what terms a model may be used", "Licence file: %1",
                                  folderReader.licenceFile)
                    : KI18n.i18nc("@info a model's folder has no licence file",
                                  "No licence file in this folder. Check the model's terms where you got it.")
                color: AppColors.muted
                font: AppFonts.small
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            QQC2.Button {
                id: openLicenceAction

                visible: folderReader.licenceFile !== ""
                text: KI18n.i18nc("@action:button read a model's licence file", "Open Licence")
                onClicked: Qt.openUrlExternally(RecordingStatus.folderUrl(reading.folder + "/" + folderReader.licenceFile))
            }
        }

        InfoLine {
            text: KI18n.i18nc("@info what a Model block's folder has to hold",
                              "A folder with an ONNX model. Reverblabr never downloads a model and includes none.")
        }
    }

    // --- what it may take ---
    CollapsibleSection {
        title: KI18n.i18nc("@title:group how a Model block runs its model", "Running")
        expanded: true

        ParamRow {
            Layout.fillWidth: true
            blockId: panel.blockId
            descriptor: BlockCatalogue.ready ? BlockCatalogue.describeParam("effect.model", "spareTime") : ({})
            currentValue: panel.params.spareTime
            label: KI18n.i18nc("@label:listbox the time a Model block allows a model for each run", "Spare time")
        }

        ParamRow {
            Layout.fillWidth: true
            blockId: panel.blockId
            descriptor: BlockCatalogue.ready ? BlockCatalogue.describeParam("effect.model", "monoMode") : ({})
            currentValue: panel.params.monoMode
            label: KI18n.i18nc("@label:listbox what a mono model does with a stereo lane", "Mono models")
        }

        InfoLine {
            text: KI18n.i18nc("@info what a Model block does when it has nothing to give",
                              "Sound only leaves this block after the model has processed it. In every other case the block is silent, even when it is switched off.")
        }
    }

    QtDialogs.FolderDialog {
        id: folderDialog

        title: KI18n.i18nc("@title:window", "Choose a Model Folder")
        currentFolder: reading.folder !== "" ? RecordingStatus.folderUrl(reading.folder) : ""
        onAccepted: reading.choose(folderDialog.selectedFolder)
    }
}
