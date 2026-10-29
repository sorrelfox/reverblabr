// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "stream/streamencoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
}

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>

namespace rb::stream
{

namespace
{

/// The encoder and the container for each format, and the samples the
/// encoder takes. **The libraries, not FFmpeg's own Opus and Vorbis
/// encoders**, which FFmpeg itself calls experimental.
struct Recipe {
    const char *encoder;
    const char *muxer;
    AVSampleFormat sampleFormat;
};

[[nodiscard]] Recipe recipeFor(StreamFormat format)
{
    switch (format) {
    case StreamFormat::Mp3:
        return {"libmp3lame", "mp3", AV_SAMPLE_FMT_FLTP};
    case StreamFormat::Opus:
        return {"libopus", "ogg", AV_SAMPLE_FMT_FLT};
    case StreamFormat::Vorbis:
        return {"libvorbis", "ogg", AV_SAMPLE_FMT_FLTP};
    case StreamFormat::Aac:
        // FFmpeg's own AAC encoder, never libfdk_aac, for the reason the
        // M4A recordings give.
        return {"aac", "adts", AV_SAMPLE_FMT_FLTP};
    }
    return {"libmp3lame", "mp3", AV_SAMPLE_FMT_FLTP};
}

/// Whether FFmpeg will make \a format at 48 kHz stereo.
[[nodiscard]] bool opens(StreamFormat format)
{
    const Recipe recipe = recipeFor(format);
    const AVCodec *codec = avcodec_find_encoder_by_name(recipe.encoder);
    if (codec == nullptr || av_guess_format(recipe.muxer, nullptr, nullptr) == nullptr) {
        return false;
    }
    AVCodecContext *context = avcodec_alloc_context3(codec);
    if (context == nullptr) {
        return false;
    }
    context->sample_rate = 48000;
    context->sample_fmt = recipe.sampleFormat;
    av_channel_layout_default(&context->ch_layout, 2);
    context->bit_rate = 128000;
    context->time_base = AVRational{1, 48000};
    const bool opened = avcodec_open2(context, codec, nullptr) >= 0;
    avcodec_free_context(&context);
    return opened;
}

// FFmpeg 7 made the buffer a write callback is given const.
#if LIBAVFORMAT_VERSION_MAJOR >= 61
using WrittenBytes = const std::uint8_t *;
#else
using WrittenBytes = std::uint8_t *;
#endif

} // namespace

std::uint32_t streamRateFor(StreamFormat format, std::uint32_t sessionRate) noexcept
{
    if (format != StreamFormat::Opus && (sessionRate == 44100 || sessionRate == 48000)) {
        return sessionRate;
    }
    return 48000;
}

BitrateRange bitrateRange(StreamFormat format, std::uint32_t channels) noexcept
{
    const bool mono = channels < 2;
    switch (format) {
    case StreamFormat::Vorbis:
        return mono ? BitrateRange{32, 192} : BitrateRange{48, 320};
    case StreamFormat::Opus:
        return mono ? BitrateRange{32, 256} : BitrateRange{32, 320};
    case StreamFormat::Mp3:
    case StreamFormat::Aac:
        break;
    }
    return BitrateRange{32, 320};
}

std::uint32_t clampedBitrate(StreamFormat format, std::uint32_t channels, std::uint32_t kbps) noexcept
{
    const BitrateRange range = bitrateRange(format, channels);
    return std::clamp(kbps, range.lowest, range.highest);
}

const StreamEncoderProbe &probeStreamEncoders()
{
    static const StreamEncoderProbe probe = [] {
        av_log_set_level(AV_LOG_ERROR);
        StreamEncoderProbe found;
        found.mp3 = opens(StreamFormat::Mp3);
        found.opus = opens(StreamFormat::Opus);
        found.vorbis = opens(StreamFormat::Vorbis);
        found.aac = opens(StreamFormat::Aac);
        return found;
    }();
    return probe;
}

std::optional<std::string_view> streamFormatUnavailable(StreamFormat format, const StreamEncoderProbe &probe)
{
    const bool available = (format == StreamFormat::Mp3 && probe.mp3) || (format == StreamFormat::Opus && probe.opus)
        || (format == StreamFormat::Vorbis && probe.vorbis) || (format == StreamFormat::Aac && probe.aac);
    if (!available) {
        return "encoderMissing";
    }
    return std::nullopt;
}

struct StreamEncoder::Private {
    Spec spec;
    AVFormatContext *output = nullptr;
    AVCodecContext *encoder = nullptr;
    AVStream *stream = nullptr;
    AVFrame *frame = nullptr;
    AVPacket *packet = nullptr;
    /// Frames waiting for a whole encoder frame, interleaved.
    std::vector<float> pending;
    std::int64_t samplesSent = 0;
    /// What the container has written and the caller has not taken yet.
    std::vector<char> bytes;

    ~Private()
    {
        close();
    }

    static int written(void *opaque, WrittenBytes data, int size)
    {
        auto *self = static_cast<Private *>(opaque);
        self->bytes.insert(self->bytes.end(), data, data + size);
        return size;
    }

    void close()
    {
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&encoder);
        if (output != nullptr) {
            if (output->pb != nullptr) {
                av_freep(&output->pb->buffer);
                avio_context_free(&output->pb);
            }
            avformat_free_context(output);
            output = nullptr;
        }
        stream = nullptr;
        pending.clear();
        samplesSent = 0;
    }

    [[nodiscard]] std::expected<void, StreamError> open(const Track &track)
    {
        const Recipe recipe = recipeFor(spec.format);
        const AVCodec *codec = avcodec_find_encoder_by_name(recipe.encoder);
        if (codec == nullptr || avformat_alloc_output_context2(&output, nullptr, recipe.muxer, nullptr) < 0) {
            return std::unexpected(StreamError::EncoderMissing);
        }
        encoder = avcodec_alloc_context3(codec);
        stream = avformat_new_stream(output, nullptr);
        frame = av_frame_alloc();
        packet = av_packet_alloc();
        if (encoder == nullptr || stream == nullptr || frame == nullptr || packet == nullptr) {
            return std::unexpected(StreamError::EncoderFailed);
        }
        encoder->sample_rate = static_cast<int>(spec.sampleRate);
        encoder->sample_fmt = recipe.sampleFormat;
        av_channel_layout_default(&encoder->ch_layout, static_cast<int>(spec.channels));
        encoder->time_base = AVRational{1, static_cast<int>(spec.sampleRate)};
        encoder->bit_rate =
            static_cast<std::int64_t>(clampedBitrate(spec.format, spec.channels, spec.bitrateKbps)) * 1000;
        if ((output->oformat->flags & AVFMT_GLOBALHEADER) != 0) {
            encoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }
        // **A refusal here is the settings'**, not the moment's: trying again
        // with the same ones gets the same answer.
        if (avcodec_open2(encoder, codec, nullptr) < 0) {
            return std::unexpected(StreamError::UnsupportedSettings);
        }
        if (avcodec_parameters_from_context(stream->codecpar, encoder) < 0) {
            return std::unexpected(StreamError::EncoderFailed);
        }
        stream->time_base = encoder->time_base;
        // The title goes in the comment header of an Ogg stream; MP3 and
        // AAC carry none, and are titled by the server.
        if (isOgg(spec.format)) {
            for (const auto &[key, value] : {std::pair{"title", &track.title},
                                             std::pair{"artist", &track.artist},
                                             std::pair{"album", &track.album}}) {
                if (!value->empty()) {
                    av_dict_set(&stream->metadata, key, value->c_str(), 0);
                }
            }
        }

        frame->format = encoder->sample_fmt;
        frame->sample_rate = encoder->sample_rate;
        av_channel_layout_copy(&frame->ch_layout, &encoder->ch_layout);
        frame->nb_samples = encoder->frame_size > 0 ? encoder->frame_size : 1152;
        if (av_frame_get_buffer(frame, 0) < 0) {
            return std::unexpected(StreamError::EncoderFailed);
        }

        constexpr int kBufferBytes = 4096;
        auto *buffer = static_cast<unsigned char *>(av_malloc(kBufferBytes));
        if (buffer == nullptr) {
            return std::unexpected(StreamError::EncoderFailed);
        }
        output->pb = avio_alloc_context(buffer, kBufferBytes, 1, this, nullptr, &Private::written, nullptr);
        if (output->pb == nullptr) {
            av_free(buffer);
            return std::unexpected(StreamError::EncoderFailed);
        }
        // A stream, not a file: nothing goes back to fill in a length or an
        // index, and every packet is passed on as soon as it is made.
        output->pb->seekable = 0;
        output->flags |= AVFMT_FLAG_FLUSH_PACKETS;
        AVDictionary *options = nullptr;
        if (spec.format == StreamFormat::Mp3) {
            av_dict_set(&options, "id3v2_version", "0", 0);
            av_dict_set(&options, "write_xing", "0", 0);
        }
        if (isOgg(spec.format)) {
            // Half a second a page: a listener who tunes in hears sound
            // that soon, at a few bytes of overhead a page.
            av_dict_set(&options, "page_duration", "500000", 0);
        }
        const int header = avformat_write_header(output, &options);
        av_dict_free(&options);
        if (header < 0) {
            return std::unexpected(StreamError::EncoderFailed);
        }
        avio_flush(output->pb);
        return {};
    }

    void fill(int frames)
    {
        const int channels = encoder->ch_layout.nb_channels;
        frame->nb_samples = frames;
        if (encoder->sample_fmt == AV_SAMPLE_FMT_FLT) {
            std::memcpy(frame->data[0], pending.data(), static_cast<std::size_t>(frames * channels) * sizeof(float));
        } else {
            for (int channel = 0; channel < channels; ++channel) {
                auto *out = reinterpret_cast<float *>(frame->data[channel]);
                for (int index = 0; index < frames; ++index) {
                    out[index] = pending[static_cast<std::size_t>(index * channels + channel)];
                }
            }
        }
        pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(frames * channels));
        frame->pts = samplesSent;
        samplesSent += frames;
    }

    [[nodiscard]] bool send(AVFrame *input)
    {
        if (avcodec_send_frame(encoder, input) < 0) {
            return false;
        }
        for (;;) {
            const int received = avcodec_receive_packet(encoder, packet);
            if (received == AVERROR(EAGAIN) || received == AVERROR_EOF) {
                return true;
            }
            if (received < 0) {
                return false;
            }
            av_packet_rescale_ts(packet, encoder->time_base, stream->time_base);
            packet->stream_index = stream->index;
            const int muxed = av_write_frame(output, packet);
            av_packet_unref(packet);
            if (muxed < 0) {
                return false;
            }
        }
    }

    [[nodiscard]] std::expected<void, StreamError> encodePending()
    {
        const int channels = encoder->ch_layout.nb_channels;
        const int size = encoder->frame_size > 0 ? encoder->frame_size : 1152;
        while (pending.size() >= static_cast<std::size_t>(size * channels)) {
            if (av_frame_make_writable(frame) < 0) {
                return std::unexpected(StreamError::EncoderFailed);
            }
            fill(size);
            if (!send(frame)) {
                return std::unexpected(StreamError::EncoderFailed);
            }
        }
        avio_flush(output->pb);
        return {};
    }

    /// The last of the audio and the container's end, for an Ogg stream
    /// that another is about to follow.
    [[nodiscard]] std::expected<void, StreamError> finish()
    {
        if (output == nullptr) {
            return {};
        }
        const int channels = encoder->ch_layout.nb_channels;
        bool ok = true;
        if (!pending.empty()) {
            ok = av_frame_make_writable(frame) >= 0;
            if (ok) {
                fill(static_cast<int>(pending.size() / static_cast<std::size_t>(channels)));
                ok = send(frame);
            }
        }
        ok = ok && send(nullptr);
        ok = ok && av_write_trailer(output) >= 0;
        if (output->pb != nullptr) {
            avio_flush(output->pb);
        }
        close();
        if (!ok) {
            return std::unexpected(StreamError::EncoderFailed);
        }
        return {};
    }
};

StreamEncoder::StreamEncoder()
    : d(std::make_unique<Private>())
{
}

StreamEncoder::~StreamEncoder() = default;

std::expected<std::unique_ptr<StreamEncoder>, StreamError> StreamEncoder::create(const Spec &spec)
{
    if (streamFormatUnavailable(spec.format)) {
        return std::unexpected(StreamError::EncoderMissing);
    }
    std::unique_ptr<StreamEncoder> encoder{new StreamEncoder};
    encoder->d->spec = spec;
    encoder->d->spec.channels = std::clamp(spec.channels, 1U, 2U);
    if (const std::expected<void, StreamError> opened = encoder->d->open(spec.track); !opened) {
        return std::unexpected(opened.error());
    }
    return encoder;
}

std::expected<void, StreamError> StreamEncoder::encode(std::span<const float> samples)
{
    if (d->output == nullptr) {
        return std::unexpected(StreamError::EncoderFailed);
    }
    d->pending.insert(d->pending.end(), samples.begin(), samples.end());
    return d->encodePending();
}

std::expected<void, StreamError> StreamEncoder::restart(const Track &track)
{
    d->spec.track = track;
    if (!isOgg(d->spec.format)) {
        return {};
    }
    if (const std::expected<void, StreamError> finished = d->finish(); !finished) {
        return finished;
    }
    return d->open(track);
}

std::vector<char> StreamEncoder::take()
{
    return std::exchange(d->bytes, {});
}

const StreamEncoder::Spec &StreamEncoder::spec() const noexcept
{
    return d->spec;
}

} // namespace rb::stream
