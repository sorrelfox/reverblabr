// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/pluginscanner.h"
#include "core/xrunhistory.h"
#include "record/m4afile.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QStringList>

#include <vector>

namespace rb::core
{

/// The shape of what GetDiagnostics answers, as "version" in it.
/// It is for a person and a bug report, not for a program to act on, so it
/// grows freely; the number says which description in docs/dbus-api.md it
/// follows.
inline constexpr int kDiagnosticsVersion = 1;

/// Each event as { "at": ISO 8601 with milliseconds, "total", "added" }.
[[nodiscard]] QJsonArray xrunHistoryJson(const std::vector<XrunEvent> &events);

/// **Every format a Recorder can write**, and whether it can here: { "id",
/// "available" } and a "reason" for one that cannot. Judged by \a probe;
/// by what this system's FFmpeg can encode by default.
[[nodiscard]] QJsonArray recordingFormatsJson(const record::EncoderProbe &probe = record::probeEncoders());

/// **What the last plugin scan found**, without the plugins themselves
/// (ListPlugins has those): { "scanning", "formats": { format: count },
/// "total", "blocked": [ { "format", "path", "reason" } ], "incomplete":
/// [ { "format", "path" } ] }, the last the files a scan got only part of
/// the way through.
[[nodiscard]] QJsonObject pluginScanJson(const QList<InstalledPlugin> &plugins,
                                         const QList<BlockedCandidate> &blocked,
                                         bool scanning,
                                         const QList<BlockedCandidate> &incomplete = {});

/// This process's log as LogRing keeps it, oldest first, or an empty array
/// when nothing installed one.
[[nodiscard]] QJsonArray logJson();

} // namespace rb::core
