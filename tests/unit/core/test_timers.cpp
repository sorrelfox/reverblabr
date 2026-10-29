// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// A session's timers: how the file holds them, the systemd calendar
// expression each becomes, and when each next starts its session.

#include "core/timers.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <gtest/gtest.h>

#include <vector>

using namespace rb::core;
using namespace Qt::StringLiterals;

namespace
{

Timer at(TimerRepeat repeat, QTime time, std::uint8_t days = 0)
{
    Timer timer;
    timer.id = u"t1"_s;
    timer.repeat = repeat;
    timer.time = time;
    timer.days = days;
    return timer;
}

/// A mask from day numbers, Monday 1 to Sunday 7, as QDate counts them.
std::uint8_t days(std::initializer_list<int> numbers)
{
    std::uint8_t mask = 0;
    for (const int number : numbers) {
        mask |= static_cast<std::uint8_t>(1U << (number - 1));
    }
    return mask;
}

std::string text(const QJsonObject &json)
{
    return QJsonDocument(json).toJson(QJsonDocument::Compact).toStdString();
}

} // namespace

// **The calendar expressions systemd is given**: each repeat, and the
// ways a set of days can be written.
TEST(Timers, EachRepeatBecomesItsCalendarExpression)
{
    Timer once = at(TimerRepeat::Once, QTime(14, 30));
    once.date = QDate(2026, 11, 3);
    EXPECT_EQ(onCalendar(once), u"2026-11-03 14:30:00"_s);
    EXPECT_EQ(onCalendar(at(TimerRepeat::Daily, QTime(7, 0))), u"*-*-* 07:00:00"_s);
    EXPECT_EQ(onCalendar(at(TimerRepeat::Weekdays, QTime(9, 5))), u"Mon..Fri *-*-* 09:05:00"_s);
    EXPECT_EQ(onCalendar(at(TimerRepeat::Daily, QTime(0, 0))), u"*-*-* 00:00:00"_s);
    EXPECT_EQ(onCalendar(at(TimerRepeat::Daily, QTime(23, 59))), u"*-*-* 23:59:00"_s);
}

TEST(Timers, ChosenDaysAreListedWithRunsOfThreeOrMoreAsARange)
{
    const std::vector<std::pair<std::uint8_t, QString>> cases{
        {days({1}), u"Mon *-*-* 18:00:00"_s},
        {days({7}), u"Sun *-*-* 18:00:00"_s},
        {days({1, 3, 6}), u"Mon,Wed,Sat *-*-* 18:00:00"_s},
        {days({1, 2}), u"Mon,Tue *-*-* 18:00:00"_s},
        {days({1, 2, 3}), u"Mon..Wed *-*-* 18:00:00"_s},
        {days({1, 2, 3, 4, 5}), u"Mon..Fri *-*-* 18:00:00"_s},
        {days({6, 7}), u"Sat,Sun *-*-* 18:00:00"_s},
        {days({1, 2, 3, 5, 6, 7}), u"Mon..Wed,Fri..Sun *-*-* 18:00:00"_s},
        {days({1, 2, 4, 5, 6}), u"Mon,Tue,Thu..Sat *-*-* 18:00:00"_s},
        // Every day of the week is every day, and said so.
        {days({1, 2, 3, 4, 5, 6, 7}), u"*-*-* 18:00:00"_s},
    };
    for (const auto &[mask, expected] : cases) {
        EXPECT_EQ(onCalendar(at(TimerRepeat::Days, QTime(18, 0), mask)), expected) << int(mask);
    }
    EXPECT_TRUE(onCalendar(at(TimerRepeat::Days, QTime(18, 0), 0)).isEmpty()) << "no days never runs";
}

TEST(Timers, ATimerSurvivesTheFileInTheShapeItIsWritten)
{
    const std::vector<QJsonObject> written{
        {{u"id"_s, u"a"_s},
         {u"enabled"_s, true},
         {u"repeat"_s, u"once"_s},
         {u"date"_s, u"2026-11-03"_s},
         {u"time"_s, u"14:30"_s},
         {u"duration"_s, 3600},
         {u"quitApplications"_s, true}},
        {{u"id"_s, u"b"_s},
         {u"enabled"_s, false},
         {u"repeat"_s, u"days"_s},
         {u"days"_s, QJsonArray{u"mon"_s, u"thu"_s, u"sun"_s}},
         {u"time"_s, u"07:30"_s},
         {u"duration"_s, 1},
         {u"quitApplications"_s, false}},
        {{u"id"_s, u"c"_s},
         {u"enabled"_s, true},
         {u"repeat"_s, u"weekdays"_s},
         {u"time"_s, u"00:00"_s},
         {u"duration"_s, kTimerMaxDurationSeconds},
         {u"quitApplications"_s, false}},
    };
    for (const QJsonObject &json : written) {
        const std::optional<Timer> timer = timerFromJson(json);
        ASSERT_TRUE(timer.has_value()) << text(json);
        EXPECT_EQ(timerToJson(*timer), json);
    }
}

TEST(Timers, AnythingMissingOrWrongIsNoTimer)
{
    const QJsonObject good{
        {u"id"_s, u"good-1_A"_s}, {u"repeat"_s, u"daily"_s}, {u"time"_s, u"08:00"_s}, {u"duration"_s, 60}};
    ASSERT_TRUE(timerFromJson(good).has_value());
    // Left out, these are on and quit nothing.
    EXPECT_TRUE(timerFromJson(good)->enabled);
    EXPECT_FALSE(timerFromJson(good)->quitApplications);

    const auto with = [&good](const QString &key, const QJsonValue &value) {
        QJsonObject changed = good;
        if (value.isUndefined()) {
            changed.remove(key);
        } else {
            changed.insert(key, value);
        }
        return changed;
    };
    const std::vector<QJsonObject> broken{
        with(u"id"_s, QJsonValue::Undefined),
        with(u"id"_s, u""_s),
        // **The id is part of a file name and of a command line.**
        with(u"id"_s, u"../timer"_s),
        with(u"id"_s, u"a b"_s),
        with(u"id"_s, u"a;rm"_s),
        with(u"id"_s, QString(65, u'a')),
        with(u"repeat"_s, u"hourly"_s), // a later version's, perhaps; never run by this one
        with(u"repeat"_s, QJsonValue::Undefined),
        with(u"time"_s, u"24:00"_s),
        with(u"time"_s, u"8"_s),
        with(u"time"_s, QJsonValue::Undefined),
        with(u"duration"_s, 0),
        with(u"duration"_s, -60),
        with(u"duration"_s, 1.5),
        with(u"duration"_s, u"60"_s),
        with(u"duration"_s, kTimerMaxDurationSeconds + 1),
        with(u"duration"_s, QJsonValue::Undefined),
        with(u"enabled"_s, u"yes"_s),
        with(u"quitApplications"_s, 1),
        // A one-off needs its date, and a timer on chosen days its days.
        with(u"repeat"_s, u"once"_s),
        [&] {
            QJsonObject once = with(u"repeat"_s, u"once"_s);
            once.insert(u"date"_s, u"2026-02-30"_s);
            return once;
        }(),
        with(u"repeat"_s, u"days"_s),
        [&] {
            QJsonObject chosen = with(u"repeat"_s, u"days"_s);
            chosen.insert(u"days"_s, QJsonArray{});
            return chosen;
        }(),
        [&] {
            QJsonObject chosen = with(u"repeat"_s, u"days"_s);
            chosen.insert(u"days"_s, QJsonArray{u"mon"_s, u"funday"_s});
            return chosen;
        }(),
    };
    for (const QJsonObject &json : broken) {
        EXPECT_FALSE(timerFromJson(json).has_value()) << text(json);
    }
}

TEST(Timers, OnlyTheTimersThisVersionCanRunAreRead)
{
    const QJsonArray file{
        QJsonObject{{u"id"_s, u"a"_s}, {u"repeat"_s, u"daily"_s}, {u"time"_s, u"08:00"_s}, {u"duration"_s, 60}},
        QJsonObject{{u"at"_s, u"2026-09-20T08:00:00Z"_s}},
        u"not even an object"_s,
        QJsonObject{{u"id"_s, u"b"_s}, {u"repeat"_s, u"weekdays"_s}, {u"time"_s, u"09:00"_s}, {u"duration"_s, 60}},
    };
    const QList<Timer> timers = timersFromJson(file);
    ASSERT_EQ(timers.size(), 2);
    EXPECT_EQ(timers.at(0).id, u"a"_s);
    EXPECT_EQ(timers.at(1).id, u"b"_s);
}

// **A copy's timers are its own**: an imported session gets new ids, and
// everything else, even a timer this version cannot read, is kept.
TEST(Timers, ACopyGetsNewIdsAndKeepsEverythingElse)
{
    const QJsonArray file{
        QJsonObject{{u"id"_s, u"a"_s}, {u"repeat"_s, u"daily"_s}, {u"time"_s, u"08:00"_s}, {u"duration"_s, 60}},
        QJsonObject{{u"at"_s, u"later"_s}},
    };
    const QJsonArray copied = timersWithNewIds(file);
    ASSERT_EQ(copied.size(), 2);
    const QJsonObject first = copied.at(0).toObject();
    EXPECT_NE(first.value(u"id"_s).toString(), u"a"_s);
    EXPECT_TRUE(timerIdIsValid(first.value(u"id"_s).toString()));
    EXPECT_EQ(first.value(u"time"_s).toString(), u"08:00"_s);
    EXPECT_EQ(copied.at(1).toObject().value(u"at"_s).toString(), u"later"_s);
    EXPECT_NE(copied.at(1).toObject().value(u"id"_s).toString(), first.value(u"id"_s).toString());
}

// **When it next starts the session**, which is what the Schedule tab
// and `reverblabr-ctl timers list` say. Local time, as the timer is set.
TEST(Timers, TheNextRunIsTheFirstTimeAfterNowThatTheRuleAllows)
{
    // Wednesday 4 November 2026, 12:00.
    const QDateTime now(QDate(2026, 11, 4), QTime(12, 0));
    ASSERT_EQ(now.date().dayOfWeek(), 3);

    EXPECT_EQ(nextRun(at(TimerRepeat::Daily, QTime(13, 0)), now), QDateTime(QDate(2026, 11, 4), QTime(13, 0)));
    EXPECT_EQ(nextRun(at(TimerRepeat::Daily, QTime(11, 0)), now), QDateTime(QDate(2026, 11, 5), QTime(11, 0)));
    // Not the minute it is now: that one has started.
    EXPECT_EQ(nextRun(at(TimerRepeat::Daily, QTime(12, 0)), now), QDateTime(QDate(2026, 11, 5), QTime(12, 0)));

    // Friday at noon: Monday next.
    const QDateTime friday(QDate(2026, 11, 6), QTime(12, 0));
    EXPECT_EQ(nextRun(at(TimerRepeat::Weekdays, QTime(9, 0)), friday), QDateTime(QDate(2026, 11, 9), QTime(9, 0)));
    EXPECT_EQ(nextRun(at(TimerRepeat::Weekdays, QTime(13, 0)), friday), QDateTime(QDate(2026, 11, 6), QTime(13, 0)));

    // Wednesday only, from a Wednesday afternoon: a week on.
    EXPECT_EQ(nextRun(at(TimerRepeat::Days, QTime(9, 0), days({3})), now), QDateTime(QDate(2026, 11, 11), QTime(9, 0)));
    EXPECT_EQ(nextRun(at(TimerRepeat::Days, QTime(9, 0), days({1, 7})), now),
              QDateTime(QDate(2026, 11, 8), QTime(9, 0)));
    EXPECT_FALSE(nextRun(at(TimerRepeat::Days, QTime(9, 0), 0), now).has_value());

    Timer once = at(TimerRepeat::Once, QTime(14, 30));
    once.date = QDate(2026, 11, 4);
    EXPECT_EQ(nextRun(once, now), QDateTime(QDate(2026, 11, 4), QTime(14, 30)));
    once.time = QTime(11, 30);
    EXPECT_FALSE(nextRun(once, now).has_value()) << "a one-off in the past never runs again";
}

TEST(Timers, IdsAreLettersDigitsDashesAndUnderscores)
{
    EXPECT_TRUE(timerIdIsValid(u"0b8f8ad2-4c1e-4a4e-9a51-2f6d3c1e7b10"_s));
    EXPECT_TRUE(timerIdIsValid(newTimerId()));
    EXPECT_NE(newTimerId(), newTimerId());
    EXPECT_FALSE(timerIdIsValid(u""_s));
    EXPECT_FALSE(timerIdIsValid(u"a.b"_s));
    EXPECT_FALSE(timerIdIsValid(u"a/b"_s));
    EXPECT_FALSE(timerIdIsValid(u"ä"_s));
    EXPECT_FALSE(timerIdIsValid(u"{0b8f8ad2}"_s));
    // A command line would read these as options.
    EXPECT_FALSE(timerIdIsValid(u"-morning"_s));
    EXPECT_FALSE(timerIdIsValid(u"--help"_s));
    EXPECT_FALSE(timerIdIsValid(u"_morning"_s));
    EXPECT_TRUE(timerIdIsValid(u"morning-"_s));
}

// **When it last ran**, which a fire from systemd is checked against.
TEST(Timers, ThePreviousRunIsTheLastTimeAtOrBeforeNowThatTheRuleAllows)
{
    const QDateTime now(QDate(2026, 11, 4), QTime(12, 0)); // a Wednesday
    EXPECT_EQ(previousRun(at(TimerRepeat::Daily, QTime(12, 0)), now), now);
    EXPECT_EQ(previousRun(at(TimerRepeat::Daily, QTime(13, 0)), now), QDateTime(QDate(2026, 11, 3), QTime(13, 0)));
    // Monday to Friday, from a Monday morning: the Friday before.
    const QDateTime monday(QDate(2026, 11, 9), QTime(8, 0));
    EXPECT_EQ(previousRun(at(TimerRepeat::Weekdays, QTime(9, 0)), monday), QDateTime(QDate(2026, 11, 6), QTime(9, 0)));
    EXPECT_EQ(previousRun(at(TimerRepeat::Days, QTime(9, 0), days({3})), now),
              QDateTime(QDate(2026, 11, 4), QTime(9, 0)));
    Timer once = at(TimerRepeat::Once, QTime(14, 30));
    once.date = QDate(2026, 11, 4);
    EXPECT_FALSE(previousRun(once, now).has_value()) << "still to come";
    once.time = QTime(11, 30);
    EXPECT_EQ(previousRun(once, now), QDateTime(QDate(2026, 11, 4), QTime(11, 30)));
}

TEST(Timers, AnIdAnotherSessionHasIsTaken)
{
    const QString listing = uR"([{"id":"a","timers":[{"id":"morning"}]},{"id":"b","timers":[]}])"_s;
    EXPECT_TRUE(timerIdTakenElsewhere(listing, u"b"_s, u"morning"_s));
    EXPECT_FALSE(timerIdTakenElsewhere(listing, u"a"_s, u"morning"_s)) << "its own session's is replaced, not taken";
    EXPECT_FALSE(timerIdTakenElsewhere(listing, u"b"_s, u"evening"_s));
}
