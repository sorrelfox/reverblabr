// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/timers.h"

#include <QStringList>
#include <QUuid>

#include <algorithm>
#include <array>
#include <cmath>

using namespace Qt::StringLiterals;

namespace rb::core
{

namespace
{

/// The days of the week as the file names them, Monday first, which is also
/// the order of the bits in Timer::days.
constexpr std::array<QLatin1StringView, 7>
    kDayNames{"mon"_L1, "tue"_L1, "wed"_L1, "thu"_L1, "fri"_L1, "sat"_L1, "sun"_L1};

/// The same days as systemd.time(7) spells them.
constexpr std::array<QLatin1StringView, 7>
    kCalendarDays{"Mon"_L1, "Tue"_L1, "Wed"_L1, "Thu"_L1, "Fri"_L1, "Sat"_L1, "Sun"_L1};

constexpr std::uint8_t kWeekdays = 0b0011111;
constexpr std::uint8_t kEveryDay = 0b1111111;

constexpr auto kTimeFormat = "HH:mm"_L1;
constexpr auto kDateFormat = "yyyy-MM-dd"_L1;

std::optional<TimerRepeat> repeatFromName(const QString &name)
{
    if (name == "once"_L1) {
        return TimerRepeat::Once;
    }
    if (name == "daily"_L1) {
        return TimerRepeat::Daily;
    }
    if (name == "weekdays"_L1) {
        return TimerRepeat::Weekdays;
    }
    if (name == "days"_L1) {
        return TimerRepeat::Days;
    }
    return std::nullopt;
}

/// The days a timer starts on, as a mask, whatever its repeat.
std::uint8_t daysOf(const Timer &timer)
{
    switch (timer.repeat) {
    case TimerRepeat::Once:
        return 0;
    case TimerRepeat::Daily:
        return kEveryDay;
    case TimerRepeat::Weekdays:
        return kWeekdays;
    case TimerRepeat::Days:
        return timer.days & kEveryDay;
    }
    return 0;
}

/// The days of \a mask for `OnCalendar=`, with every run of three or more
/// days as a range, since `Mon..Fri` is what a person reading
/// `systemctl --user list-timers` expects to see.
QString calendarDays(std::uint8_t mask)
{
    QStringList parts;
    int day = 0;
    while (day < 7) {
        if ((mask & (1U << day)) == 0) {
            ++day;
            continue;
        }
        int last = day;
        while (last + 1 < 7 && (mask & (1U << (last + 1))) != 0) {
            ++last;
        }
        if (last - day >= 2) {
            parts.append(kCalendarDays.at(day) + ".."_L1 + kCalendarDays.at(last));
        } else {
            for (int each = day; each <= last; ++each) {
                parts.append(kCalendarDays.at(each));
            }
        }
        day = last + 1;
    }
    return parts.join(u',');
}

} // namespace

bool timerIdIsValid(QStringView id)
{
    if (id.isEmpty() || id.size() > 64) {
        return false;
    }
    for (const QChar character : id) {
        const char16_t code = character.unicode();
        const bool allowed = (code >= u'a' && code <= u'z') || (code >= u'A' && code <= u'Z')
            || (code >= u'0' && code <= u'9') || code == u'-' || code == u'_';
        if (!allowed) {
            return false;
        }
    }
    return true;
}

std::optional<Timer> timerFromJson(const QJsonObject &json)
{
    Timer timer;
    timer.id = json.value("id"_L1).toString();
    if (!timerIdIsValid(timer.id)) {
        return std::nullopt;
    }
    if (const QJsonValue enabled = json.value("enabled"_L1); !enabled.isUndefined()) {
        if (!enabled.isBool()) {
            return std::nullopt;
        }
        timer.enabled = enabled.toBool();
    }
    const std::optional<TimerRepeat> repeat = repeatFromName(json.value("repeat"_L1).toString());
    if (!repeat) {
        return std::nullopt;
    }
    timer.repeat = *repeat;
    timer.time = QTime::fromString(json.value("time"_L1).toString(), kTimeFormat);
    if (!timer.time.isValid()) {
        return std::nullopt;
    }
    if (timer.repeat == TimerRepeat::Once) {
        timer.date = QDate::fromString(json.value("date"_L1).toString(), kDateFormat);
        if (!timer.date.isValid()) {
            return std::nullopt;
        }
    }
    if (timer.repeat == TimerRepeat::Days) {
        const QJsonValue days = json.value("days"_L1);
        if (!days.isArray()) {
            return std::nullopt;
        }
        for (const QJsonValue &day : days.toArray()) {
            const QString name = day.toString();
            const auto found =
                std::ranges::find_if(kDayNames, [&name](QLatin1StringView each) { return name == each; });
            if (!day.isString() || found == kDayNames.end()) {
                return std::nullopt;
            }
            timer.days |= static_cast<std::uint8_t>(1U << (found - kDayNames.begin()));
        }
        // **No days is no timer**: it would never run, and a unit that
        // never elapses is clutter in `systemctl --user list-timers`.
        if (timer.days == 0) {
            return std::nullopt;
        }
    }
    const QJsonValue duration = json.value("duration"_L1);
    const double seconds = duration.toDouble(-1.0);
    if (!duration.isDouble() || seconds != std::floor(seconds) || seconds < 1.0 || seconds > kTimerMaxDurationSeconds) {
        return std::nullopt;
    }
    timer.durationSeconds = static_cast<int>(seconds);
    if (const QJsonValue quit = json.value("quitApplications"_L1); !quit.isUndefined()) {
        if (!quit.isBool()) {
            return std::nullopt;
        }
        timer.quitApplications = quit.toBool();
    }
    return timer;
}

QJsonObject timerToJson(const Timer &timer)
{
    QJsonObject json{
        {"id"_L1, timer.id},
        {"enabled"_L1, timer.enabled},
        {"repeat"_L1, timerRepeatName(timer.repeat)},
        {"time"_L1, timer.time.toString(kTimeFormat)},
        {"duration"_L1, timer.durationSeconds},
        {"quitApplications"_L1, timer.quitApplications},
    };
    if (timer.repeat == TimerRepeat::Once) {
        json.insert("date"_L1, timer.date.toString(kDateFormat));
    }
    if (timer.repeat == TimerRepeat::Days) {
        QJsonArray days;
        for (std::size_t day = 0; day < kDayNames.size(); ++day) {
            if ((timer.days & (1U << day)) != 0) {
                days.append(QString(kDayNames.at(day)));
            }
        }
        json.insert("days"_L1, days);
    }
    return json;
}

QList<Timer> timersFromJson(const QJsonArray &timers)
{
    QList<Timer> read;
    for (const QJsonValue &value : timers) {
        if (std::optional<Timer> timer = timerFromJson(value.toObject())) {
            read.append(*timer);
        }
    }
    return read;
}

QJsonArray timersWithNewIds(const QJsonArray &timers)
{
    QJsonArray renamed;
    for (const QJsonValue &value : timers) {
        if (!value.isObject()) {
            renamed.append(value);
            continue;
        }
        QJsonObject timer = value.toObject();
        timer.insert("id"_L1, newTimerId());
        renamed.append(timer);
    }
    return renamed;
}

QString newTimerId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString timerRepeatName(TimerRepeat repeat)
{
    switch (repeat) {
    case TimerRepeat::Once:
        return u"once"_s;
    case TimerRepeat::Daily:
        return u"daily"_s;
    case TimerRepeat::Weekdays:
        return u"weekdays"_s;
    case TimerRepeat::Days:
        return u"days"_s;
    }
    return {};
}

QString onCalendar(const Timer &timer)
{
    // Seconds written out, so the expression reads the same as the one
    // `systemd-analyze calendar` normalises it to.
    const QString time = timer.time.toString(u"HH:mm:00"_s);
    if (timer.repeat == TimerRepeat::Once) {
        return timer.date.toString(kDateFormat) + u' ' + time;
    }
    const std::uint8_t days = daysOf(timer);
    if (days == 0) {
        return {};
    }
    if (days == kEveryDay) {
        return u"*-*-* "_s + time;
    }
    return calendarDays(days) + u" *-*-* "_s + time;
}

std::optional<QDateTime> nextRun(const Timer &timer, const QDateTime &after)
{
    const QDateTime local = after.toLocalTime();
    if (timer.repeat == TimerRepeat::Once) {
        const QDateTime at(timer.date, timer.time);
        return at > local ? std::optional<QDateTime>(at) : std::nullopt;
    }
    const std::uint8_t days = daysOf(timer);
    if (days == 0) {
        return std::nullopt;
    }
    // Today, and then each of the next seven days: a day of the week named
    // in the mask comes round within a week.
    for (int offset = 0; offset <= 7; ++offset) {
        const QDate date = local.date().addDays(offset);
        if ((days & (1U << (date.dayOfWeek() - 1))) == 0) {
            continue;
        }
        const QDateTime at(date, timer.time);
        if (at > local) {
            return at;
        }
    }
    return std::nullopt;
}

} // namespace rb::core
