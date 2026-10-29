// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "plugin/formats.h"

#include <lilv/lilv.h>

#include <string>
#include <vector>

namespace rb::pluginhost
{

/// The exit code of a scan that could not read its candidate: nothing in it
/// is a plugin this host can describe. Not a crash, so not blocklisted.
inline constexpr int kScanUnreadable = 5;

/// **Describes one candidate**: `reverblabr-plugin-host --scan
/// <format> <path>`. Every plugin in it goes to standard output as a line of
/// JSON (plugin::toJsonLine), and the process exits 0.
///
/// It runs in a process of its own for the reason a plugin does: describing
/// a plugin can mean loading its code, and code that crashes on being
/// loaded takes only this process with it. The engine blocklists that
/// candidate and carries on with the next.
[[nodiscard]] int scan(const std::string &format, const std::string &path);

/// A node's text, or empty.
[[nodiscard]] std::string text(const LilvNode *node);

/// **An LV2 plugin's settings: its control inputs, in port order**.
/// The scan describes a plugin with these and the host numbers the ring's
/// changes by them, so the two always agree which is which.
[[nodiscard]] std::vector<plugin::PluginParam> lv2Params(LilvWorld *world, const LilvPlugin *plugin);

} // namespace rb::pluginhost
