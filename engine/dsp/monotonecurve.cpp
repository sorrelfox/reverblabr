// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "dsp/monotonecurve.h"

#include <algorithm>

namespace rb::dsp
{

void MonotoneCurve::fit(std::span<const double> x, std::span<const float> y) noexcept RB_NONBLOCKING
{
    m_count = std::min({x.size(), y.size(), kMaxPoints});
    for (std::size_t point = 0; point < m_count; ++point) {
        m_x[point] = x[point];
        m_y[point] = static_cast<double>(y[point]);
    }
    if (m_count < 2) {
        m_slope[0] = 0.0;
        return;
    }
    // The secants, and from them the slopes (Fritsch and Carlson, 1980, as
    // Fritsch and Butland weighted them): at a point between a rise and a
    // fall, or next to a flat stretch, level; elsewhere a weighted harmonic
    // mean of the secants either side, which is never more than three times
    // either of them and so keeps each piece within its two values.
    std::array<double, kMaxPoints> secant{};
    for (std::size_t point = 0; point + 1 < m_count; ++point) {
        secant[point] = (m_y[point + 1] - m_y[point]) / (m_x[point + 1] - m_x[point]);
    }
    m_slope[0] = secant[0];
    m_slope[m_count - 1] = secant[m_count - 2];
    for (std::size_t point = 1; point + 1 < m_count; ++point) {
        const double before = secant[point - 1];
        const double after = secant[point];
        if (before * after <= 0.0) {
            m_slope[point] = 0.0;
            continue;
        }
        const double wide = m_x[point] - m_x[point - 1];
        const double next = m_x[point + 1] - m_x[point];
        const double w1 = 2.0 * next + wide;
        const double w2 = next + 2.0 * wide;
        m_slope[point] = (w1 + w2) / (w1 / before + w2 / after);
    }
}

float MonotoneCurve::at(double x) const noexcept RB_NONBLOCKING
{
    if (m_count == 0) {
        return 0.0f;
    }
    if (m_count == 1 || x <= m_x[0]) {
        return static_cast<float>(m_y[0]);
    }
    if (x >= m_x[m_count - 1]) {
        return static_cast<float>(m_y[m_count - 1]);
    }
    std::size_t piece = 0;
    while (piece + 2 < m_count && x > m_x[piece + 1]) {
        ++piece;
    }
    const double width = m_x[piece + 1] - m_x[piece];
    const double t = (x - m_x[piece]) / width;
    const double t2 = t * t;
    const double t3 = t2 * t;
    const double h00 = 2.0 * t3 - 3.0 * t2 + 1.0;
    const double h10 = t3 - 2.0 * t2 + t;
    const double h01 = -2.0 * t3 + 3.0 * t2;
    const double h11 = t3 - t2;
    return static_cast<float>(h00 * m_y[piece] + h10 * width * m_slope[piece] + h01 * m_y[piece + 1]
                              + h11 * width * m_slope[piece + 1]);
}

} // namespace rb::dsp
