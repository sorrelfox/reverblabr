// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "recordinglist.h"

#include "appshell.h"
#include "core/enginefacade.h"

#include <KFormat>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>

#include <cmath>

using namespace Qt::StringLiterals;

namespace rb::ui
{

namespace
{

struct Row {
    QString path;
    QString name;
    QDateTime started;
    double seconds = 0.0;
    QString format;
    double sizeBytes = 0.0;
    bool inProgress = false;
    QVariantList peaks;
    int channels = 0;
    int sampleRate = 0;
    QVariantMap tags;
};

[[nodiscard]] Row rowFrom(const QJsonObject &object)
{
    Row row;
    row.path = object.value(u"path"_s).toString();
    row.name = object.value(u"name"_s).toString();
    row.started = QDateTime::fromString(object.value(u"started"_s).toString(), Qt::ISODateWithMs).toLocalTime();
    row.seconds = object.value(u"seconds"_s).toDouble();
    row.format = object.value(u"format"_s).toString();
    row.sizeBytes = object.value(u"sizeBytes"_s).toDouble();
    row.inProgress = object.value(u"inProgress"_s).toBool();
    row.peaks = object.value(u"peaks"_s).toArray().toVariantList();
    row.channels = object.value(u"channels"_s).toInt();
    row.sampleRate = object.value(u"sampleRate"_s).toInt();
    row.tags = object.value(u"tags"_s).toObject().toVariantMap();
    return row;
}

} // namespace

class RecordingList::Private
{
public:
    QString sessionId;
    QList<Row> rows;
    QPointer<core::EngineFacade> facade;
};

RecordingList::RecordingList(QObject *parent)
    : QAbstractListModel(parent)
    , d(std::make_unique<Private>())
{
    d->facade = AppShell::engine();
    if (!d->facade) {
        return;
    }
    connect(
        d->facade, &core::EngineFacade::recordingsChanged, this, [this](const QString &sessionId, const QString &json) {
            if (sessionId != d->sessionId) {
                return;
            }
            QList<Row> rows;
            for (const QJsonValue &value : QJsonDocument::fromJson(json.toUtf8()).array()) {
                rows.append(rowFrom(value.toObject()));
            }
            const bool counted = rows.size() != d->rows.size();
            beginResetModel();
            d->rows = std::move(rows);
            endResetModel();
            if (counted) {
                Q_EMIT countChanged();
            }
        });
}

RecordingList::~RecordingList() = default;

QString RecordingList::sessionId() const
{
    return d->sessionId;
}

void RecordingList::setSessionId(const QString &sessionId)
{
    if (sessionId == d->sessionId) {
        return;
    }
    d->sessionId = sessionId;
    const bool hadRows = !d->rows.isEmpty();
    beginResetModel();
    d->rows.clear();
    endResetModel();
    if (hadRows) {
        Q_EMIT countChanged();
    }
    Q_EMIT sessionIdChanged();
    // **Asked after this returns**, as the presets are: an engine in
    // the same process answers at once, and the answer would otherwise
    // arrive while a binding that set the session is still being worked out.
    if (d->facade && !sessionId.isEmpty()) {
        QMetaObject::invokeMethod(
            d->facade, [facade = d->facade, sessionId] { facade->listRecordings(sessionId); }, Qt::QueuedConnection);
    }
}

int RecordingList::count() const
{
    return static_cast<int>(d->rows.size());
}

int RecordingList::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant RecordingList::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= d->rows.size()) {
        return {};
    }
    const Row &row = d->rows.at(index.row());
    switch (role) {
    case PathRole:
        return row.path;
    case Qt::DisplayRole:
    case NameRole:
        return row.name;
    case StartedRole:
        return row.started;
    case SecondsRole:
        return row.seconds;
    case FormatRole:
        return row.format;
    case SizeBytesRole:
        return row.sizeBytes;
    case InProgressRole:
        return row.inProgress;
    case PeaksRole:
        return row.peaks;
    case ChannelsRole:
        return row.channels;
    case SampleRateRole:
        return row.sampleRate;
    case TagsRole:
        return row.tags;
    }
    return {};
}

QHash<int, QByteArray> RecordingList::roleNames() const
{
    return {
        {PathRole, "path"},
        {NameRole, "name"},
        {StartedRole, "started"},
        {SecondsRole, "seconds"},
        {FormatRole, "format"},
        {SizeBytesRole, "sizeBytes"},
        {InProgressRole, "inProgress"},
        {PeaksRole, "peaks"},
        {ChannelsRole, "channels"},
        {SampleRateRole, "sampleRate"},
        {TagsRole, "tags"},
    };
}

QVariantMap RecordingList::get(int row) const
{
    QVariantMap map;
    if (row < 0 || row >= count()) {
        return map;
    }
    const QHash<int, QByteArray> names = roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it) {
        map.insert(QString::fromUtf8(it.value()), data(index(row), it.key()));
    }
    return map;
}

int RecordingList::rowOf(const QString &path) const
{
    for (qsizetype row = 0; row < d->rows.size(); ++row) {
        if (d->rows.at(row).path == path) {
            return static_cast<int>(row);
        }
    }
    return -1;
}

void RecordingList::rescan()
{
    if (d->facade) {
        d->facade->rescanRecordings();
    }
}

QString RecordingList::formatDuration(double seconds)
{
    const auto whole = static_cast<qint64>(std::floor(std::max(0.0, seconds)));
    const qint64 hours = whole / 3600;
    const qint64 minutes = (whole / 60) % 60;
    const qint64 rest = whole % 60;
    // Hours only when there are some, as a player writes a length.
    return hours > 0 ? u"%1:%2:%3"_s.arg(hours).arg(minutes, 2, 10, u'0').arg(rest, 2, 10, u'0')
                     : u"%1:%2"_s.arg(minutes).arg(rest, 2, 10, u'0');
}

QString RecordingList::formatSize(double bytes)
{
    // Powers of a thousand, "MB", as a file manager on this desktop writes
    // a file's size by default and as the design does.
    return KFormat().formatByteSize(bytes, 1, KFormat::MetricBinaryDialect);
}

QString RecordingList::formatName(const QString &format)
{
    return format.toUpper();
}

} // namespace rb::ui
