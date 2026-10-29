// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "record/audiofile.h"

#include "record/m4afile.h"

#include <fcntl.h>
#include <sndfile.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <system_error>
#include <utility>

namespace rb::record
{

namespace
{

struct FormatEntry {
    FileFormat format;
    std::string_view id;
    std::string_view extension;
};

constexpr std::array kFormats{
    FormatEntry{FileFormat::Wav, "wav", "wav"},
    FormatEntry{FileFormat::Aiff, "aiff", "aiff"},
    FormatEntry{FileFormat::Flac, "flac", "flac"},
    FormatEntry{FileFormat::Mp3, "mp3", "mp3"},
    FormatEntry{FileFormat::Opus, "opus", "opus"},
    FormatEntry{FileFormat::Vorbis, "vorbis", "ogg"},
    FormatEntry{FileFormat::AacM4a, "aac", "m4a"},
    FormatEntry{FileFormat::AlacM4a, "alac", "m4a"},
};

[[nodiscard]] int subtypeFor(SampleFormat sample, bool floatAllowed)
{
    switch (sample) {
    case SampleFormat::Int16:
        return SF_FORMAT_PCM_16;
    case SampleFormat::Int24:
        return SF_FORMAT_PCM_24;
    case SampleFormat::Float32:
        return floatAllowed ? SF_FORMAT_FLOAT : SF_FORMAT_PCM_24;
    }
    return SF_FORMAT_PCM_24;
}

/// libsndfile's format word for a spec.
[[nodiscard]] int sndfileFormat(const FileSpec &spec)
{
    switch (spec.format) {
    case FileFormat::Wav:
        // RF64 that turns itself back into a plain WAV when it is closed
        // under 4 GB, which is nearly always: nobody's player has to know
        // what RF64 is unless the file needs it.
        return SF_FORMAT_RF64 | subtypeFor(spec.sampleFormat, true);
    case FileFormat::Aiff:
        return SF_FORMAT_AIFF | subtypeFor(spec.sampleFormat, true);
    case FileFormat::Flac:
        return SF_FORMAT_FLAC | subtypeFor(spec.sampleFormat, false);
    case FileFormat::Mp3:
        return SF_FORMAT_MPEG | SF_FORMAT_MPEG_LAYER_III;
    case FileFormat::Opus:
        return SF_FORMAT_OGG | SF_FORMAT_OPUS;
    case FileFormat::Vorbis:
        return SF_FORMAT_OGG | SF_FORMAT_VORBIS;
    case FileFormat::AacM4a:
    case FileFormat::AlacM4a:
        break; // FFmpeg's, never libsndfile's
    }
    return SF_FORMAT_RF64 | SF_FORMAT_PCM_24;
}

[[nodiscard]] int bitsFor(const FileSpec &spec)
{
    const int subtype = sndfileFormat(spec) & SF_FORMAT_SUBMASK;
    if (subtype == SF_FORMAT_PCM_16) {
        return 16;
    }
    if (subtype == SF_FORMAT_PCM_24) {
        return 24;
    }
    return 0; // floating point, or an encoder that takes floating point
}

[[nodiscard]] FileError writeErrorFromErrno(int error)
{
    return error == ENOSPC || error == EDQUOT ? FileError::DiskFull : FileError::WriteFailed;
}

} // namespace

std::optional<FileFormat> fileFormatFromId(std::string_view id)
{
    for (const FormatEntry &entry : kFormats) {
        if (entry.id == id) {
            return entry.format;
        }
    }
    return std::nullopt;
}

std::string_view fileFormatId(FileFormat format) noexcept
{
    for (const FormatEntry &entry : kFormats) {
        if (entry.format == format) {
            return entry.id;
        }
    }
    return "wav";
}

std::string_view fileExtension(FileFormat format) noexcept
{
    for (const FormatEntry &entry : kFormats) {
        if (entry.format == format) {
            return entry.extension;
        }
    }
    return "wav";
}

std::optional<SampleFormat> sampleFormatFromId(std::string_view id)
{
    if (id == "int16") {
        return SampleFormat::Int16;
    }
    if (id == "int24") {
        return SampleFormat::Int24;
    }
    if (id == "float32") {
        return SampleFormat::Float32;
    }
    return std::nullopt;
}

bool isLossy(FileFormat format) noexcept
{
    return format == FileFormat::Mp3 || format == FileFormat::Opus || format == FileFormat::Vorbis
        || format == FileFormat::AacM4a;
}

bool hasLengthInHeader(FileFormat format) noexcept
{
    return format == FileFormat::Wav || format == FileFormat::Aiff;
}

std::string_view fileErrorName(FileError error) noexcept
{
    switch (error) {
    case FileError::FolderNotCreated:
        return "FolderNotCreated";
    case FileError::FileExists:
        return "FileExists";
    case FileError::FileNotCreated:
        return "FileNotCreated";
    case FileError::UnsupportedFormat:
        return "UnsupportedFormat";
    case FileError::DiskFull:
        return "DiskFull";
    case FileError::WriteFailed:
        return "WriteFailed";
    case FileError::EncoderMissing:
        return "EncoderMissing";
    }
    return "WriteFailed";
}

std::expected<AudioFileWriter, FileError> AudioFileWriter::create(const std::filesystem::path &path,
                                                                  const FileSpec &spec)
{
    std::error_code error;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            return std::unexpected(FileError::FolderNotCreated);
        }
    }

    if (spec.format == FileFormat::AacM4a || spec.format == FileFormat::AlacM4a) {
        std::expected<std::unique_ptr<M4aFile>, FileError> m4a = M4aFile::create(path, spec);
        if (!m4a) {
            return std::unexpected(m4a.error());
        }
        AudioFileWriter writer;
        writer.m_m4a = std::move(*m4a);
        writer.m_path = path;
        writer.m_spec = spec;
        return writer;
    }

    SF_INFO info{};
    info.samplerate = static_cast<int>(spec.sampleRate);
    info.channels = static_cast<int>(spec.channels);
    info.format = sndfileFormat(spec);
    if (sf_format_check(&info) == 0) {
        return std::unexpected(FileError::UnsupportedFormat);
    }

    // **O_EXCL**: a name somebody else's file already has is refused here,
    // by the kernel, rather than found out afterwards by the person whose
    // recording it was.
    const int descriptor = ::open(path.c_str(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (descriptor < 0) {
        return std::unexpected(errno == EEXIST ? FileError::FileExists : FileError::FileNotCreated);
    }
    errno = 0;
    SNDFILE *file = sf_open_fd(descriptor, SFM_WRITE, &info, SF_TRUE);
    if (file == nullptr) {
        // The file was made, so the place is fine: what libsndfile refused
        // is what it was asked to write there — Opus at 44.1 kHz, say —
        // unless the disk itself said no to the header.
        const int system = errno;
        const bool refusedByDisk = sf_error(nullptr) == SF_ERR_SYSTEM && system != 0;
        ::close(descriptor);
        std::filesystem::remove(path, error);
        if (refusedByDisk) {
            return std::unexpected(writeErrorFromErrno(system));
        }
        return std::unexpected(FileError::UnsupportedFormat);
    }
    if (spec.format == FileFormat::Wav) {
        sf_command(file, SFC_RF64_AUTO_DOWNGRADE, nullptr, SF_TRUE);
    }
    // Past full scale is held there, not wrapped round to the other end:
    // a click at the top of a loud recording beats a crack.
    sf_command(file, SFC_SET_CLIPPING, nullptr, SF_TRUE);
    if (isLossy(spec.format)) {
        // **The rate first, then how much of it**: libsndfile's MP3 encoder
        // reads the second in the light of the first. A level of 0 is the
        // encoder's best, 1 its smallest; the rate asked for is placed
        // between the two in proportion.
        int mode = spec.variableBitrate ? SF_BITRATE_MODE_VARIABLE : SF_BITRATE_MODE_CONSTANT;
        sf_command(file, SFC_SET_BITRATE_MODE, &mode, sizeof(mode));
        const double kbps = std::clamp<double>(spec.bitrateKbps, kMinBitrateKbps, kMaxBitrateKbps);
        double level = (static_cast<double>(kMaxBitrateKbps) - kbps) / (kMaxBitrateKbps - kMinBitrateKbps);
        sf_command(file, SFC_SET_COMPRESSION_LEVEL, &level, sizeof(level));
    }

    AudioFileWriter writer;
    writer.m_file = file;
    writer.m_path = path;
    writer.m_spec = spec;
    writer.m_bits = bitsFor(spec);
    writer.m_noise = spec.ditherSeed == 0 ? 1 : spec.ditherSeed;
    return writer;
}

AudioFileWriter::AudioFileWriter(AudioFileWriter &&other) noexcept
    : m_file(std::exchange(other.m_file, nullptr))
    , m_m4a(std::move(other.m_m4a))
    , m_path(std::move(other.m_path))
    , m_spec(other.m_spec)
    , m_frames(other.m_frames)
    , m_scratch(std::move(other.m_scratch))
    , m_noise(other.m_noise)
    , m_bits(other.m_bits)
{
}

AudioFileWriter &AudioFileWriter::operator=(AudioFileWriter &&other) noexcept
{
    if (this != &other) {
        if (m_file != nullptr) {
            sf_close(m_file);
        }
        m_file = std::exchange(other.m_file, nullptr);
        m_m4a = std::move(other.m_m4a);
        m_path = std::move(other.m_path);
        m_spec = other.m_spec;
        m_frames = other.m_frames;
        m_scratch = std::move(other.m_scratch);
        m_noise = other.m_noise;
        m_bits = other.m_bits;
    }
    return *this;
}

AudioFileWriter::~AudioFileWriter()
{
    if (m_file != nullptr) {
        sf_close(m_file);
    }
}

bool AudioFileWriter::quantizes() const noexcept
{
    return m_bits != 0;
}

float AudioFileWriter::nextNoise() noexcept
{
    // xorshift64*: fast, and plenty for noise nobody is meant to hear.
    m_noise ^= m_noise >> 12;
    m_noise ^= m_noise << 25;
    m_noise ^= m_noise >> 27;
    const std::uint64_t value = m_noise * 0x2545F4914F6CDD1DULL;
    return static_cast<float>(value >> 40) * (1.0f / 16777216.0f); // [0, 1)
}

std::expected<void, FileError> AudioFileWriter::write(std::span<const float> samples)
{
    if (m_m4a) {
        std::expected<void, FileError> written = m_m4a->write(samples);
        if (written) {
            m_frames += samples.size() / m_spec.channels;
        }
        return written;
    }
    if (m_file == nullptr) {
        return std::unexpected(FileError::WriteFailed);
    }
    const std::size_t channels = m_spec.channels;
    const auto frames = static_cast<sf_count_t>(samples.size() / channels);
    if (frames == 0) {
        return {};
    }

    errno = 0;
    sf_count_t done = 0;
    if (quantizes()) {
        // **Rounded here, with dither, rather than by libsndfile**, which
        // truncates. libsndfile takes an int as a full 32-bit sample and
        // keeps its top bits, so the rounded value is put there.
        const double scale = std::ldexp(1.0, m_bits - 1);
        const double top = scale - 1.0;
        const int shift = 32 - m_bits;
        m_scratch.resize(samples.size());
        for (std::size_t index = 0; index < samples.size(); ++index) {
            // Two uniform draws make a triangle one step wide either side.
            const double dither =
                m_spec.dither ? static_cast<double>(nextNoise()) - static_cast<double>(nextNoise()) : 0.0;
            const double value =
                std::clamp(std::round(static_cast<double>(samples[index]) * scale + dither), -scale, top);
            m_scratch[index] = static_cast<int>(static_cast<std::int64_t>(value) * (std::int64_t{1} << shift));
        }
        done = sf_writef_int(m_file, m_scratch.data(), frames);
    } else {
        done = sf_writef_float(m_file, samples.data(), frames);
    }
    if (done != frames) {
        const int error = errno;
        m_frames += static_cast<std::uint64_t>(std::max<sf_count_t>(done, 0));
        return std::unexpected(writeErrorFromErrno(error));
    }
    m_frames += static_cast<std::uint64_t>(frames);
    return {};
}

void AudioFileWriter::refreshHeader()
{
    if (m_m4a) {
        // A fragment is written every second by itself; this hands what is
        // written to the disk.
        m_m4a->flush();
        return;
    }
    if (m_file == nullptr) {
        return;
    }
    if (hasLengthInHeader(m_spec.format)) {
        sf_command(m_file, SFC_UPDATE_HEADER_NOW, nullptr, 0);
    }
    // And onto the disk, so a power cut costs a few seconds rather than
    // whatever the page cache was holding.
    sf_write_sync(m_file);
}

std::expected<void, FileError> AudioFileWriter::close()
{
    if (m_m4a) {
        std::expected<void, FileError> closed = m_m4a->close();
        m_m4a.reset();
        return closed;
    }
    if (m_file == nullptr) {
        return {};
    }
    errno = 0;
    const int result = sf_close(std::exchange(m_file, nullptr));
    if (result != 0) {
        return std::unexpected(writeErrorFromErrno(errno));
    }
    return {};
}

} // namespace rb::record
