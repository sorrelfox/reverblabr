// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

// A stream's bytes read back as a player reads them: FFmpeg finds the
// format, decodes every frame, and says what it heard.

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/mem.h>
}

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace rb::test
{

struct Decoded {
    bool opened = false;
    std::string codec;
    int sampleRate = 0;
    int channels = 0;
    double seconds = 0.0;
    float peak = 0.0f;
};

namespace detail
{

struct Reader {
    const std::vector<char> *bytes = nullptr;
    std::size_t at = 0;

    static int read(void *opaque, std::uint8_t *out, int size)
    {
        auto *self = static_cast<Reader *>(opaque);
        const std::size_t left = self->bytes->size() - self->at;
        if (left == 0) {
            return AVERROR_EOF;
        }
        const std::size_t count = std::min<std::size_t>(left, static_cast<std::size_t>(size));
        std::memcpy(out, self->bytes->data() + self->at, count);
        self->at += count;
        return static_cast<int>(count);
    }
};

} // namespace detail

[[nodiscard]] inline Decoded decode(const std::vector<char> &bytes)
{
    Decoded result;
    detail::Reader reader{&bytes, 0};
    constexpr int kBufferBytes = 4096;
    auto *buffer = static_cast<unsigned char *>(av_malloc(kBufferBytes));
    AVIOContext *io = avio_alloc_context(buffer, kBufferBytes, 0, &reader, &detail::Reader::read, nullptr, nullptr);
    AVFormatContext *input = avformat_alloc_context();
    input->pb = io;
    if (avformat_open_input(&input, nullptr, nullptr, nullptr) < 0) {
        av_freep(&io->buffer);
        avio_context_free(&io);
        return result;
    }
    if (avformat_find_stream_info(input, nullptr) < 0 || input->nb_streams == 0) {
        avformat_close_input(&input);
        av_freep(&io->buffer);
        avio_context_free(&io);
        return result;
    }
    const AVCodecParameters *parameters = input->streams[0]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(parameters->codec_id);
    AVCodecContext *decoder = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(decoder, parameters);
    if (codec == nullptr || avcodec_open2(decoder, codec, nullptr) < 0) {
        avcodec_free_context(&decoder);
        avformat_close_input(&input);
        av_freep(&io->buffer);
        avio_context_free(&io);
        return result;
    }
    result.opened = true;
    result.codec = avcodec_get_name(parameters->codec_id);
    result.sampleRate = decoder->sample_rate;
    result.channels = decoder->ch_layout.nb_channels;

    std::int64_t samples = 0;
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    const auto take = [&] {
        while (avcodec_receive_frame(decoder, frame) >= 0) {
            samples += frame->nb_samples;
            const bool planar = av_sample_fmt_is_planar(static_cast<AVSampleFormat>(frame->format)) != 0;
            const int count = planar ? frame->nb_samples : frame->nb_samples * frame->ch_layout.nb_channels;
            const auto format = static_cast<AVSampleFormat>(frame->format);
            for (int index = 0; index < count; ++index) {
                float value = 0.0f;
                if (format == AV_SAMPLE_FMT_FLTP || format == AV_SAMPLE_FMT_FLT) {
                    value = reinterpret_cast<const float *>(frame->data[0])[index];
                } else if (format == AV_SAMPLE_FMT_S16P || format == AV_SAMPLE_FMT_S16) {
                    value = reinterpret_cast<const std::int16_t *>(frame->data[0])[index] / 32768.0f;
                }
                result.peak = std::max(result.peak, std::fabs(value));
            }
        }
    };
    while (av_read_frame(input, packet) >= 0) {
        if (packet->stream_index == 0 && avcodec_send_packet(decoder, packet) >= 0) {
            take();
        }
        av_packet_unref(packet);
    }
    avcodec_send_packet(decoder, nullptr);
    take();
    if (result.sampleRate > 0) {
        result.seconds = static_cast<double>(samples) / result.sampleRate;
    }
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&decoder);
    avformat_close_input(&input);
    av_freep(&io->buffer);
    avio_context_free(&io);
    return result;
}

} // namespace rb::test
