// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "languagesetting.h"

#include "language.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <QCollator>
#include <QHash>
#include <QLocale>
#include <QVariantMap>

#include <algorithm>
#include <utility>
#include <vector>

using namespace Qt::StringLiterals;

namespace rb::ui
{

namespace
{

KSharedConfig::Ptr g_config;
std::optional<QStringList> g_catalogs;

KConfigGroup group()
{
    const KSharedConfig::Ptr config = g_config ? g_config : KSharedConfig::openConfig();
    return KConfigGroup(config, QLatin1StringView(notify::languageSetting::group));
}

/// The languages with a catalog of the window's domain, wherever KI18n finds
/// them (the data folders, so a build tree's own too, docs/i18n.md).
QStringList installedCatalogs()
{
    if (g_catalogs) {
        return *g_catalogs;
    }
    QStringList codes;
    const QSet<QString> found = KLocalizedString::availableDomainTranslations("reverblabr");
    for (const QString &code : found) {
        // **KI18n counts the words as they are written as a catalog**, by
        // the name en_US: here that is English, offered already.
        if (code != u"en_US"_s && code != QLatin1StringView(notify::languageSetting::english)
            && notify::isLanguageCode(code)) {
            codes.append(code);
        }
    }
    codes.sort();
    return codes;
}

/// **The name most of a language's locales give it.** CLDR names some by
/// their variety (Spanish in Spain is "español de España", English in the
/// United States "American English"), and a catalog for the whole language
/// is called what the rest call it.
QString plainName(const QLocale &locale)
{
    QHash<QString, int> votes;
    const QList<QLocale> all = QLocale::matchingLocales(locale.language(), locale.script(), QLocale::AnyTerritory);
    for (const QLocale &one : all) {
        ++votes[one.nativeLanguageName()];
    }
    QString best = locale.nativeLanguageName();
    int most = votes.value(best);
    for (auto vote = votes.cbegin(); vote != votes.cend(); ++vote) {
        if (vote.value() > most) {
            most = vote.value();
            best = vote.key();
        }
    }
    return best;
}

/// What the language a candidate names is to KI18n: English is where its
/// search stops, since that is what the words are written in.
bool isEnglish(const QString &candidate)
{
    return candidate == QLatin1StringView(notify::languageSetting::english) || candidate == u"en_US"_s;
}

} // namespace

LanguageSetting::LanguageSetting(QObject *parent)
    : QObject(parent)
    , m_catalogs(installedCatalogs())
    , m_systemLanguage(systemLanguageAmong(m_catalogs, notify::ProcessLanguage::originalEnvironment()))
{
    m_inForce = effective(notify::ProcessLanguage::atStart());

    // **English first, then the rest in the reader's order**: sorted by
    // name with the collation of the locale this window runs in.
    std::vector<std::pair<QString, QString>> named;
    named.reserve(m_catalogs.size());
    for (const QString &code : std::as_const(m_catalogs)) {
        named.emplace_back(code, nameOf(code));
    }
    QCollator collator;
    std::sort(named.begin(), named.end(), [&collator](const auto &one, const auto &other) {
        return collator.compare(one.second, other.second) < 0;
    });
    const QString english = QString::fromLatin1(notify::languageSetting::english);
    m_languages.append(QVariantMap{{u"code"_s, english}, {u"name"_s, nameOf(english)}});
    for (const auto &[code, name] : named) {
        m_languages.append(QVariantMap{{u"code"_s, code}, {u"name"_s, name}});
    }

    // **A language whose catalog has gone is following the system**, as
    // main() and the daemon take it (notify::chosenLanguage()).
    const QString stored = group().readEntry(notify::languageSetting::key, QString()).trimmed();
    m_chosen = stored == english || m_catalogs.contains(stored) ? stored : QString();
}

QVariantList LanguageSetting::languages() const
{
    return m_languages;
}

QString LanguageSetting::chosen() const
{
    return m_chosen;
}

void LanguageSetting::setChosen(const QString &code)
{
    // Only what there is to choose: anything else would put a language with
    // no catalog in the file, which every process then ignores.
    if (code == m_chosen
        || (!code.isEmpty() && code != QLatin1StringView(notify::languageSetting::english)
            && !m_catalogs.contains(code))) {
        return;
    }
    m_chosen = code;
    KConfigGroup settings = group();
    // **Nothing in the file to follow the system**, rather than a word for
    // it: a later version can then change what following it means.
    if (code.isEmpty()) {
        settings.deleteEntry(notify::languageSetting::key);
    } else {
        settings.writeEntry(notify::languageSetting::key, code);
    }
    settings.sync();
    Q_EMIT chosenChanged();
}

QString LanguageSetting::systemLanguage() const
{
    return m_systemLanguage;
}

QString LanguageSetting::systemLanguageName() const
{
    return nameOf(m_systemLanguage);
}

QString LanguageSetting::inForce() const
{
    return m_inForce;
}

bool LanguageSetting::restartNeeded() const
{
    return effective(m_chosen) != m_inForce;
}

QString LanguageSetting::effective(const QString &code) const
{
    return code.isEmpty() ? m_systemLanguage : code;
}

QString LanguageSetting::nameOf(const QString &code)
{
    // language[_territory][@modifier], as the catalogs' folders are named.
    QString base = code;
    QString modifier;
    if (const qsizetype at = code.indexOf(u'@'); at >= 0) {
        base = code.left(at);
        modifier = code.mid(at + 1);
    }
    QLocale locale(base);
    if (locale.language() == QLocale::C || locale.language() == QLocale::AnyLanguage) {
        return code;
    }
    // **A modifier that names a script is that script** (Serbian in Latin
    // letters); any other is said after the name.
    if (modifier == "latin"_L1 || modifier == "cyrillic"_L1) {
        locale = QLocale(locale.language(),
                         modifier == "latin"_L1 ? QLocale::LatinScript : QLocale::CyrillicScript,
                         base.contains(u'_') ? locale.territory() : QLocale::AnyTerritory);
        modifier.clear();
    }
    QString name = plainName(locale);
    if (name.isEmpty()) {
        return code;
    }
    // **Capitalised as that language capitalises**: a name begins the line
    // in a list, and "português" is written in lower case only in a sentence.
    const qsizetype first = name.at(0).isHighSurrogate() && name.size() > 1 ? 2 : 1;
    name = locale.toUpper(name.left(first)) + name.mid(first);
    if (base.contains(u'_')) {
        if (const QString territory = locale.nativeTerritoryName(); !territory.isEmpty()) {
            name = u"%1 (%2)"_s.arg(name, territory);
        }
    }
    if (!modifier.isEmpty()) {
        name = u"%1 (%2)"_s.arg(name, modifier);
    }
    return name;
}

QString LanguageSetting::systemLanguageAmong(const QStringList &catalogs, const QProcessEnvironment &environment)
{
    const QString english = QString::fromLatin1(notify::languageSetting::english);
    // **The locale of the words, as gettext takes it**: in C, or with none
    // at all, it translates nothing, whatever LANGUAGE says.
    QStringList locales;
    for (const QString &name : {u"LC_ALL"_s, u"LC_MESSAGES"_s, u"LANG"_s}) {
        if (const QString value = environment.value(name); !value.isEmpty()) {
            locales.append(value);
        }
    }
    const QString messages = locales.value(0);
    if (messages.isEmpty() || messages == "C"_L1 || messages == "POSIX"_L1 || messages.startsWith("C."_L1)) {
        return english;
    }
    QStringList wanted = environment.value(u"LANGUAGE"_s).split(u':', Qt::SkipEmptyParts);
    wanted.append(locales);
    for (const QString &value : std::as_const(wanted)) {
        // language[_territory][.charset][@modifier], as the variables write
        // a locale.
        QString locale = value;
        QString modifier;
        if (const qsizetype at = locale.indexOf(u'@'); at >= 0) {
            modifier = locale.mid(at);
            locale = locale.left(at);
        }
        if (const qsizetype dot = locale.indexOf(u'.'); dot >= 0) {
            locale = locale.left(dot);
        }
        const QString language = locale.section(u'_', 0, 0);
        // **Each as itself, then without its territory**, which is how
        // KI18n looks a locale up: pt_BR before pt.
        QStringList candidates;
        if (!modifier.isEmpty()) {
            candidates << locale + modifier << language + modifier;
        }
        candidates << locale << language;
        for (const QString &candidate : std::as_const(candidates)) {
            if (catalogs.contains(candidate)) {
                return candidate;
            }
            if (isEnglish(candidate)) {
                return english;
            }
        }
    }
    return english;
}

void LanguageSetting::setConfig(const KSharedConfig::Ptr &config)
{
    g_config = config;
}

void LanguageSetting::setCatalogsForTesting(const std::optional<QStringList> &codes)
{
    g_catalogs = codes;
}

} // namespace rb::ui
