// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QList>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QVariantMap>

#include <memory>

namespace rb::ui
{

/// **One recording, in full**, for a Recording Inspector window: its
/// outline, how long it is and what it is, its name, and its tags.
///
/// The engine answers for it (GetRecording, RenameRecording,
/// SetRecordingTags), and this follows the one file: when it is renamed —
/// here or in another inspector — `path` follows it, and when its tags are
/// saved the answer is what the file holds now.
class RecordingInspector : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    /// The file. Setting it asks the engine for the rest.
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    /// Whether the engine has answered with the file's details.
    Q_PROPERTY(bool loaded READ isLoaded NOTIFY detailsChanged)
    /// The file's name without its extension, which is what can be renamed,
    /// and its extension, which cannot.
    Q_PROPERTY(QString baseName READ baseName NOTIFY pathChanged)
    Q_PROPERTY(QString extension READ extension NOTIFY pathChanged)
    Q_PROPERTY(double seconds READ seconds NOTIFY detailsChanged)
    Q_PROPERTY(int sampleRate READ sampleRate NOTIFY detailsChanged)
    Q_PROPERTY(int channels READ channels NOTIFY detailsChanged)
    Q_PROPERTY(double sizeBytes READ sizeBytes NOTIFY detailsChanged)
    Q_PROPERTY(QString format READ format NOTIFY detailsChanged)
    /// 512 amplitudes from 0 to 1, from the start of the file to its end.
    Q_PROPERTY(QList<qreal> peaks READ peaks NOTIFY detailsChanged)
    /// `{title, artist, album, genre, year, track, trackCount, disc,
    /// discCount, comment}`, as the file holds them.
    Q_PROPERTY(QVariantMap tags READ tags NOTIFY detailsChanged)
    /// Why the last rename or tag save did not happen, as a sentence for a
    /// person, or empty.
    Q_PROPERTY(QString refusal READ refusal NOTIFY refusalChanged)
    /// The file is no longer in the index: trashed, deleted, moved.
    Q_PROPERTY(bool gone READ isGone NOTIFY detailsChanged)

public:
    explicit RecordingInspector(QObject *parent = nullptr);
    ~RecordingInspector() override;

    [[nodiscard]] QString path() const;
    void setPath(const QString &path);
    [[nodiscard]] bool isLoaded() const;
    [[nodiscard]] QString baseName() const;
    [[nodiscard]] QString extension() const;
    [[nodiscard]] double seconds() const;
    [[nodiscard]] int sampleRate() const;
    [[nodiscard]] int channels() const;
    [[nodiscard]] double sizeBytes() const;
    [[nodiscard]] QString format() const;
    [[nodiscard]] QList<qreal> peaks() const;
    [[nodiscard]] QVariantMap tags() const;
    [[nodiscard]] QString refusal() const;
    [[nodiscard]] bool isGone() const;

    /// Asks the engine again.
    Q_INVOKABLE void refresh();
    /// Renames the file to \a baseName, keeping its extension.
    Q_INVOKABLE void rename(const QString &baseName);
    /// Writes \a tags into the file.
    Q_INVOKABLE void saveTags(const QVariantMap &tags);

Q_SIGNALS:
    void pathChanged();
    void detailsChanged();
    void refusalChanged();
    /// The engine said the file was renamed.
    void renamed(const QString &oldPath, const QString &newPath);
    /// The engine wrote the tags.
    void tagsSaved();

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace rb::ui
