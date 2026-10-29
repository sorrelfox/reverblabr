// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace rb::stream
{

/// A colour, eight bits a channel.
struct Rgb {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;

    [[nodiscard]] bool operator==(const Rgb &other) const = default;
};

/// "#rrggbb" or "#rgb", as a session file keeps a colour; nothing for
/// anything else.
[[nodiscard]] std::optional<Rgb> parseColour(std::string_view text) noexcept;

/// How bright a colour looks, from 0 to 1 (the sRGB luma weights, which is
/// all choosing between light and dark text needs).
[[nodiscard]] double lumaOf(Rgb colour) noexcept;

/// A rectangle in pixels.
struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    [[nodiscard]] bool operator==(const Rect &other) const = default;
};

/// **How much of each pixel a shape covers**, from 0 to 255: what text is
/// drawn into before it is given a colour, so the same words can be drawn
/// in whichever colour the picture behind them asks for.
struct Coverage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> values;

    [[nodiscard]] bool empty() const noexcept
    {
        return width <= 0 || height <= 0;
    }
};

/// A picture with its own transparency: eight-bit RGBA, rows packed, not
/// premultiplied. What an image file is read into.
struct Picture {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;

    [[nodiscard]] bool empty() const noexcept
    {
        return width <= 0 || height <= 0;
    }
};

/// How a picture is made to fit a space.
enum class Fit : std::uint8_t {
    /// All of it, as large as fits, keeping its shape.
    Contain,
    /// All of the space, cutting off what spills over, keeping its shape.
    Cover,
};

/// **An image file, read and scaled to fit \a width × \a height**, through
/// the system's FFmpeg (PNG, JPEG, WebP and whatever else it decodes).
/// Nothing when it cannot be read. **Only a regular file, and not a huge
/// one**: a path somebody typed may be a pipe, which would hold the stream
/// for as long as nothing writes to it, or a file far larger than any
/// picture.
[[nodiscard]] std::optional<Picture> loadPicture(const std::filesystem::path &path, int width, int height, Fit fit);

/// **What a frame of the video is drawn on**: eight-bit RGBA, rows packed,
/// always opaque. Plain memory and plain loops, so it is drawn on the
/// stream's thread with nothing but the C++ library (the engine has no
/// GUI toolkit, ADR-0027).
class Canvas
{
public:
    Canvas() = default;
    Canvas(int width, int height);

    [[nodiscard]] int width() const noexcept
    {
        return m_width;
    }

    [[nodiscard]] int height() const noexcept
    {
        return m_height;
    }

    /// The pixels, four bytes each, a row after another.
    [[nodiscard]] std::span<const std::uint8_t> pixels() const noexcept
    {
        return m_pixels;
    }

    /// The pixel at \a x, \a y, which must be inside.
    [[nodiscard]] Rgb at(int x, int y) const noexcept;

    void fill(Rgb colour) noexcept;
    /// \a area in \a colour, \a opacity from 0 to 1, cut to the canvas.
    void fillRect(Rect area, Rgb colour, double opacity = 1.0) noexcept;
    /// \a picture with its top left at \a x, \a y, through its own
    /// transparency and then \a opacity.
    void draw(const Picture &picture, int x, int y, double opacity = 1.0) noexcept;
    /// \a coverage in \a colour with its top left at \a x, \a y.
    void paint(const Coverage &coverage, int x, int y, Rgb colour, double opacity = 1.0) noexcept;
    /// The same canvas as \a other, which must be as large, copied in.
    void copyFrom(const Canvas &other) noexcept;

    /// How bright \a area is on average, from 0 to 1: what decides whether
    /// text over it is light or dark.
    [[nodiscard]] double luma(Rect area) const noexcept;

private:
    /// \a area cut to the canvas.
    [[nodiscard]] Rect clipped(Rect area) const noexcept;

    int m_width = 0;
    int m_height = 0;
    std::vector<std::uint8_t> m_pixels;
};

} // namespace rb::stream
