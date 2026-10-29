// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "stream/canvas.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <string>

namespace rb::stream
{

namespace
{

/// The largest image file read: far more than any picture a stream shows.
constexpr std::size_t kMaxPictureBytes = 32U * 1024U * 1024U;

/// **The largest picture decoded or made, each way**: 8192 pixels, more than
/// any frame a stream sends. A file that says it is larger is refused
/// before anything its size would ask for is allocated.
constexpr int kMaxPictureSide = 8192;

[[nodiscard]] int hexDigit(char c) noexcept
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

[[nodiscard]] std::uint8_t mix(std::uint8_t under, std::uint8_t over, int alpha) noexcept
{
    // alpha is 0 to 255; rounded, so a full cover is exactly the colour.
    return static_cast<std::uint8_t>((under * (255 - alpha) + over * alpha + 127) / 255);
}

/// The whole of a regular file, or nothing. **Opened without waiting** and
/// looked at before it is read, so a pipe or a device is turned away
/// rather than waited on.
[[nodiscard]] std::optional<std::vector<std::uint8_t>> readSmallFile(const std::filesystem::path &path)
{
    const int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOCTTY);
    if (fd < 0) {
        return std::nullopt;
    }
    struct stat info{};
    if (::fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size <= 0
        || static_cast<std::size_t>(info.st_size) > kMaxPictureBytes) {
        ::close(fd);
        return std::nullopt;
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(info.st_size));
    std::size_t got = 0;
    while (got < bytes.size()) {
        const ssize_t read = ::read(fd, bytes.data() + got, bytes.size() - got);
        if (read < 0 && errno == EINTR) {
            continue;
        }
        if (read <= 0) {
            break;
        }
        got += static_cast<std::size_t>(read);
    }
    ::close(fd);
    bytes.resize(got);
    return bytes;
}

/// The decoder for what \a bytes start with. **By the file's own
/// signature**, not by asking FFmpeg's demuxers to guess: some of those
/// follow what a file names (a playlist, a sequence of files), and a
/// picture is to be one file and nothing else.
[[nodiscard]] AVCodecID codecFor(const std::vector<std::uint8_t> &bytes) noexcept
{
    const auto startsWith = [&bytes](std::string_view signature, std::size_t at = 0) {
        return bytes.size() >= at + signature.size()
            && std::memcmp(bytes.data() + at, signature.data(), signature.size()) == 0;
    };
    if (startsWith("\x89PNG\r\n\x1a\n")) {
        return AV_CODEC_ID_PNG;
    }
    if (startsWith("\xff\xd8\xff")) {
        return AV_CODEC_ID_MJPEG;
    }
    if (startsWith("RIFF") && startsWith("WEBP", 8)) {
        return AV_CODEC_ID_WEBP;
    }
    if (startsWith("BM")) {
        return AV_CODEC_ID_BMP;
    }
    return AV_CODEC_ID_NONE;
}

/// The image in \a bytes, decoded.
[[nodiscard]] AVFrame *decode(const std::vector<std::uint8_t> &bytes)
{
    const AVCodec *codec = avcodec_find_decoder(codecFor(bytes));
    if (codec == nullptr) {
        return nullptr;
    }
    AVCodecContext *context = avcodec_alloc_context3(codec);
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    bool decoded = false;
    if (context != nullptr) {
        // The decoder itself refuses a picture whose header claims more.
        context->max_pixels = static_cast<std::int64_t>(kMaxPictureSide) * kMaxPictureSide;
    }
    if (context != nullptr && packet != nullptr && frame != nullptr && avcodec_open2(context, codec, nullptr) >= 0
        && av_new_packet(packet, static_cast<int>(bytes.size())) >= 0) {
        std::memcpy(packet->data, bytes.data(), bytes.size());
        packet->flags |= AV_PKT_FLAG_KEY;
        if (avcodec_send_packet(context, packet) >= 0) {
            (void)avcodec_send_packet(context, nullptr);
            decoded = avcodec_receive_frame(context, frame) >= 0;
        }
    }
    av_packet_free(&packet);
    avcodec_free_context(&context);
    if (!decoded) {
        av_frame_free(&frame);
        return nullptr;
    }
    return frame;
}

} // namespace

std::optional<Rgb> parseColour(std::string_view text) noexcept
{
    if (text.empty() || text.front() != '#') {
        return std::nullopt;
    }
    text.remove_prefix(1);
    if (text.size() != 3 && text.size() != 6) {
        return std::nullopt;
    }
    std::array<int, 6> digits{};
    for (std::size_t index = 0; index < text.size(); ++index) {
        digits[index] = hexDigit(text[index]);
        if (digits[index] < 0) {
            return std::nullopt;
        }
    }
    if (text.size() == 3) {
        return Rgb{static_cast<std::uint8_t>(digits[0] * 17),
                   static_cast<std::uint8_t>(digits[1] * 17),
                   static_cast<std::uint8_t>(digits[2] * 17)};
    }
    return Rgb{static_cast<std::uint8_t>(digits[0] * 16 + digits[1]),
               static_cast<std::uint8_t>(digits[2] * 16 + digits[3]),
               static_cast<std::uint8_t>(digits[4] * 16 + digits[5])};
}

double lumaOf(Rgb colour) noexcept
{
    return (0.2126 * colour.r + 0.7152 * colour.g + 0.0722 * colour.b) / 255.0;
}

std::optional<Picture> loadPicture(const std::filesystem::path &path, int width, int height, Fit fit)
{
    if (path.empty() || width <= 0 || height <= 0 || width > kMaxPictureSide || height > kMaxPictureSide) {
        return std::nullopt;
    }
    const std::optional<std::vector<std::uint8_t>> bytes = readSmallFile(path);
    if (!bytes) {
        return std::nullopt;
    }
    AVFrame *frame = decode(*bytes);
    if (frame == nullptr) {
        return std::nullopt;
    }
    const int sourceWidth = frame->width;
    const int sourceHeight = frame->height;
    if (sourceWidth <= 0 || sourceHeight <= 0 || sourceWidth > kMaxPictureSide || sourceHeight > kMaxPictureSide) {
        av_frame_free(&frame);
        return std::nullopt;
    }
    const double across = static_cast<double>(width) / sourceWidth;
    const double down = static_cast<double>(height) / sourceHeight;
    int scaledWidth = width;
    int scaledHeight = height;
    if (fit == Fit::Contain) {
        // All of it, as large as fits: never larger than the space.
        const double scale = std::min(across, down);
        scaledWidth = std::clamp(static_cast<int>(std::lround(sourceWidth * scale)), 1, width);
        scaledHeight = std::clamp(static_cast<int>(std::lround(sourceHeight * scale)), 1, height);
    } else {
        // **Only the part of the source that covers the space** is scaled,
        // straight to the space's size: a picture far thinner than the
        // frame is never first made as large as the frame is wide.
        const double scale = std::max(across, down);
        const int cutWidth = std::clamp(static_cast<int>(std::lround(width / scale)), 1, sourceWidth);
        const int cutHeight = std::clamp(static_cast<int>(std::lround(height / scale)), 1, sourceHeight);
        frame->crop_left = static_cast<std::size_t>((sourceWidth - cutWidth) / 2);
        frame->crop_right = static_cast<std::size_t>(sourceWidth - cutWidth) - frame->crop_left;
        frame->crop_top = static_cast<std::size_t>((sourceHeight - cutHeight) / 2);
        frame->crop_bottom = static_cast<std::size_t>(sourceHeight - cutHeight) - frame->crop_top;
        if (av_frame_apply_cropping(frame, AV_FRAME_CROP_UNALIGNED) < 0) {
            av_frame_free(&frame);
            return std::nullopt;
        }
    }

    SwsContext *scaler = sws_getContext(frame->width,
                                        frame->height,
                                        static_cast<AVPixelFormat>(frame->format),
                                        scaledWidth,
                                        scaledHeight,
                                        AV_PIX_FMT_RGBA,
                                        SWS_BICUBIC,
                                        nullptr,
                                        nullptr,
                                        nullptr);
    if (scaler == nullptr) {
        av_frame_free(&frame);
        return std::nullopt;
    }
    Picture scaled;
    scaled.width = scaledWidth;
    scaled.height = scaledHeight;
    scaled.rgba.assign(static_cast<std::size_t>(scaledWidth) * static_cast<std::size_t>(scaledHeight) * 4U, 0);
    std::uint8_t *planes[4] = {scaled.rgba.data(), nullptr, nullptr, nullptr};
    const int strides[4] = {scaledWidth * 4, 0, 0, 0};
    const int rows = sws_scale(scaler, frame->data, frame->linesize, 0, frame->height, planes, strides);
    sws_freeContext(scaler);
    av_frame_free(&frame);
    if (rows <= 0) {
        return std::nullopt;
    }
    return scaled;
}

Canvas::Canvas(int width, int height)
    : m_width(std::max(width, 0))
    , m_height(std::max(height, 0))
    , m_pixels(static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height) * 4U, 255)
{
}

Rgb Canvas::at(int x, int y) const noexcept
{
    const std::uint8_t *pixel = m_pixels.data()
        + (static_cast<std::size_t>(y) * static_cast<std::size_t>(m_width) + static_cast<std::size_t>(x)) * 4U;
    return Rgb{pixel[0], pixel[1], pixel[2]};
}

Rect Canvas::clipped(Rect area) const noexcept
{
    const int left = std::clamp(area.x, 0, m_width);
    const int top = std::clamp(area.y, 0, m_height);
    const int right = std::clamp(area.x + std::max(area.width, 0), 0, m_width);
    const int bottom = std::clamp(area.y + std::max(area.height, 0), 0, m_height);
    return Rect{left, top, right - left, bottom - top};
}

void Canvas::fill(Rgb colour) noexcept
{
    fillRect(Rect{0, 0, m_width, m_height}, colour);
}

void Canvas::fillRect(Rect area, Rgb colour, double opacity) noexcept
{
    const Rect inside = clipped(area);
    const int alpha = static_cast<int>(std::lround(std::clamp(opacity, 0.0, 1.0) * 255.0));
    if (alpha == 0) {
        return;
    }
    for (int y = inside.y; y < inside.y + inside.height; ++y) {
        std::uint8_t *pixel = m_pixels.data()
            + (static_cast<std::size_t>(y) * static_cast<std::size_t>(m_width) + static_cast<std::size_t>(inside.x))
                * 4U;
        for (int x = 0; x < inside.width; ++x, pixel += 4) {
            pixel[0] = mix(pixel[0], colour.r, alpha);
            pixel[1] = mix(pixel[1], colour.g, alpha);
            pixel[2] = mix(pixel[2], colour.b, alpha);
            pixel[3] = 255;
        }
    }
}

void Canvas::draw(const Picture &picture, int x, int y, double opacity) noexcept
{
    const Rect inside = clipped(Rect{x, y, picture.width, picture.height});
    const double scale = std::clamp(opacity, 0.0, 1.0);
    for (int row = inside.y; row < inside.y + inside.height; ++row) {
        std::uint8_t *pixel = m_pixels.data()
            + (static_cast<std::size_t>(row) * static_cast<std::size_t>(m_width) + static_cast<std::size_t>(inside.x))
                * 4U;
        const std::uint8_t *source = picture.rgba.data()
            + (static_cast<std::size_t>(row - y) * static_cast<std::size_t>(picture.width)
               + static_cast<std::size_t>(inside.x - x))
                * 4U;
        for (int column = 0; column < inside.width; ++column, pixel += 4, source += 4) {
            const int alpha = static_cast<int>(std::lround(source[3] * scale));
            pixel[0] = mix(pixel[0], source[0], alpha);
            pixel[1] = mix(pixel[1], source[1], alpha);
            pixel[2] = mix(pixel[2], source[2], alpha);
        }
    }
}

void Canvas::paint(const Coverage &coverage, int x, int y, Rgb colour, double opacity) noexcept
{
    const Rect inside = clipped(Rect{x, y, coverage.width, coverage.height});
    const double scale = std::clamp(opacity, 0.0, 1.0);
    for (int row = inside.y; row < inside.y + inside.height; ++row) {
        std::uint8_t *pixel = m_pixels.data()
            + (static_cast<std::size_t>(row) * static_cast<std::size_t>(m_width) + static_cast<std::size_t>(inside.x))
                * 4U;
        const std::uint8_t *covered = coverage.values.data()
            + static_cast<std::size_t>(row - y) * static_cast<std::size_t>(coverage.width)
            + static_cast<std::size_t>(inside.x - x);
        for (int column = 0; column < inside.width; ++column, pixel += 4, ++covered) {
            if (*covered == 0) {
                continue;
            }
            const int alpha = static_cast<int>(std::lround(*covered * scale));
            pixel[0] = mix(pixel[0], colour.r, alpha);
            pixel[1] = mix(pixel[1], colour.g, alpha);
            pixel[2] = mix(pixel[2], colour.b, alpha);
        }
    }
}

void Canvas::copyFrom(const Canvas &other) noexcept
{
    if (other.m_pixels.size() == m_pixels.size()) {
        std::memcpy(m_pixels.data(), other.m_pixels.data(), m_pixels.size());
    }
}

double Canvas::luma(Rect area) const noexcept
{
    const Rect inside = clipped(area);
    if (inside.width <= 0 || inside.height <= 0) {
        return 0.0;
    }
    // Every fourth pixel each way is plenty for an average.
    double sum = 0.0;
    std::size_t count = 0;
    for (int y = inside.y; y < inside.y + inside.height; y += 4) {
        for (int x = inside.x; x < inside.x + inside.width; x += 4) {
            sum += lumaOf(at(x, y));
            ++count;
        }
    }
    return count > 0 ? sum / static_cast<double>(count) : 0.0;
}

} // namespace rb::stream
