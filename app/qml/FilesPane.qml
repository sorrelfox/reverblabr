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

// The sidebar's Files tab: the recordings this session has made,
// newest first, and what can be done with one.
//
// **The list is the engine's recordings index**, told again whenever a
// Recorder starts or finishes a file. It is checked against the disk each
// time the tab comes into view, so a file deleted in a file manager is gone
// from it by the time anybody looks.
//
// **The actions are the desktop's** (FileActions): the folder opened in
// the file manager with the file selected, the desktop's "Open With" list,
// and the freedesktop trash — never a delete. They are in a menu on a right
// click, on the menu key, and on the chosen card's own button; Delete moves
// the chosen one to the trash and Enter opens it.
FocusScope {
    id: pane

    /// Whose recordings.
    property alias sessionId: recordings.sessionId
    /// Whether the tab is in view. Coming into view checks the disk.
    property bool shown: false

    /// The path of the chosen recording, or empty. **Kept by path**, since a
    /// row number stops meaning the same file the moment one arrives.
    property string selectedPath: ""

    readonly property alias recordings: recordings
    readonly property alias list: list
    readonly property alias menu: menu
    readonly property alias failureMessage: failure
    readonly property alias emptyMessage: empty

    /// Enter or a double click. The Recording Inspector opens from here.
    signal recordingActivated(string path)

    function select(path: string) {
        pane.selectedPath = path;
        list.currentIndex = recordings.rowOf(path);
    }

    function openMenu(path: string, item: Item, x: real, y: real) {
        pane.select(path);
        menu.path = path;
        menu.popup(item, x, y);
    }

    function trash(path: string) {
        if (path !== "") {
            // A file being listened to is let go of before it goes.
            if (RecordingPreview.path === path) {
                RecordingPreview.stop();
            }
            FileActions.moveToTrash(path, pane.Window.window);
        }
    }

    onShownChanged: if (pane.shown) {
        recordings.rescan();
    }

    RecordingList {
        id: recordings

        // A chosen recording that has gone — trashed, deleted outside — is
        // no longer chosen.
        onCountChanged: if (recordings.rowOf(pane.selectedPath) < 0) {
            pane.selectedPath = "";
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: AppMetrics.libraryInset
        spacing: AppMetrics.hairSpacing

        QQC2.Label {
            Layout.fillWidth: true
            Layout.leftMargin: AppMetrics.libraryInset
            Layout.topMargin: AppMetrics.followsSystem ? Kirigami.Units.smallSpacing : AppMetrics.libraryInset * 2
            text: KI18n.i18nc("@title:group the recordings in the Files tab", "Recordings")
            font: AppFonts.libraryHeading
            color: AppColors.muted
            Accessible.role: Accessible.Heading
        }

        QQC2.ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            visible: recordings.count > 0

            ListView {
                id: list

                clip: true
                model: recordings
                spacing: AppMetrics.hairSpacing
                keyNavigationEnabled: true
                activeFocusOnTab: true
                focus: true
                currentIndex: -1
                Accessible.role: Accessible.List
                Accessible.name: KI18n.i18nc("@info the list of recordings in the Files tab", "Recordings")

                onCurrentIndexChanged: if (list.currentIndex >= 0) {
                    pane.selectedPath = recordings.get(list.currentIndex).path ?? "";
                }

                delegate: RecordingCard {
                    width: ListView.view.width
                    list: recordings
                    selected: pane.selectedPath === path
                    onClicked: {
                        pane.select(path);
                        list.forceActiveFocus();
                    }
                    onActivated: pane.recordingActivated(path)
                    onMenuRequested: (x, y) => pane.openMenu(path, this, x, y)
                }

                Keys.onReturnPressed: pane.recordingActivated(pane.selectedPath)
                Keys.onEnterPressed: pane.recordingActivated(pane.selectedPath)
                Keys.onDeletePressed: pane.trash(pane.selectedPath)
                // Space listens to the chosen recording, or pauses it.
                Keys.onSpacePressed: if (pane.selectedPath !== "") {
                    RecordingPreview.toggle(pane.selectedPath);
                }
                Keys.onMenuPressed: {
                    const card = list.currentItem;
                    if (card !== null) {
                        pane.openMenu(pane.selectedPath, card, card.width / 2, card.height / 2);
                    }
                }
            }
        }

        // **An empty list says what would fill it.**
        PlaceholderMessage {
            id: empty

            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: recordings.count === 0
            text: KI18n.i18nc("@info:placeholder the Files tab of a session that has recorded nothing",
                              "Nothing recorded yet. Put a Recorder in a lane and run the session: its files appear here.")
        }
    }

    QQC2.Menu {
        id: menu

        /// Which recording the menu is about.
        property string path: ""

        QQC2.MenuItem {
            text: KI18n.i18nc("@action:inmenu open the file manager at a recording's folder", "Open Containing Folder")
            icon.name: "document-open-folder"
            onTriggered: FileActions.showInFolder(menu.path, pane.Window.window)
        }

        QQC2.MenuItem {
            text: KI18n.i18nc("@action:inmenu choose an application to open a recording with", "Open With…")
            icon.name: "document-open"
            onTriggered: FileActions.openWith(menu.path, pane.Window.window)
        }

        QQC2.MenuSeparator {}

        DestructiveMenuItem {
            text: KI18n.i18nc("@action:inmenu", "Move to Trash")
            icon.name: "user-trash"
            onTriggered: pane.trash(menu.path)
        }
    }

    // **KIO's words when an action fails**, which are already translated
    // and already say what went wrong with which file.
    Kirigami.InlineMessage {
        id: failure

        anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
            margins: AppMetrics.libraryInset
        }
        type: Kirigami.MessageType.Error
        showCloseButton: true
        visible: false

        Connections {
            target: FileActions

            function onFailed(path: string, action: int, error: string) {
                failure.text = error;
                failure.visible = true;
            }
        }
    }
}
