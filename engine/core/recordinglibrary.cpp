// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/recordinglibrary.h"

#include "record/summary.h"
#include "record/tags.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QThreadPool>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <optional>

using namespace Qt::StringLiterals;

namespace rb::core
{

namespace
{

Q_LOGGING_CATEGORY(lcLibrary, "reverblabr.library")

/// The index's layout. Written into the file, so that a later version can
/// tell an index it has to bring up to date from one it made itself.
constexpr int kSchemaVersion = 1;

/// A peak as a byte, 0 to 255 for silence to full scale. A waveform drawn
/// a few dozen pixels high has no use for more.
[[nodiscard]] QByteArray packPeaks(const std::vector<float> &peaks)
{
    QByteArray packed(static_cast<qsizetype>(peaks.size()), '\0');
    for (std::size_t i = 0; i < peaks.size(); ++i) {
        const float clamped = std::clamp(peaks[i], 0.0f, 1.0f);
        packed[static_cast<qsizetype>(i)] =
            static_cast<char>(static_cast<unsigned char>(std::lround(clamped * 255.0f)));
    }
    return packed;
}

/// \a packed brought down to \a count peaks, each the loudest of its share.
[[nodiscard]] QJsonArray unpackPeaks(const QByteArray &packed, int count)
{
    QJsonArray peaks;
    if (packed.isEmpty() || count <= 0) {
        return peaks;
    }
    const qsizetype size = packed.size();
    for (int bucket = 0; bucket < count; ++bucket) {
        const qsizetype from = bucket * size / count;
        const qsizetype to = std::max(from + 1, (bucket + 1) * size / count);
        int loudest = 0;
        for (qsizetype i = from; i < to && i < size; ++i) {
            loudest = std::max(loudest, static_cast<int>(static_cast<unsigned char>(packed[i])));
        }
        peaks.append(std::round(loudest / 255.0 * 1000.0) / 1000.0);
    }
    return peaks;
}

} // namespace

std::optional<record::FileFormat> formatOfPath(const QString &path)
{
    const QString extension = QFileInfo(path).suffix().toLower();
    if (extension == "wav"_L1) {
        return record::FileFormat::Wav;
    }
    if (extension == "aiff"_L1 || extension == "aif"_L1) {
        return record::FileFormat::Aiff;
    }
    if (extension == "flac"_L1) {
        return record::FileFormat::Flac;
    }
    if (extension == "mp3"_L1) {
        return record::FileFormat::Mp3;
    }
    if (extension == "opus"_L1) {
        return record::FileFormat::Opus;
    }
    if (extension == "ogg"_L1 || extension == "oga"_L1) {
        return record::FileFormat::Vorbis;
    }
    if (extension == "m4a"_L1) {
        // AAC and Apple Lossless are tagged alike, as an MP4.
        return record::FileFormat::AacM4a;
    }
    return std::nullopt;
}

QJsonObject tagsToJson(const record::Tags &tags)
{
    const auto text = [](const std::string &value) { return QString::fromStdString(value); };
    return QJsonObject{
        {u"title"_s, text(tags.title)},
        {u"artist"_s, text(tags.artist)},
        {u"album"_s, text(tags.album)},
        {u"genre"_s, text(tags.genre)},
        {u"year"_s, text(tags.year)},
        {u"track"_s, text(tags.track)},
        {u"trackCount"_s, text(tags.trackCount)},
        {u"disc"_s, text(tags.disc)},
        {u"discCount"_s, text(tags.discCount)},
        {u"comment"_s, text(tags.comment)},
    };
}

record::Tags tagsFromJson(const QJsonObject &json)
{
    const auto text = [&json](const char *key) {
        return json.value(QLatin1StringView{key}).toString().trimmed().toStdString();
    };
    record::Tags tags;
    tags.title = text("title");
    tags.artist = text("artist");
    tags.album = text("album");
    tags.genre = text("genre");
    tags.year = text("year");
    tags.track = text("track");
    tags.trackCount = text("trackCount");
    tags.disc = text("disc");
    tags.discCount = text("discCount");
    tags.comment = text("comment");
    return tags;
}

QString defaultLibraryPath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(u"library.db"_s);
}

class RecordingLibrary::Private
{
public:
    explicit Private(RecordingLibrary *owner)
        : q(owner)
    {
        // One file at a time: reading is disk-bound, and two at once would
        // only take longer each.
        pool.setMaxThreadCount(1);
    }

    RecordingLibrary *q;
    QString connectionName;
    bool open = false;
    /// The files a Recorder is writing now. A row marked in progress that
    /// is not one of these was left by a crash.
    QSet<QString> live;
    QThreadPool pool;

    /// One row of a SELECT of every column, as recordings() gives it.
    [[nodiscard]] static QJsonObject rowOf(const QSqlQuery &query, int peakCount);

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
            qCWarning(lcLibrary) << "the recordings index refused" << query.lastQuery() << query.lastError().text();
            return false;
        }
        return true;
    }

    [[nodiscard]] QString sessionOf(const QString &path) const
    {
        QSqlQuery query = prepare(u"SELECT session_id FROM recordings WHERE path = ?"_s);
        query.addBindValue(path);
        return exec(query) && query.next() ? query.value(0).toString() : QString{};
    }

    /// Reads \a path through on the worker and fills its row in when that
    /// is done. \a frames and \a sampleRate are what the Recorder counted,
    /// kept when the file cannot be read back (a format libsndfile does not
    /// decode).
    void summarise(const QString &path, quint64 frames, quint32 sampleRate)
    {
        // **Answered on the library's own thread**, where its connection to
        // the index lives. The library waits for the pool before it goes, so
        // it is there to be posted to; an answer still queued when it goes
        // is dropped with it.
        RecordingLibrary *self = q;
        pool.start([self, path, frames, sampleRate] {
            std::optional<record::FileSummary> summary =
                record::summarise(std::filesystem::path{path.toStdString()}, RecordingLibrary::kPeakCount);
            // Its tags too, which the Recorder wrote as it finished and
            // the Files tab and the Recording Inspector show.
            std::optional<record::Tags> tags;
            if (const std::optional<record::FileFormat> format = formatOfPath(path)) {
                tags = record::readTags(std::filesystem::path{path.toStdString()}, *format);
            }
            QMetaObject::invokeMethod(
                self,
                [self, path, frames, sampleRate, summary = std::move(summary), tags = std::move(tags)] {
                    self->d->finish(path, frames, sampleRate, summary, tags);
                },
                Qt::QueuedConnection);
        });
    }

    void finish(const QString &path,
                quint64 frames,
                quint32 sampleRate,
                const std::optional<record::FileSummary> &summary,
                const std::optional<record::Tags> &tags)
    {
        const QFileInfo info(path);
        if (!info.exists()) {
            forget(path);
            return;
        }
        QSqlQuery query = prepare(
            u"UPDATE recordings SET in_progress = 0, frames = ?, sample_rate = ?, channels = ?, size_bytes = ?, "
            "peaks = ?, tags = ? WHERE path = ?"_s);
        query.addBindValue(static_cast<qlonglong>(summary ? summary->frames : frames));
        query.addBindValue(summary ? summary->sampleRate : sampleRate);
        query.addBindValue(summary ? summary->channels : 0U);
        query.addBindValue(info.size());
        query.addBindValue(summary ? packPeaks(summary->peaks) : QByteArray{});
        query.addBindValue(QJsonDocument(tags ? tagsToJson(*tags) : QJsonObject{}).toJson(QJsonDocument::Compact));
        query.addBindValue(path);
        if (exec(query) && query.numRowsAffected() > 0) {
            Q_EMIT q->changed(sessionOf(path));
        }
    }

    void forget(const QString &path)
    {
        const QString session = sessionOf(path);
        QSqlQuery query = prepare(u"DELETE FROM recordings WHERE path = ?"_s);
        query.addBindValue(path);
        if (exec(query) && query.numRowsAffected() > 0) {
            Q_EMIT q->changed(session);
        }
    }
};

RecordingLibrary::RecordingLibrary(const QString &path, QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>(this))
{
    d->connectionName = u"reverblabr-library-%1"_s.arg(reinterpret_cast<quintptr>(this), 0, 16);
    if (!QSqlDatabase::isDriverAvailable(u"QSQLITE"_s)) {
        qCWarning(lcLibrary) << "Qt's SQLite driver is missing; recordings will not be listed";
        return;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSqlDatabase db = QSqlDatabase::addDatabase(u"QSQLITE"_s, d->connectionName);
    db.setDatabaseName(path);
    // **Several engines may share one index** — a test run's parallel
    // processes, a window and a daemon — so a write waits its turn rather
    // than failing.
    db.setConnectOptions(u"QSQLITE_BUSY_TIMEOUT=5000"_s);
    if (!db.open()) {
        qCWarning(lcLibrary) << "the recordings index at" << path << "could not be opened:" << db.lastError().text();
        return;
    }
    QSqlQuery query(db);
    const bool made = query.exec(
                          u"CREATE TABLE IF NOT EXISTS recordings ("
                          "path TEXT PRIMARY KEY, session_id TEXT NOT NULL, block_id TEXT, "
                          "started_ms INTEGER, in_progress INTEGER NOT NULL DEFAULT 1, "
                          "frames INTEGER NOT NULL DEFAULT 0, sample_rate INTEGER NOT NULL DEFAULT 0, "
                          "channels INTEGER NOT NULL DEFAULT 0, size_bytes INTEGER NOT NULL DEFAULT 0, "
                          "format TEXT, tags TEXT NOT NULL DEFAULT '{}', peaks BLOB)"_s)
        && query.exec(u"CREATE INDEX IF NOT EXISTS recordings_by_session ON recordings (session_id)"_s)
        && query.exec(u"PRAGMA user_version = %1"_s.arg(kSchemaVersion));
    if (!made) {
        qCWarning(lcLibrary) << "the recordings index could not be set up:" << query.lastError().text();
        return;
    }
    d->open = true;
}

RecordingLibrary::~RecordingLibrary()
{
    d->pool.waitForDone();
    {
        QSqlDatabase db = QSqlDatabase::database(d->connectionName, false);
        db.close();
    }
    QSqlDatabase::removeDatabase(d->connectionName);
}

bool RecordingLibrary::isOpen() const
{
    return d->open;
}

void RecordingLibrary::fileStarted(const QString &sessionId, const QString &blockId, const QString &path)
{
    d->live.insert(path);
    if (!d->open) {
        return;
    }
    QSqlQuery query = d->prepare(
        u"INSERT OR REPLACE INTO recordings (path, session_id, block_id, started_ms, in_progress, format) "
        "VALUES (?, ?, ?, ?, 1, ?)"_s);
    query.addBindValue(path);
    query.addBindValue(sessionId);
    query.addBindValue(blockId);
    query.addBindValue(QDateTime::currentMSecsSinceEpoch());
    query.addBindValue(QFileInfo(path).suffix().toLower());
    if (d->exec(query)) {
        Q_EMIT changed(sessionId);
    }
}

void RecordingLibrary::fileFinished(const QString &sessionId,
                                    const QString &blockId,
                                    const QString &path,
                                    quint64 frames,
                                    quint32 sampleRate)
{
    d->live.remove(path);
    if (!d->open) {
        return;
    }
    // A file the index never saw start — the index was made after it, or
    // could not be written then — is taken in as it finishes.
    if (d->sessionOf(path).isEmpty()) {
        fileStarted(sessionId, blockId, path);
        d->live.remove(path);
    }
    d->summarise(path, frames, sampleRate);
}

void RecordingLibrary::rescan()
{
    if (!d->open) {
        return;
    }
    QSqlQuery query = d->prepare(u"SELECT path, in_progress, size_bytes FROM recordings"_s);
    if (!d->exec(query)) {
        return;
    }
    QStringList gone;
    QStringList unfinished;
    QStringList resized;
    while (query.next()) {
        const QString path = query.value(0).toString();
        const QFileInfo info(path);
        if (!info.exists()) {
            gone.append(path);
        } else if (query.value(1).toBool() && !d->live.contains(path)) {
            unfinished.append(path);
        } else if (!query.value(1).toBool() && info.size() != query.value(2).toLongLong()) {
            // Changed since: cut in an editor, tagged, converted in place.
            resized.append(path);
        }
    }
    for (const QString &path : std::as_const(gone)) {
        d->forget(path);
    }
    for (const QString &path : std::as_const(unfinished) + resized) {
        d->summarise(path, 0, 0);
    }
}

QJsonArray RecordingLibrary::recordings(const QString &sessionId) const
{
    QJsonArray rows;
    if (!d->open) {
        return rows;
    }
    QSqlQuery query = d->prepare(
        u"SELECT path, session_id, block_id, started_ms, in_progress, frames, sample_rate, channels, size_bytes, "
        "format, tags, peaks FROM recordings WHERE session_id = ? ORDER BY started_ms DESC, path"_s);
    query.addBindValue(sessionId);
    if (!d->exec(query)) {
        return rows;
    }
    while (query.next()) {
        rows.append(d->rowOf(query, kListPeakCount));
    }
    return rows;
}

int RecordingLibrary::recordingCount(const QString &sessionId) const
{
    if (!d->open) {
        return 0;
    }
    QSqlQuery query = d->prepare(u"SELECT COUNT(*) FROM recordings WHERE session_id = ?"_s);
    query.addBindValue(sessionId);
    if (!d->exec(query) || !query.next()) {
        return 0;
    }
    return query.value(0).toInt();
}

std::optional<QJsonObject> RecordingLibrary::recording(const QString &path, int peakCount) const
{
    if (!d->open) {
        return std::nullopt;
    }
    QSqlQuery query = d->prepare(
        u"SELECT path, session_id, block_id, started_ms, in_progress, frames, sample_rate, channels, size_bytes, "
        "format, tags, peaks FROM recordings WHERE path = ?"_s);
    query.addBindValue(path);
    if (!d->exec(query) || !query.next()) {
        return std::nullopt;
    }
    return d->rowOf(query, peakCount);
}

QJsonObject RecordingLibrary::Private::rowOf(const QSqlQuery &query, int peakCount)
{
    {
        const QString path = query.value(0).toString();
        const qlonglong frames = query.value(5).toLongLong();
        const int rate = query.value(6).toInt();
        QJsonObject row;
        row.insert(u"path"_s, path);
        row.insert(u"name"_s, QFileInfo(path).fileName());
        row.insert(u"sessionId"_s, query.value(1).toString());
        row.insert(u"blockId"_s, query.value(2).toString());
        row.insert(
            u"started"_s,
            QDateTime::fromMSecsSinceEpoch(query.value(3).toLongLong(), QTimeZone::UTC).toString(Qt::ISODateWithMs));
        row.insert(u"inProgress"_s, query.value(4).toBool());
        row.insert(u"seconds"_s, rate > 0 ? static_cast<double>(frames) / rate : 0.0);
        row.insert(u"sampleRate"_s, rate);
        row.insert(u"channels"_s, query.value(7).toInt());
        row.insert(u"sizeBytes"_s, static_cast<double>(query.value(8).toLongLong()));
        row.insert(u"format"_s, query.value(9).toString());
        row.insert(u"tags"_s, QJsonDocument::fromJson(query.value(10).toByteArray()).object());
        row.insert(u"peaks"_s, unpackPeaks(query.value(11).toByteArray(), peakCount));
        return row;
    }
}

std::optional<QJsonObject> RecordingLibrary::details(const QString &path) const
{
    std::optional<QJsonObject> row = recording(path, kPeakCount);
    if (!row) {
        return std::nullopt;
    }
    if (const std::optional<record::FileFormat> format = formatOfPath(path)) {
        if (const std::optional<record::Tags> tags =
                record::readTags(std::filesystem::path{path.toStdString()}, *format)) {
            row->insert(u"tags"_s, tagsToJson(*tags));
        }
    }
    return row;
}

std::expected<QString, QString> RecordingLibrary::rename(const QString &path, const QString &name)
{
    const QFileInfo from(path);
    const QString base = name.trimmed();
    // **A name, not a path**: the file stays where it is and keeps its
    // extension, which says what it is.
    if (base.isEmpty() || base == "."_L1 || base == ".."_L1 || base.contains(u'/') || base.contains(QChar(0))
        || base.size() > 230) {
        return std::unexpected(u"invalidName"_s);
    }
    if (!from.exists()) {
        return std::unexpected(u"notFound"_s);
    }
    const QString suffix = from.suffix();
    const QString target = from.dir().filePath(suffix.isEmpty() ? base : base + u'.' + suffix);
    if (target == path) {
        return target;
    }
    if (QFileInfo::exists(target)) {
        return std::unexpected(u"exists"_s);
    }
    if (!QFile::rename(path, target)) {
        return std::unexpected(u"failed"_s);
    }
    if (d->open) {
        QSqlQuery query = d->prepare(u"UPDATE recordings SET path = ?, format = ? WHERE path = ?"_s);
        query.addBindValue(target);
        query.addBindValue(QFileInfo(target).suffix().toLower());
        query.addBindValue(path);
        if (d->exec(query) && query.numRowsAffected() > 0) {
            Q_EMIT changed(d->sessionOf(target));
        }
    }
    return target;
}

bool RecordingLibrary::saveTags(const QString &path, const QJsonObject &tags)
{
    const std::optional<record::FileFormat> format = formatOfPath(path);
    if (!format || !QFileInfo::exists(path)) {
        return false;
    }
    const std::filesystem::path file{path.toStdString()};
    if (!record::writeTags(file, *format, tagsFromJson(tags))) {
        return false;
    }
    // **What the file says now**, read back, rather than what was asked
    // for: a field a format has no room for is not pretended to be there.
    const std::optional<record::Tags> written = record::readTags(file, *format);
    if (d->open) {
        QSqlQuery query = d->prepare(u"UPDATE recordings SET tags = ?, size_bytes = ? WHERE path = ?"_s);
        query.addBindValue(
            QJsonDocument(written ? tagsToJson(*written) : QJsonObject{}).toJson(QJsonDocument::Compact));
        query.addBindValue(QFileInfo(path).size());
        query.addBindValue(path);
        if (d->exec(query) && query.numRowsAffected() > 0) {
            Q_EMIT changed(d->sessionOf(path));
        }
    }
    return true;
}

void RecordingLibrary::waitForIdle()
{
    d->pool.waitForDone();
    QCoreApplication::sendPostedEvents(this, QEvent::MetaCall);
}

} // namespace rb::core
