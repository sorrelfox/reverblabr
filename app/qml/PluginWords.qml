// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma Singleton

import QtQuick
import org.kde.ki18n

// What a plugin block's host is doing, in words. The engine publishes
// a number (core::PluginHealth): 0 not hosted, 1 starting, 2 running,
// 3 crashed, 4 stopped responding.
QtObject {
    /// A format as it is written: "LV2", "CLAP", "LADSPA", "Built-in".
    function formatName(format: string): string {
        switch (format) {
        case "lv2":
            return KI18n.i18nc("@label a plugin format", "LV2");
        case "clap":
            return KI18n.i18nc("@label a plugin format", "CLAP");
        case "ladspa":
            return KI18n.i18nc("@label a plugin format", "LADSPA");
        case "vst3":
            return KI18n.i18nc("@label a plugin format", "VST3");
        case "builtin":
            return KI18n.i18nc("@label the plugins built into Reverblabr, as a format", "Built-in");
        default:
            return format.toUpperCase();
        }
    }

    /// The longer form, for a library entry's tooltip.
    function pluginTooltip(plugin: var): string {
        const channels = plugin.audioInputs === plugin.audioOutputs
            ? KI18n.i18ncp("@info a plugin's channels, the same in and out", "%1 channel", "%1 channels",
                           plugin.audioInputs)
            : KI18n.i18nc("@info a plugin's channels in and out, '1 in, 2 out'", "%1 in, %2 out",
                          plugin.audioInputs, plugin.audioOutputs);
        return plugin.category
            ? KI18n.i18nc("@info:tooltip a plugin: its kind, who made it, its format and its channels",
                          "%1 by %2, %3, %4", plugin.category, plugin.vendor, formatName(plugin.format), channels)
            : KI18n.i18nc("@info:tooltip a plugin: who made it, its format and its channels",
                          "By %1, %2, %3", plugin.vendor, formatName(plugin.format), channels);
    }

    function failed(health: int): bool {
        return health === 3 || health === 4;
    }

    /// One line, for a face and a panel's header.
    function status(health: int): string {
        switch (health) {
        case 1:
            return KI18n.i18nc("@info:status a plugin's host is starting", "Starting…");
        case 2:
            return KI18n.i18nc("@info:status a plugin is running in its host", "Running");
        case 3:
            return KI18n.i18nc("@info:status a plugin's host crashed", "Plugin crashed");
        case 4:
            return KI18n.i18nc("@info:status a plugin's host stopped responding", "Plugin stopped responding");
        default:
            return KI18n.i18nc("@info:status a plugin that is not running", "Not running");
        }
    }

    /// What it means, for the panel of a plugin that is down.
    function explanation(health: int): string {
        return health === 4
            ? KI18n.i18nc("@info a plugin's host stopped responding",
                          "Plugin stopped responding. It was closed, and the sound goes around it until you restart it.")
            : KI18n.i18nc("@info a plugin's host crashed",
                          "Plugin crashed. The sound goes around it until you restart it.");
    }
}
