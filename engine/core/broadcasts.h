// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/blockcatalogue.h"
#include "mpris/player.h"
#include "stream/broadcaststream.h"
#include "stream/outputstream.h"
#include "stream/streamencoder.h"

#include <QJsonObject>
#include <QString>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>

namespace rb::dsp
{
class Block;
}

namespace rb::rt
{
class AudioTap;
}

namespace rb::core
{

/// What \a block's stream is to be made from, read off its settings now:
/// everything but the password. A now-playing file starting with `~/` is in
/// the home folder, and is looked at only while the block's title comes
/// from a file.
[[nodiscard]] stream::BroadcastSettings broadcastSettings(const dsp::Block &block, const QString &sessionName);

/// **The now-playing file of a block whose title follows what is
/// playing** (Broadcast, Live Stream): empty unless its title comes from a
/// file. One starting with `~/` is in the home folder.
[[nodiscard]] std::filesystem::path nowPlayingFileOf(const dsp::Block &block);

/// \a typed, a path somebody typed, with a `~/` at its start taken as the
/// home folder.
[[nodiscard]] std::filesystem::path homePath(const std::string &typed);

/// **A stream for a block that streams**: a Broadcast block's radio
/// stream, or a Live Stream block's video, saying where it stands to
/// \a sink as the `BroadcastStatusChanged` signal carries it, on the
/// stream's own thread. Nothing for any other block.
[[nodiscard]] std::unique_ptr<stream::OutputStream> makeOutputStream(const dsp::Block &block,
                                                                     std::shared_ptr<rt::AudioTap> tap,
                                                                     std::function<void(const QJsonObject &)> sink);

/// Hands \a stream the settings \a block has now.
void configureOutputStream(stream::OutputStream &stream, const dsp::Block &block, const QString &sessionName);

/// Whether blocks of \a type stream to a server with a secret from the
/// store: Broadcast, and Live Stream where the build has it.
[[nodiscard]] bool isStreamingType(std::string_view type);

/// **The media player \a block's title follows**: a player key, or empty
/// for the one the session captures (mpris::choosePlayer()). Nothing when
/// its title does not come from a player.
[[nodiscard]] std::optional<QString> broadcastPlayer(const dsp::Block &block);

/// What an Application block captures, by the three names it stores, for
/// choosing the player a title follows.
[[nodiscard]] mpris::CapturedApplication capturedApplication(const dsp::Block &applicationSource);

/// A player's track as a stream takes it.
[[nodiscard]] stream::Track streamTrack(const mpris::Track &track);

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
