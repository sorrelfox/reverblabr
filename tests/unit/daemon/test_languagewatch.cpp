// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The daemon follows the language chosen in the window while it runs
// (ADR-0036): the settings file is written the way KConfig writes it, a new
// file renamed over the old one, and the daemon's words are in the new
// language from then on. Two made-up languages, "xx" and "yy", are the only
// ones there are (tests/languages).

#include "language.h"
#include "languagewatch.h"
#include "texts.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <gtest/gtest.h>

#include <clocale>
#include <cstdio>

using namespace rb;
using namespace Qt::StringLiterals;

namespace
{

QCoreApplication &application()
{
    static int argc = 1;
    static char name[] = "reverblabr-daemon-test";
    static char *argv[] = {name, nullptr};
    // **Never destroyed**, as the other suites' applications: a static one
    // would go after the Qt globals it needs.
    static auto *const made = new QCoreApplication(argc, argv);
    return *made;
}

template <typename Predicate>
bool waitFor(Predicate predicate, int milliseconds = 5000)
{
    QElapsedTimer clock;
    clock.start();
    while (!predicate() && clock.elapsed() < milliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return predicate();
}

/// **As KConfig saves a file**: the whole of it written beside the old one,
/// then renamed over it, which a watch on the old file alone sees once.
void save(const QString &path, const QByteArray &text)
{
    const QString written = path + u".new"_s;
    QFile file(written);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(text);
    file.close();
    ASSERT_EQ(::rename(QFile::encodeName(written).constData(), QFile::encodeName(path).constData()), 0);
}

} // namespace

TEST(LanguageWatch, TheDaemonsWordsFollowTheLanguageChosenInTheWindow)
{
    // **A real locale for the words**: gettext translates nothing in C.
    qputenv("LC_ALL", "en_US.UTF-8");
    application();
    if (std::setlocale(LC_ALL, "") == nullptr || QByteArray(std::setlocale(LC_MESSAGES, nullptr)).startsWith("C")) {
        GTEST_SKIP() << "Needs a real locale such as en_US.UTF-8 (Fedora: glibc-langpack-en); gettext ignores "
                        "catalogs in the C locale.";
    }
    qputenv("XDG_DATA_DIRS", RB_TEST_LANGUAGES_DIR);
    QTemporaryDir config;
    ASSERT_TRUE(config.isValid());
    const QString path = config.filePath(u"reverblabrrc"_s);
    save(path, "[General]\nLanguage=xx\n");

    // As main() starts: the choice applied, then the catalog found.
    qunsetenv("LANGUAGE");
    notify::ProcessLanguage::applyAtStart(notify::chosenLanguage(path));
    notify::bindCatalog();
    const notify::Message nothing = RB_I18NC("@info KRunner", "Nothing is being recorded");
    ASSERT_EQ(notify::translate(nothing), u"[xx] Nothing is being recorded"_s);

    daemon::LanguageWatch watch(path);
    QSignalSpy changed(&watch, &daemon::LanguageWatch::changed);
    EXPECT_EQ(watch.language(), u"xx"_s);

    // **Another language**, saved as the window saves it.
    save(path, "[General]\nLanguage=yy\n");
    ASSERT_TRUE(waitFor([&] { return watch.language() == u"yy"_s; })) << "the change was never seen";
    EXPECT_EQ(qgetenv("LANGUAGE"), QByteArray("yy"));
    EXPECT_EQ(notify::translate(nothing), u"[yy] Nothing is being recorded"_s);
    EXPECT_EQ(changed.count(), 1);

    // **Another setting in the same file** is no change of language.
    save(path, "[AutoRun]\nStart=every-engine-start\n\n[General]\nLanguage=yy\n");
    QThread::msleep(400);
    QCoreApplication::processEvents();
    EXPECT_EQ(changed.count(), 1) << "a change of Auto Run was taken for one of language";

    // **The system's again**: LANGUAGE as the daemon was started with it,
    // which here is none, and gettext's words in English.
    save(path, "[AutoRun]\nStart=every-engine-start\n");
    ASSERT_TRUE(waitFor([&] { return watch.language().isEmpty(); })) << "following the system again was never seen";
    EXPECT_TRUE(qgetenv("LANGUAGE").isEmpty());
    EXPECT_EQ(notify::translate(nothing), u"Nothing is being recorded"_s);

    // And a third time, to see that the watch outlived the renames.
    save(path, "[General]\nLanguage=xx\n");
    ASSERT_TRUE(waitFor([&] { return watch.language() == u"xx"_s; })) << "the watch was lost after a rename";
    EXPECT_EQ(notify::translate(nothing), u"[xx] Nothing is being recorded"_s);
}
