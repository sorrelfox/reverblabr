// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "autostartunit.h"

#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>

#include <functional>
#include <optional>

namespace rb::core
{
class EngineFacade;
}

namespace rb::daemon
{

/// Auto Run (ADR-0021): the sessions whose `autoRun` setting is
/// "login" run whenever the background engine does, and the engine's user
/// unit is started at login exactly while there is such a session.
///
/// - **Starts them.** start() lists the sessions and opens and starts every
///   one set to run at login; the daemon calls it once, as it starts, unless
///   in safe mode. One already open (a daemon opens again what was open) is
///   started where it is.
/// - **Keeps the unit in step.** Every list of sessions says whether any is
///   set to run at login; the unit is then enabled or disabled if it is not
///   already so. A change of the setting, or a session opening or closing,
///   has the list looked at again, so the answer never waits for somebody
///   else to ask; and a list somebody else asked for is looked at too, which
///   is how a session deleted without being open is noticed.
/// - **Looks without saying so.** \a listing is the engine's list of
///   sessions as SessionsListed would carry it, read directly
///   (Engine::sessionListing()): asking the engine to list them would put a
///   SessionsListed on the bus for every window, each time this looks.
///
/// Follows \a engine and drives \a unit; both must outlive this.
class AutoRun : public QObject
{
    Q_OBJECT

public:
    AutoRun(core::EngineFacade *engine,
            AutostartUnit *unit,
            std::function<QString()> listing,
            QObject *parent = nullptr);

    /// Starts every session set to run at login, once the engine has said
    /// which they are.
    void start();

    /// Whether any session is set to run at login, as the engine last said;
    /// empty until it has.
    [[nodiscard]] std::optional<bool> wanted() const;

Q_SIGNALS:
    /// A session set to run at login was asked to start.
    void started(const QString &sessionId);

private:
    void listed(const QString &sessionsJson);
    void opened(const QString &sessionId);
    void patched(const QString &sessionId, quint64 revision, const QString &patchJson);
    void unitChanged(AutostartUnit::State state);
    void reconcile();
    void listSoon();

    core::EngineFacade *m_engine = nullptr;
    AutostartUnit *m_unit = nullptr;
    std::function<QString()> m_listing;
    QTimer m_listSoon;
    bool m_starting = false;
    /// Opened here to be started: started when they have opened.
    QSet<QString> m_toStart;
    std::optional<bool> m_wanted;
    /// What was last asked of the unit, so a unit the manager will not
    /// change is not asked again and again.
    std::optional<bool> m_asked;
};

} // namespace rb::daemon
