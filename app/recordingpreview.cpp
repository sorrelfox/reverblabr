// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "recordingpreview.h"

#include "appshell.h"
#include "core/enginefacade.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace rb::ui
{

class RecordingPreview::Private
{
public:
    QPointer<core::EngineFacade> facade;
    QString path;
    QString state = u"stopped"_s;
    double seconds = 0.0;
    double duration = 0.0;
    /// A seek asked for before the file it is in had started, carried out
    /// once the engine says it has.
    double pendingFraction = -1.0;
};

RecordingPreview::RecordingPreview(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->facade = AppShell::engine();
    if (!d->facade) {
        return;
    }
    connect(d->facade, &core::EngineFacade::previewChanged, this, [this](const QString &json) {
        const QJsonObject status = QJsonDocument::fromJson(json.toUtf8()).object();
        d->path = status.value(u"path"_s).toString();
        d->state = status.value(u"state"_s).toString();
        d->seconds = status.value(u"seconds"_s).toDouble();
        d->duration = status.value(u"duration"_s).toDouble();
        if (d->pendingFraction >= 0.0 && d->duration > 0.0 && d->state != "stopped"_L1) {
            const double fraction = std::exchange(d->pendingFraction, -1.0);
            d->facade->seekPreview(fraction * d->duration);
        }
        Q_EMIT changed();
    });
}

RecordingPreview::~RecordingPreview() = default;

QString RecordingPreview::path() const
{
    return d->state == "stopped"_L1 ? QString{} : d->path;
}

bool RecordingPreview::isPlaying() const
{
    return d->state == "playing"_L1;
}

bool RecordingPreview::isPaused() const
{
    return d->state == "paused"_L1;
}

double RecordingPreview::seconds() const
{
    return d->seconds;
}

double RecordingPreview::duration() const
{
    return d->duration;
}

double RecordingPreview::progress() const
{
    return d->duration > 0.0 ? std::clamp(d->seconds / d->duration, 0.0, 1.0) : 0.0;
}

void RecordingPreview::toggle(const QString &path)
{
    if (path == d->path && isPlaying()) {
        pause();
    } else {
        play(path);
    }
}

void RecordingPreview::play(const QString &path)
{
    if (d->facade && !path.isEmpty()) {
        d->facade->previewRecording(path);
    }
}

void RecordingPreview::pause()
{
    if (d->facade) {
        d->facade->pausePreview();
    }
}

void RecordingPreview::seekTo(const QString &path, double fraction)
{
    if (!d->facade || path.isEmpty()) {
        return;
    }
    fraction = std::clamp(fraction, 0.0, 1.0);
    if (path == d->path && d->state != "stopped"_L1 && d->duration > 0.0) {
        d->facade->seekPreview(fraction * d->duration);
        return;
    }
    // Another file, or one that has ended: start it, and seek once the
    // engine has said how long it is.
    d->pendingFraction = fraction;
    d->facade->previewRecording(path);
}

void RecordingPreview::stop()
{
    if (d->facade) {
        d->facade->stopPreview();
    }
}

} // namespace rb::ui
