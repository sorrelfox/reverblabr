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
import io.github.reverblabr

// **The session as a list, not a picture**: for somebody who cannot
// see the canvas, or cannot aim at it. The blocks are in lanes, a lane
// being a run of blocks each feeding only the next, and every block is a
// row a screen reader reads out whole: what it is, whether it is running,
// and what it is joined to.
//
// **The canvas's own blocks, not a copy.** Each row reads its words from
// the block's tile, which is still there behind the list, so the two
// cannot say different things; and every key a row does not take — C, D,
// Esc, Space — goes on to the canvas, which does what it does for a tile.
//
// **One Tab stop for the whole list**, as a list box has: Tab comes to the
// row that was last in hand, and the arrows, Home and End go from row to
// row across the lanes. Enter opens the settings, the Menu key the block's
// menu, and Delete takes it out.
Rectangle {
    id: list

    /// The canvas this is a view of.
    required property var canvas

    color: AppColors.canvas

    /// The row Tab comes back to.
    property string currentId: ""
    /// Where the list is scrolled to, for a panel placed beside a row.
    readonly property real scrollY: flick.contentY
    /// How far down the list starts, to leave the canvas's connection mode
    /// where it is.
    property real topInset: 0

    /// The lanes, each a list of block ids in the order the sound goes
    /// through them. **A lane ends where the signal branches or joins**: a
    /// block feeding two others, or fed by two, starts or ends one.
    ///
    /// **Replaced only when it changes**, not on every edit: a new list is
    /// new rows, and new rows are a keyboard that has lost its place.
    property var lanes: []
    readonly property string laneKey: JSON.stringify(list.laneList())

    onLaneKeyChanged: {
        const had = list.focusedId();
        list.lanes = JSON.parse(list.laneKey);
        if (had !== "") {
            Qt.callLater(() => list.focusRow(had));
        }
    }

    function laneList(): var {
        void SessionModel.revision;
        void list.canvas.wires.count;
        void list.canvas.blocks.count;
        const ids = list.canvas.blocksInReadingOrder();
        const next = {};
        const previous = {};
        for (const id of ids) {
            next[id] = [];
            previous[id] = [];
        }
        for (let row = 0; row < list.canvas.wires.count; ++row) {
            const wire = list.canvas.wires.at(row);
            if (next[wire.fromBlock] === undefined || previous[wire.toBlock] === undefined) {
                continue;
            }
            if (next[wire.fromBlock].indexOf(wire.toBlock) < 0) {
                next[wire.fromBlock].push(wire.toBlock);
            }
            if (previous[wire.toBlock].indexOf(wire.fromBlock) < 0) {
                previous[wire.toBlock].push(wire.fromBlock);
            }
        }
        const joined = (from, to) => next[from].length === 1 && previous[to].length === 1 && next[from][0] === to;
        const placed = {};
        const found = [];
        for (const id of ids) {
            if (placed[id]) {
                continue;
            }
            // Back to where this block's lane begins, then along it.
            let start = id;
            const seen = {};
            seen[start] = true;
            while (previous[start].length === 1 && joined(previous[start][0], start) && !seen[previous[start][0]]
                   && !placed[previous[start][0]]) {
                start = previous[start][0];
                seen[start] = true;
            }
            const lane = [start];
            placed[start] = true;
            let at = start;
            while (next[at].length === 1 && joined(at, next[at][0]) && !placed[next[at][0]]) {
                at = next[at][0];
                lane.push(at);
                placed[at] = true;
            }
            found.push(lane);
        }
        return found;
    }

    /// Every block in the list's order: lane by lane, first to last.
    function order(): var {
        return [].concat(...list.lanes);
    }

    /// The rows there are, lane by lane. **Asked of the repeaters each
    /// time**, never kept in a property: Qt 6.10 holds an object put in a
    /// \c var property as a map of bare pointers, which outlive the rows,
    /// and writing to it while the rows are rebuilt crashes the engine.
    function allRows(): var {
        const found = [];
        for (let lane = 0; lane < laneRepeater.count; ++lane) {
            // A lane's rows are made by the one child of it that is a
            // repeater.
            for (const child of laneRepeater.itemAt(lane)?.children ?? []) {
                const rows = child as Repeater;
                for (let index = 0; rows !== null && index < rows.count; ++index) {
                    const row = rows.itemAt(index);
                    if (row !== null) {
                        found.push(row);
                    }
                }
            }
        }
        return found;
    }

    function rowFor(blockId: string): var {
        return list.allRows().find(row => row.blockId === blockId) ?? null;
    }

    /// Gives the keyboard to \a blockId's row.
    function focusRow(blockId: string) {
        const row = list.rowFor(blockId);
        if (row !== null) {
            // **The focus first, then the Tab stop**: Qt will not take the
            // stop off the row that has the focus, so the row the keyboard
            // is leaving has to have been left before it loses it.
            row.forceActiveFocus(Qt.TabFocusReason);
            list.currentId = blockId;
            list.showRow(blockId);
        }
    }

    /// The block whose row has the keyboard, or nothing.
    function focusedId(): string {
        return list.allRows().find(row => row.activeFocus)?.blockId ?? "";
    }

    /// Scrolls \a blockId's row into sight.
    function showRow(blockId: string) {
        const row = list.rowFor(blockId);
        if (row === null) {
            return;
        }
        const top = row.mapToItem(column, 0, 0).y;
        if (top < flick.contentY) {
            flick.contentY = top;
        } else if (top + row.height > flick.contentY + flick.height) {
            flick.contentY = top + row.height - flick.height;
        }
    }

    /// Where \a blockId's row is, in the canvas's pixels, for a settings
    /// panel to be put beside it.
    function rowRect(blockId: string): rect {
        void list.scrollY;
        const row = list.rowFor(blockId);
        if (row === null) {
            return Qt.rect(0, 0, 0, 0);
        }
        const at = row.mapToItem(list.canvas, 0, 0);
        return Qt.rect(at.x, at.y, row.width, row.height);
    }

    /// Moves the keyboard \a step rows on from \a blockId, or to the first
    /// or last row.
    function moveFrom(blockId: string, step: real) {
        const ids = list.order();
        if (ids.length === 0) {
            return;
        }
        const index = ids.indexOf(blockId);
        const wanted = step === Number.NEGATIVE_INFINITY ? 0
            : step === Number.POSITIVE_INFINITY ? ids.length - 1
            : Math.max(0, Math.min(ids.length - 1, index + step));
        list.focusRow(ids[wanted]);
    }

    // **One Tab stop.** Moved here rather than bound on each row, so that
    // the order is ours: see focusRow().
    onCurrentIdChanged: {
        for (const row of list.allRows()) {
            row.activeFocusOnTab = row.blockId === list.currentId;
        }
    }

    /// The row that keeps the Tab stop, when the one that had it has gone.
    onLanesChanged: {
        const ids = list.order();
        if (ids.indexOf(list.currentId) < 0) {
            list.currentId = ids.length > 0 ? ids[0] : "";
        }
    }

    Accessible.role: Accessible.List
    Accessible.name: KI18n.i18nc("@info:whatsthis the session shown as a list of its blocks", "Blocks in the session")

    QQC2.Label {
        anchors.centerIn: parent
        width: Math.min(parent.width - Kirigami.Units.gridUnit * 2, Kirigami.Units.gridUnit * 24)
        visible: list.lanes.length === 0
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        color: AppColors.muted
        text: SessionModel.sessionId !== ""
            ? KI18n.i18nc("@info:placeholder", "Drag a block from the list on the left, or click one to add it")
            : KI18n.i18nc("@info:placeholder", "Open or create a session to start building one")
    }

    Flickable {
        id: flick

        anchors.fill: parent
        anchors.topMargin: list.topInset
        clip: true
        contentWidth: width
        contentHeight: column.height + Kirigami.Units.largeSpacing * 2
        boundsBehavior: Flickable.StopAtBounds

        QQC2.ScrollBar.vertical: QQC2.ScrollBar {}

        Column {
            id: column

            x: Kirigami.Units.largeSpacing
            y: Kirigami.Units.largeSpacing
            // **Not the whole width**: a block's settings open beside its
            // row, as they do beside a tile, and need the room.
            width: Math.min(flick.width - Kirigami.Units.largeSpacing * 2, Kirigami.Units.gridUnit * 28)
            spacing: Kirigami.Units.largeSpacing

            Repeater {
                id: laneRepeater

                model: list.lanes

                delegate: Column {
                    id: lane

                    required property var modelData
                    required property int index

                    width: column.width
                    spacing: AppMetrics.hairSpacing

                    Accessible.role: Accessible.List
                    Accessible.name: laneHeading.text

                    Kirigami.Heading {
                        id: laneHeading

                        level: 4
                        width: parent.width
                        elide: Text.ElideRight
                        color: AppColors.muted
                        text: KI18n.i18ncp("@title a lane of the session shown as a list: a run of blocks, each feeding the next",
                                           "Lane %2, %1 block", "Lane %2, %1 blocks", lane.modelData.length,
                                           lane.index + 1)
                        Accessible.role: Accessible.Heading
                        Accessible.name: laneHeading.text
                    }

                    Repeater {
                        model: lane.modelData

                        delegate: Rectangle {
                            id: row

                            required property string modelData

                            readonly property string blockId: row.modelData
                            readonly property var tile: {
                                void list.canvas.tiles.count;
                                void SessionModel.revision;
                                return list.canvas.tileFor(row.blockId);
                            }
                            readonly property bool selected: list.canvas.isSelected(row.blockId)
                            /// Where a wire drawn from the keyboard would go.
                            readonly property bool aimedAt: list.canvas.keyWiring
                                                            && list.canvas.wiringOverBlock === row.blockId

                            width: lane.width
                            implicitHeight: rowColumn.implicitHeight + AppMetrics.rowSpacing * 2
                            height: implicitHeight
                            radius: AppMetrics.controlRadius
                            color: row.selected ? AppColors.accentSoft : AppColors.surface
                            border.width: row.activeFocus || row.aimedAt ? 2 : 1
                            border.color: row.activeFocus || row.aimedAt ? AppColors.accentMark : AppColors.border

                            // Set by the list as the row in hand changes,
                            // not bound: see onCurrentIdChanged.
                            activeFocusOnTab: false

                            Accessible.role: Accessible.ListItem
                            Accessible.name: row.tile !== null ? row.tile.Accessible.name : ""
                            Accessible.description: row.tile !== null ? row.tile.Accessible.description : ""
                            Accessible.focusable: true
                            Accessible.selected: row.selected
                            Accessible.onPressAction: list.canvas.activateBlock(row.blockId)

                            Component.onCompleted: row.activeFocusOnTab = row.blockId === list.currentId

                            onActiveFocusChanged: if (row.activeFocus) {
                                list.currentId = row.blockId;
                                list.showRow(row.blockId);
                                if (!list.canvas.keyWiring && !list.canvas.keyUnwiring) {
                                    list.canvas.selectOnly(row.blockId);
                                }
                            }

                            ColumnLayout {
                                id: rowColumn

                                anchors {
                                    left: parent.left
                                    right: parent.right
                                    verticalCenter: parent.verticalCenter
                                    leftMargin: AppMetrics.rowSpacing * 2
                                    rightMargin: AppMetrics.rowSpacing * 2
                                }
                                spacing: 0

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: Kirigami.Units.smallSpacing

                                    ElidedLabel {
                                        Layout.fillWidth: true
                                        text: row.tile !== null ? row.tile.displayName : ""
                                        color: AppColors.text
                                        Accessible.ignored: true
                                    }

                                    QQC2.Label {
                                        text: row.tile !== null ? row.tile.stateText : ""
                                        color: row.tile !== null && row.tile.live ? AppColors.liveText : AppColors.muted
                                        font: AppFonts.small
                                        Accessible.ignored: true
                                    }
                                }

                                ElidedLabel {
                                    Layout.fillWidth: true
                                    text: row.tile !== null ? row.tile.connectionText : ""
                                    color: AppColors.muted
                                    font: AppFonts.small
                                    Accessible.ignored: true
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                onPressed: mouse => {
                                    row.forceActiveFocus(Qt.MouseFocusReason);
                                    if (mouse.button === Qt.RightButton) {
                                        list.canvas.tileMenu.popup();
                                    }
                                }
                                onDoubleClicked: list.canvas.activateBlock(row.blockId)
                            }

                            Keys.onPressed: event => {
                                if (list.canvas.keyWiring || list.canvas.keyUnwiring || event.modifiers !== Qt.NoModifier) {
                                    return;
                                }
                                switch (event.key) {
                                case Qt.Key_Down:
                                    list.moveFrom(row.blockId, 1);
                                    break;
                                case Qt.Key_Up:
                                    list.moveFrom(row.blockId, -1);
                                    break;
                                case Qt.Key_Home:
                                    list.moveFrom(row.blockId, Number.NEGATIVE_INFINITY);
                                    break;
                                case Qt.Key_End:
                                    list.moveFrom(row.blockId, Number.POSITIVE_INFINITY);
                                    break;
                                case Qt.Key_Return:
                                case Qt.Key_Enter:
                                    list.canvas.activateBlock(row.blockId);
                                    break;
                                case Qt.Key_Menu:
                                    list.canvas.selectOnly(row.blockId);
                                    list.canvas.tileMenu.popup(row, 0, row.height);
                                    break;
                                case Qt.Key_Delete: {
                                    // **The keyboard goes to the next row**,
                                    // not nowhere: the block it was on has gone.
                                    const ids = list.order();
                                    const index = ids.indexOf(row.blockId);
                                    const after = index + 1 < ids.length ? ids[index + 1]
                                        : index > 0 ? ids[index - 1] : "";
                                    list.canvas.selectOnly(row.blockId);
                                    list.canvas.removeSelected();
                                    if (after !== "") {
                                        Qt.callLater(() => list.focusRow(after));
                                    } else {
                                        list.canvas.forceActiveFocus(Qt.OtherFocusReason);
                                    }
                                    break;
                                }
                                default:
                                    return;
                                }
                                event.accepted = true;
                            }
                        }
                    }
                }
            }
        }
    }
}
