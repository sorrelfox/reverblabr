// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "dsp/scopetrace.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace rb::dsp
{

namespace
{

/// The silence below which a crossing is not looked for: -100 dBFS.
constexpr float kQuietest = 1.0e-5f;
/// How far below nought, as a share of the loudest point, a signal has to go
/// before it can cross again.
constexpr float kBelowShare = 0.1f;
/// How near a whole point a position has to be to count as on it: the sum
/// that gives a column's edge lands a hair either side.
constexpr double kWholeEnough = 1.0e-6;

[[nodiscard]] float middleOf(const rt::ScopePoint &point, std::size_t trace) noexcept
{
    return 0.5f * (point.low[trace] + point.high[trace]);
}

/// The line between the two points either side of \a position, which is in
/// range.
[[nodiscard]] ScopeColumn readAt(std::span<const rt::ScopePoint> points, std::size_t trace, double position) noexcept
{
    const auto first = static_cast<std::size_t>(position);
    const std::size_t next = std::min(first + 1, points.size() - 1);
    const auto share = static_cast<float>(position - static_cast<double>(first));
    const rt::ScopePoint &from = points[first];
    const rt::ScopePoint &to = points[next];
    return ScopeColumn{from.low[trace] + share * (to.low[trace] - from.low[trace]),
                       from.high[trace] + share * (to.high[trace] - from.high[trace]),
                       true};
}

} // namespace

std::size_t scopeLevelFor(double frames) noexcept
{
    for (std::size_t level = 0; level + 1 < rt::kScopeLevels; ++level) {
        const double reach = static_cast<double>(rt::kScopePoints - 1) * rt::scopeFramesPerPoint(level);
        if (frames <= reach) {
            return level;
        }
    }
    return rt::kScopeLevels - 1;
}

ScopeCrossing latestRisingCrossing(std::span<const rt::ScopePoint> points, std::size_t trace, std::size_t last) noexcept
{
    ScopeCrossing crossing;
    if (trace >= rt::kScopeTraces || points.size() < 2) {
        return crossing;
    }
    const std::size_t end = std::min(last + 1, points.size());

    float loudest = 0.0f;
    for (std::size_t index = 0; index < end; ++index) {
        // Written so that a NaN is left out.
        const float magnitude = std::max(std::fabs(points[index].low[trace]), std::fabs(points[index].high[trace]));
        if (magnitude > loudest && magnitude <= std::numeric_limits<float>::max()) {
            loudest = magnitude;
        }
    }
    if (loudest < kQuietest) {
        return crossing;
    }

    const float below = -kBelowShare * loudest;
    bool armed = false;
    float before = 0.0f;
    for (std::size_t index = 0; index < end; ++index) {
        const float now = middleOf(points[index], trace);
        if (armed && before < 0.0f && now >= 0.0f) {
            crossing.found = true;
            crossing.point = static_cast<double>(index) - 1.0 + static_cast<double>(-before / (now - before));
            armed = false;
        }
        if (now <= below) {
            armed = true;
        }
        before = now;
    }
    return crossing;
}

void scopeColumns(std::span<const rt::ScopePoint> points,
                  std::size_t trace,
                  double firstPoint,
                  double pointsPerColumn,
                  std::span<ScopeColumn> columns) noexcept
{
    const auto count = static_cast<std::ptrdiff_t>(points.size());
    if (trace >= rt::kScopeTraces || count == 0 || pointsPerColumn <= 0.0) {
        std::ranges::fill(columns, ScopeColumn{});
        return;
    }
    for (std::size_t column = 0; column < columns.size(); ++column) {
        const double from = firstPoint + static_cast<double>(column) * pointsPerColumn;
        const double to = from + pointsPerColumn;
        ScopeColumn into;
        if (from > -kWholeEnough && from < static_cast<double>(count - 1)) {
            // The lines between the points, read at the column's two edges.
            // So the next column starts where this one ended and the two
            // join up, and a stretch moved by a fraction of a point changes
            // by a fraction of what it would have, which is what keeps a
            // triggered tone from shaking.
            const ScopeColumn before = readAt(points, trace, std::max(from, 0.0));
            const ScopeColumn after = readAt(points, trace, std::min(to, static_cast<double>(count - 1)));
            into.low = std::min(before.low, after.low);
            into.high = std::max(before.high, after.high);
            into.heard = true;
            // And every point inside it, so that none is lost between them.
            const auto inside = static_cast<std::ptrdiff_t>(std::ceil(from));
            const auto end = std::min(static_cast<std::ptrdiff_t>(std::ceil(to)), count);
            for (std::ptrdiff_t index = std::max<std::ptrdiff_t>(inside, 0); index < end; ++index) {
                const rt::ScopePoint &point = points[static_cast<std::size_t>(index)];
                into.low = std::min(into.low, point.low[trace]);
                into.high = std::max(into.high, point.high[trace]);
            }
        }
        columns[column] = into;
    }
}

} // namespace rb::dsp
