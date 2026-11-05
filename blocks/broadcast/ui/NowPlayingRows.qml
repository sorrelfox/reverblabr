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
import org.kde.ki18n

// **Where a stream's title learns what is playing**: nowhere but its
// template, a text file, or a media player. The same rows in every panel of
// a block whose title follows what is playing (Broadcast, Live Stream),
// over the same three parameters (nowPlaying, nowPlayingFile,
// nowPlayingPlayer), so a source added here is offered by all of them.
ColumnLayout {
    id: rows

    /// The block whose settings these are, and its parameters as the panel
    /// has them.
    property string blockId: ""
    property var params: ({})

    readonly property string source: rows.params.nowPlaying ?? "none"
    readonly property string player: rows.params.nowPlayingPlayer ?? ""
    /// The players a title can follow. **The bus is read only while the
    /// title is to come from one**: until then MediaPlayers is never asked.
    readonly property var playerChoices: words.playerChoices(
        rows.source === "player" ? MediaPlayers.players : [], rows.player)

    /// The controls, so that a test drives what a person drives.
    readonly property alias sourceBox: nowPlayingControl
    readonly property alias fileField: nowPlayingFileControl
    readonly property alias playerBox: playerControl
    readonly property alias playerLine: playerHint

    spacing: AppMetrics.panelRowGap

    BroadcastWords {
        id: words
    }

    function set(paramId: string, value: var) {
        SessionModel.setParam(rows.blockId, paramId, value);
    }

    component FieldLabel: QQC2.Label {
        color: AppColors.muted
        horizontalAlignment: Text.AlignRight
        elide: Text.ElideRight
        wrapMode: Text.Wrap
        maximumLineCount: 2
        Layout.preferredWidth: AppMetrics.panelLabelWidth
    }

    component ParamField: QQC2.TextField {
        id: field

        required property string paramId

        Layout.preferredWidth: 1
        Layout.fillWidth: true
        onEditingFinished: {
            if (field.text !== (rows.params[field.paramId] ?? "")) {
                rows.set(field.paramId, field.text);
            }
        }

        SessionBound {
            control: field
            property: "text"
            value: rows.params[field.paramId] ?? ""
            suspended: field.activeFocus
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: AppMetrics.labelSpacing

        FieldLabel {
            text: KI18n.i18nc("@label:listbox where a stream's title comes from", "Now playing")
        }

        QQC2.ComboBox {
            id: nowPlayingControl

            Layout.preferredWidth: 1
            Layout.fillWidth: true
            model: words.nowPlayingChoices.map(id => words.nowPlayingName(id))
            Accessible.name: KI18n.i18nc("@label:listbox", "Now playing")
            onActivated: index => rows.set("nowPlaying", words.nowPlayingChoices[index])

            SessionBound {
                control: nowPlayingControl
                property: "currentIndex"
                value: words.nowPlayingChoices.indexOf(rows.source)
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: AppMetrics.labelSpacing
        visible: rows.source === "file"

        FieldLabel {
            text: KI18n.i18nc("@label:textbox the text file a stream's title is read from", "File")
        }

        ParamField {
            id: nowPlayingFileControl

            paramId: "nowPlayingFile"
            placeholderText: KI18n.i18nc("@info:placeholder", "Its first line is the title")
            Accessible.name: KI18n.i18nc("@label:textbox", "File")
        }

        QQC2.ToolButton {
            icon.name: "document-open"
            display: QQC2.AbstractButton.IconOnly
            text: KI18n.i18nc("@action:button choose the text file a stream's title is read from", "Choose File…")
            Accessible.name: text
            QQC2.ToolTip.text: text
            QQC2.ToolTip.visible: hovered
            onClicked: fileDialog.open()
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: AppMetrics.labelSpacing
        visible: rows.source === "player"

        FieldLabel {
            text: KI18n.i18nc("@label:listbox the media player a stream's title follows", "Player")
        }

        QQC2.ComboBox {
            id: playerControl

            Layout.preferredWidth: 1
            Layout.fillWidth: true
            model: rows.playerChoices.map(choice => choice.name)
            Accessible.name: KI18n.i18nc("@label:listbox", "Player")
            onActivated: index => rows.set("nowPlayingPlayer", rows.playerChoices[index].id)
            // **A player that comes or goes is a new list**, which puts
            // the box back to its first line whatever the block holds.
            onModelChanged: Qt.callLater(playerBound.settle)

            SessionBound {
                id: playerBound

                control: playerControl
                property: "currentIndex"
                value: rows.playerChoices.findIndex(choice => choice.id === rows.player)
            }
        }
    }

    InfoLine {
        id: playerHint

        visible: rows.source === "player" && rows.player === ""
        text: KI18n.i18nc("@info how a stream's title picks the media player it follows",
                          "A player this session's Application blocks capture, or the one playing when they capture none.")
    }

    QtDialogs.FileDialog {
        id: fileDialog

        title: KI18n.i18nc("@title:window", "Choose the Now Playing File")
        nameFilters: [KI18n.i18nc("@item:inlistbox a file type", "Text files (*.txt)"),
            KI18n.i18nc("@item:inlistbox a file type", "All files (*)")]
        onAccepted: rows.set("nowPlayingFile", RecordingStatus.folderFromUrl(fileDialog.selectedFile))
    }
}
