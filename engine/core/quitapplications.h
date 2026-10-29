// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "pw/pwobjects.h"

#include <QList>
#include <QString>

#include <cstdint>

namespace rb::core
{

/// One application to ask to quit, when a timer set to quit the captured
/// applications ends: a process by its id, or a sandboxed application by its
/// Flatpak id.
struct QuitTarget {
    /// The process, as the server saw it on the client's socket
    /// (`pipewire.sec.pid`), or -1 for a Flatpak.
    qint64 pid = -1;
    /// What PipeWire said the program is, checked against the process before
    /// it is signalled.
    QString binary;
    /// The Flatpak id the portal gave the client
    /// (`pipewire.access.portal.app_id`), when it is a sandboxed one.
    QString flatpakId;

    [[nodiscard]] bool operator==(const QuitTarget &other) const = default;
};

/// What to quit for \a streams, each once, from what **the server** says of
/// each stream's client, never from what the application says of itself:
/// `application.process.id` and `application.id` are the client's to set,
/// and a pid a client chose is a pid anybody's process can be given. A
/// client that went through the portal is a Flatpak, ended by its id; any
/// other is its process. A stream whose client the server says nothing about
/// is left alone.
[[nodiscard]] QList<QuitTarget> quitTargets(const QList<pw::PwNode> &streams, const QList<pw::PwClient> &clients);

/// What quitProcess() did.
enum class QuitResult : std::uint8_t {
    /// Asked to quit, with SIGTERM.
    Signalled,
    /// Gone before it could be asked.
    Gone,
    /// Another user's process.
    NotOurs,
    /// Not running \a binary any more: the pid was reused, or it never was.
    WrongProgram,
};

/// Asks \a pid to quit, as logging out would, **only while it is the process
/// it was**: this user's, and running \a binary (its executable's name, or
/// its command name). Through a pidfd, so the checks and the signal are
/// about one process: a pid freed and given to another between them gets
/// nothing.
[[nodiscard]] QuitResult quitProcess(qint64 pid, const QString &binary);

} // namespace rb::core
