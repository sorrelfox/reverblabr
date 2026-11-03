// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "rt/scope.h"

#include <cstddef>
#include <span>

namespace rb::dsp
{

/// **What a surface does with the points it copied out of a scope's rings**:
/// which ring to read, where a periodic sound starts, and what falls in each
/// column of the screen. Plain functions on plain points, so that they can be
/// checked without a window. Not for the audio thread.

/// The finest ring of a slot that reaches back over \a frames samples.
/// Past what the coarsest reaches, the coarsest.
[[nodiscard]] std::size_t scopeLevelFor(double frames) noexcept;

/// A rising zero crossing, as a position among the points: 12.25 is a quarter
/// of the way from point 12 to point 13.
struct ScopeCrossing {
    bool found = false;
    double point = 0.0;
};

/// **The latest rising zero crossing at or before point \a last.**
///
/// A trace started from one stands still on a periodic sound, whatever moment
/// it was taken at.
///
/// **The signal has to have been properly below nought first**: a tenth of
/// the loudest point in the search. Without that, a ripple on a slow wave near
/// nought crosses over and over, and the trace jumps between them. The
/// position is worked out between the two points either side, or a tone would
/// shake by up to a whole sample.
///
/// A coarser ring's point is the middle of its extremes. Nothing found means
/// nothing in \a points goes through nought, such as silence or a constant.
[[nodiscard]] ScopeCrossing
latestRisingCrossing(std::span<const rt::ScopePoint> points, std::size_t trace, std::size_t last) noexcept;

/// What is drawn in one column: the lowest and highest of what falls in it.
/// \a heard is false where the sound has not reached back that far.
struct ScopeColumn {
    float low = 0.0f;
    float high = 0.0f;
    bool heard = false;
};

/// **Fills \a columns from \a points**, the first column starting at
/// \a firstPoint, each \a pointsPerColumn wide.
///
/// A column is the lowest and highest of the sound across its width, taking
/// the sound as straight lines between the points. So nothing narrow is lost
/// however many points fall in one column, a view with fewer points than
/// columns draws the sound and not a staircase, and neighbours join up.
void scopeColumns(std::span<const rt::ScopePoint> points,
                  std::size_t trace,
                  double firstPoint,
                  double pointsPerColumn,
                  std::span<ScopeColumn> columns) noexcept;

} // namespace rb::dsp
