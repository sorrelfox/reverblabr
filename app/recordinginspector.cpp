// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "recordinginspector.h"

#include "appshell.h"
#include "core/enginefacade.h"
#include "errormessages.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>

using namespace Qt::StringLiterals;

namespace rb::ui
{

class RecordingInspector::Private
{
public:
    QPointer<core::EngineFacade> facade;
    QString path;
    QJsonObject details;
    bool loaded = false;
    bool gone = false;
    bool savingTags = false;
    QString refusal;
};

RecordingInspector::RecordingInspector(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->facade = AppShell::engine();
    if (!d->facade) {
        return;
    }
    connect(d->facade, &core::EngineFacade::recordingDetails, this, [this](const QString &path, const QString &json) {
        if (path != d->path) {
            return;
        }
        d->details = QJsonDocument::fromJson(json.toUtf8()).object();
        d->loaded = true;
        d->gone = false;
        Q_EMIT detailsChanged();
        if (std::exchange(d->savingTags, false)) {
            Q_EMIT tagsSaved();
        }
    });
    connect(d->facade, &core::EngineFacade::recordingRenamed, this, [this](const QString &from, const QString &to) {
        // **Whichever window renamed it**, this one follows the file.
        if (from != d->path) {
            return;
        }
        d->path = to;
        Q_EMIT pathChanged();
        Q_EMIT renamed(from, to);
        refresh();
    });
    // **Its session's list says when a file has gone**: trashed, deleted
    // outside and found by a rescan. The engine is asked again rather than
    // the list believed, and its answer of recording.notFound is "gone".
    connect(
        d->facade, &core::EngineFacade::recordingsChanged, this, [this](const QString &sessionId, const QString &json) {
            if (!d->loaded || sessionId != d->details.value(u"sessionId"_s).toString()) {
                return;
            }
            for (const QJsonValue &row : QJsonDocument::fromJson(json.toUtf8()).array()) {
                if (row.toObject().value(u"path"_s).toString() == d->path) {
                    return;
                }
            }
            refresh();
        });
    connect(d->facade, &core::EngineFacade::error, this, [this](const QString &code, const QString &argsJson) {
        if (!code.startsWith("recording."_L1)) {
            return;
        }
        const QJsonObject args = QJsonDocument::fromJson(argsJson.toUtf8()).object();
        if (args.value(u"file"_s).toString() != d->path) {
            return;
        }
        if (code == "recording.notFound"_L1) {
            d->gone = true;
            Q_EMIT detailsChanged();
            return;
        }
        if (code == "recording.renameRefused"_L1 || code == "recording.tagsNotSaved"_L1) {
            d->savingTags = false;
            // The file's name as the person knows it, not its whole path.
            QVariantMap words = args.toVariantMap();
            words.insert(u"file"_s, QFileInfo(d->path).fileName());
            d->refusal = app::errorMessage(code, words);
            Q_EMIT refusalChanged();
        }
    });
}

RecordingInspector::~RecordingInspector() = default;

QString RecordingInspector::path() const
{
    return d->path;
}

void RecordingInspector::setPath(const QString &path)
{
    if (path == d->path) {
        return;
    }
    d->path = path;
    d->details = {};
    d->loaded = false;
    d->gone = false;
    Q_EMIT pathChanged();
    Q_EMIT detailsChanged();
    // Asked after this returns, as every list is.
    if (d->facade && !path.isEmpty()) {
        QMetaObject::invokeMethod(this, &RecordingInspector::refresh, Qt::QueuedConnection);
    }
}

bool RecordingInspector::isLoaded() const
{
    return d->loaded;
}

QString RecordingInspector::baseName() const
{
    return QFileInfo(d->path).completeBaseName();
}

QString RecordingInspector::extension() const
{
    return QFileInfo(d->path).suffix();
}

double RecordingInspector::seconds() const
{
    return d->details.value(u"seconds"_s).toDouble();
}

int RecordingInspector::sampleRate() const
{
    return d->details.value(u"sampleRate"_s).toInt();
}

int RecordingInspector::channels() const
{
    return d->details.value(u"channels"_s).toInt();
}

double RecordingInspector::sizeBytes() const
{
    return d->details.value(u"sizeBytes"_s).toDouble();
}

QString RecordingInspector::format() const
{
    return d->details.value(u"format"_s).toString();
}

QList<qreal> RecordingInspector::peaks() const
{
    QList<qreal> peaks;
    for (const QJsonValue &peak : d->details.value(u"peaks"_s).toArray()) {
        peaks.append(peak.toDouble());
    }
    return peaks;
}

QVariantMap RecordingInspector::tags() const
{
    return d->details.value(u"tags"_s).toObject().toVariantMap();
}

QString RecordingInspector::refusal() const
{
    return d->refusal;
}

bool RecordingInspector::isGone() const
{
    return d->gone;
}

void RecordingInspector::refresh()
{
    if (d->facade && !d->path.isEmpty()) {
        d->facade->getRecording(d->path);
    }
}

void RecordingInspector::rename(const QString &baseName)
{
    if (!d->facade || d->path.isEmpty() || baseName.trimmed() == this->baseName()) {
        return;
    }
    d->refusal.clear();
    Q_EMIT refusalChanged();
    d->facade->renameRecording(d->path, baseName);
}

void RecordingInspector::saveTags(const QVariantMap &tags)
{
    if (!d->facade || d->path.isEmpty()) {
        return;
    }
    d->refusal.clear();
    Q_EMIT refusalChanged();
    d->savingTags = true;
    d->facade->setRecordingTags(
        d->path, QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(tags)).toJson(QJsonDocument::Compact)));
}

} // namespace rb::ui
