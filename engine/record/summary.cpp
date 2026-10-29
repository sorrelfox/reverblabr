// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "record/summary.h"

#include "record/audiofilereader.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace rb::record
{

namespace
{

/// How many frames go into one first-pass peak. Fine enough that the last
/// division into buckets is exact to a few hundredths of a second, coarse
/// enough that three hours is half a million numbers.
constexpr std::size_t kChunkFrames = 1024;

} // namespace

std::optional<FileSummary> summarise(const std::filesystem::path &path, std::uint32_t buckets)
{
    // Through the reader, so an M4A has an outline too: libsndfile
    // does not read it, FFmpeg does.
    std::optional<AudioFileReader> reader = AudioFileReader::open(path);
    if (!reader) {
        return std::nullopt;
    }
    const auto channels = static_cast<std::size_t>(std::max<std::uint32_t>(reader->channels(), 1));

    // **One peak a chunk first, then the buckets.** The number of frames a
    // header gives is not always what decodes, so the division into
    // buckets waits until the end is known.
    std::vector<float> chunks;
    std::vector<float> buffer(kChunkFrames * channels);
    std::uint64_t frames = 0;
    for (;;) {
        const std::size_t got = reader->read(buffer, kChunkFrames);
        if (got == 0) {
            break;
        }
        float loudest = 0.0f;
        for (std::size_t i = 0; i < got * channels; ++i) {
            loudest = std::max(loudest, std::abs(buffer[i]));
        }
        chunks.push_back(loudest);
        frames += got;
    }

    FileSummary summary;
    summary.frames = frames;
    summary.sampleRate = reader->sampleRate();
    summary.channels = static_cast<std::uint32_t>(channels);
    if (buckets == 0 || chunks.empty()) {
        return summary;
    }
    summary.peaks.assign(buckets, 0.0f);
    for (std::size_t chunk = 0; chunk < chunks.size(); ++chunk) {
        const std::size_t bucket = std::min<std::size_t>(chunk * buckets / chunks.size(), buckets - 1);
        summary.peaks[bucket] = std::max(summary.peaks[bucket], chunks[chunk]);
    }
    // Fewer chunks than buckets, in a file shorter than the buckets are
    // many: each empty bucket takes the one before it rather than reading
    // as silence in the middle of a sound.
    if (chunks.size() < buckets) {
        for (std::size_t bucket = 1; bucket < buckets; ++bucket) {
            const std::size_t chunk = bucket * chunks.size() / buckets;
            summary.peaks[bucket] = chunks[std::min(chunk, chunks.size() - 1)];
        }
        summary.peaks[0] = chunks[0];
    }
    return summary;
}

} // namespace rb::record
