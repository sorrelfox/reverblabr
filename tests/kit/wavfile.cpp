// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "wavfile.h"

#if RB_HAVE_SNDFILE
#include <sndfile.h>
#endif

#include <cstdint>
#include <vector>

namespace rb::test
{

std::string_view audioFileErrorName(AudioFileError error) noexcept
{
    switch (error) {
    case AudioFileError::NotSupported:
        return "notSupported";
    case AudioFileError::NotReadable:
        return "notReadable";
    case AudioFileError::NotWritable:
        return "notWritable";
    }
    return "unknown";
}

bool audioFilesSupported() noexcept
{
    return RB_HAVE_SNDFILE != 0;
}

#if RB_HAVE_SNDFILE

std::expected<AudioFile, AudioFileError> readAudioFile(const std::string &path)
{
    SF_INFO info{};
    SNDFILE *const file = sf_open(path.c_str(), SFM_READ, &info);
    if (file == nullptr) {
        return std::unexpected(AudioFileError::NotReadable);
    }

    const auto channels = static_cast<std::uint32_t>(info.channels);
    const auto frames = static_cast<std::size_t>(info.frames);
    // libsndfile hands back one array with the channels woven together.
    std::vector<float> interleaved(frames * channels);
    const sf_count_t read = sf_readf_float(file, interleaved.data(), static_cast<sf_count_t>(frames));
    sf_close(file);
    if (read < 0) {
        return std::unexpected(AudioFileError::NotReadable);
    }

    AudioFile result;
    result.sampleRate = static_cast<double>(info.samplerate);
    result.channels.assign(channels, Signal(static_cast<std::size_t>(read), 0.0f));
    for (std::size_t frame = 0; frame < static_cast<std::size_t>(read); ++frame) {
        for (std::uint32_t channel = 0; channel < channels; ++channel) {
            result.channels[channel][frame] = interleaved[frame * channels + channel];
        }
    }
    return result;
}

std::expected<void, AudioFileError>
writeWavFile(const std::string &path, const MultiSignal &channels, double sampleRate)
{
    if (channels.empty()) {
        return std::unexpected(AudioFileError::NotWritable);
    }
    const std::size_t frames = frameCount(channels);

    SF_INFO info{};
    info.samplerate = static_cast<int>(sampleRate);
    info.channels = static_cast<int>(channels.size());
    // Floats, so that a golden file is exactly what the engine produced and
    // a null test against it means something.
    info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;

    SNDFILE *const file = sf_open(path.c_str(), SFM_WRITE, &info);
    if (file == nullptr) {
        return std::unexpected(AudioFileError::NotWritable);
    }

    std::vector<float> interleaved(frames * channels.size(), 0.0f);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        for (std::size_t channel = 0; channel < channels.size(); ++channel) {
            interleaved[frame * channels.size() + channel] =
                frame < channels[channel].size() ? channels[channel][frame] : 0.0f;
        }
    }
    const sf_count_t written = sf_writef_float(file, interleaved.data(), static_cast<sf_count_t>(frames));
    sf_close(file);
    return written == static_cast<sf_count_t>(frames) ? std::expected<void, AudioFileError>{}
                                                      : std::unexpected(AudioFileError::NotWritable);
}

#else

std::expected<AudioFile, AudioFileError> readAudioFile(const std::string &path)
{
    static_cast<void>(path);
    return std::unexpected(AudioFileError::NotSupported);
}

std::expected<void, AudioFileError>
writeWavFile(const std::string &path, const MultiSignal &channels, double sampleRate)
{
    static_cast<void>(path);
    static_cast<void>(channels);
    static_cast<void>(sampleRate);
    return std::unexpected(AudioFileError::NotSupported);
}

#endif

} // namespace rb::test
