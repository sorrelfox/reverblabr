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

// One virtual device in the Virtual Devices window: what it is
// called, what kind it is, whether it is there, and the switch, rename and
// delete that change it.
//
// **Drawn as the block library draws a block** — the same icon in the same
// tinted square, a title and a line under it — because an output here is
// what an Input Device block will record, and a microphone what an Output
// Device block will feed: the icons are those blocks' own.
//
// **Nothing here changes what it shows by itself.** A rename shows the old
// name until the engine's list carries the new one, and a switch that the
// engine refused goes back to where it was: the row says what the engine
// says, and `pending` covers the moment in between.
QQC2.ItemDelegate {
    id: row

    required property int index
    required property string deviceId
    required property string name
    required property string direction
    required property string channels
    required property string scope
    required property string status
    required property bool pending
    /// **The model, for the one role an Item already has a property for**:
    /// a device's `enabled` cannot be declared on a delegate, which is an
    /// Item and has an `enabled` of its own.
    required property var model

    /// Whether the device is switched on.
    readonly property bool deviceEnabled: row.model.enabled

    /// **Every user's device**, an administrator's to change. The
    /// row asks the model the same way whatever the scope, and the model
    /// sends an all-users change through the password prompt; this is
    /// what the padlock in front of the switch, rename and delete keys on.
    readonly property bool forAllUsers: row.scope === "system"
    /// Whether it is the list's selected row, and whether the list has the
    /// keys: the row is drawn selected only when both are so.
    property bool current: false
    property bool listFocused: false

    /// **Waiting for the administrator's password**: one all-users change
    /// is out with the helper. Every all-users row then takes no
    /// change at all, not only the one it is about — the model would
    /// refuse a second one, and the window should not offer it.
    readonly property bool authorizing: VirtualDevices.authorizing
    /// Whether a change may be asked for now: one at a time, and for an
    /// all-users device, not while the password is being asked for.
    readonly property bool actionable: !row.pending && !(row.forAllUsers && row.authorizing)
    /// What the switch, rename and delete say beyond their names on an
    /// all-users row: that the password will be asked for.
    readonly property string passwordNote: row.forAllUsers
        ? KI18n.i18nc("@info:tooltip", "Needs an administrator's password") : ""
    readonly property bool renaming: nameField.visible
    /// The parts, for the tests.
    readonly property alias enabledSwitch: enabledSwitch
    readonly property alias renameButton: renameButton
    readonly property alias deleteButton: deleteButton
    readonly property alias nameField: nameField
    readonly property alias detailLabel: detailLabel
    readonly property alias scopeBadge: scopeBadge
    readonly property alias lockMarker: lockMarker

    /// **Drawn on the selection colour**, which the desktop's style fills a
    /// selected row with: its words then take the colour made for it. The
    /// Reverblabr style marks a selected row with a surface instead.
    readonly property bool onSelection: row.highlighted && AppMetrics.followsSystem
    readonly property color textColor: row.onSelection ? AppColors.textOnAccent : AppColors.text

    /// The block whose icon and tint the device wears.
    readonly property string blockType: row.direction === "input" ? "source.device" : "output.device"

    /// The words for how many channels it has.
    readonly property string channelsText: row.channels === "mono"
        ? KI18n.i18nc("@item a virtual device with one channel", "Mono")
        : KI18n.i18nc("@item a virtual device with two channels", "Stereo")

    /// What is wrong or under way, or nothing while it is simply there.
    readonly property string statusText: {
        if (row.pending && row.forAllUsers && row.authorizing) {
            return KI18n.i18nc("@info:status a change to a virtual device for all users is waiting",
                               "Waiting for the administrator's password…");
        }
        if (row.pending) {
            return KI18n.i18nc("@info:status a change to a virtual device is being made", "Saving…");
        }
        switch (row.status) {
        case "disabled":
            return KI18n.i18nc("@info:status a virtual device switched off", "Off: no app can see it");
        case "creating":
            return KI18n.i18nc("@info:status", "Starting…");
        case "missing":
            return KI18n.i18nc("@info:status", "Missing: the audio system has not made it");
        case "waitingForPipeWire":
            return KI18n.i18nc("@info:status", "Waiting for the audio system");
        default:
            return "";
        }
    }

    /// The row's line under the name: channels, and where it stands.
    readonly property string detailText: row.statusText === ""
        ? row.channelsText
        : KI18n.i18nc("@info a virtual device's channels, then its state: 'Stereo · Starting…'", "%1 · %2",
                      row.channelsText, row.statusText)

    /// A press on the row, which selects it.
    signal selected()
    /// Renaming has ended, sent or not: the keys go back to the list.
    signal editingEnded()
    signal renameRequested(string deviceId, string name)
    signal enableRequested(string deviceId, bool enabled)
    signal deleteRequested(string deviceId, string name)

    /// Starts renaming, which a double-click on the name, F2 and the rename
    /// button do.
    function startRenaming() {
        if (!row.actionable) {
            return;
        }
        nameField.cancelled = false;
        nameField.text = row.name;
        nameField.visible = true;
        nameField.forceActiveFocus();
        nameField.selectAll();
    }

    /// Asks for the switch to be flipped, as Space does on a selected row.
    function toggleEnabled() {
        if (row.actionable) {
            row.enableRequested(row.deviceId, !row.deviceEnabled);
        }
    }

    /// Asks for it to be deleted, after the window's confirmation.
    function requestDelete() {
        if (row.actionable) {
            row.deleteRequested(row.deviceId, row.name);
        }
    }

    highlighted: row.current && row.listFocused
    // **The list has the keys, not each row**: the arrows, F2, Space and
    // Delete are the list's, and act on the selected row; Tab goes through
    // the row's own switch and buttons.
    focusPolicy: Qt.NoFocus
    // A double action is refused rather than queued: the row is dimmed
    // while the engine answers.
    opacity: row.pending ? AppMetrics.disabledOpacity + (1 - AppMetrics.disabledOpacity) / 2 : 1

    Accessible.role: Accessible.ListItem
    Accessible.name: row.name
    Accessible.description: !row.forAllUsers
        ? row.detailText
        : KI18n.i18nc("@info:whatsthis a virtual device's details, then who it is for", "%1, for all users",
                      row.detailText)

    onClicked: row.selected()
    onDoubleClicked: row.startRenaming()

    contentItem: RowLayout {
        spacing: AppMetrics.followsSystem ? Kirigami.Units.smallSpacing : AppMetrics.rowSpacing

        // The icon in its tinted square, as the library draws a block.
        Rectangle {
            implicitWidth: AppMetrics.followsSystem ? Kirigami.Units.iconSizes.smallMedium : AppMetrics.controlHeight
            implicitHeight: implicitWidth
            radius: AppMetrics.tileRadius
            color: AppMetrics.followsSystem ? "transparent" : AppColors.surface
            border.width: AppMetrics.followsSystem ? 0 : 1
            border.color: AppColors.border

            Kirigami.Icon {
                anchors.centerIn: parent
                source: BlockPresentation.icon(row.blockType)
                color: AppColors.categoryTint(BlockPresentation.category(row.blockType))
                isMask: true
                implicitWidth: AppMetrics.followsSystem ? Kirigami.Units.iconSizes.smallMedium
                                                        : AppMetrics.libraryIconSize
                implicitHeight: implicitWidth
                Accessible.ignored: true
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                spacing: AppMetrics.smallSpacing

                QQC2.Label {
                    id: nameLabel

                    visible: !nameField.visible
                    // **As wide as the name and no wider**, so that the
                    // badge saying who it is for sits beside it; a long name
                    // still gives way, and elides.
                    Layout.fillWidth: true
                    // Rounded up: a width of fractional pixels given back
                    // as whole ones elided a name that fitted.
                    Layout.maximumWidth: Math.ceil(nameLabel.implicitWidth) + 1
                    text: row.name
                    color: row.textColor
                    font: AppFonts.libraryTitle
                    elide: Text.ElideRight
                    Accessible.ignored: true
                }

                QQC2.TextField {
                    id: nameField

                    /// Set by Escape, so that losing the focus as the field
                    /// goes away does not send what was typed.
                    property bool cancelled: false

                    visible: false
                    Layout.fillWidth: true
                    maximumLength: VirtualDevices.maximumNameLength
                    Accessible.name: KI18n.i18nc("@label:textbox", "Device name")

                    function finish(send: bool) {
                        if (!nameField.visible) {
                            return;
                        }
                        const typed = nameField.text.trim();
                        // **Hidden first.** On a desktop, losing the focus
                        // ends the editing again, at once, and that second
                        // finish must find the box gone: sent twice, an
                        // all-users rename ran into its own password prompt
                        // and was refused. (Offscreen the focus goes later,
                        // so no QML test sees it.)
                        nameField.visible = false;
                        // **The focus flag goes too**, not only the box: the
                        // list is a focus scope, and handing it the focus
                        // back would give it straight to a hidden field that
                        // still said it wanted it — and Space would type.
                        nameField.focus = false;
                        if (send && typed !== "" && typed !== row.name) {
                            row.renameRequested(row.deviceId, typed);
                        }
                        row.editingEnded();
                    }

                    onEditingFinished: nameField.finish(!nameField.cancelled)
                    Keys.onEscapePressed: {
                        nameField.cancelled = true;
                        nameField.finish(false);
                    }
                }

                // **Who it is for**, only when that is not just this user.
                Rectangle {
                    id: scopeBadge

                    visible: row.forAllUsers
                    implicitWidth: scopeText.implicitWidth + AppMetrics.mediumSpacing * 2
                    implicitHeight: scopeText.implicitHeight + AppMetrics.hairSpacing * 2
                    radius: AppMetrics.smallRadius
                    color: AppColors.surface2
                    border.width: 1
                    border.color: AppColors.border

                    QQC2.Label {
                        id: scopeText

                        anchors.centerIn: parent
                        text: KI18n.i18nc("@info a virtual device every user of the computer has", "All users")
                        color: AppColors.muted
                        font: AppFonts.small
                        Accessible.ignored: true
                    }
                }

                Item {
                    visible: !nameField.visible
                    Layout.fillWidth: true
                }
            }

            QQC2.Label {
                id: detailLabel

                Layout.fillWidth: true
                text: row.detailText
                color: row.onSelection ? row.textColor
                    : row.status === "missing" && !row.pending ? AppColors.warning : AppColors.muted
                font: AppFonts.libraryDetail
                elide: Text.ElideRight
                Accessible.ignored: true
            }
        }

        // --- the row's actions ---
        //
        // **A padlock in front of them on an all-users row**: each of
        // the three asks for an administrator's password, and says so in
        // its tooltip and to a screen reader as well.

        Kirigami.Icon {
            id: lockMarker

            visible: row.forAllUsers
            source: AppMetrics.followsSystem ? "object-locked" : AppGlyphs.lock
            isMask: !AppMetrics.followsSystem
            color: row.onSelection ? row.textColor : AppColors.muted
            implicitWidth: AppMetrics.followsSystem ? Kirigami.Units.iconSizes.small : AppMetrics.buttonIconSize
            implicitHeight: implicitWidth
            Layout.alignment: Qt.AlignVCenter
            Accessible.role: Accessible.Graphic
            Accessible.name: row.passwordNote

            HoverHandler {
                id: lockHover
            }
            QQC2.ToolTip.text: row.passwordNote
            QQC2.ToolTip.visible: lockHover.hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
        }

        QQC2.Switch {
            id: enabledSwitch

            enabled: row.actionable
            checked: row.deviceEnabled
            Accessible.name: KI18n.i18nc("@action:button switch a virtual device on or off", "Device on")
            Accessible.description: row.passwordNote
            QQC2.ToolTip.text: row.forAllUsers
                ? KI18n.i18nc("@info:tooltip %1 is 'Device on'", "%1 (needs an administrator's password)",
                              enabledSwitch.Accessible.name)
                : enabledSwitch.Accessible.name
            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            // **Asked for, then put back to what the engine says.** Flipping
            // the switch writes `checked` and so ends its binding; the
            // binding is restored at once, and the engine's list (or its
            // refusal) is what moves it.
            onToggled: {
                const wanted = enabledSwitch.checked;
                enabledSwitch.checked = Qt.binding(() => row.deviceEnabled);
                row.enableRequested(row.deviceId, wanted);
            }
        }

        QQC2.ToolButton {
            id: renameButton

            enabled: row.actionable
            icon.name: "edit-rename"
            display: QQC2.AbstractButton.IconOnly
            Accessible.name: KI18n.i18nc("@action:button", "Rename %1", row.name)
            Accessible.description: row.passwordNote
            QQC2.ToolTip.text: row.forAllUsers
                ? KI18n.i18nc("@info:tooltip", "Rename (needs an administrator's password)")
                : KI18n.i18nc("@info:tooltip", "Rename")
            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            onClicked: row.startRenaming()

            Binding on implicitWidth {
                when: !AppMetrics.followsSystem
                value: AppMetrics.mediumControlHeight
            }
            Binding on implicitHeight {
                when: !AppMetrics.followsSystem
                value: AppMetrics.mediumControlHeight
            }
        }

        QQC2.ToolButton {
            id: deleteButton

            enabled: row.actionable
            icon.name: "trash-empty-symbolic"
            display: QQC2.AbstractButton.IconOnly
            Accessible.name: KI18n.i18nc("@action:button", "Delete %1", row.name)
            Accessible.description: row.passwordNote
            QQC2.ToolTip.text: row.forAllUsers
                ? KI18n.i18nc("@info:tooltip", "Delete (needs an administrator's password)")
                : KI18n.i18nc("@info:tooltip", "Delete")
            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            onClicked: row.requestDelete()

            Binding on implicitWidth {
                when: !AppMetrics.followsSystem
                value: AppMetrics.mediumControlHeight
            }
            Binding on implicitHeight {
                when: !AppMetrics.followsSystem
                value: AppMetrics.mediumControlHeight
            }
        }
    }
}
