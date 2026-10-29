// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "timerschedule.h"

#include "core/enginefacade.h"
#include "core/timers.h"
#include "timerunits.h"

#include <QDBusError>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QStringList>

#include <map>

using namespace Qt::StringLiterals;

Q_STATIC_LOGGING_CATEGORY(lcTimers, "reverblabr.daemon.timers")

namespace rb::daemon
{

namespace
{

constexpr auto kListDelay = std::chrono::milliseconds(100);

/// Whether the file at \a path was written here: its first line says so.
bool isOurs(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    return file.readLine().trimmed() == QByteArrayView(timerunits::kWrittenBy);
}

QByteArray contentsOf(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool writeFile(const QString &path, const QString &text)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(text.toUtf8());
    return file.commit();
}

struct Units {
    QString timer;
    QString service;
};

} // namespace

TimerSchedule::TimerSchedule(core::EngineFacade *engine,
                             QDBusConnection bus,
                             QString unitDirectory,
                             QString ctlPath,
                             std::function<QString()> listing,
                             QObject *parent)
    : QObject(parent)
    , m_engine(engine)
    , m_bus(std::move(bus))
    , m_directory(std::move(unitDirectory))
    , m_ctlPath(std::move(ctlPath))
    , m_listing(std::move(listing))
{
    m_listSoon.setSingleShot(true);
    m_listSoon.setInterval(kListDelay);
    connect(&m_listSoon, &QTimer::timeout, this, [this] { reconcile(m_listing()); });

    connect(m_engine, &core::EngineFacade::sessionsListed, this, &TimerSchedule::reconcile);
    connect(m_engine, &core::EngineFacade::sessionOpened, this, [this] { m_listSoon.start(); });
    connect(m_engine, &core::EngineFacade::sessionClosed, this, [this] { m_listSoon.start(); });
    connect(m_engine, &core::EngineFacade::sessionPatch, this, &TimerSchedule::patched);
    // **What the units are now, and what the timers want**, before anything
    // changes: a unit left behind by a session deleted while the engine was
    // not running goes, and one somebody deleted by hand comes back.
    m_listSoon.start();
}

QString TimerSchedule::userUnitDirectory()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)).filePath(u"systemd/user"_s);
}

void TimerSchedule::refresh()
{
    m_listSoon.stop();
    reconcile(m_listing());
}

void TimerSchedule::patched(const QString &sessionId, quint64 revision, const QString &patchJson)
{
    Q_UNUSED(sessionId)
    Q_UNUSED(revision)
    const QJsonObject patch = QJsonDocument::fromJson(patchJson.toUtf8()).object();
    const QString op = patch.value("op"_L1).toString();
    // A renamed session's units say its new name.
    const bool sessionRenamed = op == "rename"_L1 && patch.value("blockId"_L1).toString().isEmpty();
    if (op == "setTimer"_L1 || op == "removeTimer"_L1 || sessionRenamed) {
        m_listSoon.start();
    }
}

void TimerSchedule::reconcile(const QString &sessionsJson)
{
    if (m_busy) {
        m_again = true;
        return;
    }
    const QDir directory(m_directory);

    // **What the timers want**: two units for each that is on.
    std::map<QString, Units> wanted;
    const QJsonArray sessions = QJsonDocument::fromJson(sessionsJson.toUtf8()).array();
    for (const QJsonValue &value : sessions) {
        const QJsonObject session = value.toObject();
        const QString name = session.value("name"_L1).toString();
        for (const core::Timer &timer : core::timersFromJson(session.value("timers"_L1).toArray())) {
            if (!timer.enabled) {
                continue;
            }
            const QString base = timerunits::baseName(timer.id);
            // Two sessions with one timer between them would share its
            // units; the first keeps them.
            if (wanted.contains(base)) {
                qCWarning(lcTimers) << "two sessions have the timer" << timer.id << "; only the first is scheduled";
                continue;
            }
            wanted.emplace(base,
                           Units{timerunits::timerUnit(timer, name), timerunits::serviceUnit(timer, name, m_ctlPath)});
        }
    }

    // **What is there**, of ours.
    QSet<QString> present;
    const QStringList files =
        directory.entryList({QLatin1StringView(timerunits::kPrefix) + u'*'}, QDir::Files | QDir::NoDotAndDotDot);
    for (const QString &file : files) {
        const QString id = timerunits::timerIdOf(file);
        if (!id.isEmpty() && isOurs(directory.filePath(file))) {
            present.insert(timerunits::baseName(id));
        }
    }

    QStringList removed;
    for (const QString &base : std::as_const(present)) {
        if (!wanted.contains(base)) {
            removed.append(base);
        }
    }
    std::map<QString, Units> changed;
    for (const auto &[base, units] : wanted) {
        if (contentsOf(directory.filePath(base + u".timer"_s)) != units.timer.toUtf8()
            || contentsOf(directory.filePath(base + u".service"_s)) != units.service.toUtf8()) {
            changed.emplace(base, units);
        }
    }
    if (removed.isEmpty() && changed.empty()) {
        Q_EMIT settled();
        return;
    }
    m_busy = true;

    // **Stopped and disabled before their files go**, so the manager
    // forgets them rather than holding on to a unit that is not there.
    for (const QString &base : std::as_const(removed)) {
        qCInfo(lcTimers) << "removing" << base << ": no timer wants it any more";
        m_steps.emplace_back([this, base] { call("StopUnit", {QString(base + u".timer"_s), u"replace"_s}); });
        m_steps.emplace_back([this, base] { call("DisableUnitFiles", {QStringList{base + u".timer"_s}, false}); });
    }
    m_steps.emplace_back([this, directory, removed, changed] {
        for (const QString &base : removed) {
            for (const QString &suffix : {u".timer"_s, u".service"_s}) {
                const QString path = directory.filePath(base + suffix);
                if (isOurs(path) && !QFile::remove(path)) {
                    qCWarning(lcTimers) << "could not remove" << path;
                }
            }
            // A link the manager could not take away, with no manager to
            // ask; never anything but a link of that name.
            const QFileInfo wants(directory.filePath(u"timers.target.wants/"_s + base + u".timer"_s));
            if (wants.isSymLink()) {
                QFile::remove(wants.filePath());
            }
        }
        if (!changed.empty() && !QDir().mkpath(directory.path())) {
            qCWarning(lcTimers) << "could not make" << directory.path();
        }
        for (const auto &[base, units] : changed) {
            qCInfo(lcTimers) << "writing" << base;
            if (!writeFile(directory.filePath(base + u".timer"_s), units.timer)
                || !writeFile(directory.filePath(base + u".service"_s), units.service)) {
                qCWarning(lcTimers) << "could not write the units of" << base << "in" << directory.path();
            }
        }
        next();
    });
    // **Then as `systemctl --user daemon-reload`, `enable` and `restart`**:
    // the manager reads the new files, the timer is started at every login
    // from now on, and its next time is worked out from what it now says.
    m_steps.emplace_back([this] { call("Reload", {}); });
    for (const auto &[base, units] : changed) {
        m_steps.emplace_back(
            [this, base] { call("EnableUnitFiles", {QStringList{base + u".timer"_s}, false, false}); });
        m_steps.emplace_back([this, base] { call("RestartUnit", {QString(base + u".timer"_s), u"replace"_s}); });
    }
    next();
}

void TimerSchedule::call(const char *method, const QVariantList &arguments)
{
    QDBusMessage message = QDBusMessage::createMethodCall(u"org.freedesktop.systemd1"_s,
                                                          u"/org/freedesktop/systemd1"_s,
                                                          u"org.freedesktop.systemd1.Manager"_s,
                                                          QLatin1StringView(method));
    message.setArguments(arguments);
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, method](QDBusPendingCallWatcher *finished) {
        finished->deleteLater();
        if (finished->isError()) {
            const QDBusError error = finished->error();
            if (error.type() == QDBusError::ServiceUnknown
                || error.name() == "org.freedesktop.DBus.Error.NameHasNoOwner"_L1) {
                if (!std::exchange(m_saidNoManager, true)) {
                    qCWarning(lcTimers) << "no systemd user instance on the bus: the timers' units are written, "
                                           "but nothing will start them";
                }
            } else if (error.name() != "org.freedesktop.systemd1.NoSuchUnit"_L1) {
                // A unit that is not loaded has nothing to stop.
                qCWarning(lcTimers) << method << "failed:" << error.name() << error.message();
            }
        }
        next();
    });
}

void TimerSchedule::next()
{
    if (!m_steps.empty()) {
        const Step step = std::move(m_steps.front());
        m_steps.pop_front();
        step();
        return;
    }
    m_busy = false;
    Q_EMIT settled();
    if (std::exchange(m_again, false)) {
        reconcile(m_listing());
    }
}

} // namespace rb::daemon
