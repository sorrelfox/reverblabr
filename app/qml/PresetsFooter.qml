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

// The row at the foot of a settings panel: which preset the block is set
// from, and what can be done about it, and putting its settings back to
// their defaults.
//
// **One row, used by every panel**, rather than each panel growing its own:
// a block that can be set from a preset can be set from one the same way,
// and the panels were already keeping the space for it.
//
// **"Manual" is worked out from the values, not remembered.** A block
// carries the id of the preset it was last set from, and nothing clears
// that when somebody turns a knob — nor should it, because undo has to put
// it back. So the answer comes from whether the values are still a preset's,
// which is what makes "Manual" right after any hand edit.
//
// **A panel with no use for presets keeps the row**, without the presets:
// a meter has settings to put back like any other block, and the bin is
// how its block is taken off from its own settings too.
RowLayout {
    id: footer

    /// The block whose settings these are.
    required property string blockId

    readonly property string typeId: SessionModel.blockType(footer.blockId)
    /// Whether the presets are offered: the label, the box and their
    /// entries in the menu. The panel says, from what its body asks.
    property bool showsPresets: true
    readonly property bool isPlugin: footer.typeId.startsWith("plugin.")

    /// The parameters a reset puts back: **what a preset would set**, so
    /// that "the defaults" mean what a preset of the defaults would. A
    /// block's readings are not settings, and which ear a hearing block is
    /// for says which of a pair it is (`inPresets: false`). A password or a
    /// stream key is in the desktop's secret store and is no parameter, so
    /// it stays where it is.
    ///
    /// **Not for a plugin block.** A plugin's settings are more than the
    /// parameters it lists: its saved state holds the rest (a file it has
    /// loaded, a mode it is in), and only the plugin can make that new
    /// again. Clearing the parameters alone would leave a mixture of its
    /// defaults and its old state that the plugin never offered; its own
    /// presets are the way back to a starting point.
    readonly property var resettable: {
        if (footer.isPlugin || !BlockCatalogue.ready) {
            return [];
        }
        return BlockCatalogue.params(footer.typeId)
            .filter(descriptor => descriptor.readOnly !== true && descriptor.inPresets !== false)
            .map(descriptor => descriptor.id);
    }
    /// Which of those the block has away from their defaults: what a reset
    /// would change. **Read through the revision**, so that the menu entry
    /// is offered after a change and not after a reset.
    readonly property var changedFromDefaults: {
        const revision = SessionModel.revision;
        if (footer.blockId === "" || revision < 0) {
            return [];
        }
        const held = SessionModel.params(footer.blockId);
        return footer.resettable.filter(id => id in held);
    }
    /// Every preset for this kind of block, shipped ones first.
    readonly property var presets: {
        const all = Presets.revision >= 0 ? Presets.forType(footer.typeId) : [];
        // **A plugin block's presets are its plugin's**: another plugin's
        // settings, under the same kind of block, mean nothing to this one.
        if (!footer.typeId.startsWith("plugin.")) {
            return all;
        }
        const plugin = SessionModel.revision >= 0 ? (SessionModel.param(footer.blockId, "plugin") ?? "") : "";
        return all.filter(preset => preset.plugin === plugin);
    }
    /// The one the block is showing, or empty for Manual. **Read through the
    /// session's revision** so that it is worked out again after every edit,
    /// which is how turning one knob changes the word in the box.
    /// What the block is set to now, **including the parameters nobody has
    /// touched**. A session holds a parameter only once it has been set, so
    /// asking it alone would say a fresh Volume block at 100 % matches no
    /// preset — while the preset that says exactly 100 % sits in the list
    /// underneath. This is the same reckoning saving a preset uses, so what
    /// the box says and what a save would write cannot disagree.
    readonly property var effectiveParams: {
        // **Read at the top so that this is a binding and not a snapshot.**
        // `SessionModel.param()` is a function call, which QML cannot watch;
        // without naming the revision the values would be worked out once
        // and then never again, and the box would keep saying whatever was
        // true when the panel opened.
        const revision = SessionModel.revision;
        const values = {};
        if (footer.blockId === "" || !BlockCatalogue.ready || revision < 0) {
            return values;
        }
        for (const descriptor of BlockCatalogue.params(footer.typeId)) {
            if (descriptor.readOnly === true) {
                continue;
            }
            const set = SessionModel.param(footer.blockId, descriptor.id);
            values[descriptor.id] = set === undefined ? descriptor.default : set;
        }
        // A plugin block's settings are its plugin's, which the catalogue
        // cannot list: what the session holds of them.
        if (footer.typeId.startsWith("plugin.")) {
            const held = SessionModel.params(footer.blockId);
            for (const id in held) {
                if (!(id in values)) {
                    values[id] = held[id];
                }
            }
        }
        return values;
    }

    readonly property string shownId: SessionModel.revision >= 0 && Presets.revision >= 0
        ? Presets.shownBy(footer.typeId, SessionModel.block(footer.blockId).preset ?? "", footer.effectiveParams)
        : ""
    readonly property bool isFactory: footer.shownId !== "" && Presets.isFactory(footer.typeId, footer.shownId)

    /// The controls, for a test.
    readonly property alias chooser: chooser
    readonly property alias menuButton: menuButton
    readonly property alias removeButton: removeButton
    readonly property alias saveDialog: saveDialog
    readonly property alias renameDialog: renameDialog
    readonly property alias deleteDialog: deleteDialog
    readonly property alias presetMenu: actions
    readonly property alias settingsMenu: settingsActions
    readonly property alias resetAction: resetAction
    readonly property alias resetDialog: resetDialog

    /// What the box says when the block's values are nobody's preset.
    readonly property string manualText: KI18n.i18nc("@item:inlistbox the block's settings match no preset", "Manual")

    Layout.fillWidth: true
    spacing: AppMetrics.followsSystem ? Kirigami.Units.smallSpacing : AppMetrics.rowSpacing

    // **Taking the block off the canvas, from its own settings**, at
    // the far end of the row from the presets, as every panel in the design
    // has it. One undoable step, like Del on the canvas, so it asks nothing;
    // the panel closes itself when its block goes.
    QQC2.ToolButton {
        id: removeButton

        // **The bin in the text's colour, muted**, as the design draws it.
        // Taking a block off is undoable, and the theme's red edit-delete
        // shouts about something that is not dangerous; this is the same
        // shape drawn in the text colour, which the tint then follows.
        icon.name: "trash-empty-symbolic"
        icon.color: AppColors.muted
        display: QQC2.AbstractButton.IconOnly
        Accessible.name: KI18n.i18nc("@action:button take this block off the canvas", "Remove block")
        QQC2.ToolTip.text: Accessible.name
        QQC2.ToolTip.visible: hovered
        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
        onClicked: SessionModel.removeBlocks([footer.blockId])

        // The prototype's 26-pixel square; the desktop style's own size
        // while following the system.
        Binding on implicitWidth {
            when: !AppMetrics.followsSystem
            value: AppMetrics.mediumControlHeight
        }
        Binding on implicitHeight {
            when: !AppMetrics.followsSystem
            value: AppMetrics.mediumControlHeight
        }
    }

    Item {
        Layout.fillWidth: true
    }

    QQC2.Label {
        visible: footer.showsPresets
        text: KI18n.i18nc("@label saved settings for this block", "Presets:")
        color: AppColors.muted
    }

    QQC2.ComboBox {
        id: chooser

        visible: footer.showsPresets
        // Wide enough for a shipped preset's name, which is two words and a
        // level: "Unchanged (100 %)" — **as wide as that at most, and
        // narrower when the card is**. A fixed width here made the footer
        // wider than a narrow card, and the card's column laid everything
        // out at the footer's width, past the card's right edge.
        Layout.fillWidth: true
        Layout.maximumWidth: AppMetrics.followsSystem ? Kirigami.Units.gridUnit * 10 : AppMetrics.fieldWidth
        Layout.minimumWidth: Kirigami.Units.gridUnit * 4
        // **"Manual" is an entry, not a blank.** A box that was empty when
        // the values matched nothing would look like a box that had not
        // loaded yet.
        model: [{
            id: "",
            name: footer.manualText
        }].concat(footer.presets)
        textRole: "name"
        valueRole: "id"
        Accessible.name: KI18n.i18nc("@label:listbox", "Preset")
        // **Choosing one applies it**, as one undoable change. Choosing
        // "Manual" does nothing: there is nothing to set a block to when the
        // answer is "no preset", and undo is how somebody goes back.
        onActivated: if (chooser.currentValue !== "") {
            Presets.apply(AppShell.sessionId, footer.blockId, chooser.currentValue);
        }

        SessionBound {
            control: chooser
            property: "currentIndex"
            value: Math.max(0, chooser.model.findIndex(entry => entry.id === footer.shownId))
        }
    }

    QQC2.ToolButton {
        id: menuButton

        // A block with nothing to set and no presets has nothing to put in
        // the menu.
        visible: footer.showsPresets || footer.resettable.length > 0
        icon.name: "overflow-menu"
        display: QQC2.AbstractButton.IconOnly
        Accessible.name: footer.showsPresets
            ? KI18n.i18nc("@action:button", "Preset actions")
            : KI18n.i18nc("@action:button the menu of a block's settings, for a block with no presets", "Settings actions")
        QQC2.ToolTip.text: Accessible.name
        QQC2.ToolTip.visible: hovered
        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
        onClicked: (footer.showsPresets ? actions : settingsActions).popup()

        // The prototype's 26-pixel square; the desktop style's own size
        // while following the system.
        Binding on implicitWidth {
            when: !AppMetrics.followsSystem
            value: AppMetrics.mediumControlHeight
        }
        Binding on implicitHeight {
            when: !AppMetrics.followsSystem
            value: AppMetrics.mediumControlHeight
        }

        QQC2.Menu {
            id: actions

            QQC2.MenuItem {
                text: KI18n.i18nc("@action:inmenu save the block's settings as a new preset", "Save as Preset…")
                icon.name: "document-save-as"
                onTriggered: saveDialog.open()
            }

            QQC2.MenuItem {
                text: KI18n.i18nc("@action:inmenu", "Rename Preset…")
                icon.name: "edit-rename"
                // **Only one of the person's own.** A preset that ships with
                // the application has no file and no name of its own to
                // change.
                enabled: footer.shownId !== "" && !footer.isFactory
                onTriggered: renameDialog.open()
            }

            QQC2.MenuItem {
                text: KI18n.i18nc("@action:inmenu", "Delete Preset…")
                icon.name: "edit-delete"
                enabled: footer.shownId !== "" && !footer.isFactory
                onTriggered: deleteDialog.open()
            }

            QQC2.MenuSeparator {}

            QQC2.MenuItem {
                text: KI18n.i18nc("@action:inmenu open the preset's file in the file manager",
                                  "Show in File Manager")
                icon.name: "document-open-folder"
                enabled: footer.shownId !== "" && !footer.isFactory
                // **The file manager, not a file dialog.** A preset is a
                // small JSON file somebody may want to copy, send or keep in
                // version control, and the point of showing it is to put
                // them where it is.
                onTriggered: Qt.openUrlExternally(Presets.fileOf(footer.typeId, footer.shownId))
            }

            // **Only for a block that has settings to put back**, which a
            // plugin block is not (see resettable). A Menu lays out every
            // entry it holds, seen or not, so the rule and the entry come
            // and go with an Instantiator rather than a `visible`.
            Instantiator {
                model: footer.resettable.length > 0 ? 1 : 0
                delegate: QQC2.MenuSeparator {}
                onObjectAdded: (index, object) => actions.insertItem(actions.count, object)
                onObjectRemoved: (index, object) => actions.removeItem(object)
            }

            Instantiator {
                model: footer.resettable.length > 0 ? 1 : 0
                delegate: QQC2.MenuItem {
                    action: resetAction
                }
                onObjectAdded: (index, object) => actions.insertItem(actions.count, object)
                onObjectRemoved: (index, object) => actions.removeItem(object)
            }
        }

        // **The menu of a block with no presets**: the reset alone.
        QQC2.Menu {
            id: settingsActions

            QQC2.MenuItem {
                action: resetAction
            }
        }
    }

    /// Putting the block's settings back to their defaults, from either
    /// menu. It asks first.
    QQC2.Action {
        id: resetAction

        text: KI18n.i18nc("@action:inmenu put every setting of this block back to how a new block starts",
                          "Reset to Default Settings…")
        icon.name: "edit-reset"
        // Offered whenever the block has settings; there is only something
        // to do once one of them has been changed.
        enabled: footer.changedFromDefaults.length > 0
        onTriggered: resetDialog.open()
    }

    AppDialog {
        id: saveDialog

        /// Where the name is typed, for a test.
        readonly property alias nameField: saveName
        /// Set when the first attempt was refused because something of that
        /// name is there already, so that pressing Save again replaces it.
        property bool replacing: false

        // **Centred on the window, not on this row.** A dialog parented to
        // the footer would be laid out against a strip a few pixels tall.
        parent: QQC2.Overlay.overlay
        title: KI18n.i18nc("@title:window", "Save as Preset")
        preferredWidth: Kirigami.Units.gridUnit * 22
        standardButtons: Kirigami.Dialog.Cancel | Kirigami.Dialog.Ok
        onOpened: {
            saveDialog.replacing = false;
            // Starts at what it is showing, so that saving a small change to
            // a preset of one's own is a matter of pressing Return.
            saveName.text = footer.isFactory ? "" : Presets.nameOf(footer.typeId, footer.shownId);
            saveName.selectAll();
            saveName.forceActiveFocus();
        }
        onAccepted: Presets.saveAs(AppShell.sessionId, footer.blockId, saveName.text, saveDialog.replacing)

        ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            QQC2.TextField {
                id: saveName

                Layout.fillWidth: true
                implicitWidth: Kirigami.Units.gridUnit * 20
                Accessible.name: KI18n.i18nc("@label:textbox", "Preset name")
                onAccepted: saveDialog.accept()
            }

            QQC2.Label {
                Layout.fillWidth: true
                // **Says what pressing Save will do to what is already
                // there.** A dialog that silently wrote over somebody's
                // preset would be a dialog they stopped trusting.
                text: saveDialog.replacing
                    ? KI18n.i18nc("@info", "“%1” will be replaced.", saveName.text)
                    : KI18n.i18nc("@info", "Saved settings for this kind of block, to use in any session.")
                color: saveDialog.replacing ? AppColors.warningText : AppColors.muted
                wrapMode: Text.WordWrap
            }
        }
    }

    AppDialog {
        id: renameDialog

        readonly property alias nameField: renameName

        // **Centred on the window, not on this row.** A dialog parented to
        // the footer would be laid out against a strip a few pixels tall.
        parent: QQC2.Overlay.overlay
        title: KI18n.i18nc("@title:window", "Rename Preset")
        preferredWidth: Kirigami.Units.gridUnit * 22
        standardButtons: Kirigami.Dialog.Cancel | Kirigami.Dialog.Ok
        onOpened: {
            renameName.text = Presets.nameOf(footer.typeId, footer.shownId);
            renameName.selectAll();
            renameName.forceActiveFocus();
        }
        onAccepted: Presets.rename(footer.typeId, footer.shownId, renameName.text)

        QQC2.TextField {
            id: renameName

            implicitWidth: Kirigami.Units.gridUnit * 20
            Accessible.name: KI18n.i18nc("@label:textbox", "Preset name")
            onAccepted: renameDialog.accept()
        }
    }

    AppPromptDialog {
        id: deleteDialog

        // **Centred on the window, not on this row.** A dialog parented to
        // the footer would be laid out against a strip a few pixels tall.
        parent: QQC2.Overlay.overlay
        title: KI18n.i18nc("@title:window", "Delete Preset")
        subtitle: KI18n.i18nc("@info", "Delete the preset “%1”? This cannot be undone.",
                              Presets.nameOf(footer.typeId, footer.shownId))
        standardButtons: Kirigami.Dialog.Cancel
        customFooterActions: [
            Kirigami.Action {
                text: KI18n.i18nc("@action:button", "Delete")
                icon.name: "edit-delete"
                onTriggered: {
                    Presets.remove(footer.typeId, footer.shownId);
                    deleteDialog.close();
                }
            }
        ]
    }

    // **A reset asks first**, as deleting a preset does, though one undo
    // takes it back: it changes every setting at once, and somebody who
    // picked the entry by mistake should not have to work out what moved.
    AppPromptDialog {
        id: resetDialog

        // **Centred on the window, not on this row.**
        parent: QQC2.Overlay.overlay
        title: KI18n.i18nc("@title:window", "Reset to Default Settings")
        subtitle: KI18n.i18nc("@info %1 is the block's name",
                              "Put every setting of “%1” back to how a new block starts? Undo takes it back.",
                              (SessionModel.revision >= 0 ? SessionModel.block(footer.blockId).nickname ?? "" : "")
                              || BlockPresentation.name(footer.typeId))
        standardButtons: Kirigami.Dialog.Cancel
        customFooterActions: [
            Kirigami.Action {
                text: KI18n.i18nc("@action:button put the block's settings back to their defaults", "Reset")
                icon.name: "edit-reset"
                onTriggered: {
                    SessionModel.resetParams(footer.blockId, footer.resettable);
                    resetDialog.close();
                }
            }
        ]
    }

    // **Saving onto a preset is refused once and offered again.** The engine
    // answers alreadyExists rather than writing over one, so the dialog says
    // what replacing it would do and the next Save is the one that does it.
    Connections {
        target: Presets

        function onRefused(presetId, reason) {
            if (reason === "alreadyExists") {
                saveDialog.replacing = true;
                saveDialog.open();
            }
        }
    }
}
