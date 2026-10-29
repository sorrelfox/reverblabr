// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/timers.h"

#include <QString>

namespace rb::daemon
{

/// The systemd user units a session's timer becomes (core/timers.h): a
/// `.timer` that elapses at the timer's times, and the `.service` it starts,
/// which runs `reverblabr-ctl timer-fire <id>`. Only the text of them;
/// TimerSchedule writes them and tells the service manager.
///
/// **Ours, and marked so**: every file starts with kWrittenBy, and nothing
/// without it is ever rewritten or removed, whatever it is called.
namespace timerunits
{

/// What every unit's file name starts with.
inline constexpr auto kPrefix = "reverblabr-timer-";
/// The first line of every unit written here.
inline constexpr auto kWrittenBy = "# Written by reverblabrd.";

/// `reverblabr-timer-<id>`, the units' name without their suffix.
[[nodiscard]] QString baseName(const QString &timerId);
/// The timer's id from a unit's file name, or empty when the name is not
/// one of ours.
[[nodiscard]] QString timerIdOf(const QString &fileName);

/// The `.timer` unit: OnCalendar= from core::onCalendar(), to the second,
/// and not run late for a time that passed while the machine was off.
[[nodiscard]] QString timerUnit(const core::Timer &timer, const QString &sessionName);
/// The `.service` unit the timer starts: \a ctlPath with `timer-fire` and
/// the timer's id, once.
[[nodiscard]] QString serviceUnit(const core::Timer &timer, const QString &sessionName, const QString &ctlPath);

} // namespace timerunits

} // namespace rb::daemon
