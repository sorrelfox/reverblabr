// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

// **How the Kirigami platform plugin hears the design's colours**.
//
// The plugin (app/kirigami) is loaded by Kirigami from a file of its own, so
// it cannot reach `rb::ui::Appearance` or the QML tokens. What it can reach
// is the application object. While the Reverblabr control style is in use,
// `Appearance::setDesignColors()` leaves the token colours there, as a
// property holding a `QVariantMap` of `AppColors` names to colours, and
// then sets the application palette, whose change is what tells every
// Kirigami theme to read them again. With the property empty or absent the
// plugin makes its colours from the application palette alone. Two entries
// are not colours: `monoFamily`, the fixed-width face, and `cornerRadius`,
// the corner Kirigami rounds its dialogs and messages by.
namespace rb::ui
{

/// The application object's property holding the token colours.
inline constexpr char kDesignColorsProperty[] = "reverblabrDesignColors";

} // namespace rb::ui
