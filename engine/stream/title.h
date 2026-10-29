// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace rb::stream
{

/// **What is playing**, as whatever knows says it. The text file a
/// Broadcast block can be pointed at fills in the title; anything else that
/// knows more (a media player, say) hands the stream a whole track through
/// BroadcastStream::setNowPlaying().
struct Track {
    std::string artist{};
    std::string title{};
    std::string album{};

    [[nodiscard]] bool operator==(const Track &other) const = default;
};

/// The title listeners see, from a template with tokens in it:
///
/// - `%artist`, `%title`, `%album` — the track's
/// - `%session` — the session's name
/// - `%%` — a percent sign
///
/// **The text between two tokens goes with them**: it is kept only when
/// the tokens either side of it both have something to say, so that
/// "%artist - %title" with no artist is the title alone rather than
/// " - Title". Text after the last token goes with that token, so
/// "%title (%album)" with no album has no brackets; text before the first
/// is a label, and stays. A template with no tokens at all is a title of
/// its own. Empty when every token was empty: there is nothing to tell the
/// listeners.
[[nodiscard]] std::string renderTitle(std::string_view pattern, const Track &track, std::string_view session);

/// The title in a now-playing file: its first line that has anything on
/// it, trimmed, as UTF-8 and no longer than a server keeps. Nothing when
/// the file cannot be read. **The file is somebody else's**, written by
/// whatever plays the music, and may be half-written when it is read; a
/// title that is wrong for a second is put right at the next look.
[[nodiscard]] std::optional<std::string> readNowPlayingFile(const std::filesystem::path &path);

/// Watches a now-playing file, and says when what it holds has changed.
/// Looked at, not notified: a stream has a loop of its own anyway, and a
/// look at a file's time once a second costs nothing.
class NowPlayingFile
{
public:
    /// What the file holds now, if that is not what it held at the last
    /// look; nothing if it is, or the file is not there or not set.
    [[nodiscard]] std::optional<std::string> poll(const std::filesystem::path &path);

private:
    std::filesystem::path m_path;
    std::optional<std::filesystem::file_time_type> m_modified;
    std::optional<std::string> m_title;
};

} // namespace rb::stream
