// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "stream/protocol.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>

struct AVCodecContext;
struct AVPacket;

namespace rb::stream
{

/// Where an RTMP stream goes, read from the address a service gives:
/// `rtmp://host[:port]/app` or `rtmps://…`. The stream key is apart: it is
/// a secret, and lives in the desktop's secret store.
struct RtmpTarget {
    /// RTMPS: RTMP inside TLS.
    bool secure = false;
    std::string host;
    std::uint16_t port = 1935;
    /// Everything after the host, without the slashes either side: the
    /// application, and an instance if the service has one ("live2",
    /// "app/instance").
    std::string app;

    [[nodiscard]] bool operator==(const RtmpTarget &other) const = default;
};

/// \a url as a target, or NotConfigured for one that is not an RTMP
/// address: another scheme, no host, a port that is not one, or anything
/// but printable characters (nothing a person typed can reach the
/// protocol's own fields).
[[nodiscard]] std::expected<RtmpTarget, StreamError> parseRtmpUrl(std::string_view url);

/// \a target written back as an address, the way a server is told it
/// was asked for (`tcUrl`).
[[nodiscard]] std::string rtmpUrl(const RtmpTarget &target);

/// **A stream's connection to an RTMP server, through FFmpeg**: its FLV
/// muxer and its `rtmp` and `rtmps` protocols, with H.264 and AAC in it.
///
/// **Nothing it does can hold a stopping stream.** The host's name is
/// looked up first, on a thread that is left behind when it takes too long
/// (Connection::resolve), so a name that does not resolve is said as such.
/// FFmpeg is then given the name itself, which RTMPS needs: it goes in the
/// TLS hello (SNI), and a service with many names refuses a hello without
/// one. FFmpeg looks the name up again, where nothing can call it off, so
/// **connecting runs on a thread of its own** that is left behind, and lets
/// its connection go by itself, when the stream stops or the time is up.
/// Every other wait inside FFmpeg (connecting, the TLS handshake, the RTMP
/// handshake, a write the network will not take) is called off by its
/// interrupt callback within a tenth of a second.
///
/// **RTMPS checks the server's certificate** against the system's
/// authorities: a stream key is a password, and goes nowhere that cannot
/// prove who it is.
class RtmpOutput
{
public:
    struct Options {
        /// How long connecting, both handshakes and the stream's start may
        /// take, for each address the name has.
        std::chrono::milliseconds connectTimeout{10000};
        /// How long a write may wait for the network before the
        /// connection is given up as lost.
        std::chrono::milliseconds sendTimeout{10000};
        /// The authorities an RTMPS server's certificate is checked
        /// against. Empty for the system's; a test gives its own.
        std::string caFile;
    };

    /// Connects to \a target with \a key and starts the stream with
    /// \a video's and \a audio's headers.
    [[nodiscard]] static std::expected<std::unique_ptr<RtmpOutput>, StreamError> open(const RtmpTarget &target,
                                                                                      const std::string &key,
                                                                                      const AVCodecContext *video,
                                                                                      const AVCodecContext *audio,
                                                                                      const Options &options,
                                                                                      const std::atomic<bool> &cancel);

    /// Ends the stream: a last word to the server, which is not waited for
    /// past a second, or at all once the stream is stopping.
    ~RtmpOutput();
    RtmpOutput(const RtmpOutput &) = delete;
    RtmpOutput &operator=(const RtmpOutput &) = delete;

    /// A packet from the video encoder, its time in frames.
    [[nodiscard]] std::expected<void, StreamError> writeVideo(AVPacket *packet);
    /// A packet from the audio encoder, its time in samples.
    [[nodiscard]] std::expected<void, StreamError> writeAudio(AVPacket *packet);

    /// What has gone to the server, in bytes, headers and all.
    [[nodiscard]] std::uint64_t bytesSent() const noexcept;

private:
    RtmpOutput();

    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace rb::stream
