// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QLatin1StringView>
#include <QObject>
#include <QQmlEngine>
#include <QStringList>

class QCommandLineParser;

namespace rb::ui
{

/// **What a launch asks the window for**.
///
/// The desktop entry's actions start Reverblabr with `--new-session` or
/// `--sessions`, and KRunner's Open, through the daemon, with
/// `--open-session <id>`. A launch while Reverblabr already runs is handed to the
/// running one rather than starting a second (KDBusService, in main()), so
/// the first launch's arguments and every later one's arrive here alike and
/// go to the window as signals.
///
/// Kept apart from the bus, so what a command line means can be tested with
/// no bus at all, and a QML test can make the requests itself.
class LaunchRequests : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    enum class Request {
        Show, ///< Nothing in particular: bring the window forward.
        NewSession, ///< `--new-session`: a new, untitled session.
        Sessions, ///< `--sessions`: the Sessions window.
        OpenSession, ///< `--open-session <id>`: that session, in front.
    };
    Q_ENUM(Request)

    static constexpr QLatin1StringView kNewSessionOption{"new-session"};
    static constexpr QLatin1StringView kSessionsOption{"sessions"};
    static constexpr QLatin1StringView kOpenSessionOption{"open-session"};

    explicit LaunchRequests(QObject *parent = nullptr);

    [[nodiscard]] static LaunchRequests *instance();
    static LaunchRequests *create(QQmlEngine *engine, QJSEngine *scriptEngine);

    /// Adds `--new-session`, `--sessions` and `--open-session` to \a parser,
    /// main()'s, so `--help` lists them and a mistyped one is refused there.
    static void addOptions(QCommandLineParser &parser);

    /// What \a arguments, a whole command line with the program first, asks
    /// for. The other options Reverblabr takes are no concern here; they
    /// were checked by the launch that had them. **New Session wins** over
    /// the others, and opening a session over the Sessions window, when a
    /// command line has more than one.
    [[nodiscard]] static Request requestOf(const QStringList &arguments);

    /// The session `--open-session` names in \a arguments, or nothing.
    [[nodiscard]] static QString sessionOf(const QStringList &arguments);

    /// Tells the window what \a arguments asks for.
    Q_INVOKABLE void deliver(const QStringList &arguments);

Q_SIGNALS:
    void showRequested();
    void newSessionRequested();
    void sessionsWindowRequested();
    void openSessionRequested(const QString &sessionId);
};

} // namespace rb::ui
