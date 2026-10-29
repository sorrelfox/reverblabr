// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "rt/audiotap.h"

#include <QObject>
#include <QString>

#include <cstdint>
#include <memory>

namespace rb::pw
{

class Connection;

/// **A recording played to the default output**, for somebody
/// listening to what they recorded.
///
/// **An ordinary playback stream, and nothing of a session's.** It has a
/// node of its own, `reverblabr.preview`, with the role of music, which
/// the session manager links to the default output as it links any
/// player's. It is never linked to a session's node and never linked by
/// the engine's own link manager, so no session hears it, and it asks the
/// graph for nothing: PipeWire converts the file's rate and channels to
/// the output's the way it does for every player.
///
/// **The audio thread only copies.** What it plays it reads out of an
/// rt::AudioTap that somebody else — the player's decoding thread — keeps
/// ahead of it, and a cycle the tap cannot fill is filled with silence.
///
/// Create it on the connection's thread, which is the only thread any of
/// this may be called from.
class PreviewStream : public QObject
{
    Q_OBJECT

public:
    explicit PreviewStream(Connection &connection, QObject *parent = nullptr);
    ~PreviewStream() override;

    /// Plays from \a source, which holds interleaved frames of
    /// source->channelCount() channels at \a sampleRate, and must outlive
    /// the stream or its stop(). \a title is what the audio applet shows.
    /// False if there is no connection or the server refused the stream.
    [[nodiscard]] bool start(rt::AudioTap &source, std::uint32_t sampleRate, const QString &title);

    /// Pauses (false) or resumes (true) without letting go of the stream.
    void setActive(bool active);

    /// Lets go of the stream. The tap is no longer read once this returns.
    void stop();

    [[nodiscard]] bool isRunning() const noexcept;

    /// The stream's node's id in the graph, once the server has given it
    /// one; 0 before that.
    [[nodiscard]] std::uint32_t nodeId() const noexcept;

    /// How many frames of silence the stream has had to play because the
    /// tap was empty. Nothing is late while a file plays; after its end,
    /// everything is.
    [[nodiscard]] std::uint64_t starvedFrames() const noexcept;

    /// The node's name, which every preview has.
    static constexpr const char *kNodeName = "reverblabr.preview";

Q_SIGNALS:
    /// The server took the stream down: the connection went, or it failed.
    void failed(const QString &reason);

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace rb::pw
