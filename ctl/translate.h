// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QString>

namespace rb::ctl
{

/// The command-line tool's own catalog. It is not the window's `reverblabr`:
/// the window translates through KI18n, which the tool does not link, so the
/// tool's words are extracted into a template of their own
/// (`reverblabr-ctl.pot`, see Messages.sh) and read with plain gettext.
inline constexpr char kTextDomain[] = "reverblabr-ctl";

/// Binds the catalog: to `$REVERBLABR_CTL_LOCALEDIR` when it is set (the
/// tests' own catalog), otherwise to where the build installs catalogs.
void setUpTranslations();

/// One message for a person, in their language. The context is one of
/// KDE's markers (`@info`, `@info:usage`, …), as the window's messages
/// carry, so the two templates read alike to a translator.
[[nodiscard]] QString ctlTr(const char *context, const char *text);

/// The same with a count, for the language's plural forms.
[[nodiscard]] QString ctlTrn(const char *context, const char *singular, const char *plural, unsigned long count);

} // namespace rb::ctl
