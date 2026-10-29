// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "record/filename.h"

#include <array>
#include <format>
#include <string>
#include <utility>

namespace rb::record
{

namespace
{

constexpr std::string_view kSession = "%session";
constexpr std::string_view kBlock = "%block";
constexpr std::string_view kDate = "%date";
constexpr std::string_view kYear = "%year";
constexpr std::string_view kTime = "%time";
constexpr std::string_view kCounter = "%counter";

/// What a name that came out empty is called instead.
constexpr std::string_view kFallbackName = "Recording";

/// How many names freePath() tries before it gives up counting and makes
/// the name unique with the counter it has reached.
constexpr unsigned kMaxAttempts = 100000;

[[nodiscard]] bool isContinuationByte(char byte)
{
    return (static_cast<unsigned char>(byte) & 0xC0U) == 0x80U;
}

[[nodiscard]] std::string trimmed(std::string text)
{
    const auto isEdge = [](char c) { return c == ' ' || c == '.' || c == '\t'; };
    std::size_t begin = 0;
    while (begin < text.size() && isEdge(text[begin])) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && isEdge(text[end - 1])) {
        --end;
    }
    return text.substr(begin, end - begin);
}

} // namespace

std::string renderFileName(std::string_view pattern, const FileNameValues &values)
{
    const std::array<std::pair<std::string_view, std::string>, 6> tokens{{
        {kYear, std::format("{:%Y}", values.when)},
        {kSession, values.session},
        {kBlock, values.block},
        {kDate, std::format("{:%Y-%m-%d}", values.when)},
        {kTime, std::format("{:%H-%M-%S}", values.when)},
        {kCounter, std::format("{:03}", values.counter)},
    }};

    std::string out;
    out.reserve(pattern.size() + 32);
    std::size_t at = 0;
    while (at < pattern.size()) {
        if (pattern[at] != '%') {
            out.push_back(pattern[at]);
            ++at;
            continue;
        }
        if (pattern.substr(at, 2) == "%%") {
            out.push_back('%');
            at += 2;
            continue;
        }
        bool matched = false;
        for (const auto &[token, value] : tokens) {
            if (pattern.substr(at, token.size()) == token) {
                out += value;
                at += token.size();
                matched = true;
                break;
            }
        }
        if (!matched) {
            out.push_back('%');
            ++at;
        }
    }
    return out;
}

std::string sanitizeFileName(std::string_view name)
{
    std::string clean;
    clean.reserve(name.size());
    for (const char c : name) {
        const auto byte = static_cast<unsigned char>(c);
        if (c == '/' || c == '\\') {
            // A session called "Mix 1/2" is a name, not a folder.
            clean.push_back('-');
        } else if (byte < 0x20U || byte == 0x7FU) {
            // A newline or a tab in a file name is only ever a mistake, and
            // a NUL would end it early.
            clean.push_back(' ');
        } else {
            clean.push_back(c);
        }
    }
    clean = trimmed(std::move(clean));

    if (clean.size() > kMaxFileNameBytes) {
        std::size_t cut = kMaxFileNameBytes;
        // Back to the start of a character: half of one is not UTF-8.
        while (cut > 0 && isContinuationByte(clean[cut])) {
            --cut;
        }
        clean.resize(cut);
        clean = trimmed(std::move(clean));
    }
    if (clean.empty()) {
        return std::string{kFallbackName};
    }
    return clean;
}

bool hasCounter(std::string_view pattern)
{
    return pattern.find(kCounter) != std::string_view::npos;
}

std::filesystem::path freePath(const std::filesystem::path &folder,
                               std::string_view pattern,
                               FileNameValues values,
                               std::string_view extension,
                               const std::function<bool(const std::filesystem::path &)> &exists,
                               unsigned *counterUsed)
{
    if (counterUsed != nullptr) {
        *counterUsed = values.counter;
    }
    const auto withExtension = [&](const std::string &stem) { return folder / (stem + "." + std::string{extension}); };

    if (hasCounter(pattern)) {
        for (unsigned attempt = 0; attempt < kMaxAttempts; ++attempt, ++values.counter) {
            const std::filesystem::path path = withExtension(sanitizeFileName(renderFileName(pattern, values)));
            if (!exists(path)) {
                if (counterUsed != nullptr) {
                    *counterUsed = values.counter;
                }
                return path;
            }
        }
    }

    const std::string stem = sanitizeFileName(renderFileName(pattern, values));
    std::filesystem::path path = withExtension(stem);
    for (unsigned copy = 2; exists(path) && copy < kMaxAttempts; ++copy) {
        path = withExtension(std::format("{} ({})", stem, copy));
    }
    return path;
}

} // namespace rb::record
