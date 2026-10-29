// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace rb::stream::text
{

// **ASCII, whatever the locale.** What goes to a server is the protocol's
// bytes, and <cctype> answers by the C locale of whoever runs the engine.

[[nodiscard]] constexpr bool isSpace(char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

[[nodiscard]] constexpr bool isAlnum(char c) noexcept
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

[[nodiscard]] constexpr char lower(char c) noexcept
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

[[nodiscard]] constexpr std::string_view trimmed(std::string_view text) noexcept
{
    while (!text.empty() && isSpace(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && isSpace(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

/// Cuts \a text to at most \a maxBytes, **and only when it is longer**,
/// at the start of a character: a character the cut would split is left
/// out whole, and everything before it is kept as it was.
inline void cutUtf8(std::string &text, std::size_t maxBytes)
{
    if (text.size() <= maxBytes) {
        return;
    }
    std::size_t end = maxBytes;
    // Back from the cut to the first byte of the character it falls in;
    // when that is the cut itself, nothing is split.
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80) {
        --end;
    }
    text.resize(end);
}

} // namespace rb::stream::text
