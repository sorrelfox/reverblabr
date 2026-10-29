// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "stream/title.h"

#include <optional>
#include <string>

namespace rb::stream
{

/// **What every stream a session sends to a server answers to**, whatever
/// it sends: a Broadcast block's radio stream and a Live Stream block's
/// video both take a secret from the desktop's store, a track from
/// whatever knows what is playing, and a word to stop. The session runtime
/// holds them all as this, and hands each its own settings.
class OutputStream
{
public:
    virtual ~OutputStream() = default;

    /// The secret the server is logged in to with, from the secret store:
    /// a Broadcast block's password, a Live Stream block's stream key.
    /// Nothing when the store has none; the stream says so and waits.
    virtual void setPassword(std::optional<std::string> password) = 0;
    /// The secret store could not be asked. The stream says so and waits
    /// until it is given a secret.
    virtual void setPasswordUnavailable() = 0;
    /// **What is playing**, from whatever knows (a media player, say).
    /// Takes over from a now-playing file until the stream is configured
    /// with another one.
    virtual void setNowPlaying(Track track) = 0;
    /// Tells the stream's thread to stop, and does not wait for it.
    virtual void requestStop() = 0;
};

} // namespace rb::stream
