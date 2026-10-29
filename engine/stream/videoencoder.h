// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "stream/protocol.h"

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct AVCodecContext;
struct AVPacket;

namespace rb::stream
{

class Canvas;

/// The H.264 encoders a live stream can use, through the system's FFmpeg.
/// **Which of them a system has depends on its distribution and its
/// hardware** (ADR-0027): Fedora's FFmpeg has no x264, and OpenH264 only
/// once Cisco's library is installed; Fedora's VA-API drivers encode no
/// H.264; NVENC needs NVIDIA's own driver.
enum class VideoEncoderKind : std::uint8_t {
    /// The graphics card's encoder through VA-API (Intel, AMD).
    Vaapi,
    /// NVIDIA's encoder.
    Nvenc,
    /// x264, the best software encoder.
    X264,
    /// Cisco's OpenH264, a software encoder whose binary Cisco licenses.
    OpenH264,
};

/// The stable id a session file holds, "vaapi" and the like.
[[nodiscard]] std::string_view videoEncoderId(VideoEncoderKind kind) noexcept;
[[nodiscard]] std::optional<VideoEncoderKind> videoEncoderFromId(std::string_view id) noexcept;
/// Every kind, in the order an automatic choice tries them.
[[nodiscard]] std::span<const VideoEncoderKind> allVideoEncoders() noexcept;

/// **Which H.264 encoders work here**, found by opening each and encoding a
/// frame with it: a library that is installed but cannot encode (the
/// OpenH264 stand-in Fedora ships until Cisco's is installed, a VA-API
/// driver without H.264, NVENC with no NVIDIA driver) fails then, and not
/// in the middle of a stream.
struct VideoEncoderProbe {
    bool vaapi = false;
    bool nvenc = false;
    bool x264 = false;
    bool openh264 = false;
    /// The render node the VA-API encoder worked on, on a computer with
    /// more than one graphics card.
    std::string vaapiDevice;

    [[nodiscard]] bool has(VideoEncoderKind kind) const noexcept;
    [[nodiscard]] bool any() const noexcept
    {
        return vaapi || nvenc || x264 || openh264;
    }
};

/// The probe, made once a process: it opens graphics devices, which takes
/// a moment and wakes every graphics card. **Not on a thread something
/// waits on**: a stream's own thread asks it, and the engine has it made on
/// a thread of its own (core::probeVideoEncodersFor()).
[[nodiscard]] const VideoEncoderProbe &probeVideoEncoders();

/// **The software encoders only** (x264, OpenH264), made once: no graphics
/// card is touched, so it is quick enough for any thread. What is known
/// until the whole probe is done.
[[nodiscard]] const VideoEncoderProbe &probeSoftwareVideoEncoders();

/// The whole probe, once it is done; nothing before.
[[nodiscard]] std::optional<VideoEncoderProbe> finishedVideoEncoderProbe();

/// **The encoders to try, in order**: the one asked for, if it is here,
/// and then the rest; with nothing asked for, the hardware first (it costs
/// the processor nothing), then x264, then OpenH264. Only the ones the
/// probe found.
[[nodiscard]] std::vector<VideoEncoderKind> videoEncoderOrder(std::optional<VideoEncoderKind> wanted,
                                                              const VideoEncoderProbe &probe);

/// What a frame is handed to: the packets an encoder made, which the
/// caller writes. False stops the encoding with a failure.
using PacketSink = std::function<bool(AVPacket *packet)>;

/// **The pictures, as H.264**, for a muxer that wants its headers apart
/// (FLV): the sequence and picture headers are in the encoder's extra
/// data, and every packet is a frame.
///
/// Set for live streaming whichever the encoder: a key frame every two
/// seconds, which every service asks for and lets a viewer join quickly;
/// no frames that wait for later ones; and a bit rate held to what is
/// asked, so the connection is not swamped.
class VideoEncoder
{
public:
    struct Spec {
        VideoEncoderKind kind = VideoEncoderKind::X264;
        int width = 1280;
        int height = 720;
        int fps = 30;
        std::uint32_t bitrateKbps = 3000;
        /// The VA-API render node; empty for the probe's.
        std::string vaapiDevice;
    };

    /// \a spec's encoder, or why it would not open.
    [[nodiscard]] static std::expected<std::unique_ptr<VideoEncoder>, StreamError> create(const Spec &spec);
    /// **The first of \a order that opens** with \a spec's size and rate,
    /// so a graphics card that will not encode this size falls back to the
    /// next; why the last one failed, when none would.
    [[nodiscard]] static std::expected<std::unique_ptr<VideoEncoder>, StreamError>
    createFirst(const std::vector<VideoEncoderKind> &order, Spec spec);

    ~VideoEncoder();
    VideoEncoder(const VideoEncoder &) = delete;
    VideoEncoder &operator=(const VideoEncoder &) = delete;

    /// Encodes \a picture as frame number \a index, and hands every packet
    /// made to \a sink.
    [[nodiscard]] std::expected<void, StreamError>
    encode(const Canvas &picture, std::int64_t index, const PacketSink &sink);

    [[nodiscard]] const Spec &spec() const noexcept;
    [[nodiscard]] const AVCodecContext *context() const noexcept;

private:
    VideoEncoder();

    struct Private;
    std::unique_ptr<Private> d;
};

/// **The sound, as AAC**, through FFmpeg's own encoder, for the same muxer:
/// raw frames, with the configuration in the extra data.
class AacEncoder
{
public:
    [[nodiscard]] static std::expected<std::unique_ptr<AacEncoder>, StreamError>
    create(std::uint32_t sampleRate, std::uint32_t channels, std::uint32_t bitrateKbps);

    ~AacEncoder();
    AacEncoder(const AacEncoder &) = delete;
    AacEncoder &operator=(const AacEncoder &) = delete;

    /// Interleaved frames at the encoder's rate; whole AAC frames go to
    /// \a sink as they fill, the rest waits for more.
    [[nodiscard]] std::expected<void, StreamError> encode(std::span<const float> samples, const PacketSink &sink);

    [[nodiscard]] std::uint32_t sampleRate() const noexcept;
    [[nodiscard]] std::uint32_t channels() const noexcept;
    [[nodiscard]] const AVCodecContext *context() const noexcept;

private:
    AacEncoder();

    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace rb::stream
