// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "texts.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <libintl.h>

using namespace Qt::StringLiterals;

namespace rb::notify
{

namespace
{

constexpr char kDomain[] = "reverblabr";

} // namespace

QString translate(const Message &message)
{
    // **How gettext keeps a context**: "context", EOT, "text" is the msgid
    // pgettext looks up, and the one KI18n's i18nc() looks up.
    QByteArray key(message.context);
    key.append('\004');
    key.append(message.text);
    const char *translated = dgettext(kDomain, key.constData());
    if (translated == key.constData()) {
        return QString::fromUtf8(message.text);
    }
    return QString::fromUtf8(translated);
}

void bindCatalog()
{
    const QStringList folders =
        QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, u"locale"_s, QStandardPaths::LocateDirectory);
    for (const QString &folder : folders) {
        const QDir locale(folder);
        for (const QString &language : locale.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (locale.exists(language + u"/LC_MESSAGES/reverblabr.mo"_s)) {
                bindtextdomain(kDomain, QFile::encodeName(folder).constData());
                bind_textdomain_codeset(kDomain, "UTF-8");
                return;
            }
        }
    }
}

} // namespace rb::notify
