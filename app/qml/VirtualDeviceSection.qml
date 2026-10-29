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

// One kind of virtual device in the Virtual Devices window: a
// heading, a line saying what this kind is for, and its devices.
//
// **What it is for is always said**, even while there is none of it: the
// difference between an output and a microphone is the thing somebody
// opening this window for the first time has to work out.
//
// **The keys act on the selected row**: the arrows move, F2 renames, Space
// switches it on or off and Delete asks whether to delete it. The row's own
// buttons are there for the pointer, and in the Tab order as well.
ColumnLayout {
    id: section

    /// "output" or "input".
    required property string direction
    required property string title
    required property string description
    /// What the list says while it has nothing.
    required property string emptyText

    readonly property alias list: list
    readonly property alias devices: filter
    readonly property alias emptyLabel: emptyLabel

    signal renameRequested(string deviceId, string name)
    signal enableRequested(string deviceId, bool enabled)
    signal deleteRequested(string deviceId, string name)

    /// Selects the device \a deviceId, if it is one of this section's, and
    /// gives the list the focus. Answers whether it was.
    function select(deviceId: string): bool {
        const row = filter.rowOf(deviceId);
        if (row < 0) {
            return false;
        }
        list.currentIndex = row;
        list.forceActiveFocus();
        return true;
    }

    spacing: AppMetrics.smallSpacing

    QQC2.Label {
        Layout.fillWidth: true
        text: section.title
        color: AppMetrics.followsSystem ? AppColors.text : AppColors.muted
        font: AppMetrics.followsSystem ? AppFonts.strong : AppFonts.libraryHeading
        Accessible.role: Accessible.Heading
        Accessible.name: section.title
    }

    QQC2.Label {
        Layout.fillWidth: true
        text: section.description
        color: AppColors.muted
        font: AppFonts.small
        wrapMode: Text.WordWrap
    }

    // **A column of rows, not a ListView.** The window scrolls, not each
    // list, so each list is as tall as its rows — and a ListView sized to
    // its own content height is one that starts empty at a height of
    // nothing and never makes a row to measure. A Repeater makes every row;
    // there are at most a few dozen.
    FocusScope {
        id: list

        /// The selected row, which the keys act on.
        property int currentIndex: 0
        readonly property int count: repeater.count

        /// The selected row. **A function, not a property**: a binding on
        /// Repeater.itemAt() is worked out once, while the list is empty,
        /// and nothing tells it the rows have come.
        function currentRow(): VirtualDeviceRow {
            return list.itemAtIndex(list.currentIndex);
        }

        function itemAtIndex(index: int): VirtualDeviceRow {
            return repeater.itemAt(index) as VirtualDeviceRow;
        }

        /// Moves the selection by \a step rows, within the list.
        function move(step: int) {
            if (list.count > 0) {
                list.currentIndex = Math.max(0, Math.min(list.count - 1, list.currentIndex + step));
            }
        }

        Layout.fillWidth: true
        implicitHeight: rows.implicitHeight
        visible: list.count > 0
        activeFocusOnTab: true
        Accessible.role: Accessible.List
        Accessible.name: section.title

        // **The selection stays on a row that is there**: a device deleted
        // from the end takes the selection back to the one before it.
        onCountChanged: {
            if (list.currentIndex >= list.count) {
                list.currentIndex = Math.max(0, list.count - 1);
            }
        }

        Column {
            id: rows

            width: list.width
            spacing: AppMetrics.hairSpacing

            Repeater {
                id: repeater

                model: VirtualDeviceFilter {
                    id: filter

                    sourceModel: VirtualDevices
                    direction: section.direction
                }

                delegate: VirtualDeviceRow {
                    width: rows.width
                    current: index === list.currentIndex
                    listFocused: list.activeFocus
                    onSelected: {
                        list.currentIndex = index;
                        list.forceActiveFocus();
                    }
                    onEditingEnded: list.forceActiveFocus()
                    onRenameRequested: (deviceId, name) => section.renameRequested(deviceId, name)
                    onEnableRequested: (deviceId, enabled) => section.enableRequested(deviceId, enabled)
                    onDeleteRequested: (deviceId, name) => section.deleteRequested(deviceId, name)
                }
            }
        }

        Keys.onPressed: event => {
            const row = list.currentRow();
            if (row === null || row.renaming) {
                return;
            }
            switch (event.key) {
            case Qt.Key_Up:
                list.move(-1);
                break;
            case Qt.Key_Down:
                list.move(1);
                break;
            case Qt.Key_Home:
                list.currentIndex = 0;
                break;
            case Qt.Key_End:
                list.currentIndex = list.count - 1;
                break;
            case Qt.Key_F2:
                row.startRenaming();
                break;
            case Qt.Key_Space:
                row.toggleEnabled();
                break;
            case Qt.Key_Delete:
                row.requestDelete();
                break;
            default:
                return;
            }
            event.accepted = true;
        }
    }

    QQC2.Label {
        id: emptyLabel

        Layout.fillWidth: true
        visible: list.count === 0
        text: section.emptyText
        color: AppColors.muted
        font: AppFonts.small
        wrapMode: Text.WordWrap
    }
}
