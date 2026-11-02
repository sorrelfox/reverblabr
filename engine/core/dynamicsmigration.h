// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QJsonObject>

namespace rb::core
{

/// Whether \a params are the Dynamics Processor's from before its curve was
/// made of four points: a ratio, a knee, an expander range or a limit switch.
[[nodiscard]] bool holdsOldDynamicsParams(const QJsonObject &params);

/// The nearest curve to what \a params used to describe, as the block's
/// present parameters. Everything that is not about the curve stays as it
/// was, and a parameter \a params does not mention counts as its old default.
///
/// - The curve left the diagonal half a knee below the threshold, so that is
///   the new threshold.
/// - The height is the one that lets a peak at 0 dBFS out where it came out
///   before, which is where the old and the new curve are compared by ear.
/// - A ratio of 1:1 compressed nothing, so the bend goes to the top of the
///   graph.
/// - An expander keeps its threshold and its ratio. The range is gone, and a
///   range that stopped a steep ratio after a few decibels becomes a gentler
///   line instead, or it would turn into a silence that was never asked for.
[[nodiscard]] QJsonObject dynamicsParamsFromOld(const QJsonObject &params);

} // namespace rb::core
