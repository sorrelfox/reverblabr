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

// **One recording, in a window of its own**, as the design's screen
// 1f draws it: its outline with a playhead, a transport, what the file is,
// its name and its tags, and what can be done with it.
//
// Several can be open at once, one a file. The one preview there is plays
// in whichever window asked last; every window shows where it has got to
// if it is its file.
//
// **The name is the file's.** Changing it renames the file on the disk, in
// its folder, keeping its extension — when the field is left or Enter is
// pressed. The tags are saved into the file with Save tags, and the window
// then shows what the file holds.
QQC2.ApplicationWindow {
    id: root

    /// The recording.
    property alias path: inspector.path

    Kirigami.Theme.colorSet: Kirigami.Theme.Window
    Kirigami.Theme.inherit: false
    color: root.ownTitleBar ? "transparent" : AppColors.window

    readonly property bool ownTitleBar: Appearance.ownStyle
    readonly property string stateName: "RecordingInspector"

    /// The parts, for the tests.
    readonly property alias inspector: inspector
    readonly property alias waveform: waveform
    readonly property alias playButton: playButton
    readonly property alias timeLabel: timeLabel
    readonly property alias formatLabel: formatLabel
    readonly property alias nameField: nameField
    readonly property alias saveTagsButton: saveTagsButton
    readonly property alias refusedMessage: refusedMessage
    readonly property alias goneMessage: goneMessage
    readonly property alias titleBar: titleBar
    readonly property alias chrome: chrome

    /// Whether this window's file is the one being listened to.
    readonly property bool previewing: RecordingPreview.path !== "" && RecordingPreview.path === inspector.path
    /// Where the listening has got to in this file, in seconds.
    readonly property real position: root.previewing ? RecordingPreview.seconds : 0

    /// The tags as they are in the fields, which is what Save tags writes.
    property var editedTags: ({})
    /// Whether the fields say something the file does not.
    readonly property bool tagsEdited: {
        const saved = inspector.tags;
        for (const key in root.editedTags) {
            if ((root.editedTags[key] ?? "") !== (saved[key] ?? "")) {
                return true;
            }
        }
        return false;
    }

    // A field's name, right-aligned in a column of its own, as the
    // Recorder's settings write theirs; wide enough for "Comment".
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

    /// A length as the transport writes it: "30:14", "1:12:04".
    function clock(seconds: real): string {
        const whole = Math.max(0, Math.floor(seconds));
        const hours = Math.floor(whole / 3600);
        const minutes = Math.floor(whole / 60) % 60;
        const rest = String(whole % 60).padStart(2, "0");
        return hours > 0 ? hours + ":" + String(minutes).padStart(2, "0") + ":" + rest : minutes + ":" + rest;
    }

    function setTag(key: string, value: string) {
        const next = Object.assign({}, root.editedTags);
        next[key] = value;
        root.editedTags = next;
    }

    /// Listens from \a seconds into the file, starting it if it is not the
    /// one being listened to.
    function seekTo(seconds: real) {
        if (inspector.seconds > 0) {
            RecordingPreview.seekTo(inspector.path, seconds / inspector.seconds);
        }
    }

    function saveSize() {
        if (root.visibility !== Window.Maximized && root.visibility !== Window.FullScreen) {
            WindowState.saveWindowSize(root.stateName, root.width, root.height);
        }
    }

    title: inspector.baseName + (inspector.extension !== "" ? "." + inspector.extension : "")
    flags: root.ownTitleBar ? (Qt.Window | Qt.FramelessWindowHint) : Qt.Window
    // The design's 820 by 520.
    width: WindowState.windowSize(root.stateName).width > 0
        ? WindowState.windowSize(root.stateName).width : Kirigami.Units.gridUnit * 46
    height: WindowState.windowSize(root.stateName).height > 0
        ? WindowState.windowSize(root.stateName).height : Kirigami.Units.gridUnit * 29
    minimumWidth: Kirigami.Units.gridUnit * 30
    minimumHeight: Kirigami.Units.gridUnit * 24

    onClosing: root.saveSize()

    RecordingInspector {
        id: inspector

        // What the file holds becomes what the fields say, whenever the
        // engine answers — after a save, too.
        onDetailsChanged: root.editedTags = Object.assign({}, inspector.tags)
    }

    menuBar: WindowTitleBar {
        id: titleBar

        visible: root.ownTitleBar
        titleBar: root.ownTitleBar
        frameRadius: chrome.frameRadius
        maximized: chrome.edgeToEdge
        onWindowMenuRequested: chrome.showWindowMenu()

        Kirigami.Icon {
            source: BlockPresentation.icon("output.recorder")
            color: AppColors.categoryTint("outputs")
            isMask: true
            implicitWidth: AppMetrics.panelIconSize
            implicitHeight: AppMetrics.panelIconSize
        }

        QQC2.Label {
            text: root.title
            color: AppColors.text
            font: AppFonts.strong
            elide: Text.ElideMiddle
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Kirigami.InlineMessage {
            id: goneMessage

            Layout.fillWidth: true
            Layout.margins: AppMetrics.panelPadding
            type: Kirigami.MessageType.Warning
            visible: inspector.gone
            text: KI18n.i18nc("@info", "This recording isn't in the list any more: it was moved to the trash, deleted or moved.")
        }

        // --- the outline, with the playhead ---
        Rectangle {
            id: wavePanel

            Layout.fillWidth: true
            Layout.preferredHeight: Kirigami.Units.gridUnit * 8
            Layout.margins: AppMetrics.panelPadding
            Layout.bottomMargin: 0
            radius: AppMetrics.panelRadius
            color: AppColors.canvas
            border.width: 1
            border.color: AppColors.border
            clip: true

            WaveformItem {
                id: waveform

                anchors.fill: parent
                anchors.leftMargin: AppMetrics.mediumSpacing
                anchors.rightMargin: AppMetrics.mediumSpacing
                anchors.topMargin: AppMetrics.mediumSpacing
                anchors.bottomMargin: AppMetrics.mediumSpacing * 2
                peaks: inspector.peaks
                progress: inspector.seconds > 0 ? root.position / inspector.seconds : 0
                playedColor: AppColors.live
                restColor: AppColors.well

                Accessible.role: Accessible.Slider
                Accessible.name: KI18n.i18nc("@label the outline of a recording, clicked to listen from there", "Position")
                Accessible.description: root.clock(root.position) + " / " + root.clock(inspector.seconds)

                // **A click listens from there.**
                TapHandler {
                    onTapped: eventPoint => root.seekTo(waveform.fractionAt(eventPoint.position.x) * inspector.seconds)
                }

                // The playhead: a line where the listening has got to.
                Rectangle {
                    id: playhead

                    visible: root.previewing
                    x: waveform.width * waveform.progress
                    width: 1
                    height: waveform.height
                    color: AppColors.text
                }
            }

            QQC2.Label {
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.margins: AppMetrics.smallSpacing
                anchors.leftMargin: AppMetrics.mediumSpacing
                text: root.clock(0)
                font: AppFonts.meterScale
                color: AppColors.muted
            }

            QQC2.Label {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: AppMetrics.smallSpacing
                anchors.rightMargin: AppMetrics.mediumSpacing
                text: root.clock(inspector.seconds)
                font: AppFonts.meterScale
                color: AppColors.muted
            }
        }

        // --- the transport ---
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: AppMetrics.panelPadding
            spacing: AppMetrics.mediumSpacing

            QQC2.Button {
                id: playButton

                icon.name: root.previewing && RecordingPreview.playing ? "media-playback-pause" : "media-playback-start"
                display: QQC2.AbstractButton.IconOnly
                text: root.previewing && RecordingPreview.playing
                    ? KI18n.i18nc("@action:button pause listening to a recording", "Pause")
                    : KI18n.i18nc("@action:button listen to a recording", "Play")
                enabled: inspector.loaded && !inspector.gone
                QQC2.ToolTip.text: text + " (" + KI18n.i18nc("@info the key that plays and pauses", "Space") + ")"
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                onClicked: RecordingPreview.toggle(inspector.path)
            }

            QQC2.Button {
                icon.name: "media-skip-backward"
                display: QQC2.AbstractButton.IconOnly
                text: KI18n.i18nc("@action:button listen from the start of a recording", "To the start")
                enabled: playButton.enabled
                QQC2.ToolTip.text: text
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                onClicked: root.seekTo(0)
            }

            QQC2.Button {
                icon.name: "media-skip-forward"
                display: QQC2.AbstractButton.IconOnly
                text: KI18n.i18nc("@action:button go to the last seconds of a recording", "To the end")
                enabled: playButton.enabled
                QQC2.ToolTip.text: text
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                // The last five seconds, which is what is worth hearing of
                // the end: at the very end there is nothing left to play.
                onClicked: root.seekTo(Math.max(0, inspector.seconds - 5))
            }

            QQC2.Label {
                id: timeLabel

                text: root.clock(root.position) + " / " + root.clock(inspector.seconds)
                font: AppFonts.readout
                color: AppColors.text
            }

            Item {
                Layout.fillWidth: true
            }

            QQC2.Label {
                id: formatLabel

                text: {
                    if (!inspector.loaded) {
                        return "";
                    }
                    const parts = [inspector.format.toUpperCase()];
                    if (inspector.sampleRate > 0) {
                        parts.push(KI18n.i18nc("@info a sample rate in kilohertz", "%1 kHz",
                                               Number(inspector.sampleRate / 1000).toLocaleString(Qt.locale(), 'f',
                                                   inspector.sampleRate % 1000 === 0 ? 0 : 1)));
                    }
                    if (inspector.channels === 1) {
                        parts.push(KI18n.i18nc("@info one channel", "Mono"));
                    } else if (inspector.channels === 2) {
                        parts.push(KI18n.i18nc("@info two channels", "Stereo"));
                    } else if (inspector.channels > 2) {
                        parts.push(KI18n.i18ncp("@info how many channels", "%1 channel", "%1 channels", inspector.channels));
                    }
                    parts.push(sizes.formatSize(inspector.sizeBytes));
                    return parts.join(" · ");
                }
                color: AppColors.muted
            }

            RecordingList {
                id: sizes
            }
        }

        // --- the name and the tags ---
        GridLayout {
            Layout.fillWidth: true
            Layout.leftMargin: AppMetrics.panelPadding
            Layout.rightMargin: AppMetrics.panelPadding
            columns: 4
            columnSpacing: AppMetrics.labelSpacing
            rowSpacing: AppMetrics.smallSpacing
            enabled: inspector.loaded && !inspector.gone

            FieldLabel {
                text: KI18n.i18nc("@label:textbox a recording's file name", "File name")
            }

            RowLayout {
                Layout.columnSpan: 3
                Layout.fillWidth: true
                spacing: AppMetrics.smallSpacing

                QQC2.TextField {
                    id: nameField

                    Layout.fillWidth: true
                    font.family: AppFonts.monoFamily
                    text: inspector.baseName
                    Accessible.name: KI18n.i18nc("@label:textbox a recording's file name", "File name")
                    // **Renamed when the field is left**, or on Enter; Escape
                    // puts the name back.
                    onEditingFinished: inspector.rename(nameField.text)
                    Keys.onEscapePressed: nameField.text = inspector.baseName
                }

                QQC2.Label {
                    text: inspector.extension !== "" ? "." + inspector.extension : ""
                    font.family: AppFonts.monoFamily
                    color: AppColors.muted
                }
            }

            Repeater {
                model: [
                    { key: "title", name: KI18n.i18nc("@label a recording's tag", "Title"), span: 3 },
                    { key: "artist", name: KI18n.i18nc("@label a recording's tag", "Artist"), span: 1 },
                    { key: "album", name: KI18n.i18nc("@label a recording's tag", "Album"), span: 1 },
                    { key: "genre", name: KI18n.i18nc("@label a recording's tag", "Genre"), span: 1 },
                    { key: "year", name: KI18n.i18nc("@label a recording's tag", "Year"), span: 1 },
                    { key: "track", name: KI18n.i18nc("@label a recording's tag", "Track"), span: 1 },
                    { key: "trackCount", name: KI18n.i18nc("@label a recording's tag, how many tracks there are", "Of tracks"), span: 1 },
                    { key: "comment", name: KI18n.i18nc("@label a recording's tag", "Comment"), span: 3 }
                ]

                delegate: Item {
                    id: tagCell

                    required property var modelData

                    Layout.columnSpan: 1 + tagCell.modelData.span
                    Layout.fillWidth: true
                    implicitHeight: tagRow.implicitHeight

                    RowLayout {
                        id: tagRow

                        anchors.fill: parent
                        spacing: AppMetrics.labelSpacing

                        FieldLabel {
                            text: tagCell.modelData.name
                        }

                        QQC2.TextField {
                            objectName: "tag-" + tagCell.modelData.key
                            Layout.fillWidth: true
                            text: root.editedTags[tagCell.modelData.key] ?? ""
                            Accessible.name: tagCell.modelData.name
                            onTextEdited: root.setTag(tagCell.modelData.key, text)
                            onAccepted: if (root.tagsEdited) {
                                inspector.saveTags(root.editedTags);
                            }
                        }
                    }
                }
            }
        }

        Kirigami.InlineMessage {
            id: refusedMessage

            Layout.fillWidth: true
            Layout.margins: AppMetrics.panelPadding
            type: Kirigami.MessageType.Error
            visible: inspector.refusal !== ""
            text: inspector.refusal
        }

        Item {
            Layout.fillHeight: true
        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        // --- what can be done with it ---
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: AppMetrics.panelPadding
            spacing: AppMetrics.buttonSpacing
            enabled: inspector.loaded && !inspector.gone

            QQC2.Button {
                text: KI18n.i18nc("@action:button open the file manager at a recording's folder", "Open Containing Folder")
                icon.name: "document-open-folder"
                onClicked: FileActions.showInFolder(inspector.path, root)
            }

            QQC2.Button {
                text: KI18n.i18nc("@action:button choose an application to open a recording with", "Open With…")
                icon.name: "document-open"
                onClicked: FileActions.openWith(inspector.path, root)
            }

            QQC2.Button {
                text: KI18n.i18nc("@action:button", "Move to Trash")
                icon.name: "user-trash"
                onClicked: {
                    if (RecordingPreview.path === inspector.path) {
                        RecordingPreview.stop();
                    }
                    FileActions.moveToTrash(inspector.path, root);
                }
            }

            Item {
                Layout.fillWidth: true
            }

            QQC2.Button {
                id: saveTagsButton

                text: KI18n.i18nc("@action:button write a recording's tags into its file", "Save Tags")
                highlighted: true
                enabled: root.tagsEdited
                onClicked: inspector.saveTags(root.editedTags)
            }
        }
    }

    // **Space listens, the arrows move**, wherever the focus is but in a
    // field being typed into.
    Shortcut {
        sequences: ["Space"]
        enabled: !(root.activeFocusItem instanceof TextInput) && playButton.enabled
        onActivated: RecordingPreview.toggle(inspector.path)
    }

    Shortcut {
        sequences: ["Left"]
        enabled: !(root.activeFocusItem instanceof TextInput) && root.previewing
        onActivated: root.seekTo(Math.max(0, root.position - 5))
    }

    Shortcut {
        sequences: ["Right"]
        enabled: !(root.activeFocusItem instanceof TextInput) && root.previewing
        onActivated: root.seekTo(Math.min(inspector.seconds, root.position + 5))
    }

    WindowChrome {
        id: chrome

        window: root
        active: root.ownTitleBar
    }
}
