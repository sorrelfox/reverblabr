// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "record/m4afile.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <format>
#include <system_error>
#include <vector>

namespace rb::record
{

namespace
{

/// The muxer for .m4a: MP4 as Apple's players expect it.
constexpr const char *kMuxer = "ipod";

/// FFmpeg's own encoders, by name. **Never libfdk_aac**, which some systems
/// have and which may not be distributed with this program, and never
/// HE-AAC: a file this writes is one anybody may make.
constexpr const char *kAacEncoder = "aac";
constexpr const char *kAlacEncoder = "alac";

/// A fragment a second, which is what a crash can cost.
constexpr const char *kFragmentMicroseconds = "1000000";

[[nodiscard]] bool isAac(FileFormat format)
{
    return format == FileFormat::AacM4a;
}

/// Whether FFmpeg will encode with \a name at 48 kHz stereo in \a sampleFormat.
[[nodiscard]] bool opens(const char *name, AVSampleFormat sampleFormat)
{
    const AVCodec *codec = avcodec_find_encoder_by_name(name);
    if (codec == nullptr || av_guess_format(kMuxer, nullptr, nullptr) == nullptr) {
        return false;
    }
    AVCodecContext *context = avcodec_alloc_context3(codec);
    if (context == nullptr) {
        return false;
    }
    context->sample_rate = 48000;
    context->sample_fmt = sampleFormat;
    av_channel_layout_default(&context->ch_layout, 2);
    context->bit_rate = sampleFormat == AV_SAMPLE_FMT_FLTP ? 256000 : 0;
    context->time_base = AVRational{1, 48000};
    const bool opened = avcodec_open2(context, codec, nullptr) >= 0;
    avcodec_free_context(&context);
    return opened;
}

[[nodiscard]] AVSampleFormat sampleFormatFor(const FileSpec &spec)
{
    if (isAac(spec.format)) {
        return AV_SAMPLE_FMT_FLTP;
    }
    return spec.sampleFormat == SampleFormat::Int16 ? AV_SAMPLE_FMT_S16P : AV_SAMPLE_FMT_S32P;
}

[[nodiscard]] FileError errorFromErrno(int error)
{
    return error == ENOSPC || error == EDQUOT ? FileError::DiskFull : FileError::WriteFailed;
}

/// Copies the fragmented file at \a from into a regular MP4 at \a to.
///
/// **The encoder's priming goes back where a player looks for it.** An AAC
/// encoder puts \a primingSamples of silence in front of the first sample,
/// and says so with timestamps that start below zero; a fragmented file has
/// nowhere to keep that, so the copy starts its timestamps that far below
/// zero again, and the muxer writes the edit list that tells a player to
/// skip them.
[[nodiscard]] bool
remux(const std::filesystem::path &from, const std::filesystem::path &to, int primingSamples, int rate)
{
    AVFormatContext *input = nullptr;
    if (avformat_open_input(&input, from.c_str(), nullptr, nullptr) < 0) {
        return false;
    }
    bool ok = avformat_find_stream_info(input, nullptr) >= 0;
    AVFormatContext *output = nullptr;
    ok = ok && avformat_alloc_output_context2(&output, nullptr, kMuxer, to.c_str()) >= 0;
    for (unsigned index = 0; ok && index < input->nb_streams; ++index) {
        AVStream *stream = avformat_new_stream(output, nullptr);
        ok = stream != nullptr && avcodec_parameters_copy(stream->codecpar, input->streams[index]->codecpar) >= 0;
        if (ok) {
            stream->codecpar->codec_tag = 0;
            stream->time_base = input->streams[index]->time_base;
        }
    }
    ok = ok && avio_open(&output->pb, to.c_str(), AVIO_FLAG_WRITE) >= 0;
    ok = ok && avformat_write_header(output, nullptr) >= 0;
    AVPacket *packet = av_packet_alloc();
    while (ok && av_read_frame(input, packet) >= 0) {
        const AVRational from = input->streams[packet->stream_index]->time_base;
        const std::int64_t shift = av_rescale_q(primingSamples, AVRational{1, rate}, from);
        if (packet->pts != AV_NOPTS_VALUE) {
            packet->pts -= shift;
        }
        if (packet->dts != AV_NOPTS_VALUE) {
            packet->dts -= shift;
        }
        av_packet_rescale_ts(packet, from, output->streams[packet->stream_index]->time_base);
        packet->pos = -1;
        ok = av_interleaved_write_frame(output, packet) >= 0;
        av_packet_unref(packet);
    }
    av_packet_free(&packet);
    ok = ok && av_write_trailer(output) >= 0;
    if (output != nullptr) {
        if (output->pb != nullptr) {
            avio_closep(&output->pb);
        }
        avformat_free_context(output);
    }
    avformat_close_input(&input);
    return ok;
}

} // namespace

const EncoderProbe &probeEncoders()
{
    static const EncoderProbe probe = [] {
        // FFmpeg talks to stderr about its encoders' statistics; the engine
        // has its own logging, and what matters comes back as errors.
        av_log_set_level(AV_LOG_ERROR);
        EncoderProbe found;
        found.aac = opens(kAacEncoder, AV_SAMPLE_FMT_FLTP);
        found.alac = opens(kAlacEncoder, AV_SAMPLE_FMT_S16P);
        return found;
    }();
    return probe;
}

std::optional<std::string_view> formatUnavailable(FileFormat format, const EncoderProbe &probe)
{
    if ((format == FileFormat::AacM4a && !probe.aac) || (format == FileFormat::AlacM4a && !probe.alac)) {
        return "encoderMissing";
    }
    return std::nullopt;
}

struct M4aFile::Private {
    std::filesystem::path path;
    FileSpec spec;
    AVFormatContext *output = nullptr;
    AVCodecContext *encoder = nullptr;
    AVStream *stream = nullptr;
    AVFrame *frame = nullptr;
    AVPacket *packet = nullptr;
    /// Frames waiting for a whole encoder frame, interleaved.
    std::vector<float> pending;
    std::int64_t samplesSent = 0;
    std::uint64_t noise = 0;
    /// The silence the encoder puts in front, which a player is told to skip.
    int primingSamples = 0;

    ~Private()
    {
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&encoder);
        if (output != nullptr) {
            if (output->pb != nullptr) {
                avio_closep(&output->pb);
            }
            avformat_free_context(output);
        }
    }

    [[nodiscard]] float nextNoise()
    {
        noise ^= noise >> 12;
        noise ^= noise << 25;
        noise ^= noise >> 27;
        return static_cast<float>((noise * 0x2545F4914F6CDD1DULL) >> 40) * (1.0f / 16777216.0f);
    }

    /// The encoder's samples for one frame of \a frames, from the front of
    /// the pending frames.
    void fill(int frames)
    {
        const int channels = encoder->ch_layout.nb_channels;
        frame->nb_samples = frames;
        const int bits = encoder->sample_fmt == AV_SAMPLE_FMT_S16P ? 16 : 24;
        const double scale = std::ldexp(1.0, bits - 1);
        for (int channel = 0; channel < channels; ++channel) {
            for (int index = 0; index < frames; ++index) {
                const float sample = pending[static_cast<std::size_t>(index * channels + channel)];
                if (encoder->sample_fmt == AV_SAMPLE_FMT_FLTP) {
                    reinterpret_cast<float *>(frame->data[channel])[index] = sample;
                    continue;
                }
                const double dither = spec.dither ? static_cast<double>(nextNoise()) - nextNoise() : 0.0;
                const double value = std::clamp(std::round(sample * scale + dither), -scale, scale - 1.0);
                if (encoder->sample_fmt == AV_SAMPLE_FMT_S16P) {
                    reinterpret_cast<std::int16_t *>(frame->data[channel])[index] = static_cast<std::int16_t>(value);
                } else {
                    // 24 bits at the top of a 32-bit sample, which is how the
                    // ALAC encoder takes them.
                    reinterpret_cast<std::int32_t *>(frame->data[channel])[index] =
                        static_cast<std::int32_t>(static_cast<std::int64_t>(value) * 256);
                }
            }
        }
        pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(frames * channels));
        frame->pts = samplesSent;
        samplesSent += frames;
    }

    /// Hands \a input (or nothing, to drain) to the encoder and writes what
    /// comes out.
    [[nodiscard]] bool encode(AVFrame *input)
    {
        errno = 0;
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
            const int written = av_interleaved_write_frame(output, packet);
            av_packet_unref(packet);
            if (written < 0) {
                return false;
            }
        }
    }
};

M4aFile::M4aFile()
    : d(std::make_unique<Private>())
{
}

M4aFile::~M4aFile() = default;

std::expected<std::unique_ptr<M4aFile>, FileError> M4aFile::create(const std::filesystem::path &path,
                                                                   const FileSpec &spec)
{
    if (formatUnavailable(spec.format)) {
        return std::unexpected(FileError::EncoderMissing);
    }
    // **O_EXCL here too**: FFmpeg would open whatever is at the path and
    // write over it, so the name is claimed first.
    const int claimed = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (claimed < 0) {
        return std::unexpected(errno == EEXIST ? FileError::FileExists : FileError::FileNotCreated);
    }
    ::close(claimed);

    std::unique_ptr<M4aFile> file{new M4aFile};
    Private &d = *file->d;
    d.path = path;
    d.spec = spec;
    d.noise = spec.ditherSeed == 0 ? 1 : spec.ditherSeed;
    const auto refuse = [&path](FileError error) {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        return std::unexpected(error);
    };

    const AVCodec *codec = avcodec_find_encoder_by_name(isAac(spec.format) ? kAacEncoder : kAlacEncoder);
    if (codec == nullptr || avformat_alloc_output_context2(&d.output, nullptr, kMuxer, path.c_str()) < 0) {
        return refuse(FileError::EncoderMissing);
    }
    d.encoder = avcodec_alloc_context3(codec);
    d.stream = avformat_new_stream(d.output, nullptr);
    d.frame = av_frame_alloc();
    d.packet = av_packet_alloc();
    if (d.encoder == nullptr || d.stream == nullptr || d.frame == nullptr || d.packet == nullptr) {
        return refuse(FileError::FileNotCreated);
    }
    d.encoder->sample_rate = static_cast<int>(spec.sampleRate);
    d.encoder->sample_fmt = sampleFormatFor(spec);
    av_channel_layout_default(&d.encoder->ch_layout, static_cast<int>(spec.channels));
    d.encoder->time_base = AVRational{1, static_cast<int>(spec.sampleRate)};
    if (isAac(spec.format)) {
        // The rate asked for, for the whole file.
        d.encoder->bit_rate =
            static_cast<std::int64_t>(std::clamp(spec.bitrateKbps, kMinBitrateKbps, kMaxBitrateKbps)) * 1000;
    } else if (d.encoder->sample_fmt == AV_SAMPLE_FMT_S32P) {
        d.encoder->bits_per_raw_sample = 24;
    }
    if ((d.output->oformat->flags & AVFMT_GLOBALHEADER) != 0) {
        d.encoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    if (avcodec_open2(d.encoder, codec, nullptr) < 0) {
        return refuse(FileError::UnsupportedFormat);
    }
    d.primingSamples = d.encoder->initial_padding;
    if (avcodec_parameters_from_context(d.stream->codecpar, d.encoder) < 0) {
        return refuse(FileError::UnsupportedFormat);
    }
    d.stream->time_base = d.encoder->time_base;

    d.frame->format = d.encoder->sample_fmt;
    d.frame->sample_rate = d.encoder->sample_rate;
    av_channel_layout_copy(&d.frame->ch_layout, &d.encoder->ch_layout);
    d.frame->nb_samples = d.encoder->frame_size > 0 ? d.encoder->frame_size : 1024;
    if (av_frame_get_buffer(d.frame, 0) < 0) {
        return refuse(FileError::FileNotCreated);
    }

    if (avio_open(&d.output->pb, path.c_str(), AVIO_FLAG_WRITE) < 0) {
        return refuse(FileError::FileNotCreated);
    }
    // **Fragments, not one index at the end**: empty_moov puts an index
    // with no samples in it at the front, and each fragment after it
    // carries its own, so every fragment that reached the disk plays.
    AVDictionary *options = nullptr;
    av_dict_set(&options, "movflags", "empty_moov+default_base_moof", 0);
    av_dict_set(&options, "frag_duration", kFragmentMicroseconds, 0);
    const int header = avformat_write_header(d.output, &options);
    av_dict_free(&options);
    if (header < 0) {
        return refuse(FileError::FileNotCreated);
    }
    return file;
}

std::expected<void, FileError> M4aFile::write(std::span<const float> samples)
{
    d->pending.insert(d->pending.end(), samples.begin(), samples.end());
    const int channels = d->encoder->ch_layout.nb_channels;
    const int size = d->encoder->frame_size > 0 ? d->encoder->frame_size : 1024;
    while (d->pending.size() >= static_cast<std::size_t>(size * channels)) {
        if (av_frame_make_writable(d->frame) < 0) {
            return std::unexpected(FileError::WriteFailed);
        }
        d->fill(size);
        if (!d->encode(d->frame)) {
            return std::unexpected(errorFromErrno(errno));
        }
    }
    return {};
}

void M4aFile::flush()
{
    if (d->output != nullptr && d->output->pb != nullptr) {
        avio_flush(d->output->pb);
    }
}

std::expected<void, FileError> M4aFile::close()
{
    if (d->output == nullptr) {
        return {};
    }
    const int channels = d->encoder->ch_layout.nb_channels;
    bool ok = true;
    // The last frames, which make a short last frame of their own.
    if (!d->pending.empty()) {
        ok = av_frame_make_writable(d->frame) >= 0;
        if (ok) {
            d->fill(static_cast<int>(d->pending.size() / static_cast<std::size_t>(channels)));
            ok = d->encode(d->frame);
        }
    }
    ok = ok && d->encode(nullptr);
    ok = ok && av_write_trailer(d->output) >= 0;
    const int error = errno;
    avio_closep(&d->output->pb);
    avformat_free_context(d->output);
    d->output = nullptr;
    if (!ok) {
        return std::unexpected(errorFromErrno(error));
    }

    // **A regular MP4 in its place**, for the players that are fussy about
    // fragments. The fragmented file plays, so a copy that fails keeps it.
    static std::atomic<unsigned> s_copies{0};
    std::filesystem::path copy = d->path;
    copy += std::format(".{}-{}.remux.m4a", ::getpid(), s_copies.fetch_add(1));
    std::error_code ignored;
    if (remux(d->path, copy, d->primingSamples, static_cast<int>(d->spec.sampleRate))) {
        std::filesystem::rename(copy, d->path, ignored);
    }
    std::filesystem::remove(copy, ignored);
    return {};
}

} // namespace rb::record
