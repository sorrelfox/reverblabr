// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "rt/realtime.h"

namespace rb::dsp
{

/// Level treated as silence, in dB. Level readouts never go below it.
inline constexpr float kSilenceDb = -120.0f;

/// The linear gain that corresponds to kSilenceDb.
inline constexpr float kSilenceGain = 1.0e-6f;

/// Converts a level in decibels to a linear gain factor.
[[nodiscard]] float dbToGain(float db) noexcept RB_NONBLOCKING;

/// Converts a linear gain factor to decibels. Gains at or below kSilenceGain,
/// including zero, negative values and NaN, return kSilenceDb.
[[nodiscard]] float gainToDb(float gain) noexcept RB_NONBLOCKING;

} // namespace rb::dsp
