// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What a command line asks the window for.

#include "launchrequests.h"

#include <QCommandLineParser>
#include <QFile>
#include <QRegularExpression>
#include <QSignalSpy>
#include <gtest/gtest.h>

using namespace rb::ui;
using namespace Qt::StringLiterals;

namespace
{

using Request = LaunchRequests::Request;

TEST(LaunchRequestsTest, ACommandLineSaysWhatItWants)
{
    EXPECT_EQ(LaunchRequests::requestOf({u"reverblabr"_s}), Request::Show);
    EXPECT_EQ(LaunchRequests::requestOf({u"reverblabr"_s, u"--new-session"_s}), Request::NewSession);
    EXPECT_EQ(LaunchRequests::requestOf({u"reverblabr"_s, u"--sessions"_s}), Request::Sessions);
    // Among Reverblabr's other options, which are none of this one's concern.
    EXPECT_EQ(LaunchRequests::requestOf({u"/usr/bin/reverblabr"_s, u"--daemon"_s, u"--sessions"_s}), Request::Sessions);
    // **New Session wins** over the list, whichever comes first.
    EXPECT_EQ(LaunchRequests::requestOf({u"reverblabr"_s, u"--sessions"_s, u"--new-session"_s}), Request::NewSession);
    // The program's own name is not an option, whatever it is called.
    EXPECT_EQ(LaunchRequests::requestOf({u"--sessions"_s}), Request::Show);
    // After "--" come file names, never options.
    EXPECT_EQ(LaunchRequests::requestOf({u"reverblabr"_s, u"--"_s, u"--new-session"_s}), Request::Show);
    EXPECT_EQ(LaunchRequests::requestOf({}), Request::Show);
}

TEST(LaunchRequestsTest, KRunnersOpenNamesTheSession)
{
    const QString id = u"6f1c2a52-3d1e-4c7b-9a51-0d2b8e7f4a10"_s;
    // As the daemon starts the window, and as main()'s parser also takes it.
    EXPECT_EQ(LaunchRequests::requestOf({u"reverblabr"_s, u"--open-session"_s, id}), Request::OpenSession);
    EXPECT_EQ(LaunchRequests::sessionOf({u"reverblabr"_s, u"--open-session"_s, id}), id);
    EXPECT_EQ(LaunchRequests::sessionOf({u"reverblabr"_s, u"--open-session="_s + id}), id);
    // **Over the Sessions window**, which is the less particular request;
    // New Session still wins over both.
    EXPECT_EQ(LaunchRequests::requestOf({u"reverblabr"_s, u"--sessions"_s, u"--open-session"_s, id}),
              Request::OpenSession);
    EXPECT_EQ(LaunchRequests::requestOf({u"reverblabr"_s, u"--open-session"_s, id, u"--new-session"_s}),
              Request::NewSession);
    // **The option's value is never read as an option**, as main()'s parser
    // reads it too.
    EXPECT_EQ(LaunchRequests::requestOf({u"reverblabr"_s, u"--open-session"_s, u"--new-session"_s}),
              Request::OpenSession);
    EXPECT_EQ(LaunchRequests::sessionOf({u"reverblabr"_s, u"--open-session"_s, u"--new-session"_s}),
              u"--new-session"_s);
    EXPECT_EQ(LaunchRequests::requestOf({u"reverblabr"_s, u"--open-session="_s + id, u"--sessions"_s}),
              Request::OpenSession);
    // No id, or one after "--", is no session.
    EXPECT_EQ(LaunchRequests::requestOf({u"reverblabr"_s, u"--open-session"_s}), Request::Show);
    EXPECT_EQ(LaunchRequests::requestOf({u"reverblabr"_s, u"--"_s, u"--open-session"_s, id}), Request::Show);
    EXPECT_TRUE(LaunchRequests::sessionOf({u"reverblabr"_s}).isEmpty());
}

TEST(LaunchRequestsTest, EachRequestReachesTheWindowAsItsOwnSignal)
{
    LaunchRequests requests;
    QSignalSpy shown(&requests, &LaunchRequests::showRequested);
    QSignalSpy made(&requests, &LaunchRequests::newSessionRequested);
    QSignalSpy listed(&requests, &LaunchRequests::sessionsWindowRequested);
    QSignalSpy opened(&requests, &LaunchRequests::openSessionRequested);

    requests.deliver({u"reverblabr"_s, u"--new-session"_s});
    requests.deliver({u"reverblabr"_s, u"--sessions"_s});
    requests.deliver({u"reverblabr"_s});
    requests.deliver({u"reverblabr"_s, u"--open-session"_s, u"some-id"_s});

    EXPECT_EQ(made.count(), 1);
    EXPECT_EQ(listed.count(), 1);
    EXPECT_EQ(shown.count(), 1);
    ASSERT_EQ(opened.count(), 1);
    EXPECT_EQ(opened.first().first().toString(), u"some-id"_s);
}

TEST(LaunchRequestsTest, MainTakesTheOptionsTheDesktopEntryUses)
{
    QCommandLineParser parser;
    LaunchRequests::addOptions(parser);
    ASSERT_TRUE(parser.parse({u"reverblabr"_s, u"--new-session"_s, u"--sessions"_s, u"--open-session"_s, u"id"_s}))
        << parser.errorText().toStdString();
    EXPECT_EQ(parser.value(QString(LaunchRequests::kOpenSessionOption)), u"id"_s);

    // **Every action's Exec line is something main() takes**: an action
    // whose option main() refused would start nothing at all.
    QFile desktop(QStringLiteral(RB_DESKTOP_FILE));
    ASSERT_TRUE(desktop.open(QIODevice::ReadOnly));
    const QString entry = QString::fromUtf8(desktop.readAll());
    static const QRegularExpression exec(u"^Exec=reverblabr(.*)$"_s, QRegularExpression::MultilineOption);
    int actions = 0;
    for (auto match = exec.globalMatch(entry); match.hasNext();) {
        const QStringList arguments =
            QStringList{u"reverblabr"_s} + match.next().captured(1).split(u' ', Qt::SkipEmptyParts);
        EXPECT_TRUE(parser.parse(arguments)) << "main() refuses " << arguments.join(u' ').toStdString();
        if (arguments.size() > 1) {
            ++actions;
            EXPECT_NE(LaunchRequests::requestOf(arguments), Request::Show)
                << arguments.join(u' ').toStdString() << " asks for nothing";
        }
    }
    EXPECT_EQ(actions, 2) << "the desktop entry's New Session and All Sessions";
}

} // namespace
