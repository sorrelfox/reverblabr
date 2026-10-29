// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "translate.h"

#include <libintl.h>

#include <clocale>
#include <cstdlib>
#include <string>

namespace rb::ctl
{

void setUpTranslations()
{
    std::setlocale(LC_ALL, "");
    const char *overridden = std::getenv("REVERBLABR_CTL_LOCALEDIR");
    const char *directory = overridden != nullptr && *overridden != '\0' ? overridden : RB_CTL_LOCALEDIR;
    bindtextdomain(kTextDomain, directory);
    bind_textdomain_codeset(kTextDomain, "UTF-8");
}

namespace
{

/// gettext's key for a message with a context: the context, EOT, the text.
[[nodiscard]] std::string keyFor(const char *context, const char *text)
{
    std::string key(context);
    key += '\004';
    key += text;
    return key;
}

} // namespace

QString ctlTr(const char *context, const char *text)
{
    const std::string key = keyFor(context, text);
    const char *translated = dgettext(kTextDomain, key.c_str());
    // **gettext hands back the key it was given when it has nothing**, and
    // the key here carries the context: that is the untranslated text.
    return QString::fromUtf8(translated == key.c_str() ? text : translated);
}

QString ctlTrn(const char *context, const char *singular, const char *plural, unsigned long count)
{
    const std::string key = keyFor(context, singular);
    const char *translated = dngettext(kTextDomain, key.c_str(), plural, count);
    if (translated == key.c_str()) {
        return QString::fromUtf8(singular);
    }
    return QString::fromUtf8(translated);
}

} // namespace rb::ctl
