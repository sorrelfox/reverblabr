// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "record/audiofile.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace rb::record
{

/// **Which M4A encoders this system's FFmpeg has**, found by asking it for
/// each one and opening it. Having a codec's name is not the same as
/// having its encoder: a distribution's FFmpeg may list AAC and refuse to
/// encode it, so an encoder counts only once it has opened.
struct EncoderProbe {
    bool aac = false;
    bool alac = false;
};

/// What FFmpeg says, asked once and remembered: the answer does not change
/// while the program runs.
[[nodiscard]] const EncoderProbe &probeEncoders();

/// Why \a format cannot be written on this system, as the name the engine
/// puts in a reason ("encoderMissing"), or nothing if it can. Only the M4A
/// formats can be missing; everything else is libsndfile's, which is always
/// there. \a probe is what to judge by; the system's answer by default.
[[nodiscard]] std::optional<std::string_view> formatUnavailable(FileFormat format,
                                                                const EncoderProbe &probe = probeEncoders());

/// One M4A file being written through FFmpeg: AAC-LC with FFmpeg's own
/// encoder, or ALAC.
///
/// **Fragmented while it is written.** The file is an MP4 whose index is
/// written a second at a time, in fragments, rather than once at the end:
/// a process killed in the middle leaves a file that plays up to the last
/// fragment, with nothing to repair. **A regular MP4 when it is finished**:
/// close() copies the fragments into one ordinary MP4 in its place, which
/// is what the players that are fussy about fragments want, and which is
/// what TagLib tags. If that copy fails, the fragmented file is kept: it
/// plays.
///
/// No patented encoder is ever bundled or chosen: HE-AAC and FFmpeg's
/// other AAC encoders are left alone.
class M4aFile
{
public:
    [[nodiscard]] static std::expected<std::unique_ptr<M4aFile>, FileError> create(const std::filesystem::path &path,
                                                                                   const FileSpec &spec);
    ~M4aFile();
    M4aFile(const M4aFile &) = delete;
    M4aFile &operator=(const M4aFile &) = delete;

    /// Interleaved frames, a whole number of them.
    [[nodiscard]] std::expected<void, FileError> write(std::span<const float> samples);
    /// Hands what has been written to the disk.
    void flush();
    /// Finishes the file: the encoder's last frames, the index, and the
    /// copy into a regular MP4.
    [[nodiscard]] std::expected<void, FileError> close();

private:
    M4aFile();

    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace rb::record
