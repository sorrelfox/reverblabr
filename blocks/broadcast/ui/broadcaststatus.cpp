// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "broadcaststatus.h"

#include "appshell.h"
#include "stream/streamencoder.h"

#include <QDateTime>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QPointer>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace rb::ui
{

class BroadcastStatus::Private
{
public:
    QPointer<core::EngineFacade> facade;
    QHash<QString, QJsonObject> streams;
    int revision = 0;
    std::function<qint64()> clock = [] { return QDateTime::currentMSecsSinceEpoch(); };

    [[nodiscard]] static QString keyOf(const QString &sessionId, const QString &blockId)
    {
        return sessionId + u'/' + blockId;
    }
};

BroadcastStatus::BroadcastStatus(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    attach(AppShell::engine());
}

BroadcastStatus::~BroadcastStatus() = default;

BroadcastStatus *BroadcastStatus::create(QQmlEngine *, QJSEngine *)
{
    return new BroadcastStatus;
}

void BroadcastStatus::attach(core::EngineFacade *facade)
{
    if (d->facade) {
        d->facade->disconnect(this);
    }
    d->facade = facade;
    d->streams.clear();
    if (!d->facade) {
        return;
    }
    connect(facade,
            &core::EngineFacade::broadcastStatusChanged,
            this,
            [this](const QString &sessionId, const QString &blockId, const QString &statusJson) {
                QJsonObject status = QJsonDocument::fromJson(statusJson.toUtf8()).object();
                QJsonObject &kept = d->streams[Private::keyOf(sessionId, blockId)];
                // **Whether there is a password is kept** when a status
                // does not say: a stream's own word is about the stream.
                if (!status.contains(u"passwordStored"_s) && kept.contains(u"passwordStored"_s)) {
                    status.insert(u"passwordStored"_s, kept.value(u"passwordStored"_s));
                }
                kept = status;
                ++d->revision;
                Q_EMIT changed();
            });
}

void BroadcastStatus::setClock(std::function<qint64()> clock)
{
    d->clock = std::move(clock);
}

int BroadcastStatus::revision() const
{
    return d->revision;
}

QString BroadcastStatus::state(const QString &sessionId, const QString &blockId) const
{
    return d->streams.value(Private::keyOf(sessionId, blockId)).value(u"state"_s).toString(u"idle"_s);
}

QVariantMap BroadcastStatus::status(const QString &sessionId, const QString &blockId) const
{
    return d->streams.value(Private::keyOf(sessionId, blockId)).toVariantMap();
}

bool BroadcastStatus::passwordStored(const QString &sessionId, const QString &blockId) const
{
    return d->streams.value(Private::keyOf(sessionId, blockId)).value(u"passwordStored"_s).toBool();
}

int BroadcastStatus::connectedSeconds(const QString &sessionId, const QString &blockId) const
{
    const QJsonObject status = d->streams.value(Private::keyOf(sessionId, blockId));
    const auto since = static_cast<qint64>(status.value(u"connectedSince"_s).toDouble());
    if (status.value(u"state"_s).toString() != "connected"_L1 || since <= 0) {
        return 0;
    }
    return static_cast<int>(std::max<qint64>(0, d->clock() - since) / 1000);
}

void BroadcastStatus::request(const QString &sessionId, const QString &blockId)
{
    if (d->facade && !sessionId.isEmpty() && !blockId.isEmpty()) {
        d->facade->getBroadcastStatus(sessionId, blockId);
    }
}

void BroadcastStatus::setPassword(const QString &sessionId, const QString &blockId, const QString &password)
{
    if (d->facade && !sessionId.isEmpty() && !blockId.isEmpty()) {
        d->facade->setBroadcastPassword(sessionId, blockId, password);
    }
}

QVariantMap BroadcastStatus::bitrateRange(const QString &format, bool mono)
{
    stream::StreamFormat which = stream::StreamFormat::Mp3;
    if (format == "opus"_L1) {
        which = stream::StreamFormat::Opus;
    } else if (format == "vorbis"_L1) {
        which = stream::StreamFormat::Vorbis;
    } else if (format == "aac"_L1) {
        which = stream::StreamFormat::Aac;
    }
    const stream::BitrateRange range = stream::bitrateRange(which, mono ? 1 : 2);
    return {{u"lowest"_s, range.lowest}, {u"highest"_s, range.highest}};
}

QString BroadcastStatus::bytesText(double bytes)
{
    return QLocale().formattedDataSize(static_cast<qint64>(bytes), 1, QLocale::DataSizeTraditionalFormat);
}

} // namespace rb::ui
