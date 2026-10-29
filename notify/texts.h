// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QString>

namespace rb::notify
{

/// A string a person reads, with the context a translator needs: what
/// `i18nc(context, text)` would be, where KI18n cannot be used.
struct Message {
    const char *context;
    const char *text;
};

/// **Marks a message for extraction** (docs/i18n.md) without translating it:
/// xgettext is given the keyword `RB_I18NC:1c,2`.
#define RB_I18NC(context, text)                                                                                        \
    ::rb::notify::Message                                                                                              \
    {                                                                                                                  \
        context, text                                                                                                  \
    }

/// **The message in the language the person reads**, from the same
/// `reverblabr` catalog the window's KI18n uses, looked up with plain
/// gettext so that the daemon, which has no KDE Frameworks (ADR-0017), can
/// say it too. The English text when there is no translation.
[[nodiscard]] QString translate(const Message &message);

/// **Points gettext at the catalog**, as KI18n does for the window: the
/// first `locale` folder of the data paths holding a `reverblabr.mo` for any
/// language. For a process that has no KI18n to do it; harmless in one that
/// has.
void bindCatalog();

} // namespace rb::notify
