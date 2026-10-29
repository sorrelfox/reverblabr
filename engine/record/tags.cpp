// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "record/tags.h"

#include <aifffile.h>
#include <flacfile.h>
#include <id3v2tag.h>
#include <infotag.h>
#include <mp4file.h>
#include <mp4tag.h>
#include <mpegfile.h>
#include <opusfile.h>
#include <tpropertymap.h>
#include <tstring.h>
#include <vorbisfile.h>
#include <wavfile.h>
#include <xiphcomment.h>

#include <array>
#include <fstream>
#include <utility>

namespace rb::record
{

namespace
{

[[nodiscard]] std::string trimmed(std::string text)
{
    const auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    std::size_t begin = 0;
    while (begin < text.size() && isSpace(text[begin])) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && isSpace(text[end - 1])) {
        --end;
    }
    return text.substr(begin, end - begin);
}

[[nodiscard]] TagLib::String utf8(const std::string &text)
{
    return TagLib::String(text, TagLib::String::UTF8);
}

[[nodiscard]] std::string fromTagLib(const TagLib::String &text)
{
    return text.to8Bit(true);
}

/// "3" and "12" as the one "3/12" an ID3v2 or an INFO tag holds.
[[nodiscard]] std::string numberOf(const std::string &number, const std::string &count)
{
    if (number.empty()) {
        return {};
    }
    return count.empty() ? number : number + "/" + count;
}

/// Everything but the numbers, by the names TagLib's property interface
/// knows. The same names for every tag type: TagLib maps each to its own.
void putText(TagLib::PropertyMap &map, const Tags &tags)
{
    const std::array<std::pair<const char *, const std::string *>, 6> fields{{
        {"TITLE", &tags.title},
        {"ARTIST", &tags.artist},
        {"ALBUM", &tags.album},
        {"GENRE", &tags.genre},
        {"DATE", &tags.year},
        {"COMMENT", &tags.comment},
    }};
    for (const auto &[key, value] : fields) {
        if (!value->empty()) {
            map.replace(key, TagLib::StringList(utf8(*value)));
        }
    }
}

/// For a tag that holds "number/total" in one field: ID3v2 and INFO.
[[nodiscard]] TagLib::PropertyMap combinedMap(const Tags &tags)
{
    TagLib::PropertyMap map;
    putText(map, tags);
    if (const std::string track = numberOf(tags.track, tags.trackCount); !track.empty()) {
        map.replace("TRACKNUMBER", TagLib::StringList(utf8(track)));
    }
    if (const std::string disc = numberOf(tags.disc, tags.discCount); !disc.empty()) {
        map.replace("DISCNUMBER", TagLib::StringList(utf8(disc)));
    }
    return map;
}

/// For Vorbis comments, which keep the totals in fields of their own.
[[nodiscard]] TagLib::PropertyMap xiphMap(const Tags &tags)
{
    TagLib::PropertyMap map;
    putText(map, tags);
    const std::array<std::pair<const char *, const std::string *>, 4> numbers{{
        {"TRACKNUMBER", &tags.track},
        {"TRACKTOTAL", &tags.trackCount},
        {"DISCNUMBER", &tags.disc},
        {"DISCTOTAL", &tags.discCount},
    }};
    for (const auto &[key, value] : numbers) {
        if (!value->empty()) {
            map.replace(key, TagLib::StringList(utf8(*value)));
        }
    }
    return map;
}

[[nodiscard]] std::string first(const TagLib::PropertyMap &map, const char *key)
{
    const auto found = map.find(key);
    return found == map.end() || found->second.isEmpty() ? std::string{} : fromTagLib(found->second.front());
}

/// Splits "3/12" back into its number and its total.
void splitNumber(const std::string &combined, std::string &number, std::string &count)
{
    const std::size_t slash = combined.find('/');
    number = combined.substr(0, slash);
    count = slash == std::string::npos ? std::string{} : combined.substr(slash + 1);
}

[[nodiscard]] Tags fromMap(const TagLib::PropertyMap &map, bool combined)
{
    Tags tags;
    tags.title = first(map, "TITLE");
    tags.artist = first(map, "ARTIST");
    tags.album = first(map, "ALBUM");
    tags.genre = first(map, "GENRE");
    tags.year = first(map, "DATE");
    tags.comment = first(map, "COMMENT");
    if (combined) {
        splitNumber(first(map, "TRACKNUMBER"), tags.track, tags.trackCount);
        splitNumber(first(map, "DISCNUMBER"), tags.disc, tags.discCount);
    } else {
        tags.track = first(map, "TRACKNUMBER");
        tags.trackCount = first(map, "TRACKTOTAL");
        tags.disc = first(map, "DISCNUMBER");
        tags.discCount = first(map, "DISCTOTAL");
    }
    return tags;
}

/// **Whether the file is what it is said to be**, by its first bytes.
/// TagLib's MPEG reader looks for a frame anywhere in a file and will find
/// one in a FLAC; writing an ID3v2 tag onto that would damage it. A WAV that
/// has gone RF64 is not one TagLib can rewrite either.
[[nodiscard]] bool looksLike(const std::filesystem::path &path, FileFormat format)
{
    std::ifstream file(path, std::ios::binary);
    std::array<char, 12> head{};
    file.read(head.data(), head.size());
    if (!file) {
        return false;
    }
    const std::string_view magic(head.data(), 4);
    const std::string_view form(head.data() + 8, 4);
    const auto byte = [&head](std::size_t index) { return static_cast<unsigned char>(head[index]); };
    switch (format) {
    case FileFormat::Wav:
        return magic == "RIFF" && form == "WAVE";
    case FileFormat::Aiff:
        return magic == "FORM" && (form == "AIFF" || form == "AIFC");
    case FileFormat::Flac:
        return magic == "fLaC";
    case FileFormat::Opus:
    case FileFormat::Vorbis:
        return magic == "OggS";
    case FileFormat::Mp3:
        return magic.starts_with("ID3") || (byte(0) == 0xFFU && (byte(1) & 0xE0U) == 0xE0U);
    case FileFormat::AacM4a:
    case FileFormat::AlacM4a:
        return std::string_view(head.data() + 4, 4) == "ftyp";
    }
    return false;
}

} // namespace

bool Tags::isEmpty() const
{
    return *this == Tags{};
}

Tags renderTags(const Tags &patterns, const FileNameValues &values)
{
    const auto render = [&values](const std::string &pattern) {
        return pattern.empty() ? std::string{} : trimmed(renderFileName(pattern, values));
    };
    Tags tags;
    tags.title = render(patterns.title);
    tags.artist = render(patterns.artist);
    tags.album = render(patterns.album);
    tags.genre = render(patterns.genre);
    tags.year = render(patterns.year);
    tags.track = render(patterns.track);
    tags.trackCount = render(patterns.trackCount);
    tags.disc = render(patterns.disc);
    tags.discCount = render(patterns.discCount);
    tags.comment = render(patterns.comment);
    return tags;
}

bool writeTags(const std::filesystem::path &path, FileFormat format, const Tags &tags)
{
    if (tags.isEmpty()) {
        return true;
    }
    if (!looksLike(path, format)) {
        return false;
    }
    switch (format) {
    case FileFormat::Mp3: {
        TagLib::MPEG::File file(path.c_str(), false);
        if (!file.isValid()) {
            return false;
        }
        file.ID3v2Tag(true)->setProperties(combinedMap(tags));
        return file.save(TagLib::MPEG::File::ID3v2, TagLib::File::StripOthers, TagLib::ID3v2::v4);
    }
    case FileFormat::Flac: {
        TagLib::FLAC::File file(path.c_str(), false);
        if (!file.isValid()) {
            return false;
        }
        file.xiphComment(true)->setProperties(xiphMap(tags));
        return file.save();
    }
    case FileFormat::Opus: {
        TagLib::Ogg::Opus::File file(path.c_str(), false);
        if (!file.isValid() || file.tag() == nullptr) {
            return false;
        }
        file.tag()->setProperties(xiphMap(tags));
        return file.save();
    }
    case FileFormat::Vorbis: {
        TagLib::Ogg::Vorbis::File file(path.c_str(), false);
        if (!file.isValid() || file.tag() == nullptr) {
            return false;
        }
        file.tag()->setProperties(xiphMap(tags));
        return file.save();
    }
    case FileFormat::Wav: {
        TagLib::RIFF::WAV::File file(path.c_str(), false);
        if (!file.isValid()) {
            return false;
        }
        const TagLib::PropertyMap map = combinedMap(tags);
        file.InfoTag()->setProperties(map);
        file.ID3v2Tag()->setProperties(map);
        return file.save(TagLib::RIFF::WAV::File::AllTags, TagLib::File::StripOthers, TagLib::ID3v2::v4);
    }
    case FileFormat::Aiff: {
        TagLib::RIFF::AIFF::File file(path.c_str(), false);
        if (!file.isValid()) {
            return false;
        }
        file.tag()->setProperties(combinedMap(tags));
        return file.save(TagLib::ID3v2::v4);
    }
    case FileFormat::AacM4a:
    case FileFormat::AlacM4a: {
        TagLib::MP4::File file(path.c_str(), false);
        if (!file.isValid() || file.tag() == nullptr) {
            return false;
        }
        file.tag()->setProperties(combinedMap(tags));
        return file.save();
    }
    }
    return false;
}

std::optional<Tags> readTags(const std::filesystem::path &path, FileFormat format)
{
    if (!looksLike(path, format)) {
        return std::nullopt;
    }
    switch (format) {
    case FileFormat::Mp3: {
        TagLib::MPEG::File file(path.c_str(), false);
        if (!file.isValid()) {
            return std::nullopt;
        }
        return file.hasID3v2Tag() ? fromMap(file.ID3v2Tag()->properties(), true) : Tags{};
    }
    case FileFormat::Flac: {
        TagLib::FLAC::File file(path.c_str(), false);
        if (!file.isValid()) {
            return std::nullopt;
        }
        return file.hasXiphComment() ? fromMap(file.xiphComment()->properties(), false) : Tags{};
    }
    case FileFormat::Opus: {
        TagLib::Ogg::Opus::File file(path.c_str(), false);
        if (!file.isValid() || file.tag() == nullptr) {
            return std::nullopt;
        }
        return fromMap(file.tag()->properties(), false);
    }
    case FileFormat::Vorbis: {
        TagLib::Ogg::Vorbis::File file(path.c_str(), false);
        if (!file.isValid() || file.tag() == nullptr) {
            return std::nullopt;
        }
        return fromMap(file.tag()->properties(), false);
    }
    case FileFormat::Wav: {
        TagLib::RIFF::WAV::File file(path.c_str(), false);
        if (!file.isValid()) {
            return std::nullopt;
        }
        // The ID3v2 chunk, which has every field; the INFO list has no
        // place for a disc or a genre in every reader.
        return file.hasID3v2Tag() ? fromMap(file.ID3v2Tag()->properties(), true) : Tags{};
    }
    case FileFormat::Aiff: {
        TagLib::RIFF::AIFF::File file(path.c_str(), false);
        if (!file.isValid()) {
            return std::nullopt;
        }
        return file.hasID3v2Tag() ? fromMap(file.tag()->properties(), true) : Tags{};
    }
    case FileFormat::AacM4a:
    case FileFormat::AlacM4a: {
        TagLib::MP4::File file(path.c_str(), false);
        if (!file.isValid() || file.tag() == nullptr) {
            return std::nullopt;
        }
        return fromMap(file.tag()->properties(), true);
    }
    }
    return std::nullopt;
}

} // namespace rb::record
