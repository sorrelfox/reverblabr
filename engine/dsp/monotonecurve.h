// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "rt/realtime.h"

#include <array>
#include <cstddef>
#include <span>

namespace rb::dsp
{

/// A smooth curve through a few points that never overshoots them:
/// piecewise cubic Hermite with Fritsch and Carlson's tangents ("PCHIP").
///
/// **Between two points the curve stays between their values**: it rises
/// where they rise, falls where they fall, and is level at a point that is
/// a peak or a dip, so a dip set with points is never made deeper, nor a
/// lift higher, than the points say. A cubic spline through the same points
/// swings past them. Beyond the first and last points the curve is level at
/// their values.
///
/// Real-time and allocation-free: at most kMaxPoints, held in place.
class MonotoneCurve
{
public:
    static constexpr std::size_t kMaxPoints = 24;

    /// Through the points (\a x[i], \a y[i]), \a x strictly rising; at most
    /// kMaxPoints are taken. No points is a curve that is 0 everywhere; one
    /// is level at its value.
    void fit(std::span<const double> x, std::span<const float> y) noexcept RB_NONBLOCKING;

    /// The curve at \a x.
    [[nodiscard]] float at(double x) const noexcept RB_NONBLOCKING;

    [[nodiscard]] std::size_t size() const noexcept RB_NONBLOCKING
    {
        return m_count;
    }

private:
    std::size_t m_count = 0;
    std::array<double, kMaxPoints> m_x{};
    std::array<double, kMaxPoints> m_y{};
    /// The curve's slope at each point.
    std::array<double, kMaxPoints> m_slope{};
};

} // namespace rb::dsp
