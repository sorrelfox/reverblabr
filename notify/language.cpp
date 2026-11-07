// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "language.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <libintl.h>

#include <algorithm>
#include <optional>

using namespace Qt::StringLiterals;

#if defined(__GLIBC__)
// **glibc's count of loaded catalogs**: a lookup it has cached is used again
// only while the count is what it was, and the cache does not know which
// language LANGUAGE named. KI18n moves it the same way when it changes
// LANGUAGE for a lookup of its own.
extern "C" int _nl_msg_cat_cntr;
#endif

namespace rb::notify
{

namespace
{

constexpr QByteArrayView kVariable = "LANGUAGE";

/// What applyAtStart() found and was given; nothing is restored for a
/// process that never called it.
bool g_applied = false;
std::optional<QByteArray> g_original;
QString g_atStart;

/// **The value of \a key in \a group of a KConfig file, read by hand.**
/// QSettings keeps what it read of a file for as long as the file's size and
/// time look the same to it, and a language chosen again within a moment, as
/// long a code as the last, went unseen. A group is a line `[group]`; a key
/// is `key=value` in it, trimmed on either side; a key with a locale or a
/// flag after it (`key[de]`, `key[$e]`) is another key. The last one wins.
QString readEntry(const QString &path, QByteArrayView group, QByteArrayView key)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QByteArray header = '[' + group.toByteArray() + ']';
    bool inGroup = false;
    QString value;
    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#')) {
            continue;
        }
        if (line.startsWith('[')) {
            inGroup = line == header;
            continue;
        }
        const qsizetype equals = line.indexOf('=');
        if (inGroup && equals > 0 && line.left(equals).trimmed() == key) {
            value = QString::fromUtf8(line.mid(equals + 1).trimmed());
        }
    }
    return value;
}

} // namespace

QString settingsPath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)).filePath(u"reverblabrrc"_s);
}

bool isLanguageCode(const QString &word)
{
    static const QRegularExpression code(uR"(^[a-z]{2,3}(_([A-Z]{2}|[0-9]{3}))?(@[a-z]+)?$)"_s);
    return code.match(word).hasMatch();
}

bool hasCatalog(const QString &code)
{
    if (!isLanguageCode(code)) {
        return false;
    }
    const QString catalog = code + u"/LC_MESSAGES/reverblabr.mo"_s;
    const QStringList folders =
        QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, u"locale"_s, QStandardPaths::LocateDirectory);
    return std::any_of(folders.cbegin(), folders.cend(), [&catalog](const QString &folder) {
        return QFileInfo::exists(QDir(folder).filePath(catalog));
    });
}

QString chosenLanguage(const QString &settingsPath)
{
    if (settingsPath.isEmpty()) {
        return {};
    }
    const QString code = readEntry(settingsPath, languageSetting::group, languageSetting::key);
    if (code == QLatin1StringView(languageSetting::english)) {
        return code;
    }
    return hasCatalog(code) ? code : QString();
}

void ProcessLanguage::applyAtStart(const QString &code)
{
    g_applied = true;
    g_original = qEnvironmentVariableIsSet(kVariable.data()) ? std::optional<QByteArray>(qgetenv(kVariable.data()))
                                                             : std::nullopt;
    g_atStart = code;
    if (!code.isEmpty()) {
        qputenv(kVariable.data(), code.toLatin1());
    } else if (!g_original) {
        qputenv(kVariable.data(), QByteArray());
    }
}

bool ProcessLanguage::follow(const QString &code)
{
    const QByteArray wanted = code.isEmpty() ? g_original.value_or(QByteArray()) : code.toLatin1();
    if (qEnvironmentVariableIsSet(kVariable.data()) && qgetenv(kVariable.data()) == wanted) {
        return false;
    }
    qputenv(kVariable.data(), wanted);
#if defined(__GLIBC__)
    ++_nl_msg_cat_cntr;
#endif
    return true;
}

QString ProcessLanguage::atStart()
{
    return g_atStart;
}

QProcessEnvironment ProcessLanguage::restored(QProcessEnvironment environment)
{
    if (!g_applied) {
        return environment;
    }
    const QString variable = QString::fromLatin1(kVariable);
    if (g_original) {
        environment.insert(variable, QString::fromLocal8Bit(*g_original));
    } else {
        environment.remove(variable);
    }
    return environment;
}

QProcessEnvironment ProcessLanguage::originalEnvironment()
{
    return restored(QProcessEnvironment::systemEnvironment());
}

} // namespace rb::notify
