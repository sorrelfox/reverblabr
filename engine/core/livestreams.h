// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/blockcatalogue.h"
#include "stream/livestream.h"
#include "stream/videoencoder.h"

#include <QJsonObject>
#include <QObject>
#include <QString>

#include <functional>

namespace rb::dsp
{
class Block;
}

namespace rb::core
{

/// What \a block's live stream is made from, read off its settings now:
/// everything but the key. Paths starting with `~/` are in the home folder,
/// and a colour that is not one is the default background.
[[nodiscard]] stream::LiveStreamSettings liveStreamSettings(const dsp::Block &block, const QString &sessionName);

/// A live stream's status as the `BroadcastStatusChanged` signal carries
/// it: what a radio stream's says (broadcastStatusJson()), and `{encoder,
/// width, height, fps, framesSent, framesDropped}`.
[[nodiscard]] QJsonObject liveStreamStatusJson(const stream::LiveStreamStatus &status);

/// **The Live Stream block's encoders this system has not got**, for the
/// catalogue: each H.264 encoder \a probe did not find, "encoderMissing";
/// and the automatic choice too, "noH264Encoder", when it found none at
/// all, which is a block that cannot stream here.
[[nodiscard]] UnavailableChoices liveStreamUnavailableChoices(const stream::VideoEncoderProbe &probe);

/// **The same, from what is known now**: the whole probe once it is made,
/// and until then the software encoders alone, the graphics cards' listed
/// as missing. Never waits for a graphics card.
[[nodiscard]] UnavailableChoices liveStreamUnavailableChoices();

/// **Has the whole encoder probe made on a thread of its own**, so that the
/// engine's thread never waits for the graphics cards, and calls
/// \a whenDone on \a receiver's thread once it is: for a catalogue that said
/// "software only" to be said again. Nothing, and false, when the probe is
/// made already.
bool probeVideoEncodersFor(QObject *receiver, std::function<void()> whenDone);

} // namespace rb::core
