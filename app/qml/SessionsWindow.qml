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

// **Every saved session in one table**: what each captures, how many
// recordings it has made, how loud it is now and since when it runs, with
// Run and Stop for any number of them at once.
//
// **A window, not a tab or a dialog**: it is about all the sessions, and
// somebody starting three of them for an evening wants it beside the
// session window rather than over it. It stays above the main window, which
// it belongs to, as the Virtual Devices window does.
//
// **The columns are the person's**: shown or hidden from the Columns menu,
// dragged into another order by their headers, wider or narrower by the
// edge of a header, and sorted by clicking one. All of it is kept in the
// state file, not in any session.
QQC2.ApplicationWindow {
    id: root

    Kirigami.Theme.colorSet: Kirigami.Theme.Window
    Kirigami.Theme.inherit: false
    color: root.ownTitleBar ? "transparent" : AppColors.window

    /// What a session made here is called: the main window's words.
    property string untitledName: ""

    /// Whether the window draws its own title bar: the main window's answer.
    readonly property bool ownTitleBar: Appearance.ownStyle
    /// Where the window's size is kept in the state file.
    readonly property string stateName: "Sessions"

    /// The parts, for the tests.
    readonly property alias titleBar: titleBar
    readonly property alias chrome: chrome
    readonly property alias overview: overview
    readonly property alias table: table
    readonly property alias columnHeads: columnHeads
    readonly property alias placeholder: placeholder
    readonly property alias newButton: newButton
    readonly property alias runButton: runButton
    readonly property alias stopButton: stopButton
    readonly property alias deleteButton: deleteButton
    readonly property alias columnsMenu: columnsMenu
    readonly property alias deleteDialog: deleteDialog

    /// The sessions picked in the table, by id, in the order they were
    /// picked. **By id, not by row**: a row moves when the table is sorted
    /// or a session starts, and what somebody picked must not move with it.
    property list<string> selectedIds: []

    /// Counts up whenever a row changes. **What the buttons below read
    /// through**: rowAt() is a call, and a binding only follows properties,
    /// so without this Run would stay off after the session it was waiting
    /// for had stopped.
    property int rowsRevision: 0

    /// Which of the picked ones can be run, and which stopped.
    readonly property list<string> runnable: root.rowsRevision >= 0 && root.selectedIds.filter(id => {
        const row = overview.rowAt(overview.rowOf(id));
        return row.state !== undefined && row.state !== "running" && row.state !== "starting";
    })
    readonly property list<string> stoppable: root.rowsRevision >= 0 && root.selectedIds.filter(id => {
        const row = overview.rowAt(overview.rowOf(id));
        return row.state === "running" || row.state === "starting";
    })

    /// Picks \a sessionId, the way a click does: alone, or added to (or
    /// taken out of) what is picked when Ctrl is held.
    function select(sessionId: string, add: bool) {
        if (!add) {
            root.selectedIds = [sessionId];
            return;
        }
        const picked = root.selectedIds.slice();
        const at = picked.indexOf(sessionId);
        if (at >= 0) {
            picked.splice(at, 1);
        } else {
            picked.push(sessionId);
        }
        root.selectedIds = picked;
    }

    /// Picks every session from the last one picked to \a sessionId, the way
    /// Shift and a click do.
    function selectRange(sessionId: string) {
        const last = root.selectedIds.length > 0 ? root.selectedIds[root.selectedIds.length - 1] : sessionId;
        const from = overview.rowOf(last);
        const to = overview.rowOf(sessionId);
        const picked = [];
        for (let row = Math.min(from, to); row <= Math.max(from, to); ++row) {
            picked.push(overview.idAt(row));
        }
        root.selectedIds = picked;
    }

    /// What a column is called at its head.
    function columnTitle(key: string): string {
        switch (key) {
        case "name":
            return KI18n.i18nc("@title:column a session's name", "Name");
        case "sources":
            return KI18n.i18nc("@title:column where a session's sound comes from", "Sources");
        case "recordings":
            return KI18n.i18nc("@title:column how many recordings a session made", "Recordings");
        case "level":
            return KI18n.i18nc("@title:column how loud a session is now", "Level");
        case "status":
            return KI18n.i18nc("@title:column whether and since when a session runs", "Status");
        case "autoRun":
            return KI18n.i18nc("@title:column whether a session starts at login", "Auto Run");
        case "run":
            return KI18n.i18nc("@title:column the Run or Stop button of a session", "Run");
        }
        return key;
    }

    /// How wide a column is when nobody has made it otherwise.
    function naturalWidth(key: string): real {
        switch (key) {
        case "name":
            return Kirigami.Units.gridUnit * 12;
        case "sources":
            return Kirigami.Units.gridUnit * 6;
        case "recordings":
            return Kirigami.Units.gridUnit * 6;
        case "level":
            return Kirigami.Units.gridUnit * 6;
        case "status":
            return Kirigami.Units.gridUnit * 9;
        case "autoRun":
            return Kirigami.Units.gridUnit * 6;
        case "run":
            return Kirigami.Units.gridUnit * 5;
        }
        return Kirigami.Units.gridUnit * 6;
    }

    function widthOf(key: string): real {
        const remembered = overview.columnWidth(key);
        return remembered > 0 ? remembered : root.naturalWidth(key);
    }

    /// What the Status column says of a session.
    function statusText(state: string, startedAt: var): string {
        switch (state) {
        case "running":
            return startedAt && !isNaN(startedAt.getTime())
                ? KI18n.i18nc("@info %1 is the time of day a session started", "Started at %1",
                              Qt.formatTime(startedAt, Qt.locale().timeFormat(Locale.ShortFormat)))
                : KI18n.i18nc("@info a session's status", "Running");
        case "starting":
            return KI18n.i18nc("@info a session's status", "Starting…");
        case "stopping":
            return KI18n.i18nc("@info a session's status", "Stopping…");
        case "error":
            return KI18n.i18nc("@info a session's status: it could not run", "Could not run");
        }
        return KI18n.i18nc("@info a session's status: not running", "Inactive");
    }

    /// Writes the window's size down, as the other windows do.
    function saveSize() {
        if (root.visibility !== Window.Maximized && root.visibility !== Window.FullScreen) {
            WindowState.saveWindowSize(root.stateName, root.width, root.height);
        }
    }

    title: KI18n.i18nc("@title:window", "Sessions")
    flags: root.ownTitleBar ? (Qt.Window | Qt.FramelessWindowHint) : Qt.Window

    width: WindowState.windowSize(root.stateName).width > 0
        ? WindowState.windowSize(root.stateName).width : Kirigami.Units.gridUnit * 48
    height: WindowState.windowSize(root.stateName).height > 0
        ? WindowState.windowSize(root.stateName).height : Kirigami.Units.gridUnit * 24
    minimumWidth: Kirigami.Units.gridUnit * 28
    minimumHeight: Kirigami.Units.gridUnit * 14

    onClosing: root.saveSize()

    SessionOverview {
        id: overview

        // **Levels are read only while somebody can see them.**
        watching: root.visible
        onColumnsChanged: table.forceLayout()
        onCountChanged: {
            // A session deleted anywhere is no longer picked.
            root.selectedIds = root.selectedIds.filter(id => overview.rowOf(id) >= 0);
        }
        onDataChanged: ++root.rowsRevision
        onModelReset: ++root.rowsRevision
        onLayoutChanged: ++root.rowsRevision
    }

    menuBar: WindowTitleBar {
        id: titleBar

        visible: root.ownTitleBar
        titleBar: root.ownTitleBar
        frameRadius: chrome.frameRadius
        maximized: chrome.edgeToEdge
        onWindowMenuRequested: chrome.showWindowMenu()

        QQC2.Label {
            text: root.title
            color: AppColors.text
            font: AppFonts.strong
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // **What can be done to the sessions picked**, and a new one.
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: AppMetrics.panelPadding
            spacing: AppMetrics.mediumSpacing

            QQC2.Button {
                id: newButton

                text: KI18n.i18nc("@action:button", "New Session")
                icon.name: Appearance.followsSystem ? "document-new" : ""
                icon.source: Appearance.followsSystem ? "" : AppGlyphs.add
                onClicked: Sessions.create(root.untitledName)
            }

            QQC2.Button {
                id: runButton

                text: root.runnable.length > 1
                    ? KI18n.i18ncp("@action:button run the picked sessions", "Run %1", "Run %1", root.runnable.length)
                    : KI18n.i18nc("@action:button run the picked session", "Run")
                icon.name: Appearance.followsSystem ? "media-playback-start" : ""
                icon.source: Appearance.followsSystem ? "" : AppGlyphs.run
                enabled: root.runnable.length > 0
                onClicked: overview.run(root.runnable)
            }

            QQC2.Button {
                id: stopButton

                text: root.stoppable.length > 1
                    ? KI18n.i18ncp("@action:button stop the picked sessions", "Stop %1", "Stop %1", root.stoppable.length)
                    : KI18n.i18nc("@action:button stop the picked session", "Stop")
                icon.name: Appearance.followsSystem ? "media-playback-stop" : ""
                icon.source: Appearance.followsSystem ? "" : AppGlyphs.stop
                enabled: root.stoppable.length > 0
                onClicked: overview.stop(root.stoppable)
            }

            QQC2.Button {
                id: deleteButton

                text: KI18n.i18nc("@action:button", "Delete…")
                icon.name: "edit-delete"
                // **One at a time**: a deletion that cannot be undone is
                // asked about by name, and a list of names is a question
                // nobody reads.
                enabled: root.selectedIds.length === 1
                onClicked: {
                    deleteDialog.sessionId = root.selectedIds[0];
                    deleteDialog.open();
                }
            }

            Item {
                Layout.fillWidth: true
            }

            QQC2.Button {
                id: columnsButton

                text: KI18n.i18nc("@action:button choose the table's columns", "Columns")
                icon.name: "view-list-details"
                onClicked: columnsMenu.popup(columnsButton, 0, columnsButton.height)
                Accessible.name: KI18n.i18nc("@action:button", "Choose which columns to show")
            }
        }

        // **The heads of the columns**: a click sorts by one, dragging one
        // moves it, and its right edge makes it wider or narrower.
        QQC2.HorizontalHeaderView {
            id: columnHeads

            Layout.fillWidth: true
            Layout.leftMargin: AppMetrics.panelPadding
            Layout.rightMargin: AppMetrics.panelPadding
            syncView: table
            clip: true

            delegate: Rectangle {
                id: head

                required property int column
                readonly property string key: overview.columns[head.column] ?? ""

                implicitHeight: Kirigami.Units.gridUnit * 1.6
                implicitWidth: root.widthOf(head.key)
                color: headTap.pressed ? AppColors.surface2 : AppColors.surface
                Accessible.role: Accessible.ColumnHeader
                Accessible.name: root.columnTitle(head.key)

                QQC2.Label {
                    anchors.fill: parent
                    anchors.leftMargin: AppMetrics.mediumSpacing
                    anchors.rightMargin: AppMetrics.largeSpacing
                    verticalAlignment: Text.AlignVCenter
                    text: root.columnTitle(head.key)
                        + (overview.sortColumn === head.key ? (overview.sortDescending ? " ▾" : " ▴") : "")
                    color: AppColors.muted
                    font: AppFonts.small
                    elide: Text.ElideRight
                }

                TapHandler {
                    id: headTap

                    onTapped: overview.sortBy(head.key)
                }

                // Dragged sideways, the head moves its column to wherever it
                // is let go.
                DragHandler {
                    id: headDrag

                    target: null
                    xAxis.enabled: true
                    yAxis.enabled: false
                    onActiveChanged: {
                        if (headDrag.active) {
                            return;
                        }
                        const dropped = head.mapToItem(table, headDrag.centroid.position.x, 0).x + table.contentX;
                        const target = table.columnAtX(dropped, 0);
                        if (target >= 0 && target !== head.column) {
                            overview.moveColumn(head.column, target);
                        }
                    }
                }

                // The right edge: dragged, the column is as wide as the
                // pointer says, and kept once it is let go.
                Rectangle {
                    id: edge

                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: AppMetrics.smallSpacing
                    color: edgeDrag.active || edgeHover.hovered ? AppColors.border : "transparent"

                    HoverHandler {
                        id: edgeHover

                        cursorShape: Qt.SplitHCursor
                    }

                    DragHandler {
                        id: edgeDrag

                        property real startWidth: 0

                        target: null
                        yAxis.enabled: false
                        onActiveChanged: {
                            if (edgeDrag.active) {
                                edgeDrag.startWidth = head.width;
                            } else {
                                overview.setColumnWidth(head.key, Math.round(head.width));
                            }
                        }
                        onTranslationChanged: {
                            const width = Math.max(Kirigami.Units.gridUnit * 3,
                                                   edgeDrag.startWidth + edgeDrag.translation.x);
                            table.setColumnWidth(head.column, width);
                        }
                    }
                }
            }
        }

        QQC2.ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: AppMetrics.panelPadding
            Layout.rightMargin: AppMetrics.panelPadding
            Layout.bottomMargin: AppMetrics.panelPadding

            TableView {
                id: table

                model: overview
                clip: true
                columnSpacing: 0
                rowSpacing: 0
                boundsBehavior: Flickable.StopAtBounds
                columnWidthProvider: column => root.widthOf(overview.columns[column] ?? "")

                delegate: Rectangle {
                    id: cell

                    required property int row
                    required property string columnKey
                    required property string sessionId
                    required property string name
                    required property string sessionState
                    required property bool running
                    required property var sources
                    required property int recordings
                    required property var startedAt
                    required property real levelDb
                    required property bool autoRun

                    readonly property bool picked: root.selectedIds.indexOf(cell.sessionId) >= 0

                    implicitHeight: Kirigami.Units.gridUnit * 2
                    color: cell.picked ? AppColors.accentSoft : (cell.row % 2 === 0 ? AppColors.surface : AppColors.surface2)

                    TapHandler {
                        acceptedModifiers: Qt.KeyboardModifierMask
                        onTapped: (point, button) => {
                            if (point.modifiers & Qt.ShiftModifier) {
                                root.selectRange(cell.sessionId);
                            } else {
                                root.select(cell.sessionId, (point.modifiers & Qt.ControlModifier) !== 0);
                            }
                        }
                        // **A session opened in the main window by a double
                        // click**, where it can be changed.
                        onDoubleTapped: Sessions.openSession(cell.sessionId)
                    }

                    // name
                    QQC2.Label {
                        visible: cell.columnKey === "name"
                        anchors.fill: parent
                        anchors.leftMargin: AppMetrics.mediumSpacing
                        anchors.rightMargin: AppMetrics.mediumSpacing
                        verticalAlignment: Text.AlignVCenter
                        text: cell.name
                        color: AppColors.text
                        font: cell.running ? AppFonts.strong : AppFonts.body
                        elide: Text.ElideRight
                    }

                    // sources, as the blocks' own icons
                    Row {
                        visible: cell.columnKey === "sources"
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: AppMetrics.mediumSpacing
                        spacing: AppMetrics.smallSpacing

                        Repeater {
                            model: cell.columnKey === "sources" ? cell.sources : []

                            Kirigami.Icon {
                                id: sourceIcon

                                required property var modelData

                                width: Kirigami.Units.iconSizes.small
                                height: Kirigami.Units.iconSizes.small
                                source: BlockPresentation.icon(sourceIcon.modelData.type)
                                color: AppColors.text
                                isMask: true

                                QQC2.ToolTip.visible: sourceHover.hovered
                                QQC2.ToolTip.text: sourceIcon.modelData.name !== ""
                                    ? sourceIcon.modelData.name : BlockPresentation.name(sourceIcon.modelData.type)

                                HoverHandler {
                                    id: sourceHover
                                }
                            }
                        }
                    }

                    // recordings
                    QQC2.Label {
                        visible: cell.columnKey === "recordings"
                        anchors.fill: parent
                        anchors.leftMargin: AppMetrics.mediumSpacing
                        verticalAlignment: Text.AlignVCenter
                        text: cell.recordings > 0
                            ? KI18n.i18ncp("@info how many recordings a session made", "%1 recording", "%1 recordings",
                                           cell.recordings)
                            : KI18n.i18nc("@info a session has made no recordings", "None")
                        color: cell.recordings > 0 ? AppColors.text : AppColors.muted
                        font: AppFonts.small
                    }

                    // level: a bar from -60 dBFS to full scale
                    Rectangle {
                        visible: cell.columnKey === "level"
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: AppMetrics.mediumSpacing
                        anchors.rightMargin: AppMetrics.mediumSpacing
                        height: Kirigami.Units.smallSpacing * 2
                        radius: AppMetrics.barRadius
                        color: AppColors.well
                        Accessible.role: Accessible.ProgressBar
                        Accessible.name: KI18n.i18nc("@info:tooltip", "Level")

                        Rectangle {
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            radius: AppMetrics.barRadius
                            width: parent.width * Math.max(0, Math.min(1, (cell.levelDb + 60) / 60))
                            color: cell.levelDb >= -0.1 ? AppColors.dangerMark : AppColors.liveMark
                            visible: cell.running
                        }
                    }

                    // status
                    QQC2.Label {
                        visible: cell.columnKey === "status"
                        anchors.fill: parent
                        anchors.leftMargin: AppMetrics.mediumSpacing
                        verticalAlignment: Text.AlignVCenter
                        text: root.statusText(cell.sessionState, cell.startedAt)
                        color: cell.running ? AppColors.text : (cell.sessionState === "error" ? AppColors.dangerText : AppColors.muted)
                        font: AppFonts.small
                        elide: Text.ElideRight
                    }

                    // Auto Run: the session's own setting, one step to
                    // undo in it; one only on disk is opened to be set.
                    QQC2.CheckBox {
                        objectName: "autoRunCheck"
                        visible: cell.columnKey === "autoRun"
                        anchors.centerIn: parent
                        checked: cell.autoRun
                        Accessible.name: KI18n.i18nc("@option:check %1 is a session", "Run %1 when I log in", cell.name)
                        QQC2.ToolTip.visible: hovered
                        QQC2.ToolTip.text: KI18n.i18nc("@info:tooltip", "Run when I log in")
                        onToggled: overview.setAutoRun([cell.sessionId], checked)
                    }

                    // run / stop
                    QQC2.Button {
                        visible: cell.columnKey === "run"
                        anchors.centerIn: parent
                        readonly property bool stops: cell.running || cell.sessionState === "starting"
                        text: stops ? KI18n.i18nc("@action:button stop the session", "Stop")
                                    : KI18n.i18nc("@action:button start the session", "Run")
                        icon.name: Appearance.followsSystem ? (stops ? "media-playback-stop" : "media-playback-start") : ""
                        icon.source: Appearance.followsSystem ? "" : (stops ? AppGlyphs.stop : AppGlyphs.run)
                        display: QQC2.AbstractButton.IconOnly
                        enabled: cell.sessionState !== "stopping"
                        Accessible.name: stops ? KI18n.i18nc("@action:button %1 is a session", "Stop %1", cell.name)
                                               : KI18n.i18nc("@action:button %1 is a session", "Run %1", cell.name)
                        QQC2.ToolTip.visible: hovered
                        QQC2.ToolTip.text: text
                        onClicked: stops ? overview.stop([cell.sessionId]) : overview.run([cell.sessionId])
                    }
                }
            }
        }
    }

    PlaceholderMessage {
        id: placeholder

        anchors.centerIn: parent
        width: parent.width - AppMetrics.wideSpacing * 2
        visible: overview.ready && overview.count === 0
        text: KI18n.i18nc("@info", "There are no saved sessions yet. A new session appears here as soon as it's made.")
    }

    // **Which columns show**, one entry each; the name always does.
    QQC2.Menu {
        id: columnsMenu

        Instantiator {
            model: overview.allColumns()

            delegate: QQC2.MenuItem {
                id: columnItem

                required property string modelData

                text: root.columnTitle(columnItem.modelData)
                checkable: true
                checked: overview.columns.indexOf(columnItem.modelData) >= 0
                enabled: columnItem.modelData !== "name"
                // **From what the table says, not from the item's tick**,
                // which a keyboard or an accessibility tool may not have
                // turned by the time this is heard.
                onTriggered: overview.setColumnShown(columnItem.modelData, !overview.isColumnShown(columnItem.modelData))
            }

            onObjectAdded: (index, object) => columnsMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => columnsMenu.removeItem(object)
        }
    }

    AppPromptDialog {
        id: deleteDialog

        property string sessionId: ""

        title: KI18n.i18nc("@title:window", "Delete Session")
        subtitle: KI18n.i18nc("@info", "Delete “%1”? This cannot be undone.",
                              overview.rowAt(overview.rowOf(deleteDialog.sessionId)).name ?? "")
        standardButtons: Kirigami.Dialog.Cancel
        customFooterActions: [
            Kirigami.Action {
                text: KI18n.i18nc("@action:button", "Delete")
                icon.name: "edit-delete"
                onTriggered: {
                    Sessions.deleteSession(deleteDialog.sessionId);
                    deleteDialog.close();
                }
            }
        ]
    }

    WindowChrome {
        id: chrome

        window: root
        active: root.ownTitleBar
    }
}
