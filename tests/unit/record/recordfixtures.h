// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

// What the recording tests share: a folder of their own to write in, and a
// way to read back what was written.

#include <sndfile.h>
#include <unistd.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <numbers>
#include <string>
#include <system_error>
#include <vector>

namespace rb::recordtest
{

/// A folder that is gone when the test is.
///
/// **In the build tree, not in /tmp**: the test runner points
/// XDG_CACHE_HOME there, and /tmp is memory on this platform, where the
/// long-recording test's file would sit in RAM.
class ScratchFolder
{
public:
    ScratchFolder()
    {
        static std::atomic<int> s_count{0};
        const char *cache = std::getenv("XDG_CACHE_HOME");
        const std::filesystem::path base =
            cache != nullptr && *cache != '\0' ? std::filesystem::path{cache} : std::filesystem::temp_directory_path();
        m_path = base / std::format("rb-record-test-{}-{}", ::getpid(), s_count.fetch_add(1));
        std::filesystem::remove_all(m_path);
        std::filesystem::create_directories(m_path);
    }

    ~ScratchFolder()
    {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }

    ScratchFolder(const ScratchFolder &) = delete;
    ScratchFolder &operator=(const ScratchFolder &) = delete;

    [[nodiscard]] const std::filesystem::path &path() const
    {
        return m_path;
    }

    [[nodiscard]] std::filesystem::path operator/(const std::string &name) const
    {
        return m_path / name;
    }

private:
    std::filesystem::path m_path;
};

/// A file as libsndfile reads it.
struct ReadBack {
    bool opened = false;
    int sampleRate = 0;
    int channels = 0;
    int format = 0;
    /// What the header says, which is not what is there when it is wrong.
    std::int64_t headerFrames = 0;
    /// Interleaved, everything that decodes.
    std::vector<float> samples;

    [[nodiscard]] std::size_t frames() const
    {
        return channels == 0 ? 0 : samples.size() / static_cast<std::size_t>(channels);
    }
};

/// Reads \a path. \a maxFrames stops a file whose header says "unknown"
/// from being read for ever.
[[nodiscard]] inline ReadBack readBack(const std::filesystem::path &path, std::size_t maxFrames = 200'000'000)
{
    ReadBack result;
    SF_INFO info{};
    SNDFILE *file = sf_open(path.c_str(), SFM_READ, &info);
    if (file == nullptr) {
        return result;
    }
    result.opened = true;
    result.sampleRate = info.samplerate;
    result.channels = info.channels;
    result.format = info.format;
    result.headerFrames = info.frames;
    std::vector<float> buffer(8192 * static_cast<std::size_t>(info.channels));
    while (result.frames() < maxFrames) {
        const sf_count_t got = sf_readf_float(file, buffer.data(), 8192);
        if (got <= 0) {
            break;
        }
        result.samples.insert(result.samples.end(), buffer.begin(), buffer.begin() + got * info.channels);
    }
    sf_close(file);
    return result;
}

/// A stereo sine, interleaved: \a hz on the left, half an octave up on the
/// right, so the two sides can be told apart.
[[nodiscard]] inline std::vector<float>
stereoSine(std::size_t frames, double hz, float amplitude, double rate = 48000.0)
{
    std::vector<float> samples(frames * 2);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const double t = static_cast<double>(frame) / rate;
        samples[frame * 2] = amplitude * static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * t));
        samples[frame * 2 + 1] = amplitude * static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * 1.5 * t));
    }
    return samples;
}

/// The root mean square of one channel of interleaved \a samples between
/// two frames, in dBFS.
[[nodiscard]] inline double
rmsDb(const std::vector<float> &samples, int channels, int channel, std::size_t from, std::size_t to)
{
    double sum = 0.0;
    for (std::size_t frame = from; frame < to; ++frame) {
        const double value = samples[frame * static_cast<std::size_t>(channels) + static_cast<std::size_t>(channel)];
        sum += value * value;
    }
    return 10.0 * std::log10(std::max(sum / static_cast<double>(to - from), 1e-30));
}

} // namespace rb::recordtest
