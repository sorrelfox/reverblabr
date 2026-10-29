// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace rb::record
{

/// Converts interleaved audio from one sample rate to another, through
/// soxr at its high-quality setting.
///
/// **On the writer's thread only.** Conversion allocates and takes as long
/// as it takes, which is why a file at another rate than the session's is
/// the writer's business and never the audio thread's.
///
/// A converter holds back a few milliseconds of what it is given, to look
/// ahead; flush() gives that back at the end, so what comes out is as long
/// as what went in, at the new rate.
class Resampler
{
public:
    /// Null if soxr would not make one for these rates.
    [[nodiscard]] static std::unique_ptr<Resampler>
    create(std::uint32_t fromRate, std::uint32_t toRate, std::uint32_t channels);

    ~Resampler();
    Resampler(const Resampler &) = delete;
    Resampler &operator=(const Resampler &) = delete;

    /// Converts \a input, a whole number of frames, and gives back what came
    /// out of it so far. The span is valid until the next call.
    [[nodiscard]] std::span<const float> process(std::span<const float> input);

    /// Everything still held back, for the end of a file.
    [[nodiscard]] std::span<const float> flush();

    [[nodiscard]] std::uint32_t fromRate() const noexcept
    {
        return m_from;
    }

    [[nodiscard]] std::uint32_t toRate() const noexcept
    {
        return m_to;
    }

private:
    Resampler() = default;

    void *m_soxr = nullptr;
    std::uint32_t m_from = 0;
    std::uint32_t m_to = 0;
    std::uint32_t m_channels = 0;
    std::vector<float> m_output;
};

} // namespace rb::record
