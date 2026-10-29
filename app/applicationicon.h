// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QIcon>
#include <QLatin1StringView>

namespace rb::ui
{

/// **The application's icon, by name**: the desktop entry's `Icon=`,
/// installed into the hicolor theme under the application's id, so the
/// launcher, the task manager, the tray and the window all show one picture.
inline constexpr QLatin1StringView kApplicationIconName{"io.github.reverblabr.Reverblabr"};

/// Whether the icon theme has the application's icon: once Reverblabr is
/// installed.
[[nodiscard]] bool applicationIconInstalled();

/// The application's icon: the theme's, once Reverblabr is installed, and
/// otherwise the copy built into the application, so a build that has not
/// been installed has it too.
[[nodiscard]] QIcon applicationIcon();

} // namespace rb::ui
