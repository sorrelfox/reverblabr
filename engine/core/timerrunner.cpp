// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/timerrunner.h"

#include "core/enginefacade.h"
#include "core/timers.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

#include <algorithm>
#include <chrono>

using namespace Qt::StringLiterals;

Q_STATIC_LOGGING_CATEGORY(lcTimers, "reverblabr.engine.timers")

namespace rb::core
{

namespace
{

/// The longest the end of a run goes unchecked: the countdown stops while
/// the machine sleeps, the clock does not.
constexpr auto kLongestWait = std::chrono::seconds(30);

} // namespace

TimerRunner::TimerRunner(EngineFacade *engine,
                         std::function<QString()> listing,
                         std::function<void(const QString &)> quitApplications,
                         QObject *parent)
    : QObject(parent)
    , m_engine(engine)
    , m_listing(std::move(listing))
    , m_quitApplications(std::move(quitApplications))
{
    m_check.setSingleShot(true);
    connect(&m_check, &QTimer::timeout, this, &TimerRunner::check);
    connect(m_engine, &EngineFacade::sessionOpened, this, &TimerRunner::opened);
    connect(m_engine, &EngineFacade::stateChanged, this, &TimerRunner::stateChanged);
}

QString TimerRunner::timerListing(const QDateTime &now) const
{
    QJsonArray listed;
    const QJsonArray sessions = QJsonDocument::fromJson(m_listing().toUtf8()).array();
    for (const QJsonValue &value : sessions) {
        const QJsonObject session = value.toObject();
        for (const QJsonValue &each : session.value("timers"_L1).toArray()) {
            const std::optional<Timer> timer = timerFromJson(each.toObject());
            if (!timer) {
                continue;
            }
            QJsonObject entry = timerToJson(*timer);
            entry.insert("sessionId"_L1, session.value("id"_L1));
            entry.insert("sessionName"_L1, session.value("name"_L1));
            entry.insert("onCalendar"_L1, onCalendar(*timer));
            if (const std::optional<QDateTime> next = nextRun(*timer, now)) {
                entry.insert("nextRun"_L1, static_cast<double>(next->toMSecsSinceEpoch()));
            }
            if (const auto run = m_runs.constFind(timer->id); run != m_runs.cend() && run->started) {
                entry.insert("endsAt"_L1, static_cast<double>(run->endsAt.toMSecsSinceEpoch()));
            }
            listed.append(entry);
        }
    }
    return QString::fromUtf8(QJsonDocument(listed).toJson(QJsonDocument::Compact));
}

void TimerRunner::fire(const QString &timerId)
{
    const QJsonArray sessions = QJsonDocument::fromJson(m_listing().toUtf8()).array();
    for (const QJsonValue &value : sessions) {
        const QJsonObject session = value.toObject();
        for (const QJsonValue &each : session.value("timers"_L1).toArray()) {
            const std::optional<Timer> timer = timerFromJson(each.toObject());
            if (!timer || timer->id != timerId) {
                continue;
            }
            const QString sessionId = session.value("id"_L1).toString();
            const QString state = session.value("state"_L1).toString();
            const QDateTime endsAt =
                QDateTime::currentDateTimeUtc().addSecs(static_cast<qint64>(timer->durationSeconds));
            qCInfo(lcTimers) << "timer" << timerId << "of" << session.value("name"_L1).toString() << "fired";

            const bool ours = m_runs.contains(timerId) && m_runs.value(timerId).started;
            if (state == sessionState::closed) {
                m_runs.insert(timerId, Run{sessionId, endsAt, timer->quitApplications, false});
                m_engine->openSession(sessionId);
            } else if (state == sessionState::idle || state == sessionState::error) {
                m_runs.insert(timerId, Run{sessionId, endsAt, timer->quitApplications, false});
                begin(timerId);
            } else if (ours) {
                // Fired again during its own run: it runs on from now.
                m_runs[timerId].endsAt = endsAt;
            } else {
                qCInfo(lcTimers) << "its session is running already, and is left to whoever started it";
            }
            Q_EMIT fired(timerId, sessionId);
            check();
            return;
        }
    }
    qCWarning(lcTimers) << "no session has a timer" << timerId;
    Q_EMIT notFound(timerId);
}

void TimerRunner::opened(const QString &sessionId)
{
    const QStringList timerIds = m_runs.keys();
    for (const QString &timerId : timerIds) {
        const Run &run = m_runs.value(timerId);
        if (run.sessionId == sessionId && !run.started) {
            begin(timerId);
        }
    }
}

void TimerRunner::stateChanged(const QString &sessionId, const QString &state)
{
    // **Stopped by somebody, or unable to run**: the run is over, and a
    // session started again later is not the timer's to stop.
    if (state != sessionState::idle && state != sessionState::error) {
        return;
    }
    m_runs.removeIf(
        [&sessionId](const QHash<QString, Run>::iterator &run) { return run->sessionId == sessionId && run->started; });
}

void TimerRunner::begin(const QString &timerId)
{
    Run &run = m_runs[timerId];
    run.started = true;
    m_engine->start(run.sessionId);
}

void TimerRunner::check()
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    std::chrono::milliseconds wait = kLongestWait;
    const QStringList timerIds = m_runs.keys();
    for (const QString &timerId : timerIds) {
        const Run run = m_runs.value(timerId);
        if (now < run.endsAt) {
            wait = std::min(wait, std::chrono::milliseconds(now.msecsTo(run.endsAt)));
            continue;
        }
        m_runs.remove(timerId);
        if (!run.started) {
            continue; // its session never opened
        }
        qCInfo(lcTimers) << "timer" << timerId << "is over: stopping" << run.sessionId;
        m_engine->stop(run.sessionId);
        if (run.quitApplications && m_quitApplications) {
            m_quitApplications(run.sessionId);
        }
        Q_EMIT ended(timerId, run.sessionId);
    }
    if (m_runs.isEmpty()) {
        m_check.stop();
    } else {
        m_check.start(std::max(wait, std::chrono::milliseconds(1)));
    }
}

} // namespace rb::core
