// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringView>
#include <QTime>

#include <cstdint>
#include <optional>

namespace rb::core
{

/// How often a timer comes round.
enum class TimerRepeat : std::uint8_t {
    /// Once, on a date.
    Once,
    /// Every day.
    Daily,
    /// Monday to Friday.
    Weekdays,
    /// On the days of the week it names.
    Days,
};

/// One of a session's timers: when it starts the session, and for how long.
///
/// **Kept in the session file**, in its `timers` array, so that a timer
/// travels with the session it runs and goes when the session is deleted.
/// The background engine turns every timer that is on into a systemd user
/// timer (daemon/timerschedule.h), which is what starts the session while no
/// window and no engine is running.
///
/// **Minutes, in local time**: what a person sets on a clock. The duration
/// is in seconds so that a test need not wait a minute.
struct Timer {
    /// Unique among every session's timers, and part of the units' file
    /// names, which is why only timerIdIsValid() ones are taken.
    QString id;
    bool enabled = true;
    TimerRepeat repeat = TimerRepeat::Daily;
    /// The day of a timer that runs once; unused otherwise.
    QDate date;
    /// When it starts, to the minute.
    QTime time;
    /// The days of a TimerRepeat::Days timer, as a mask: bit 0 is Monday,
    /// bit 6 is Sunday. Unused otherwise.
    std::uint8_t days = 0;
    /// How long the session runs once started, in seconds.
    int durationSeconds = 3600;
    /// When the run ends, also quit the applications the session captures.
    bool quitApplications = false;

    [[nodiscard]] bool operator==(const Timer &other) const = default;
};

/// The longest a timer can keep a session running: a week. A timer is for a
/// recording or a broadcast at a set time, and one that runs for longer is
/// better served by Auto Run.
inline constexpr int kTimerMaxDurationSeconds = 7 * 24 * 60 * 60;

/// Whether \a id can name a timer: a letter or a digit, then letters,
/// digits, `-` and `_`, at most 64. **It becomes part of a file name** and of
/// a command line, so nothing else is taken, whatever a session file says;
/// and it never starts with a dash, which a command line would read as an
/// option.
[[nodiscard]] bool timerIdIsValid(QStringView id);

/// A timer as the session file holds it:
///
///     {"id": "…", "enabled": true, "repeat": "days", "days": ["mon", "thu"],
///      "time": "07:30", "duration": 5400, "quitApplications": false}
///
/// `repeat` is `once` (with `date`, `yyyy-MM-dd`), `daily`, `weekdays` or
/// `days` (with `days`). Empty when anything about it is missing or wrong,
/// including a repeat this version does not know: such a timer is left in
/// the file untouched and never run.
[[nodiscard]] std::optional<Timer> timerFromJson(const QJsonObject &json);
[[nodiscard]] QJsonObject timerToJson(const Timer &timer);

/// The timers in a session's `timers` array that this version can run, in
/// the order it holds them.
[[nodiscard]] QList<Timer> timersFromJson(const QJsonArray &timers);

/// \a timers with a new id for each, for a session that is a copy of
/// another: two sessions holding the same timer would have one systemd timer
/// between them. Everything else about each, a timer this version cannot
/// read included, is kept as it was.
[[nodiscard]] QJsonArray timersWithNewIds(const QJsonArray &timers);

/// A new timer id: a UUID, as session ids are.
[[nodiscard]] QString newTimerId();

/// The repeat's name in the file: `once`, `daily`, `weekdays`, `days`.
[[nodiscard]] QString timerRepeatName(TimerRepeat repeat);

/// The systemd calendar expression that starts the timer, for `OnCalendar=`
/// (systemd.time(7)): `2026-11-03 14:30:00` for a timer that runs once,
/// `*-*-* 07:00:00` every day, and the days of the week in front of that for
/// the others, with a run of three or more as a range: `Mon..Fri`,
/// `Mon,Wed,Sat`, `Mon..Wed,Sun`. Every day of the week is every day.
/// Empty for a TimerRepeat::Days timer with no days, which never runs.
[[nodiscard]] QString onCalendar(const Timer &timer);

/// When the timer next starts its session after \a after, in local time, or
/// nothing when it never will again (a one-off in the past, or no days).
[[nodiscard]] std::optional<QDateTime> nextRun(const Timer &timer, const QDateTime &after);

/// When the timer last started its session, at or before \a at, in local
/// time, or nothing when it never has (a one-off still to come, or no days).
/// What a fire from systemd is checked against: one that comes long after
/// this, as after a sleep, is late and not run.
[[nodiscard]] std::optional<QDateTime> previousRun(const Timer &timer, const QDateTime &at);

/// Whether a session other than \a sessionId, in \a sessionsJson as
/// SessionsListed carries them, has a timer \a timerId. **One id, one
/// systemd timer**: a second session with the same id would share the first
/// one's units.
[[nodiscard]] bool timerIdTakenElsewhere(const QString &sessionsJson, const QString &sessionId, const QString &timerId);

} // namespace rb::core
