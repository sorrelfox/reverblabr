// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "record/resampler.h"

#include <soxr.h>

#include <cmath>

namespace rb::record
{

namespace
{

[[nodiscard]] soxr_t handle(void *soxr)
{
    return static_cast<soxr_t>(soxr);
}

} // namespace

std::unique_ptr<Resampler> Resampler::create(std::uint32_t fromRate, std::uint32_t toRate, std::uint32_t channels)
{
    if (fromRate == 0 || toRate == 0 || channels == 0) {
        return nullptr;
    }
    const soxr_io_spec_t io = soxr_io_spec(SOXR_FLOAT32_I, SOXR_FLOAT32_I);
    // High quality: a passband to 91 % of Nyquist, 20-bit stopband. What a
    // recording is kept for, and a writer thread has the time.
    const soxr_quality_spec_t quality = soxr_quality_spec(SOXR_HQ, 0);
    soxr_error_t error = nullptr;
    soxr_t soxr = soxr_create(fromRate, toRate, channels, &error, &io, &quality, nullptr);
    if (soxr == nullptr || error != nullptr) {
        if (soxr != nullptr) {
            soxr_delete(soxr);
        }
        return nullptr;
    }
    std::unique_ptr<Resampler> resampler{new Resampler};
    resampler->m_soxr = soxr;
    resampler->m_from = fromRate;
    resampler->m_to = toRate;
    resampler->m_channels = channels;
    return resampler;
}

Resampler::~Resampler()
{
    if (m_soxr != nullptr) {
        soxr_delete(handle(m_soxr));
    }
}

std::span<const float> Resampler::process(std::span<const float> input)
{
    const std::size_t frames = input.size() / m_channels;
    // What comes out of this much, and room for what was held back before.
    const auto room = static_cast<std::size_t>(std::ceil(static_cast<double>(frames) * m_to / m_from)) + 1024;
    m_output.resize(room * m_channels);
    std::size_t used = 0;
    std::size_t made = 0;
    soxr_process(handle(m_soxr), input.data(), frames, &used, m_output.data(), room, &made);
    return std::span<const float>{m_output}.first(made * m_channels);
}

std::span<const float> Resampler::flush()
{
    constexpr std::size_t kRoom = 4096;
    std::vector<float> tail;
    m_output.resize(kRoom * m_channels);
    for (;;) {
        std::size_t made = 0;
        // No input says "that was all".
        soxr_process(handle(m_soxr), nullptr, 0, nullptr, m_output.data(), kRoom, &made);
        if (made == 0) {
            break;
        }
        tail.insert(tail.end(), m_output.begin(), m_output.begin() + static_cast<std::ptrdiff_t>(made * m_channels));
    }
    m_output = std::move(tail);
    return m_output;
}

} // namespace rb::record
