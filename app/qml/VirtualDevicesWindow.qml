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

// **The virtual devices, in a window of their own**: the outputs apps play
// into and the microphones apps record from, which a session can then take
// as its input or feed with its output.
//
// **A window, not a panel or a dialog**: the devices are the desktop's, not
// a session's, and somebody routing a player into one wants the list beside
// the session window rather than over it. It stays above the main window —
// that is its `transientParent` — and goes with it when the application
// quits.
//
// **Framed as the main window is** (ADR-0012): with no frame of KWin's in the
// Reverblabr appearances, its title in a WindowTitleBar and the edges a
// WindowChrome draws; KWin's frame and title while following the
// system.
QQC2.ApplicationWindow {
    id: root

    Kirigami.Theme.colorSet: Kirigami.Theme.Window
    Kirigami.Theme.inherit: false
    color: root.ownTitleBar ? "transparent" : AppColors.window

    /// Whether the window draws its own title bar: the main window's answer,
    /// fixed at start with the control style.
    readonly property bool ownTitleBar: Appearance.ownStyle
    /// The window's parts, for the tests.
    readonly property alias titleBar: titleBar
    readonly property alias chrome: chrome
    readonly property alias placeholder: placeholder

    /// Where the window's size is kept in the state file.
    readonly property string stateName: "VirtualDevices"

    /// Writes the window's size down; the window's own close does, and the
    /// main window's, which quits the application with this one still open.
    function saveSize() {
        if (root.visibility !== Window.Maximized && root.visibility !== Window.FullScreen) {
            WindowState.saveWindowSize(root.stateName, root.width, root.height);
        }
    }

    title: KI18n.i18nc("@title:window", "Virtual Devices")
    flags: root.ownTitleBar ? (Qt.Window | Qt.FramelessWindowHint) : Qt.Window

    width: WindowState.windowSize(root.stateName).width > 0
        ? WindowState.windowSize(root.stateName).width : Kirigami.Units.gridUnit * 36
    height: WindowState.windowSize(root.stateName).height > 0
        ? WindowState.windowSize(root.stateName).height : Kirigami.Units.gridUnit * 26
    minimumWidth: Kirigami.Units.gridUnit * 24
    minimumHeight: Kirigami.Units.gridUnit * 16

    onClosing: root.saveSize()

    // **The password prompt for an all-users change belongs to this
    // window**, so the desktop puts it over the list it is about.
    Component.onCompleted: VirtualDevices.promptWindow = root
    Component.onDestruction: {
        if (VirtualDevices.promptWindow === root) {
            VirtualDevices.promptWindow = null;
        }
    }

    // **Only the title**, in a title bar of our own: the window has nothing
    // to run and no menus. KWin's title says it while following the system,
    // so there is no bar at all then.
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

    // --- the list ---

    /// The parts, for the tests.
    readonly property alias addButton: addButton
    readonly property alias addDialog: addDialog
    readonly property alias deleteDialog: deleteDialog
    readonly property alias outputs: outputs
    readonly property alias inputs: inputs
    readonly property alias refusedMessage: refusedMessage
    readonly property alias authorizingMessage: authorizingMessage
    readonly property alias userUnreadableMessage: userUnreadableMessage
    readonly property alias systemUnreadableMessage: systemUnreadableMessage

    /// Whether devices for every user can be asked for at all: the build has
    /// the helper. A test may say otherwise.
    property bool allUsersAvailable: VirtualDevices.allUsersAvailable

    /// Whether the engine has said there are none at all, with nothing
    /// hidden by a file it could not read.
    readonly property bool empty: VirtualDevices.ready && VirtualDevices.count === 0
        && Object.keys(VirtualDevices.unreadable).length === 0

    /// Asks for a device to be renamed, switched or deleted: what every row
    /// of both lists sends here.
    function rename(deviceId: string, name: string) {
        refusedMessage.visible = false;
        VirtualDevices.rename(deviceId, name);
    }
    function setDeviceEnabled(deviceId: string, enabled: bool) {
        refusedMessage.visible = false;
        VirtualDevices.setEnabled(deviceId, enabled);
    }
    function confirmDelete(deviceId: string, name: string) {
        deleteDialog.deviceId = deviceId;
        deleteDialog.deviceName = name;
        deleteDialog.open();
    }

    /// The device just made, until its row is there to be selected.
    property string awaitingSelection: ""

    function selectNewDevice() {
        if (root.awaitingSelection === "") {
            return;
        }
        const section = outputs.select(root.awaitingSelection) ? outputs
            : inputs.select(root.awaitingSelection) ? inputs : null;
        if (section !== null) {
            root.awaitingSelection = "";
            root.reveal(section.list.currentRow());
        }
    }

    /// Scrolls the window so that \a item is in view.
    function reveal(item: Item) {
        const flickable = scroll.contentItem as Flickable;
        if (item === null || flickable === null) {
            return;
        }
        const top = item.mapToItem(flickable.contentItem, 0, 0).y;
        const bottom = top + item.height;
        if (top < flickable.contentY) {
            flickable.contentY = top;
        } else if (bottom > flickable.contentY + flickable.height) {
            flickable.contentY = bottom - flickable.height;
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // **What the window is for, and the one thing to do in it.**
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: AppMetrics.panelPadding
            spacing: AppMetrics.largeSpacing

            QQC2.Label {
                Layout.fillWidth: true
                text: KI18n.i18nc("@info",
                                  "Devices that other apps can play into and record from. Each one stays until you delete it, whether or not Reverblabr is running.")
                color: AppColors.muted
                font: AppFonts.small
                wrapMode: Text.WordWrap
            }

            QQC2.Button {
                id: addButton

                text: KI18n.i18nc("@action:button", "Add Device…")
                icon.name: Appearance.followsSystem ? "list-add" : ""
                icon.source: Appearance.followsSystem ? "" : AppGlyphs.add
                enabled: VirtualDevices.ready && !VirtualDevices.creating
                Accessible.name: KI18n.i18nc("@action:button", "Add a virtual device")
                onClicked: addDialog.open()
            }
        }

        // **Refusals are said here**, beside the list they are about, not
        // only in the main window's banner. A password prompt
        // somebody closed themselves is news, not an error.
        Kirigami.InlineMessage {
            id: refusedMessage

            /// The reason of the refusal shown.
            property string reason: ""

            Layout.fillWidth: true
            Layout.leftMargin: AppMetrics.panelPadding
            Layout.rightMargin: AppMetrics.panelPadding
            Layout.bottomMargin: visible ? AppMetrics.smallSpacing : 0
            type: refusedMessage.reason === "AuthorizationCancelled" ? Kirigami.MessageType.Information
                                                                     : Kirigami.MessageType.Error
            showCloseButton: true
            visible: false
        }

        // **While the desktop asks for the administrator's password** for an
        // all-users change: the prompt is the desktop's, and may be
        // behind something, so the window says what it is waiting for.
        Kirigami.InlineMessage {
            id: authorizingMessage

            Layout.fillWidth: true
            Layout.leftMargin: AppMetrics.panelPadding
            Layout.rightMargin: AppMetrics.panelPadding
            Layout.bottomMargin: visible ? AppMetrics.smallSpacing : 0
            type: Kirigami.MessageType.Information
            visible: VirtualDevices.authorizing
            text: KI18n.i18nc("@info",
                              "Waiting for the administrator's password… Devices for all users can't be changed until it's given or cancelled.")
        }

        Kirigami.InlineMessage {
            id: userUnreadableMessage

            readonly property string reason: VirtualDevices.unreadable.user ?? ""

            Layout.fillWidth: true
            Layout.leftMargin: AppMetrics.panelPadding
            Layout.rightMargin: AppMetrics.panelPadding
            Layout.bottomMargin: visible ? AppMetrics.smallSpacing : 0
            type: Kirigami.MessageType.Warning
            visible: userUnreadableMessage.reason !== ""
            text: KI18n.i18nc("@info %1 is why the file could not be read",
                              "Your virtual devices could not be read: %1 They are not listed, and no change can be saved until the file can be read.",
                              VirtualDevices.reasonText(userUnreadableMessage.reason))
        }

        Kirigami.InlineMessage {
            id: systemUnreadableMessage

            readonly property string reason: VirtualDevices.unreadable.system ?? ""

            Layout.fillWidth: true
            Layout.leftMargin: AppMetrics.panelPadding
            Layout.rightMargin: AppMetrics.panelPadding
            Layout.bottomMargin: visible ? AppMetrics.smallSpacing : 0
            type: Kirigami.MessageType.Warning
            visible: systemUnreadableMessage.reason !== ""
            text: KI18n.i18nc("@info %1 is why the file could not be read",
                              "The devices for all users could not be read: %1 They are not listed.",
                              VirtualDevices.reasonText(systemUnreadableMessage.reason))
        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            QQC2.ScrollView {
                id: scroll

                anchors.fill: parent
                visible: !root.empty
                contentWidth: availableWidth

                ColumnLayout {
                    width: scroll.availableWidth
                    spacing: AppMetrics.wideSpacing

                    VirtualDeviceSection {
                        id: outputs

                        Layout.fillWidth: true
                        Layout.topMargin: AppMetrics.panelPadding
                        Layout.leftMargin: AppMetrics.panelPadding
                        Layout.rightMargin: AppMetrics.panelPadding
                        direction: "output"
                        title: KI18n.i18nc("@title virtual devices apps play into", "Outputs")
                        description: KI18n.i18nc("@info",
                                                 "Apps play into these like a sound card. Choose one in an Input Device block to process what they play.")
                        emptyText: KI18n.i18nc("@info:placeholder", "No virtual outputs.")
                        onRenameRequested: (deviceId, name) => root.rename(deviceId, name)
                        onEnableRequested: (deviceId, enabled) => root.setDeviceEnabled(deviceId, enabled)
                        onDeleteRequested: (deviceId, name) => root.confirmDelete(deviceId, name)
                        KeyNavigation.tab: inputs.list.count > 0 ? inputs.list : addButton
                    }

                    VirtualDeviceSection {
                        id: inputs

                        Layout.fillWidth: true
                        Layout.bottomMargin: AppMetrics.panelPadding
                        Layout.leftMargin: AppMetrics.panelPadding
                        Layout.rightMargin: AppMetrics.panelPadding
                        direction: "input"
                        title: KI18n.i18nc("@title virtual devices apps record from", "Inputs")
                        description: KI18n.i18nc("@info",
                                                 "Apps record from these like a microphone. Feed one from an Output Device block to give them what a session makes.")
                        emptyText: KI18n.i18nc("@info:placeholder", "No virtual inputs.")
                        onRenameRequested: (deviceId, name) => root.rename(deviceId, name)
                        onEnableRequested: (deviceId, enabled) => root.setDeviceEnabled(deviceId, enabled)
                        onDeleteRequested: (deviceId, name) => root.confirmDelete(deviceId, name)
                    }
                }
            }

            // **None at all**: what the two kinds are for, where the lists
            // would be.
            PlaceholderMessage {
                id: placeholder

                anchors.centerIn: parent
                width: Math.min(parent.width - AppMetrics.panelPadding * 2, Kirigami.Units.gridUnit * 24)
                visible: root.empty
                text: KI18n.i18nc("@info:placeholder",
                                  "No virtual devices yet. Add an output to send every app's sound through a session, or an input to give other apps a session's sound as a microphone.")
            }
        }
    }

    // --- adding one ---
    AppDialog {
        id: addDialog

        /// The parts, for the tests.
        readonly property alias nameField: addName
        readonly property alias outputChoice: outputChoice
        readonly property alias inputChoice: inputChoice
        readonly property alias stereoChoice: stereoChoice
        readonly property alias monoChoice: monoChoice
        readonly property alias onlyMeChoice: onlyMeChoice
        readonly property alias allUsersChoice: allUsersChoice
        readonly property alias addAction: addAction
        /// The name as it will be sent, and whether it can be.
        readonly property string typedName: addName.text.trim()
        readonly property bool nameAccepted: addDialog.typedName.length > 0
        /// **For every user of the computer**, through the administrator's
        /// password: the form's last choice.
        readonly property bool forAllUsers: allUsersChoice.checked
        /// Whether an all-users device can be asked for now: the build has
        /// the helper, and no other all-users change waits for a password.
        readonly property bool allUsersOffered: root.allUsersAvailable && !VirtualDevices.authorizing

        focus: true
        title: KI18n.i18nc("@title:window", "Add Virtual Device")
        preferredWidth: Kirigami.Units.gridUnit * 24
        standardButtons: Kirigami.Dialog.Cancel
        customFooterActions: [
            Kirigami.Action {
                id: addAction

                text: KI18n.i18nc("@action:button", "Add")
                // The padlock when the password will be asked for.
                icon.name: addDialog.forAllUsers ? "object-locked" : "list-add"
                enabled: addDialog.nameAccepted && (!addDialog.forAllUsers || addDialog.allUsersOffered)
                onTriggered: addDialog.accept()
            }
        ]
        // A fresh form on each opening, because the dialog outlives one use:
        // a stereo output, the commonest thing to want.
        onOpened: {
            addName.text = "";
            onlyMeChoice.checked = true;
            outputChoice.checked = true;
            stereoChoice.checked = true;
            addName.forceActiveFocus();
        }
        onAccepted: {
            if (!addDialog.nameAccepted || (addDialog.forAllUsers && !addDialog.allUsersOffered)) {
                return;
            }
            refusedMessage.visible = false;
            const direction = inputChoice.checked ? "input" : "output";
            const channels = monoChoice.checked ? "mono" : "stereo";
            if (addDialog.forAllUsers) {
                VirtualDevices.createForAllUsers(addDialog.typedName, direction, channels);
            } else {
                VirtualDevices.create(addDialog.typedName, direction, channels);
            }
        }

        Kirigami.FormLayout {
            QQC2.TextField {
                id: addName

                Kirigami.FormData.label: KI18n.i18nc("@label:textbox", "Name:")
                implicitWidth: Kirigami.Units.gridUnit * 14
                maximumLength: VirtualDevices.maximumNameLength
                placeholderText: KI18n.i18nc("@info:placeholder an example name for a virtual device", "Every app")
                Accessible.name: KI18n.i18nc("@label:textbox", "Device name")
                onAccepted: {
                    if (addDialog.nameAccepted) {
                        addDialog.accept();
                    }
                }
            }

            SegmentedFrame {
                id: kindFrame

                Kirigami.FormData.label: KI18n.i18nc("@label which kind of virtual device", "Kind:")

                QQC2.ButtonGroup {
                    id: kindGroup
                }

                SegmentedChoice {
                    id: outputChoice

                    text: KI18n.i18nc("@option:radio a virtual device apps play into", "Output")
                    checked: true
                    QQC2.ButtonGroup.group: kindGroup
                }

                SegmentedChoice {
                    id: inputChoice

                    text: KI18n.i18nc("@option:radio a virtual device apps record from", "Input")
                    QQC2.ButtonGroup.group: kindGroup
                }
            }

            QQC2.Label {
                Layout.maximumWidth: Kirigami.Units.gridUnit * 16
                text: inputChoice.checked
                    ? KI18n.i18nc("@info", "Apps record from it, like a microphone.")
                    : KI18n.i18nc("@info", "Apps play into it, like a sound card.")
                color: AppColors.muted
                font: AppFonts.small
                wrapMode: Text.WordWrap
            }

            SegmentedFrame {
                id: channelsFrame

                Kirigami.FormData.label: KI18n.i18nc("@label how many channels a virtual device has", "Channels:")

                QQC2.ButtonGroup {
                    id: channelsGroup
                }

                SegmentedChoice {
                    id: stereoChoice

                    text: KI18n.i18nc("@option:radio a virtual device with two channels", "Stereo")
                    checked: true
                    QQC2.ButtonGroup.group: channelsGroup
                }

                SegmentedChoice {
                    id: monoChoice

                    text: KI18n.i18nc("@option:radio a virtual device with one channel", "Mono")
                    QQC2.ButtonGroup.group: channelsGroup
                }
            }

            // **Who it is for**: this user, or every user of the
            // computer, which only an administrator may add.
            SegmentedFrame {
                id: scopeFrame

                Kirigami.FormData.label: KI18n.i18nc("@label who a virtual device is for", "For:")

                QQC2.ButtonGroup {
                    id: scopeGroup
                }

                SegmentedChoice {
                    id: onlyMeChoice

                    text: KI18n.i18nc("@option:radio a virtual device only this user has", "Only Me")
                    Accessible.name: KI18n.i18nc("@option:radio", "A device only for you")
                    checked: true
                    QQC2.ButtonGroup.group: scopeGroup
                }

                SegmentedChoice {
                    id: allUsersChoice

                    text: KI18n.i18nc("@option:radio a virtual device every user of the computer has", "All Users")
                    Accessible.name: KI18n.i18nc("@option:radio", "A device for all users, which needs an administrator's password")
                    enabled: addDialog.allUsersOffered
                    QQC2.ButtonGroup.group: scopeGroup
                    Accessible.description: allUsersChoice.whyNot
                    // **Why it can't be chosen**, on the choice itself.
                    readonly property string whyNot: !root.allUsersAvailable
                        ? KI18n.i18nc("@info:tooltip",
                                      "This copy of Reverblabr was built without the part that makes devices for all users.")
                        : VirtualDevices.authorizing
                        ? KI18n.i18nc("@info:tooltip",
                                      "Another change for all users is waiting for the administrator's password.")
                        : ""
                    // A disabled button takes no hover of its own; the
                    // handler still does.
                    HoverHandler {
                        id: allUsersHover
                    }
                    QQC2.ToolTip.text: allUsersChoice.whyNot
                    QQC2.ToolTip.visible: allUsersHover.hovered && allUsersChoice.whyNot !== ""
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                }
            }

            QQC2.Label {
                Layout.maximumWidth: Kirigami.Units.gridUnit * 16
                text: addDialog.forAllUsers
                    ? KI18n.i18nc("@info", "Every user of this computer gets it. An administrator's password is asked for.")
                    : KI18n.i18nc("@info", "Only you get it.")
                color: AppColors.muted
                font: AppFonts.small
                wrapMode: Text.WordWrap
            }
        }
    }

    // --- deleting one ---
    AppPromptDialog {
        id: deleteDialog

        property string deviceId: ""
        property string deviceName: ""
        readonly property alias deleteAction: deleteAction

        // **The keys go to the question**, or Escape would not cancel it and
        // Tab would not reach its buttons: opened from the list's Delete
        // key, the list kept them.
        focus: true
        onOpened: deleteDialog.contentItem.forceActiveFocus()
        title: KI18n.i18nc("@title:window", "Delete Virtual Device")
        // **Named, and what it does to the apps using it said**: deleting a
        // device that apps are playing into sends them somewhere else.
        subtitle: KI18n.i18nc("@info",
                              "Delete “%1”? Apps using it will move to another device. This cannot be undone.",
                              deleteDialog.deviceName)
        standardButtons: Kirigami.Dialog.Cancel
        customFooterActions: [
            Kirigami.Action {
                id: deleteAction

                text: KI18n.i18nc("@action:button", "Delete")
                icon.name: "edit-delete"
                onTriggered: {
                    refusedMessage.visible = false;
                    VirtualDevices.remove(deleteDialog.deviceId);
                    deleteDialog.close();
                }
            }
        ]
    }

    Connections {
        target: VirtualDevices

        function onRefused(code: string, reason: string, message: string) {
            refusedMessage.reason = reason;
            refusedMessage.text = message;
            refusedMessage.visible = true;
        }

        // **The new device selected and in view**, so that the next thing
        // somebody does — renaming it, picking it in a block — starts from
        // it. The engine names it before the list that holds it arrives, so
        // the selection waits for its row.
        function onCreated(deviceId: string) {
            root.awaitingSelection = deviceId;
            Qt.callLater(root.selectNewDevice);
        }

        function onCountChanged() {
            Qt.callLater(root.selectNewDevice);
        }
    }

    WindowChrome {
        id: chrome

        window: root
        active: root.ownTitleBar
    }
}
