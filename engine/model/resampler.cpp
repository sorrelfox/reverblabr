// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "model/resampler.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>

namespace rb::model
{

namespace
{

/// How many zero crossings of the sinc each side keeps, at the lower rate.
constexpr std::uint32_t kCrossings = 16;
/// The window's shape: about 90 dB down outside the pass band.
constexpr double kKaiserBeta = 9.0;
/// Where the pass band ends, as a share of the lower rate's half.
constexpr double kPassBand = 0.94;
/// The most filters in a table; rates further apart in their ratio than
/// this are not rates anybody runs.
constexpr std::uint32_t kMostPhases = 4096;

struct Ratio {
    std::uint32_t up;
    std::uint32_t down;
};

[[nodiscard]] Ratio ratioOf(std::uint32_t inRate, std::uint32_t outRate) noexcept
{
    const std::uint32_t common = std::gcd(inRate, outRate);
    return common == 0 ? Ratio{1, 1} : Ratio{outRate / common, inRate / common};
}

/// How many input samples each filter spans.
[[nodiscard]] std::uint32_t tapsFor(Ratio ratio) noexcept
{
    const std::uint32_t stretch = std::max<std::uint32_t>(1, (ratio.down + ratio.up - 1) / ratio.up);
    return 2 * kCrossings * stretch;
}

[[nodiscard]] double besselI0(double x) noexcept
{
    double sum = 1.0;
    double term = 1.0;
    for (int k = 1; k < 32; ++k) {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
    }
    return sum;
}

} // namespace

bool Resampler::supported(std::uint32_t inRate, std::uint32_t outRate) noexcept
{
    if (inRate == 0 || outRate == 0) {
        return false;
    }
    const Ratio ratio = ratioOf(inRate, outRate);
    return ratio.up <= kMostPhases && ratio.down <= kMostPhases;
}

double Resampler::delaySeconds(std::uint32_t inRate, std::uint32_t outRate) noexcept
{
    if (inRate == outRate || inRate == 0 || outRate == 0) {
        return 0.0;
    }
    const Ratio ratio = ratioOf(inRate, outRate);
    const std::uint32_t length = tapsFor(ratio) * ratio.up;
    return (static_cast<double>(length - 1) / 2.0) / static_cast<double>(ratio.up) / static_cast<double>(inRate);
}

void Resampler::prepare(std::uint32_t inRate, std::uint32_t outRate)
{
    const Ratio ratio = ratioOf(inRate, outRate);
    m_up = ratio.up;
    m_down = ratio.down;
    m_taps = inRate == outRate ? 0 : tapsFor(ratio);
    m_table.clear();
    if (m_taps > 0) {
        const std::uint32_t length = m_taps * m_up;
        const double middle = static_cast<double>(length - 1) / 2.0;
        // Cycles a sample of the rate the prototype runs at, up times the
        // input's: the lower of the two rates' half, a little inside it.
        const double cutoff =
            kPassBand * 0.5 * std::min(1.0, static_cast<double>(m_up) / m_down) / static_cast<double>(m_up);
        std::vector<double> prototype(length);
        double dc = 0.0;
        for (std::uint32_t n = 0; n < length; ++n) {
            const double x = static_cast<double>(n) - middle;
            const double sinc =
                x == 0.0 ? 2.0 * cutoff : std::sin(2.0 * std::numbers::pi * cutoff * x) / (std::numbers::pi * x);
            const double position = 2.0 * x / static_cast<double>(length - 1);
            const double window =
                besselI0(kKaiserBeta * std::sqrt(std::max(0.0, 1.0 - position * position))) / besselI0(kKaiserBeta);
            prototype[n] = sinc * window;
            dc += prototype[n];
        }
        // Row p, tap t: the prototype at p + t * up, so that a sum over the
        // newest samples is the sinc centred p/up past the newest one.
        m_table.resize(length);
        const double gain = static_cast<double>(m_up) / dc;
        for (std::uint32_t phase = 0; phase < m_up; ++phase) {
            for (std::uint32_t tap = 0; tap < m_taps; ++tap) {
                m_table[phase * m_taps + tap] = static_cast<float>(prototype[phase + tap * m_up] * gain);
            }
        }
    }
    reset();
}

void Resampler::reset()
{
    m_history.assign(m_taps, 0.0f);
    m_position = m_taps;
    m_phase = 0;
}

void Resampler::process(std::span<const float> input, std::vector<float> &output)
{
    if (m_taps == 0) {
        output.insert(output.end(), input.begin(), input.end());
        return;
    }
    m_history.insert(m_history.end(), input.begin(), input.end());
    while (m_position < m_history.size()) {
        const float *const row = m_table.data() + std::size_t{m_phase} * m_taps;
        const float *const newest = m_history.data() + m_position;
        float sum = 0.0f;
        for (std::uint32_t tap = 0; tap < m_taps; ++tap) {
            sum += row[tap] * newest[-static_cast<std::ptrdiff_t>(tap)];
        }
        output.push_back(sum);
        m_phase += m_down;
        m_position += m_phase / m_up;
        m_phase %= m_up;
    }
    // What the next outputs still reach back to, at the front again.
    const std::size_t kept = m_position - std::min<std::size_t>(m_position, m_taps);
    m_history.erase(m_history.begin(), m_history.begin() + static_cast<std::ptrdiff_t>(kept));
    m_position -= kept;
}

} // namespace rb::model
