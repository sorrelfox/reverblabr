// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "appshell.h"

#include <QElapsedTimer>
#include <QPointer>
#include <QTimer>

#include <utility>

namespace rb::ui
{

namespace
{

/// What setEngine() left for the next singleton to be made from. **Not the
/// shell itself**: the QML engine decides when the singleton exists, and it
/// may do so more than once in a process that runs several QML engines, as
/// the QML tests do.
core::EngineFacade *g_facade = nullptr;
std::function<const core::TelemetrySlot *(const QString &)> g_telemetry;
std::pair<QString, QString> g_startupNotice;
bool g_engineInBackground = true;

[[nodiscard]] qint64 steadyMilliseconds()
{
    static QElapsedTimer since = [] {
        QElapsedTimer timer;
        timer.start();
        return timer;
    }();
    return since.elapsed();
}

} // namespace

class AppShell::Private
{
public:
    QPointer<core::EngineFacade> facade;
    std::function<const core::TelemetrySlot *(const QString &)> telemetry;
    std::function<qint64()> clock = &steadyMilliseconds;
    QTimer ticker;

    QString sessionId;
    QString state = core::sessionState::idle;

    /// When the session started running, on the clock. Negative when it is
    /// not running.
    qint64 startedAt = -1;
    int elapsed = 0;

    bool hasStatus = false;
    qreal dspLoad = 0.0;
    int xruns = 0;
    int sampleRate = 0;
    int quantum = 0;
    /// The session's processing latency, in samples at that rate.
    int processLatency = 0;
};

AppShell::AppShell(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->ticker.setInterval(kStatusIntervalMs);
    connect(&d->ticker, &QTimer::timeout, this, &AppShell::refresh);
    // **Attached here, not only in create().** Whether QML reaches a
    // singleton through its create function or default-constructs it is
    // QML's business — and here it default-constructs, so this object had no
    // engine at all in the running application: Run was offered, and did
    // nothing. Every other singleton in this window already attached in its
    // constructor for exactly this reason. Nothing noticed until the window
    // had a session, because until then start() returned early on an
    // empty session id anyway.
    attach(g_facade, g_telemetry);
}

AppShell::~AppShell() = default;

void AppShell::setEngine(core::EngineFacade *facade,
                         std::function<const core::TelemetrySlot *(const QString &)> telemetry)
{
    g_facade = facade;
    g_telemetry = std::move(telemetry);
}

void AppShell::setStartupNotice(const QString &code, const QString &argsJson)
{
    g_startupNotice = code.isEmpty() ? std::pair<QString, QString>{} : std::pair{code, argsJson};
}

std::pair<QString, QString> AppShell::startupNotice()
{
    return g_startupNotice;
}

void AppShell::setEngineInBackground(bool inBackground)
{
    g_engineInBackground = inBackground;
}

bool AppShell::engineInBackground() const
{
    return g_engineInBackground;
}

void AppShell::setEngineInBackgroundHere(bool inBackground)
{
    if (g_engineInBackground == inBackground) {
        return;
    }
    g_engineInBackground = inBackground;
    Q_EMIT engineInBackgroundChanged();
}

core::EngineFacade *AppShell::engine()
{
    return g_facade;
}

const core::TelemetrySlot *AppShell::telemetryOf(const QString &sessionId)
{
    return g_telemetry && !sessionId.isEmpty() ? g_telemetry(sessionId) : nullptr;
}

TelemetryNotice *TelemetryNotice::instance()
{
    static TelemetryNotice notice;
    return &notice;
}

void AppShell::telemetryMoved(const QString &sessionId)
{
    Q_EMIT TelemetryNotice::instance()->moved(sessionId);
}

AppShell *AppShell::create(QQmlEngine *, QJSEngine *)
{
    // The constructor has already attached to the window's engine; this is
    // here because a QML singleton is entitled to a create function.
    return new AppShell;
}

void AppShell::attach(core::EngineFacade *facade, std::function<const core::TelemetrySlot *(const QString &)> telemetry)
{
    if (d->facade) {
        d->facade->disconnect(this);
    }
    d->facade = facade;
    d->telemetry = std::move(telemetry);
    if (facade != nullptr) {
        // **Only what this window is showing.** A state change about another
        // session is another window's business, or nobody's.
        connect(facade, &core::EngineFacade::stateChanged, this, [this](const QString &id, const QString &state) {
            if (id != d->sessionId) {
                return;
            }
            const bool wasRunning = isRunning();
            d->state = state;
            if (isRunning() && !wasRunning) {
                d->startedAt = d->clock();
            } else if (!isRunning()) {
                d->startedAt = -1;
            }
            refresh();
            Q_EMIT stateChanged();
        });
    }
    refresh();
}

QString AppShell::sessionId() const
{
    return d->sessionId;
}

void AppShell::setSessionId(const QString &sessionId)
{
    if (d->sessionId == sessionId) {
        return;
    }
    d->sessionId = sessionId;
    d->state = core::sessionState::idle;
    d->startedAt = -1;
    if (d->facade && !sessionId.isEmpty()) {
        // What it is doing now, rather than what it was doing when we last
        // looked: a session may already be running when a window opens it.
        d->facade->getSessionState(sessionId);
    }
    refresh();
    Q_EMIT sessionChanged();
    Q_EMIT stateChanged();
}

QString AppShell::state() const
{
    return d->state;
}

bool AppShell::isRunning() const
{
    return d->state == core::sessionState::running;
}

bool AppShell::isBusy() const
{
    return d->state == core::sessionState::starting || d->state == core::sessionState::stopping;
}

bool AppShell::hasFailed() const
{
    return d->state == core::sessionState::error;
}

bool AppShell::hasSession() const
{
    return !d->sessionId.isEmpty();
}

int AppShell::elapsedSeconds() const
{
    return d->elapsed;
}

qreal AppShell::dspLoad() const
{
    return d->dspLoad;
}

int AppShell::xruns() const
{
    return d->xruns;
}

int AppShell::sampleRate() const
{
    return d->sampleRate;
}

int AppShell::quantum() const
{
    return d->quantum;
}

qreal AppShell::processingLatencyMs() const
{
    if (d->processLatency <= 0 || d->sampleRate <= 0) {
        return 0.0;
    }
    return 1000.0 * d->processLatency / d->sampleRate; // NOLINT(*-magic-numbers)
}

qreal AppShell::latencyMs() const
{
    if (d->quantum <= 0 || d->sampleRate <= 0) {
        return 0.0;
    }
    return 1000.0 * d->quantum / d->sampleRate; // NOLINT(*-magic-numbers)
}

bool AppShell::hasStatus() const
{
    return d->hasStatus;
}

void AppShell::start()
{
    if (!d->facade || d->sessionId.isEmpty() || isBusy() || isRunning()) {
        return;
    }
    d->facade->start(d->sessionId);
}

void AppShell::stop()
{
    if (!d->facade || d->sessionId.isEmpty() || isBusy() || !isRunning()) {
        return;
    }
    d->facade->stop(d->sessionId);
}

void AppShell::toggleRun()
{
    if (isRunning()) {
        stop();
    } else {
        start();
    }
}

void AppShell::refresh()
{
    const bool wanted = isRunning() || isBusy();
    if (wanted != d->ticker.isActive()) {
        // **Nothing to count while nothing is running.** A timer that fires
        // four times a second all day to say "idle" is a battery bill.
        if (wanted) {
            d->ticker.start();
        } else {
            d->ticker.stop();
        }
    }

    const int elapsed =
        d->startedAt < 0 ? 0 : static_cast<int>((d->clock() - d->startedAt) / 1000); // NOLINT(*-magic-numbers)

    bool hasStatus = false;
    qreal dspLoad = 0.0;
    int xruns = 0;
    int sampleRate = 0;
    int quantum = 0;
    int processLatency = 0;
    if (d->telemetry && !d->sessionId.isEmpty()) {
        if (const core::TelemetrySlot *slot = d->telemetry(d->sessionId); slot != nullptr) {
            const core::TelemetrySnapshot snapshot = slot->load();
            // **What the node saw, not what we asked for.** A session that
            // has been told to run but whose node is not being scheduled has
            // nothing to report, and a dash is the honest answer.
            hasStatus = snapshot.running && snapshot.sampleRate > 0;
            dspLoad = snapshot.peakDspLoad;
            xruns = static_cast<int>(snapshot.xruns);
            sampleRate = static_cast<int>(snapshot.sampleRate);
            quantum = static_cast<int>(snapshot.frames);
            processLatency = static_cast<int>(snapshot.processLatency);
        }
    }

    if (elapsed == d->elapsed && hasStatus == d->hasStatus && qFuzzyCompare(1.0 + dspLoad, 1.0 + d->dspLoad)
        && xruns == d->xruns && sampleRate == d->sampleRate && quantum == d->quantum
        && processLatency == d->processLatency) {
        return;
    }
    d->elapsed = elapsed;
    d->hasStatus = hasStatus;
    d->dspLoad = dspLoad;
    d->xruns = xruns;
    d->sampleRate = sampleRate;
    d->quantum = quantum;
    d->processLatency = processLatency;
    Q_EMIT statusChanged();
}

void AppShell::setClock(std::function<qint64()> clock)
{
    d->clock = clock ? std::move(clock) : std::function<qint64()>(&steadyMilliseconds);
}

} // namespace rb::ui
