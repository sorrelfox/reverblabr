// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// M4A recordings: AAC-LC and ALAC through the system's FFmpeg, read
// back through FFmpeg — which is what VLC, mpv, the KDE players and most
// others on Linux decode them with. Each test is skipped, and says so, on
// a system whose FFmpeg has no encoder for its format.

#include "record/audiofile.h"
#include "record/m4afile.h"
#include "record/repair.h"
#include "record/tags.h"
#include "recordfixtures.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
}

#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <csignal>
#include <fstream>
#include <iterator>

using namespace rb::record;
using namespace rb::recordtest;

namespace
{

/// A file as FFmpeg decodes it: interleaved floats.
struct Decoded {
    bool opened = false;
    int sampleRate = 0;
    int channels = 0;
    std::string codec;
    /// How long the file says it is, in frames: what a player shows.
    std::int64_t containerFrames = 0;
    std::vector<float> samples;

    [[nodiscard]] std::size_t frames() const
    {
        return channels == 0 ? 0 : samples.size() / static_cast<std::size_t>(channels);
    }
};

[[nodiscard]] float sampleAt(const AVFrame *frame, int channel, int index)
{
    switch (static_cast<AVSampleFormat>(frame->format)) {
    case AV_SAMPLE_FMT_FLTP:
        return reinterpret_cast<const float *>(frame->data[channel])[index];
    case AV_SAMPLE_FMT_S16P:
        return static_cast<float>(reinterpret_cast<const std::int16_t *>(frame->data[channel])[index]) / 32768.0f;
    case AV_SAMPLE_FMT_S32P:
        return static_cast<float>(reinterpret_cast<const std::int32_t *>(frame->data[channel])[index] / 256)
            / 8388608.0f;
    default:
        return 0.0f;
    }
}

[[nodiscard]] Decoded decode(const std::filesystem::path &path)
{
    Decoded decoded;
    AVFormatContext *input = nullptr;
    if (avformat_open_input(&input, path.c_str(), nullptr, nullptr) < 0) {
        return decoded;
    }
    if (avformat_find_stream_info(input, nullptr) < 0 || input->nb_streams != 1) {
        avformat_close_input(&input);
        return decoded;
    }
    const AVCodecParameters *parameters = input->streams[0]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(parameters->codec_id);
    AVCodecContext *decoder = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(decoder, parameters);
    if (avcodec_open2(decoder, codec, nullptr) < 0) {
        avcodec_free_context(&decoder);
        avformat_close_input(&input);
        return decoded;
    }
    decoded.opened = true;
    decoded.sampleRate = decoder->sample_rate;
    decoded.channels = decoder->ch_layout.nb_channels;
    decoded.codec = codec->name;
    decoded.containerFrames =
        av_rescale_q(input->streams[0]->duration, input->streams[0]->time_base, AVRational{1, decoder->sample_rate});
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    const auto drain = [&] {
        while (avcodec_receive_frame(decoder, frame) >= 0) {
            // **The priming an AAC encoder puts in front is trimmed** the way
            // a player trims it, from the file's own edit list.
            int skip = 0;
            if (frame->pts != AV_NOPTS_VALUE && frame->pts < 0) {
                skip = static_cast<int>(std::min<std::int64_t>(-frame->pts, frame->nb_samples));
            }
            for (int index = skip; index < frame->nb_samples; ++index) {
                for (int channel = 0; channel < decoded.channels; ++channel) {
                    decoded.samples.push_back(sampleAt(frame, channel, index));
                }
            }
        }
    };
    while (av_read_frame(input, packet) >= 0) {
        avcodec_send_packet(decoder, packet);
        av_packet_unref(packet);
        drain();
    }
    avcodec_send_packet(decoder, nullptr);
    drain();
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&decoder);
    avformat_close_input(&input);
    return decoded;
}

/// Whether the file holds a box of this name anywhere, which is enough to
/// tell a fragmented MP4 (moof) from a regular one.
[[nodiscard]] bool holds(const std::filesystem::path &path, std::string_view box)
{
    std::ifstream file(path, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return bytes.find(box) != std::string::npos;
}

[[nodiscard]] bool encoderFor(FileFormat format)
{
    return !formatUnavailable(format).has_value();
}

#define SKIP_WITHOUT(format)                                                                                           \
    if (!encoderFor(format)) {                                                                                         \
        GTEST_SKIP() << "this system's FFmpeg has no encoder for " << fileFormatId(format)                             \
                     << ", which is what the Recorder says too";                                                       \
    }

[[nodiscard]] std::filesystem::path
write(const ScratchFolder &folder, FileFormat format, SampleFormat sample, const std::vector<float> &samples)
{
    const std::filesystem::path path = folder / std::format("take-{}.m4a", static_cast<int>(sample));
    FileSpec spec;
    spec.format = format;
    spec.sampleFormat = sample;
    spec.dither = false;
    std::expected<AudioFileWriter, FileError> writer = AudioFileWriter::create(path, spec);
    EXPECT_TRUE(writer.has_value()) << (writer ? "" : fileErrorName(writer.error()));
    if (!writer) {
        return path;
    }
    for (std::size_t at = 0; at < samples.size(); at += 2 * 1000) {
        EXPECT_TRUE(writer->write(
            std::span<const float>{samples}.subspan(at, std::min<std::size_t>(2000, samples.size() - at))));
    }
    EXPECT_TRUE(writer->close().has_value());
    return path;
}

} // namespace

TEST(M4a, AacComesBackAtItsRateLengthAndLevelAsARegularMp4)
{
    SKIP_WITHOUT(FileFormat::AacM4a);
    ScratchFolder folder;
    const std::filesystem::path path =
        write(folder, FileFormat::AacM4a, SampleFormat::Int24, stereoSine(72000, 1000.0, 0.5f));
    const Decoded file = decode(path);
    ASSERT_TRUE(file.opened);
    EXPECT_EQ(file.codec, "aac");
    EXPECT_EQ(file.sampleRate, 48000);
    EXPECT_EQ(file.channels, 2);
    // **Exactly as long as what was recorded**, by the file's own account:
    // the encoder's priming in front is marked to be skipped, and the
    // padding of its last frame is past the end.
    EXPECT_EQ(file.containerFrames, 72000);
    EXPECT_NEAR(static_cast<double>(file.frames()), 72000.0, 1024.0);
    EXPECT_NEAR(rmsDb(file.samples, 2, 0, 12000, 60000), -9.03, 0.3);
    EXPECT_NEAR(rmsDb(file.samples, 2, 1, 12000, 60000), -9.03, 0.3);
    // **Regular, not fragmented, once it is finished**: the fussy players
    // get the MP4 they expect.
    EXPECT_FALSE(holds(path, "moof"));
}

TEST(M4a, AlacComesBackSampleForSampleAt16And24Bits)
{
    SKIP_WITHOUT(FileFormat::AlacM4a);
    for (const SampleFormat sample : {SampleFormat::Int16, SampleFormat::Int24}) {
        SCOPED_TRACE(static_cast<int>(sample));
        ScratchFolder folder;
        // A signal already on the steps of both depths, so lossless means
        // exactly equal.
        std::vector<float> steps(2 * 30000);
        for (std::size_t index = 0; index < steps.size(); ++index) {
            steps[index] = static_cast<float>(static_cast<int>(index % 20001) - 10000) / 32768.0f;
        }
        const std::filesystem::path path = write(folder, FileFormat::AlacM4a, sample, steps);
        const Decoded file = decode(path);
        ASSERT_TRUE(file.opened);
        EXPECT_EQ(file.codec, "alac");
        EXPECT_EQ(file.sampleRate, 48000);
        EXPECT_EQ(file.frames(), 30000U);
        EXPECT_EQ(file.samples, steps);
        EXPECT_FALSE(holds(path, "moof"));
    }
}

// **Killed in the middle, it plays** up to its last fragment, a second at a
// time, with nothing to repair: the index of each fragment is in the
// fragment.
TEST(M4a, AKilledRecordingPlaysToItsLastFragment)
{
    SKIP_WITHOUT(FileFormat::AacM4a);
    ScratchFolder folder;
    const std::filesystem::path path = folder / "killed.m4a";
    std::array<int, 2> pipe{};
    ASSERT_EQ(::pipe(pipe.data()), 0);
    const pid_t child = ::fork();
    ASSERT_GE(child, 0);
    if (child == 0) {
        ::close(pipe[0]);
        FileSpec spec;
        spec.format = FileFormat::AacM4a;
        static std::optional<AudioFileWriter> writer; // never closed: the process dies
        std::expected<AudioFileWriter, FileError> made = AudioFileWriter::create(path, spec);
        bool ok = made.has_value();
        if (ok) {
            writer.emplace(std::move(*made));
            const std::vector<float> take = stereoSine(48000 * 3 + 12000, 1000.0, 0.5f);
            for (std::size_t at = 0; ok && at < take.size(); at += 2 * 4800) {
                ok =
                    writer
                        ->write(std::span<const float>{take}.subspan(at, std::min<std::size_t>(9600, take.size() - at)))
                        .has_value();
            }
            writer->refreshHeader();
        }
        const char done = ok ? 'y' : 'n';
        (void)!::write(pipe[1], &done, 1);
        for (;;) {
            ::pause();
        }
    }
    ::close(pipe[1]);
    char done = 0;
    ASSERT_EQ(::read(pipe[0], &done, 1), 1);
    ::close(pipe[0]);
    ::kill(child, SIGKILL);
    ::waitpid(child, nullptr, 0);
    ASSERT_EQ(done, 'y');

    EXPECT_TRUE(holds(path, "moof")) << "the file was not being written in fragments";
    const Decoded file = decode(path);
    ASSERT_TRUE(file.opened) << "a killed recording does not open";
    std::cout << "killed after 3.25 s: " << static_cast<double>(file.frames()) / 48000.0 << " s play\n";
    EXPECT_GE(file.frames(), 3U * 48000U - 2048U);
    EXPECT_LE(file.frames(), 48000U * 3U + 12000U);
    EXPECT_NEAR(rmsDb(file.samples, 2, 0, 12000, 96000), -9.03, 0.3);
    // Nothing to repair, and repairing does no harm.
    const std::expected<RepairResult, RepairError> repaired = repairFile(path);
    ASSERT_TRUE(repaired.has_value());
    EXPECT_FALSE(repaired->changed);
}

TEST(M4a, TagsRoundTripInCyrillicAndCjk)
{
    for (const FileFormat format : {FileFormat::AacM4a, FileFormat::AlacM4a}) {
        if (!encoderFor(format)) {
            continue;
        }
        SCOPED_TRACE(fileFormatId(format));
        ScratchFolder folder;
        const std::filesystem::path path = write(folder, format, SampleFormat::Int16, stereoSine(24000, 440.0, 0.3f));
        const std::size_t frames = decode(path).frames();
        Tags tags;
        tags.title = "Выпуск 12 — 東京からの手紙";
        tags.artist = "Анна и 李明";
        tags.album = "Podcast";
        tags.genre = "Speech";
        tags.year = "2026";
        tags.track = "3";
        tags.trackCount = "12";
        tags.disc = "1";
        tags.discCount = "2";
        tags.comment = "録音 — запись";
        ASSERT_TRUE(writeTags(path, format, tags));
        EXPECT_EQ(readTags(path, format), tags);
        EXPECT_EQ(decode(path).frames(), frames) << "tagging changed the audio";
    }
}

// **Only what works is offered**: an encoder is judged by whether it
// opens, and a format without one says why.
TEST(M4a, AFormatWithoutAnEncoderIsUnavailableAndSaysWhy)
{
    const EncoderProbe none;
    EXPECT_EQ(formatUnavailable(FileFormat::AacM4a, none), "encoderMissing");
    EXPECT_EQ(formatUnavailable(FileFormat::AlacM4a, none), "encoderMissing");
    EXPECT_FALSE(formatUnavailable(FileFormat::Flac, none).has_value());
    EXPECT_FALSE(formatUnavailable(FileFormat::Opus, none).has_value());
    const EncoderProbe both{true, true};
    EXPECT_FALSE(formatUnavailable(FileFormat::AacM4a, both).has_value());
    std::cout << "this system: aac " << (probeEncoders().aac ? "yes" : "no") << ", alac "
              << (probeEncoders().alac ? "yes" : "no") << "\n";
}
