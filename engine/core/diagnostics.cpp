// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/diagnostics.h"

#include "core/logring.h"
#include "record/audiofile.h"

#include <QDateTime>
#include <QString>

#include <map>

using namespace Qt::StringLiterals;

namespace rb::core
{

QJsonArray xrunHistoryJson(const std::vector<XrunEvent> &events)
{
    QJsonArray json;
    for (const XrunEvent &event : events) {
        json.append(QJsonObject{
            {"at"_L1, QDateTime::fromMSecsSinceEpoch(event.atMs).toString(Qt::ISODateWithMs)},
            {"total"_L1, static_cast<qint64>(event.total)},
            {"added"_L1, static_cast<qint64>(event.added)},
        });
    }
    return json;
}

QJsonArray recordingFormatsJson(const record::EncoderProbe &probe)
{
    QJsonArray json;
    for (const record::FileFormat format : {record::FileFormat::Wav,
                                            record::FileFormat::Aiff,
                                            record::FileFormat::Flac,
                                            record::FileFormat::Mp3,
                                            record::FileFormat::Opus,
                                            record::FileFormat::Vorbis,
                                            record::FileFormat::AacM4a,
                                            record::FileFormat::AlacM4a}) {
        const std::string_view id = record::fileFormatId(format);
        QJsonObject entry{
            {"id"_L1, QString::fromUtf8(id.data(), static_cast<qsizetype>(id.size()))},
        };
        if (const std::optional<std::string_view> reason = record::formatUnavailable(format, probe)) {
            entry.insert("available"_L1, false);
            entry.insert("reason"_L1, QString::fromUtf8(reason->data(), static_cast<qsizetype>(reason->size())));
        } else {
            entry.insert("available"_L1, true);
        }
        json.append(entry);
    }
    return json;
}

QJsonObject pluginScanJson(const QList<InstalledPlugin> &plugins,
                           const QList<BlockedCandidate> &blocked,
                           bool scanning,
                           const QList<BlockedCandidate> &incomplete)
{
    // By format name, so that the counts come out in the same order every
    // time and two bundles can be compared line by line.
    std::map<QString, qint64> counts;
    for (const InstalledPlugin &plugin : plugins) {
        ++counts[plugin.format];
    }
    QJsonObject formats;
    for (const auto &[format, count] : counts) {
        formats.insert(format, count);
    }
    QJsonArray blockedJson;
    for (const BlockedCandidate &candidate : blocked) {
        blockedJson.append(QJsonObject{
            {"format"_L1, candidate.format},
            {"path"_L1, candidate.path},
            {"reason"_L1, candidate.reason},
        });
    }
    QJsonArray incompleteJson;
    for (const BlockedCandidate &candidate : incomplete) {
        incompleteJson.append(QJsonObject{{"format"_L1, candidate.format}, {"path"_L1, candidate.path}});
    }
    return QJsonObject{
        {"scanning"_L1, scanning},
        {"formats"_L1, formats},
        {"total"_L1, static_cast<qint64>(plugins.size())},
        {"blocked"_L1, blockedJson},
        {"incomplete"_L1, incompleteJson},
    };
}

QJsonArray logJson()
{
    return QJsonArray::fromStringList(LogRing::lines());
}

} // namespace rb::core
