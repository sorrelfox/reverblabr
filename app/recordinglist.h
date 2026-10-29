// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QQmlEngine>
#include <QString>
#include <QVariantList>

#include <memory>

namespace rb::ui
{

/// **One session's recordings**, as the Files tab lists them.
///
/// The list is the engine's recordings index, asked for when the session is
/// set and told again whenever it changes: a Recorder starting or finishing
/// a file, a rescan that found a file gone. Newest first, as the engine
/// sends them.
///
/// **The words are the interface's.** The engine sends a length in seconds
/// and a size in bytes; this writes them the way a person reads them, in
/// their locale.
class RecordingList : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    /// Whose recordings. Empty lists nothing.
    Q_PROPERTY(QString sessionId READ sessionId WRITE setSessionId NOTIFY sessionIdChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        PathRole = Qt::UserRole + 1,
        NameRole,
        StartedRole,
        SecondsRole,
        FormatRole,
        SizeBytesRole,
        InProgressRole,
        PeaksRole,
        ChannelsRole,
        SampleRateRole,
        TagsRole,
    };
    Q_ENUM(Role)

    explicit RecordingList(QObject *parent = nullptr);
    ~RecordingList() override;

    [[nodiscard]] QString sessionId() const;
    void setSessionId(const QString &sessionId);
    [[nodiscard]] int count() const;

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /// One row as a map with the role names as keys, or an empty map.
    [[nodiscard]] Q_INVOKABLE QVariantMap get(int row) const;
    /// The row of \a path, or -1. **What a selection is kept by**: a row
    /// number stops meaning the same recording the moment one arrives.
    [[nodiscard]] Q_INVOKABLE int rowOf(const QString &path) const;

    /// Asks the engine to check its index against the disk, which is how a
    /// file deleted outside the application leaves the list.
    Q_INVOKABLE void rescan();

    /// A length as the list writes it: "0:42", "58:12", "1:30:41".
    [[nodiscard]] Q_INVOKABLE static QString formatDuration(double seconds);
    /// A size as the list writes it, in the reader's locale: "111.6 MB".
    [[nodiscard]] Q_INVOKABLE static QString formatSize(double bytes);
    /// A format as the list writes it: the extension, in capitals.
    [[nodiscard]] Q_INVOKABLE static QString formatName(const QString &format);

Q_SIGNALS:
    void sessionIdChanged();
    void countChanged();

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace rb::ui
