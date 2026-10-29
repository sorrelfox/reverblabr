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

// A plugin block's settings: what its host is doing, a Restart
// for one that crashed or stopped responding, which plugin it is, what a
// mono plugin does on a stereo lane, and every setting the plugin has, by
// the plugin's own names for them.
//
// **The settings keep working while the plugin is down.** They are the
// engine's, not the host's: what is set while the sound goes around the
// plugin is what the plugin gets back when it is restarted.
ColumnLayout {
    id: panel

    /// The block whose settings these are, set once the panel exists.
    property string blockId: ""

    /// What the block publishes while the session runs; a test sets it.
    property var published: Meters.frame >= 0 && Meters.live ? (Meters.levels(panel.blockId).values ?? []) : []
    readonly property int health: Math.round(panel.published[0] ?? 0)
    readonly property bool failed: PluginWords.failed(panel.health)

    readonly property alias statusLabel: statusText
    readonly property alias restartButton: restart
    readonly property alias pluginChooser: chooser
    readonly property alias monoChooser: monoChoice
    readonly property alias paramRows: rows
    readonly property alias windowButton: windowToggle
    readonly property alias windowNote: windowText
    readonly property alias pluginPresetChooser: pluginPresets
    readonly property alias savePluginPresetButton: savePluginPreset
    readonly property alias savePluginPresetDialog: saveDialog

    /// **The plugin's own window and presets**: only a plugin of a format
    /// that has them, running in its host.
    readonly property bool running: panel.health === 2
    readonly property bool hasOwnSide: (panel.format === "lv2" || panel.format === "clap" || panel.format === "vst3")
        && panel.pluginId !== ""
    /// **Whether its presets are files of its own format**, which other
    /// programs read too, so that one can be saved as one of them.
    readonly property bool savesOwnPresets: panel.format === "lv2" || panel.format === "vst3"
    readonly property bool windowOpen: SessionModel.pluginRevision >= 0 && SessionModel.pluginWindowOpen(panel.blockId)
    readonly property string windowReason: SessionModel.pluginRevision >= 0
        ? SessionModel.pluginWindowReason(panel.blockId) : ""
    readonly property var ownPresets: SessionModel.pluginRevision >= 0 ? SessionModel.pluginPresets(panel.blockId) : []
    readonly property string shownPreset: SessionModel.pluginRevision >= 0
        ? SessionModel.pluginPresetShown(panel.blockId) : ""

    /// What to say of a window that is not showing, by the engine's reason.
    function windowWords(reason: string): string {
        switch (reason) {
        case "unavailable":
            return KI18n.i18nc("@info", "This plugin has no window that can be shown here");
        case "failed":
            return KI18n.i18nc("@info", "The plugin couldn't make its window");
        case "crashed":
            return KI18n.i18nc("@info", "The plugin's window closed when the plugin stopped");
        default:
            return "";
        }
    }

    // **Asked again whenever the host is running anew**: a plugin chosen,
    // a session started, a host restarted.
    onRunningChanged: if (panel.running && panel.hasOwnSide) {
        SessionModel.listPluginPresets(panel.blockId);
    }
    Component.onCompleted: if (panel.running && panel.hasOwnSide) {
        SessionModel.listPluginPresets(panel.blockId);
    }

    readonly property var values: SessionModel.revision >= 0 ? SessionModel.params(panel.blockId) : ({})
    /// The block's type, and so the plugin's format: `plugin.<format>`.
    readonly property string blockType: SessionModel.revision >= 0 ? (SessionModel.block(panel.blockId).type ?? "") : ""
    readonly property string format: panel.blockType.substring(panel.blockType.indexOf(".") + 1)
    readonly property string pluginId: panel.values.plugin
        ?? (BlockCatalogue.describeParam(panel.blockType, "plugin")["default"] ?? "")
    /// The plugins of this format there are to choose from.
    readonly property var choosable: InstalledPlugins.ready ? InstalledPlugins.pluginsOf(panel.format) : []
    /// What the chosen plugin is, as the scan found it: its channels.
    readonly property var chosen: InstalledPlugins.ready ? InstalledPlugins.plugin(panel.format, panel.pluginId) : ({})
    /// **The plugin's settings, by its own names for them**, made
    /// into what a parameter row reads.
    readonly property var descriptors: {
        const params = InstalledPlugins.ready ? InstalledPlugins.params(panel.format, panel.pluginId) : [];
        return params.map(param => panel.descriptorOf(param));
    }

    /// A plugin's setting as the scan describes it, as a row's descriptor:
    /// the kinds a block's parameters have, and a choice's ids the values
    /// the plugin block uses for them.
    function descriptorOf(param: var): var {
        const kind = param.kind ?? "float";
        const descriptor = {
            id: param.id,
            label: param.name ?? param.id,
            minimum: param.min ?? 0,
            maximum: param.max ?? 1,
            taper: (param.log ?? false) && (param.min ?? 0) > 0 ? "log" : "linear"
        };
        if (kind === "toggle") {
            descriptor.type = "bool";
            descriptor.default = (param.default ?? 0) >= 0.5;
        } else if (kind === "choice") {
            descriptor.type = "enum";
            const choices = param.choices ?? [];
            descriptor.choices = choices.map(choice => String(choice.value));
            descriptor.choiceLabels = choices.map(choice => choice.label);
            // **The choice nearest the plugin's default**, as the plugin
            // block takes it: a CLAP plugin's default can fall between
            // them.
            const fallback = param.default ?? 0;
            let nearest = choices.length > 0 ? choices[0].value : fallback;
            for (const choice of choices) {
                if (Math.abs(choice.value - fallback) < Math.abs(nearest - fallback)) {
                    nearest = choice.value;
                }
            }
            descriptor.default = String(nearest);
        } else {
            descriptor.type = kind === "int" ? "int" : "float";
            descriptor.default = param.default ?? 0;
        }
        return descriptor;
    }

    spacing: AppMetrics.panelRowGap

    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        Kirigami.Icon {
            visible: panel.failed
            source: "dialog-error"
            implicitWidth: Kirigami.Units.iconSizes.small
            implicitHeight: Kirigami.Units.iconSizes.small
            Accessible.ignored: true
        }

        QQC2.Label {
            id: statusText

            text: panel.failed ? PluginWords.explanation(panel.health) : PluginWords.status(panel.health)
            color: panel.failed ? AppColors.dangerText : AppColors.muted
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        QQC2.Button {
            id: restart

            visible: panel.failed
            icon.name: "view-refresh"
            text: KI18n.i18nc("@action:button start a plugin that crashed again", "Restart")
            onClicked: SessionModel.restartPlugin(panel.blockId)
        }
    }

    // --- which plugin ---

    QQC2.ComboBox {
        id: chooser

        Layout.fillWidth: true
        visible: panel.choosable.length > 0
        model: panel.choosable.map(plugin => plugin.name)
        displayText: panel.pluginId === ""
            ? KI18n.i18nc("@item:inlistbox no plugin has been chosen for a plugin block", "Choose a plugin…")
            : (panel.chosen.name ?? panel.pluginId)
        Accessible.name: KI18n.i18nc("@label:listbox which plugin a plugin block runs", "Plugin")
        // **Another plugin is an edit to the session's shape**, sent once,
        // as a choice is.
        onActivated: index => SessionModel.setParam(panel.blockId, "plugin", panel.choosable[index].id)
    }

    // --- the plugin's own window ---

    RowLayout {
        Layout.fillWidth: true
        visible: panel.hasOwnSide
        spacing: Kirigami.Units.smallSpacing

        QQC2.Button {
            id: windowToggle

            enabled: panel.running
            checkable: true
            checked: panel.windowOpen
            icon.name: "window"
            text: KI18n.i18nc("@action:button show the plugin's own window", "Plugin Window")
            Accessible.description: KI18n.i18nc("@info:tooltip", "Shows the plugin's own controls in a window of their own")
            // **The answer, not the click, says whether it is open**: the
            // engine says so once the host has shown it, or why it could not.
            onClicked: {
                windowToggle.checked = Qt.binding(() => panel.windowOpen);
                SessionModel.setPluginWindow(panel.blockId, !panel.windowOpen);
            }
        }

        QQC2.Label {
            id: windowText

            Layout.fillWidth: true
            visible: !panel.windowOpen && text !== ""
            wrapMode: Text.WordWrap
            color: AppColors.muted
            text: panel.windowWords(panel.windowReason)
        }
    }

    // --- the plugin's own presets ---

    RowLayout {
        Layout.fillWidth: true
        visible: panel.hasOwnSide && panel.running && (panel.ownPresets.length > 0 || panel.savesOwnPresets)
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            text: KI18n.i18nc("@label the presets that come with a plugin, or were saved for it", "Plugin presets:")
            color: AppColors.muted
        }

        QQC2.ComboBox {
            id: pluginPresets

            Layout.fillWidth: true
            // "Manual" is an entry, as in the footer: the settings are none
            // of the plugin's presets.
            model: [{
                id: "",
                name: KI18n.i18nc("@item:inlistbox the plugin's settings match none of its presets", "Manual")
            }].concat(panel.ownPresets)
            textRole: "name"
            valueRole: "id"
            currentIndex: Math.max(0, pluginPresets.model.findIndex(entry => entry.id === panel.shownPreset))
            Accessible.name: KI18n.i18nc("@label:listbox", "Plugin preset")
            // One edit, which undo takes back.
            onActivated: index => {
                if (index > 0) {
                    SessionModel.loadPluginPreset(panel.blockId, pluginPresets.model[index].id);
                }
            }
        }

        QQC2.Button {
            id: savePluginPreset

            // **Where the plugin's other hosts find it**: an LV2 plugin's
            // presets are its own files, and so are a VST3 plugin's. A CLAP
            // plugin's settings are kept with ours, in the footer's presets.
            visible: panel.savesOwnPresets
            icon.name: "document-save"
            text: KI18n.i18nc("@action:button keep the plugin's settings as one of its own presets", "Save…")
            onClicked: saveDialog.open()
        }
    }

    AppDialog {
        id: saveDialog

        readonly property alias nameField: saveName

        parent: QQC2.Overlay.overlay
        title: KI18n.i18nc("@title:window", "Save Plugin Preset")
        preferredWidth: Kirigami.Units.gridUnit * 22
        standardButtons: Kirigami.Dialog.Cancel | Kirigami.Dialog.Ok
        onOpened: {
            saveName.text = "";
            saveName.forceActiveFocus();
        }
        onAccepted: SessionModel.savePluginPreset(panel.blockId, saveName.text)

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
                text: KI18n.i18nc("@info", "Saved as the plugin's own preset, which other programs that use this plugin can open too.")
                color: AppColors.muted
                wrapMode: Text.WordWrap
            }
        }
    }

    // --- what a mono plugin does on a stereo lane ---

    QQC2.ComboBox {
        id: monoChoice

        Layout.fillWidth: true
        visible: (panel.chosen.audioInputs ?? 2) === 1
        model: [KI18n.i18nc("@item:inlistbox a mono plugin on a stereo lane runs once a side", "Once a side (dual mono)"),
                KI18n.i18nc("@item:inlistbox a mono plugin on a stereo lane hears the two sides mixed", "On the sides mixed (downmix)")]
        currentIndex: (panel.values.monoMode ?? "dualMono") === "downmix" ? 1 : 0
        Accessible.name: KI18n.i18nc("@label:listbox", "Mono plugin on a stereo lane")
        onActivated: index => SessionModel.setParam(panel.blockId, "monoMode", index === 1 ? "downmix" : "dualMono")
    }

    // --- the plugin's own settings ---

    ColumnLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        Repeater {
            id: rows

            model: panel.descriptors

            delegate: ParamRow {
                required property var modelData

                blockId: panel.blockId
                descriptor: modelData
                label: modelData.label
                currentValue: panel.values[modelData.id]
                Layout.fillWidth: true
            }
        }
    }

    QQC2.Label {
        Layout.fillWidth: true
        visible: panel.pluginId !== "" && panel.descriptors.length === 0
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        color: AppColors.muted
        text: KI18n.i18nc("@info:placeholder a plugin whose settings are not known on this machine",
                          "This plugin's settings are not known here yet")
    }
}
