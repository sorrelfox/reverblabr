// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "stream/protocol.h"
#include "stream/title.h"

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace rb::stream
{

/// The rate a stream in \a format goes out at, for a session at
/// \a sessionRate: the session's when it is 44.1 or 48 kHz, which every
/// format and every player takes, and 48 kHz otherwise. **Opus is always
/// 48 kHz**, the one rate it is made at.
[[nodiscard]] std::uint32_t streamRateFor(StreamFormat format, std::uint32_t sessionRate) noexcept;

/// The bit rates an encoder takes for a format and a number of channels,
/// in kilobits a second. **Measured on FFmpeg's libraries**, not taken from
/// their manuals: Vorbis wants 48 kbit/s at least for two channels and
/// takes no more than 192 for one, and Opus no more than 256 for one.
struct BitrateRange {
    std::uint32_t lowest = 32;
    std::uint32_t highest = 320;
};

[[nodiscard]] BitrateRange bitrateRange(StreamFormat format, std::uint32_t channels) noexcept;

/// \a kbps brought into bitrateRange().
[[nodiscard]] std::uint32_t clampedBitrate(StreamFormat format, std::uint32_t channels, std::uint32_t kbps) noexcept;

/// Which formats the system's FFmpeg can encode, asked once. A distribution
/// may build FFmpeg without one of the libraries.
struct StreamEncoderProbe {
    bool mp3 = false;
    bool opus = false;
    bool vorbis = false;
    bool aac = false;
};

[[nodiscard]] const StreamEncoderProbe &probeStreamEncoders();

/// "encoderMissing" for a format the probe says cannot be made here.
[[nodiscard]] std::optional<std::string_view>
streamFormatUnavailable(StreamFormat format, const StreamEncoderProbe &probe = probeStreamEncoders());

/// Audio in, a stream's bytes out: the encoder and the container, through
/// the system's FFmpeg, writing to memory rather than to a file.
///
/// **What comes out can be joined at any point**, which is what a server
/// passing it on to listeners who tune in at any moment needs: MP3 frames
/// with no tag in front and no index, ADTS frames for AAC, and Ogg pages
/// for Opus and Vorbis, whose headers the server keeps and gives to each
/// new listener.
///
/// **An Ogg stream's title is in its comment header**, so a new title is a
/// new stream chained onto the end of the old one: restart() finishes the
/// one, starts the next with the title in it, and the bytes go on over the
/// same connection. Players and Icecast both read a chained stream that way.
///
/// On the stream's thread only: encoding allocates and takes its time.
class StreamEncoder
{
public:
    struct Spec {
        StreamFormat format = StreamFormat::Mp3;
        std::uint32_t sampleRate = 48000;
        std::uint32_t channels = 2;
        std::uint32_t bitrateKbps = 128;
        /// The title an Ogg stream carries in its comment header.
        Track track;
    };

    [[nodiscard]] static std::expected<std::unique_ptr<StreamEncoder>, StreamError> create(const Spec &spec);
    ~StreamEncoder();
    StreamEncoder(const StreamEncoder &) = delete;
    StreamEncoder &operator=(const StreamEncoder &) = delete;

    /// Interleaved frames, a whole number of them, at the spec's rate.
    [[nodiscard]] std::expected<void, StreamError> encode(std::span<const float> samples);

    /// For an Ogg stream: ends this one and starts the next, carrying
    /// \a track in its comment header.
    [[nodiscard]] std::expected<void, StreamError> restart(const Track &track);

    /// The bytes made since the last call, which the caller sends.
    [[nodiscard]] std::vector<char> take();

    [[nodiscard]] const Spec &spec() const noexcept;

private:
    StreamEncoder();

    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace rb::stream
