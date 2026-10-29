// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "record/audiofilereader.h"

#include <sndfile.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

namespace rb::record
{

struct AudioFileReader::Backend {
    Backend() = default;
    Backend(const Backend &) = delete;
    Backend &operator=(const Backend &) = delete;
    virtual ~Backend() = default;

    std::uint32_t sampleRate = 0;
    std::uint32_t channels = 0;
    std::uint64_t frames = 0;

    virtual std::size_t read(std::span<float> out, std::size_t frames) = 0;
    virtual std::uint64_t seek(std::uint64_t frame) = 0;
};

namespace
{

// ------------------------------------------------------------- libsndfile

struct SndfileBackend final : AudioFileReader::Backend {
    SNDFILE *file = nullptr;

    ~SndfileBackend() override
    {
        if (file != nullptr) {
            sf_close(file);
        }
    }

    std::size_t read(std::span<float> out, std::size_t wanted) override
    {
        wanted = std::min(wanted, out.size() / channels);
        const sf_count_t got = sf_readf_float(file, out.data(), static_cast<sf_count_t>(wanted));
        return got > 0 ? static_cast<std::size_t>(got) : 0;
    }

    std::uint64_t seek(std::uint64_t frame) override
    {
        if (frames > 0) {
            frame = std::min(frame, frames);
        }
        const sf_count_t at = sf_seek(file, static_cast<sf_count_t>(frame), SEEK_SET);
        return at >= 0 ? static_cast<std::uint64_t>(at) : 0;
    }
};

[[nodiscard]] std::unique_ptr<AudioFileReader::Backend> openWithSndfile(const std::filesystem::path &path)
{
    SF_INFO info{};
    SNDFILE *file = sf_open(path.c_str(), SFM_READ, &info);
    if (file == nullptr) {
        return nullptr;
    }
    auto backend = std::make_unique<SndfileBackend>();
    backend->file = file;
    if (info.channels <= 0 || info.samplerate <= 0) {
        return nullptr;
    }
    backend->sampleRate = static_cast<std::uint32_t>(info.samplerate);
    backend->channels = static_cast<std::uint32_t>(info.channels);
    backend->frames = info.frames > 0 ? static_cast<std::uint64_t>(info.frames) : 0;
    return backend;
}

// ------------------------------------------------------------------ FFmpeg

/// One sample of \a frame's channel \a channel at \a index, as a float,
/// whatever the decoder hands back: AAC decodes to planar floats, Apple
/// Lossless to planar 16- or 32-bit integers.
[[nodiscard]] float sampleOf(const AVFrame &frame, int channel, int index, int channels)
{
    const auto format = static_cast<AVSampleFormat>(frame.format);
    const bool planar = av_sample_fmt_is_planar(format) != 0;
    const int plane = planar ? channel : 0;
    const int at = planar ? index : index * channels + channel;
    const std::uint8_t *data = frame.extended_data[plane];
    switch (av_get_packed_sample_fmt(format)) {
    case AV_SAMPLE_FMT_FLT:
        return reinterpret_cast<const float *>(data)[at];
    case AV_SAMPLE_FMT_DBL:
        return static_cast<float>(reinterpret_cast<const double *>(data)[at]);
    case AV_SAMPLE_FMT_S16:
        return static_cast<float>(reinterpret_cast<const std::int16_t *>(data)[at]) / 32768.0f;
    case AV_SAMPLE_FMT_S32:
        return static_cast<float>(static_cast<double>(reinterpret_cast<const std::int32_t *>(data)[at]) / 2147483648.0);
    case AV_SAMPLE_FMT_U8:
        return (static_cast<float>(data[at]) - 128.0f) / 128.0f;
    default:
        return 0.0f;
    }
}

struct FfmpegBackend final : AudioFileReader::Backend {
    AVFormatContext *format = nullptr;
    AVCodecContext *codec = nullptr;
    AVPacket *packet = nullptr;
    AVFrame *decoded = nullptr;
    int stream = -1;
    /// Decoded frames not yet handed out, interleaved.
    std::vector<float> pending;
    std::size_t pendingAt = 0;
    bool draining = false;
    bool finished = false;
    /// After a seek: the frame asked for. What decodes before it is dropped.
    std::optional<std::uint64_t> aligningTo;

    ~FfmpegBackend() override
    {
        av_frame_free(&decoded);
        av_packet_free(&packet);
        avcodec_free_context(&codec);
        avformat_close_input(&format);
    }

    /// Where \a frame starts in the file, in samples from its start.
    [[nodiscard]] std::int64_t startOf(const AVFrame &frame) const
    {
        const AVStream *audio = format->streams[stream];
        const std::int64_t pts =
            frame.best_effort_timestamp != AV_NOPTS_VALUE ? frame.best_effort_timestamp : frame.pts;
        if (pts == AV_NOPTS_VALUE) {
            return -1;
        }
        const std::int64_t origin = audio->start_time != AV_NOPTS_VALUE ? audio->start_time : 0;
        return av_rescale_q(pts - origin, audio->time_base, AVRational{1, static_cast<int>(sampleRate)});
    }

    /// Feeds the decoder its next packet, or tells it there are no more.
    void feed()
    {
        for (;;) {
            if (av_read_frame(format, packet) < 0) {
                avcodec_send_packet(codec, nullptr);
                draining = true;
                return;
            }
            const bool ours = packet->stream_index == stream;
            if (ours) {
                avcodec_send_packet(codec, packet);
            }
            av_packet_unref(packet);
            if (ours) {
                return;
            }
        }
    }

    /// Decodes the next frame into `pending`, or says there is none.
    bool decodeMore()
    {
        while (!finished) {
            const int received = avcodec_receive_frame(codec, decoded);
            if (received == AVERROR(EAGAIN) && !draining) {
                feed();
                continue;
            }
            if (received != 0) {
                finished = true;
                return false;
            }
            const int count = decoded->nb_samples;
            int first = 0;
            if (aligningTo) {
                const std::int64_t start = startOf(*decoded);
                const auto target = static_cast<std::int64_t>(*aligningTo);
                if (start >= 0 && start + count <= target) {
                    // Wholly before the frame asked for.
                    av_frame_unref(decoded);
                    continue;
                }
                first = start >= 0 ? static_cast<int>(std::clamp<std::int64_t>(target - start, 0, count)) : 0;
                aligningTo.reset();
            }
            pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(pendingAt));
            pendingAt = 0;
            const int chans = static_cast<int>(channels);
            for (int index = first; index < count; ++index) {
                for (int channel = 0; channel < chans; ++channel) {
                    pending.push_back(sampleOf(*decoded, channel, index, chans));
                }
            }
            av_frame_unref(decoded);
            return true;
        }
        return false;
    }

    std::size_t read(std::span<float> out, std::size_t wanted) override
    {
        wanted = std::min(wanted, out.size() / channels);
        std::size_t done = 0;
        while (done < wanted) {
            const std::size_t available = (pending.size() - pendingAt) / channels;
            if (available == 0) {
                if (!decodeMore()) {
                    break;
                }
                continue;
            }
            const std::size_t take = std::min(available, wanted - done);
            std::copy_n(pending.begin() + static_cast<std::ptrdiff_t>(pendingAt),
                        take * channels,
                        out.begin() + static_cast<std::ptrdiff_t>(done * channels));
            pendingAt += take * channels;
            done += take;
        }
        return done;
    }

    std::uint64_t seek(std::uint64_t frame) override
    {
        if (frames > 0) {
            frame = std::min(frame, frames);
        }
        const AVStream *audio = format->streams[stream];
        const std::int64_t origin = audio->start_time != AV_NOPTS_VALUE ? audio->start_time : 0;
        // **A little before the frame**: a lossy decoder's first frame after
        // a seek is only half of what it should be, because its transform
        // overlaps the frame before. Seeking a few packets early and
        // decoding up to the frame asked for gives it that to overlap with.
        constexpr std::uint64_t kPreroll = 8192;
        const std::uint64_t from = frame > kPreroll ? frame - kPreroll : 0;
        const std::int64_t target = av_rescale_q(
            static_cast<std::int64_t>(from), AVRational{1, static_cast<int>(sampleRate)}, audio->time_base);
        if (av_seek_frame(format, stream, origin + target, AVSEEK_FLAG_BACKWARD) < 0) {
            return 0;
        }
        avcodec_flush_buffers(codec);
        pending.clear();
        pendingAt = 0;
        draining = false;
        finished = false;
        aligningTo = frame;
        return frame;
    }
};

[[nodiscard]] std::unique_ptr<AudioFileReader::Backend> openWithFfmpeg(const std::filesystem::path &path)
{
    auto backend = std::make_unique<FfmpegBackend>();
    if (avformat_open_input(&backend->format, path.c_str(), nullptr, nullptr) < 0) {
        return nullptr;
    }
    if (avformat_find_stream_info(backend->format, nullptr) < 0) {
        return nullptr;
    }
    const AVCodec *decoder = nullptr;
    backend->stream = av_find_best_stream(backend->format, AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0);
    if (backend->stream < 0 || decoder == nullptr) {
        return nullptr;
    }
    const AVStream *audio = backend->format->streams[backend->stream];
    backend->codec = avcodec_alloc_context3(decoder);
    if (backend->codec == nullptr || avcodec_parameters_to_context(backend->codec, audio->codecpar) < 0
        || avcodec_open2(backend->codec, decoder, nullptr) < 0) {
        return nullptr;
    }
    backend->packet = av_packet_alloc();
    backend->decoded = av_frame_alloc();
    if (backend->packet == nullptr || backend->decoded == nullptr || backend->codec->sample_rate <= 0
        || backend->codec->ch_layout.nb_channels <= 0) {
        return nullptr;
    }
    backend->sampleRate = static_cast<std::uint32_t>(backend->codec->sample_rate);
    backend->channels = static_cast<std::uint32_t>(backend->codec->ch_layout.nb_channels);
    if (audio->duration != AV_NOPTS_VALUE && audio->duration > 0) {
        backend->frames = static_cast<std::uint64_t>(
            av_rescale_q(audio->duration, audio->time_base, AVRational{1, backend->codec->sample_rate}));
    } else if (backend->format->duration > 0) {
        backend->frames = static_cast<std::uint64_t>(
            av_rescale(backend->format->duration, backend->codec->sample_rate, AV_TIME_BASE));
    }
    return backend;
}

} // namespace

std::optional<AudioFileReader> AudioFileReader::open(const std::filesystem::path &path)
{
    if (std::unique_ptr<Backend> backend = openWithSndfile(path)) {
        return AudioFileReader(std::move(backend));
    }
    if (std::unique_ptr<Backend> backend = openWithFfmpeg(path)) {
        return AudioFileReader(std::move(backend));
    }
    return std::nullopt;
}

AudioFileReader::AudioFileReader(std::unique_ptr<Backend> backend)
    : d(std::move(backend))
{
}

AudioFileReader::AudioFileReader(AudioFileReader &&other) noexcept = default;
AudioFileReader &AudioFileReader::operator=(AudioFileReader &&other) noexcept = default;
AudioFileReader::~AudioFileReader() = default;

std::uint32_t AudioFileReader::sampleRate() const noexcept
{
    return d ? d->sampleRate : 0;
}

std::uint32_t AudioFileReader::channels() const noexcept
{
    return d ? d->channels : 0;
}

std::uint64_t AudioFileReader::frames() const noexcept
{
    return d ? d->frames : 0;
}

std::size_t AudioFileReader::read(std::span<float> out, std::size_t frames)
{
    return d && d->channels > 0 ? d->read(out, frames) : 0;
}

std::uint64_t AudioFileReader::seek(std::uint64_t frame)
{
    return d ? d->seek(frame) : 0;
}

} // namespace rb::record
