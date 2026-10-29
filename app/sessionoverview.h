// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/enginefacade.h"

#include <QAbstractTableModel>
#include <QQmlEngine>
#include <QStringList>
#include <QVariantMap>

#include <memory>

namespace rb::ui
{

/// Every saved session as a row of the Sessions window: what it
/// captures, how many recordings it has made, how loud it is now and since
/// when it runs, with Run and Stop for any of them.
///
/// **Drawn from the engine's list, not from the tabs.** Sessions follows the
/// sessions the window holds; this is every one on disk, open or not, and
/// `SessionsListed` says enough about each that nothing has to be
/// opened to be shown.
///
/// **The columns are the person's**: which are shown, in what order, how
/// wide and what the table is sorted by are kept in the state file
/// (WindowState::tableLayout), not in any session.
class SessionOverview : public QAbstractTableModel
{
    Q_OBJECT
    QML_ELEMENT

    /// The columns shown, by key, in the order they sit. **What the table
    /// draws**: its column n is `columns[n]`.
    Q_PROPERTY(QStringList columns READ columns NOTIFY columnsChanged)
    /// What the rows are sorted by, and which way.
    Q_PROPERTY(QString sortColumn READ sortColumn NOTIFY sortChanged)
    Q_PROPERTY(bool sortDescending READ sortDescending NOTIFY sortChanged)
    /// How many sessions there are.
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    /// Whether the engine has answered with the list at least once.
    Q_PROPERTY(bool ready READ isReady NOTIFY countChanged)
    /// Whether the levels are read. **Only while somebody can see them**: a
    /// window that is closed has no meters to move.
    Q_PROPERTY(bool watching READ isWatching WRITE setWatching NOTIFY watchingChanged)

public:
    enum Role {
        KeyRole = Qt::UserRole + 1, ///< The column's key.
        IdRole,
        NameRole,
        /// idle, starting, running, stopping, error, or closed for a session
        /// that is only on disk.
        StateRole,
        RunningRole,
        /// `[{type, name}]`, the blocks sound comes in through.
        SourcesRole,
        RecordingsRole,
        /// When it started, as a date, or an invalid one when not running.
        StartedAtRole,
        /// The loudest the session is now, in dBFS, falling steadily after
        /// a peak so that it can be read.
        LevelRole,
        /// Whether the background engine starts it at login.
        AutoRunRole,
    };
    Q_ENUM(Role)

    /// The table these columns are remembered under.
    static constexpr QLatin1StringView kTableName{"Sessions"};
    /// How fast a level comes down after a peak, as a meter's does.
    static constexpr float kFallDbPerSecond = 24.0f;
    /// How often the levels are read while watching.
    static constexpr int kLevelTickMs = 50;
    /// How long after an edit that may change a session's sources the list
    /// is asked for again: a drag of blocks is many edits and one question.
    static constexpr int kRelistDelayMs = 250;

    explicit SessionOverview(QObject *parent = nullptr);
    ~SessionOverview() override;

    /// Every column there is, by key, in the order a table starts with:
    /// name, sources, recordings, level, status, run. **Timers and Auto Run
    /// are not among them yet**: there is nothing behind them until the
    /// scheduler and Auto Run exist.
    [[nodiscard]] Q_INVOKABLE static QStringList allColumns();

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant
    headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] QStringList columns() const;
    [[nodiscard]] QString sortColumn() const;
    [[nodiscard]] bool sortDescending() const;
    [[nodiscard]] int count() const;
    [[nodiscard]] bool isReady() const;
    [[nodiscard]] bool isWatching() const;
    void setWatching(bool watching);

    /// Shows or hides a column. **The name always stays**: a table of
    /// sessions nobody can tell apart is no table at all.
    Q_INVOKABLE void setColumnShown(const QString &key, bool shown);
    [[nodiscard]] Q_INVOKABLE bool isColumnShown(const QString &key) const;
    /// Moves the shown column at \a from to \a to, which is what dragging a
    /// header does.
    Q_INVOKABLE void moveColumn(int from, int to);
    /// How wide a column is, or nought for "as the table sees fit".
    [[nodiscard]] Q_INVOKABLE int columnWidth(const QString &key) const;
    Q_INVOKABLE void setColumnWidth(const QString &key, int width);
    /// Sorts by \a key; asked for the column it is already sorted by, the
    /// other way round, as clicking a header twice does.
    Q_INVOKABLE void sortBy(const QString &key);

    /// The session in row \a row, or empty.
    [[nodiscard]] Q_INVOKABLE QString idAt(int row) const;
    /// The row \a sessionId is in, or -1.
    [[nodiscard]] Q_INVOKABLE int rowOf(const QString &sessionId) const;
    /// One row as QML reads it outside a table: `{id, name, state, running,
    /// sources, recordings, startedAt, level}`.
    [[nodiscard]] Q_INVOKABLE QVariantMap rowAt(int row) const;

    /// Starts every one of \a sessionIds. **A session only on disk is opened
    /// first**, which puts it in a tab of the main window where it can be
    /// seen running, and started once it has opened.
    Q_INVOKABLE void run(const QStringList &sessionIds);
    /// Stops every one of \a sessionIds that is running.
    Q_INVOKABLE void stop(const QStringList &sessionIds);
    /// Sets Auto Run on or off for each of \a sessionIds: an edit of
    /// each session, one step to undo in it. **One only on disk is opened
    /// first**, into a tab, as Run opens it, and set once it has opened.
    Q_INVOKABLE void setAutoRun(const QStringList &sessionIds, bool on);
    /// Asks the engine for the list again.
    Q_INVOKABLE void refresh();

    /// The facade to talk to. A test gives its own; QML's takes AppShell's.
    void attach(core::EngineFacade *facade);

    /// Reads the levels once, now: what the timer does while watching, for a
    /// test that cannot wait for it.
    void readLevels();

Q_SIGNALS:
    void columnsChanged();
    void sortChanged();
    void countChanged();
    void watchingChanged();

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace rb::ui
