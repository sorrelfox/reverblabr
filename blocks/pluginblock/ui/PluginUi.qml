// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick

/// The plugin blocks' interface: the face a tile draws and the panel their
/// settings open in, as BlockRegister lists them (ADR-0035). A plugin's
/// help is its maker's.
BlockUi {
    typeIds: ["plugin.builtin", "plugin.lv2", "plugin.ladspa", "plugin.clap", "plugin.vst3"]

    /// A plugin's face: which plugin, and what its host is doing.
    face: PluginFace {}

    /// A plugin's panel: which plugin, and what its host is doing.
    panel: PluginPanel {}
}
