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
// from, and what can be done about it.
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
RowLayout {
    id: footer

    /// The block whose settings these are.
    required property string blockId

    readonly property string typeId: SessionModel.blockType(footer.blockId)
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
        text: KI18n.i18nc("@label saved settings for this block", "Presets:")
        color: AppColors.muted
    }

    QQC2.ComboBox {
        id: chooser

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

        icon.name: "overflow-menu"
        display: QQC2.AbstractButton.IconOnly
        Accessible.name: KI18n.i18nc("@action:button", "Preset actions")
        QQC2.ToolTip.text: Accessible.name
        QQC2.ToolTip.visible: hovered
        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
        onClicked: actions.popup()

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
        }
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
