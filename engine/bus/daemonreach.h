// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QDBusConnection>
#include <QString>

#include <chrono>
#include <functional>

/// Which engine a window runs on: the one in reverblabrd when it can
/// be reached, and otherwise its own, with a word about why.
namespace rb::bus
{

/// Whether a window can have its engine in the daemon, and if not, why.
enum class DaemonReach {
    /// The daemon has the name: it was running, or the bus has just started it.
    Reached,
    /// There is no session bus to reach it by.
    NoSessionBus,
    /// The bus knows no way to start it: no activation file names it, which
    /// is where a build that has not been installed stands.
    NotInstalled,
    /// The bus tried to start it, and it did not take the name.
    Refused,
};

struct DaemonReachResult {
    DaemonReach reach = DaemonReach::NoSessionBus;
    /// What the bus said, for the log. Empty when there is nothing to add.
    QString detail;
};

/// How long reachDaemon() lets the bus take to start the daemon. **Generous
/// on purpose**: a daemon connects to the audio system and opens again what
/// was open before it takes the name, and a window that gave up too early
/// would run its own engine beside one that was only slow.
inline constexpr std::chrono::milliseconds kActivationTimeout{10000};

/// Asks \a bus whether the daemon is there, and has the bus start it if it
/// is not. **Blocks** for as long as the start takes, up to \a timeout: it
/// is asked once, before there is a window.
[[nodiscard]] DaemonReachResult reachDaemon(const QDBusConnection &bus,
                                            std::chrono::milliseconds timeout = kActivationTimeout);

/// What the command line asked for.
struct EngineRequest {
    bool mock = false;
    bool inProcess = false;
    bool daemon = false;
};

enum class EngineKind {
    Daemon,
    InProcess,
    Mock,
};

struct EngineChoice {
    EngineKind kind = EngineKind::InProcess;
    /// **Why the window runs its own engine when nobody asked it to**: the
    /// reason argument of `engine.daemonUnreachable`. Empty when there is
    /// nothing to say, which is whenever the choice is what was asked for.
    QString fallbackReason;
};

/// The engine a window runs on. **--mock**, then **--in-process**, then
/// **--daemon** decide when given, and \a reach is not asked: an explicit
/// --daemon keeps to the daemon however the bus answers, which is what trying
/// it on a private bus needs. With none of them, the daemon when \a reach
/// says it is there, and otherwise the window's own engine with the reason.
[[nodiscard]] EngineChoice chooseEngine(const EngineRequest &request, const std::function<DaemonReachResult()> &reach);

/// The word `engine.daemonUnreachable` carries for a reach that failed:
/// `noSessionBus`, `daemonNotInstalled` or `daemonRefused`. Empty for Reached.
[[nodiscard]] QString reasonOf(DaemonReach reach);

} // namespace rb::bus
