// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "sessionlist.h"

#include "core/sharedtelemetry.h"

#include <QCollator>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDBusUnixFileDescriptor>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSet>
#include <QTimer>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <utility>

Q_LOGGING_CATEGORY(lcSessionList, "io.github.reverblabr.plasmoid", QtWarningMsg)

using namespace Qt::StringLiterals;

namespace rb::plasmoid
{

namespace
{

constexpr QLatin1StringView kRunning{"running"};
constexpr QLatin1StringView kStarting{"starting"};
constexpr QLatin1StringView kClosed{"closed"};

/// The signals the list is drawn from. **Only these**: a signal nobody here
/// listens for is never routed to the shell at all, and the window's
/// busier ones (a session's patches, a preview's position) would otherwise
/// wake it for nothing.
constexpr QLatin1StringView kHeardSignals[] = {
    QLatin1StringView("SessionsListed"),
    QLatin1StringView("StateChanged"),
    QLatin1StringView("SessionOpened"),
    QLatin1StringView("SessionClosed"),
    QLatin1StringView("RecordingFileStarted"),
    QLatin1StringView("RecordingFileFinished"),
    QLatin1StringView("Error"),
};

[[nodiscard]] float gainToDb(float gain)
{
    return gain > 0.0f ? std::max(SessionList::kSilenceDb, 20.0f * std::log10(gain)) : SessionList::kSilenceDb;
}

} // namespace

class SessionList::Private
{
public:
    explicit Private(SessionList *owner)
        : q(owner)
        , callConnectionName(u"io.github.reverblabr.plasmoid-%1"_s.arg(reinterpret_cast<quintptr>(owner), 0, 16))
    {
    }

    struct Row {
        QString id;
        QString name;
        QString state;
        float levelDb = kSilenceDb;
    };

    /// One session's levels: the memory the engine publishes them in,
    /// mapped read-only, or the question for it that is out.
    struct Telemetry {
        std::optional<core::SharedTelemetry> mapping;
        bool asking = false;
        /// The engine said no, or the memory is not of this build: asked
        /// again only once the session opens afresh.
        bool refused = false;
        /// Which question an answer is for: one sent before the memory was
        /// let go answers nothing.
        quint64 asked = 0;
    };

    SessionList *q;
    /// The shell's own connection, only listened on: it never calls the
    /// engine, so it never counts as the engine's client.
    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusServiceWatcher watcher{QString(kServiceName), bus, QDBusServiceWatcher::WatchForOwnerChange};
    /// The connection calls go through, by name, while it is open.
    QString callConnectionName;
    bool holding = false;
    QTimer letGo;

    QList<Row> rows;
    bool ready = false;
    bool engineRunning = false;
    bool installed = true;
    bool watching = false;
    QVariantMap failure;
    /// Sessions a Run is waiting to see open.
    QSet<QString> openingToRun;
    /// The files being recorded, and the session each is of.
    QHash<QString, QString> recordingFiles;

    std::map<QString, Telemetry> telemetry;
    quint64 shares = 0;
    QTimer ticker;
    QElapsedTimer sinceTick;
    QTimer relistSoon;

    [[nodiscard]] qsizetype indexOf(const QString &sessionId) const
    {
        for (qsizetype index = 0; index < rows.size(); ++index) {
            if (rows[index].id == sessionId) {
                return index;
            }
        }
        return -1;
    }

    [[nodiscard]] int countRunning() const
    {
        return static_cast<int>(std::ranges::count_if(rows, [](const Row &row) { return row.state == kRunning; }));
    }

    [[nodiscard]] bool isRecording(const QString &sessionId) const
    {
        return std::ranges::any_of(recordingFiles, [&sessionId](const QString &of) { return of == sessionId; });
    }

    /// Calls \a method on the engine through the widget's own connection,
    /// opening it if it is closed. \a activate lets the bus start an engine
    /// that is not running.
    QDBusPendingCall
    call(QLatin1StringView interface, const QString &method, const QVariantList &arguments, bool activate)
    {
        QDBusConnection connection = QDBusConnection::connectToBus(QDBusConnection::SessionBus, callConnectionName);
        setHolding(connection.isConnected());
        QDBusMessage message = QDBusMessage::createMethodCall(kServiceName, kObjectPath, interface, method);
        message.setArguments(arguments);
        message.setAutoStartService(activate);
        letGo.start();
        return connection.asyncCall(message);
    }

    /// A call whose only answer is a signal: what can go wrong with it is
    /// only that there is no engine.
    void tell(const QString &method, const QVariantList &arguments, bool activate)
    {
        auto *watch = new QDBusPendingCallWatcher(call(kInterfaceName, method, arguments, activate), q);
        QObject::connect(watch, &QDBusPendingCallWatcher::finished, q, [this, method](QDBusPendingCallWatcher *done) {
            done->deleteLater();
            const QDBusPendingReply<> reply = *done;
            if (!reply.isError()) {
                return;
            }
            qCDebug(lcSessionList) << method << "was not answered:" << reply.error().name();
            if (reply.error().type() == QDBusError::ServiceUnknown) {
                setInstalled(false);
                openingToRun.clear();
            }
        });
    }

    /// **Lets the engine go** once nothing is waited for: the connection
    /// closes, and with it the widget's place among the engine's clients.
    void release()
    {
        const bool waiting = !openingToRun.isEmpty()
            || std::ranges::any_of(telemetry, [](const auto &entry) { return entry.second.asking; });
        if (waiting) {
            letGo.start();
            return;
        }
        QDBusConnection::disconnectFromBus(callConnectionName);
        setHolding(false);
    }

    void setHolding(bool now)
    {
        if (holding != now) {
            holding = now;
            Q_EMIT q->holdingEngineChanged();
        }
    }

    void setInstalled(bool now)
    {
        if (installed != now) {
            installed = now;
            Q_EMIT q->installedChanged();
        }
    }

    void setEngineRunning(bool now)
    {
        if (engineRunning == now) {
            return;
        }
        engineRunning = now;
        Q_EMIT q->engineRunningChanged();
        if (now) {
            setInstalled(true);
        }
    }

    /// The engine went, or another took its name: nothing it said holds
    /// any more. **The sessions stay listed**, as they are on disk, and
    /// none of them runs.
    void forgetEngine()
    {
        dropAllTelemetry();
        openingToRun.clear();
        const int runningBefore = countRunning();
        const bool recordingBefore = !recordingFiles.isEmpty();
        recordingFiles.clear();
        for (Row &row : rows) {
            row.state = QString(kClosed);
            row.levelDb = kSilenceDb;
        }
        if (!rows.isEmpty()) {
            Q_EMIT q->dataChanged(q->index(0), q->index(static_cast<int>(rows.size()) - 1));
        }
        if (runningBefore != 0) {
            Q_EMIT q->runningCountChanged();
        }
        if (recordingBefore) {
            Q_EMIT q->recordingChanged();
        }
        updateTicker();
    }

    void listed(const QString &json)
    {
        const QJsonArray sessions = QJsonDocument::fromJson(json.toUtf8()).array();
        QList<Row> next;
        next.reserve(sessions.size());
        for (const QJsonValue &value : sessions) {
            const QJsonObject session = value.toObject();
            Row row;
            row.id = session.value("id"_L1).toString();
            if (row.id.isEmpty()) {
                continue;
            }
            row.name = session.value("name"_L1).toString();
            row.state = session.value("state"_L1).toString(QString(kClosed));
            if (const qsizetype old = indexOf(row.id); old >= 0 && row.state == kRunning) {
                row.levelDb = rows[old].levelDb;
            }
            next.append(row);
        }
        // **By name, as a person looks for one**, whatever order the engine
        // keeps them in.
        QCollator collator;
        collator.setNumericMode(true);
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        std::ranges::stable_sort(
            next, [&collator](const Row &a, const Row &b) { return collator.compare(a.name, b.name) < 0; });

        const int runningBefore = countRunning();
        const qsizetype countBefore = rows.size();
        const bool wasReady = ready;
        const bool sameRows = std::ranges::equal(rows, next, {}, &Row::id, &Row::id);
        if (sameRows) {
            // **The same sessions in the same order are changed in place**,
            // as opening the popup finds them nearly every time: the rows
            // stay, and nothing under the pointer moves.
            for (qsizetype index = 0; index < rows.size(); ++index) {
                Row &row = rows[index];
                if (row.name != next[index].name || row.state != next[index].state) {
                    row.name = next[index].name;
                    row.state = next[index].state;
                    row.levelDb = next[index].levelDb;
                    Q_EMIT q->dataChanged(q->index(static_cast<int>(index)), q->index(static_cast<int>(index)));
                }
            }
        } else {
            q->beginResetModel();
            rows = std::move(next);
            q->endResetModel();
        }
        ready = true;
        // A session the list says is not running records nothing.
        for (const Row &row : std::as_const(rows)) {
            if (row.state != kRunning) {
                forgetRecordings(row.id);
            }
        }
        for (auto entry = telemetry.begin(); entry != telemetry.end();) {
            entry = indexOf(entry->first) < 0 ? telemetry.erase(entry) : std::next(entry);
        }
        if (rows.size() != countBefore || !wasReady) {
            Q_EMIT q->countChanged();
        }
        if (countRunning() != runningBefore) {
            Q_EMIT q->runningCountChanged();
        }
        askForLevels();
        updateTicker();
    }

    void stateChanged(const QString &sessionId, const QString &state)
    {
        const qsizetype index = indexOf(sessionId);
        if (index < 0) {
            relistSoon.start();
            return;
        }
        Row &row = rows[index];
        if (row.state == state) {
            return;
        }
        const bool wasRunning = row.state == kRunning;
        row.state = state;
        if (state != kRunning) {
            row.levelDb = kSilenceDb;
            forgetRecordings(sessionId);
        }
        Q_EMIT q->dataChanged(q->index(static_cast<int>(index)), q->index(static_cast<int>(index)));
        if (wasRunning != (state == kRunning)) {
            Q_EMIT q->runningCountChanged();
        }
        askForLevels();
        updateTicker();
    }

    void opened(const QString &sessionId)
    {
        // **A session opened afresh publishes into fresh memory.**
        telemetry.erase(sessionId);
        if (indexOf(sessionId) < 0) {
            relistSoon.start();
        } else {
            stateChanged(sessionId, u"idle"_s);
        }
        if (openingToRun.remove(sessionId)) {
            tell(u"Start"_s, {sessionId}, false);
        }
    }

    void closed(const QString &sessionId)
    {
        telemetry.erase(sessionId);
        stateChanged(sessionId, QString(kClosed));
    }

    void forgetRecordings(const QString &sessionId)
    {
        const bool before = !recordingFiles.isEmpty();
        recordingFiles.removeIf([&sessionId](const auto &file) { return file.value() == sessionId; });
        if (before != !recordingFiles.isEmpty()) {
            Q_EMIT q->recordingChanged();
        }
    }

    void recordingFile(const QString &sessionId, const QString &path, bool started)
    {
        const bool before = !recordingFiles.isEmpty();
        const bool sessionBefore = isRecording(sessionId);
        if (started) {
            recordingFiles.insert(path, sessionId);
        } else {
            recordingFiles.remove(path);
        }
        if (const qsizetype index = indexOf(sessionId); index >= 0 && sessionBefore != isRecording(sessionId)) {
            Q_EMIT q->dataChanged(
                q->index(static_cast<int>(index)), q->index(static_cast<int>(index)), {RecordingRole});
        }
        if (before != !recordingFiles.isEmpty()) {
            Q_EMIT q->recordingChanged();
        }
    }

    void error(const QString &code, const QString &argsJson)
    {
        const QJsonObject args = QJsonDocument::fromJson(argsJson.toUtf8()).object();
        const QString sessionId = args.value("session"_L1).toString();
        if (sessionId.isEmpty()) {
            return;
        }
        const bool waitedFor = openingToRun.remove(sessionId);
        // **Only what keeps a session from running** is the widget's to
        // show; the window says the rest, as it always has.
        if (code != "session.cannotRun"_L1 && !waitedFor) {
            return;
        }
        const qsizetype index = indexOf(sessionId);
        failure = {
            {u"session"_s, sessionId},
            {u"name"_s, index >= 0 ? rows[index].name : QString()},
            {u"code"_s, code},
        };
        Q_EMIT q->failureChanged();
    }

    /// Asks for the memory of every running session that has none, while
    /// somebody looks.
    void askForLevels()
    {
        if (!watching) {
            return;
        }
        for (const Row &row : std::as_const(rows)) {
            if (row.state != kRunning) {
                continue;
            }
            Telemetry &entry = telemetry[row.id];
            if (entry.mapping || entry.asking || entry.refused) {
                continue;
            }
            share(row.id, entry);
        }
    }

    void share(const QString &sessionId, Telemetry &entry)
    {
        entry.asking = true;
        entry.asked = ++shares;
        // **Never a reason to start an engine**: a new one would not have
        // the session open.
        auto *watch = new QDBusPendingCallWatcher(call(kTelemetryInterfaceName, u"Share"_s, {sessionId}, false), q);
        QObject::connect(watch,
                         &QDBusPendingCallWatcher::finished,
                         q,
                         [this, sessionId, asked = entry.asked](QDBusPendingCallWatcher *done) {
                             done->deleteLater();
                             const auto still = telemetry.find(sessionId);
                             if (still == telemetry.end() || still->second.asked != asked) {
                                 return;
                             }
                             still->second.asking = false;
                             const QDBusPendingReply<QDBusUnixFileDescriptor> reply = *done;
                             if (reply.isError()) {
                                 qCDebug(lcSessionList) << "no levels for" << sessionId << reply.error().name();
                                 still->second.refused = true;
                                 return;
                             }
                             // The reply keeps its own descriptor; the
                             // mapping takes this one and closes it.
                             auto mapped = core::SharedTelemetry::map(::dup(reply.value().fileDescriptor()));
                             if (!mapped) {
                                 qCWarning(lcSessionList)
                                     << "the levels of" << sessionId << "are not in memory this build can read";
                                 still->second.refused = true;
                                 return;
                             }
                             still->second.mapping.emplace(std::move(*mapped));
                         });
    }

    void dropAllTelemetry()
    {
        telemetry.clear();
    }

    /// **The timer runs only while it has something to show**: somebody
    /// looks and a session runs. Otherwise the widget does nothing at all.
    void updateTicker()
    {
        const bool should = watching && countRunning() > 0;
        if (should == ticker.isActive()) {
            return;
        }
        if (should) {
            sinceTick.invalidate();
            ticker.start();
        } else {
            ticker.stop();
        }
        Q_EMIT q->readingLevelsChanged();
    }
};

SessionList::SessionList(QObject *parent)
    : QAbstractListModel(parent)
    , d(std::make_unique<Private>(this))
{
    d->letGo.setSingleShot(true);
    d->letGo.setInterval(kLetGoMs);
    connect(&d->letGo, &QTimer::timeout, this, [this] { d->release(); });

    d->ticker.setInterval(kLevelTickMs);
    connect(&d->ticker, &QTimer::timeout, this, &SessionList::readLevels);

    d->relistSoon.setSingleShot(true);
    d->relistSoon.setInterval(kRelistDelayMs);
    connect(&d->relistSoon, &QTimer::timeout, this, [this] {
        if (d->engineRunning) {
            d->tell(u"ListSessions"_s, {}, false);
        }
    });

    for (const QLatin1StringView signal : kHeardSignals) {
        d->bus.connect(QString(kServiceName),
                       QString(kObjectPath),
                       QString(kInterfaceName),
                       QString(signal),
                       this,
                       SLOT(heard(QDBusMessage)));
    }

    connect(&d->watcher,
            &QDBusServiceWatcher::serviceOwnerChanged,
            this,
            [this](const QString &, const QString &oldOwner, const QString &newOwner) {
                if (!oldOwner.isEmpty()) {
                    d->forgetEngine();
                }
                d->setEngineRunning(!newOwner.isEmpty());
                // **A new engine has opened what it remembers, stopped**:
                // the list it gives is the one that holds now.
                if (!newOwner.isEmpty()) {
                    d->tell(u"ListSessions"_s, {}, false);
                }
            });

    // **Whether there is an engine, from the bus itself**: asking the
    // engine would make the widget its client, and start one that is not
    // running at every login.
    if (d->bus.isConnected()) {
        auto *watch =
            new QDBusPendingCallWatcher(d->bus.interface()->asyncCall(u"NameHasOwner"_s, QString(kServiceName)), this);
        connect(watch, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *done) {
            done->deleteLater();
            const QDBusPendingReply<bool> reply = *done;
            if (reply.isError() || !reply.value() || d->engineRunning) {
                return;
            }
            d->setEngineRunning(true);
            d->tell(u"ListSessions"_s, {}, false);
        });
    }
}

SessionList::~SessionList()
{
    QDBusConnection::disconnectFromBus(d->callConnectionName);
}

int SessionList::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(d->rows.size());
}

QVariant SessionList::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
        return {};
    }
    const Private::Row &row = d->rows[index.row()];
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return row.name;
    case IdRole:
        return row.id;
    case StateRole:
        return row.state;
    case RunningRole:
        return row.state == kRunning;
    case LevelRole:
        return row.levelDb;
    case RecordingRole:
        return d->isRecording(row.id);
    default:
        return {};
    }
}

QHash<int, QByteArray> SessionList::roleNames() const
{
    return {
        {IdRole, "sessionId"},
        {NameRole, "name"},
        {StateRole, "sessionState"},
        {RunningRole, "running"},
        {LevelRole, "levelDb"},
        {RecordingRole, "recording"},
    };
}

int SessionList::count() const
{
    return static_cast<int>(d->rows.size());
}

bool SessionList::isReady() const
{
    return d->ready;
}

bool SessionList::isEngineRunning() const
{
    return d->engineRunning;
}

bool SessionList::isInstalled() const
{
    return d->installed;
}

int SessionList::runningCount() const
{
    return d->countRunning();
}

bool SessionList::isRecording() const
{
    return !d->recordingFiles.isEmpty();
}

bool SessionList::isWatching() const
{
    return d->watching;
}

void SessionList::setWatching(bool watching)
{
    if (d->watching == watching) {
        return;
    }
    d->watching = watching;
    Q_EMIT watchingChanged();
    if (watching) {
        refresh();
        d->askForLevels();
    } else {
        // **Nobody looks, so nothing is held**: the next look maps afresh,
        // and a failure nobody saw has nobody to tell.
        d->dropAllTelemetry();
        clearFailure();
        for (qsizetype index = 0; index < d->rows.size(); ++index) {
            if (d->rows[index].levelDb != kSilenceDb) {
                d->rows[index].levelDb = kSilenceDb;
                Q_EMIT dataChanged(
                    this->index(static_cast<int>(index)), this->index(static_cast<int>(index)), {LevelRole});
            }
        }
    }
    d->updateTicker();
}

bool SessionList::isReadingLevels() const
{
    return d->ticker.isActive();
}

bool SessionList::isHoldingEngine() const
{
    return d->holding;
}

QVariantMap SessionList::failure() const
{
    return d->failure;
}

int SessionList::rowOf(const QString &sessionId) const
{
    return static_cast<int>(d->indexOf(sessionId));
}

QVariantMap SessionList::rowAt(int row) const
{
    if (row < 0 || row >= d->rows.size()) {
        return {};
    }
    const Private::Row &entry = d->rows[row];
    return {
        {u"id"_s, entry.id},
        {u"name"_s, entry.name},
        {u"state"_s, entry.state},
        {u"running"_s, entry.state == kRunning},
        {u"level"_s, entry.levelDb},
        {u"recording"_s, d->isRecording(entry.id)},
    };
}

void SessionList::run(const QString &sessionId)
{
    const qsizetype index = d->indexOf(sessionId);
    if (index < 0) {
        return;
    }
    clearFailure();
    const QString &state = d->rows[index].state;
    if (state == kRunning || state == kStarting) {
        return;
    }
    if (state != kClosed) {
        d->tell(u"Start"_s, {sessionId}, false);
        return;
    }
    // **Opened first, in the engine**, which starts if it is not running;
    // started when the engine says it is open (Private::opened()).
    d->openingToRun.insert(sessionId);
    QTimer::singleShot(kOpenTimeoutMs, this, [this, sessionId] { d->openingToRun.remove(sessionId); });
    d->tell(u"OpenSession"_s, {sessionId}, true);
}

void SessionList::stop(const QString &sessionId)
{
    d->openingToRun.remove(sessionId);
    if (d->engineRunning) {
        d->tell(u"Stop"_s, {sessionId}, false);
    }
}

void SessionList::refresh()
{
    d->tell(u"ListSessions"_s, {}, true);
}

void SessionList::clearFailure()
{
    if (!d->failure.isEmpty()) {
        d->failure.clear();
        Q_EMIT failureChanged();
    }
}

void SessionList::readLevels()
{
    // **The fall is by the clock**, from the last read, so a slow tick does
    // not make a slow bar.
    qint64 elapsed = 0;
    if (d->sinceTick.isValid()) {
        elapsed = d->sinceTick.restart();
    } else {
        d->sinceTick.start();
    }
    const float fall = kFallDbPerSecond * static_cast<float>(elapsed) / 1000.0f;
    for (qsizetype index = 0; index < d->rows.size(); ++index) {
        Private::Row &row = d->rows[index];
        if (row.state != kRunning) {
            continue;
        }
        // **The loudest of every block's peaks**: how loud the session is
        // anywhere in it, which is what a glance at a list asks.
        float loudest = kSilenceDb;
        if (const auto found = d->telemetry.find(row.id); found != d->telemetry.end() && found->second.mapping) {
            const core::TelemetrySnapshot snapshot = found->second.mapping->slot().load();
            const std::uint32_t blocks = std::min<std::uint32_t>(snapshot.blockCount, core::kMaxMeteredBlocks);
            for (std::uint32_t block = 0; block < blocks; ++block) {
                const core::BlockTelemetry &levels = snapshot.blocks[block];
                const std::uint32_t channels = std::min<std::uint32_t>(levels.channelCount, rt::kMaxMeterChannels);
                for (std::uint32_t channel = 0; channel < channels; ++channel) {
                    loudest = std::max(loudest, gainToDb(levels.peak[channel]));
                }
            }
        }
        const float level = std::max(loudest, std::max(kSilenceDb, row.levelDb - fall));
        if (level != row.levelDb) {
            row.levelDb = level;
            Q_EMIT dataChanged(this->index(static_cast<int>(index)), this->index(static_cast<int>(index)), {LevelRole});
        }
    }
}

void SessionList::heard(const QDBusMessage &message)
{
    const QVariantList arguments = message.arguments();
    const auto text = [&arguments](qsizetype at) {
        return at < arguments.size() ? arguments[at].toString() : QString();
    };
    const QString member = message.member();
    if (member == "SessionsListed"_L1) {
        d->listed(text(0));
    } else if (member == "StateChanged"_L1) {
        d->stateChanged(text(0), text(1));
    } else if (member == "SessionOpened"_L1) {
        d->opened(text(0));
    } else if (member == "SessionClosed"_L1) {
        d->closed(text(0));
    } else if (member == "RecordingFileStarted"_L1) {
        d->recordingFile(text(0), text(2), true);
    } else if (member == "RecordingFileFinished"_L1) {
        d->recordingFile(text(0), text(2), false);
    } else if (member == "Error"_L1) {
        d->error(text(0), text(1));
    }
}

} // namespace rb::plasmoid
