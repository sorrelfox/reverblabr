// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace rb::record
{

/// What repairing a file found.
struct RepairResult {
    /// How many frames the file holds now.
    std::uint64_t frames = 0;
    /// Whether anything had to be written: a file that was closed properly,
    /// or one in a format with no length to get wrong, needs nothing.
    bool changed = false;
};

enum class RepairError : std::uint8_t {
    NotReadable, ///< The file is not there, or cannot be opened.
    NotRecognised, ///< It is not a file this knows how to repair.
    NotWritable, ///< Its header could not be written back.
};

[[nodiscard]] std::string_view repairErrorName(RepairError error) noexcept;

/// **Makes a recording that was cut off say how long it really is.**
///
/// A WAV or an AIFF says in its header how much audio follows. The writer
/// brings that up to date every few seconds, so a process killed in the
/// middle of a recording leaves a file that plays — up to the last update.
/// Everything after it is on the disk too; the header just does not admit
/// to it. This measures what is there and writes it into the header:
///
/// - **WAV**: the RIFF and data sizes, the fact chunk's count where there is
///   one, and **RF64** — its ds64 chunk — for a file past 4 GB, including
///   one that was still a plain WAV the last time its header was written.
/// - **AIFF and AIFF-C**: the FORM and SSND sizes and COMM's frame count.
/// - **FLAC**: the total in STREAMINFO, which is left at zero ("not known")
///   until the encoder finishes, by decoding what is there.
/// - Ogg and MP3 have no length to be wrong about and are left alone.
///
/// A partial frame at the end — the last write cut through the middle of
/// one — is cut off.
[[nodiscard]] std::expected<RepairResult, RepairError> repairFile(const std::filesystem::path &path);

/// **The recordings that are being written, written down**, so that the
/// next start can find the ones a crash left behind.
///
/// Each recording is one small file in the journal's folder holding its
/// path, made before the first frame is written and removed once the file
/// has been closed. One file each rather than one list, so two sessions
/// recording at once never write the same file; and named for the process,
/// so that a second copy of the engine — a test, running beside another —
/// does not repair recordings that are still being made.
class RecordingJournal
{
public:
    explicit RecordingJournal(std::filesystem::path folder);

    /// Writes down that \a recording is being written. The entry's own path,
    /// for remove(), or nothing if it could not be made — in which case a
    /// crash leaves a file that is repaired by hand rather than by itself,
    /// which is not a reason to refuse to record.
    [[nodiscard]] std::optional<std::filesystem::path> add(const std::filesystem::path &recording);

    void remove(const std::filesystem::path &entry);

    [[nodiscard]] const std::filesystem::path &folder() const noexcept
    {
        return m_folder;
    }

private:
    std::filesystem::path m_folder;
};

/// One recording a crash left behind, and what repairing it did.
struct RepairReport {
    std::filesystem::path file;
    std::expected<RepairResult, RepairError> result;
};

/// Repairs every recording the journal in \a folder names whose process is
/// no longer running, and forgets it.
[[nodiscard]] std::vector<RepairReport> repairOrphans(const std::filesystem::path &folder);

} // namespace rb::record
