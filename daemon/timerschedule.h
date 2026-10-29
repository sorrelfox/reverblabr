// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QString>
#include <QTimer>

#include <chrono>
#include <deque>
#include <functional>

namespace rb::core
{
class EngineFacade;
}

namespace rb::daemon
{

/// Keeps a systemd user timer for every session's timer that is on
/// (core/timers.h, timerunits.h), so that a timer starts its session with no
/// window open and no engine running: the timer's service runs
/// `reverblabr-ctl timer-fire`, whose call starts the engine.
///
/// - **Writes the units into \a unitDirectory**, the user's own unit
///   directory (`$XDG_CONFIG_HOME/systemd/user`), as `systemctl --user
///   edit --force --full` would: a unit that is new or changed is written,
///   the manager reloaded, and the timer enabled (so it is there after the
///   next login) and restarted (so its next time is worked out again).
/// - **Removes the ones no timer wants**, a timer switched off or deleted,
///   or its session deleted: stopped and disabled through the manager
///   first, then their files. Only files that say they were written here
///   (timerunits::kWrittenBy) are ever touched.
/// - **Looks when anything may have changed**, as AutoRun does: at start,
///   when a timer or a session's name is edited, a session opens or closes,
///   and whenever anybody lists the sessions. \a listing is the engine's
///   list, read without being announced.
/// - **Never over somebody else's**: a unit of one of our names that does
///   not say it was written here is left as it is, and said in the log.
/// - **Tries again**: a timer whose unit is not enabled (the manager refused,
///   or there was none) is enabled at the next look, and a refusal has it
///   look again a minute later.
/// - **A one-off that has passed** has its units removed, so they are not
///   left enabled for ever.
/// - **Without a service manager** (a desktop with no systemd user
///   instance) the files are still kept in step, and the log says the
///   manager could not be asked.
///
/// Follows \a engine, which must outlive this.
class TimerSchedule : public QObject
{
    Q_OBJECT

public:
    TimerSchedule(core::EngineFacade *engine,
                  QDBusConnection bus,
                  QString unitDirectory,
                  QString ctlPath,
                  std::function<QString()> listing,
                  QObject *parent = nullptr);

    /// Where the units are written: `$XDG_CONFIG_HOME/systemd/user`.
    [[nodiscard]] static QString userUnitDirectory();

    /// Looks again now rather than after the usual pause.
    void refresh();

    /// How long after a call the manager refused it looks again; a minute
    /// unless a test says otherwise.
    void setRetryInterval(std::chrono::milliseconds interval);

Q_SIGNALS:
    /// A look at the units is over: every file is written or removed, and
    /// the manager has answered every call about them.
    void settled();

private:
    /// One thing to do, in order: a manager call, or a step on disk.
    using Step = std::function<void()>;

    void patched(const QString &sessionId, quint64 revision, const QString &patchJson);
    void reconcile(const QString &sessionsJson);
    void call(const char *method, const QVariantList &arguments);
    void next();

    core::EngineFacade *m_engine = nullptr;
    QDBusConnection m_bus;
    QString m_directory;
    QString m_ctlPath;
    std::function<QString()> m_listing;
    QTimer m_listSoon;
    QTimer m_retry;
    /// The manager refused a call in this look: look again later.
    bool m_failed = false;
    std::deque<Step> m_steps;
    bool m_busy = false;
    /// Something changed while a look was under way: look again after it.
    bool m_again = false;
    /// Said once, not for every call, when there is no manager to ask.
    bool m_saidNoManager = false;
};

} // namespace rb::daemon
