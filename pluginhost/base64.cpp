// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "base64.h"

#include <cctype>
#include <cstdint>
#include <string_view>

namespace rb::pluginhost
{

namespace
{

constexpr std::string_view kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

} // namespace

std::string toBase64(const std::string &bytes)
{
    std::string text;
    text.reserve((bytes.size() + 2) / 3 * 4);
    std::size_t at = 0;
    while (at + 2 < bytes.size()) {
        const auto triple = (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at])) << 16)
            | (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + 1])) << 8)
            | static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + 2]));
        for (const int shift : {18, 12, 6, 0}) {
            text += kAlphabet[(triple >> shift) & 0x3f];
        }
        at += 3;
    }
    if (const std::size_t left = bytes.size() - at; left > 0) {
        std::uint32_t triple = static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at])) << 16;
        if (left == 2) {
            triple |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + 1])) << 8;
        }
        text += kAlphabet[(triple >> 18) & 0x3f];
        text += kAlphabet[(triple >> 12) & 0x3f];
        text += left == 2 ? kAlphabet[(triple >> 6) & 0x3f] : '=';
        text += '=';
    }
    return text;
}

std::optional<std::string> fromBase64(const std::string &text)
{
    std::string bytes;
    std::uint32_t gathered = 0;
    int bits = 0;
    for (const char c : text) {
        if (c == '=' || std::isspace(static_cast<unsigned char>(c)) != 0) {
            continue;
        }
        const std::size_t value = kAlphabet.find(c);
        if (value == std::string_view::npos) {
            return std::nullopt;
        }
        gathered = (gathered << 6) | static_cast<std::uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            bytes += static_cast<char>((gathered >> bits) & 0xff);
        }
    }
    return bytes;
}

} // namespace rb::pluginhost
