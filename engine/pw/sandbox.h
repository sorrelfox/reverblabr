// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QString>

/// Whether Reverblabr runs in a Flatpak (ADR-0034) or from an AppImage
/// (ADR-0037), and where the person's own configuration is from inside a
/// Flatpak.
namespace rb::pw
{

/// The file Flatpak puts at the root of every sandbox it runs.
inline constexpr auto kFlatpakInfo = "/.flatpak-info";

/// Whether this process runs in a Flatpak: \a marker is there. Tests name
/// another file.
[[nodiscard]] bool inFlatpak(const QString &marker = QString::fromLatin1(kFlatpakInfo));

/// The file the AppImage puts beside its share folder, named from the folder
/// the programs are in (ADR-0037). An installed copy has none.
inline constexpr auto kAppImageMarker = "../share/reverblabr/appimage";

/// Whether the programs in \a programFolder run from an AppImage: the
/// marker is there. **A file of the AppImage's own, not its environment**:
/// `APPIMAGE` and `APPDIR` are inherited by everything an AppImage starts,
/// a packaged Reverblabr among it. An empty \a programFolder means this
/// program's own, which needs the application object.
[[nodiscard]] bool inAppImage(const QString &programFolder = {});

/// The folder PipeWire reads this user's configuration from. Outside a
/// Flatpak, `$XDG_CONFIG_HOME` or `~/.config`, which is what Qt answers.
/// **Inside one, the host's**, not the sandbox's own: Flatpak points
/// `XDG_CONFIG_HOME` into the app's folder, which PipeWire never reads, and
/// hands the host's value on as `HOST_XDG_CONFIG_HOME` when the host has one.
[[nodiscard]] QString pipeWireConfigHome(bool sandboxed);

} // namespace rb::pw
