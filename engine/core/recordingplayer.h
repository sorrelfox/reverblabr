// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>

#include <memory>

namespace rb::pw
{
class Connection;
}

namespace rb::core
{

/// Where a preview stands, as `PreviewChanged` says it.
namespace previewState
{
inline constexpr QLatin1StringView playing{"playing"};
inline constexpr QLatin1StringView paused{"paused"};
inline constexpr QLatin1StringView stopped{"stopped"};
} // namespace previewState

/// **Listening to a recording**: one file at a time, from the Files
/// tab and later the Recording Inspector.
///
/// A thread of its own reads the file (record::AudioFileReader) into a
/// ring a few seconds deep, and a pw::PreviewStream of its own plays the
/// ring to the default output. **Nothing of a session's is touched**: the
/// stream is an ordinary player's, linked by the session manager, and a
/// session running beside it runs exactly as it did.
///
/// A seek starts the stream again from the new place, which is simpler
/// than moving a read head both threads share and takes as long as a
/// stream takes to start, a few tens of milliseconds.
class RecordingPlayer : public QObject
{
    Q_OBJECT

public:
    /// How often the position is said while a file plays.
    static constexpr int kTickMs = 100;

    explicit RecordingPlayer(pw::Connection &connection, QObject *parent = nullptr);
    ~RecordingPlayer() override;

    /// Plays \a path from its start, or from where it was paused if it is
    /// the file already being previewed. Another file stops the first.
    void play(const QString &path);
    void pause();
    /// Moves to \a seconds into the file, playing or paused as it was.
    void seek(double seconds);
    void stop();

    /// `{path, state, seconds, duration}`, as `PreviewChanged` carries it.
    [[nodiscard]] QJsonObject status() const;

    /// The stream's node id while one plays, for a test; 0 otherwise.
    [[nodiscard]] std::uint32_t streamNodeId() const;

Q_SIGNALS:
    /// Where the preview stands: on every change, and every kTickMs while
    /// it plays.
    void changed(const QJsonObject &status);
    /// \a path could not be previewed, and why: "unreadable",
    /// "noAudioSystem", or what the server said.
    void failed(const QString &path, const QString &reason);

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace rb::core
