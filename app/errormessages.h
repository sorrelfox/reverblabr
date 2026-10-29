// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace rb::app
{

/// The translated message for an error the engine reported as {code, args}.
/// Arguments are taken from args by the names the engine declares for the
/// code. An unknown code gets a generic message that names it.
[[nodiscard]] QString errorMessage(const QString &code, const QVariantMap &args = {});

/// The phrase for a "reason" argument, which is the engine's own name for
/// why something was refused — `NodeRefused`, `blockNotFound`. A reason the
/// table does not know is given back as it came: half a sentence a reader
/// can act on beats a sentence that says nothing.
[[nodiscard]] QString reasonMessage(const QString &reason);

/// True when the message table has an entry for the code.
[[nodiscard]] bool hasErrorMessage(const QString &code);

/// Every code in the message table.
[[nodiscard]] QStringList errorMessageCodes();

} // namespace rb::app
