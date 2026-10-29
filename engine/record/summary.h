// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace rb::record
{

/// What a finished recording is, read back from the file itself:
/// how long, at what rate, how many channels, and the outline of its sound.
struct FileSummary {
    std::uint64_t frames = 0;
    std::uint32_t sampleRate = 0;
    std::uint32_t channels = 0;
    /// The loudest sample of any channel in each of an equal share of the
    /// file, from its start to its end, as an amplitude. What a waveform is
    /// drawn from: the Files tab's small one and the Recording Inspector's.
    std::vector<float> peaks;
};

/// Reads \a path through and summarises it into \a buckets peaks, or
/// nothing if libsndfile cannot read it. **Reads the whole file**, so it
/// belongs on a worker thread; a three-hour recording takes seconds.
///
/// **Decoded, not trusted.** The length is what decodes, which is what a
/// player will play, rather than what a header says — a file cut short by
/// a crash says more than it has until it is repaired.
[[nodiscard]] std::optional<FileSummary> summarise(const std::filesystem::path &path, std::uint32_t buckets);

} // namespace rb::record
