// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "stream/title.h"

#include "stream/text.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <system_error>
#include <vector>

namespace rb::stream
{

namespace
{

/// The most of a title any server keeps, in bytes.
constexpr std::size_t kMaxTitleBytes = 256;

/// One piece of a template: text as it is, or a token's value.
struct Piece {
    std::string text;
    bool token = false;
};

[[nodiscard]] std::string_view trimmed(std::string_view value)
{
    return text::trimmed(value);
}

/// \a text with nothing that ends a line, no longer than a server keeps,
/// and not cut in the middle of a character.
[[nodiscard]] std::string fitted(std::string_view text)
{
    std::string out;
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 0x20 || byte == 0x7f) {
            continue;
        }
        out.push_back(c);
    }
    text::cutUtf8(out, kMaxTitleBytes);
    return std::string(trimmed(out));
}

} // namespace

std::string renderTitle(std::string_view pattern, const Track &track, std::string_view session)
{
    std::vector<Piece> pieces;
    const auto addText = [&pieces](std::string_view text) {
        if (pieces.empty() || pieces.back().token) {
            pieces.push_back(Piece{std::string(text), false});
        } else {
            pieces.back().text += text;
        }
    };
    constexpr std::array<std::pair<std::string_view, int>, 4> kTokens{
        {{"%artist", 0}, {"%title", 1}, {"%album", 2}, {"%session", 3}}};
    std::size_t at = 0;
    while (at < pattern.size()) {
        if (pattern[at] != '%') {
            const std::size_t next = pattern.find('%', at);
            addText(pattern.substr(at, next == std::string_view::npos ? std::string_view::npos : next - at));
            at = next == std::string_view::npos ? pattern.size() : next;
            continue;
        }
        if (pattern.substr(at).starts_with("%%")) {
            addText("%");
            at += 2;
            continue;
        }
        bool matched = false;
        for (const auto &[name, which] : kTokens) {
            if (pattern.substr(at).starts_with(name)) {
                const std::string_view values[] = {track.artist, track.title, track.album, session};
                pieces.push_back(Piece{std::string(trimmed(values[which])), true});
                at += name.size();
                matched = true;
                break;
            }
        }
        if (!matched) {
            addText("%");
            ++at;
        }
    }

    bool anyToken = false;
    bool anyValue = false;
    for (const Piece &piece : pieces) {
        anyToken = anyToken || piece.token;
        anyValue = anyValue || (piece.token && !piece.text.empty());
    }
    if (!anyToken) {
        return pieces.empty() ? std::string{} : fitted(pieces.front().text);
    }
    if (!anyValue) {
        return {};
    }

    std::string out;
    for (std::size_t index = 0; index < pieces.size(); ++index) {
        const Piece &piece = pieces[index];
        if (piece.token) {
            out += piece.text;
            continue;
        }
        // The nearest token either side, if there is one.
        const Piece *before = nullptr;
        for (std::size_t back = index; back-- > 0;) {
            if (pieces[back].token) {
                before = &pieces[back];
                break;
            }
        }
        const Piece *after = nullptr;
        for (std::size_t ahead = index + 1; ahead < pieces.size(); ++ahead) {
            if (pieces[ahead].token) {
                after = &pieces[ahead];
                break;
            }
        }
        // Text in front of everything is a label, and stays; text between
        // two tokens, or after the last, belongs to the token before it.
        const bool keep = before == nullptr || (!before->text.empty() && (after == nullptr || !after->text.empty()));
        if (keep) {
            out += piece.text;
        }
    }
    return fitted(out);
}

std::optional<std::string> readNowPlayingFile(const std::filesystem::path &path)
{
    // **A regular file, and nothing that can make a read wait**: a FIFO or a
    // device named here would hold the stream's thread until somebody wrote
    // to it, and a session that is stopping waits for that thread.
    const int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOCTTY);
    if (fd < 0) {
        return std::nullopt;
    }
    struct stat info{};
    if (::fstat(fd, &info) != 0 || !S_ISREG(info.st_mode)) {
        ::close(fd);
        return std::nullopt;
    }
    // A now-playing file is a line or two; one that is not is read no
    // further than its start.
    constexpr std::size_t kMaxRead = 4096;
    std::string content(kMaxRead, '\0');
    std::size_t got = 0;
    while (got < kMaxRead) {
        const ssize_t read = ::read(fd, content.data() + got, kMaxRead - got);
        if (read < 0 && errno == EINTR) {
            continue;
        }
        if (read <= 0) {
            break;
        }
        got += static_cast<std::size_t>(read);
    }
    ::close(fd);
    content.resize(got);
    // A byte-order mark is not part of the title.
    if (content.starts_with("\xEF\xBB\xBF")) {
        content.erase(0, 3);
    }
    std::string_view rest = content;
    while (!rest.empty()) {
        const std::size_t end = rest.find('\n');
        const std::string_view line = trimmed(rest.substr(0, end));
        if (!line.empty()) {
            return fitted(line);
        }
        if (end == std::string_view::npos) {
            break;
        }
        rest.remove_prefix(end + 1);
    }
    return std::string{};
}

std::optional<std::string> NowPlayingFile::poll(const std::filesystem::path &path)
{
    if (path != m_path) {
        m_path = path;
        m_modified.reset();
        m_title.reset();
    }
    if (path.empty()) {
        return std::nullopt;
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) {
        return std::nullopt;
    }
    const std::filesystem::file_time_type modified = std::filesystem::last_write_time(path, error);
    if (error) {
        return std::nullopt;
    }
    if (m_modified == modified) {
        return std::nullopt;
    }
    m_modified = modified;
    std::optional<std::string> title = readNowPlayingFile(path);
    if (!title || title == m_title) {
        return std::nullopt;
    }
    m_title = title;
    return title;
}

} // namespace rb::stream
