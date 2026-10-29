// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "sessionoverview.h"

#include "appshell.h"
#include "core/session.h"
#include "core/telemetry.h"
#include "dsp/decibel.h"
#include "windowstate.h"

#include <QCollator>
#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSet>
#include <QTimer>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace rb::ui
{

namespace
{

constexpr QLatin1StringView kName{"name"};
constexpr QLatin1StringView kSources{"sources"};
constexpr QLatin1StringView kRecordings{"recordings"};
constexpr QLatin1StringView kLevel{"level"};
constexpr QLatin1StringView kStatus{"status"};
constexpr QLatin1StringView kRun{"run"};
constexpr QLatin1StringView kAutoRun{"autoRun"};

/// The edit that sets a session's Auto Run.
[[nodiscard]] QString autoRunEdit(bool on)
{
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"op"_L1, u"setSessionSetting"_s},
                                                       {"key"_L1, QString(kAutoRun)},
                                                       {"value"_L1, on ? u"login"_s : u"off"_s}})
                                 .toJson(QJsonDocument::Compact));
}

/// Where a session stands, in the order a table sorted by status shows them:
/// what is making sound first, what is only on disk last.
[[nodiscard]] int statusRank(const QString &state)
{
    if (state == core::sessionState::running) {
        return 0;
    }
    if (state == core::sessionState::starting || state == core::sessionState::stopping) {
        return 1;
    }
    if (state == core::sessionState::error) {
        return 2;
    }
    if (state == core::sessionState::closed) {
        return 4;
    }
    return 3; // open and idle
}

} // namespace

class SessionOverview::Private
{
public:
    struct Row {
        QString id;
        QString name;
        QString state;
        QVariantList sources;
        int recordings = 0;
        qint64 startedAtMs = 0;
        float levelDb = dsp::kSilenceDb;
        bool autoRun = false;
    };

    struct Column {
        QString key;
        bool shown = true;
        int width = 0;
    };

    QPointer<core::EngineFacade> facade;
    QList<Row> rows;
    bool listed = false;
    /// Every column in the order they sit, shown or not.
    QList<Column> columns;
    QString sortColumn = QString(kName);
    bool descending = false;
    /// Sessions asked to run that had to be opened first.
    QSet<QString> startWhenOpen;
    /// Sessions whose Auto Run was set while they were only on disk, and
    /// what it was set to.
    QHash<QString, bool> autoRunWhenOpen;
    bool watching = false;
    QTimer ticker;
    QElapsedTimer sinceTick;
    /// Asks for the list again, once, a moment after the last edit asked.
    QTimer relistSoon;

    Private()
    {
        for (const QString &key : SessionOverview::allColumns()) {
            columns.append(Column{key, true, 0});
        }
    }

    [[nodiscard]] QStringList shown() const
    {
        QStringList keys;
        for (const Column &column : columns) {
            if (column.shown) {
                keys.append(column.key);
            }
        }
        return keys;
    }

    [[nodiscard]] Column *columnNamed(const QString &key)
    {
        const auto found = std::ranges::find(columns, key, &Column::key);
        return found == columns.end() ? nullptr : &*found;
    }

    [[nodiscard]] int rowIndex(const QString &sessionId) const
    {
        for (qsizetype index = 0; index < rows.size(); ++index) {
            if (rows.at(index).id == sessionId) {
                return static_cast<int>(index);
            }
        }
        return -1;
    }

    /// Puts the rows in the order the table is sorted by. **Stable**, and
    /// by name after the key, so rows that tie keep a reading order.
    void sort()
    {
        QCollator collator;
        collator.setNumericMode(true);
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        const auto byName = [&collator](const Row &a, const Row &b) { return collator.compare(a.name, b.name) < 0; };
        const QString key = sortColumn;
        const auto less = [&](const Row &a, const Row &b) {
            if (key == kSources) {
                if (a.sources.size() != b.sources.size()) {
                    return a.sources.size() < b.sources.size();
                }
            } else if (key == kRecordings) {
                if (a.recordings != b.recordings) {
                    return a.recordings < b.recordings;
                }
            } else if (key == kLevel) {
                if (a.levelDb != b.levelDb) {
                    return a.levelDb < b.levelDb;
                }
            } else if (key == kAutoRun) {
                if (a.autoRun != b.autoRun) {
                    return a.autoRun; // the ones that start at login first
                }
            } else if (key == kStatus || key == kRun) {
                const int rankA = statusRank(a.state);
                const int rankB = statusRank(b.state);
                if (rankA != rankB) {
                    return rankA < rankB;
                }
                if (a.startedAtMs != b.startedAtMs) {
                    return a.startedAtMs < b.startedAtMs; // the longest running first
                }
            }
            return byName(a, b);
        };
        std::ranges::stable_sort(rows,
                                 [&](const Row &a, const Row &b) { return descending ? less(b, a) : less(a, b); });
    }

    void remember() const
    {
        QVariantList written;
        for (const Column &column : columns) {
            written.append(QVariantMap{{u"key"_s, column.key}, {u"shown"_s, column.shown}, {u"width"_s, column.width}});
        }
        WindowState().setTableLayout(
            QString(kTableName),
            QVariantMap{{u"columns"_s, written}, {u"sortColumn"_s, sortColumn}, {u"descending"_s, descending}});
    }

    /// Takes back what was remembered. **Columns the file does not know go
    /// at the end, shown**; ones it names that no longer exist are dropped.
    void recall()
    {
        const QVariantMap layout = WindowState().tableLayout(QString(kTableName));
        if (layout.isEmpty()) {
            return;
        }
        QList<Column> recalled;
        const QStringList known = SessionOverview::allColumns();
        for (const QVariant &entry : layout.value(u"columns"_s).toList()) {
            const QVariantMap column = entry.toMap();
            const QString key = column.value(u"key"_s).toString();
            if (!known.contains(key) || std::ranges::find(recalled, key, &Column::key) != recalled.end()) {
                continue;
            }
            recalled.append(Column{
                key, key == kName || column.value(u"shown"_s).toBool(), std::max(0, column.value(u"width"_s).toInt())});
        }
        for (const QString &key : known) {
            if (std::ranges::find(recalled, key, &Column::key) == recalled.end()) {
                recalled.append(Column{key, true, 0});
            }
        }
        columns = recalled;
        const QString sortKey = layout.value(u"sortColumn"_s).toString();
        if (known.contains(sortKey)) {
            sortColumn = sortKey;
            descending = layout.value(u"descending"_s).toBool();
        }
    }

    [[nodiscard]] bool anyRunning() const
    {
        return std::ranges::any_of(rows, [](const Row &row) { return row.state == core::sessionState::running; });
    }
};

SessionOverview::SessionOverview(QObject *parent)
    : QAbstractTableModel(parent)
    , d(std::make_unique<Private>())
{
    d->recall();
    d->ticker.setInterval(kLevelTickMs);
    connect(&d->ticker, &QTimer::timeout, this, &SessionOverview::readLevels);
    d->relistSoon.setSingleShot(true);
    d->relistSoon.setInterval(kRelistDelayMs);
    connect(&d->relistSoon, &QTimer::timeout, this, &SessionOverview::refresh);
    attach(AppShell::engine());
}

SessionOverview::~SessionOverview() = default;

QStringList SessionOverview::allColumns()
{
    return {QString(kName),
            QString(kSources),
            QString(kRecordings),
            QString(kLevel),
            QString(kStatus),
            QString(kAutoRun),
            QString(kRun)};
}

int SessionOverview::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(d->rows.size());
}

int SessionOverview::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(d->shown().size());
}

QVariant SessionOverview::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= d->rows.size()) {
        return {};
    }
    const Private::Row &row = d->rows.at(index.row());
    switch (role) {
    case KeyRole: {
        const QStringList shown = d->shown();
        return index.column() < shown.size() ? shown.at(index.column()) : QString{};
    }
    case Qt::DisplayRole:
    case NameRole:
        return row.name;
    case IdRole:
        return row.id;
    case StateRole:
        return row.state;
    case RunningRole:
        return row.state == core::sessionState::running;
    case SourcesRole:
        return row.sources;
    case RecordingsRole:
        return row.recordings;
    case StartedAtRole:
        return row.startedAtMs > 0 ? QDateTime::fromMSecsSinceEpoch(row.startedAtMs) : QDateTime{};
    case LevelRole:
        return row.levelDb;
    case AutoRunRole:
        return row.autoRun;
    default:
        return {};
    }
}

QVariant SessionOverview::headerData(int section, Qt::Orientation orientation, int role) const
{
    const QStringList shown = d->shown();
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole || section < 0 || section >= shown.size()) {
        return QAbstractTableModel::headerData(section, orientation, role);
    }
    return shown.at(section);
}

QHash<int, QByteArray> SessionOverview::roleNames() const
{
    return {
        // **What a header view reads a column's head from**: its key. The
        // words are the window's.
        {Qt::DisplayRole, "display"},
        {KeyRole, "columnKey"},
        {IdRole, "sessionId"},
        {NameRole, "name"},
        {StateRole, "sessionState"},
        {RunningRole, "running"},
        {SourcesRole, "sources"},
        {RecordingsRole, "recordings"},
        {StartedAtRole, "startedAt"},
        {LevelRole, "levelDb"},
        {AutoRunRole, "autoRun"},
    };
}

QStringList SessionOverview::columns() const
{
    return d->shown();
}

QString SessionOverview::sortColumn() const
{
    return d->sortColumn;
}

bool SessionOverview::sortDescending() const
{
    return d->descending;
}

int SessionOverview::count() const
{
    return static_cast<int>(d->rows.size());
}

bool SessionOverview::isReady() const
{
    return d->listed;
}

bool SessionOverview::isWatching() const
{
    return d->watching;
}

void SessionOverview::setWatching(bool watching)
{
    if (d->watching == watching) {
        return;
    }
    d->watching = watching;
    if (watching && d->anyRunning()) {
        d->sinceTick.start();
        d->ticker.start();
    } else {
        d->ticker.stop();
    }
    Q_EMIT watchingChanged();
}

void SessionOverview::setColumnShown(const QString &key, bool shown)
{
    Private::Column *column = d->columnNamed(key);
    if (column == nullptr || column->shown == shown || (key == kName && !shown)) {
        return;
    }
    beginResetModel();
    column->shown = shown;
    endResetModel();
    d->remember();
    Q_EMIT columnsChanged();
}

bool SessionOverview::isColumnShown(const QString &key) const
{
    const auto found = std::ranges::find(d->columns, key, &Private::Column::key);
    return found != d->columns.end() && found->shown;
}

void SessionOverview::moveColumn(int from, int to)
{
    const QStringList shown = d->shown();
    if (from < 0 || to < 0 || from >= shown.size() || to >= shown.size() || from == to) {
        return;
    }
    // **Moved among the shown ones**, where the header was dragged; a hidden
    // column keeps its place relative to the one it sat after.
    const auto indexOf = [this](const QString &key) {
        return std::ranges::find(d->columns, key, &Private::Column::key) - d->columns.begin();
    };
    const qsizetype source = indexOf(shown.at(from));
    const qsizetype target = indexOf(shown.at(to));
    beginResetModel();
    d->columns.move(source, target);
    endResetModel();
    d->remember();
    Q_EMIT columnsChanged();
}

int SessionOverview::columnWidth(const QString &key) const
{
    const auto found = std::ranges::find(d->columns, key, &Private::Column::key);
    return found == d->columns.end() ? 0 : found->width;
}

void SessionOverview::setColumnWidth(const QString &key, int width)
{
    Private::Column *column = d->columnNamed(key);
    if (column == nullptr || column->width == std::max(0, width)) {
        return;
    }
    column->width = std::max(0, width);
    d->remember();
    Q_EMIT columnsChanged();
}

void SessionOverview::sortBy(const QString &key)
{
    if (!allColumns().contains(key)) {
        return;
    }
    Q_EMIT layoutAboutToBeChanged();
    if (d->sortColumn == key) {
        d->descending = !d->descending;
    } else {
        d->sortColumn = key;
        d->descending = false;
    }
    d->sort();
    Q_EMIT layoutChanged();
    d->remember();
    Q_EMIT sortChanged();
}

QString SessionOverview::idAt(int row) const
{
    return row >= 0 && row < d->rows.size() ? d->rows.at(row).id : QString{};
}

int SessionOverview::rowOf(const QString &sessionId) const
{
    return d->rowIndex(sessionId);
}

QVariantMap SessionOverview::rowAt(int row) const
{
    if (row < 0 || row >= d->rows.size()) {
        return {};
    }
    const Private::Row &entry = d->rows.at(row);
    return QVariantMap{
        {u"id"_s, entry.id},
        {u"name"_s, entry.name},
        {u"state"_s, entry.state},
        {u"running"_s, entry.state == core::sessionState::running},
        {u"sources"_s, entry.sources},
        {u"recordings"_s, entry.recordings},
        {u"startedAt"_s, entry.startedAtMs > 0 ? QDateTime::fromMSecsSinceEpoch(entry.startedAtMs) : QDateTime{}},
        {u"level"_s, entry.levelDb},
        {u"autoRun"_s, entry.autoRun},
    };
}

void SessionOverview::run(const QStringList &sessionIds)
{
    if (!d->facade) {
        return;
    }
    for (const QString &sessionId : sessionIds) {
        const int index = d->rowIndex(sessionId);
        if (index < 0) {
            continue;
        }
        const QString &state = d->rows.at(index).state;
        if (state == core::sessionState::running || state == core::sessionState::starting) {
            continue;
        }
        if (state == core::sessionState::closed) {
            // **Opened, then started when it says it has opened**: a start
            // before then would be asked of a session the engine does not
            // have open, and refused.
            d->startWhenOpen.insert(sessionId);
            d->facade->openSession(sessionId);
        } else {
            d->facade->start(sessionId);
        }
    }
}

void SessionOverview::setAutoRun(const QStringList &sessionIds, bool on)
{
    if (!d->facade) {
        return;
    }
    for (const QString &sessionId : sessionIds) {
        const int index = d->rowIndex(sessionId);
        if (index < 0 || d->rows.at(index).autoRun == on) {
            continue;
        }
        if (d->rows.at(index).state == core::sessionState::closed) {
            // **Opened, then set**, for the reason run() gives: an edit is
            // asked of an open session or refused.
            d->autoRunWhenOpen.insert(sessionId, on);
            d->facade->openSession(sessionId);
        } else {
            d->facade->applyEdit(sessionId, autoRunEdit(on));
        }
    }
}

void SessionOverview::stop(const QStringList &sessionIds)
{
    if (!d->facade) {
        return;
    }
    for (const QString &sessionId : sessionIds) {
        const int index = d->rowIndex(sessionId);
        if (index < 0) {
            continue;
        }
        const QString &state = d->rows.at(index).state;
        if (state == core::sessionState::running || state == core::sessionState::starting) {
            d->facade->stop(sessionId);
        }
    }
}

void SessionOverview::refresh()
{
    if (d->facade) {
        d->facade->listSessions();
    }
}

void SessionOverview::attach(core::EngineFacade *facade)
{
    if (d->facade) {
        d->facade->disconnect(this);
    }
    d->facade = facade;
    if (!d->facade) {
        return;
    }

    connect(d->facade, &core::EngineFacade::sessionsListed, this, [this](const QString &json) {
        QHash<QString, float> levels;
        for (const Private::Row &row : std::as_const(d->rows)) {
            levels.insert(row.id, row.levelDb);
        }
        QList<Private::Row> rows;
        for (const QJsonValue &entry : QJsonDocument::fromJson(json.toUtf8()).array()) {
            const QJsonObject session = entry.toObject();
            Private::Row row;
            row.id = session.value("id"_L1).toString();
            if (row.id.isEmpty()) {
                continue;
            }
            row.name = session.value("name"_L1).toString();
            row.state = session.value("state"_L1).toString(QString(core::sessionState::closed));
            row.sources = session.value("sources"_L1).toArray().toVariantList();
            row.recordings = session.value("recordings"_L1).toInt();
            row.startedAtMs = static_cast<qint64>(session.value("startedAt"_L1).toDouble());
            row.autoRun = session.value("autoRun"_L1).toString() == QLatin1StringView(core::autoRun::login);
            row.levelDb =
                row.state == core::sessionState::running ? levels.value(row.id, dsp::kSilenceDb) : dsp::kSilenceDb;
            rows.append(row);
        }
        const bool countChanges = !d->listed || rows.size() != d->rows.size();
        beginResetModel();
        d->rows = rows;
        d->listed = true;
        d->sort();
        endResetModel();
        if (countChanges) {
            Q_EMIT countChanged();
        }
        if (d->watching && d->anyRunning() && !d->ticker.isActive()) {
            d->sinceTick.start();
            d->ticker.start();
        }
    });

    // **The list is asked again whenever what it says changes**: a session
    // made, opened, closed or deleted anywhere, or one starting, which is
    // when the engine knows its start time.
    const auto relist = [this]() {
        if (d->facade) {
            d->facade->listSessions();
        }
    };
    connect(d->facade, &core::EngineFacade::sessionOpened, this, [this, relist](const QString &sessionId) {
        if (d->startWhenOpen.remove(sessionId) && d->facade) {
            d->facade->start(sessionId);
        }
        if (const auto wanted = d->autoRunWhenOpen.constFind(sessionId); wanted != d->autoRunWhenOpen.cend()) {
            const bool on = *wanted;
            d->autoRunWhenOpen.erase(wanted);
            if (d->facade) {
                d->facade->applyEdit(sessionId, autoRunEdit(on));
            }
        }
        relist();
    });
    connect(d->facade, &core::EngineFacade::sessionClosed, this, relist);
    connect(d->facade,
            &core::EngineFacade::stateChanged,
            this,
            [this, relist](const QString &sessionId, const QString &state) {
                const int index = d->rowIndex(sessionId);
                if (index < 0) {
                    relist();
                    return;
                }
                Private::Row &row = d->rows[index];
                if (row.state == state) {
                    return;
                }
                row.state = state;
                if (state != core::sessionState::running) {
                    row.startedAtMs = 0;
                    row.levelDb = dsp::kSilenceDb;
                }
                Q_EMIT dataChanged(this->index(index, 0), this->index(index, columnCount() - 1));
                if (state == core::sessionState::running) {
                    relist(); // for when it started, by the engine's clock
                }
                if (d->watching && d->anyRunning() && !d->ticker.isActive()) {
                    d->sinceTick.start();
                    d->ticker.start();
                }
            });
    // **A patch can change a row**: the session's own rename (one with no
    // block id) says its new name at once, and a block added, removed or
    // renamed can change its sources, which only the list says; that is
    // asked once the burst of an edit is over.
    connect(d->facade,
            &core::EngineFacade::sessionPatch,
            this,
            [this](const QString &sessionId, quint64, const QString &patchJson) {
                const QJsonObject patch = QJsonDocument::fromJson(patchJson.toUtf8()).object();
                const QString op = patch.value("op"_L1).toString();
                const bool ownName = op == "rename"_L1 && patch.value("blockId"_L1).toString().isEmpty();
                if (ownName) {
                    const int index = d->rowIndex(sessionId);
                    if (index >= 0) {
                        d->rows[index].name = patch.value("name"_L1).toString();
                        Q_EMIT dataChanged(this->index(index, 0), this->index(index, columnCount() - 1));
                    }
                } else if (op == "setSessionSetting"_L1 && patch.value("key"_L1).toString() == kAutoRun) {
                    // Says itself at once, as a rename does: the patch
                    // carries the value.
                    const int index = d->rowIndex(sessionId);
                    if (index >= 0) {
                        d->rows[index].autoRun =
                            patch.value("value"_L1).toString() == QLatin1StringView(core::autoRun::login);
                        Q_EMIT dataChanged(this->index(index, 0), this->index(index, columnCount() - 1));
                    }
                } else if (op == "addBlock"_L1 || op == "removeBlock"_L1 || op == "rename"_L1) {
                    d->relistSoon.start();
                }
            });
    connect(d->facade, &core::EngineFacade::recordingsChanged, this, relist);

    d->facade->listSessions();
}

void SessionOverview::readLevels()
{
    // **The fall is by the clock**, from the last read, so a slow tick does
    // not make a slow meter.
    qint64 elapsed = 0;
    if (d->sinceTick.isValid()) {
        elapsed = d->sinceTick.restart();
    } else {
        d->sinceTick.start();
    }
    const float fall = kFallDbPerSecond * static_cast<float>(elapsed) / 1000.0f;
    bool anyRunning = false;
    for (qsizetype index = 0; index < d->rows.size(); ++index) {
        Private::Row &row = d->rows[index];
        if (row.state != core::sessionState::running) {
            continue;
        }
        anyRunning = true;
        // **The loudest of every block's peaks**: how loud the session is
        // anywhere in it, which is what a glance down a list asks.
        float loudest = dsp::kSilenceDb;
        if (const core::TelemetrySlot *slot = AppShell::telemetryOf(row.id); slot != nullptr) {
            const core::TelemetrySnapshot snapshot = slot->load();
            const std::uint32_t blocks = std::min<std::uint32_t>(snapshot.blockCount, core::kMaxMeteredBlocks);
            for (std::uint32_t block = 0; block < blocks; ++block) {
                const core::BlockTelemetry &levels = snapshot.blocks[block];
                const std::uint32_t channels = std::min<std::uint32_t>(levels.channelCount, rt::kMaxMeterChannels);
                for (std::uint32_t channel = 0; channel < channels; ++channel) {
                    loudest = std::max(loudest, dsp::gainToDb(levels.peak[channel]));
                }
            }
        }
        const float level = std::max(loudest, std::max(dsp::kSilenceDb, row.levelDb - fall));
        if (level != row.levelDb) {
            row.levelDb = level;
            const QModelIndex at = this->index(static_cast<int>(index), 0);
            Q_EMIT dataChanged(at, this->index(static_cast<int>(index), columnCount() - 1), {LevelRole});
        }
    }
    if (!anyRunning) {
        d->ticker.stop();
    }
}

} // namespace rb::ui
