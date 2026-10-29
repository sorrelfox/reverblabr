// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/blockcatalogue.h"
#include "stream/broadcaststream.h"
#include "stream/streamencoder.h"

#include <QJsonObject>
#include <QString>

namespace rb::dsp
{
class Block;
}

namespace rb::core
{

/// What \a block's stream is to be made from, read off its settings now:
/// everything but the password. A now-playing file starting with `~/` is in
/// the home folder, and is looked at only while the block's title comes
/// from a file.
[[nodiscard]] stream::BroadcastSettings broadcastSettings(const dsp::Block &block, const QString &sessionName);

/// **Where a Broadcast block's password is kept** in the secret store:
/// under the session and the block, so that two sessions streaming to two
/// servers keep two passwords, and a session file that is copied or sent
/// to somebody carries none.
[[nodiscard]] QString broadcastSecretKey(const QString &sessionId, const QString &blockId);

/// A stream's status as the `BroadcastStatusChanged` signal carries it:
/// `{state, error?, connectedSince, bytesSent, kbps, reconnects, retryIn,
/// title, sampleRate}`, the times in milliseconds.
[[nodiscard]] QJsonObject broadcastStatusJson(const stream::BroadcastStatus &status);

/// **The Broadcast block's formats this system cannot make**, for the
/// catalogue: the ones whose encoders \a probe did not find, with the
/// reason "encoderMissing", as the Recorder's are.
[[nodiscard]] UnavailableChoices
broadcastUnavailableChoices(const stream::StreamEncoderProbe &probe = stream::probeStreamEncoders());

} // namespace rb::core
