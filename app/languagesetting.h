// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <KSharedConfig>
#include <QObject>
#include <QProcessEnvironment>
#include <QQmlEngine>
#include <QStringList>
#include <QVariantList>

#include <optional>

namespace rb::ui
{

/// **The language Reverblabr speaks** ([ADR-0036]): the desktop's, which is
/// the default, English, or any language Reverblabr has a catalog for. The
/// Settings window's Language.
///
/// **In the settings file** (`reverblabrrc`, notify::languageSetting), which
/// the window reads as it starts, before Qt does (main()), and the daemon
/// reads as it starts and whenever it changes. **The window takes it at its
/// next start**: much of what it says is worked out once, and a window
/// switched while it is open would speak two languages at once. So this
/// says which language is in force, and whether a restart is what it takes.
class LanguageSetting : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// **What there is to choose, after following the system**: English,
    /// then every language with a catalog, each `{code, name}`, named in its
    /// own language and in the order a person reading this one would look
    /// for them.
    Q_PROPERTY(QVariantList languages READ languages CONSTANT)
    /// The language chosen: a code of languages, or empty to follow the
    /// system.
    Q_PROPERTY(QString chosen READ chosen WRITE setChosen NOTIFY chosenChanged)
    /// What following the system gives: the code of the first of the
    /// desktop's languages Reverblabr has a catalog for, or `en`.
    Q_PROPERTY(QString systemLanguage READ systemLanguage CONSTANT)
    /// That language's name, in that language.
    Q_PROPERTY(QString systemLanguageName READ systemLanguageName CONSTANT)
    /// The language this window has spoken since it started, as a code.
    Q_PROPERTY(QString inForce READ inForce CONSTANT)
    /// Whether the language chosen is another than the one in force, which
    /// only a restart brings.
    Q_PROPERTY(bool restartNeeded READ restartNeeded NOTIFY chosenChanged)

public:
    explicit LanguageSetting(QObject *parent = nullptr);

    [[nodiscard]] QVariantList languages() const;
    [[nodiscard]] QString chosen() const;
    void setChosen(const QString &code);
    [[nodiscard]] QString systemLanguage() const;
    [[nodiscard]] QString systemLanguageName() const;
    [[nodiscard]] QString inForce() const;
    [[nodiscard]] bool restartNeeded() const;

    /// **A language's name in that language**, as a person who reads it
    /// looks for it in a list: "Deutsch", "Português (Brasil)", "简体中文
    /// (中国)". The code itself for one QLocale does not know.
    [[nodiscard]] static QString nameOf(const QString &code);

    /// **What following the system gives**, among \a catalogs: the first
    /// language of \a environment that has one, in the order KI18n reads
    /// them (LANGUAGE's list, then LC_ALL, LC_MESSAGES and LANG), each as
    /// itself and then without its territory; `en` once English comes first,
    /// or when none has a catalog. English too where the locale is C, in
    /// which gettext translates nothing.
    [[nodiscard]] static QString systemLanguageAmong(const QStringList &catalogs,
                                                     const QProcessEnvironment &environment);

    /// Where to read and write. A test gives its own, so that running the
    /// tests does not reach into whoever is running them.
    static void setConfig(const KSharedConfig::Ptr &config);

    /// **The languages with a catalog, as a test says they are**, rather
    /// than as the installed catalogs say. Nothing for the real ones.
    static void setCatalogsForTesting(const std::optional<QStringList> &codes);

Q_SIGNALS:
    void chosenChanged();

private:
    /// The language a choice gives: \a code, or the system's for none.
    [[nodiscard]] QString effective(const QString &code) const;

    QStringList m_catalogs;
    QVariantList m_languages;
    QString m_chosen;
    QString m_systemLanguage;
    QString m_inForce;
};

} // namespace rb::ui
