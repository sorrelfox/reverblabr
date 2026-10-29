// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/enginefacade.h"

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QUrl>

#include <functional>
#include <memory>

namespace rb::ui
{

/// **What each Recorder is doing**, as the engine has said it: for the
/// REC pill on its face and the status line in its panel, and the one place
/// its Pause and Split buttons send from.
///
/// The engine says a file started, finished, was paused or resumed, or that
/// a recording stopped by itself (the facade's Recording… signals); this
/// keeps the latest word per block of each session, and the time the
/// recording has been going, which is what "REC 00:12:04" counts. **The
/// clock is the interface's**, a wall clock that stops while the recording
/// is paused, because a count of frames would have to be sent from the
/// engine sixty times a second to say the same thing.
class RecordingStatus : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// Goes up whenever anything here changes, for bindings to depend on.
    Q_PROPERTY(int revision READ revision NOTIFY changed)

public:
    /// What a Recorder is doing. The strings are what QML compares with.
    static constexpr QLatin1StringView kIdle{"idle"}; ///< Not recording: the session is not running, or not yet.
    static constexpr QLatin1StringView kRecording{"recording"};
    static constexpr QLatin1StringView kPaused{"paused"};
    /// It ended by itself, at a time limit or on silence (stopReason()).
    static constexpr QLatin1StringView kStopped{"stopped"};

    explicit RecordingStatus(QObject *parent = nullptr);
    ~RecordingStatus() override;

    static RecordingStatus *create(QQmlEngine *engine, QJSEngine *scriptEngine);

    [[nodiscard]] Q_INVOKABLE QString state(const QString &sessionId, const QString &blockId) const;
    /// "timeLimit" or "silence" when the state is stopped, else empty.
    [[nodiscard]] Q_INVOKABLE QString stopReason(const QString &sessionId, const QString &blockId) const;
    /// The file being written, or the last one that was.
    [[nodiscard]] Q_INVOKABLE QString file(const QString &sessionId, const QString &blockId) const;
    /// How long the recording has been going, in whole seconds, pauses
    /// left out: from its first file on, across splits.
    [[nodiscard]] Q_INVOKABLE int elapsedSeconds(const QString &sessionId, const QString &blockId) const;
    /// "01:02:03", which is what the pill shows.
    [[nodiscard]] Q_INVOKABLE static QString clock(int seconds);

    /// Pause, resume or split, through the engine (ControlRecording).
    Q_INVOKABLE void pause(const QString &sessionId, const QString &blockId);
    Q_INVOKABLE void resume(const QString &sessionId, const QString &blockId);
    Q_INVOKABLE void split(const QString &sessionId, const QString &blockId);

    /// **What the next file will be called**, for the name editor's live
    /// preview: the writer's own naming, with this moment's date and time.
    [[nodiscard]] Q_INVOKABLE static QString
    namePreview(const QString &pattern, const QString &sessionName, const QString &blockName, const QString &formatId);
    /// The words for an engine's reason, "encoderMissing" and the like.
    [[nodiscard]] Q_INVOKABLE static QString reasonText(const QString &reason);
    /// Where a Recorder with no folder of its own writes.
    [[nodiscard]] Q_INVOKABLE static QString defaultFolder();
    /// \a folder as a URL a file manager opens, the default one for empty,
    /// and a leading ~ as the home folder.
    [[nodiscard]] Q_INVOKABLE static QUrl folderUrl(const QString &folder);
    /// A folder the picker chose, as the text the setting holds.
    [[nodiscard]] Q_INVOKABLE static QString folderFromUrl(const QUrl &url);

    [[nodiscard]] int revision() const;

    void attach(core::EngineFacade *facade);
    /// The clock, for a test: milliseconds since some fixed moment.
    void setClock(std::function<qint64()> clock);

Q_SIGNALS:
    void changed();

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace rb::ui
