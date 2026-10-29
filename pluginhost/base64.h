// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <optional>
#include <string>

namespace rb::pluginhost
{

/// **Base64, for a plugin's own state in a session file**: the bytes a
/// CLAP or a VST3 plugin keeps, as text. Reading skips white space and the
/// padding; nothing when \a text has a character base64 has not.
[[nodiscard]] std::string toBase64(const std::string &bytes);
[[nodiscard]] std::optional<std::string> fromBase64(const std::string &text);

} // namespace rb::pluginhost
