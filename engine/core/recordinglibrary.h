// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "record/audiofile.h"
#include "record/tags.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>

#include <expected>
#include <memory>
#include <optional>

namespace rb::core
{

/// Which kind of recording \a path is, by its extension, for reading and
/// writing its tags; nothing for an extension the Recorder never writes.
[[nodiscard]] std::optional<record::FileFormat> formatOfPath(const QString &path);

/// A recording's tags as the interface XML spells them: `{title, artist,
/// album, genre, year, track, trackCount, disc, discCount, comment}`.
[[nodiscard]] QJsonObject tagsToJson(const record::Tags &tags);
[[nodiscard]] record::Tags tagsFromJson(const QJsonObject &json);

/// Where the recordings index lives: `library.db` in the application's data
/// folder (`$XDG_DATA_HOME/reverblabr`).
[[nodiscard]] QString defaultLibraryPath();

/// **The recordings index**: every file a Recorder has written, which
/// session made it, when, how long it is, what it is and the outline of its
/// sound, in SQLite at defaultLibraryPath().
///
/// **The engine keeps it, not the window.** A file is finished by the
/// engine, and later by a daemon that records with no window open (M4); a
/// list kept by the interface would miss every recording made while it was
/// closed.
///
/// **Filled from the Recorder's own events.** fileStarted() puts a row in
/// as the file is made, marked in progress; fileFinished() fills it in from
/// the file itself — read through on a worker thread, because a three-hour
/// recording takes seconds to decode — and says so with changed().
///
/// **The disk has the last word.** rescan() drops every row whose file is
/// gone — deleted in a file manager, moved, on a drive that is not there —
/// and finishes any row still marked in progress that nothing is writing,
/// which is what a crash leaves. The engine rescans when it starts and when
/// the interface asks.
class RecordingLibrary : public QObject
{
    Q_OBJECT

public:
    /// How many peaks are kept for each recording's waveform.
    static constexpr int kPeakCount = 512;
    /// How many are sent with each row of a list: the Files tab's small
    /// waveform. The Recording Inspector asks for more.
    static constexpr int kListPeakCount = 48;

    /// Opens (and if need be makes) the index at \a path.
    explicit RecordingLibrary(const QString &path, QObject *parent = nullptr);
    ~RecordingLibrary() override;

    /// Whether the index could be opened. An index that cannot is a list
    /// that stays empty, never a reason for the engine not to start.
    [[nodiscard]] bool isOpen() const;

    /// A Recorder of \a sessionId has started writing \a path.
    void fileStarted(const QString &sessionId, const QString &blockId, const QString &path);

    /// That file is closed. Its length, rate, channels and peaks are read
    /// from the file itself, off this thread; changed() says when. \a frames
    /// and \a sampleRate are what the Recorder counted, kept for a file that
    /// cannot be read back.
    void fileFinished(const QString &sessionId,
                      const QString &blockId,
                      const QString &path,
                      quint64 frames = 0,
                      quint32 sampleRate = 0);

    /// Checks every row against the disk. See the class comment.
    void rescan();

    /// Every recording \a sessionId made, newest first, as the interface XML
    /// describes a row: `{path, name, sessionId, blockId, started, seconds,
    /// sampleRate, channels, format, sizeBytes, inProgress, peaks}`.
    [[nodiscard]] QJsonArray recordings(const QString &sessionId) const;
    /// How many recordings \a sessionId has made, as recordings() would list
    /// them, **without reading any of them**: what a list of every session
    /// says of each, where a row with its peaks would be a waste.
    [[nodiscard]] int recordingCount(const QString &sessionId) const;

    /// One recording as recordings() gives a row, with \a peakCount peaks,
    /// or nothing if the index does not have it. What the Recording
    /// Inspector draws.
    [[nodiscard]] std::optional<QJsonObject> recording(const QString &path, int peakCount = kPeakCount) const;

    /// One recording in full, for the Recording Inspector: recording()
    /// with its tags read from the file now, which is where they are
    /// changed from outside (a tag editor, a music player). Nothing if the
    /// index does not have it.
    [[nodiscard]] std::optional<QJsonObject> details(const QString &path) const;

    /// **Renames a recording on the disk** to \a name — a file name
    /// without its extension, which it keeps — in the folder it is in, and
    /// the index with it. Gives back the new path, or why not:
    /// "invalidName", "exists", "notFound" or "failed". A file is never
    /// written over.
    [[nodiscard]] std::expected<QString, QString> rename(const QString &path, const QString &name);

    /// Writes \a tags into the recording (TagLib) and keeps what the
    /// file then says in the index. False if they could not be written.
    [[nodiscard]] bool saveTags(const QString &path, const QJsonObject &tags);

    /// Waits for every file still being read. For a test, and for shutting
    /// down.
    void waitForIdle();

Q_SIGNALS:
    /// What recordings(\a sessionId) gives has changed.
    void changed(const QString &sessionId);

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace rb::core
