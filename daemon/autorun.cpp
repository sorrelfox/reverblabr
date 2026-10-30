// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "autorun.h"

#include "core/enginefacade.h"
#include "core/session.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSettings>
#include <QStandardPaths>

using namespace Qt::StringLiterals;

Q_LOGGING_CATEGORY(lcAutoRun, "reverblabr.daemon.autorun")

namespace rb::daemon
{

namespace
{

/// How long a burst of changes is gathered before the sessions are listed
/// once for all of them.
constexpr auto kListDelay = std::chrono::milliseconds(100);

} // namespace

AutoRun::AutoRun(core::EngineFacade *engine, AutostartUnit *unit, std::function<QString()> listing, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
    , m_unit(unit)
    , m_listing(std::move(listing))
{
    m_listSoon.setSingleShot(true);
    m_listSoon.setInterval(kListDelay);
    connect(&m_listSoon, &QTimer::timeout, this, [this] { listed(m_listing()); });

    connect(m_engine, &core::EngineFacade::sessionsListed, this, &AutoRun::listed);
    connect(m_engine, &core::EngineFacade::sessionOpened, this, &AutoRun::opened);
    connect(m_engine, &core::EngineFacade::sessionClosed, this, &AutoRun::listSoon);
    connect(m_engine, &core::EngineFacade::sessionPatch, this, &AutoRun::patched);
    connect(m_unit, &AutostartUnit::stateChanged, this, &AutoRun::unitChanged);

    // **What the unit is now, and what the sessions want**, before anything
    // changes: a unit left enabled by a session since deleted is disabled,
    // and one a session wants but that was disabled by hand is enabled.
    m_unit->refresh();
    listSoon();
}

void AutoRun::start()
{
    m_starting = true;
    m_listSoon.stop();
    listed(m_listing());
}

std::optional<bool> AutoRun::wanted() const
{
    return m_wanted;
}

void AutoRun::listed(const QString &sessionsJson)
{
    bool wanted = false;
    const QJsonArray sessions = QJsonDocument::fromJson(sessionsJson.toUtf8()).array();
    for (const QJsonValue &value : sessions) {
        const QJsonObject session = value.toObject();
        if (session.value("autoRun"_L1).toString() != QLatin1StringView(core::autoRun::login)) {
            continue;
        }
        wanted = true;
        if (!m_starting) {
            continue;
        }
        const QString id = session.value("id"_L1).toString();
        const QString state = session.value("state"_L1).toString();
        // **Opened first when it is only on disk**, then started once it has
        // opened; one the daemon has open already is started where it is.
        // One starting or running is left as it is.
        if (state == core::sessionState::closed) {
            m_toStart.insert(id);
            m_engine->openSession(id);
        } else if (state == core::sessionState::idle || state == core::sessionState::error) {
            qCInfo(lcAutoRun) << "starting" << session.value("name"_L1).toString() << "at login";
            m_engine->start(id);
            Q_EMIT started(id);
        }
    }
    m_starting = false;
    m_wanted = wanted;
    reconcile();
}

void AutoRun::opened(const QString &sessionId)
{
    if (m_toStart.remove(sessionId)) {
        qCInfo(lcAutoRun) << "starting" << sessionId << "at login";
        m_engine->start(sessionId);
        Q_EMIT started(sessionId);
    }
    listSoon();
}

void AutoRun::patched(const QString &sessionId, quint64 revision, const QString &patchJson)
{
    Q_UNUSED(sessionId)
    Q_UNUSED(revision)
    const QJsonObject patch = QJsonDocument::fromJson(patchJson.toUtf8()).object();
    if (patch.value("op"_L1).toString() == "setSessionSetting"_L1 && patch.value("key"_L1).toString() == "autoRun"_L1) {
        listSoon();
    }
}

void AutoRun::unitChanged(AutostartUnit::State state)
{
    Q_UNUSED(state)
    reconcile();
}

void AutoRun::reconcile()
{
    if (!m_wanted.has_value()) {
        return;
    }
    const bool wanted = *m_wanted;
    switch (m_unit->state()) {
    case AutostartUnit::State::Unknown:
        return; // the manager's answer is on its way
    case AutostartUnit::State::Enabled:
        if (wanted) {
            m_asked.reset();
        } else if (m_asked != false) {
            qCInfo(lcAutoRun) << "no session runs at login any more: disabling" << AutostartUnit::kUnitName;
            m_asked = false;
            m_unit->setEnabled(false);
        }
        return;
    case AutostartUnit::State::Disabled:
        if (!wanted) {
            m_asked.reset();
        } else if (m_asked != true) {
            qCInfo(lcAutoRun) << "a session runs at login: enabling" << AutostartUnit::kUnitName;
            m_asked = true;
            m_unit->setEnabled(true);
        }
        return;
    case AutostartUnit::State::Masked:
    case AutostartUnit::State::NotInstalled:
    case AutostartUnit::State::NoManager:
    case AutostartUnit::State::Failed:
        // **Said, and left alone**: a masked unit is somebody's decision, and
        // the others are nothing this process can put right.
        if (wanted) {
            qCWarning(lcAutoRun) << "a session is set to run at login, but" << AutostartUnit::kUnitName
                                 << "cannot be enabled:" << m_unit->state() << m_unit->detail();
        }
        return;
    }
}

void AutoRun::listSoon()
{
    m_listSoon.start();
}

namespace
{

constexpr QByteArrayView kRunning = "running";
constexpr QByteArrayView kExited = "exited";

} // namespace

AutoRunMarker::AutoRunMarker(QString path)
    : m_path(std::move(path))
{
}

QString AutoRunMarker::defaultPath()
{
    // **The variable itself, not QStandardPaths::RuntimeLocation**, which
    // makes up a folder under /tmp when there is none: a marker there would
    // outlive the login it is about.
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (runtime.isEmpty()) {
        return {};
    }
    return QDir(runtime).filePath(u"reverblabr/autorun"_s);
}

bool AutoRunMarker::due() const
{
    if (m_path.isEmpty()) {
        return true;
    }
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly)) {
        return true; // the first engine of this login
    }
    return file.readAll().trimmed() != kExited;
}

void AutoRunMarker::running() const
{
    write(kRunning);
}

void AutoRunMarker::exited() const
{
    write(kExited);
}

void AutoRunMarker::write(QByteArrayView state) const
{
    if (m_path.isEmpty()) {
        return;
    }
    if (!QDir().mkpath(QFileInfo(m_path).path())) {
        qCWarning(lcAutoRun) << "cannot make the folder of" << m_path;
        return;
    }
    QFile file(m_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(state.data(), state.size()) < 0) {
        qCWarning(lcAutoRun) << "cannot write" << m_path << file.errorString();
    }
}

bool autoRunEveryEngineStart(const QString &settingsPath)
{
    if (settingsPath.isEmpty() || !QFileInfo::exists(settingsPath)) {
        return false;
    }
    // **QSettings, not KConfig**: the daemon has no KDE Frameworks
    // (ADR-0017), and a group with a plain key and a plain word in it reads
    // the same either way.
    const QSettings settings(settingsPath, QSettings::IniFormat);
    const QString key =
        QLatin1StringView(core::autoRunStart::group) + u'/' + QLatin1StringView(core::autoRunStart::key);
    return settings.value(key).toString() == QLatin1StringView(core::autoRunStart::everyEngineStart);
}

QString defaultSettingsPath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)).filePath(u"reverblabrrc"_s);
}

} // namespace rb::daemon
