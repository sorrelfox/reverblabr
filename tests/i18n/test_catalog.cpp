// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Proves that the application's strings are looked up in its KI18n catalog:
// a made-up catalog for the language "xx" is compiled at build time, and the
// C++ strings, the error messages and the real main window's QML title must
// come out translated.

#include "errormessages.h"

#include <KLocalizedQmlContext>
#include <KLocalizedString>
#include <QQmlApplicationEngine>
#include <QTest>
#include <QtQml/QQmlExtensionPlugin>

#include <clocale>
#include <cstring>

Q_IMPORT_QML_PLUGIN(io_github_reverblabrPlugin)

using namespace Qt::StringLiterals;

class CatalogTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        // gettext ignores the requested languages in the C locale (glibc treats
        // C, POSIX and C.UTF-8 alike), so catalogs can't load there. Real
        // sessions always have a locale; minimal containers may not.
        const char *messagesLocale = std::setlocale(LC_MESSAGES, nullptr);
        if (!messagesLocale || std::strcmp(messagesLocale, "C") == 0 || std::strcmp(messagesLocale, "POSIX") == 0
            || std::strncmp(messagesLocale, "C.", 2) == 0) {
            QSKIP(
                "Needs a real locale such as en_US.UTF-8 (Fedora: glibc-langpack-en); gettext ignores catalogs in "
                "the C locale.");
        }

        KLocalizedString::setApplicationDomain("reverblabr");
        KLocalizedString::addDomainLocaleDir("reverblabr", QStringLiteral(TEST_LOCALE_DIR));
        KLocalizedString::setLanguages({u"xx"_s});
        QVERIFY(KLocalizedString::isApplicationTranslatedInto(u"xx"_s));
    }

    void cppStringsAreTranslated()
    {
        QCOMPARE(i18nc("@title:window", "Reverblabr"), u"[xx] Reverblabr"_s);
    }

    void errorMessagesAreTranslated()
    {
        QCOMPARE(rb::app::errorMessage(u"graph.cycle"_s), u"[xx] Loop in the connections."_s);
    }

    void qmlStringsAreTranslated()
    {
        QQmlApplicationEngine engine;
        KLocalization::setupLocalizedContext(&engine);
        engine.loadFromModule("io.github.reverblabr", "Main");
        QCOMPARE(engine.rootObjects().size(), 1);
        QCOMPARE(engine.rootObjects().first()->property("title").toString(), u"[xx] Reverblabr"_s);
    }

    void missingTranslationsFallBackToEnglish()
    {
        QCOMPARE(i18nc("@info", "A sentence that no catalog translates"), u"A sentence that no catalog translates"_s);
    }
};

QTEST_MAIN(CatalogTest)

#include "test_catalog.moc"
