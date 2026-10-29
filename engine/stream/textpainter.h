// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "stream/canvas.h"

#include <memory>
#include <string>
#include <string_view>

namespace rb::stream
{

/// A line of text set in a size, ready to be painted.
struct TextLine {
    /// What the line is covered with, its baseline at \a ascent.
    Coverage coverage;
    int ascent = 0;
    /// The text it ended up as: shorter than asked, with an ellipsis, when
    /// it did not fit.
    std::string text;
    /// The size it was set in, in pixels, which is smaller than asked when
    /// it had to shrink to fit.
    int pixelSize = 0;
};

/// **Text for the video, with the desktop's own fonts**: FreeType draws
/// the glyphs, HarfBuzz places them (joined Arabic, Indic clusters,
/// ligatures) and fontconfig finds the font, the same three the desktop
/// draws its own text with. **A character the font has not got is taken
/// from one that has**, asked of fontconfig, so a title in Japanese
/// under a Latin font is still a title and not boxes.
///
/// One line at a time, in the direction of its script; a line that mixes
/// directions is set in the order it was typed. On the stream's thread
/// only, one painter a stream.
class TextPainter
{
public:
    /// A painter with the desktop's sans-serif font, or nothing when the
    /// system has no font at all. **Not fatal to a stream**: it goes out
    /// with the picture and no words.
    [[nodiscard]] static std::unique_ptr<TextPainter> create();
    ~TextPainter();
    TextPainter(const TextPainter &) = delete;
    TextPainter &operator=(const TextPainter &) = delete;

    /// \a text set in \a pixelSize, bold or not. **No wider than
    /// \a maxWidth**: a longer line is set smaller, down to \a minPixelSize,
    /// and cut short with an ellipsis if it still does not fit.
    [[nodiscard]] TextLine set(std::string_view text, int pixelSize, bool bold, int maxWidth, int minPixelSize);

    /// The file of the font it sets in, for a test to see what was found.
    [[nodiscard]] std::string fontFile(bool bold) const;

private:
    TextPainter();

    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace rb::stream
