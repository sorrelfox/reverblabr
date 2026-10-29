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

// The window every other part of the interface hangs in.
//
// **Who draws the title bar depends on the appearance** (ADR-0012). With
// *Follow system* KWin does, in the person's own decoration theme, and the
// design's title-bar content — the session tabs, the clock, Run/Stop — is
// the toolbar row directly under it. In the Reverblabr appearances the
// window has no frame and that row is its title bar, as the
// prototype draws it: the compositor still moves, resizes, snaps and tiles
// it and draws its shadow, and the menus sit behind a button in the row.
//
// **A plain QQC2.ApplicationWindow, themed by Kirigami.** Kirigami's own
// application windows are built around a page stack and a global drawer,
// and a canvas with a sidebar beside it is neither: pushing and popping
// would be a gesture nothing here means. They also leave no room for a menu
// bar — the content starts under it — and the menus are what a KDE
// application is expected to have. Kirigami's theming, units and controls
// are attached properties and work in any window.
QQC2.ApplicationWindow {
    id: root

    // The window colour set, so that everything inside it starts from the
    // scheme's window colours rather than from a view's.
    Kirigami.Theme.colorSet: Kirigami.Theme.Window
    Kirigami.Theme.inherit: false
    // **Transparent round the rounded corners** of a window with no frame;
    // the background below draws the window's own shape.
    color: root.ownTitleBar ? "transparent" : AppColors.window

    /// Whether the window draws its own title bar: in the Reverblabr
    /// appearances, whose control style is chosen at start, so this is fixed
    /// for the window's life too.
    readonly property bool ownTitleBar: Appearance.ownStyle
    /// Drawn as a maximised window is — square, with no border and no
    /// shadow, as the prototype draws one. The frame's, and writable, so the
    /// design comparison can ask for the look it compares against.
    property alias edgeToEdge: chrome.edgeToEdge
    /// The window's corner: the prototype's 8 pixels while it has a frame of
    /// its own drawing, none while it is maximised or KWin frames it.
    readonly property real frameRadius: chrome.frameRadius
    /// Whether the classic menu bar is up (Ctrl+M).
    property bool menuBarShown: WindowState.menuBarShown(root.ownTitleBar)
    /// The menus, in the menu bar, and the same menus behind the header's
    /// menu button.
    readonly property alias menus: classicMenuBar
    readonly property alias mainMenu: mainMenu
    /// The header row, and the title bar's own parts, for the tests.
    readonly property alias toolBar: toolBar
    readonly property alias windowFrame: chrome.windowFrame
    readonly property alias resizeGrips: chrome.resizeGrips
    readonly property alias frameBorder: chrome.frameBorder
    readonly property alias windowMenu: chrome.windowMenu
    readonly property alias showMenuBarAction: showMenuBarAction

    // **No frame of KWin's in the Reverblabr appearances**: the header is the
    // title bar. Fixed at start with the control style, for the same reason.
    flags: root.ownTitleBar ? (Qt.Window | Qt.FramelessWindowHint) : Qt.Window

    /// The window menu: the compositor's own where it can be asked for — it
    /// alone can keep a window above the others or move it to another
    /// desktop — and a smaller one of ours where it cannot.
    function showWindowMenu() {
        chrome.showWindowMenu();
    }

    /// What a session nobody has named is called. **One place for it**,
    /// because the launch that makes the first one and the New Session that
    /// makes the next must not disagree.
    readonly property string untitledName: KI18n.i18nc("@title the name a new session is given", "Untitled")

    /// Which sidebar tab is showing. Held here rather than in the sidebar
    /// so that the menu items and the tab bar are the same thing.
    property alias sidebarIndex: sidebar.currentIndex
    /// The sidebar and the canvas, for whatever fills them.
    property alias sidebar: sidebar
    property alias canvas: canvas
    /// One block's settings.
    ///
    /// **The canvas's, not the window's**. A block's settings open
    /// beside that block rather than in a pane at the side of the window, so
    /// the panel lives on the canvas; this is how the window and a test
    /// still reach it.
    readonly property alias settingsPanel: sessionCanvas.settings
    /// The strip across the top of the canvas that says what has gone
    /// wrong.
    property alias errorBanner: errorBanner
    /// The session's own tab in the sidebar.
    property alias infoPane: infoPane
    /// The status bar, for a test that reads it.
    readonly property alias statusBar: statusBar
    /// The Files tab.
    property alias filesPane: filesPane
    property alias schedulePane: schedulePane

    /// **The Recording Inspectors that are open**, one a file, by the
    /// window: several can be, and opening a file that has one brings that
    /// one forward rather than making a second.
    property list<RecordingInspectorWindow> inspectors

    function openInspector(path: string): var {
        if (path === "") {
            return null;
        }
        for (const window of root.inspectors) {
            if (window.path === path) {
                window.show();
                window.raise();
                window.requestActivate();
                return window;
            }
        }
        const window = inspectorComponent.createObject(null, { path: path }) as RecordingInspectorWindow;
        root.inspectors = root.inspectors.concat([window]);
        window.show();
        return window;
    }

    Component {
        id: inspectorComponent

        RecordingInspectorWindow {
            id: inspectorWindow

            onClosing: {
                root.inspectors = root.inspectors.filter(window => window !== inspectorWindow);
                inspectorWindow.destroy();
            }
        }
    }
    /// The canvas inside it, for the zoom actions and for a test.
    property alias sessionCanvas: sessionCanvas
    /// The block library in the sidebar's first tab.
    property alias blockLibrary: blockLibrary
    /// The session actions, so a test presses what a menu item presses
    /// rather than a copy of it.
    readonly property alias newSessionAction: newSessionAction
    readonly property alias automaticConnectionsAction: automaticConnectionsAction
    readonly property alias openSessionAction: openSessionAction
    readonly property alias closeSessionAction: closeSessionAction
    readonly property alias renameSessionAction: renameAction
    readonly property alias deleteSessionAction: deleteSessionAction
    /// The dialogs those actions put up, each null until one has been
    /// wanted: a test asks the loader for the dialog it built.
    readonly property alias openSessionDialog: openSessionDialog
    readonly property alias renameSessionDialog: renameSessionDialog
    readonly property alias deleteSessionDialog: deleteSessionDialog
    /// The tab bar, so a test can press a tab rather than call the thing a
    /// tab calls.
    readonly property alias sessionTabs: sessionTabs
    readonly property alias exportSessionAction: exportSessionAction
    readonly property alias importSessionAction: importSessionAction
    readonly property alias undoAction: undoAction
    readonly property alias redoAction: redoAction
    /// The file dialogs, for a test that has no business opening one: it
    /// asks them what they would be called and hands the path over itself.
    readonly property alias exportDialog: exportDialog
    readonly property alias importDialog: importDialog
    /// The three appearances, as the Settings menu offers them.
    readonly property alias darkAppearanceAction: darkAppearanceAction
    readonly property alias lightAppearanceAction: lightAppearanceAction
    readonly property alias systemAppearanceAction: systemAppearanceAction
    /// The Virtual Devices window: its action, and the window itself
    /// once it has been opened — null until then.
    readonly property alias virtualDevicesAction: virtualDevicesAction
    readonly property VirtualDevicesWindow virtualDevicesWindow: virtualDevicesLoader.item as VirtualDevicesWindow
    /// The Sessions window: its action, and the window once it has
    /// been opened — null until then.
    readonly property alias sessionsWindowAction: sessionsWindowAction
    readonly property SessionsWindow sessionsWindow: sessionsWindowLoader.item as SessionsWindow
    /// The system tray's popover, once the icon has been clicked —
    /// null until then.
    readonly property TrayPopover trayPopover: trayPopoverLoader.item as TrayPopover

    title: KI18n.i18nc("@title:window", "Reverblabr")

    // **A plain application window is not visible until it is told to be.**
    // Kirigami's own window types set this for you, so switching away from
    // one is exactly where it goes missing — and an application that starts
    // with no window at all is not something a test that creates its own
    // window would ever notice.
    visible: true

    // **Which session the window shows is Sessions' answer**: the tab
    // that is in front. The shell watches its state, the model follows its
    // contents and the meters read its levels, and none of them has to be
    // told twice.
    Binding {
        target: AppShell
        property: "sessionId"
        value: Sessions.currentId
    }

    Binding {
        target: SessionModel
        property: "sessionId"
        value: AppShell.sessionId
    }

    // What went wrong in the last session is not shown against this one.
    Binding {
        target: ErrorSurface
        property: "sessionId"
        value: AppShell.sessionId
    }

    // The meters read a pointer rather than a property, so they are told
    // which session to read rather than bound to it.
    Connections {
        target: AppShell

        function onSessionChanged() {
            Meters.watch(AppShell.sessionId);
        }
    }

    Component.onCompleted: {
        Meters.watch(AppShell.sessionId);
        // **The launch is a resumption.** Whatever was open comes back,
        // stopped, and in the order it sat in; with nothing remembered this
        // makes one, because the window is never empty. The name is the
        // interface's word, which is why the engine is not the one choosing
        // it.
        Sessions.restore(root.untitledName);
    }


    width: WindowState.windowWidth > 0 ? WindowState.windowWidth : Kirigami.Units.gridUnit * 60
    height: WindowState.windowHeight > 0 ? WindowState.windowHeight : Kirigami.Units.gridUnit * 38
    minimumWidth: Kirigami.Units.gridUnit * 30
    minimumHeight: Kirigami.Units.gridUnit * 20

    // **One handler, because a second would replace it.** QML takes the
    // last `onClosing` written and forgets the others, which is exactly the
    // sort of thing that works until somebody adds one.
    onClosing: {
        WindowState.save(root.width, root.height, sidebar.width);
        // The Virtual Devices window goes with this one, without a close of
        // its own to write its size down.
        if (root.virtualDevicesWindow !== null && root.virtualDevicesWindow.visible) {
            root.virtualDevicesWindow.saveSize();
        }
        // And so does the Sessions window.
        if (root.sessionsWindow !== null && root.sessionsWindow.visible) {
            root.sessionsWindow.saveSize();
        }
        // **With a tray, closing is hiding**: the application goes on
        // in the tray, so the windows that belong to this one are closed
        // here rather than by the application ending.
        if (TrayState.available) {
            if (root.virtualDevicesWindow !== null) {
                root.virtualDevicesWindow.close();
            }
            if (root.sessionsWindow !== null) {
                root.sessionsWindow.close();
            }
        }
        // The Recording Inspectors go with it too: they are windows of
        // this one, not applications of their own.
        // A copy, since each one takes itself out of the list as it closes.
        const inspectors = Array.from(root.inspectors);
        for (const inspector of inspectors) {
            inspector.saveSize();
            inspector.close();
        }
        // Which sessions were open is written down as it changes too, so a
        // crash still resumes to something recent; this is the ordinary
        // close.
        Sessions.remember();
    }

    // --- what the window can do, in one place ---
    //
    // A menu item and the button beside it are the same action, so they
    // cannot disagree about whether it is enabled or what it is called.

    QQC2.Action {
        id: runAction

        text: AppShell.running
            ? KI18n.i18nc("@action stop the session", "Stop")
            : KI18n.i18nc("@action start the session", "Run")
        icon.name: AppShell.running ? "media-playback-stop" : "media-playback-start"
        enabled: AppShell.hasSession && !AppShell.busy
        shortcut: "Space"
        onTriggered: AppShell.toggleRun()
    }

    QQC2.Action {
        id: automaticConnectionsAction

        text: KI18n.i18nc("@action:inmenu wire new blocks up as they are added", "Automatic Connections")
        checkable: true
        // **The session's own setting**, not the window's: it travels with
        // the file, so the menu item and the Info tab are two ways to the
        // same switch rather than two switches. The canvas acts on it:
        // with it on, a block added beside another is wired from it
        // and a block taken out of the middle of a lane leaves the lane
        // joined up behind it.
        checked: SessionModel.autoConnect
        enabled: AppShell.hasSession
        onTriggered: SessionModel.setAutoConnect(checked)
    }

    QQC2.Action {
        id: newSessionAction

        text: KI18n.i18nc("@action:inmenu", "&New Session")
        icon.name: "document-new"
        shortcut: StandardKey.New
        onTriggered: Sessions.create(root.untitledName)
    }

    QQC2.Action {
        id: openSessionAction

        text: KI18n.i18nc("@action:inmenu", "&Open Session…")
        icon.name: "document-open"
        // **A literal sequence, not StandardKey.Open.** The menu showed
        // Ctrl+O and the key did nothing; New and Close, written the same
        // way, worked. Spelled out, it works.
        shortcut: "Ctrl+O"
        onTriggered: openSessionDialog.open()
    }

    // **Every saved session in a table of its own**, where several can
    // be started and stopped at once. One window: asked for again, it comes
    // to the front.
    QQC2.Action {
        id: sessionsWindowAction

        text: KI18n.i18nc("@action:inmenu open the window listing every saved session", "&All Sessions…")
        icon.name: "view-list-details"
        onTriggered: root.showSessionsWindow()
    }

    /// Shows this window, or brings it to the front: what the tray's middle
    /// click and its Show Reverblabr do.
    function showWindow() {
        root.show();
        root.raise();
        root.requestActivate();
    }

    /// Opens the tray's popover, or closes it: a click on the icon.
    function toggleTrayPopover() {
        trayPopoverLoader.active = true;
        root.trayPopover.toggle();
    }

    // **The tray knows when it is the way back**: with this window closed
    // into it, its icon must not hide among the passive ones.
    Binding {
        target: TrayState
        property: "windowShown"
        value: root.visible
    }

    // **What the tray asks for**. The icon lives in main(); TrayState
    // carries its clicks here, which is also how a test makes them.
    Connections {
        target: TrayState

        function onPopoverRequested() {
            root.toggleTrayPopover();
        }

        function onWindowToggleRequested() {
            if (root.visible) {
                root.close();
            } else {
                root.showWindow();
            }
        }

        function onSessionsWindowRequested() {
            root.showSessionsWindow();
        }

        function onForwardRequested(sessionId: string, blockId: string) {
            root.showWindow();
            if (Sessions.isOpen(sessionId)) {
                Sessions.currentId = sessionId;
            }
        }

        // **A tray that goes away takes the way back with it**: a hidden
        // window comes back rather than being left out of reach.
        function onAvailableChanged() {
            if (!TrayState.available && !root.visible) {
                root.showWindow();
            }
        }
    }

    // **What a global shortcut asks for**. The registry lives in
    // main(); GlobalShortcuts carries its presses here, which is also how a
    // test makes them. Show Reverblabr brings this window forward; the block
    // shortcuts act on the session this window shows.
    readonly property ShortcutTargets shortcutTargets: ShortcutTargets {}

    Connections {
        target: GlobalShortcuts

        function onShowRequested() {
            root.showWindow();
        }

        function onBlockActionRequested(actionId: string) {
            root.shortcutTargets.act(actionId);
        }
    }

    // **What a launch asks for**: the desktop entry's New Session and
    // All Sessions, KRunner's Open, and launching Reverblabr again while it
    // runs, which hands its arguments to this one rather than opening a
    // second window.
    Connections {
        target: LaunchRequests

        function onShowRequested() {
            root.showWindow();
        }

        function onNewSessionRequested() {
            root.showWindow();
            newSessionAction.trigger();
        }

        function onSessionsWindowRequested() {
            root.showSessionsWindow();
        }

        function onOpenSessionRequested(sessionId: string) {
            root.showWindow();
            Sessions.openSession(sessionId);
        }
    }

    // **Quit from the tray ends the application with this window hidden**,
    // which closes nothing: what closing writes down is written here too.
    Connections {
        target: Qt.application

        function onAboutToQuit() {
            Sessions.remember();
        }
    }

    /// Opens the Sessions window, or brings it to the front. **Also what
    /// the tray calls**.
    function showSessionsWindow() {
        sessionsWindowLoader.active = true;
        const window = root.sessionsWindow;
        window.show();
        window.raise();
        window.requestActivate();
    }

    QQC2.Action {
        id: closeSessionAction

        text: KI18n.i18nc("@action:inmenu", "&Close Session")
        icon.name: "window-close"
        shortcut: StandardKey.Close
        // **The last session is not closed.** A window with no session has
        // nothing to show and no way back to one, so the way to be rid of
        // this one is to open or make another first.
        enabled: Sessions.open.length > 1
        onTriggered: Sessions.closeSession(Sessions.currentId)
    }

    QQC2.Action {
        id: renameAction

        text: KI18n.i18nc("@action:inmenu rename the session", "Rename Session…")
        icon.name: "edit-rename"
        shortcut: "F2"
        enabled: AppShell.hasSession
        // **F2 renames whatever is in front of the person pressing it.**
        // With a tile selected that is the block; with none it is the
        // session. One key for one idea beats two keys for one idea in two
        // places.
        onTriggered: if (!sessionCanvas.startRenaming()) {
            renameSessionDialog.open();
        }
    }

    QQC2.Action {
        id: exportSessionAction

        text: KI18n.i18nc("@action:inmenu", "&Export Session…")
        icon.name: "document-save-as"
        enabled: AppShell.hasSession
        onTriggered: exportDialog.open()
    }

    QQC2.Action {
        id: importSessionAction

        text: KI18n.i18nc("@action:inmenu", "&Import Session…")
        icon.name: "document-import"
        onTriggered: importDialog.open()
    }

    QQC2.Action {
        id: deleteSessionAction

        text: KI18n.i18nc("@action:inmenu", "&Delete Session…")
        icon.name: "edit-delete"
        enabled: AppShell.hasSession
        // **Asked first, always.** Deleting a session takes the file with
        // it and there is no undo that reaches across a file.
        onTriggered: deleteSessionDialog.open()
    }

    QQC2.Action {
        id: findAction

        text: KI18n.i18nc("@action:inmenu put the cursor in the block library's search field", "Find Block…")
        icon.name: "search"
        shortcut: StandardKey.Find
        // The library is in the sidebar's first tab, so finding a block
        // means showing that tab as well as typing in it: a cursor in a
        // field nobody can see is a key that appears to do nothing.
        onTriggered: {
            sidebar.currentIndex = 0;
            blockLibrary.focusSearch();
        }
    }

    // **The session showing's history, as the engine keeps it**.
    // The entry names the step it would take back — "Undo Move Block" — so
    // that nobody has to press it to find out. A text field with the focus
    // keeps Ctrl+Z for its own typing: it takes the shortcut before the
    // window does.
    QQC2.Action {
        id: undoAction

        text: SessionModel.undoText
        icon.name: "edit-undo"
        shortcut: StandardKey.Undo
        enabled: SessionModel.canUndo
        onTriggered: SessionModel.undo()
    }

    QQC2.Action {
        id: redoAction

        text: SessionModel.redoText
        icon.name: "edit-redo"
        shortcut: StandardKey.Redo
        enabled: SessionModel.canRedo
        onTriggered: SessionModel.redo()
    }

    QQC2.Action {
        id: zoomInAction

        text: KI18n.i18nc("@action:inmenu", "Zoom In")
        icon.name: "zoom-in"
        shortcut: StandardKey.ZoomIn
        onTriggered: sessionCanvas.zoomIn()
    }

    QQC2.Action {
        id: zoomOutAction

        text: KI18n.i18nc("@action:inmenu", "Zoom Out")
        icon.name: "zoom-out"
        shortcut: StandardKey.ZoomOut
        onTriggered: sessionCanvas.zoomOut()
    }

    QQC2.Action {
        id: actualSizeAction

        text: KI18n.i18nc("@action:inmenu show the canvas at 100 per cent", "Actual Size")
        icon.name: "zoom-original"
        shortcut: "Ctrl+0"
        onTriggered: sessionCanvas.actualSize()
    }

    QQC2.Action {
        id: fitAction

        text: KI18n.i18nc("@action:inmenu zoom out until the whole session is on screen", "Fit to Window")
        icon.name: "zoom-fit-best"
        shortcut: "Ctrl+9"
        onTriggered: sessionCanvas.fitToView()
    }

    // **The session as a list**, for somebody who cannot see the
    // canvas or cannot aim at it. The person's choice, kept between runs,
    // and the same switch as the Info tab's.
    QQC2.Action {
        id: showListAction

        text: KI18n.i18nc("@action:inmenu show the session's blocks as a list instead of on the canvas", "Show as &List")
        icon.name: "view-list-details"
        checkable: true
        checked: WindowState.listShown
        onTriggered: WindowState.listShown = showListAction.checked
    }

    QQC2.Action {
        id: showBlocksAction

        text: KI18n.i18nc("@action:inmenu show the blocks tab of the sidebar", "Blocks")
        shortcut: "Ctrl+1"
        onTriggered: sidebar.currentIndex = sidebar.blocksTab
    }

    QQC2.Action {
        id: showFilesAction

        text: KI18n.i18nc("@action:inmenu show the files tab of the sidebar", "Files")
        shortcut: "Ctrl+2"
        onTriggered: sidebar.currentIndex = sidebar.filesTab
    }

    QQC2.Action {
        id: showInfoAction

        text: KI18n.i18nc("@action:inmenu show the info tab of the sidebar", "Info")
        shortcut: "Ctrl+3"
        onTriggered: sidebar.currentIndex = sidebar.infoTab
    }

    QQC2.Action {
        id: showScheduleAction

        text: KI18n.i18nc("@action:inmenu show the schedule tab of the sidebar", "Schedule")
        shortcut: "Ctrl+4"
        onTriggered: sidebar.currentIndex = sidebar.scheduleTab
    }

    QQC2.Action {
        id: handbookAction

        text: KI18n.i18nc("@action:inmenu", "Handbook")
        icon.name: "help-contents"
        shortcut: StandardKey.HelpContents
        onTriggered: Qt.openUrlExternally(AppInfo.handbookUrl)
    }

    QQC2.Action {
        id: reportBugAction

        text: KI18n.i18nc("@action:inmenu", "Report Bug…")
        icon.name: "tools-report-bug"
        // **Greyed out rather than wrong.** This build has nowhere to send
        // a bug, and quietly sending somebody to the wrong tracker is worse
        // than a menu item they cannot press.
        enabled: AppInfo.bugReportUrl.toString().length > 0
        onTriggered: Qt.openUrlExternally(AppInfo.bugReportUrl)
    }

    // **Which look the window has** (ADR-0012): the design's own, dark
    // or light, or the desktop's. One choice of three, so the menu shows it
    // as one: a group whose checked item is the one in force. Colours and
    // fonts change as it is picked; nothing waits for a restart.
    QQC2.ActionGroup {
        id: appearanceGroup
    }

    QQC2.Action {
        id: darkAppearanceAction

        text: KI18n.i18nc("@action:inmenu an appearance", "Reverblabr &Dark")
        checkable: true
        checked: Appearance.mode === Appearance.ReverblabrDark
        QQC2.ActionGroup.group: appearanceGroup
        onTriggered: Appearance.mode = Appearance.ReverblabrDark
    }

    QQC2.Action {
        id: lightAppearanceAction

        text: KI18n.i18nc("@action:inmenu an appearance", "Reverblabr &Light")
        checkable: true
        checked: Appearance.mode === Appearance.ReverblabrLight
        QQC2.ActionGroup.group: appearanceGroup
        onTriggered: Appearance.mode = Appearance.ReverblabrLight
    }

    QQC2.Action {
        id: systemAppearanceAction

        // The desktop's colour scheme, font and spacing: for a person who
        // needs large text, high contrast or simply their own theme.
        text: KI18n.i18nc("@action:inmenu an appearance: the desktop's colours and fonts", "&Follow System")
        checkable: true
        checked: Appearance.mode === Appearance.FollowSystem
        QQC2.ActionGroup.group: appearanceGroup
        onTriggered: Appearance.mode = Appearance.FollowSystem
    }

    // **The virtual devices' window**, from the Settings menu and the title
    // bar's button alike. One window: asked for again, it comes to the front
    // rather than opening a second list of the same devices.
    QQC2.Action {
        id: virtualDevicesAction

        text: KI18n.i18nc("@action:inmenu open the window that manages virtual audio devices", "&Virtual Devices…")
        icon.name: "audio-card"
        onTriggered: root.showVirtualDevices()
    }

    function showVirtualDevices() {
        virtualDevicesLoader.active = true;
        const window = root.virtualDevicesWindow;
        window.show();
        window.raise();
        window.requestActivate();
    }

    QQC2.Action {
        id: aboutAction

        text: KI18n.i18nc("@action:inmenu", "About Reverblabr")
        icon.name: "help-about"
        onTriggered: aboutLoader.active = true
    }

    // **KDE's Ctrl+M**: the menu bar comes and goes, and while it is
    // gone its menus are behind the header's menu button. The choice is kept.
    QQC2.Action {
        id: showMenuBarAction

        text: KI18n.i18nc("@action:inmenu", "Show &Menubar")
        icon.name: "show-menu"
        shortcut: "Ctrl+M"
        checkable: true
        checked: root.menuBarShown
        onTriggered: {
            root.menuBarShown = showMenuBarAction.checked;
            WindowState.setMenuBarShown(root.ownTitleBar, root.menuBarShown);
        }
    }

    // **The title bar is the top row, and the menu bar comes under it** in
    // the Reverblabr appearances; KWin's title bar is above the window, so
    // with *Follow system* the menu bar is the top row, as it always was.
    // The window's two slots are filled once, at start, either way round.
    menuBar: root.ownTitleBar ? toolBar : classicMenuBar
    header: root.ownTitleBar ? classicMenuBar : toolBar

    // **The same menus behind the header's button**. The menu bar's
    // menus cannot be lent out — a menu belongs to one parent — so this is
    // a second set of the same actions, which a test holds to the first.
    QQC2.Menu {
        id: mainMenu

        QQC2.Menu {
            title: KI18n.i18nc("@title:menu", "&Session")

            QQC2.MenuItem {
                action: newSessionAction
            }

            QQC2.MenuItem {
                action: openSessionAction
            }

            QQC2.MenuItem {
                action: sessionsWindowAction
            }

            QQC2.MenuItem {
                action: closeSessionAction
            }

            QQC2.MenuSeparator {}

            QQC2.MenuItem {
                action: runAction
            }

            QQC2.MenuSeparator {}

            QQC2.MenuItem {
                action: automaticConnectionsAction
            }

            QQC2.MenuItem {
                action: renameAction
            }

            DestructiveMenuItem {
                action: deleteSessionAction
            }

            QQC2.MenuSeparator {}

            QQC2.MenuItem {
                action: exportSessionAction
            }

            QQC2.MenuItem {
                action: importSessionAction
            }
        }

        QQC2.Menu {
            title: KI18n.i18nc("@title:menu", "&Edit")

            QQC2.MenuItem {
                action: undoAction
            }

            QQC2.MenuItem {
                action: redoAction
            }

            QQC2.MenuSeparator {}

            QQC2.MenuItem {
                action: findAction
            }
        }

        QQC2.Menu {
            title: KI18n.i18nc("@title:menu", "&View")

            QQC2.MenuItem {
                action: zoomInAction
            }

            QQC2.MenuItem {
                action: zoomOutAction
            }

            QQC2.MenuItem {
                action: actualSizeAction
            }

            QQC2.MenuItem {
                action: fitAction
            }

            QQC2.MenuItem {
                action: showListAction
            }

            QQC2.MenuSeparator {}

            QQC2.MenuItem {
                action: showBlocksAction
            }

            QQC2.MenuItem {
                action: showFilesAction
            }

            QQC2.MenuItem {
                action: showInfoAction
            }

            QQC2.MenuItem {
                action: showScheduleAction
            }
        }

        QQC2.Menu {
            title: KI18n.i18nc("@title:menu", "Se&ttings")

            QQC2.Menu {
                title: KI18n.i18nc("@title:menu", "&Appearance")

                QQC2.MenuItem {
                    action: darkAppearanceAction
                }

                QQC2.MenuItem {
                    action: lightAppearanceAction
                }

                QQC2.MenuItem {
                    action: systemAppearanceAction
                }
            }

            QQC2.MenuItem {
                action: showMenuBarAction
            }

            QQC2.MenuSeparator {}

            QQC2.MenuItem {
                action: virtualDevicesAction
            }
        }

        QQC2.Menu {
            title: KI18n.i18nc("@title:menu", "&Help")

            QQC2.MenuItem {
                action: handbookAction
            }

            QQC2.MenuItem {
                action: reportBugAction
            }

            QQC2.MenuSeparator {}

            QQC2.MenuItem {
                action: aboutAction
            }
        }
    }

    QQC2.MenuBar {
        id: classicMenuBar

        visible: root.menuBarShown

        QQC2.Menu {
            title: KI18n.i18nc("@title:menu", "&Session")

            QQC2.MenuItem {
                action: newSessionAction
            }

            QQC2.MenuItem {
                action: openSessionAction
            }

            QQC2.MenuItem {
                action: sessionsWindowAction
            }

            QQC2.MenuItem {
                action: closeSessionAction
            }

            QQC2.MenuSeparator {}

            QQC2.MenuItem {
                action: runAction
            }

            QQC2.MenuSeparator {}

            QQC2.MenuItem {
                action: automaticConnectionsAction
            }

            QQC2.MenuItem {
                action: renameAction
            }

            DestructiveMenuItem {
                action: deleteSessionAction
            }

            QQC2.MenuSeparator {}

            QQC2.MenuItem {
                action: exportSessionAction
            }

            QQC2.MenuItem {
                action: importSessionAction
            }
        }

        QQC2.Menu {
            title: KI18n.i18nc("@title:menu", "&Edit")

            QQC2.MenuItem {
                action: undoAction
            }

            QQC2.MenuItem {
                action: redoAction
            }

            QQC2.MenuSeparator {}

            QQC2.MenuItem {
                action: findAction
            }
        }

        QQC2.Menu {
            title: KI18n.i18nc("@title:menu", "&View")

            QQC2.MenuItem {
                action: zoomInAction
            }

            QQC2.MenuItem {
                action: zoomOutAction
            }

            QQC2.MenuItem {
                action: actualSizeAction
            }

            QQC2.MenuItem {
                action: fitAction
            }

            QQC2.MenuItem {
                action: showListAction
            }

            QQC2.MenuSeparator {}

            QQC2.MenuItem {
                action: showBlocksAction
            }

            QQC2.MenuItem {
                action: showFilesAction
            }

            QQC2.MenuItem {
                action: showInfoAction
            }

            QQC2.MenuItem {
                action: showScheduleAction
            }
        }

        QQC2.Menu {
            title: KI18n.i18nc("@title:menu", "Se&ttings")

            QQC2.Menu {
                title: KI18n.i18nc("@title:menu", "&Appearance")

                QQC2.MenuItem {
                    action: darkAppearanceAction
                }

                QQC2.MenuItem {
                    action: lightAppearanceAction
                }

                QQC2.MenuItem {
                    action: systemAppearanceAction
                }
            }

            QQC2.MenuItem {
                action: showMenuBarAction
            }

            QQC2.MenuSeparator {}

            QQC2.MenuItem {
                action: virtualDevicesAction
            }
        }

        QQC2.Menu {
            title: KI18n.i18nc("@title:menu", "&Help")

            QQC2.MenuItem {
                action: handbookAction
            }

            QQC2.MenuItem {
                action: reportBugAction
            }

            QQC2.MenuSeparator {}

            QQC2.MenuItem {
                action: aboutAction
            }
        }
    }

    ToolBarRow {
        id: toolBar

        runAction: runAction
        virtualDevicesAction: virtualDevicesAction
        titleBar: root.ownTitleBar
        mainMenu: mainMenu
        menuButtonShown: !root.menuBarShown
        frameRadius: root.frameRadius
        maximized: root.edgeToEdge
        onWindowMenuRequested: root.showWindowMenu()

        // The session tabs, in the slot the row has been leaving for them.
        // **As wide as the tabs, in a title bar**: the list takes every
        // press inside it, and the room past the last tab is the title
        // bar's to move the window by.
        tabs: SessionTabs {
            id: sessionTabs

            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            width: root.ownTitleBar ? Math.min(sessionTabs.contentWidth, parent.width) : parent.width
            newTabAction: newSessionAction
        }
    }

    footer: StatusBarRow {
        id: statusBar

        frameRadius: root.frameRadius
    }

    // **The window's shape and its edges**, in a window with no frame:
    // the hairline round it, the grips that resize it, the compositor's
    // shadow, the window menu and the rounded background. None of it
    // while KWin frames the window.
    WindowChrome {
        id: chrome

        window: root
        active: root.ownTitleBar
    }

    // **The banner is in the content, not in the header row.** It comes and
    // goes, and a header that changes height moves the whole window's
    // furniture with it; here it pushes the canvas down and nothing else.
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ErrorBanner {
            id: errorBanner

            Layout.fillWidth: true
        }

        QQC2.SplitView {
            id: split

            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal

            SidebarPane {
                id: sidebar

                QQC2.SplitView.preferredWidth: WindowState.sidebarWidth > 0
                    ? WindowState.sidebarWidth
                    : Kirigami.Units.gridUnit * 14
                QQC2.SplitView.minimumWidth: Kirigami.Units.gridUnit * 10
                QQC2.SplitView.maximumWidth: root.width / 2

                blocksContent: [
                    BlockLibrary {
                        id: blockLibrary

                        anchors.fill: parent
                        // **The canvas names the type a block travels
                        // under**, being the half of the gesture that has
                        // to accept it.
                        mimeType: sessionCanvas.blockTypeMimeType
                        // Where a clicked block goes is the canvas's to
                        // decide: in automatic-connections mode it follows
                        // whatever is selected.
                        onBlockChosen: typeId => sessionCanvas.addBlockAfterSelection(typeId)
                    }
                ]

                filesContent: [
                    FilesPane {
                        id: filesPane

                        anchors.fill: parent
                        sessionId: SessionModel.sessionId
                        shown: sidebar.currentIndex === sidebar.filesTab
                        onRecordingActivated: path => root.openInspector(path)
                    }
                ]

                infoContent: [
                    SessionInfoPane {
                        id: infoPane

                        anchors.fill: parent
                    }
                ]

                scheduleContent: [
                    SchedulePane {
                        id: schedulePane

                        anchors.fill: parent
                    }
                ]
            }

            CanvasPlaceholder {
                id: canvas

                QQC2.SplitView.fillWidth: true

                content: [
                    SessionCanvas {
                        id: sessionCanvas

                        anchors.fill: parent
                    }
                ]
            }
        }
    }

    // **Made when it is asked for, not when the window is.** The About
    // page lays out a licence, a component list and a row of links; doing
    // all of that at start-up for a dialog most people never open is work
    // nobody asked for.
    // **The file dialogs are not built and thrown away** like the ones
    // below: a FileDialog is a handle to the platform's own dialog rather
    // than a window of ours, so there is nothing to pay for by keeping it.
    QtDialogs.FileDialog {
        id: exportDialog

        title: KI18n.i18nc("@title:window", "Export Session")
        fileMode: QtDialogs.FileDialog.SaveFile
        defaultSuffix: Sessions.fileSuffix().replace(".", "")
        currentFolder: StandardPaths.standardLocations(StandardPaths.DocumentsLocation)[0]
        // **Named after the session**, so that exporting three of them does
        // not leave three files called the same thing.
        currentFile: exportDialog.currentFolder + "/" + Sessions.nameOf(Sessions.currentId) + Sessions.fileSuffix()
        nameFilters: [
            KI18n.i18nc("@item:inlistbox a session file", "Reverblabr session (*%1)", Sessions.fileSuffix())
        ]
        onAccepted: Sessions.exportCurrent(exportDialog.selectedFile)
    }

    QtDialogs.FileDialog {
        id: importDialog

        title: KI18n.i18nc("@title:window", "Import Session")
        fileMode: QtDialogs.FileDialog.OpenFile
        currentFolder: StandardPaths.standardLocations(StandardPaths.DocumentsLocation)[0]
        nameFilters: [
            KI18n.i18nc("@item:inlistbox a session file", "Reverblabr session (*%1)", Sessions.fileSuffix())
        ]
        onAccepted: Sessions.importFrom(importDialog.selectedFile)
    }

    // **These are children of the window, not of a Loader.** A dialog built
    // inside a Loader is parented to an item with no size, and a Kirigami
    // dialog lays itself out against its parent: one of these came up in the
    // window's top-left corner instead of the middle, and the other drew
    // nothing at all while still dimming the window behind it. Building them
    // with the window costs three small dialogs at startup, which is worth
    // rather less than having them work.

    AppDialog {
        id: openSessionDialog

        /// Which row is picked out, so that Open and Return act on it.
        property string chosenId: ""

        title: KI18n.i18nc("@title:window", "Open Session")
        preferredWidth: Kirigami.Units.gridUnit * 26
        preferredHeight: Kirigami.Units.gridUnit * 22
        standardButtons: Kirigami.Dialog.Cancel | Kirigami.Dialog.Ok
        // The saved sessions are one list, which runs to the dialog's sides.
        listContent: true
        onAccepted: if (openSessionDialog.chosenId !== "") {
            Sessions.openSession(openSessionDialog.chosenId);
        }
        // **The list starts at the session in front.** Somebody opening this
        // usually wants a different one, but they are looking for it from
        // where they are. Set on each opening, not once when it was built.
        onOpened: {
            openSessionDialog.chosenId = Sessions.currentId;
            savedList.currentIndex = -1;
            for (let row = 0; row < Sessions.saved.length; ++row) {
                if (Sessions.saved[row].id === Sessions.currentId) {
                    savedList.currentIndex = row;
                    break;
                }
            }
        }

        ListView {
            id: savedList

            implicitWidth: Kirigami.Units.gridUnit * 24
            implicitHeight: Kirigami.Units.gridUnit * 14
            model: Sessions.saved
            currentIndex: -1
            clip: true

            delegate: QQC2.ItemDelegate {
                id: savedRow

                required property int index
                required property var modelData

                width: savedList.width
                text: savedRow.modelData.name
                highlighted: savedList.currentIndex === savedRow.index
                // **Says which are already open** rather than hiding them:
                // opening one that is open brings it to the front, which is
                // worth being able to do. A word, not an icon, because an
                // icon on one row and none on the next indents the names
                // differently.
                QQC2.Label {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.right: parent.right
                    anchors.rightMargin: Kirigami.Units.largeSpacing
                    text: KI18n.i18nc("@label:listbox this session is already open", "Open")
                    color: AppColors.muted
                    visible: Sessions.isOpen(savedRow.modelData.id)
                }
                onClicked: {
                    savedList.currentIndex = savedRow.index;
                    openSessionDialog.chosenId = savedRow.modelData.id;
                }
                onDoubleClicked: {
                    openSessionDialog.chosenId = savedRow.modelData.id;
                    openSessionDialog.accept();
                }
            }

            PlaceholderMessage {
                anchors.centerIn: parent
                width: parent.width - Kirigami.Units.gridUnit * 4
                visible: savedList.count === 0 && Sessions.ready
                text: KI18n.i18nc("@info:placeholder", "No saved sessions")
            }
        }
    }

    AppDialog {
        id: renameSessionDialog

        /// Where the name is typed. **Named, because a dialog's contentItem
        /// is whatever the dialog wrapped it in**, which a test should not
        /// have to know the shape of.
        readonly property alias nameField: nameField

        title: KI18n.i18nc("@title:window", "Rename Session")
        preferredWidth: Kirigami.Units.gridUnit * 22
        standardButtons: Kirigami.Dialog.Cancel | Kirigami.Dialog.Ok
        onAccepted: Sessions.renameSession(Sessions.currentId, nameField.text)
        // The box says what the session is called now, ready to be typed
        // over. Set on each opening, because the dialog outlives one use.
        onOpened: {
            nameField.text = SessionModel.name;
            nameField.selectAll();
            nameField.forceActiveFocus();
        }

        QQC2.TextField {
            id: nameField

            // **A width of its own.** A field that only fills what it is
            // given is a field a dialog can give nothing to.
            implicitWidth: Kirigami.Units.gridUnit * 20
            // **A name nobody can read is not a name.** An empty one is
            // refused by Sessions rather than accepted and then argued with.
            Accessible.name: KI18n.i18nc("@label:textbox", "Session name")
            onAccepted: renameSessionDialog.accept()
        }
    }

    AppPromptDialog {
        id: deleteSessionDialog

        title: KI18n.i18nc("@title:window", "Delete Session")
        // **Named, so that nobody deletes the wrong one.** A dialog that says
        // "this session" is a dialog somebody reads after pressing the
        // button.
        subtitle: KI18n.i18nc("@info", "Delete “%1”? This cannot be undone.", Sessions.nameOf(Sessions.currentId))
        standardButtons: Kirigami.Dialog.Cancel
        customFooterActions: [
            Kirigami.Action {
                text: KI18n.i18nc("@action:button", "Delete")
                icon.name: "edit-delete"
                onTriggered: {
                    Sessions.deleteSession(Sessions.currentId);
                    deleteSessionDialog.close();
                }
            }
        ]
    }

    // **Made the first time it is asked for, then kept**: closing it hides
    // it, and the next time it opens where it was, with no second instance.
    // A window of its own, not a dialog in this one; it stays above this
    // window, which it belongs to, and goes when the application quits.
    Loader {
        id: virtualDevicesLoader

        active: false

        sourceComponent: VirtualDevicesWindow {
            transientParent: root
        }
    }

    // The tray's popover: made the first time the icon is clicked.
    // **Not this window's child**: it shows while this one is hidden.
    Loader {
        id: trayPopoverLoader

        active: false

        sourceComponent: TrayPopover {
            transientParent: null
            onOpenWindowRequested: root.showWindow()
            onAllSessionsRequested: root.showSessionsWindow()
        }
    }

    // Made the first time it is asked for, then kept, as the Virtual
    // Devices window is.
    Loader {
        id: sessionsWindowLoader

        active: false

        sourceComponent: SessionsWindow {
            transientParent: root
            untitledName: root.untitledName
        }
    }

    Loader {
        id: aboutLoader

        active: false
        onLoaded: (aboutLoader.item as Kirigami.Dialog).open()

        sourceComponent: AppDialog {
            title: KI18n.i18nc("@title:window", "About Reverblabr")
            preferredWidth: Kirigami.Units.gridUnit * 30
            preferredHeight: Kirigami.Units.gridUnit * 24
            standardButtons: Kirigami.Dialog.Close
            onClosed: aboutLoader.active = false

            Kirigami.AboutItem {
                aboutData: AppInfo.aboutData
            }
        }
    }
}
