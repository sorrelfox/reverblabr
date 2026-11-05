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
import org.kde.ki18n

// The Recorder's file name: the pattern, chips that put a token in it
// where the cursor is, and what the next file will be called, live.
//
// **The pattern is text with tokens in it** — "%session %date %time" — as
// the engine holds it, so what is typed is what is kept, and a person who
// knows the tokens can type them. The chips are for the rest of us, and the
// preview is the writer's own naming, so it cannot promise a name the file
// will not get: a slash in a session's name is a hyphen there too.
ColumnLayout {
    id: editor

    /// The block whose file name this is.
    property string blockId: ""
    /// The format the preview's extension is for.
    property string format: "mp3"

    readonly property string pattern: SessionModel.revision >= 0 ? (SessionModel.param(editor.blockId, "fileName") ?? "") : ""
    readonly property string blockName: SessionModel.revision >= 0
        ? (SessionModel.block(editor.blockId).nickname || editor.blockId) : editor.blockId
    /// What the next file will be called. Read again every few seconds, so
    /// the time in it moves on.
    property string preview: ""

    readonly property alias field: field
    readonly property alias chips: chips
    readonly property alias previewLabel: previewText

    spacing: AppMetrics.smallSpacing

    RecorderWords {
        id: words
    }

    function refresh() {
        editor.preview = RecordingStatus.namePreview(field.activeFocus ? field.text : editor.pattern,
                                                     SessionModel.name, editor.blockName, editor.format);
    }

    /// Puts \a token where the cursor is, with a space either side where it
    /// would otherwise run into the words next to it, and keeps it.
    function insert(token: string) {
        const at = field.activeFocus ? field.cursorPosition : field.text.length;
        const before = field.text.slice(0, at);
        const after = field.text.slice(at);
        const lead = before.length > 0 && !/\s$/.test(before) ? " " : "";
        const trail = after.length > 0 && !/^\s/.test(after) ? " " : "";
        field.text = before + lead + token + trail + after;
        field.cursorPosition = (before + lead + token).length;
        editor.commit();
    }

    function commit() {
        if (field.text !== editor.pattern) {
            SessionModel.setParam(editor.blockId, "fileName", field.text);
        }
        editor.refresh();
    }

    onPatternChanged: editor.refresh()
    onFormatChanged: editor.refresh()
    onBlockNameChanged: editor.refresh()
    Component.onCompleted: editor.refresh()

    Timer {
        interval: 5000
        repeat: true
        running: editor.visible
        onTriggered: editor.refresh()
    }

    QQC2.TextField {
        id: field

        Layout.fillWidth: true
        Layout.preferredWidth: 1
        placeholderText: words.defaultPattern
        Accessible.name: KI18n.i18nc("@label:textbox a recording's file name, tokens and all", "File name")
        onTextEdited: editor.refresh()
        onEditingFinished: editor.commit()

        SessionBound {
            control: field
            property: "text"
            value: editor.pattern
            suspended: field.activeFocus
        }
    }

    Flow {
        id: chips

        Layout.fillWidth: true
        spacing: AppMetrics.smallSpacing

        Repeater {
            model: words.tokens

            delegate: QQC2.Button {
                id: chip

                required property string modelData

                text: words.tokenName(chip.modelData)
                icon.name: "list-add"
                flat: true
                font: AppFonts.small
                focusPolicy: Qt.NoFocus
                Accessible.name: KI18n.i18nc("@action:button %1 is a file-name token, 'Session'",
                                             "Add %1 to the file name", words.tokenName(chip.modelData))
                onClicked: editor.insert(chip.modelData)
            }
        }
    }

    ElidedLabel {
        id: previewText

        text: KI18n.i18nc("@info what the next recording's file will be called", "Next file: %1", editor.preview)
        font: AppFonts.small
        color: AppColors.muted
        elide: Text.ElideMiddle
        Layout.fillWidth: true
        Layout.preferredWidth: 1
    }
}
