// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "record/impulsefile.h"

#include "record/resampler.h"

#include <sndfile.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <span>

namespace rb::record
{

namespace
{

constexpr std::size_t kChunkFrames = 4096;

} // namespace

std::expected<ImpulseFile, ImpulseFileError> readImpulseFile(const std::filesystem::path &given)
{
    std::filesystem::path path = given;
    const std::string text = given.string();
    const char *home = std::getenv("HOME");
    if (home != nullptr && (text == "~" || text.starts_with("~/"))) {
        path = std::filesystem::path{home} / text.substr(std::min<std::size_t>(text.size(), 2));
    }
    SF_INFO info{};
    SNDFILE *file = sf_open(path.c_str(), SFM_READ, &info);
    if (file == nullptr) {
        return std::unexpected(ImpulseFileError::Unreadable);
    }
    if (info.channels <= 0 || info.samplerate <= 0) {
        sf_close(file);
        return std::unexpected(ImpulseFileError::Unreadable);
    }
    const auto channels = static_cast<std::size_t>(info.channels);
    const auto limit = static_cast<std::size_t>(kMaxImpulseFileSeconds * info.samplerate);
    // **The header is asked first**, so a long file is refused before any
    // of it is read; the reading below stops at the limit whatever the
    // header said, since a header can be wrong.
    if (info.frames > 0 && static_cast<std::size_t>(info.frames) > limit) {
        sf_close(file);
        return std::unexpected(ImpulseFileError::TooLong);
    }

    ImpulseFile impulse;
    impulse.sampleRate = static_cast<std::uint32_t>(info.samplerate);
    impulse.channels.resize(channels);
    std::vector<float> buffer(kChunkFrames * channels);
    for (;;) {
        const sf_count_t got = sf_readf_float(file, buffer.data(), static_cast<sf_count_t>(kChunkFrames));
        if (got <= 0) {
            break;
        }
        if (impulse.frames() + static_cast<std::size_t>(got) > limit) {
            sf_close(file);
            return std::unexpected(ImpulseFileError::TooLong);
        }
        for (std::size_t channel = 0; channel < channels; ++channel) {
            std::vector<float> &into = impulse.channels[channel];
            for (std::size_t frame = 0; frame < static_cast<std::size_t>(got); ++frame) {
                into.push_back(buffer[frame * channels + channel]);
            }
        }
    }
    sf_close(file);
    if (impulse.frames() == 0) {
        return std::unexpected(ImpulseFileError::Empty);
    }
    return impulse;
}

std::vector<std::vector<float>> resampleImpulse(const ImpulseFile &impulse, std::uint32_t toRate)
{
    if (impulse.sampleRate == toRate || impulse.channels.empty()) {
        return impulse.channels;
    }
    const std::size_t channels = impulse.channels.size();
    const std::size_t frames = impulse.frames();
    const std::unique_ptr<Resampler> resampler =
        Resampler::create(impulse.sampleRate, toRate, static_cast<std::uint32_t>(channels));
    if (!resampler) {
        return {};
    }
    std::vector<float> interleaved(frames * channels);
    for (std::size_t channel = 0; channel < channels; ++channel) {
        for (std::size_t frame = 0; frame < frames; ++frame) {
            interleaved[frame * channels + channel] = impulse.channels[channel][frame];
        }
    }
    std::vector<float> converted;
    const std::span<const float> first = resampler->process(interleaved);
    converted.assign(first.begin(), first.end());
    const std::span<const float> rest = resampler->flush();
    converted.insert(converted.end(), rest.begin(), rest.end());

    // **As long in time as it was**: what the resampler hands back at the
    // end is its filter running out, not more of the response.
    const auto wanted = static_cast<std::size_t>(
        std::llround(static_cast<double>(frames) * toRate / static_cast<double>(impulse.sampleRate)));
    const std::size_t made = std::min(wanted, converted.size() / channels);
    std::vector<std::vector<float>> result(channels, std::vector<float>(made));
    for (std::size_t channel = 0; channel < channels; ++channel) {
        for (std::size_t frame = 0; frame < made; ++frame) {
            result[channel][frame] = converted[frame * channels + channel];
        }
    }
    return result;
}

} // namespace rb::record
