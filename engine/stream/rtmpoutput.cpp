// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "stream/rtmpoutput.h"

#include "stream/connection.h"
#include "stream/text.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
}

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <system_error>
#include <thread>
#include <vector>

namespace rb::stream
{

namespace
{

using Clock = std::chrono::steady_clock;

/// How long the last word to the server may take when a stream ends of
/// its own accord.
constexpr std::chrono::milliseconds kGoodbye{1000};

[[nodiscard]] std::string withBrackets(const std::string &host)
{
    return host.find(':') != std::string::npos ? "[" + host + "]" : host;
}

[[nodiscard]] StreamError errorFor(int code) noexcept
{
    if (code == AVERROR(ECONNREFUSED) || code == AVERROR(ENETUNREACH) || code == AVERROR(EHOSTUNREACH)) {
        return StreamError::ConnectFailed;
    }
    if (code == AVERROR(ETIMEDOUT)) {
        return StreamError::TimedOut;
    }
    if (code == AVERROR(EPIPE) || code == AVERROR(ECONNRESET) || code == AVERROR_EOF) {
        return StreamError::Closed;
    }
    // A handshake that failed, a certificate that is not the server's, or
    // the server refusing the key: FFmpeg says them all the same way.
    return StreamError::Rejected;
}

} // namespace

std::expected<RtmpTarget, StreamError> parseRtmpUrl(std::string_view url)
{
    url = text::trimmed(url);
    // Nothing but printable ASCII: an address has no spaces, and a control
    // character in it would be one in the protocol.
    if (url.empty() || !std::ranges::all_of(url, [](char c) { return c > 0x20 && c < 0x7f; })) {
        return std::unexpected(StreamError::NotConfigured);
    }
    RtmpTarget target;
    const auto startsWith = [&url](std::string_view scheme) {
        if (url.size() < scheme.size()) {
            return false;
        }
        for (std::size_t index = 0; index < scheme.size(); ++index) {
            if (text::lower(url[index]) != scheme[index]) {
                return false;
            }
        }
        return true;
    };
    if (startsWith("rtmps://")) {
        target.secure = true;
        target.port = 443;
        url.remove_prefix(8);
    } else if (startsWith("rtmp://")) {
        url.remove_prefix(7);
    } else {
        return std::unexpected(StreamError::NotConfigured);
    }
    const std::size_t slash = url.find('/');
    std::string_view authority = url.substr(0, slash);
    const std::string_view path = slash == std::string_view::npos ? std::string_view{} : url.substr(slash + 1);
    // **No user or password in the address**: the key is the secret, and
    // it lives in the secret store.
    if (authority.find('@') != std::string_view::npos) {
        return std::unexpected(StreamError::NotConfigured);
    }
    std::string_view port;
    if (authority.starts_with('[')) {
        const std::size_t close = authority.find(']');
        if (close == std::string_view::npos) {
            return std::unexpected(StreamError::NotConfigured);
        }
        target.host = std::string{authority.substr(1, close - 1)};
        const std::string_view rest = authority.substr(close + 1);
        if (!rest.empty()) {
            if (rest.front() != ':') {
                return std::unexpected(StreamError::NotConfigured);
            }
            port = rest.substr(1);
        }
    } else {
        const std::size_t colon = authority.find(':');
        target.host = std::string{authority.substr(0, colon)};
        if (colon != std::string_view::npos) {
            port = authority.substr(colon + 1);
        }
    }
    if (target.host.empty()) {
        return std::unexpected(StreamError::NotConfigured);
    }
    if (!port.empty()) {
        unsigned number = 0;
        const auto [end, error] = std::from_chars(port.data(), port.data() + port.size(), number);
        if (error != std::errc{} || end != port.data() + port.size() || number == 0 || number > 65535) {
            return std::unexpected(StreamError::NotConfigured);
        }
        target.port = static_cast<std::uint16_t>(number);
    }
    std::string_view app = path;
    while (!app.empty() && app.front() == '/') {
        app.remove_prefix(1);
    }
    while (!app.empty() && app.back() == '/') {
        app.remove_suffix(1);
    }
    target.app = std::string{app};
    return target;
}

std::string rtmpUrl(const RtmpTarget &target)
{
    std::string url = target.secure ? "rtmps://" : "rtmp://";
    url += withBrackets(target.host);
    if (target.port != (target.secure ? 443 : 1935)) {
        url += ":" + std::to_string(target.port);
    }
    url += "/" + target.app;
    return url;
}

/// **What the streams' headers are made from, copied off the encoders**,
/// so the thread that connects needs nothing of the caller's, which may be
/// gone by the time it is done.
struct Headers {
    AVCodecParameters *video = avcodec_parameters_alloc();
    AVCodecParameters *audio = avcodec_parameters_alloc();
    AVRational videoBase{1, 30};
    AVRational audioBase{1, 48000};
    AVRational frameRate{30, 1};

    Headers() = default;
    Headers(const Headers &) = delete;
    Headers &operator=(const Headers &) = delete;

    ~Headers()
    {
        avcodec_parameters_free(&video);
        avcodec_parameters_free(&audio);
    }
};

struct RtmpOutput::Private {
    AVFormatContext *output = nullptr;
    AVStream *video = nullptr;
    AVStream *audio = nullptr;
    AVRational videoBase{1, 30};
    AVRational audioBase{1, 48000};
    /// The caller's word to stop, once the connection is the caller's.
    /// **Not while it is being made**: that happens on a thread the caller
    /// may leave behind, which must not look at anything of the caller's.
    std::atomic<const std::atomic<bool> *> cancel{nullptr};
    /// Set when the caller gave up waiting for the connection: FFmpeg's
    /// waits end, and the thread lets the connection go by itself.
    std::shared_ptr<std::atomic<bool>> abandoned = std::make_shared<std::atomic<bool>>(false);
    /// When the wait FFmpeg is in now has to give up, in the steady
    /// clock's ticks.
    std::atomic<Clock::rep> deadline{0};
    Options options;
    bool failed = false;

    ~Private()
    {
        close();
    }

    void until(std::chrono::milliseconds wait)
    {
        deadline.store((Clock::now() + wait).time_since_epoch().count(), std::memory_order_relaxed);
    }

    /// FFmpeg asks this every tenth of a second while it waits.
    static int interrupted(void *opaque)
    {
        const auto *self = static_cast<const Private *>(opaque);
        return self->stopping() || self->overdue() ? 1 : 0;
    }

    [[nodiscard]] bool stopping() const
    {
        const std::atomic<bool> *callers = cancel.load(std::memory_order_acquire);
        return abandoned->load(std::memory_order_relaxed)
            || (callers != nullptr && callers->load(std::memory_order_relaxed));
    }

    [[nodiscard]] bool overdue() const
    {
        return Clock::now().time_since_epoch().count() > deadline.load(std::memory_order_relaxed);
    }

    void close()
    {
        if (output == nullptr) {
            return;
        }
        if (output->pb != nullptr) {
            // **A goodbye, but no waiting for it**: none at all once the
            // stream is stopping, since the interrupt callback says so at
            // once.
            until(kGoodbye);
            if (!failed) {
                (void)av_write_trailer(output);
            }
            (void)avio_closep(&output->pb);
        }
        avformat_free_context(output);
        output = nullptr;
    }

    [[nodiscard]] std::expected<void, StreamError> write(AVPacket *packet, AVStream *stream, AVRational base)
    {
        if (failed || output == nullptr) {
            return std::unexpected(StreamError::Closed);
        }
        av_packet_rescale_ts(packet, base, stream->time_base);
        packet->stream_index = stream->index;
        until(options.sendTimeout);
        const int written = av_interleaved_write_frame(output, packet);
        if (written < 0) {
            failed = true;
            if (stopping()) {
                return std::unexpected(StreamError::Closed);
            }
            return std::unexpected(overdue() ? StreamError::TimedOut : errorFor(written));
        }
        return {};
    }

    [[nodiscard]] std::expected<void, StreamError>
    start(const RtmpTarget &target, const std::string &key, const Headers &headers)
    {
        if (avformat_alloc_output_context2(&output, nullptr, "flv", nullptr) < 0 || output == nullptr) {
            return std::unexpected(StreamError::EncoderMissing);
        }
        video = avformat_new_stream(output, nullptr);
        audio = avformat_new_stream(output, nullptr);
        if (video == nullptr || audio == nullptr || avcodec_parameters_copy(video->codecpar, headers.video) < 0
            || avcodec_parameters_copy(audio->codecpar, headers.audio) < 0) {
            return std::unexpected(StreamError::EncoderFailed);
        }
        videoBase = headers.videoBase;
        audioBase = headers.audioBase;
        video->time_base = videoBase;
        audio->time_base = audioBase;
        video->avg_frame_rate = headers.frameRate;
        output->interrupt_callback = AVIOInterruptCB{&Private::interrupted, this};
        // **Every packet goes out as it is made**: a stream's audience is
        // waiting for it.
        output->flags |= AVFMT_FLAG_FLUSH_PACKETS;

        // **The name, not an address**: RTMPS sends it in the TLS hello
        // (SNI), without which a service with many names does not know
        // which certificate to show, and refuses.
        const std::string url = std::string{target.secure ? "rtmps://" : "rtmp://"} + withBrackets(target.host) + ":"
            + std::to_string(target.port) + "/";
        AVDictionary *settings = nullptr;
        av_dict_set(&settings, "rtmp_app", target.app.c_str(), 0);
        av_dict_set(&settings, "rtmp_playpath", key.c_str(), 0);
        av_dict_set(&settings, "rtmp_tcurl", rtmpUrl(target).c_str(), 0);
        av_dict_set(&settings, "tcp_nodelay", "1", 0);
        if (target.secure) {
            av_dict_set(&settings, "tls_verify", "1", 0);
            if (!options.caFile.empty()) {
                av_dict_set(&settings, "ca_file", options.caFile.c_str(), 0);
            }
        }
        until(options.connectTimeout);
        const int opened =
            avio_open2(&output->pb, url.c_str(), AVIO_FLAG_WRITE, &output->interrupt_callback, &settings);
        av_dict_free(&settings);
        if (opened < 0) {
            failed = true;
            return std::unexpected(stopping()      ? StreamError::Closed
                                       : overdue() ? StreamError::TimedOut
                                                   : errorFor(opened));
        }
        AVDictionary *muxing = nullptr;
        // A live stream has no length to go back and write.
        av_dict_set(&muxing, "flvflags", "no_duration_filesize", 0);
        const int header = avformat_write_header(output, &muxing);
        av_dict_free(&muxing);
        if (header < 0) {
            failed = true;
            return std::unexpected(stopping()      ? StreamError::Closed
                                       : overdue() ? StreamError::TimedOut
                                                   : errorFor(header));
        }
        return {};
    }
};

RtmpOutput::RtmpOutput()
    : d(std::make_unique<Private>())
{
}

RtmpOutput::~RtmpOutput() = default;

std::expected<std::unique_ptr<RtmpOutput>, StreamError> RtmpOutput::open(const RtmpTarget &target,
                                                                         const std::string &key,
                                                                         const AVCodecContext *video,
                                                                         const AVCodecContext *audio,
                                                                         const Options &options,
                                                                         const std::atomic<bool> &cancel)
{
    if (target.host.empty()) {
        return std::unexpected(StreamError::NotConfigured);
    }
    if (key.empty()) {
        return std::unexpected(StreamError::NoPassword);
    }
    if (video == nullptr || audio == nullptr) {
        return std::unexpected(StreamError::EncoderFailed);
    }
    // **The name looked up first, our way**, on a thread left behind if it
    // takes too long: a name that does not resolve is said as such, and
    // FFmpeg finds the answer waiting when it asks the same.
    const std::expected<std::vector<std::string>, StreamError> addresses =
        Connection::resolve(target.host, target.port, options.connectTimeout, cancel);
    if (!addresses) {
        return std::unexpected(addresses.error());
    }

    auto headers = std::make_shared<Headers>();
    if (headers->video == nullptr || headers->audio == nullptr
        || avcodec_parameters_from_context(headers->video, video) < 0
        || avcodec_parameters_from_context(headers->audio, audio) < 0) {
        return std::unexpected(StreamError::EncoderFailed);
    }
    headers->videoBase = video->time_base;
    headers->audioBase = audio->time_base;
    headers->frameRate = video->framerate;

    // **Connecting runs on a thread of its own, left behind when the stream
    // stops or the time is up.** FFmpeg's waits end at its interrupt
    // callback, but it looks the name up itself, where nothing can call it
    // off; the thread lets the connection go by itself when it is done.
    struct Attempt {
        std::mutex mutex;
        std::condition_variable done;
        bool finished = false;
        std::expected<void, StreamError> result;
        std::unique_ptr<RtmpOutput> output;
    };
    auto attempt = std::make_shared<Attempt>();
    std::unique_ptr<RtmpOutput> made{new RtmpOutput};
    made->d->options = options;
    const std::shared_ptr<std::atomic<bool>> abandoned = made->d->abandoned;
    try {
        std::thread([attempt, headers, target, key, output = std::move(made)]() mutable {
            std::expected<void, StreamError> result = output->d->start(target, key, *headers);
            const std::lock_guard lock(attempt->mutex);
            attempt->result = result;
            attempt->output = std::move(output);
            attempt->finished = true;
            attempt->done.notify_all();
        }).detach();
    } catch (const std::system_error &) {
        return std::unexpected(StreamError::ConnectFailed);
    }

    // FFmpeg gives up by itself at the connect timeout; a moment more for
    // it to say so.
    const auto deadline = Clock::now() + options.connectTimeout + kGoodbye;
    std::unique_lock lock(attempt->mutex);
    while (!attempt->finished) {
        if (cancel.load(std::memory_order_relaxed) || Clock::now() >= deadline) {
            abandoned->store(true, std::memory_order_relaxed);
            return std::unexpected(cancel.load(std::memory_order_relaxed) ? StreamError::Closed
                                                                          : StreamError::TimedOut);
        }
        attempt->done.wait_for(lock, std::chrono::milliseconds{50});
    }
    if (!attempt->result) {
        return std::unexpected(attempt->result.error());
    }
    std::unique_ptr<RtmpOutput> connected = std::move(attempt->output);
    // From here on the caller's word stops it.
    connected->d->cancel.store(&cancel, std::memory_order_release);
    return connected;
}

std::expected<void, StreamError> RtmpOutput::writeVideo(AVPacket *packet)
{
    return d->write(packet, d->video, d->videoBase);
}

std::expected<void, StreamError> RtmpOutput::writeAudio(AVPacket *packet)
{
    return d->write(packet, d->audio, d->audioBase);
}

std::uint64_t RtmpOutput::bytesSent() const noexcept
{
    if (d->output == nullptr || d->output->pb == nullptr) {
        return 0;
    }
    return static_cast<std::uint64_t>(std::max<std::int64_t>(0, d->output->pb->bytes_written));
}

} // namespace rb::stream
