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
