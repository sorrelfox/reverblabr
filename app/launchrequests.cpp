// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "launchrequests.h"

#include <KLocalizedString>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QLoggingCategory>

using namespace Qt::StringLiterals;

namespace
{

Q_LOGGING_CATEGORY(lcLaunch, "reverblabr.launch", QtInfoMsg)

} // namespace

namespace rb::ui
{

LaunchRequests::LaunchRequests(QObject *parent)
    : QObject(parent)
{
}

LaunchRequests *LaunchRequests::instance()
{
    static auto *requests = new LaunchRequests;
    return requests;
}

LaunchRequests *LaunchRequests::create(QQmlEngine *, QJSEngine *)
{
    LaunchRequests *requests = instance();
    // **One for main() and the window**, which is why QML must not own it.
    QJSEngine::setObjectOwnership(requests, QJSEngine::CppOwnership);
    return requests;
}

void LaunchRequests::addOptions(QCommandLineParser &parser)
{
    parser.addOption(
        QCommandLineOption(QString(kNewSessionOption), i18nc("@info:shell", "Open a new, untitled session")));
    parser.addOption(QCommandLineOption(QString(kSessionsOption),
                                        i18nc("@info:shell", "Open the window listing every saved session")));
}

LaunchRequests::Request LaunchRequests::requestOf(const QStringList &arguments)
{
    // **Read by hand rather than with a parser of their own**: a parser that
    // knew only these two would refuse the rest, and whether it still
    // records what it knows after refusing is not something to lean on.
    // Everything here arrives as the long form, the way the desktop entry
    // writes it and the way main()'s parser accepts it.
    const QString newSession = u"--"_s + kNewSessionOption;
    const QString sessions = u"--"_s + kSessionsOption;
    bool wantsSessions = false;
    for (qsizetype index = 1; index < arguments.size(); ++index) {
        const QString &argument = arguments.at(index);
        // After "--" everything is a file name, never an option.
        if (argument == "--"_L1) {
            break;
        }
        if (argument == newSession) {
            return Request::NewSession;
        }
        if (argument == sessions) {
            wantsSessions = true;
        }
    }
    return wantsSessions ? Request::Sessions : Request::Show;
}

void LaunchRequests::deliver(const QStringList &arguments)
{
    const Request request = requestOf(arguments);
    qCInfo(lcLaunch).noquote() << "launch request:"
                               << QString::fromLatin1(QMetaEnum::fromType<Request>().valueToKey(int(request)));
    switch (request) {
    case Request::Show:
        Q_EMIT showRequested();
        break;
    case Request::NewSession:
        Q_EMIT newSessionRequested();
        break;
    case Request::Sessions:
        Q_EMIT sessionsWindowRequested();
        break;
    }
}

} // namespace rb::ui
