// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "opensessions.h"

#include "core/enginefacade.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <utility>

using namespace Qt::StringLiterals;

namespace rb::daemon
{

OpenSessions::OpenSessions(core::EngineFacade *engine, QString path, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
    , m_path(std::move(path))
{
    QFile file(m_path);
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonArray ids = QJsonDocument::fromJson(file.readAll()).object().value("open"_L1).toArray();
        for (const QJsonValue &id : ids) {
            if (!id.toString().isEmpty() && !m_ids.contains(id.toString())) {
                m_ids.append(id.toString());
            }
        }
    }
    connect(engine, &core::EngineFacade::sessionOpened, this, [this](const QString &sessionId) {
        if (!m_frozen && !m_ids.contains(sessionId)) {
            m_ids.append(sessionId);
            write();
        }
    });
    connect(engine, &core::EngineFacade::sessionClosed, this, [this](const QString &sessionId) {
        if (!m_frozen && m_ids.removeAll(sessionId) > 0) {
            write();
        }
    });
    // **A session whose file has gone is not asked for again** every time a
    // daemon starts: the engine says it is not there, once, and it leaves
    // the list.
    connect(engine, &core::EngineFacade::error, this, [this](const QString &code, const QString &argsJson) {
        if (m_frozen || code != "session.notFound"_L1) {
            return;
        }
        const QString sessionId = QJsonDocument::fromJson(argsJson.toUtf8()).object().value("session"_L1).toString();
        if (m_ids.removeAll(sessionId) > 0) {
            write();
        }
    });
}

void OpenSessions::restore()
{
    // A copy: opening one says so, and the list it is read from is this one.
    const QStringList ids = m_ids;
    for (const QString &id : ids) {
        m_engine->openSession(id);
    }
}

void OpenSessions::freeze()
{
    m_frozen = true;
}

QStringList OpenSessions::ids() const
{
    return m_ids;
}

void OpenSessions::write() const
{
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }
    file.write(QJsonDocument(QJsonObject{{"open"_L1, QJsonArray::fromStringList(m_ids)}}).toJson());
    (void)file.commit();
}

} // namespace rb::daemon
