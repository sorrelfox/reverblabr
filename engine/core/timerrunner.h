// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>

#include <functional>

namespace rb::core
{

class EngineFacade;

/// What a timer does when its time comes (core/timers.h): the facade's
/// fireTimer() and listTimers(), the same in the engine and the mock.
///
/// - **Starts the session**, opening it first when it is only on disk, as
///   `reverblabr-ctl start` does. One already running is left as it is,
///   and not stopped later either: somebody started it, not the timer.
/// - **Stops it when the duration is over**, from the moment it fired. A
///   timer that fires again while its run is under way runs on from then.
///   Somebody stopping the session, or the session failing, ends the run.
/// - **Watches the clock, not a countdown**: the end is a time of day, and
///   it is looked at least every half a minute, so a laptop that slept
///   through the end stops the session as soon as it wakes rather than a
///   whole duration later.
/// - **Then quits the applications the session captures**, for a timer
///   set to, through \a quitApplications: the engine knows which
///   processes those are, and the mock only counts the request.
///
/// Uses the facade's own calls and signals, so it does nothing a client
/// could not do. Reads the sessions through \a listing, as SessionsListed
/// would carry them, without saying so on the bus. \a engine must outlive
/// this.
class TimerRunner : public QObject
{
    Q_OBJECT

public:
    TimerRunner(EngineFacade *engine,
                std::function<QString()> listing,
                std::function<void(const QString &sessionId)> quitApplications,
                QObject *parent = nullptr);

    /// Every session's timers, as TimersListed carries them: each timer as
    /// the file holds it, with `sessionId`, `sessionName`, `onCalendar`,
    /// `nextRun` (milliseconds since the epoch, left out when it never
    /// runs again) and, while it is keeping its session running, `endsAt`.
    [[nodiscard]] QString timerListing(const QDateTime &now) const;

    void fire(const QString &timerId);

Q_SIGNALS:
    /// The timer was found, and its session started or asked to.
    void fired(const QString &timerId, const QString &sessionId);
    /// No session has a timer of that id.
    void notFound(const QString &timerId);
    /// The run is over and the session was asked to stop.
    void ended(const QString &timerId, const QString &sessionId);

private:
    struct Run {
        QString sessionId;
        QDateTime endsAt;
        bool quitApplications = false;
        /// Asked to start; until then, waiting for the session to open.
        bool started = false;
    };

    void opened(const QString &sessionId);
    void stateChanged(const QString &sessionId, const QString &state);
    void begin(const QString &timerId);
    void check();

    EngineFacade *m_engine = nullptr;
    std::function<QString()> m_listing;
    std::function<void(const QString &)> m_quitApplications;
    /// The runs under way, by timer.
    QHash<QString, Run> m_runs;
    QTimer m_check;
};

} // namespace rb::core
