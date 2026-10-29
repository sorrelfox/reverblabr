// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>

#include <memory>

namespace rb::ui
{

/// **The recording being listened to**, for every part of the window
/// that shows it: the Files tab's cards and, later, the Recording Inspector.
///
/// There is one preview at a time and the engine plays it — on a player's
/// stream of its own to the default output, apart from every session. This
/// follows what the engine says about it and asks it to play, pause, seek
/// and stop.
class RecordingPreview : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// The file being previewed, playing or paused, or empty.
    Q_PROPERTY(QString path READ path NOTIFY changed)
    Q_PROPERTY(bool playing READ isPlaying NOTIFY changed)
    Q_PROPERTY(bool paused READ isPaused NOTIFY changed)
    Q_PROPERTY(double seconds READ seconds NOTIFY changed)
    Q_PROPERTY(double duration READ duration NOTIFY changed)
    /// How far through the file, from 0 to 1.
    Q_PROPERTY(double progress READ progress NOTIFY changed)

public:
    explicit RecordingPreview(QObject *parent = nullptr);
    ~RecordingPreview() override;

    [[nodiscard]] QString path() const;
    [[nodiscard]] bool isPlaying() const;
    [[nodiscard]] bool isPaused() const;
    [[nodiscard]] double seconds() const;
    [[nodiscard]] double duration() const;
    [[nodiscard]] double progress() const;

    /// Plays \a path, or pauses it if it is the one playing: what the play
    /// button and Space do.
    Q_INVOKABLE void toggle(const QString &path);
    Q_INVOKABLE void play(const QString &path);
    Q_INVOKABLE void pause();
    /// Moves to \a fraction of the way through \a path, starting it first
    /// if it is not the one being previewed: a click on a waveform.
    Q_INVOKABLE void seekTo(const QString &path, double fraction);
    Q_INVOKABLE void stop();

Q_SIGNALS:
    void changed();

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace rb::ui
