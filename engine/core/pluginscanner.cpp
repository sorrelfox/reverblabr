// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/pluginscanner.h"

#include "plugin/formats.h"
#include "pw/pluginhostprocess.h"

#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QProcess>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTimer>

#include <deque>
#include <utility>

using namespace Qt::StringLiterals;

Q_LOGGING_CATEGORY(lcScanner, "reverblabr.plugins")

namespace rb::core
{

namespace
{

/// Two: each plugin's settings. A cache of another version is found
/// again from nothing, which is all a cache is for.
constexpr int kSchemaVersion = 2;
/// What a candidate that could be read is, and the two that are
/// blocklisted.
constexpr QLatin1StringView kRead{"ok"};
constexpr QLatin1StringView kUnreadable{"unreadable"};
constexpr QLatin1StringView kCrashed{"crashed"};
constexpr QLatin1StringView kHung{"hung"};
/// **A candidate whose describing stopped part of the way**: it told
/// of some plugins and then nothing more for as long as the limit allows.
/// What it told is kept, and so is what an earlier scan found in it that
/// this one did not reach; it is looked into again at the next scan, and
/// said to be incomplete until one gets through it.
constexpr QLatin1StringView kIncomplete{"incomplete"};
/// How long a burst of changes in a watched folder settles before the scan.
constexpr std::chrono::milliseconds kSettle{1000};
/// The host's exit code for a candidate that holds nothing it can describe.
constexpr int kScanUnreadable = 5;

[[nodiscard]] QString fromStd(const std::string &text)
{
    return QString::fromStdString(text);
}

} // namespace

QJsonObject pluginsJson(const QList<InstalledPlugin> &plugins,
                        const QList<BlockedCandidate> &blocked,
                        bool scanning,
                        const QList<BlockedCandidate> &incomplete)
{
    QJsonArray found;
    for (const InstalledPlugin &plugin : plugins) {
        found.append(QJsonObject{{"format"_L1, plugin.format},
                                 {"id"_L1, plugin.id},
                                 {"name"_L1, plugin.name},
                                 {"vendor"_L1, plugin.vendor},
                                 {"category"_L1, plugin.category},
                                 {"audioInputs"_L1, plugin.audioInputs},
                                 {"audioOutputs"_L1, plugin.audioOutputs},
                                 {"parameters"_L1, plugin.parameters}});
    }
    QJsonArray refused;
    for (const BlockedCandidate &candidate : blocked) {
        refused.append(
            QJsonObject{{"format"_L1, candidate.format}, {"path"_L1, candidate.path}, {"reason"_L1, candidate.reason}});
    }
    QJsonArray cutShort;
    for (const BlockedCandidate &candidate : incomplete) {
        cutShort.append(QJsonObject{{"format"_L1, candidate.format}, {"path"_L1, candidate.path}});
    }
    return QJsonObject{
        {"scanning"_L1, scanning}, {"plugins"_L1, found}, {"blocked"_L1, refused}, {"incomplete"_L1, cutShort}};
}

std::vector<plugin::PluginParam> paramsFromJson(const QJsonArray &json)
{
    std::vector<plugin::PluginParam> params;
    for (const QJsonValue &entry : json) {
        const QJsonObject object = entry.toObject();
        plugin::PluginParam param;
        param.id = object.value("id"_L1).toString().toStdString();
        param.name = object.value("name"_L1).toString().toStdString();
        const QString kind = object.value("kind"_L1).toString();
        param.kind = kind == "int"_L1 ? plugin::PluginParam::Kind::Int
            : kind == "toggle"_L1     ? plugin::PluginParam::Kind::Toggle
            : kind == "choice"_L1     ? plugin::PluginParam::Kind::Choice
                                      : plugin::PluginParam::Kind::Float;
        param.minimum = static_cast<float>(object.value("min"_L1).toDouble());
        param.maximum = static_cast<float>(object.value("max"_L1).toDouble(1.0));
        param.defaultValue = static_cast<float>(object.value("default"_L1).toDouble());
        param.logarithmic = object.value("log"_L1).toBool();
        for (const QJsonValue &choice : object.value("choices"_L1).toArray()) {
            param.choices.emplace_back(static_cast<float>(choice.toObject().value("value"_L1).toDouble()),
                                       choice.toObject().value("label"_L1).toString().toStdString());
        }
        if (!param.id.empty()) {
            params.push_back(std::move(param));
        }
    }
    return params;
}

QString defaultPluginCachePath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath(u"plugins.sqlite"_s);
}

class PluginScanner::Private
{
public:
    PluginScanner *q = nullptr;
    QString connectionName;
    bool open = false;
    QString program = pw::defaultPluginHostProgram();
    std::chrono::milliseconds timeout{10000};
    int parallel = 4;
    std::deque<plugin::Candidate> queue;
    int active = 0;
    bool scanning = false;
    bool again = false;
    bool againRetry = false;
    int described = 0;
    QFileSystemWatcher watcher;
    QTimer settle;
    bool watching = true;

    [[nodiscard]] QSqlDatabase db() const
    {
        return QSqlDatabase::database(connectionName, false);
    }

    [[nodiscard]] QSqlQuery prepare(const QString &sql) const
    {
        QSqlQuery query(db());
        query.prepare(sql);
        return query;
    }

    bool exec(QSqlQuery &query) const
    {
        if (!query.exec()) {
            qCWarning(lcScanner) << "the plugin cache refused" << query.lastQuery() << query.lastError().text();
            return false;
        }
        return true;
    }

    /// What the cache knows of \a format's candidates: modification time
    /// and status, by path.
    [[nodiscard]] QHash<QString, std::pair<qint64, QString>> known(const QString &format) const
    {
        QHash<QString, std::pair<qint64, QString>> result;
        QSqlQuery query = prepare(u"SELECT path, modified, status FROM candidates WHERE format = ?"_s);
        query.addBindValue(format);
        if (exec(query)) {
            while (query.next()) {
                result.insert(query.value(0).toString(), {query.value(1).toLongLong(), query.value(2).toString()});
            }
        }
        return result;
    }

    void forget(const QString &format, const QString &path)
    {
        QSqlQuery plugins = prepare(u"DELETE FROM plugins WHERE format = ? AND candidate = ?"_s);
        plugins.addBindValue(format);
        plugins.addBindValue(path);
        exec(plugins);
        QSqlQuery candidate = prepare(u"DELETE FROM candidates WHERE format = ? AND path = ?"_s);
        candidate.addBindValue(format);
        candidate.addBindValue(path);
        exec(candidate);
    }

    /// Writes down what describing \a candidate came to. What an earlier
    /// scan found in it goes, unless the describing was \a cutShort: then
    /// what this one did not reach is kept as it was.
    void record(const plugin::Candidate &candidate,
                const QString &status,
                const QList<InstalledPlugin> &found,
                bool cutShort = false)
    {
        QSqlDatabase database = db();
        database.transaction();
        const QString format = fromStd(candidate.format);
        const QString path = fromStd(candidate.path);
        if (!cutShort) {
            QSqlQuery plugins = prepare(u"DELETE FROM plugins WHERE format = ? AND candidate = ?"_s);
            plugins.addBindValue(format);
            plugins.addBindValue(path);
            exec(plugins);
        }
        for (const InstalledPlugin &plugin : found) {
            QSqlQuery insert = prepare(
                u"INSERT OR REPLACE INTO plugins (format, id, candidate, name, vendor, category, "
                "audio_inputs, audio_outputs, parameters, params) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"_s);
            for (const QVariant &value :
                 {QVariant(plugin.format),
                  QVariant(plugin.id),
                  QVariant(path),
                  QVariant(plugin.name),
                  QVariant(plugin.vendor),
                  QVariant(plugin.category),
                  QVariant(plugin.audioInputs),
                  QVariant(plugin.audioOutputs),
                  QVariant(plugin.parameters),
                  QVariant(QString::fromUtf8(QJsonDocument(plugin.params).toJson(QJsonDocument::Compact)))}) {
                insert.addBindValue(value);
            }
            exec(insert);
        }
        QSqlQuery upsert =
            prepare(u"INSERT OR REPLACE INTO candidates (format, path, modified, status) VALUES (?, ?, ?, ?)"_s);
        upsert.addBindValue(format);
        upsert.addBindValue(path);
        upsert.addBindValue(static_cast<qint64>(candidate.modified));
        upsert.addBindValue(status);
        exec(upsert);
        database.commit();
    }

    /// The plugins a scanning host wrote, a line of JSON each, keeping only
    /// those of the format it was asked about.
    [[nodiscard]] static QList<InstalledPlugin> parse(const QByteArray &output, const QString &format)
    {
        QList<InstalledPlugin> found;
        for (const QByteArray &line : output.split('\n')) {
            if (line.trimmed().isEmpty()) {
                continue;
            }
            const QJsonObject json = QJsonDocument::fromJson(line).object();
            InstalledPlugin plugin;
            plugin.format = json.value("format"_L1).toString();
            plugin.id = json.value("id"_L1).toString();
            if (plugin.format != format || plugin.id.isEmpty()) {
                continue;
            }
            plugin.name = json.value("name"_L1).toString();
            if (plugin.name.isEmpty()) {
                plugin.name = plugin.id;
            }
            plugin.vendor = json.value("vendor"_L1).toString();
            plugin.category = json.value("category"_L1).toString();
            plugin.audioInputs = json.value("audioInputs"_L1).toInt();
            plugin.audioOutputs = json.value("audioOutputs"_L1).toInt();
            plugin.parameters = json.value("parameters"_L1).toInt();
            plugin.params = json.value("params"_L1).toArray();
            found.append(plugin);
        }
        return found;
    }

    /// Starts describing as many queued candidates as may run at once, and
    /// finishes the scan when none are left.
    void pump()
    {
        while (active < parallel && !queue.empty()) {
            describe(queue.front());
            queue.pop_front();
        }
        if (active == 0 && queue.empty() && scanning) {
            scanning = false;
            Q_EMIT q->scanningChanged(false);
            Q_EMIT q->changed();
            if (std::exchange(again, false)) {
                q->scan(std::exchange(againRetry, false));
            }
        }
    }

    /// **Describes one candidate in a host of its own.** The limit is on
    /// progress, not on the whole: the host writes each plugin's
    /// line as soon as it has described it, and every line starts the
    /// limit again. A file of 194 plugins that takes a second on an idle
    /// machine took over nine under a parallel build, and a limit on the
    /// whole killed it and blocklisted it as hung with nothing found. Now
    /// only a candidate that says nothing new for the whole limit is given
    /// up on: hung if it had said nothing, incomplete if it had.
    void describe(const plugin::Candidate &candidate)
    {
        ++active;
        ++described;
        auto *process = new QProcess(q);
        auto *limit = new QTimer(process);
        limit->setSingleShot(true);
        // A coarse timer may fire up to 5 % early.
        limit->setTimerType(Qt::PreciseTimer);
        auto timedOut = std::make_shared<bool>(false);
        auto output = std::make_shared<QByteArray>();
        QObject::connect(limit, &QTimer::timeout, process, [process, timedOut] {
            *timedOut = true;
            process->kill();
        });
        QObject::connect(process, &QProcess::readyReadStandardOutput, process, [process, limit, output, this] {
            const QByteArray more = process->readAllStandardOutput();
            output->append(more);
            if (more.contains('\n')) {
                limit->start(timeout);
            }
        });
        QObject::connect(process,
                         &QProcess::finished,
                         q,
                         [this, process, candidate, timedOut, output](int exitCode, QProcess::ExitStatus exitStatus) {
                             const QString format = fromStd(candidate.format);
                             output->append(process->readAllStandardOutput());
                             if (*timedOut) {
                                 // Only whole lines: the last may have been
                                 // cut off by the kill.
                                 const qsizetype end = output->lastIndexOf('\n');
                                 const QList<InstalledPlugin> told =
                                     end < 0 ? QList<InstalledPlugin>{} : parse(output->left(end + 1), format);
                                 if (told.isEmpty()) {
                                     qCWarning(lcScanner)
                                         << "hung while being described, blocklisted:" << candidate.path;
                                     record(candidate, kHung, {});
                                 } else {
                                     qCWarning(lcScanner) << "stopped part of the way through being described, after"
                                                          << told.size() << "plugins:" << candidate.path;
                                     record(candidate, kIncomplete, told, true);
                                 }
                             } else if (exitStatus == QProcess::CrashExit) {
                                 qCWarning(lcScanner)
                                     << "crashed while being described, blocklisted:" << candidate.path;
                                 record(candidate, kCrashed, {});
                             } else if (exitCode == kScanUnreadable || exitCode != 0) {
                                 record(candidate, kUnreadable, {});
                             } else {
                                 record(candidate, kRead, parse(*output, format));
                             }
                             process->deleteLater();
                             --active;
                             pump();
                         });
        QObject::connect(process, &QProcess::errorOccurred, q, [process, candidate](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) {
                qCWarning(lcScanner) << "the plugin host could not be started to describe" << candidate.path;
                // finished() is not said for a process that never started.
                QMetaObject::invokeMethod(
                    process, [process] { Q_EMIT process->finished(-1, QProcess::NormalExit); }, Qt::QueuedConnection);
            }
        });
        process->setProcessChannelMode(QProcess::SeparateChannels);
        process->setStandardErrorFile(QProcess::nullDevice());
        limit->start(timeout);
        process->start(program, {u"--scan"_s, fromStd(candidate.format), fromStd(candidate.path)});
    }

    /// Watches the folders the formats look in that are there.
    void watchFolders()
    {
        if (!watcher.directories().isEmpty()) {
            watcher.removePaths(watcher.directories());
        }
        if (!watching) {
            return;
        }
        QStringList folders;
        for (const std::unique_ptr<plugin::PluginFormat> &format : plugin::formats()) {
            for (const std::filesystem::path &root : format->searchPaths()) {
                const QString folder = QString::fromStdString(root.string());
                if (QFileInfo(folder).isDir()) {
                    folders.append(folder);
                }
            }
        }
        if (!folders.isEmpty()) {
            watcher.addPaths(folders);
        }
    }
};

PluginScanner::PluginScanner(const QString &cachePath, QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->q = this;
    d->connectionName = u"reverblabr-plugins-%1"_s.arg(reinterpret_cast<quintptr>(this), 0, 16);
    d->settle.setSingleShot(true);
    d->settle.setInterval(kSettle);
    connect(&d->settle, &QTimer::timeout, this, [this] { scan(); });
    connect(&d->watcher, &QFileSystemWatcher::directoryChanged, this, [this] { d->settle.start(); });

    if (!QSqlDatabase::isDriverAvailable(u"QSQLITE"_s)) {
        qCWarning(lcScanner) << "Qt's SQLite driver is missing; plugins will be found again every time";
        return;
    }
    QDir().mkpath(QFileInfo(cachePath).absolutePath());
    QSqlDatabase db = QSqlDatabase::addDatabase(u"QSQLITE"_s, d->connectionName);
    db.setDatabaseName(cachePath);
    db.setConnectOptions(u"QSQLITE_BUSY_TIMEOUT=5000"_s);
    if (!db.open()) {
        qCWarning(lcScanner) << "the plugin cache at" << cachePath << "could not be opened:" << db.lastError().text();
        return;
    }
    QSqlQuery query(db);
    if (query.exec(u"PRAGMA user_version"_s) && query.next() && query.value(0).toInt() != kSchemaVersion) {
        (void)query.exec(u"DROP TABLE IF EXISTS plugins"_s);
        (void)query.exec(u"DROP TABLE IF EXISTS candidates"_s);
    }
    d->open = query.exec(
                  u"CREATE TABLE IF NOT EXISTS candidates (format TEXT NOT NULL, path TEXT NOT NULL, "
                  "modified INTEGER NOT NULL, status TEXT NOT NULL, PRIMARY KEY (format, path))"_s)
        && query.exec(
            u"CREATE TABLE IF NOT EXISTS plugins (format TEXT NOT NULL, id TEXT NOT NULL, "
            "candidate TEXT NOT NULL, name TEXT, vendor TEXT, category TEXT, audio_inputs INTEGER, "
            "audio_outputs INTEGER, parameters INTEGER, params TEXT, PRIMARY KEY (format, id))"_s)
        && query.exec(u"PRAGMA user_version = %1"_s.arg(kSchemaVersion));
    if (!d->open) {
        qCWarning(lcScanner) << "the plugin cache could not be set up:" << query.lastError().text();
    }
}

PluginScanner::~PluginScanner()
{
    // The processes are children and go with this; their finished() would
    // reach a cache already closed.
    for (QProcess *process : findChildren<QProcess *>()) {
        process->disconnect(this);
        process->kill();
        process->waitForFinished(1000);
    }
    {
        QSqlDatabase db = QSqlDatabase::database(d->connectionName, false);
        db.close();
    }
    QSqlDatabase::removeDatabase(d->connectionName);
}

void PluginScanner::setHostProgram(const QString &program)
{
    d->program = program;
}

void PluginScanner::setTimeout(std::chrono::milliseconds timeout)
{
    d->timeout = timeout;
}

void PluginScanner::setParallel(int processes)
{
    d->parallel = std::max(processes, 1);
}

void PluginScanner::setWatchingFolders(bool watching)
{
    d->watching = watching;
    d->watchFolders();
}

void PluginScanner::scan(bool retryBlocked)
{
    if (d->scanning) {
        d->again = true;
        d->againRetry = d->againRetry || retryBlocked;
        return;
    }
    d->scanning = true;
    d->described = 0;
    Q_EMIT scanningChanged(true);
    d->watchFolders();

    for (const std::unique_ptr<plugin::PluginFormat> &format : plugin::formats()) {
        const QString name = QString::fromUtf8(format->name().data(), static_cast<qsizetype>(format->name().size()));
        QHash<QString, std::pair<qint64, QString>> known = d->known(name);
        for (const plugin::Candidate &candidate : plugin::candidatesOf(*format)) {
            const auto was = known.constFind(fromStd(candidate.path));
            const bool blocked = was != known.constEnd() && (was->second == kCrashed || was->second == kHung);
            const bool unfinished = was != known.constEnd() && was->second == kIncomplete;
            if (was == known.constEnd() || was->first != candidate.modified || (blocked && retryBlocked)
                || unfinished) {
                d->queue.push_back(candidate);
            }
            known.remove(fromStd(candidate.path));
        }
        // What is left is no longer there, and nor are its plugins.
        for (auto gone = known.cbegin(); gone != known.cend(); ++gone) {
            d->forget(name, gone.key());
        }
    }
    d->pump();
}

bool PluginScanner::isScanning() const
{
    return d->scanning;
}

QList<InstalledPlugin> PluginScanner::plugins() const
{
    QList<InstalledPlugin> result;
    if (!d->open) {
        return result;
    }
    QSqlQuery query = d->prepare(
        u"SELECT format, id, name, vendor, category, audio_inputs, audio_outputs, "
        "parameters, candidate FROM plugins ORDER BY format, name COLLATE NOCASE, id"_s);
    if (d->exec(query)) {
        while (query.next()) {
            InstalledPlugin plugin;
            plugin.format = query.value(0).toString();
            plugin.id = query.value(1).toString();
            plugin.name = query.value(2).toString();
            plugin.vendor = query.value(3).toString();
            plugin.category = query.value(4).toString();
            plugin.audioInputs = query.value(5).toInt();
            plugin.audioOutputs = query.value(6).toInt();
            plugin.parameters = query.value(7).toInt();
            plugin.candidate = query.value(8).toString();
            result.append(plugin);
        }
    }
    return result;
}

QList<BlockedCandidate> PluginScanner::blocked() const
{
    QList<BlockedCandidate> result;
    if (!d->open) {
        return result;
    }
    QSqlQuery query = d->prepare(
        u"SELECT format, path, status FROM candidates WHERE status IN (?, ?) "
        "ORDER BY format, path"_s);
    query.addBindValue(QString(kCrashed));
    query.addBindValue(QString(kHung));
    if (d->exec(query)) {
        while (query.next()) {
            result.append(
                BlockedCandidate{query.value(0).toString(), query.value(1).toString(), query.value(2).toString()});
        }
    }
    return result;
}

QList<BlockedCandidate> PluginScanner::incomplete() const
{
    QList<BlockedCandidate> result;
    if (!d->open) {
        return result;
    }
    QSqlQuery query = d->prepare(u"SELECT format, path FROM candidates WHERE status = ? ORDER BY format, path"_s);
    query.addBindValue(QString(kIncomplete));
    if (d->exec(query)) {
        while (query.next()) {
            result.append(BlockedCandidate{query.value(0).toString(), query.value(1).toString(), QString(kIncomplete)});
        }
    }
    return result;
}

std::optional<plugin::PluginDescription> PluginScanner::describe(const QString &format, const QString &id) const
{
    if (!d->open) {
        return std::nullopt;
    }
    QSqlQuery query = d->prepare(
        u"SELECT name, vendor, category, audio_inputs, audio_outputs, params, candidate FROM plugins "
        "WHERE format = ? AND id = ?"_s);
    query.addBindValue(format);
    query.addBindValue(id);
    if (!d->exec(query) || !query.next()) {
        return std::nullopt;
    }
    plugin::PluginDescription description;
    description.format = format.toStdString();
    description.id = id.toStdString();
    description.name = query.value(0).toString().toStdString();
    description.vendor = query.value(1).toString().toStdString();
    description.category = query.value(2).toString().toStdString();
    description.audioInputs = static_cast<std::uint32_t>(query.value(3).toInt());
    description.audioOutputs = static_cast<std::uint32_t>(query.value(4).toInt());
    description.params = paramsFromJson(QJsonDocument::fromJson(query.value(5).toString().toUtf8()).array());
    description.parameters = static_cast<std::uint32_t>(description.params.size());
    description.location = query.value(6).toString().toStdString();
    return description;
}

QJsonArray PluginScanner::paramsJson(const QString &format, const QString &id) const
{
    if (!d->open) {
        return {};
    }
    QSqlQuery query = d->prepare(u"SELECT params FROM plugins WHERE format = ? AND id = ?"_s);
    query.addBindValue(format);
    query.addBindValue(id);
    if (!d->exec(query) || !query.next()) {
        return {};
    }
    return QJsonDocument::fromJson(query.value(0).toString().toUtf8()).array();
}

int PluginScanner::lastScanDescribed() const
{
    return d->described;
}

} // namespace rb::core
