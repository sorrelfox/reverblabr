// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QLatin1StringView>
#include <QString>
#include <QVariantMap>

#include <expected>

namespace rb::helper
{

/// The KAuth helper's id, which is its D-Bus name on the system bus.
inline constexpr QLatin1StringView kHelperId{"io.github.reverblabr.virtualdevices"};

/// The one action it offers: write the all-users virtual devices (ADR-0013).
inline constexpr QLatin1StringView kWriteAction{"io.github.reverblabr.virtualdevices.write"};

/// The reason the arguments themselves were refused: not exactly the two
/// lists below.
inline constexpr QLatin1StringView kBadRequest{"BadRequest"};

/// The reason given when the file no longer holds the list the caller
/// started from: somebody else changed the all-users devices meanwhile, and
/// writing would undo their change.
inline constexpr QLatin1StringView kChangedElsewhere{"ChangedElsewhere"};

/// The reply's data key that carries the reason for a refusal.
inline constexpr QLatin1StringView kReasonKey{"reason"};

/// What the helper does, apart from KAuth, so that a test can run it
/// against a directory of its own.
///
/// \a arguments must hold exactly two lists in the form
/// pw::virtualDevicesToVariant() writes:
/// - `devices`, the whole list the file is to hold afterwards;
/// - `previous`, the list the caller saw and changed. The file must still
///   hold exactly that, or nothing is written (kChangedElsewhere): on a
///   machine with several users, another user's change would otherwise be
///   lost without a word.
///
/// **Only ever one file is written**: `<root>/etc/pipewire/pipewire.conf.d/
/// 90-reverblabr-virtual-devices-system.conf`, with its directory made when
/// missing, or the file removed when the list is empty. Nothing in the
/// arguments can name another; the helper's own caller passes `/` as
/// \a root, and only tests pass anything else.
///
/// The error is the reason to reply with: a pw::VirtualDeviceError's name,
/// kBadRequest or kChangedElsewhere.
[[nodiscard]] std::expected<void, QString> writeSystemDevices(const QVariantMap &arguments, const QString &root);

} // namespace rb::helper
