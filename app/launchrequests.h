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
/// `--sessions`. A launch while Reverblabr already runs is handed to the
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
    };
    Q_ENUM(Request)

    static constexpr QLatin1StringView kNewSessionOption{"new-session"};
    static constexpr QLatin1StringView kSessionsOption{"sessions"};

    explicit LaunchRequests(QObject *parent = nullptr);

    [[nodiscard]] static LaunchRequests *instance();
    static LaunchRequests *create(QQmlEngine *engine, QJSEngine *scriptEngine);

    /// Adds `--new-session` and `--sessions` to \a parser, main()'s, so
    /// `--help` lists them and a mistyped one is refused there.
    static void addOptions(QCommandLineParser &parser);

    /// What \a arguments, a whole command line with the program first, asks
    /// for. The other options Reverblabr takes are no concern here; they
    /// were checked by the launch that had them. **New Session wins** over
    /// Sessions when a command line has both.
    [[nodiscard]] static Request requestOf(const QStringList &arguments);

    /// Tells the window what \a arguments asks for.
    Q_INVOKABLE void deliver(const QStringList &arguments);

Q_SIGNALS:
    void showRequested();
    void newSessionRequested();
    void sessionsWindowRequested();
};

} // namespace rb::ui
