// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "stream/textpainter.h"

#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SYNTHESIS_H
#include <hb-ft.h>
#include <hb.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>
#include <vector>

namespace rb::stream
{

namespace
{

/// One character of the text, and where it is in the UTF-8.
struct Character {
    char32_t code = 0;
    std::size_t offset = 0;
    std::size_t length = 0;
};

/// \a text as characters. **Whatever is not UTF-8 is a replacement
/// character**, and a line break or any other control character is a
/// space: a title is one line, whatever a player or a file says.
[[nodiscard]] std::vector<Character> decode(std::string_view text, std::string &cleaned)
{
    std::vector<Character> characters;
    cleaned.clear();
    std::size_t at = 0;
    while (at < text.size()) {
        const auto byte = static_cast<unsigned char>(text[at]);
        char32_t code = 0xfffd;
        std::size_t length = 1;
        const auto continuation = [&](std::size_t index) {
            return at + index < text.size() && (static_cast<unsigned char>(text[at + index]) & 0xc0) == 0x80;
        };
        if (byte < 0x80) {
            code = byte;
        } else if ((byte & 0xe0) == 0xc0 && continuation(1)) {
            code = static_cast<char32_t>(((byte & 0x1f) << 6) | (text[at + 1] & 0x3f));
            length = 2;
            code = code < 0x80 ? 0xfffd : code;
        } else if ((byte & 0xf0) == 0xe0 && continuation(1) && continuation(2)) {
            code = static_cast<char32_t>(((byte & 0x0f) << 12) | ((text[at + 1] & 0x3f) << 6) | (text[at + 2] & 0x3f));
            length = 3;
            code = code < 0x800 || (code >= 0xd800 && code <= 0xdfff) ? 0xfffd : code;
        } else if ((byte & 0xf8) == 0xf0 && continuation(1) && continuation(2) && continuation(3)) {
            code = static_cast<char32_t>(((byte & 0x07) << 18) | ((text[at + 1] & 0x3f) << 12)
                                         | ((text[at + 2] & 0x3f) << 6) | (text[at + 3] & 0x3f));
            length = 4;
            code = code < 0x10000 || code > 0x10ffff ? 0xfffd : code;
        }
        at += length;
        if (code < 0x20 || code == 0x7f) {
            code = ' ';
        }
        // Back out as UTF-8 again, so that HarfBuzz is given only what
        // was read.
        Character character{code, cleaned.size(), 0};
        if (code < 0x80) {
            cleaned.push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            cleaned.push_back(static_cast<char>(0xc0 | (code >> 6)));
            cleaned.push_back(static_cast<char>(0x80 | (code & 0x3f)));
        } else if (code < 0x10000) {
            cleaned.push_back(static_cast<char>(0xe0 | (code >> 12)));
            cleaned.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
            cleaned.push_back(static_cast<char>(0x80 | (code & 0x3f)));
        } else {
            cleaned.push_back(static_cast<char>(0xf0 | (code >> 18)));
            cleaned.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3f)));
            cleaned.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
            cleaned.push_back(static_cast<char>(0x80 | (code & 0x3f)));
        }
        character.length = cleaned.size() - character.offset;
        characters.push_back(character);
    }
    return characters;
}

/// One font file, open in FreeType and in HarfBuzz.
struct Face {
    FT_Face face = nullptr;
    hb_font_t *font = nullptr;
    std::string file;
    /// Whether it is bold itself, or has to be made so.
    bool bold = false;
    int pixelSize = 0;

    Face() = default;
    Face(const Face &) = delete;
    Face &operator=(const Face &) = delete;

    ~Face()
    {
        if (font != nullptr) {
            hb_font_destroy(font);
        }
        if (face != nullptr) {
            FT_Done_Face(face);
        }
    }

    void setSize(int size)
    {
        if (size == pixelSize) {
            return;
        }
        pixelSize = size;
        (void)FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(size));
        hb_ft_font_changed(font);
    }

    [[nodiscard]] bool has(char32_t code) const
    {
        return FT_Get_Char_Index(face, code) != 0;
    }
};

/// A glyph placed on the line.
struct Placed {
    Face *face = nullptr;
    unsigned glyph = 0;
    /// In 26.6 fixed point, from the start of the line and the baseline.
    long x = 0;
    long y = 0;
};

/// fontconfig's answer for \a pattern: the file and its face index.
[[nodiscard]] std::optional<std::pair<std::string, int>> match(FcPattern *pattern, std::optional<char32_t> needs)
{
    FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);
    FcResult result = FcResultNoMatch;
    FcPattern *found = FcFontMatch(nullptr, pattern, &result);
    if (found == nullptr) {
        return std::nullopt;
    }
    std::optional<std::pair<std::string, int>> answer;
    FcChar8 *file = nullptr;
    int index = 0;
    FcCharSet *characters = nullptr;
    const bool covers = !needs
        || (FcPatternGetCharSet(found, FC_CHARSET, 0, &characters) == FcResultMatch
            && FcCharSetHasChar(characters, static_cast<FcChar32>(*needs)) != 0);
    if (covers && FcPatternGetString(found, FC_FILE, 0, &file) == FcResultMatch && file != nullptr) {
        (void)FcPatternGetInteger(found, FC_INDEX, 0, &index);
        answer = std::pair{std::string{reinterpret_cast<const char *>(file)}, index};
    }
    FcPatternDestroy(found);
    return answer;
}

[[nodiscard]] FcPattern *sansPattern(bool bold)
{
    FcPattern *pattern = FcPatternCreate();
    FcPatternAddString(pattern, FC_FAMILY, reinterpret_cast<const FcChar8 *>("sans-serif"));
    FcPatternAddInteger(pattern, FC_WEIGHT, bold ? FC_WEIGHT_BOLD : FC_WEIGHT_REGULAR);
    FcPatternAddBool(pattern, FC_SCALABLE, FcTrue);
    return pattern;
}

} // namespace

struct TextPainter::Private {
    FT_Library library = nullptr;
    /// Every face opened, by file and index, kept for the painter's life.
    std::map<std::pair<std::string, int>, std::unique_ptr<Face>> faces;
    std::array<Face *, 2> primary{};
    /// What has been asked of fontconfig for a character the primary font
    /// lacks: a face, or none at all.
    std::map<std::pair<char32_t, bool>, Face *> fallbacks;

    ~Private()
    {
        faces.clear();
        if (library != nullptr) {
            FT_Done_FreeType(library);
        }
    }

    [[nodiscard]] Face *open(const std::pair<std::string, int> &which, bool bold)
    {
        const auto key = std::pair{which.first, which.second * 2 + (bold ? 1 : 0)};
        if (const auto found = faces.find(key); found != faces.end()) {
            return found->second.get();
        }
        auto made = std::make_unique<Face>();
        if (FT_New_Face(library, which.first.c_str(), which.second, &made->face) != 0) {
            return nullptr;
        }
        made->font = hb_ft_font_create_referenced(made->face);
        made->file = which.first;
        made->bold = bold;
        Face *face = made.get();
        faces.emplace(key, std::move(made));
        return face;
    }

    /// The face to set \a code in.
    [[nodiscard]] Face *faceFor(char32_t code, bool bold)
    {
        Face *first = primary[bold ? 1 : 0];
        if (first->has(code) || code == ' ') {
            return first;
        }
        const auto key = std::pair{code, bold};
        if (const auto found = fallbacks.find(key); found != fallbacks.end()) {
            return found->second != nullptr ? found->second : first;
        }
        FcPattern *pattern = sansPattern(bold);
        FcCharSet *characters = FcCharSetCreate();
        FcCharSetAddChar(characters, static_cast<FcChar32>(code));
        FcPatternAddCharSet(pattern, FC_CHARSET, characters);
        FcCharSetDestroy(characters);
        const std::optional<std::pair<std::string, int>> which = match(pattern, code);
        FcPatternDestroy(pattern);
        Face *face = which ? open(*which, bold) : nullptr;
        fallbacks.emplace(key, face);
        return face != nullptr ? face : first;
    }

    /// \a characters of \a text shaped at \a size: the glyphs and how far
    /// the line goes, in 26.6.
    [[nodiscard]] std::pair<std::vector<Placed>, long>
    shape(const std::string &text, const std::vector<Character> &characters, std::size_t count, int size, bool bold)
    {
        std::vector<Placed> placed;
        long pen = 0;
        std::size_t start = 0;
        hb_buffer_t *buffer = hb_buffer_create();
        while (start < count) {
            // A run is as many characters as one face sets.
            Face *face = faceFor(characters[start].code, bold);
            std::size_t end = start + 1;
            while (end < count && faceFor(characters[end].code, bold) == face) {
                ++end;
            }
            face->setSize(size);
            hb_buffer_clear_contents(buffer);
            hb_buffer_add_utf8(
                buffer,
                text.data(),
                static_cast<int>(text.size()),
                static_cast<unsigned>(characters[start].offset),
                static_cast<int>(characters[end - 1].offset + characters[end - 1].length - characters[start].offset));
            hb_buffer_guess_segment_properties(buffer);
            hb_shape(face->font, buffer, nullptr, 0);
            unsigned glyphs = 0;
            const hb_glyph_info_t *info = hb_buffer_get_glyph_infos(buffer, &glyphs);
            const hb_glyph_position_t *position = hb_buffer_get_glyph_positions(buffer, &glyphs);
            for (unsigned index = 0; index < glyphs; ++index) {
                placed.push_back(
                    Placed{face, info[index].codepoint, pen + position[index].x_offset, position[index].y_offset});
                pen += position[index].x_advance;
            }
            start = end;
        }
        hb_buffer_destroy(buffer);
        return {std::move(placed), pen};
    }
};

TextPainter::TextPainter()
    : d(std::make_unique<Private>())
{
}

TextPainter::~TextPainter() = default;

std::unique_ptr<TextPainter> TextPainter::create()
{
    std::unique_ptr<TextPainter> painter{new TextPainter};
    if (FT_Init_FreeType(&painter->d->library) != 0) {
        return nullptr;
    }
    for (const bool bold : {false, true}) {
        FcPattern *pattern = sansPattern(bold);
        const std::optional<std::pair<std::string, int>> which = match(pattern, std::nullopt);
        FcPatternDestroy(pattern);
        Face *face = which ? painter->d->open(*which, bold) : nullptr;
        if (face == nullptr) {
            return nullptr;
        }
        painter->d->primary[bold ? 1 : 0] = face;
    }
    return painter;
}

std::string TextPainter::fontFile(bool bold) const
{
    return d->primary[bold ? 1 : 0]->file;
}

TextLine TextPainter::set(std::string_view text, int pixelSize, bool bold, int maxWidth, int minPixelSize)
{
    TextLine line;
    std::string cleaned;
    std::vector<Character> characters = decode(text, cleaned);
    // Spaces at either end are nothing to see.
    while (!characters.empty() && characters.back().code == ' ') {
        characters.pop_back();
    }
    std::size_t first = 0;
    while (first < characters.size() && characters[first].code == ' ') {
        ++first;
    }
    characters.erase(characters.begin(), characters.begin() + static_cast<std::ptrdiff_t>(first));
    if (characters.empty() || pixelSize <= 0 || maxWidth <= 0) {
        return line;
    }
    const long limit = static_cast<long>(maxWidth) * 64;
    minPixelSize = std::clamp(minPixelSize, 1, pixelSize);

    // **Smaller first, then shorter**: the whole title in a smaller size
    // says more than half of it in a large one.
    int size = pixelSize;
    auto [placed, width] = d->shape(cleaned, characters, characters.size(), size, bold);
    while (width > limit && size > minPixelSize) {
        const int scaled =
            static_cast<int>(static_cast<double>(size) * static_cast<double>(limit) / static_cast<double>(width));
        size = std::max(minPixelSize, std::min(size - 1, scaled));
        std::tie(placed, width) = d->shape(cleaned, characters, characters.size(), size, bold);
    }
    std::string shown = cleaned.substr(characters.front().offset);
    if (width > limit) {
        // As many characters as fit with an ellipsis after them.
        std::size_t low = 0;
        std::size_t high = characters.size();
        std::vector<Placed> best;
        long bestWidth = 0;
        std::string bestText;
        while (low < high) {
            const std::size_t mid = (low + high + 1) / 2;
            std::size_t kept = mid;
            while (kept > 0 && characters[kept - 1].code == ' ') {
                --kept;
            }
            std::string candidate = cleaned.substr(
                characters.front().offset,
                kept > 0 ? characters[kept - 1].offset + characters[kept - 1].length - characters.front().offset : 0);
            candidate += "…";
            std::string candidateCleaned;
            const std::vector<Character> candidateCharacters = decode(candidate, candidateCleaned);
            auto [tried, triedWidth] =
                d->shape(candidateCleaned, candidateCharacters, candidateCharacters.size(), size, bold);
            if (triedWidth <= limit) {
                low = mid;
                best = std::move(tried);
                bestWidth = triedWidth;
                bestText = std::move(candidate);
            } else {
                high = mid - 1;
            }
        }
        if (best.empty()) {
            return line;
        }
        placed = std::move(best);
        width = bestWidth;
        shown = std::move(bestText);
    }

    Face *primary = d->primary[bold ? 1 : 0];
    primary->setSize(size);
    const int ascent = static_cast<int>((primary->face->size->metrics.ascender + 63) / 64);
    const int descent = static_cast<int>((-primary->face->size->metrics.descender + 63) / 64);
    // Room either side for what a glyph draws past its advance, such as an
    // italic's overhang or a made-up bold.
    const int pad = std::max(2, size / 8);
    line.coverage.width = static_cast<int>((width + 63) / 64) + pad * 2;
    line.coverage.height = ascent + descent + pad * 2;
    line.coverage.values.assign(
        static_cast<std::size_t>(line.coverage.width) * static_cast<std::size_t>(line.coverage.height), 0);
    line.ascent = ascent + pad;
    line.text = std::move(shown);
    line.pixelSize = size;

    for (const Placed &glyph : placed) {
        glyph.face->setSize(size);
        if (FT_Load_Glyph(glyph.face->face, glyph.glyph, FT_LOAD_DEFAULT | FT_LOAD_NO_BITMAP) != 0) {
            continue;
        }
        FT_GlyphSlot slot = glyph.face->face->glyph;
        // **A bold that the font has not got is made**, as the desktop
        // does, rather than set in the regular weight.
        if (glyph.face->bold && (glyph.face->face->style_flags & FT_STYLE_FLAG_BOLD) == 0) {
            FT_GlyphSlot_Embolden(slot);
        }
        if (FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL) != 0) {
            continue;
        }
        const FT_Bitmap &bitmap = slot->bitmap;
        const int left = static_cast<int>(std::lround(static_cast<double>(glyph.x) / 64.0)) + pad + slot->bitmap_left;
        const int top =
            line.ascent - static_cast<int>(std::lround(static_cast<double>(glyph.y) / 64.0)) - slot->bitmap_top;
        for (unsigned row = 0; row < bitmap.rows; ++row) {
            const int y = top + static_cast<int>(row);
            if (y < 0 || y >= line.coverage.height) {
                continue;
            }
            for (unsigned column = 0; column < bitmap.width; ++column) {
                const int x = left + static_cast<int>(column);
                if (x < 0 || x >= line.coverage.width) {
                    continue;
                }
                const std::uint8_t value = bitmap.buffer[static_cast<std::ptrdiff_t>(row) * bitmap.pitch + column];
                std::uint8_t &into =
                    line.coverage.values[static_cast<std::size_t>(y) * static_cast<std::size_t>(line.coverage.width)
                                         + static_cast<std::size_t>(x)];
                into = std::max(into, value);
            }
        }
    }
    return line;
}

} // namespace rb::stream
