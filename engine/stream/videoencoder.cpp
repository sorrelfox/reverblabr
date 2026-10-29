// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "stream/videoencoder.h"

#include "stream/canvas.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/hwcontext.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <utility>

namespace rb::stream
{

namespace
{

constexpr std::array kAllEncoders{
    VideoEncoderKind::Vaapi,
    VideoEncoderKind::Nvenc,
    VideoEncoderKind::X264,
    VideoEncoderKind::OpenH264,
};

[[nodiscard]] const char *codecName(VideoEncoderKind kind) noexcept
{
    switch (kind) {
    case VideoEncoderKind::Vaapi:
        return "h264_vaapi";
    case VideoEncoderKind::Nvenc:
        return "h264_nvenc";
    case VideoEncoderKind::X264:
        return "libx264";
    case VideoEncoderKind::OpenH264:
        return "libopenh264";
    }
    return "libx264";
}

/// The render nodes there are, in order: where a VA-API encoder may be.
[[nodiscard]] std::vector<std::string> renderNodes()
{
    std::vector<std::string> nodes;
    for (int node = 128; node < 192; ++node) {
        const std::string path = "/dev/dri/renderD" + std::to_string(node);
        std::error_code error;
        if (std::filesystem::exists(path, error)) {
            nodes.push_back(path);
        }
    }
    return nodes;
}

/// Whether \a kind opens and takes a frame, small and plain, on \a device.
[[nodiscard]] bool encodesHere(VideoEncoderKind kind, const std::string &device = {})
{
    VideoEncoder::Spec spec;
    spec.kind = kind;
    spec.width = 320;
    spec.height = 192;
    spec.fps = 30;
    spec.bitrateKbps = 300;
    spec.vaapiDevice = device;
    std::expected<std::unique_ptr<VideoEncoder>, StreamError> made = VideoEncoder::create(spec);
    if (!made) {
        return false;
    }
    Canvas picture(spec.width, spec.height);
    picture.fill(Rgb{0x20, 0x40, 0x60});
    return (*made)->encode(picture, 0, [](AVPacket *) { return true; }).has_value();
}

} // namespace

std::string_view videoEncoderId(VideoEncoderKind kind) noexcept
{
    switch (kind) {
    case VideoEncoderKind::Vaapi:
        return "vaapi";
    case VideoEncoderKind::Nvenc:
        return "nvenc";
    case VideoEncoderKind::X264:
        return "x264";
    case VideoEncoderKind::OpenH264:
        return "openh264";
    }
    return "x264";
}

std::optional<VideoEncoderKind> videoEncoderFromId(std::string_view id) noexcept
{
    for (const VideoEncoderKind kind : kAllEncoders) {
        if (videoEncoderId(kind) == id) {
            return kind;
        }
    }
    return std::nullopt;
}

std::span<const VideoEncoderKind> allVideoEncoders() noexcept
{
    return kAllEncoders;
}

bool VideoEncoderProbe::has(VideoEncoderKind kind) const noexcept
{
    switch (kind) {
    case VideoEncoderKind::Vaapi:
        return vaapi;
    case VideoEncoderKind::Nvenc:
        return nvenc;
    case VideoEncoderKind::X264:
        return x264;
    case VideoEncoderKind::OpenH264:
        return openh264;
    }
    return false;
}

namespace
{

/// Set once the whole probe is made.
std::atomic<bool> s_probeFinished{false};

} // namespace

const VideoEncoderProbe &probeSoftwareVideoEncoders()
{
    static const VideoEncoderProbe probe = [] {
        av_log_set_level(AV_LOG_ERROR);
        VideoEncoderProbe found;
        found.x264 = encodesHere(VideoEncoderKind::X264);
        found.openh264 = encodesHere(VideoEncoderKind::OpenH264);
        return found;
    }();
    return probe;
}

std::optional<VideoEncoderProbe> finishedVideoEncoderProbe()
{
    if (!s_probeFinished.load(std::memory_order_acquire)) {
        return std::nullopt;
    }
    return probeVideoEncoders();
}

const VideoEncoderProbe &probeVideoEncoders()
{
    static const VideoEncoderProbe probe = [] {
        VideoEncoderProbe found = probeSoftwareVideoEncoders();
        // **Every graphics card in turn**: on a computer with two, the
        // first may be the one that cannot encode.
        if (avcodec_find_encoder_by_name(codecName(VideoEncoderKind::Vaapi)) != nullptr) {
            for (const std::string &node : renderNodes()) {
                if (encodesHere(VideoEncoderKind::Vaapi, node)) {
                    found.vaapi = true;
                    found.vaapiDevice = node;
                    break;
                }
            }
        }
        found.nvenc = encodesHere(VideoEncoderKind::Nvenc);
        return found;
    }();
    s_probeFinished.store(true, std::memory_order_release);
    return probe;
}

std::vector<VideoEncoderKind> videoEncoderOrder(std::optional<VideoEncoderKind> wanted, const VideoEncoderProbe &probe)
{
    std::vector<VideoEncoderKind> order;
    if (wanted && probe.has(*wanted)) {
        order.push_back(*wanted);
    }
    for (const VideoEncoderKind kind : kAllEncoders) {
        if (probe.has(kind) && std::ranges::find(order, kind) == order.end()) {
            order.push_back(kind);
        }
    }
    return order;
}

struct VideoEncoder::Private {
    Spec spec;
    AVCodecContext *encoder = nullptr;
    AVBufferRef *device = nullptr;
    AVBufferRef *frames = nullptr;
    SwsContext *converter = nullptr;
    AVFrame *picture = nullptr;
    AVFrame *uploaded = nullptr;
    AVPacket *packet = nullptr;

    ~Private()
    {
        sws_freeContext(converter);
        av_frame_free(&picture);
        av_frame_free(&uploaded);
        av_packet_free(&packet);
        avcodec_free_context(&encoder);
        av_buffer_unref(&frames);
        av_buffer_unref(&device);
    }

    [[nodiscard]] bool hardwareFrames()
    {
        const std::string node = spec.vaapiDevice.empty() ? probeVideoEncoders().vaapiDevice : spec.vaapiDevice;
        if (av_hwdevice_ctx_create(&device, AV_HWDEVICE_TYPE_VAAPI, node.empty() ? nullptr : node.c_str(), nullptr, 0)
            < 0) {
            return false;
        }
        frames = av_hwframe_ctx_alloc(device);
        if (frames == nullptr) {
            return false;
        }
        auto *context = reinterpret_cast<AVHWFramesContext *>(frames->data);
        context->format = AV_PIX_FMT_VAAPI;
        context->sw_format = AV_PIX_FMT_NV12;
        context->width = spec.width;
        context->height = spec.height;
        context->initial_pool_size = 8;
        if (av_hwframe_ctx_init(frames) < 0) {
            return false;
        }
        encoder->hw_frames_ctx = av_buffer_ref(frames);
        uploaded = av_frame_alloc();
        return encoder->hw_frames_ctx != nullptr && uploaded != nullptr;
    }

    [[nodiscard]] std::expected<void, StreamError> open()
    {
        const AVCodec *codec = avcodec_find_encoder_by_name(codecName(spec.kind));
        if (codec == nullptr) {
            return std::unexpected(StreamError::EncoderMissing);
        }
        encoder = avcodec_alloc_context3(codec);
        picture = av_frame_alloc();
        packet = av_packet_alloc();
        if (encoder == nullptr || picture == nullptr || packet == nullptr) {
            return std::unexpected(StreamError::EncoderFailed);
        }
        const bool hardware = spec.kind == VideoEncoderKind::Vaapi;
        const AVPixelFormat software = hardware ? AV_PIX_FMT_NV12 : AV_PIX_FMT_YUV420P;
        encoder->width = spec.width;
        encoder->height = spec.height;
        encoder->time_base = AVRational{1, spec.fps};
        encoder->framerate = AVRational{spec.fps, 1};
        encoder->pix_fmt = hardware ? AV_PIX_FMT_VAAPI : software;
        // A key frame every two seconds: what services ask for, and how soon
        // a viewer who joins sees a picture.
        encoder->gop_size = spec.fps * 2;
        encoder->max_b_frames = 0;
        const std::int64_t bitrate = static_cast<std::int64_t>(spec.bitrateKbps) * 1000;
        encoder->bit_rate = bitrate;
        encoder->rc_max_rate = bitrate;
        encoder->rc_buffer_size = static_cast<int>(std::min<std::int64_t>(bitrate, 1 << 30));
        // **The headers apart from the frames**, as FLV keeps them.
        encoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        // The picture is converted with BT.709's colours into video's
        // range, and says so.
        encoder->colorspace = AVCOL_SPC_BT709;
        encoder->color_primaries = AVCOL_PRI_BT709;
        encoder->color_trc = AVCOL_TRC_BT709;
        encoder->color_range = AVCOL_RANGE_MPEG;

        AVDictionary *options = nullptr;
        switch (spec.kind) {
        case VideoEncoderKind::X264:
            // Fast enough for a frame that is mostly still on any processor,
            // and nothing held back for later frames.
            av_dict_set(&options, "preset", "veryfast", 0);
            av_dict_set(&options, "tune", "zerolatency", 0);
            // Key frames on time only, never early for a change of scene:
            // services count the seconds between them.
            av_dict_set(&options, "x264-params", "scenecut=0", 0);
            break;
        case VideoEncoderKind::OpenH264:
            av_dict_set(&options, "rc_mode", "bitrate", 0);
            break;
        case VideoEncoderKind::Nvenc:
            av_dict_set(&options, "preset", "p4", 0);
            av_dict_set(&options, "tune", "ll", 0);
            av_dict_set(&options, "rc", "cbr", 0);
            break;
        case VideoEncoderKind::Vaapi:
            av_dict_set(&options, "rc_mode", "CBR", 0);
            break;
        }
        if (hardware && !hardwareFrames()) {
            av_dict_free(&options);
            return std::unexpected(StreamError::EncoderMissing);
        }
        const int opened = avcodec_open2(encoder, codec, &options);
        av_dict_free(&options);
        if (opened < 0) {
            // **The encoder is there and said no**: a stand-in library, a
            // card without H.264, or a size it does not take.
            return std::unexpected(StreamError::EncoderMissing);
        }

        picture->format = software;
        picture->width = spec.width;
        picture->height = spec.height;
        if (av_frame_get_buffer(picture, 0) < 0) {
            return std::unexpected(StreamError::EncoderFailed);
        }
        converter = sws_getContext(spec.width,
                                   spec.height,
                                   AV_PIX_FMT_RGBA,
                                   spec.width,
                                   spec.height,
                                   software,
                                   SWS_BILINEAR,
                                   nullptr,
                                   nullptr,
                                   nullptr);
        if (converter == nullptr) {
            return std::unexpected(StreamError::EncoderFailed);
        }
        (void)sws_setColorspaceDetails(converter,
                                       sws_getCoefficients(SWS_CS_DEFAULT),
                                       1,
                                       sws_getCoefficients(SWS_CS_ITU709),
                                       0,
                                       0,
                                       1 << 16,
                                       1 << 16);
        return {};
    }

    [[nodiscard]] bool drain(const PacketSink &sink)
    {
        for (;;) {
            const int received = avcodec_receive_packet(encoder, packet);
            if (received == AVERROR(EAGAIN) || received == AVERROR_EOF) {
                return true;
            }
            if (received < 0) {
                return false;
            }
            const bool taken = sink(packet);
            av_packet_unref(packet);
            if (!taken) {
                return false;
            }
        }
    }
};

VideoEncoder::VideoEncoder()
    : d(std::make_unique<Private>())
{
}

VideoEncoder::~VideoEncoder() = default;

std::expected<std::unique_ptr<VideoEncoder>, StreamError> VideoEncoder::create(const Spec &spec)
{
    std::unique_ptr<VideoEncoder> made{new VideoEncoder};
    made->d->spec = spec;
    // H.264's 4:2:0 wants even sizes.
    made->d->spec.width = std::max(16, spec.width & ~1);
    made->d->spec.height = std::max(16, spec.height & ~1);
    made->d->spec.fps = std::clamp(spec.fps, 1, 60);
    if (const std::expected<void, StreamError> opened = made->d->open(); !opened) {
        return std::unexpected(opened.error());
    }
    return made;
}

std::expected<std::unique_ptr<VideoEncoder>, StreamError>
VideoEncoder::createFirst(const std::vector<VideoEncoderKind> &order, Spec spec)
{
    StreamError last = StreamError::EncoderMissing;
    for (const VideoEncoderKind kind : order) {
        spec.kind = kind;
        std::expected<std::unique_ptr<VideoEncoder>, StreamError> made = create(spec);
        if (made) {
            return made;
        }
        last = made.error();
    }
    return std::unexpected(last);
}

std::expected<void, StreamError> VideoEncoder::encode(const Canvas &picture, std::int64_t index, const PacketSink &sink)
{
    if (picture.width() != d->spec.width || picture.height() != d->spec.height) {
        return std::unexpected(StreamError::EncoderFailed);
    }
    // The encoder may still hold the last frame: a fresh buffer if so.
    if (av_frame_make_writable(d->picture) < 0) {
        return std::unexpected(StreamError::EncoderFailed);
    }
    const std::uint8_t *planes[4] = {picture.pixels().data(), nullptr, nullptr, nullptr};
    const int strides[4] = {picture.width() * 4, 0, 0, 0};
    if (sws_scale(d->converter, planes, strides, 0, picture.height(), d->picture->data, d->picture->linesize) <= 0) {
        return std::unexpected(StreamError::EncoderFailed);
    }
    d->picture->pts = index;
    AVFrame *sent = d->picture;
    if (d->spec.kind == VideoEncoderKind::Vaapi) {
        av_frame_unref(d->uploaded);
        if (av_hwframe_get_buffer(d->frames, d->uploaded, 0) < 0
            || av_hwframe_transfer_data(d->uploaded, d->picture, 0) < 0) {
            return std::unexpected(StreamError::EncoderFailed);
        }
        d->uploaded->pts = index;
        sent = d->uploaded;
    }
    if (avcodec_send_frame(d->encoder, sent) < 0 || !d->drain(sink)) {
        return std::unexpected(StreamError::EncoderFailed);
    }
    return {};
}

const VideoEncoder::Spec &VideoEncoder::spec() const noexcept
{
    return d->spec;
}

const AVCodecContext *VideoEncoder::context() const noexcept
{
    return d->encoder;
}

struct AacEncoder::Private {
    AVCodecContext *encoder = nullptr;
    AVFrame *frame = nullptr;
    AVPacket *packet = nullptr;
    std::vector<float> pending;
    std::int64_t samplesSent = 0;

    ~Private()
    {
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&encoder);
    }
};

AacEncoder::AacEncoder()
    : d(std::make_unique<Private>())
{
}

AacEncoder::~AacEncoder() = default;

std::expected<std::unique_ptr<AacEncoder>, StreamError>
AacEncoder::create(std::uint32_t sampleRate, std::uint32_t channels, std::uint32_t bitrateKbps)
{
    // FFmpeg's own AAC encoder, never libfdk_aac, as for the M4A
    // recordings and the Broadcast block (ADR-0026).
    const AVCodec *codec = avcodec_find_encoder_by_name("aac");
    if (codec == nullptr) {
        return std::unexpected(StreamError::EncoderMissing);
    }
    std::unique_ptr<AacEncoder> made{new AacEncoder};
    Private &p = *made->d;
    p.encoder = avcodec_alloc_context3(codec);
    p.frame = av_frame_alloc();
    p.packet = av_packet_alloc();
    if (p.encoder == nullptr || p.frame == nullptr || p.packet == nullptr) {
        return std::unexpected(StreamError::EncoderFailed);
    }
    p.encoder->sample_rate = static_cast<int>(sampleRate);
    p.encoder->sample_fmt = AV_SAMPLE_FMT_FLTP;
    av_channel_layout_default(&p.encoder->ch_layout, static_cast<int>(std::clamp(channels, 1U, 2U)));
    p.encoder->time_base = AVRational{1, static_cast<int>(sampleRate)};
    p.encoder->bit_rate = static_cast<std::int64_t>(std::clamp(bitrateKbps, 32U, 320U)) * 1000;
    p.encoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (avcodec_open2(p.encoder, codec, nullptr) < 0) {
        return std::unexpected(StreamError::UnsupportedSettings);
    }
    p.frame->format = p.encoder->sample_fmt;
    p.frame->sample_rate = p.encoder->sample_rate;
    av_channel_layout_copy(&p.frame->ch_layout, &p.encoder->ch_layout);
    p.frame->nb_samples = p.encoder->frame_size > 0 ? p.encoder->frame_size : 1024;
    if (av_frame_get_buffer(p.frame, 0) < 0) {
        return std::unexpected(StreamError::EncoderFailed);
    }
    return made;
}

std::expected<void, StreamError> AacEncoder::encode(std::span<const float> samples, const PacketSink &sink)
{
    Private &p = *d;
    p.pending.insert(p.pending.end(), samples.begin(), samples.end());
    const auto channels = static_cast<std::size_t>(p.encoder->ch_layout.nb_channels);
    const auto size = static_cast<std::size_t>(p.encoder->frame_size > 0 ? p.encoder->frame_size : 1024);
    std::size_t used = 0;
    while (p.pending.size() - used >= size * channels) {
        if (av_frame_make_writable(p.frame) < 0) {
            return std::unexpected(StreamError::EncoderFailed);
        }
        p.frame->nb_samples = static_cast<int>(size);
        for (std::size_t channel = 0; channel < channels; ++channel) {
            auto *out = reinterpret_cast<float *>(p.frame->data[channel]);
            for (std::size_t index = 0; index < size; ++index) {
                out[index] = p.pending[used + index * channels + channel];
            }
        }
        used += size * channels;
        p.frame->pts = p.samplesSent;
        p.samplesSent += static_cast<std::int64_t>(size);
        if (avcodec_send_frame(p.encoder, p.frame) < 0) {
            return std::unexpected(StreamError::EncoderFailed);
        }
        for (;;) {
            const int received = avcodec_receive_packet(p.encoder, p.packet);
            if (received == AVERROR(EAGAIN) || received == AVERROR_EOF) {
                break;
            }
            if (received < 0) {
                return std::unexpected(StreamError::EncoderFailed);
            }
            const bool taken = sink(p.packet);
            av_packet_unref(p.packet);
            if (!taken) {
                return std::unexpected(StreamError::EncoderFailed);
            }
        }
    }
    p.pending.erase(p.pending.begin(), p.pending.begin() + static_cast<std::ptrdiff_t>(used));
    return {};
}

std::uint32_t AacEncoder::sampleRate() const noexcept
{
    return static_cast<std::uint32_t>(d->encoder->sample_rate);
}

std::uint32_t AacEncoder::channels() const noexcept
{
    return static_cast<std::uint32_t>(d->encoder->ch_layout.nb_channels);
}

const AVCodecContext *AacEncoder::context() const noexcept
{
    return d->encoder;
}

} // namespace rb::stream
