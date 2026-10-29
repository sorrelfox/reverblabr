// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "signals.h"

#include <expected>
#include <string>
#include <string_view>

namespace rb::test
{

/// Why an audio file could not be read or written.
enum class AudioFileError {
    NotSupported, ///< This build has no libsndfile.
    NotReadable,
    NotWritable,
};

[[nodiscard]] std::string_view audioFileErrorName(AudioFileError error) noexcept;

/// What a file held, and at what rate.
struct AudioFile {
    MultiSignal channels;
    double sampleRate = kTestSampleRate;
};

/// Whether this build can read and write audio files at all.
[[nodiscard]] bool audioFilesSupported() noexcept;

/// Reads a WAV or FLAC file into one signal per channel, as floats.
///
/// Fixtures are made by the tests themselves or come from a CC0 source; no
/// recording of anyone else's is ever committed (see docs/adr/0005).
[[nodiscard]] std::expected<AudioFile, AudioFileError> readAudioFile(const std::string &path);

/// Writes one signal per channel as a 32-bit float WAV. Used to keep a golden
/// file when a test wants one, and to make a fixture on the spot.
[[nodiscard]] std::expected<void, AudioFileError>
writeWavFile(const std::string &path, const MultiSignal &channels, double sampleRate = kTestSampleRate);

} // namespace rb::test
