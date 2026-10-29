// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/enginefacade.h"
#include "core/telemetry.h"

#include <QObject>
#include <QQmlEngine>
#include <QString>

#include <functional>
#include <memory>
#include <utility>

namespace rb::ui
{

/// What the window knows about the engine, and the two things it can ask of
/// it.
///
/// **One object for the whole window.** The toolbar row, the status bar and
/// the menus all say the same things about the same session, and three
/// objects working it out separately is three chances to disagree.
///
/// **It holds no session model and no meters.** What a session *is* belongs
/// to the session task; a block's levels belong to the meter hub. This is
/// the frame: which session is in front, what it is doing, and what the
/// status bar says about the node running it.
///
/// **The engine is handed to it, never made by it.** The application gives
/// it the real one and a test gives it the mock, and the QML in between
/// cannot tell which it has — which is the whole point of the facade.
///
/// **Says that where a session's levels are read has changed** (ADR-0018):
/// an engine in another process has mapped them, or is letting a mapping
/// go. Whoever keeps an address from AppShell::telemetryOf() asks again,
/// there and then; an engine in this process never says anything, because
/// its addresses never change.
class TelemetryNotice : public QObject
{
    Q_OBJECT

public:
    [[nodiscard]] static TelemetryNotice *instance();

Q_SIGNALS:
    void moved(const QString &sessionId);
};

/// Telemetry deliberately does not come through the facade ([ADR-0003]:
/// sixty signals a second to say what a pointer can say for nothing), so
/// where to read it is given separately. With no source the status bar has
/// nothing to show and says so, which is what a session that is not running
/// looks like anyway.
class AppShell : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// The session the window is showing. Empty until one is opened, which
    /// is the sessions task's business.
    Q_PROPERTY(QString sessionId READ sessionId WRITE setSessionId NOTIFY sessionChanged)
    /// One of rb::core::sessionState: idle, starting, running, stopping or
    /// error. **A code, not a word to read**; the interface holds the words.
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool running READ isRunning NOTIFY stateChanged)
    /// Starting or stopping: the moment in which Run/Stop should not be
    /// pressed again.
    Q_PROPERTY(bool busy READ isBusy NOTIFY stateChanged)
    Q_PROPERTY(bool failed READ hasFailed NOTIFY stateChanged)
    /// Whether there is a session to run at all.
    Q_PROPERTY(bool hasSession READ hasSession NOTIFY sessionChanged)

    /// How long the session has been running, in whole seconds. **Wall
    /// clock, not audio cycles**: it is a clock on a wall, and it keeps
    /// counting through an xrun.
    Q_PROPERTY(int elapsedSeconds READ elapsedSeconds NOTIFY statusChanged)
    /// The last cycle's share of its own deadline, 0…1. Below zero means
    /// there is nothing to report.
    Q_PROPERTY(qreal dspLoad READ dspLoad NOTIFY statusChanged)
    Q_PROPERTY(int xruns READ xruns NOTIFY statusChanged)
    /// The rate the graph is really running at, or 0 when nothing is.
    Q_PROPERTY(int sampleRate READ sampleRate NOTIFY statusChanged)
    /// How many frames the last cycle ran at. **PipeWire's to choose and
    /// not ours to ask for**, and it changes it while the session runs.
    Q_PROPERTY(int quantum READ quantum NOTIFY statusChanged)
    /// What that quantum is worth in milliseconds at that rate, which is
    /// the part of the delay the graph's own buffering accounts for. Zero
    /// when there is nothing to work it out from.
    Q_PROPERTY(qreal latencyMs READ latencyMs NOTIFY statusChanged)
    /// How late the session's own processing makes the sound, in
    /// milliseconds: the longest path's latency, lookaheads and the
    /// like, which the session also reports to the audio system. Zero for
    /// a session with none.
    Q_PROPERTY(qreal processingLatencyMs READ processingLatencyMs NOTIFY statusChanged)
    /// Whether the node has anything to say at all, which is what decides
    /// between a number and a dash.
    Q_PROPERTY(bool hasStatus READ hasStatus NOTIFY statusChanged)
    /// **Whether the engine is the background one** (ADR-0017), which is
    /// what Auto Run needs: false when this window runs an engine of
    /// its own, because the background engine could not be reached or
    /// `--in-process` asked for it. Writable for the QML tests.
    Q_PROPERTY(bool engineInBackground READ engineInBackground WRITE setEngineInBackgroundHere NOTIFY
                   engineInBackgroundChanged)

public:
    /// How often the status bar is worked out again. The task asks for at
    /// least once a second; four times a second is close enough to live for
    /// a load figure without being a meter.
    static constexpr int kStatusIntervalMs = 250;

    explicit AppShell(QObject *parent = nullptr);
    ~AppShell() override;

    /// Where the QML singleton gets its engine. **Set before any QML is
    /// loaded**: the application sets the real engine, a test sets the mock.
    /// \a telemetry answers where a session's levels are published, and may
    /// be left out by a caller that has none.
    static void setEngine(core::EngineFacade *facade,
                          std::function<const core::TelemetrySlot *(const QString &)> telemetry = {});

    /// **Something the window has to say before anything has gone wrong**:
    /// that the engine it runs on is not the one it would have had.
    /// The error surface shows it as the error \a code with \a argsJson, and
    /// keeps it whichever session is in front. Set before any QML is loaded,
    /// like setEngine(); an empty \a code takes it back.
    static void setStartupNotice(const QString &code, const QString &argsJson = {});
    /// What setStartupNotice() was given: the code, then its arguments.
    [[nodiscard]] static std::pair<QString, QString> startupNotice();

    /// Says whether the engine setEngine() is given runs in the background
    /// engine, before any QML is loaded. The mock counts as background, so
    /// the QML tests see what a window on the daemon shows unless they say
    /// otherwise.
    static void setEngineInBackground(bool inBackground);
    [[nodiscard]] bool engineInBackground() const;
    void setEngineInBackgroundHere(bool inBackground);

    /// The QML singleton, made from whatever setEngine() was given.
    static AppShell *create(QQmlEngine *engine, QJSEngine *scriptEngine);

    /// The engine setEngine() was given, for the other pieces of the
    /// interface that talk to it — the block catalogue and the session.
    /// **One facade for the whole window**, so that everything sees the
    /// same engine and the same session.
    [[nodiscard]] static core::EngineFacade *engine();

    /// Where a session's levels are published, or null. **Telemetry does
    /// not travel through the facade** ([ADR-0003]), so where to read it is
    /// handed over separately — a pointer today, a shared-memory mapping
    /// when the engine becomes a daemon.
    [[nodiscard]] static const core::TelemetrySlot *telemetryOf(const QString &sessionId);

    /// What telemetryOf(\a sessionId) answers has changed: TelemetryNotice
    /// says so to whoever keeps an address. For the engine in another
    /// process, whose mappings come and go (ADR-0018).
    static void telemetryMoved(const QString &sessionId);

    /// The same, for a test that wants one of its own rather than the
    /// singleton.
    void attach(core::EngineFacade *facade, std::function<const core::TelemetrySlot *(const QString &)> telemetry = {});

    [[nodiscard]] QString sessionId() const;
    void setSessionId(const QString &sessionId);
    [[nodiscard]] QString state() const;
    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] bool isBusy() const;
    [[nodiscard]] bool hasFailed() const;
    [[nodiscard]] bool hasSession() const;
    [[nodiscard]] int elapsedSeconds() const;
    [[nodiscard]] qreal dspLoad() const;
    [[nodiscard]] int xruns() const;
    [[nodiscard]] int sampleRate() const;
    [[nodiscard]] int quantum() const;
    [[nodiscard]] qreal latencyMs() const;
    [[nodiscard]] qreal processingLatencyMs() const;
    [[nodiscard]] bool hasStatus() const;

    /// Asks the engine to run the session, or to stop it. Nothing happens
    /// without a session, and nothing happens while one is starting or
    /// stopping: the answer to the last request has not arrived yet.
    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void toggleRun();

    /// Works the status out now rather than at the next tick. **For a
    /// test**, which should not have to wait a quarter of a second to find
    /// out what the engine said.
    void refresh();

    /// The clock the elapsed time is measured against, in milliseconds.
    void setClock(std::function<qint64()> clock);

Q_SIGNALS:
    void sessionChanged();
    void stateChanged();
    void statusChanged();
    void engineInBackgroundChanged();

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace rb::ui
