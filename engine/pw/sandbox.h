// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QString>

/// Whether Reverblabr runs in a Flatpak, and where the person's own
/// configuration is from inside one (ADR-0034).
namespace rb::pw
{

/// The file Flatpak puts at the root of every sandbox it runs.
inline constexpr auto kFlatpakInfo = "/.flatpak-info";

/// Whether this process runs in a Flatpak: \a marker is there. Tests name
/// another file.
[[nodiscard]] bool inFlatpak(const QString &marker = QString::fromLatin1(kFlatpakInfo));

/// The folder PipeWire reads this user's configuration from. Outside a
/// Flatpak, `$XDG_CONFIG_HOME` or `~/.config`, which is what Qt answers.
/// **Inside one, the host's**, not the sandbox's own: Flatpak points
/// `XDG_CONFIG_HOME` into the app's folder, which PipeWire never reads, and
/// hands the host's value on as `HOST_XDG_CONFIG_HOME` when the host has one.
[[nodiscard]] QString pipeWireConfigHome(bool sandboxed);

} // namespace rb::pw
