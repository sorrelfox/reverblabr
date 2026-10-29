// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QAbstractListModel>
#include <QDBusMessage>
#include <QString>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>

namespace rb::plasmoid
{

/// **Every saved session, as the panel widget shows them** (ADR-0024): its
/// name, whether it runs, how loud it is and whether it records, with Run
/// and Stop.
///
/// **A client of the engine's public API and nothing else of it**: the
/// sessions and their states come from `io.github.reverblabr.Engine1`, and
/// the levels from the memory `io.github.reverblabr.Telemetry` hands out,
/// which this build reads with the same code as the window of the same
/// build. No string in here is for a person; the widget's QML holds the
/// words.
///
/// **It costs nothing while nothing runs.** What the engine says is heard
/// as it is said, and nothing is asked again on a timer. The levels are
/// read only while the popup is open and a session runs.
///
/// **It does not keep the engine alive.** The engine stays while anybody
/// who has called it is still on the bus (ADR-0017), and the desktop shell
/// never leaves it. So the signals are heard on the shell's connection,
/// which never calls the engine, and every call goes through a connection
/// of the widget's own, closed again a few seconds after the last answer it
/// waits for.
class SessionList : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    /// How many sessions there are.
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    /// Whether the engine has answered with the list at least once.
    Q_PROPERTY(bool ready READ isReady NOTIFY countChanged)
    /// Whether the engine is on the bus now.
    Q_PROPERTY(bool engineRunning READ isEngineRunning NOTIFY engineRunningChanged)
    /// False once the bus has said it has no engine to start: Reverblabr's
    /// background engine is not installed.
    Q_PROPERTY(bool installed READ isInstalled NOTIFY installedChanged)
    /// How many sessions run.
    Q_PROPERTY(int runningCount READ runningCount NOTIFY runningCountChanged)
    /// Whether any session is writing a recording.
    Q_PROPERTY(bool recording READ isRecording NOTIFY recordingChanged)
    /// Whether somebody can see the list: the popup is open. **Only then
    /// are the levels read**, and opening it asks for the list again,
    /// starting the engine if it is not running.
    Q_PROPERTY(bool watching READ isWatching WRITE setWatching NOTIFY watchingChanged)
    /// Whether the levels are being read now, on a timer: only while
    /// watching and a session runs.
    Q_PROPERTY(bool readingLevels READ isReadingLevels NOTIFY readingLevelsChanged)
    /// Whether the widget's own connection is open, which makes it one of
    /// the engine's clients.
    Q_PROPERTY(bool holdingEngine READ isHoldingEngine NOTIFY holdingEngineChanged)
    /// The last session that could not be started: `{session, name, code}`,
    /// or empty.
    Q_PROPERTY(QVariantMap failure READ failure NOTIFY failureChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        /// idle, starting, running, stopping, error, or closed for a session
        /// that is only on disk.
        StateRole,
        RunningRole,
        /// The loudest the session is now, in dBFS, falling steadily after
        /// a peak so that it can be read.
        LevelRole,
        RecordingRole,
    };
    Q_ENUM(Role)

    /// Where the engine is on the session bus.
    static constexpr QLatin1StringView kServiceName{"io.github.reverblabr.Engine"};
    static constexpr QLatin1StringView kObjectPath{"/io/github/reverblabr/Engine"};
    static constexpr QLatin1StringView kInterfaceName{"io.github.reverblabr.Engine1"};
    static constexpr QLatin1StringView kTelemetryInterfaceName{"io.github.reverblabr.Telemetry"};

    /// A level that is no level: idle or silent.
    static constexpr float kSilenceDb = -120.0f;
    /// How fast a level comes down after a peak, as the window's do.
    static constexpr float kFallDbPerSecond = 24.0f;
    /// How often the levels are read while somebody looks: twenty times a
    /// second is enough for a bar a few pixels tall.
    static constexpr int kLevelTickMs = 50;
    /// How long the widget's own connection stays open after the last call
    /// that needs no further answer.
    static constexpr int kLetGoMs = 3000;
    /// How long a Run on a session that is not open waits for it to open.
    static constexpr int kOpenTimeoutMs = 10000;
    /// How long after something the list does not know about it is asked
    /// for again: several such signals in a row are one question.
    static constexpr int kRelistDelayMs = 250;

    explicit SessionList(QObject *parent = nullptr);
    ~SessionList() override;

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int count() const;
    [[nodiscard]] bool isReady() const;
    [[nodiscard]] bool isEngineRunning() const;
    [[nodiscard]] bool isInstalled() const;
    [[nodiscard]] int runningCount() const;
    [[nodiscard]] bool isRecording() const;
    [[nodiscard]] bool isWatching() const;
    void setWatching(bool watching);
    [[nodiscard]] bool isReadingLevels() const;
    [[nodiscard]] bool isHoldingEngine() const;
    [[nodiscard]] QVariantMap failure() const;

    /// The row \a sessionId is in, or -1.
    [[nodiscard]] Q_INVOKABLE int rowOf(const QString &sessionId) const;
    /// One row as QML reads it outside a list: `{id, name, state, running,
    /// level, recording}`.
    [[nodiscard]] Q_INVOKABLE QVariantMap rowAt(int row) const;

    /// Starts \a sessionId. **A session only on disk is opened first**, in
    /// the engine, and started once it has opened.
    Q_INVOKABLE void run(const QString &sessionId);
    /// Stops \a sessionId.
    Q_INVOKABLE void stop(const QString &sessionId);
    /// Asks the engine for the list, starting it if it is not running.
    Q_INVOKABLE void refresh();
    /// Forgets the last failure, once it has been read.
    Q_INVOKABLE void clearFailure();

    /// Reads the levels once, now: what the timer does while watching, for a
    /// test that cannot wait for it.
    void readLevels();

Q_SIGNALS:
    void countChanged();
    void engineRunningChanged();
    void installedChanged();
    void runningCountChanged();
    void recordingChanged();
    void watchingChanged();
    void readingLevelsChanged();
    void holdingEngineChanged();
    void failureChanged();

private Q_SLOTS:
    void heard(const QDBusMessage &message);

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace rb::plasmoid
