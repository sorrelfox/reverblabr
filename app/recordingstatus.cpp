// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "recordingstatus.h"

#include "appshell.h"
#include "core/recordingnames.h"
#include "errormessages.h"

#include <QDateTime>
#include <QDir>
#include <QHash>
#include <QPointer>
#include <QUrl>

using namespace Qt::StringLiterals;

namespace rb::ui
{

class RecordingStatus::Private
{
public:
    /// One Recorder, as far as its engine has said.
    struct Recorder {
        QString state = QString(kIdle);
        QString stopReason;
        QString file;
        /// When the time counted so far was last added to, and how much it
        /// was then: the clock runs from `since` while recording.
        qint64 since = 0;
        qint64 countedMs = 0;
    };

    QPointer<core::EngineFacade> facade;
    QHash<QString, Recorder> recorders;
    int revision = 0;
    std::function<qint64()> clock = [] { return QDateTime::currentMSecsSinceEpoch(); };

    [[nodiscard]] static QString keyOf(const QString &sessionId, const QString &blockId)
    {
        return sessionId + u'/' + blockId;
    }

    [[nodiscard]] qint64 elapsedMs(const Recorder &recorder) const
    {
        return recorder.state == kRecording ? recorder.countedMs + (clock() - recorder.since) : recorder.countedMs;
    }
};

RecordingStatus::RecordingStatus(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    attach(AppShell::engine());
}

RecordingStatus::~RecordingStatus() = default;

RecordingStatus *RecordingStatus::create(QQmlEngine *, QJSEngine *)
{
    return new RecordingStatus;
}

void RecordingStatus::attach(core::EngineFacade *facade)
{
    if (d->facade) {
        d->facade->disconnect(this);
    }
    d->facade = facade;
    d->recorders.clear();
    if (!d->facade) {
        return;
    }
    const auto touched = [this] {
        ++d->revision;
        Q_EMIT changed();
    };
    connect(facade,
            &core::EngineFacade::recordingFileStarted,
            this,
            [this, touched](const QString &sessionId, const QString &blockId, const QString &path) {
                Private::Recorder &recorder = d->recorders[Private::keyOf(sessionId, blockId)];
                // **A split is a new file, not a new recording**: the clock
                // goes on. Only a recording that had ended starts again.
                if (recorder.state == kIdle || recorder.state == kStopped) {
                    recorder.countedMs = 0;
                    recorder.stopReason.clear();
                }
                if (recorder.state != kPaused) {
                    if (recorder.state != kRecording) {
                        recorder.since = d->clock();
                    }
                    recorder.state = QString(kRecording);
                }
                recorder.file = path;
                touched();
            });
    connect(facade,
            &core::EngineFacade::recordingPaused,
            this,
            [this, touched](const QString &sessionId, const QString &blockId, bool paused) {
                Private::Recorder &recorder = d->recorders[Private::keyOf(sessionId, blockId)];
                if (paused && recorder.state == kRecording) {
                    recorder.countedMs = d->elapsedMs(recorder);
                    recorder.state = QString(kPaused);
                } else if (!paused && recorder.state == kPaused) {
                    recorder.since = d->clock();
                    recorder.state = QString(kRecording);
                }
                touched();
            });
    connect(facade,
            &core::EngineFacade::recordingStopped,
            this,
            [this, touched](const QString &sessionId, const QString &blockId, const QString &reason) {
                Private::Recorder &recorder = d->recorders[Private::keyOf(sessionId, blockId)];
                recorder.countedMs = d->elapsedMs(recorder);
                recorder.state = QString(kStopped);
                recorder.stopReason = reason;
                touched();
            });
    // **A session that stops takes its Recorders with it**; their last file
    // stays, for the status line to name.
    connect(facade,
            &core::EngineFacade::stateChanged,
            this,
            [this, touched](const QString &sessionId, const QString &state) {
                if (state == core::sessionState::running) {
                    return;
                }
                bool any = false;
                for (auto it = d->recorders.begin(); it != d->recorders.end(); ++it) {
                    if (it.key().startsWith(sessionId + u'/') && it->state != kIdle) {
                        it->countedMs = d->elapsedMs(*it);
                        it->state = QString(kIdle);
                        it->stopReason.clear();
                        any = true;
                    }
                }
                if (any) {
                    touched();
                }
            });
}

void RecordingStatus::setClock(std::function<qint64()> clock)
{
    d->clock = std::move(clock);
}

int RecordingStatus::revision() const
{
    return d->revision;
}

QString RecordingStatus::state(const QString &sessionId, const QString &blockId) const
{
    const auto found = d->recorders.constFind(Private::keyOf(sessionId, blockId));
    return found == d->recorders.constEnd() ? QString(kIdle) : found->state;
}

QString RecordingStatus::stopReason(const QString &sessionId, const QString &blockId) const
{
    return d->recorders.value(Private::keyOf(sessionId, blockId)).stopReason;
}

QString RecordingStatus::file(const QString &sessionId, const QString &blockId) const
{
    return d->recorders.value(Private::keyOf(sessionId, blockId)).file;
}

int RecordingStatus::elapsedSeconds(const QString &sessionId, const QString &blockId) const
{
    const auto found = d->recorders.constFind(Private::keyOf(sessionId, blockId));
    return found == d->recorders.constEnd() ? 0 : static_cast<int>(d->elapsedMs(*found) / 1000);
}

QString RecordingStatus::clock(int seconds)
{
    const int hours = seconds / 3600;
    const int minutes = seconds / 60 % 60;
    return u"%1:%2:%3"_s.arg(hours, 2, 10, u'0').arg(minutes, 2, 10, u'0').arg(seconds % 60, 2, 10, u'0');
}

void RecordingStatus::pause(const QString &sessionId, const QString &blockId)
{
    if (d->facade) {
        d->facade->controlRecording(sessionId, blockId, u"pause"_s);
    }
}

void RecordingStatus::resume(const QString &sessionId, const QString &blockId)
{
    if (d->facade) {
        d->facade->controlRecording(sessionId, blockId, u"resume"_s);
    }
}

void RecordingStatus::split(const QString &sessionId, const QString &blockId)
{
    if (d->facade) {
        d->facade->controlRecording(sessionId, blockId, u"split"_s);
    }
}

QString RecordingStatus::namePreview(const QString &pattern,
                                     const QString &sessionName,
                                     const QString &blockName,
                                     const QString &formatId)
{
    return core::recordingFileNamePreview(pattern, sessionName, blockName, formatId);
}

QString RecordingStatus::reasonText(const QString &reason)
{
    return app::reasonMessage(reason);
}

QString RecordingStatus::defaultFolder()
{
    return core::defaultRecordingFolderPath();
}

QUrl RecordingStatus::folderUrl(const QString &folder)
{
    QString path = folder.trimmed();
    if (path.isEmpty()) {
        path = defaultFolder();
    } else if (path == "~"_L1 || path.startsWith("~/"_L1)) {
        path = QDir::homePath() + path.mid(1);
    }
    return QUrl::fromLocalFile(path);
}

QString RecordingStatus::folderFromUrl(const QUrl &url)
{
    QString path = url.toLocalFile();
    // Under the home folder it is kept as ~/…, so a session taken to
    // another account still records into that account's folder of the
    // same name.
    const QString home = QDir::homePath();
    if (path == home) {
        return u"~"_s;
    }
    if (path.startsWith(home + u'/')) {
        return u"~"_s + path.mid(home.size());
    }
    return path;
}

} // namespace rb::ui
