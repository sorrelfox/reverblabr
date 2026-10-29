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
#include <vector>

namespace rb::record
{

/// An impulse response as a file holds it: its rate and every
/// channel, one after the other rather than interleaved.
struct ImpulseFile {
    std::uint32_t sampleRate = 0;
    std::vector<std::vector<float>> channels;

    [[nodiscard]] std::size_t frames() const noexcept
    {
        return channels.empty() ? 0 : channels.front().size();
    }
};

/// Why an impulse file could not be read. The names are what the FIR
/// Filter publishes and the interface words.
enum class ImpulseFileError : std::uint8_t {
    Unreadable, ///< Not there, not allowed, or not audio libsndfile knows.
    Empty, ///< Audio, but not one frame of it.
    TooLong, ///< Longer than kMaxImpulseFileSeconds: a song, not a response.
};

/// The longest file taken as an impulse response. **A limit on reading,
/// not on the response**: a minute of eight channels is a few tens of
/// megabytes, where somebody picking a whole album by mistake would be
/// gigabytes. The block keeps less than this (FirFilter::kMaxSeconds).
inline constexpr double kMaxImpulseFileSeconds = 60.0;

/// Reads the whole of \a path: WAV, FLAC, AIFF or anything else libsndfile
/// decodes. **Not for the audio thread**: it reads the disk and allocates.
///
/// A leading "~" is the home folder, as the interface writes a path under
/// it, so that a session taken to another account finds that account's
/// file of the same name.
[[nodiscard]] std::expected<ImpulseFile, ImpulseFileError> readImpulseFile(const std::filesystem::path &path);

/// \a impulse at \a toRate, through the same resampler the recordings use:
/// the same length in time, aligned at its first sample. The channels as
/// they were when the rates are the same.
[[nodiscard]] std::vector<std::vector<float>> resampleImpulse(const ImpulseFile &impulse, std::uint32_t toRate);

} // namespace rb::record
