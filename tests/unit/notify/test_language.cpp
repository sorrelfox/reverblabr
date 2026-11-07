// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The language chosen in the window (ADR-0036), as every process reads it
// without KDE Frameworks: the words in the settings file, LANGUAGE set from
// them and given back as it was to what a process starts, and gettext's
// words following a change. The window's side, through KConfig, is
// app/test_languagesetting.cpp.

#include "language.h"
#include "texts.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <clocale>

using namespace rb;
using namespace Qt::StringLiterals;

namespace
{

/// A settings file of the test's own, written as KConfig writes one.
class SettingsFile
{
public:
    SettingsFile()
    {
        EXPECT_TRUE(m_directory.isValid());
    }

    [[nodiscard]] QString path() const
    {
        return m_directory.filePath(u"reverblabrrc"_s);
    }

    void write(const QByteArray &text) const
    {
        QFile file(path());
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(text);
    }

private:
    QTemporaryDir m_directory;
};

/// **The made-up languages are the only ones there are**: the data folders
/// are the build's folder of test catalogs ("xx" and "yy") and nothing of
/// the machine's.
void onlyTheTestLanguages()
{
    qputenv("XDG_DATA_DIRS", RB_TEST_LANGUAGES_DIR);
}

/// A real locale for the words, or why there is none: gettext translates
/// nothing in C, whatever LANGUAGE says.
[[nodiscard]] bool wordsLocale()
{
    qputenv("LC_ALL", "en_US.UTF-8");
    return std::setlocale(LC_ALL, "") != nullptr;
}

} // namespace

TEST(LanguageSettingFile, TheWindowsWordsAreReadWithoutKdeFrameworks)
{
    onlyTheTestLanguages();
    const SettingsFile settings;
    EXPECT_EQ(notify::chosenLanguage(settings.path()), QString()) << "no file is following the system";

    // **[General] as KConfig writes it**, beside a group of another setting.
    settings.write("[AutoRun]\nStart=every-engine-start\n\n[General]\nLanguage=xx\n");
    EXPECT_EQ(notify::chosenLanguage(settings.path()), u"xx"_s);
    settings.write("[General]\nLanguage=en\n");
    EXPECT_EQ(notify::chosenLanguage(settings.path()), u"en"_s) << "English has no catalog and needs none";

    // **Anything else is following the system**: a language whose catalog
    // has gone, a list, a word that is no language, and no key.
    for (const QByteArray &text : {QByteArray("[General]\nLanguage=de\n"),
                                   QByteArray("[General]\nLanguage=xx:yy\n"),
                                   QByteArray("[General]\nLanguage=../xx\n"),
                                   QByteArray("[General]\nLanguage=\n"),
                                   QByteArray("[AutoRun]\nStart=once-a-login\n")}) {
        settings.write(text);
        EXPECT_EQ(notify::chosenLanguage(settings.path()), QString()) << text.toStdString();
    }
    EXPECT_EQ(notify::chosenLanguage(QString()), QString());
}

TEST(LanguageSettingFile, ACodeIsACatalogsFolderName)
{
    for (const QString &code : {u"de"_s, u"pt_BR"_s, u"es_419"_s, u"sr@latin"_s, u"ast"_s}) {
        EXPECT_TRUE(notify::isLanguageCode(code)) << code.toStdString();
    }
    for (const QString &word : {u""_s, u"de:fr"_s, u"de_DE.UTF-8"_s, u"x-test"_s, u"DE"_s, u"../de"_s}) {
        EXPECT_FALSE(notify::isLanguageCode(word)) << word.toStdString();
    }
    onlyTheTestLanguages();
    EXPECT_TRUE(notify::hasCatalog(u"xx"_s));
    EXPECT_TRUE(notify::hasCatalog(u"yy"_s));
    EXPECT_FALSE(notify::hasCatalog(u"zz"_s));
}

TEST(ProcessLanguage, TheChoiceIsLanguageAndWhatWasThereIsKeptForWhatThisStarts)
{
    qputenv("LANGUAGE", "fr:de");
    notify::ProcessLanguage::applyAtStart(u"xx"_s);
    EXPECT_EQ(qgetenv("LANGUAGE"), QByteArray("xx"));
    EXPECT_EQ(notify::ProcessLanguage::atStart(), u"xx"_s);
    // **A program this one starts gets the desktop's list back**, whatever
    // the environment it is handed says.
    EXPECT_EQ(notify::ProcessLanguage::originalEnvironment().value(u"LANGUAGE"_s), u"fr:de"_s);
    QProcessEnvironment handed;
    handed.insert(u"LANGUAGE"_s, u"xx"_s);
    handed.insert(u"HOME"_s, u"/nowhere"_s);
    const QProcessEnvironment restored = notify::ProcessLanguage::restored(handed);
    EXPECT_EQ(restored.value(u"LANGUAGE"_s), u"fr:de"_s);
    EXPECT_EQ(restored.value(u"HOME"_s), u"/nowhere"_s) << "the rest is not touched";

    // **Following the system leaves it as it was.**
    qputenv("LANGUAGE", "fr");
    notify::ProcessLanguage::applyAtStart(QString());
    EXPECT_EQ(qgetenv("LANGUAGE"), QByteArray("fr"));
    EXPECT_TRUE(notify::ProcessLanguage::atStart().isEmpty());

    // **Not set, it is made to exist, empty**, which reads as not set; what
    // is started is given none at all, as it was.
    qunsetenv("LANGUAGE");
    notify::ProcessLanguage::applyAtStart(QString());
    EXPECT_TRUE(qEnvironmentVariableIsSet("LANGUAGE"));
    EXPECT_TRUE(qgetenv("LANGUAGE").isEmpty());
    EXPECT_FALSE(notify::ProcessLanguage::originalEnvironment().contains(u"LANGUAGE"_s));
    qunsetenv("LANGUAGE");
    notify::ProcessLanguage::applyAtStart(u"yy"_s);
    EXPECT_EQ(qgetenv("LANGUAGE"), QByteArray("yy"));
    EXPECT_FALSE(notify::ProcessLanguage::restored(QProcessEnvironment::systemEnvironment()).contains(u"LANGUAGE"_s));
}

TEST(ProcessLanguage, ItsWordsFollowAChangeAsSoonAsItIsMade)
{
    if (!wordsLocale()) {
        GTEST_SKIP() << "Needs a real locale such as en_US.UTF-8 (Fedora: glibc-langpack-en); gettext ignores "
                        "catalogs in the C locale.";
    }
    onlyTheTestLanguages();
    qunsetenv("LANGUAGE");
    notify::ProcessLanguage::applyAtStart(u"xx"_s);
    notify::bindCatalog();
    const notify::Message finished = RB_I18NC("@title a notification", "Recording finished");
    EXPECT_EQ(notify::translate(finished), u"[xx] Recording finished"_s);

    // **Not what gettext looked up before**: its cache does not know which
    // language LANGUAGE named, and is told to forget.
    EXPECT_TRUE(notify::ProcessLanguage::follow(u"yy"_s));
    EXPECT_EQ(qgetenv("LANGUAGE"), QByteArray("yy"));
    EXPECT_EQ(notify::translate(finished), u"[yy] Recording finished"_s);
    EXPECT_FALSE(notify::ProcessLanguage::follow(u"yy"_s)) << "the same language again is no change";

    // **English**, which has no catalog, and the system's again: LANGUAGE
    // as it was, which here is none.
    EXPECT_TRUE(notify::ProcessLanguage::follow(u"en"_s));
    EXPECT_EQ(notify::translate(finished), u"Recording finished"_s);
    EXPECT_TRUE(notify::ProcessLanguage::follow(u"xx"_s));
    EXPECT_EQ(notify::translate(finished), u"[xx] Recording finished"_s);
    EXPECT_TRUE(notify::ProcessLanguage::follow(QString()));
    EXPECT_TRUE(qgetenv("LANGUAGE").isEmpty());
    EXPECT_EQ(notify::translate(finished), u"Recording finished"_s);
}
