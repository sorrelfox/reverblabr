// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QDBusArgument>
#include <QDBusConnection>
#include <QElapsedTimer>
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVariantMap>

#include <functional>
#include <optional>

namespace rb::core
{
class EngineFacade;
}

namespace rb::daemon
{

/// Where the runner is served, beside the engine and under the same name.
inline constexpr QLatin1StringView kRunnerPath{"/io/github/reverblabr/Runner"};
/// The interface KRunner calls a runner on the bus by.
inline constexpr QLatin1StringView kRunnerInterface{"org.kde.krunner1"};

/// An action offered beside a match, as org.kde.krunner1 carries it:
/// `(sss)`, its id, its words and its icon.
struct RunnerAction {
    QString id;
    QString text;
    QString iconName;
};

/// One match, as org.kde.krunner1 carries it: `(sssida{sv})`. The
/// properties are the optional extras: `subtext`, and `actions`, the ids
/// of the actions this match offers out of all the runner has.
struct RunnerMatch {
    QString id;
    QString text;
    QString iconName;
    int categoryRelevance = 0;
    double relevance = 0;
    QVariantMap properties;
};

using RunnerActions = QList<RunnerAction>;
using RunnerMatches = QList<RunnerMatch>;

QDBusArgument &operator<<(QDBusArgument &argument, const RunnerAction &action);
const QDBusArgument &operator>>(const QDBusArgument &argument, RunnerAction &action);
QDBusArgument &operator<<(QDBusArgument &argument, const RunnerMatch &match);
const QDBusArgument &operator>>(const QDBusArgument &argument, RunnerMatch &match);

/// Registers the types above with Qt D-Bus, for the runner and for a test
/// reading its answers. Harmless to call more than once.
void registerRunnerTypes();

/// **The sessions in KRunner** (ADR-0023): "rb" and part of a session's
/// name offers to run or stop it, or to open it in the window; "rb rec"
/// says what is being recorded.
///
/// - **Served by the daemon, on an object of its own**, so a query is
///   answered from the sessions the engine has, read directly
///   (Engine::sessionListing()) rather than asked for and waited on.
/// - **The list is kept between keystrokes**, since reading it loads every
///   session's file: until a session opens, closes, changes state or is
///   renamed, somebody lists the sessions, KRunner closes, or a moment has
///   passed (a file deleted or copied in by hand says nothing). Run reads it
///   afresh.
/// - **KRunner is not a client.** A query does not keep the engine from
///   exiting when it is idle: only calls on the engine's own object do that.
/// - **Recording is followed from the engine's signals**, as the tray
///   follows it: a recorder writing a file and not paused is recording.
/// - **The window is started for Open**, with `--open-session` and the
///   activation token KRunner handed over, in a scope of its own
///   (startWindow()). Only an id that is a session's id is ever put on its
///   command line.
///
/// Follows \a engine, which must outlive this.
class SessionRunner : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.krunner1")

public:
    SessionRunner(core::EngineFacade *engine,
                  std::function<QString()> listing,
                  QString windowProgram,
                  QObject *parent = nullptr);

    /// Serves the runner at kRunnerPath on \a bus. False when the path is
    /// taken.
    bool publish(QDBusConnection bus);

public Q_SLOTS:
    /// Every action a match may offer. KRunner asks once.
    Q_SCRIPTABLE rb::daemon::RunnerActions Actions();
    /// What \a query, KRunner's whole line, matches.
    Q_SCRIPTABLE rb::daemon::RunnerMatches Match(const QString &query);
    /// Does what a match stands for: \a actionId empty for the match itself
    /// (run or stop the session), or one of Actions().
    Q_SCRIPTABLE void Run(const QString &matchId, const QString &actionId);
    /// The token the next window started may come forward with on Wayland.
    Q_SCRIPTABLE void SetActivationToken(const QString &token);
    /// KRunner has closed. Nothing is kept between queries.
    Q_SCRIPTABLE void Teardown();

private:
    void opened(const QString &sessionId);
    void forgetRecorders(const QString &sessionId);
    void openInWindow(const QString &sessionId, const QString &token);
    /// The sessions as SessionsListed carries them: kept, or read again.
    [[nodiscard]] QString listing();
    void forgetListing();
    [[nodiscard]] bool isRecording(const QString &sessionId) const;
    [[nodiscard]] bool hasRecorders(const QString &sessionId) const;

    core::EngineFacade *m_engine = nullptr;
    std::function<QString()> m_listing;
    std::optional<QString> m_listed;
    QElapsedTimer m_listedAt;
    QString m_windowProgram;
    QString m_activationToken;
    /// Opened here to be run: run when they have opened.
    QSet<QString> m_toStart;
    /// Recorders writing a file, as "session/block", and those of them
    /// paused.
    QSet<QString> m_writing;
    QSet<QString> m_paused;
};

/// Starts the window, \a program with \a arguments, and moves it into a
/// systemd scope of its own when there is a user manager on \a bus. **Not
/// left in the daemon's own unit**: stopping or restarting the engine would
/// take the window with it, which ADR-0017 promises it does not. \a token,
/// when there is one, is the launch's activation token. False when the
/// program would not start.
bool startWindow(const QDBusConnection &bus,
                 const QString &program,
                 const QStringList &arguments,
                 const QString &token);

} // namespace rb::daemon

Q_DECLARE_METATYPE(rb::daemon::RunnerAction)
Q_DECLARE_METATYPE(rb::daemon::RunnerMatch)
