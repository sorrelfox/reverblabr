// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QDateTime>
#include <QString>

namespace rb::core
{

/// **What a Recorder's next file will be called**, for the name
/// editor's live preview: \a pattern with its tokens filled in for
/// \a when, made safe the way the writer makes it safe, with the
/// extension of \a formatId. The counter is the first one, 001; the
/// writer counts on from it past files that are already there.
///
/// The same functions the writer names its files with, so the preview
/// cannot promise a name the file will not have.
[[nodiscard]] QString recordingFileNamePreview(const QString &pattern,
                                               const QString &sessionName,
                                               const QString &blockName,
                                               const QString &formatId,
                                               const QDateTime &when = QDateTime::currentDateTime());

/// Where a Recorder that names no folder writes (core::defaultRecordingFolder),
/// for an interface to show.
[[nodiscard]] QString defaultRecordingFolderPath();

} // namespace rb::core
