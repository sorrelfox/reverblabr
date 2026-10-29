// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

// An RTMP server for a test to stream to: FFmpeg's own, in listening mode,
// on a thread, taking one stream and reading it as a player would. What it
// read is there to be looked at: the streams' kinds and sizes, and every
// packet's time.

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace rb::test
{

/// A port nothing listens on, on the loopback.
[[nodiscard]] inline std::uint16_t freeRtmpPort()
{
    std::mt19937 random(static_cast<unsigned>(::getpid()) ^ static_cast<unsigned>(std::random_device{}()));
    std::uniform_int_distribution<int> pick(20000, 60000);
    for (int attempt = 0; attempt < 200; ++attempt) {
        const int port = pick(random);
        const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(static_cast<std::uint16_t>(port));
        const bool ok = ::bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof address) == 0;
        ::close(fd);
        if (ok) {
            return static_cast<std::uint16_t>(port);
        }
    }
    return 0;
}

/// What a stream turned out to hold.
struct ReceivedStream {
    bool opened = false;
    AVCodecID videoCodec = AV_CODEC_ID_NONE;
    AVCodecID audioCodec = AV_CODEC_ID_NONE;
    int width = 0;
    int height = 0;
    int sampleRate = 0;
    bool videoHeaders = false;
    /// Every packet's time, in seconds, by kind.
    std::vector<double> videoTimes;
    std::vector<double> audioTimes;
    /// When each audio packet arrived here.
    std::vector<std::chrono::steady_clock::time_point> audioArrivals;
    std::uint64_t keyFrames = 0;
};

class RtmpListener
{
public:
    /// Listens on \a port for a stream published to /\a app/\a key.
    RtmpListener(std::uint16_t port, std::string app, std::string key)
        : RtmpListener("rtmp://127.0.0.1:" + std::to_string(port) + "/" + app + "/" + key, true)
    {
    }

    /// Reads the stream at \a url: listening for its publisher, or as a
    /// player of a server's stream.
    RtmpListener(std::string url, bool listen)
        : m_url(std::move(url))
        , m_listen(listen)
        , m_thread([this] { run(); })
    {
    }

    ~RtmpListener()
    {
        m_stopping = true;
        m_thread.join();
    }

    RtmpListener(const RtmpListener &) = delete;
    RtmpListener &operator=(const RtmpListener &) = delete;

    [[nodiscard]] ReceivedStream received() const
    {
        const std::lock_guard lock(m_mutex);
        return m_received;
    }

    /// Whether the publisher has gone and the stream ended.
    [[nodiscard]] bool ended() const
    {
        return m_ended;
    }

private:
    static int interrupted(void *opaque)
    {
        return static_cast<RtmpListener *>(opaque)->m_stopping ? 1 : 0;
    }

    void run()
    {
        AVFormatContext *input = avformat_alloc_context();
        input->interrupt_callback = AVIOInterruptCB{&RtmpListener::interrupted, this};
        AVDictionary *options = nullptr;
        if (m_listen) {
            av_dict_set(&options, "listen", "1", 0);
        }
        const int opened = avformat_open_input(&input, m_url.c_str(), nullptr, &options);
        av_dict_free(&options);
        if (opened < 0) {
            m_ended = true;
            return;
        }
        (void)avformat_find_stream_info(input, nullptr);
        {
            const std::lock_guard lock(m_mutex);
            m_received.opened = true;
            for (unsigned index = 0; index < input->nb_streams; ++index) {
                const AVCodecParameters *codec = input->streams[index]->codecpar;
                if (codec->codec_type == AVMEDIA_TYPE_VIDEO) {
                    m_received.videoCodec = codec->codec_id;
                    m_received.width = codec->width;
                    m_received.height = codec->height;
                    m_received.videoHeaders = codec->extradata_size > 0;
                } else if (codec->codec_type == AVMEDIA_TYPE_AUDIO) {
                    m_received.audioCodec = codec->codec_id;
                    m_received.sampleRate = codec->sample_rate;
                }
            }
        }
        AVPacket *packet = av_packet_alloc();
        while (!m_stopping && av_read_frame(input, packet) >= 0) {
            const AVStream *stream = input->streams[packet->stream_index];
            const double time =
                packet->pts == AV_NOPTS_VALUE ? -1.0 : static_cast<double>(packet->pts) * av_q2d(stream->time_base);
            {
                const std::lock_guard lock(m_mutex);
                if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                    m_received.videoTimes.push_back(time);
                    if ((packet->flags & AV_PKT_FLAG_KEY) != 0) {
                        ++m_received.keyFrames;
                    }
                } else {
                    m_received.audioTimes.push_back(time);
                    m_received.audioArrivals.push_back(std::chrono::steady_clock::now());
                }
            }
            av_packet_unref(packet);
        }
        av_packet_free(&packet);
        avformat_close_input(&input);
        m_ended = true;
    }

    std::string m_url;
    bool m_listen = true;
    mutable std::mutex m_mutex;
    ReceivedStream m_received;
    std::atomic<bool> m_stopping{false};
    std::atomic<bool> m_ended{false};
    std::thread m_thread;
};

} // namespace rb::test
