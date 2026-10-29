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
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

// libsndfile's handle, kept out of every header that includes this one.
struct sf_private_tag;

namespace rb::record
{

class M4aFile;

/// The kinds of file a Recorder writes. The ids are the ones a session
/// holds (blocks::Recorder::kFormat…).
enum class FileFormat : std::uint8_t {
    /// WAV, which becomes RF64 by itself if it grows past 4 GB: a plain WAV
    /// cannot say how long it is beyond that.
    Wav,
    Aiff,
    Flac,
    Mp3,
    Opus,
    Vorbis,
    /// M4A through FFmpeg, only where the system's FFmpeg encodes it
    /// (formatUnavailable() in m4afile.h): AAC-LC, and Apple Lossless.
    AacM4a,
    AlacM4a,
};

/// How each sample is stored, where the format stores samples. FLAC has no
/// floating point, so Float32 is written to it as 24 bits; the lossy
/// formats encode what they are given and ignore it.
enum class SampleFormat : std::uint8_t {
    Int16,
    Int24,
    Float32,
};

[[nodiscard]] std::optional<FileFormat> fileFormatFromId(std::string_view id);
[[nodiscard]] std::string_view fileFormatId(FileFormat format) noexcept;
/// The extension a file of this format gets, without the dot.
[[nodiscard]] std::string_view fileExtension(FileFormat format) noexcept;
[[nodiscard]] std::optional<SampleFormat> sampleFormatFromId(std::string_view id);

/// Whether a file in this format has a header that says how long it is,
/// and so is wrong about it after a crash until it is repaired. The others
/// are a stream of frames that ends where it ends.
[[nodiscard]] bool hasLengthInHeader(FileFormat format) noexcept;

/// What a file is to be.
struct FileSpec {
    FileFormat format = FileFormat::Wav;
    SampleFormat sampleFormat = SampleFormat::Int24;
    std::uint32_t sampleRate = 48000;
    std::uint32_t channels = 2;
    /// Where the dither's noise starts. Fixed by default, so the same audio
    /// makes the same file and a test can say what to expect.
    std::uint64_t ditherSeed = 0x2545F4914F6CDD1DULL;
    /// Whether rounding to 16 or 24 bits is dithered. **On unless somebody
    /// has a reason**: a test that has to find every sample exactly where it
    /// put it, or somebody writing a signal that is already at that depth.
    bool dither = true;
    /// **What a lossy format aims at**, in kilobits a second for the
    /// whole file: MP3, Opus, Vorbis and AAC. libsndfile takes it as a
    /// compression level between its encoders' highest and lowest rates,
    /// and FFmpeg's AAC as the rate itself.
    std::uint32_t bitrateKbps = 256;
    /// Variable rate, spent where the sound needs it, or the same rate all
    /// the way through. AAC is always the latter here.
    bool variableBitrate = true;
};

/// The kilobits a second a lossy format can be asked for, and the one a
/// Recorder asks for unless told otherwise.
inline constexpr std::uint32_t kMinBitrateKbps = 32;
inline constexpr std::uint32_t kMaxBitrateKbps = 320;

/// Whether \a format is one that aims at a bit rate rather than keeping
/// every sample.
[[nodiscard]] bool isLossy(FileFormat format) noexcept;

/// Why a file could not be made or written. The names are what the engine
/// puts in an error's "reason".
enum class FileError : std::uint8_t {
    FolderNotCreated, ///< The folder is not there and could not be made.
    FileExists, ///< Something is already at that path; it is never overwritten.
    FileNotCreated, ///< The file could not be made there: permissions, a read-only disk.
    UnsupportedFormat, ///< This format cannot be written at this rate or with these channels.
    DiskFull, ///< The disk has no room left.
    WriteFailed, ///< Writing failed for another reason.
    /// This system's FFmpeg cannot encode this format: a distribution
    /// may ship it without the encoder.
    EncoderMissing,
};

[[nodiscard]] std::string_view fileErrorName(FileError error) noexcept;

/// One audio file being written, through libsndfile.
///
/// **A file is never overwritten.** It is made with O_EXCL, so a name that
/// is already taken is a FileExists rather than somebody's earlier
/// recording gone.
///
/// **Fewer bits get dither.** Writing a 16- or 24-bit file from the
/// engine's floating point rounds every sample, and rounding a quiet
/// passage the same way every time turns into distortion that follows the
/// signal. Triangular noise of one step either side, added before the
/// rounding, trades that for a noise floor that follows nothing, which is
/// what every mastering tool does and what nobody can hear at 24 bits.
///
/// **A crash leaves a file that plays.** refreshHeader() writes the length
/// so far into a WAV's or an AIFF's header, and the writer calls it every
/// few seconds, so a process killed in the middle leaves a file that plays
/// up to the last refresh — and repairFile() gives it the rest.
class AudioFileWriter
{
public:
    /// Makes the file and its folder. The file exists, empty but for its
    /// header, when this succeeds.
    [[nodiscard]] static std::expected<AudioFileWriter, FileError> create(const std::filesystem::path &path,
                                                                          const FileSpec &spec);

    AudioFileWriter(AudioFileWriter &&other) noexcept;
    AudioFileWriter &operator=(AudioFileWriter &&other) noexcept;
    AudioFileWriter(const AudioFileWriter &) = delete;
    AudioFileWriter &operator=(const AudioFileWriter &) = delete;
    /// Closes the file if close() was not called.
    ~AudioFileWriter();

    /// Appends interleaved frames: \a samples holds a whole number of
    /// frames of spec().channels samples.
    [[nodiscard]] std::expected<void, FileError> write(std::span<const float> samples);

    /// Writes the length so far into the header, and asks the disk to keep
    /// what has been written. For a format with no length in its header it
    /// only does the second.
    void refreshHeader();

    /// Finishes the file: the encoders flush, the header is final.
    [[nodiscard]] std::expected<void, FileError> close();

    [[nodiscard]] std::uint64_t framesWritten() const noexcept
    {
        return m_frames;
    }

    [[nodiscard]] const std::filesystem::path &path() const noexcept
    {
        return m_path;
    }

    [[nodiscard]] const FileSpec &spec() const noexcept
    {
        return m_spec;
    }

private:
    AudioFileWriter() = default;

    [[nodiscard]] bool quantizes() const noexcept;
    [[nodiscard]] float nextNoise() noexcept;

    sf_private_tag *m_file = nullptr;
    /// An M4A is FFmpeg's, not libsndfile's.
    std::unique_ptr<M4aFile> m_m4a;
    std::filesystem::path m_path;
    FileSpec m_spec;
    std::uint64_t m_frames = 0;
    /// Where the dithered samples are put together before they go to the
    /// file.
    std::vector<int> m_scratch;
    std::uint64_t m_noise = 0;
    int m_bits = 0;
};

} // namespace rb::record
