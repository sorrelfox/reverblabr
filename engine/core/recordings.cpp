// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/recordings.h"

#include "blocks/recorder.h"
#include "dsp/block.h"
#include "record/repair.h"

#include <QDir>
#include <QLoggingCategory>
#include <QSet>
#include <QStandardPaths>

#include <string>

using namespace Qt::StringLiterals;

namespace rb::core
{

namespace
{

Q_LOGGING_CATEGORY(RB_RECORD, "reverblabr.record")

[[nodiscard]] std::filesystem::path toPath(const QString &path)
{
    return std::filesystem::path{path.toStdString()};
}

} // namespace

QString defaultRecordingFolder()
{
    QString music = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    if (music.isEmpty()) {
        music = QDir::homePath();
    }
    return QDir(music).filePath(u"Reverblabr"_s);
}

QString recordingJournalFolder()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation))
        .filePath(u"reverblabr/recordings"_s);
}

record::RecordingSettings
recordingSettings(const dsp::Block &block, const QString &sessionName, const QString &blockName)
{
    record::RecordingSettings settings;
    settings.format = record::fileFormatFromId(block.param("format").text()).value_or(record::FileFormat::Wav);
    settings.sampleFormat =
        record::sampleFormatFromId(block.param("sampleFormat").text()).value_or(record::SampleFormat::Int24);

    QString folder = QString::fromStdString(block.param("folder").text()).trimmed();
    if (folder.isEmpty()) {
        folder = defaultRecordingFolder();
    } else if (folder == "~"_L1 || folder.startsWith("~/"_L1)) {
        folder = QDir::homePath() + folder.mid(1);
    }
    settings.folder = toPath(folder);

    std::string pattern = block.param("fileName").text();
    if (QString::fromStdString(pattern).trimmed().isEmpty()) {
        pattern = std::string{blocks::Recorder::kDefaultFileName};
    }
    settings.fileNamePattern = pattern;
    settings.sessionName = sessionName.toStdString();
    settings.blockName = blockName.toStdString();
    // "session", or a number of frames a second.
    bool isNumber = false;
    const unsigned rate = QString::fromStdString(block.param("sampleRate").text()).toUInt(&isNumber);
    settings.fileRate = isNumber ? rate : 0;

    // File limits, each only when it is switched on.
    const auto on = [&block](const char *id) { return block.param(id).asBool().value_or(false); };
    const auto number = [&block](const char *id) { return block.param(id).asNumber().value_or(0.0); };
    if (on("splitEnabled")) {
        const std::string unit = block.param("splitUnit").text();
        const double every = number("splitEvery");
        if (unit == blocks::Recorder::kUnitMegabytes) {
            settings.limits.splitEveryBytes = static_cast<std::uint64_t>(every * 1024.0 * 1024.0);
        } else {
            settings.limits.splitEverySeconds = every * (unit == blocks::Recorder::kUnitHours ? 3600.0 : 60.0);
        }
    }
    if (on("endEnabled")) {
        settings.limits.endAfterSeconds =
            number("endAfter") * (block.param("endUnit").text() == blocks::Recorder::kUnitMinutes ? 60.0 : 3600.0);
    }
    if (on("silenceEnabled")) {
        const std::string action = block.param("silenceAction").text();
        settings.limits.silence = action == blocks::Recorder::kSilenceStop ? record::FileLimits::Silence::Stop
            : action == blocks::Recorder::kSilenceSplit                    ? record::FileLimits::Silence::Split
                                                                           : record::FileLimits::Silence::Remove;
        settings.limits.silenceSeconds = number("silenceSeconds");
        settings.limits.silenceThresholdDb = static_cast<float>(number("silenceThreshold"));
    }

    // The format options.
    settings.bitrateKbps = static_cast<std::uint32_t>(block.param("bitrate").asNumber().value_or(256.0));
    settings.variableBitrate = block.param("bitrateMode").text() != blocks::Recorder::kBitrateConstant;
    settings.mono = block.param("channels").text() == blocks::Recorder::kChannelsMono;

    // Tags, as patterns: the writer fills the tokens in for each file.
    settings.tags.title = block.param("tagTitle").text();
    settings.tags.artist = block.param("tagArtist").text();
    settings.tags.album = block.param("tagAlbum").text();
    settings.tags.genre = block.param("tagGenre").text();
    settings.tags.year = block.param("tagYear").text();
    settings.tags.track = block.param("tagTrack").text();
    settings.tags.trackCount = block.param("tagTrackCount").text();
    settings.tags.disc = block.param("tagDisc").text();
    settings.tags.discCount = block.param("tagDiscCount").text();
    settings.tags.comment = block.param("tagComment").text();
    return settings;
}

std::uint32_t recordingGapLanes(const Session &session,
                                const QString &recorderId,
                                const QStringList &inputLanes,
                                const std::function<bool(const QString &blockId)> &leavesGaps)
{
    // Everything upstream of the Recorder, and which of it has nothing
    // upstream of its own.
    QSet<QString> seen{recorderId};
    QStringList waiting{recorderId};
    QStringList sources;
    while (!waiting.isEmpty()) {
        const QString block = waiting.takeLast();
        bool fed = false;
        for (const Wire &wire : session.wires) {
            if (wire.toBlock != block) {
                continue;
            }
            fed = true;
            if (!seen.contains(wire.fromBlock)) {
                seen.insert(wire.fromBlock);
                waiting.append(wire.fromBlock);
            }
        }
        if (!fed && block != recorderId) {
            sources.append(block);
        }
    }

    std::uint32_t lanes = 0;
    for (const QString &source : std::as_const(sources)) {
        const qsizetype lane = inputLanes.indexOf(source);
        if (!leavesGaps(source) || lane < 0 || lane >= 32) {
            return 0;
        }
        lanes |= 1U << static_cast<std::uint32_t>(lane);
    }
    return lanes;
}

UnavailableChoices recordingUnavailableChoices(const record::EncoderProbe &probe)
{
    std::map<std::string, std::string> formats;
    for (const record::FileFormat format : {record::FileFormat::AacM4a, record::FileFormat::AlacM4a}) {
        if (const std::optional<std::string_view> reason = record::formatUnavailable(format, probe)) {
            formats.emplace(std::string{record::fileFormatId(format)}, std::string{*reason});
        }
    }
    UnavailableChoices unavailable;
    if (!formats.empty()) {
        unavailable.emplace(std::pair{std::string{blocks::Recorder::kTypeId}, std::string{"format"}}, formats);
    }
    return unavailable;
}

int repairInterruptedRecordings(const QString &journalFolder)
{
    int repaired = 0;
    for (const record::RepairReport &report : record::repairOrphans(toPath(journalFolder))) {
        const QString file = QString::fromStdString(report.file.string());
        if (report.result) {
            ++repaired;
            qCInfo(RB_RECORD) << "repaired a recording a crash cut off:" << file << "now holds" << report.result->frames
                              << "frames";
        } else {
            qCWarning(RB_RECORD) << "could not repair the recording" << file << ":"
                                 << QString::fromUtf8(record::repairErrorName(report.result.error()));
        }
    }
    return repaired;
}

} // namespace rb::core
