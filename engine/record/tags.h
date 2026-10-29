// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "record/audiofile.h"
#include "record/filename.h"

#include <filesystem>
#include <optional>
#include <string>

namespace rb::record
{

/// A recording's tags. Every one is text, and every one may hold the
/// file-name tokens — `%session`, `%date`, `%year`, `%counter` and the rest
/// — so a Track of "%counter" numbers the files a recording splits into.
/// An empty tag is not written.
struct Tags {
    std::string title;
    std::string artist;
    std::string album;
    std::string genre;
    std::string year;
    std::string track;
    std::string trackCount;
    std::string disc;
    std::string discCount;
    std::string comment;

    [[nodiscard]] bool isEmpty() const;
    [[nodiscard]] bool operator==(const Tags &other) const = default;
};

/// \a patterns with their tokens filled in for one file, and the space
/// either side of each taken off.
[[nodiscard]] Tags renderTags(const Tags &patterns, const FileNameValues &values);

/// **Writes \a tags into a finished file**, in the tags its format has:
///
/// - MP3: ID3v2.4, and nothing else (no ID3v1, which holds only Latin-1)
/// - FLAC, Ogg Opus and Ogg Vorbis: Vorbis comments, with the totals as
///   TRACKTOTAL and DISCTOTAL
/// - WAV: a RIFF INFO list, which most players read, and an ID3v2 chunk,
///   which the rest do
/// - AIFF: an ID3v2 chunk
/// - M4A: MP4 atoms
///
/// Text is UTF-8 throughout, so Cyrillic, CJK and everything else survive.
/// A WAV past 4 GB is RF64, which TagLib cannot rewrite, and is left alone.
/// False if the tags could not be written; the audio is never touched.
[[nodiscard]] bool writeTags(const std::filesystem::path &path, FileFormat format, const Tags &tags);

/// The tags a file holds, read back the way writeTags() wrote them — from a
/// WAV, its ID3v2 chunk, which has room for every field — or
/// nothing if it cannot be read. For the tests now, and for the Recording
/// Inspector later.
[[nodiscard]] std::optional<Tags> readTags(const std::filesystem::path &path, FileFormat format);

} // namespace rb::record
