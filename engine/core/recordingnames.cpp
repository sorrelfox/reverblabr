// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/recordingnames.h"

#include "blocks/recorder.h"
#include "core/recordings.h"
#include "record/audiofile.h"
#include "record/filename.h"

#include <chrono>
#include <string>

namespace rb::core
{

QString recordingFileNamePreview(const QString &pattern,
                                 const QString &sessionName,
                                 const QString &blockName,
                                 const QString &formatId,
                                 const QDateTime &when)
{
    record::FileNameValues values;
    values.session = sessionName.toStdString();
    values.block = blockName.toStdString();
    const QDate date = when.date();
    const QTime time = when.time();
    values.when = std::chrono::local_days{std::chrono::year{date.year()} / date.month() / date.day()}
        + std::chrono::hours{time.hour()} + std::chrono::minutes{time.minute()} + std::chrono::seconds{time.second()};
    values.counter = 1;
    // An empty pattern is the default one, as the engine reads it.
    const std::string used =
        pattern.trimmed().isEmpty() ? std::string{blocks::Recorder::kDefaultFileName} : pattern.toStdString();
    const record::FileFormat format =
        record::fileFormatFromId(formatId.toStdString()).value_or(record::FileFormat::Mp3);
    const std::string name = record::sanitizeFileName(record::renderFileName(used, values)) + "."
        + std::string{record::fileExtension(format)};
    return QString::fromStdString(name);
}

QString defaultRecordingFolderPath()
{
    return defaultRecordingFolder();
}

} // namespace rb::core
