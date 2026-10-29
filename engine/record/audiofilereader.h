// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>

namespace rb::record
{

/// One recording read back: what a preview plays, what the
/// Recording Inspector draws and what the recordings index reads a
/// finished file's outline from.
///
/// **Every format the Recorder writes.** WAV, AIFF, FLAC, MP3, Opus and
/// Vorbis through libsndfile; M4A — AAC and Apple Lossless — through
/// FFmpeg, which libsndfile does not read.
///
/// Frames come out interleaved, as floats from −1 to 1 whatever the file
/// stores. Not thread-safe; one thread reads at a time.
class AudioFileReader
{
public:
    /// Opens \a path, or nothing if neither library can read it.
    [[nodiscard]] static std::optional<AudioFileReader> open(const std::filesystem::path &path);

    AudioFileReader(AudioFileReader &&other) noexcept;
    AudioFileReader &operator=(AudioFileReader &&other) noexcept;
    AudioFileReader(const AudioFileReader &) = delete;
    AudioFileReader &operator=(const AudioFileReader &) = delete;
    ~AudioFileReader();

    [[nodiscard]] std::uint32_t sampleRate() const noexcept;
    [[nodiscard]] std::uint32_t channels() const noexcept;
    /// How many frames the file says it holds. What decodes may be a
    /// little different for a lossy format; read() says where it ends.
    [[nodiscard]] std::uint64_t frames() const noexcept;

    /// Reads up to \a frames interleaved frames into \a out, which holds at
    /// least that many times channels() samples. Gives back how many it
    /// read: fewer at the end of the file, and 0 after it.
    std::size_t read(std::span<float> out, std::size_t frames);

    /// Moves to \a frame, clamped to the end. Gives back where it is now.
    /// **To the frame**, for M4A as well: FFmpeg seeks to the packet before
    /// it, and what decodes before the frame is dropped.
    std::uint64_t seek(std::uint64_t frame);

    /// What reads the file: libsndfile or FFmpeg, behind one face.
    struct Backend;

private:
    explicit AudioFileReader(std::unique_ptr<Backend> backend);

    std::unique_ptr<Backend> d;
};

} // namespace rb::record
