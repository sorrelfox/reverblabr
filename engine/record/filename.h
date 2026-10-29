// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace rb::record
{

/// The longest a file name may be before its extension, in bytes. **Well
/// under the 255 bytes a Linux file system allows**, so that the extension
/// and a " (12)" that keeps it from landing on another file both still fit.
inline constexpr std::size_t kMaxFileNameBytes = 230;

/// What the tokens in a file-name pattern stand for.
///
/// **The time is given, not read.** A test has to be able to say what the
/// date is, and a recording split into several files wants each file named
/// for when it started rather than when the pattern was read.
struct FileNameValues {
    std::string session;
    std::string block;
    std::chrono::local_seconds when{};
    unsigned counter = 1;
};

/// \a pattern with its tokens filled in, made safe to use as a file name,
/// without an extension.
///
/// - `%session` — the session's name
/// - `%block` — the block's name, or its id when it has none
/// - `%date` — the day, as ISO 8601: `2026-09-25`
/// - `%year` — the year alone, which is what a Year tag holds
/// - `%time` — the time of day as `14-03-22`: ISO's order with hyphens,
///   because a colon is refused by the file systems on most memory sticks
///   and misread by several file managers
/// - `%counter` — a number, `001` upwards, that keeps names apart
/// - `%%` — a percent sign
///
/// Anything else is kept as it was typed, a `%` that starts no token
/// included, so a typo shows in the file name rather than vanishing.
[[nodiscard]] std::string renderFileName(std::string_view pattern, const FileNameValues &values);

/// \a name made into something every file system here will take: no `/`,
/// no control characters, no leading dot that would hide it, no space or
/// dot at either end, at most kMaxFileNameBytes bytes and cut only between
/// characters. Nothing left gives "Recording".
[[nodiscard]] std::string sanitizeFileName(std::string_view name);

/// Whether \a pattern has a `%counter` in it.
[[nodiscard]] bool hasCounter(std::string_view pattern);

/// **A path in \a folder that nothing is at yet**, from \a pattern and
/// \a values, with \a extension (no dot) after it.
///
/// A pattern with `%counter` counts up from values.counter until the name
/// is free. One without gets " (2)", " (3)" and so on after the name, so a
/// second recording in the same second never overwrites the first. \a exists
/// is asked rather than the disk, so a caller can hold names it is about to
/// use and a test needs no files.
///
/// \a counterUsed, when given, is set to the counter the name was made with,
/// so the next file of the same recording can start from the one after it.
[[nodiscard]] std::filesystem::path freePath(const std::filesystem::path &folder,
                                             std::string_view pattern,
                                             FileNameValues values,
                                             std::string_view extension,
                                             const std::function<bool(const std::filesystem::path &)> &exists,
                                             unsigned *counterUsed = nullptr);

} // namespace rb::record
