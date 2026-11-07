// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The language the window speaks (ADR-0036): the Settings window's choice
// and the words it is kept in, which every process reads without KDE
// Frameworks (notify/test_language.cpp holds that side); the languages there
// are to choose, each named in itself; what following the system gives; and
// when a restart is what a choice takes.

#include "language.h"
#include "languagesetting.h"

#include <KConfigGroup>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <initializer_list>
#include <utility>

using namespace rb;
using namespace Qt::StringLiterals;

namespace
{

QCoreApplication &application()
{
    if (QCoreApplication *running = QCoreApplication::instance()) {
        return *running;
    }
    static int argc = 1;
    static char name[] = "reverblabr-app-test";
    static char *argv[] = {name, nullptr};
    static auto *const made = new QCoreApplication(argc, argv);
    return *made;
}

/// **Data folders of the test's own**, with a made-up catalog for each of
/// the languages given and nothing of the machine's: what the window finds
/// is what is installed, wherever KI18n looks.
class Catalogs
{
public:
    explicit Catalogs(const QStringList &codes)
    {
        EXPECT_TRUE(m_data.isValid() && m_home.isValid());
        for (const QString &code : codes) {
            const QString folder = m_data.filePath(u"locale/"_s + code + u"/LC_MESSAGES"_s);
            EXPECT_TRUE(QDir().mkpath(folder));
            // KI18n looks for the file; what is in it is gettext's business.
            QFile catalog(QDir(folder).filePath(u"reverblabr.mo"_s));
            EXPECT_TRUE(catalog.open(QIODevice::WriteOnly));
        }
        qputenv("XDG_DATA_DIRS", QFile::encodeName(m_data.path()));
        qputenv("XDG_DATA_HOME", QFile::encodeName(m_home.path()));
    }

private:
    QTemporaryDir m_data;
    QTemporaryDir m_home;
};

/// A settings file of the test's own, so that running the tests does not
/// reach into whoever is running them.
class SettingsFile
{
public:
    SettingsFile()
    {
        EXPECT_TRUE(m_directory.isValid());
        m_config = KSharedConfig::openConfig(path(), KConfig::SimpleConfig);
        ui::LanguageSetting::setConfig(m_config);
    }

    ~SettingsFile()
    {
        ui::LanguageSetting::setConfig({});
    }

    SettingsFile(const SettingsFile &) = delete;
    SettingsFile &operator=(const SettingsFile &) = delete;

    [[nodiscard]] QString path() const
    {
        return m_directory.filePath(u"reverblabrrc"_s);
    }

    [[nodiscard]] QByteArray text() const
    {
        QFile file(path());
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

    [[nodiscard]] KSharedConfig::Ptr config() const
    {
        return m_config;
    }

private:
    QTemporaryDir m_directory;
    KSharedConfig::Ptr m_config;
};

/// The environment a process was started with, made of \a variables alone.
QProcessEnvironment startedWith(std::initializer_list<std::pair<QString, QString>> variables)
{
    QProcessEnvironment environment;
    for (const auto &[name, value] : variables) {
        environment.insert(name, value);
    }
    return environment;
}

/// The process as if started with \a lang and nothing else that names a
/// language, following the system.
void startedIn(const QByteArray &lang)
{
    for (const char *variable : {"LANGUAGE", "LC_ALL", "LC_MESSAGES"}) {
        qunsetenv(variable);
    }
    qputenv("LANG", lang);
    notify::ProcessLanguage::applyAtStart(QString());
}

} // namespace

TEST(LanguageSettingTest, FollowingTheSystemIsTheDefaultAndAChoiceIsWrittenAsTheDaemonReadsIt)
{
    application();
    const Catalogs catalogs({u"de"_s, u"pt_BR"_s, u"zh_CN"_s});
    const SettingsFile settings;
    {
        ui::LanguageSetting setting;
        EXPECT_TRUE(setting.chosen().isEmpty()) << "a first run does not follow the system";
        const QSignalSpy changed(&setting, &ui::LanguageSetting::chosenChanged);
        setting.setChosen(u"pt_BR"_s);
        setting.setChosen(u"pt_BR"_s);
        EXPECT_EQ(changed.count(), 1);
        // **Only what there is to choose**: a language with no catalog would
        // be one every process ignores.
        setting.setChosen(u"fr"_s);
        EXPECT_EQ(setting.chosen(), u"pt_BR"_s);
        EXPECT_EQ(changed.count(), 1);
    }
    // **The words the daemon reads**, with no KDE Frameworks of its own, and
    // the window before Qt starts.
    EXPECT_TRUE(settings.text().contains("[General]\nLanguage=pt_BR\n")) << settings.text().toStdString();
    EXPECT_EQ(notify::chosenLanguage(settings.path()), u"pt_BR"_s);

    // Remembered; and following the system again leaves no word in the file.
    {
        ui::LanguageSetting setting;
        EXPECT_EQ(setting.chosen(), u"pt_BR"_s);
        setting.setChosen(u"en"_s);
        EXPECT_EQ(notify::chosenLanguage(settings.path()), u"en"_s);
        setting.setChosen(QString());
    }
    settings.config()->reparseConfiguration();
    EXPECT_FALSE(settings.config()->group(u"General"_s).hasKey("Language"));
    EXPECT_TRUE(notify::chosenLanguage(settings.path()).isEmpty());

    // **A language whose catalog has gone is following the system**, here
    // as in every process.
    KConfigGroup group = settings.config()->group(u"General"_s);
    group.writeEntry("Language", u"fr"_s);
    group.sync();
    EXPECT_TRUE(ui::LanguageSetting().chosen().isEmpty());
    EXPECT_TRUE(notify::chosenLanguage(settings.path()).isEmpty());
}

TEST(LanguageSettingTest, EnglishThenEveryCatalogEachNamedInItself)
{
    application();
    const Catalogs catalogs({u"zh_CN"_s, u"sr@latin"_s, u"de"_s, u"es"_s, u"pt_BR"_s});
    const SettingsFile settings;
    const ui::LanguageSetting setting;
    QStringList codes;
    QStringList names;
    for (const QVariant &language : setting.languages()) {
        codes.append(language.toMap().value(u"code"_s).toString());
        names.append(language.toMap().value(u"name"_s).toString());
    }
    // **English first**, which has no catalog; then the catalogs by name.
    EXPECT_EQ(codes, (QStringList{u"en"_s, u"de"_s, u"es"_s, u"pt_BR"_s, u"sr@latin"_s, u"zh_CN"_s}));
    EXPECT_EQ(names.value(0), u"English"_s);
    EXPECT_EQ(names.value(1), u"Deutsch"_s);
    EXPECT_EQ(names.value(2), u"Español"_s);
    EXPECT_EQ(names.value(3), u"Português (Brasil)"_s);
    EXPECT_EQ(names.value(4), u"Srpski"_s);
}

TEST(LanguageSettingTest, ALanguageIsNamedAsItsSpeakersNameIt)
{
    application();
    // **What most of a language's locales call it**, not one variety's
    // name: a catalog for Spanish is not "español de España".
    EXPECT_EQ(ui::LanguageSetting::nameOf(u"es"_s), u"Español"_s);
    EXPECT_EQ(ui::LanguageSetting::nameOf(u"en"_s), u"English"_s);
    EXPECT_EQ(ui::LanguageSetting::nameOf(u"de"_s), u"Deutsch"_s);
    EXPECT_EQ(ui::LanguageSetting::nameOf(u"fr"_s), u"Français"_s);
    // **Capitalised as its own language capitalises**, in its own script.
    EXPECT_EQ(ui::LanguageSetting::nameOf(u"ru"_s), u"Русский"_s);
    EXPECT_EQ(ui::LanguageSetting::nameOf(u"ja"_s), u"日本語"_s);
    // **With the territory the code names**, in the same language.
    EXPECT_EQ(ui::LanguageSetting::nameOf(u"pt_BR"_s), u"Português (Brasil)"_s);
    EXPECT_EQ(ui::LanguageSetting::nameOf(u"en_GB"_s), u"English (United Kingdom)"_s);
    const QString chinese = ui::LanguageSetting::nameOf(u"zh_CN"_s);
    EXPECT_TRUE(chinese.contains(u"中文"_s) && chinese.endsWith(u"(中国)"_s)) << chinese.toStdString();
    // **A modifier that names a script is that script**; another is said.
    EXPECT_EQ(ui::LanguageSetting::nameOf(u"sr@latin"_s), u"Srpski"_s);
    EXPECT_EQ(ui::LanguageSetting::nameOf(u"sr"_s), u"Српски"_s);
    EXPECT_EQ(ui::LanguageSetting::nameOf(u"ca@valencia"_s), u"Català (valencia)"_s);
    // A code no locale knows is named by itself.
    EXPECT_EQ(ui::LanguageSetting::nameOf(u"xx"_s), u"xx"_s);
}

TEST(LanguageSettingTest, FollowingTheSystemGivesTheFirstOfTheDesktopsLanguagesWithACatalog)
{
    const QStringList catalogs{u"de"_s, u"pt_BR"_s, u"sr@latin"_s};
    const auto among = [&catalogs](const QProcessEnvironment &environment) {
        return ui::LanguageSetting::systemLanguageAmong(catalogs, environment);
    };
    EXPECT_EQ(among(startedWith({{u"LANG"_s, u"de_DE.UTF-8"_s}})), u"de"_s);
    // **As itself, then without its territory**, as KI18n looks a locale up.
    EXPECT_EQ(among(startedWith({{u"LANG"_s, u"de_AT.UTF-8"_s}})), u"de"_s);
    EXPECT_EQ(among(startedWith({{u"LANG"_s, u"pt_BR.UTF-8"_s}})), u"pt_BR"_s);
    EXPECT_EQ(among(startedWith({{u"LANG"_s, u"pt_PT.UTF-8"_s}})), u"en"_s) << "pt_BR is not Portugal's";
    EXPECT_EQ(among(startedWith({{u"LANG"_s, u"sr_RS.UTF-8@latin"_s}})), u"sr@latin"_s);
    // **LANGUAGE's list first**, past a language with no catalog; and
    // English where it comes first, though another has a catalog.
    EXPECT_EQ(among(startedWith({{u"LANGUAGE"_s, u"fr:de"_s}, {u"LANG"_s, u"en_US.UTF-8"_s}})), u"de"_s);
    EXPECT_EQ(among(startedWith({{u"LANGUAGE"_s, u"en:de"_s}, {u"LANG"_s, u"de_DE.UTF-8"_s}})), u"en"_s);
    // Then LC_ALL, LC_MESSAGES and LANG, in gettext's order.
    EXPECT_EQ(among(startedWith({{u"LC_ALL"_s, u"pt_BR.UTF-8"_s}, {u"LANG"_s, u"de_DE.UTF-8"_s}})), u"pt_BR"_s);
    EXPECT_EQ(among(startedWith({{u"LC_MESSAGES"_s, u"de_DE.UTF-8"_s}, {u"LANG"_s, u"en_US.UTF-8"_s}})), u"de"_s);
    // **English for the rest**: no catalog, English itself, and C, in which
    // gettext translates nothing whatever LANGUAGE says.
    EXPECT_EQ(among(startedWith({{u"LANG"_s, u"fr_FR.UTF-8"_s}})), u"en"_s);
    EXPECT_EQ(among(startedWith({{u"LANG"_s, u"en_GB.UTF-8"_s}})), u"en"_s);
    EXPECT_EQ(among(startedWith({{u"LANGUAGE"_s, u"de"_s}, {u"LANG"_s, u"C.UTF-8"_s}})), u"en"_s);
    EXPECT_EQ(among(startedWith({})), u"en"_s);
}

TEST(LanguageSettingTest, ARestartIsWhatAnotherLanguageThanTheOneInForceTakes)
{
    application();
    const Catalogs catalogs({u"de"_s, u"pt_BR"_s});
    const SettingsFile settings;
    // **Started following the system, which gives German.**
    startedIn("de_DE.UTF-8");
    {
        ui::LanguageSetting setting;
        EXPECT_EQ(setting.systemLanguage(), u"de"_s);
        EXPECT_EQ(setting.systemLanguageName(), u"Deutsch"_s);
        EXPECT_EQ(setting.inForce(), u"de"_s);
        EXPECT_FALSE(setting.restartNeeded());
        setting.setChosen(u"de"_s);
        EXPECT_FALSE(setting.restartNeeded()) << "German is what the window speaks already";
        setting.setChosen(u"en"_s);
        EXPECT_TRUE(setting.restartNeeded());
        setting.setChosen(u"pt_BR"_s);
        EXPECT_TRUE(setting.restartNeeded());
        setting.setChosen(QString());
        EXPECT_FALSE(setting.restartNeeded());
    }
    // **Started in a language chosen**: following the system now is
    // another language, German, and so is a restart.
    notify::ProcessLanguage::applyAtStart(u"pt_BR"_s);
    {
        ui::LanguageSetting setting;
        EXPECT_EQ(setting.inForce(), u"pt_BR"_s);
        EXPECT_TRUE(setting.restartNeeded());
        // **What following the system gives is the desktop's**, read from
        // the environment the process was started with, not the LANGUAGE
        // the choice put there.
        EXPECT_EQ(qgetenv("LANGUAGE"), QByteArray("pt_BR"));
        EXPECT_EQ(setting.systemLanguage(), u"de"_s);
        setting.setChosen(u"pt_BR"_s);
        EXPECT_FALSE(setting.restartNeeded());
    }
}
