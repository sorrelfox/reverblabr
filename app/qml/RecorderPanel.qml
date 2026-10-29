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

// The Recorder's settings, as the design draws them: what it is doing
// and for how long, Pause and Split file, and three sections — General (the
// file's name, where it goes and how good it is), Tags and Limits.
//
// **Every control is one of the block's own parameters**, written through
// SessionModel and put back by a SessionBound, so an undo, a preset or a
// second window moves it like anything else, and it is saved with the
// session. The one thing here that is not a parameter is the quality list,
// which sets several of them at once — as one step to undo — and reads back
// as "Custom" when they are set by hand.
ColumnLayout {
    id: panel

    /// The block whose settings these are, set once the panel exists.
    property string blockId: ""
    /// Which section is showing: "general", "tags" or "limits".
    property string section: "general"

    readonly property var params: SessionModel.revision >= 0 ? SessionModel.params(panel.blockId) : ({})
    readonly property string format: panel.params.format ?? "mp3"
    readonly property bool lossy: words.isLossy(panel.format)

    /// What the engine offers, and what it cannot here.
    property var formatParam: BlockCatalogue.ready ? BlockCatalogue.describeParam("output.recorder", "format") : ({})
    readonly property var formats: panel.formatParam.choices ?? words.formats
    readonly property var unavailable: panel.formatParam.unavailableChoices ?? []
    readonly property var qualities: words.qualities.filter(quality => panel.formats.indexOf(quality.params.format) >= 0)
    readonly property string quality: words.qualityOf(panel.params)

    readonly property string recording: RecordingStatus.revision >= 0
        ? RecordingStatus.state(SessionModel.sessionId, panel.blockId) : "idle"
    readonly property bool going: panel.recording === "recording" || panel.recording === "paused"
    readonly property bool isPaused: panel.recording === "paused"
    readonly property string sampleRate: panel.params.sampleRate ?? words.sampleRates[0]
    readonly property string channels: panel.params.channels ?? words.channelChoices[0]
    property int elapsed: 0

    /// The controls, so that a test drives what a person drives.
    readonly property alias statusLabel: statusText
    readonly property alias timeLabel: timeText
    readonly property alias pauseButton: pauseControl
    readonly property alias splitButton: splitControl
    readonly property alias sections: sectionFrame
    readonly property alias fileName: fileNameEditor
    readonly property alias folderField: folderControl
    readonly property alias showFolderButton: showFolder
    readonly property alias qualityBox: qualityControl
    readonly property alias qualityLine: qualitySummary
    readonly property alias formatOptions: formatSection
    readonly property alias formatBox: formatControl
    readonly property alias bitrateBox: bitrateControl
    readonly property alias variableButton: variableChoice
    readonly property alias constantButton: constantChoice
    readonly property alias depthBox: depthControl
    readonly property alias rateBox: rateControl
    readonly property alias stereoButton: stereoChoice
    readonly property alias monoButton: monoChoice
    readonly property alias unavailableLine: unavailableHint
    readonly property alias tagFields: tagFields
    readonly property alias splitCheck: splitEnabled
    readonly property alias splitEveryBox: splitEvery
    readonly property alias splitUnitBox: splitUnit
    readonly property alias endCheck: endEnabled
    readonly property alias endAfterBox: endAfter
    readonly property alias endUnitBox: endUnit
    readonly property alias silenceCheck: silenceEnabled
    readonly property alias silenceActionBox: silenceAction
    readonly property alias silenceSecondsBox: silenceSeconds
    readonly property alias thresholdSlider: threshold
    readonly property alias words: words

    spacing: AppMetrics.panelRowGap

    RecorderWords {
        id: words
    }

    function refresh() {
        panel.elapsed = RecordingStatus.elapsedSeconds(SessionModel.sessionId, panel.blockId);
    }

    function set(paramId: string, value: var) {
        SessionModel.setParam(panel.blockId, paramId, value);
    }

    /// Everything a quality sets, as one step to undo.
    function applyQuality(id: string) {
        const quality = words.qualities.find(entry => entry.id === id);
        if (quality === undefined) {
            return;
        }
        const changes = [];
        for (const key in quality.params) {
            changes.push([key, quality.params[key]]);
        }
        SessionModel.setParams(panel.blockId, changes);
    }

    onRecordingChanged: panel.refresh()
    Component.onCompleted: panel.refresh()

    Timer {
        interval: 1000
        repeat: true
        running: panel.recording === "recording"
        onTriggered: panel.refresh()
    }

    component FieldLabel: QQC2.Label {
        color: AppColors.muted
        horizontalAlignment: Text.AlignRight
        elide: Text.ElideRight
        // A label column is fixed; a longer word goes onto a second line
        // rather than out of sight.
        wrapMode: Text.Wrap
        maximumLineCount: 2
        Layout.preferredWidth: AppMetrics.panelLabelWidth
    }

    // --- what it is doing ---
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: statusRow.implicitHeight + AppMetrics.mediumSpacing * 2
        radius: AppMetrics.smallRadius
        color: AppColors.field
        border.color: AppColors.border

        RowLayout {
            id: statusRow

            anchors {
                fill: parent
                leftMargin: AppMetrics.largeSpacing
                rightMargin: AppMetrics.largeSpacing
            }
            spacing: AppMetrics.largeSpacing

            Rectangle {
                implicitWidth: 9
                implicitHeight: 9
                radius: height / 2
                color: panel.recording === "recording" ? AppColors.dangerMark : AppColors.muted
            }

            QQC2.Label {
                id: statusText

                text: words.statusText(panel.recording, panel.format,
                                       RecordingStatus.stopReason(SessionModel.sessionId, panel.blockId))
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            QQC2.Label {
                id: timeText

                visible: panel.going
                text: RecordingStatus.clock(panel.elapsed)
                font: AppFonts.mono
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: AppMetrics.buttonSpacing

        QQC2.Button {
            id: pauseControl

            // Half the row each, whatever the words are.
            Layout.preferredWidth: 1
            Layout.minimumWidth: 0

            text: panel.isPaused ? KI18n.i18nc("@action:button resume a paused recording", "Resume")
                                           : KI18n.i18nc("@action:button pause a recording", "Pause")
            icon.name: panel.isPaused ? "media-playback-start" : "media-playback-pause"
            enabled: panel.going
            Layout.fillWidth: true
            onClicked: panel.isPaused ? RecordingStatus.resume(SessionModel.sessionId, panel.blockId)
                                                : RecordingStatus.pause(SessionModel.sessionId, panel.blockId)
        }

        QQC2.Button {
            id: splitControl

            // Half the row each, whatever the words are.
            Layout.preferredWidth: 1
            Layout.minimumWidth: 0

            text: KI18n.i18nc("@action:button end this recording's file and start the next", "Split file")
            icon.name: "edit-cut"
            enabled: panel.recording === "recording"
            Layout.fillWidth: true
            onClicked: RecordingStatus.split(SessionModel.sessionId, panel.blockId)
        }
    }

    SegmentedFrame {
        id: sectionFrame

        Layout.alignment: Qt.AlignHCenter

        QQC2.ButtonGroup {
            id: sectionGroup
        }

        Repeater {
            model: [
                { id: "general", name: KI18n.i18nc("@title:tab a Recorder's settings", "General") },
                { id: "tags", name: KI18n.i18nc("@title:tab a Recorder's settings", "Tags") },
                { id: "limits", name: KI18n.i18nc("@title:tab a Recorder's settings", "Limits") }
            ]

            delegate: SegmentedChoice {
                id: tab

                required property var modelData

                text: tab.modelData.name
                checked: panel.section === tab.modelData.id
                QQC2.ButtonGroup.group: sectionGroup
                onClicked: panel.section = tab.modelData.id
            }
        }
    }

    // --- General ---
    ColumnLayout {
        visible: panel.section === "general"
        Layout.fillWidth: true
        spacing: AppMetrics.panelRowGap

        RowLayout {
            Layout.fillWidth: true
            spacing: AppMetrics.labelSpacing

            FieldLabel {
                text: KI18n.i18nc("@label a recording's file name", "File name")
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: AppMetrics.smallSpacing
            }

            RecorderFileName {
                id: fileNameEditor

                blockId: panel.blockId
                format: panel.format
                Layout.fillWidth: true
                // The preview is as long as a file name is; it elides
                // rather than widening the card.
                Layout.preferredWidth: 1
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: AppMetrics.labelSpacing

            FieldLabel {
                text: KI18n.i18nc("@label the folder recordings go to", "Save to")
            }

            QQC2.TextField {
                id: folderControl

                // **Takes what the row has left, and asks for nothing**: a field
                // that asked for its own width made the card wider than the card.
                Layout.preferredWidth: 1

                Layout.fillWidth: true
                // A hint longer than the field is whole on hover.
                QQC2.ToolTip.text: placeholderText
                QQC2.ToolTip.visible: hovered && text === "" && placeholderText !== ""
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                placeholderText: RecordingStatus.defaultFolder()
                Accessible.name: KI18n.i18nc("@label:textbox", "Save to")
                onEditingFinished: {
                    if (folderControl.text !== (panel.params.folder ?? "")) {
                        panel.set("folder", folderControl.text);
                    }
                }

                SessionBound {
                    control: folderControl
                    property: "text"
                    value: panel.params.folder ?? ""
                    suspended: folderControl.activeFocus
                }
            }

            QQC2.ToolButton {
                icon.name: "document-open-folder"
                display: QQC2.AbstractButton.IconOnly
                text: KI18n.i18nc("@action:button choose the folder recordings go to", "Choose Folder…")
                Accessible.name: text
                QQC2.ToolTip.text: text
                QQC2.ToolTip.visible: hovered
                onClicked: folderDialog.open()
            }

            QQC2.ToolButton {
                id: showFolder

                icon.name: "system-file-manager"
                display: QQC2.AbstractButton.IconOnly
                text: KI18n.i18nc("@action:button open the folder recordings go to in the file manager",
                                  "Show in File Manager")
                Accessible.name: text
                QQC2.ToolTip.text: text
                QQC2.ToolTip.visible: hovered
                onClicked: Qt.openUrlExternally(RecordingStatus.folderUrl(panel.params.folder ?? ""))
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: AppMetrics.labelSpacing

            FieldLabel {
                text: KI18n.i18nc("@label:listbox how good a recording is, and how big", "Quality")
            }

            QQC2.ComboBox {
                id: qualityControl

                // **Takes what the row has left, and asks for nothing**: a field
                // that asked for its own width made the card wider than the card.
                Layout.preferredWidth: 1

                Layout.fillWidth: true
                model: panel.qualities.map(quality => quality.id)
                displayText: words.qualityName(panel.quality)
                Accessible.name: KI18n.i18nc("@label:listbox", "Quality")

                delegate: QQC2.ItemDelegate {
                    required property string modelData
                    required property int index

                    width: qualityControl.width
                    text: words.qualityName(modelData)
                    highlighted: qualityControl.highlightedIndex === index
                }

                onActivated: index => panel.applyQuality(qualityControl.model[index])

                SessionBound {
                    control: qualityControl
                    property: "currentIndex"
                    value: qualityControl.model.indexOf(panel.quality)
                }
            }
        }

        InfoLine {
            id: qualitySummary

            text: words.qualitySummary(panel.quality)
        }

        CollapsibleSection {
            id: formatSection

            title: KI18n.i18nc("@title:group a recording's format and its settings", "Format options")

            RowLayout {
                Layout.fillWidth: true
                spacing: AppMetrics.labelSpacing

                FieldLabel {
                    text: KI18n.i18nc("@label:listbox a recording's file format", "Format")
                }

                QQC2.ComboBox {
                    id: formatControl

                    // **Takes what the row has left, and asks for nothing**: a field
                    // that asked for its own width made the card wider than the card.
                    Layout.preferredWidth: 1

                    Layout.fillWidth: true
                    model: panel.formats
                    displayText: words.formatName(panel.format)
                    Accessible.name: KI18n.i18nc("@label:listbox", "Format")

                    delegate: QQC2.ItemDelegate {
                        required property string modelData
                        required property int index

                        width: formatControl.width
                        text: words.formatName(modelData)
                        highlighted: formatControl.highlightedIndex === index
                    }

                    onActivated: index => panel.set("format", String(panel.formats[index]))

                    SessionBound {
                        control: formatControl
                        property: "currentIndex"
                        value: panel.formats.indexOf(panel.format)
                    }
                }
            }

            RowLayout {
                visible: panel.lossy
                Layout.fillWidth: true
                spacing: AppMetrics.labelSpacing

                FieldLabel {
                    text: KI18n.i18nc("@label:listbox how many bits a second a recording keeps", "Bit rate")
                }

                QQC2.ComboBox {
                    id: bitrateControl

                    // **Takes what the row has left, and asks for nothing**: a field
                    // that asked for its own width made the card wider than the card.
                    Layout.preferredWidth: 1

                    Layout.fillWidth: true
                    model: words.bitrates
                    displayText: KI18n.i18nc("@item a recording's bit rate", "%1 kbps", panel.params.bitrate ?? 256)
                    Accessible.name: KI18n.i18nc("@label:listbox", "Bit rate")

                    delegate: QQC2.ItemDelegate {
                        required property int modelData
                        required property int index

                        width: bitrateControl.width
                        text: KI18n.i18nc("@item a recording's bit rate", "%1 kbps", modelData)
                        highlighted: bitrateControl.highlightedIndex === index
                    }

                    onActivated: index => panel.set("bitrate", words.bitrates[index])

                    SessionBound {
                        control: bitrateControl
                        property: "currentIndex"
                        value: words.bitrates.indexOf(Number(panel.params.bitrate ?? 256))
                    }
                }

                // AAC here is always the same rate throughout.
                SegmentedFrame {
                    visible: panel.format !== "aac"

                    QQC2.ButtonGroup {
                        id: modeGroup
                    }

                    SegmentedChoice {
                        id: variableChoice

                        text: KI18n.i18nc("@action:button a bit rate that varies with the sound", "VBR")
                        checked: (panel.params.bitrateMode ?? "vbr") === "vbr"
                        QQC2.ButtonGroup.group: modeGroup
                        Accessible.name: KI18n.i18nc("@action:button", "Variable bit rate")
                        onClicked: panel.set("bitrateMode", "vbr")
                    }

                    SegmentedChoice {
                        id: constantChoice

                        text: KI18n.i18nc("@action:button the same bit rate all the way through", "CBR")
                        checked: (panel.params.bitrateMode ?? "vbr") === "cbr"
                        QQC2.ButtonGroup.group: modeGroup
                        Accessible.name: KI18n.i18nc("@action:button", "Constant bit rate")
                        onClicked: panel.set("bitrateMode", "cbr")
                    }
                }
            }

            RowLayout {
                visible: !panel.lossy
                Layout.fillWidth: true
                spacing: AppMetrics.labelSpacing

                FieldLabel {
                    text: KI18n.i18nc("@label:listbox how many bits each sample of a recording keeps", "Bit depth")
                }

                QQC2.ComboBox {
                    id: depthControl

                    // **Takes what the row has left, and asks for nothing**: a field
                    // that asked for its own width made the card wider than the card.
                    Layout.preferredWidth: 1

                    readonly property string chosen: panel.params.sampleFormat ?? "int24"
                    // FLAC and ALAC have no floating point.
                    readonly property var offered: panel.format === "flac" || panel.format === "alac"
                        ? ["int16", "int24"] : words.sampleFormats

                    Layout.fillWidth: true
                    model: depthControl.offered
                    displayText: words.sampleFormatName(words.depthOf(panel.format, depthControl.chosen))
                    Accessible.name: KI18n.i18nc("@label:listbox", "Bit depth")

                    delegate: QQC2.ItemDelegate {
                        required property string modelData
                        required property int index

                        width: depthControl.width
                        text: words.sampleFormatName(modelData)
                        highlighted: depthControl.highlightedIndex === index
                    }

                    onActivated: index => panel.set("sampleFormat", String(depthControl.offered[index]))

                    SessionBound {
                        control: depthControl
                        property: "currentIndex"
                        value: depthControl.offered.indexOf(words.depthOf(panel.format, depthControl.chosen))
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: AppMetrics.labelSpacing

                FieldLabel {
                    text: KI18n.i18nc("@label:listbox a recording's sample rate", "Sample rate")
                }

                QQC2.ComboBox {
                    id: rateControl

                    // **Takes what the row has left, and asks for nothing**: a field
                    // that asked for its own width made the card wider than the card.
                    Layout.preferredWidth: 1

                    readonly property int sessionHz: Number(SessionModel.sampleRateSetting) || 0

                    Layout.fillWidth: true
                    model: words.sampleRates
                    displayText: words.sampleRateName(panel.sampleRate, rateControl.sessionHz)
                    Accessible.name: KI18n.i18nc("@label:listbox", "Sample rate")

                    delegate: QQC2.ItemDelegate {
                        required property string modelData
                        required property int index

                        width: rateControl.width
                        text: words.sampleRateName(modelData, rateControl.sessionHz)
                        highlighted: rateControl.highlightedIndex === index
                    }

                    onActivated: index => panel.set("sampleRate", String(words.sampleRates[index]))

                    SessionBound {
                        control: rateControl
                        property: "currentIndex"
                        value: words.sampleRates.indexOf(panel.sampleRate)
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: AppMetrics.labelSpacing

                FieldLabel {
                    text: KI18n.i18nc("@label how many channels a recording has", "Channels")
                }

                SegmentedFrame {
                    QQC2.ButtonGroup {
                        id: channelGroup
                    }

                    SegmentedChoice {
                        id: stereoChoice

                        text: words.channelsName(words.channelChoices[0])
                        checked: panel.channels === words.channelChoices[0]
                        QQC2.ButtonGroup.group: channelGroup
                        onClicked: panel.set("channels", words.channelChoices[0])
                    }

                    SegmentedChoice {
                        id: monoChoice

                        text: words.channelsName(words.channelChoices[1])
                        checked: panel.channels === words.channelChoices[1]
                        QQC2.ButtonGroup.group: channelGroup
                        onClicked: panel.set("channels", words.channelChoices[1])
                    }
                }

                Item {
                    Layout.fillWidth: true
                }
            }

            // **What is not offered, and how to get it**.
            InfoLine {
                id: unavailableHint

                visible: panel.unavailable.length > 0
                text: panel.unavailable.length === 0 ? ""
                    : KI18n.i18ncp("@info %2 are formats, 'AAC and ALAC'; %3 is the reason",
                                   "%2 isn't offered: %3.", "%2 aren't offered: %3.", panel.unavailable.length,
                                   panel.unavailable.map(entry => words.formatName(entry.id)).join(
                                       KI18n.i18nc("@info the word between two formats", " and ")),
                                   RecordingStatus.reasonText(panel.unavailable[0].reason))
            }
        }
    }

    // --- Tags ---
    GridLayout {
        id: tagFields

        visible: panel.section === "tags"
        Layout.fillWidth: true
        columns: 2
        columnSpacing: AppMetrics.labelSpacing
        rowSpacing: AppMetrics.smallSpacing

        Repeater {
            model: [
                { id: "tagTitle", name: KI18n.i18nc("@label a recording's tag", "Title") },
                { id: "tagArtist", name: KI18n.i18nc("@label a recording's tag", "Artist") },
                { id: "tagAlbum", name: KI18n.i18nc("@label a recording's tag", "Album") },
                { id: "tagGenre", name: KI18n.i18nc("@label a recording's tag", "Genre") },
                { id: "tagYear", name: KI18n.i18nc("@label a recording's tag", "Year") },
                { id: "tagTrack", name: KI18n.i18nc("@label a recording's tag", "Track") },
                { id: "tagTrackCount", name: KI18n.i18nc("@label a recording's tag, how many tracks there are", "Of tracks") },
                { id: "tagDisc", name: KI18n.i18nc("@label a recording's tag", "Disc") },
                { id: "tagDiscCount", name: KI18n.i18nc("@label a recording's tag, how many discs there are", "Of discs") },
                { id: "tagComment", name: KI18n.i18nc("@label a recording's tag", "Comment") }
            ]

            delegate: RowLayout {
                id: tagRow

                required property var modelData

                Layout.columnSpan: 2
                Layout.fillWidth: true
                spacing: AppMetrics.labelSpacing

                FieldLabel {
                    text: tagRow.modelData.name
                }

                QQC2.TextField {
                    id: tagField

                    // **Takes what the row has left, and asks for nothing**: a field
                    // that asked for its own width made the card wider than the card.
                    Layout.preferredWidth: 1

                    readonly property string paramId: tagRow.modelData.id

                    Layout.fillWidth: true
                    Accessible.name: tagRow.modelData.name
                    onEditingFinished: {
                        if (tagField.text !== (panel.params[tagField.paramId] ?? "")) {
                            panel.set(tagField.paramId, tagField.text);
                        }
                    }

                    SessionBound {
                        control: tagField
                        property: "text"
                        value: panel.params[tagField.paramId] ?? ""
                        suspended: tagField.activeFocus
                    }
                }
            }
        }

        InfoLine {
            Layout.columnSpan: 2
            text: KI18n.i18nc("@info", "The file-name tokens work here too: %session, %date, %year, %counter.")
        }
    }

    // --- Limits ---
    ColumnLayout {
        visible: panel.section === "limits"
        Layout.fillWidth: true
        spacing: AppMetrics.panelRowGap

        RowLayout {
            Layout.fillWidth: true
            spacing: AppMetrics.smallSpacing

            QQC2.CheckBox {
                id: splitEnabled

                text: KI18n.i18nc("@option:check followed by a number and a unit", "Start a new file every")
                onToggled: panel.set("splitEnabled", splitEnabled.checked)

                SessionBound {
                    control: splitEnabled
                    property: "checked"
                    value: panel.params.splitEnabled ?? false
                }
            }

            QQC2.SpinBox {
                id: splitEvery

                from: 1
                to: 100000
                editable: true
                Accessible.name: KI18n.i18nc("@label:spinbox", "Start a new file every")
                onValueModified: panel.set("splitEvery", splitEvery.value)

                SessionBound {
                    control: splitEvery
                    property: "value"
                    value: panel.params.splitEvery ?? 30
                }
            }

            QQC2.ComboBox {
                id: splitUnit

                readonly property var units: ["minutes", "hours", "megabytes"]

                model: [KI18n.i18nc("@item a unit", "minutes"), KI18n.i18nc("@item a unit", "hours"),
                    KI18n.i18nc("@item a unit", "MB")]
                Accessible.name: KI18n.i18nc("@label:listbox", "Unit")
                onActivated: index => panel.set("splitUnit", splitUnit.units[index])

                SessionBound {
                    control: splitUnit
                    property: "currentIndex"
                    value: splitUnit.units.indexOf(panel.params.splitUnit ?? "minutes")
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: AppMetrics.smallSpacing

            QQC2.CheckBox {
                id: endEnabled

                text: KI18n.i18nc("@option:check followed by a number and a unit", "End the recording after")
                onToggled: panel.set("endEnabled", endEnabled.checked)

                SessionBound {
                    control: endEnabled
                    property: "checked"
                    value: panel.params.endEnabled ?? false
                }
            }

            QQC2.SpinBox {
                id: endAfter

                from: 1
                to: 100000
                editable: true
                Accessible.name: KI18n.i18nc("@label:spinbox", "End the recording after")
                onValueModified: panel.set("endAfter", endAfter.value)

                SessionBound {
                    control: endAfter
                    property: "value"
                    value: panel.params.endAfter ?? 1
                }
            }

            QQC2.ComboBox {
                id: endUnit

                readonly property var units: ["minutes", "hours"]

                model: [KI18n.i18nc("@item a unit", "minutes"), KI18n.i18nc("@item a unit", "hours")]
                Accessible.name: KI18n.i18nc("@label:listbox", "Unit")
                onActivated: index => panel.set("endUnit", endUnit.units[index])

                SessionBound {
                    control: endUnit
                    property: "currentIndex"
                    value: endUnit.units.indexOf(panel.params.endUnit ?? "hours")
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: AppMetrics.smallSpacing

            QQC2.CheckBox {
                id: silenceEnabled

                Accessible.name: KI18n.i18nc("@option:check", "Act on silence")
                onToggled: panel.set("silenceEnabled", silenceEnabled.checked)

                SessionBound {
                    control: silenceEnabled
                    property: "checked"
                    value: panel.params.silenceEnabled ?? false
                }
            }

            QQC2.ComboBox {
                id: silenceAction

                readonly property var actions: ["remove", "split", "stop"]

                model: [KI18n.i18nc("@item what silence does to a recording", "Remove silence"),
                    KI18n.i18nc("@item what silence does to a recording", "Split on silence"),
                    KI18n.i18nc("@item what silence does to a recording", "Stop on silence")]
                Accessible.name: KI18n.i18nc("@label:listbox", "What silence does")
                onActivated: index => panel.set("silenceAction", silenceAction.actions[index])

                SessionBound {
                    control: silenceAction
                    property: "currentIndex"
                    value: silenceAction.actions.indexOf(panel.params.silenceAction ?? "remove")
                }
            }

            QQC2.Label {
                text: KI18n.i18nc("@label followed by a number of seconds", "after")
            }

            QQC2.SpinBox {
                id: silenceSeconds

                // Tenths of a second, as the spin box counts whole numbers.
                from: 1
                to: 36000
                editable: true
                textFromValue: (value, locale) => Number(value / 10).toLocaleString(locale, "f", 1)
                valueFromText: (text, locale) => Math.round(Number.fromLocaleString(locale, text) * 10)
                Accessible.name: KI18n.i18nc("@label:spinbox", "Seconds of silence")
                onValueModified: panel.set("silenceSeconds", silenceSeconds.value / 10)

                SessionBound {
                    control: silenceSeconds
                    property: "value"
                    value: Math.round(Number(panel.params.silenceSeconds ?? 2) * 10)
                }
            }

            QQC2.Label {
                text: KI18n.i18nc("@label after a number", "seconds")
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: AppMetrics.labelSpacing

            FieldLabel {
                text: KI18n.i18nc("@label:slider the level below which a recording counts as silent",
                                  "Silence below")
            }

            QQC2.Slider {
                id: threshold

                from: -96
                to: -20
                stepSize: 1
                Layout.fillWidth: true
                Accessible.name: KI18n.i18nc("@label:slider", "Silence threshold")
                onMoved: panel.set("silenceThreshold", threshold.value)

                SessionBound {
                    control: threshold
                    property: "value"
                    value: Number(panel.params.silenceThreshold ?? -60)
                    suspended: threshold.pressed
                }
            }

            QQC2.Label {
                text: KI18n.i18nc("@item a level in decibels", "%1 dB", Math.round(threshold.value))
                font: AppFonts.mono
            }
        }

        InfoLine {
            text: threshold.value <= -90
                ? KI18n.i18nc("@info the silence threshold at its digital end",
                              "Only digital silence — nothing but zeros — counts.")
                : KI18n.i18nc("@info the silence threshold", "Anything quieter than this on every channel counts as silence.")
        }
    }

    QtDialogs.FolderDialog {
        id: folderDialog

        title: KI18n.i18nc("@title:window", "Choose Where Recordings Go")
        currentFolder: RecordingStatus.folderUrl(panel.params.folder ?? "")
        onAccepted: panel.set("folder", RecordingStatus.folderFromUrl(folderDialog.selectedFolder))
    }
}
