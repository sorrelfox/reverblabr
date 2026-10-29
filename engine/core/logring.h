// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QString>
#include <QStringList>
#include <QtLogging>

#include <cstddef>

namespace rb::core
{

/// **The last lines this process logged**, for the diagnostics
/// bundle: a bug report is only as good as what was said just before it went
/// wrong, and by then the journal is somewhere the person reporting it does
/// not know to look.
///
/// install() puts a Qt message handler in front of the one there was, which
/// still gets every message: the ring only keeps a copy. It is for this
/// process as a whole, since a message handler is. The audio thread never
/// comes here; what it says goes through rt::RealtimeLogger and is drained
/// into Qt's logging on an ordinary thread first.
class LogRing
{
public:
    /// How many lines a ring keeps unless it is told otherwise.
    static constexpr std::size_t kDefaultCapacity = 500;

    /// Starts keeping the last \a capacity lines. A second call changes the
    /// capacity and keeps the handler that is already there.
    static void install(std::size_t capacity = kDefaultCapacity);
    /// Whether install() has been called in this process.
    [[nodiscard]] static bool isInstalled();

    /// The lines kept, oldest first: "2026-09-28T23:10:00.123 warning
    /// category: message".
    [[nodiscard]] static QStringList lines();

    /// How a line is written, for install() and for a test.
    [[nodiscard]] static QString formatLine(QtMsgType type, const QMessageLogContext &context, const QString &message);

    /// Puts the handler back as it was and forgets every line. A test's.
    static void uninstallForTesting();
};

} // namespace rb::core
