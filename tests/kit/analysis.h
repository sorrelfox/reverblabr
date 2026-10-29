// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "signals.h"

#include <cstdint>
#include <optional>
#include <span>

namespace rb::test
{

/// How far apart two signals have to be before a test calls them different.
/// Ninety decibels down is far below anything anyone can hear, and far above
/// the rounding of single-precision arithmetic.
inline constexpr float kNullToleranceDb = -90.0f;

/// The largest sample of \a signal, as a linear amplitude.
[[nodiscard]] float peak(std::span<const float> signal);

/// The root mean square of \a signal, as a linear amplitude.
[[nodiscard]] float rms(std::span<const float> signal);

/// The same two in decibels, with the silence floor of dsp::gainToDb().
[[nodiscard]] float peakDb(std::span<const float> signal);
[[nodiscard]] float rmsDb(std::span<const float> signal);

/// The largest step from one sample to the next. A click is a step where the
/// signal says there should not be one, so this is the click detector.
[[nodiscard]] float maxSampleStep(std::span<const float> signal);

/// The level of what is left when \a b is taken away from \a a, in decibels.
/// Signals of different lengths are compared over the shorter one.
///
/// **The result bottoms out at dsp::kSilenceDb (-120 dB)**, which is what
/// identical signals give, so a tolerance below that can never be met. Ask
/// for exact equality instead when that is what is meant.
[[nodiscard]] float nullTestDb(std::span<const float> a, std::span<const float> b);

/// Whether \a a and \a b are the same signal to within \a toleranceDb.
[[nodiscard]] bool nulls(std::span<const float> a, std::span<const float> b, float toleranceDb = kNullToleranceDb);

/// How far \a delayed lags \a reference, in samples, found by correlating
/// the two. Nothing if neither signal has anything in it.
///
/// \a maxLag bounds the search; it is the longest delay worth considering.
[[nodiscard]] std::optional<int>
latencySamples(std::span<const float> reference, std::span<const float> delayed, int maxLag);

/// The total harmonic distortion of a signal that should be a sine at
/// \a frequencyHz: the level of the harmonics over the level of the
/// fundamental, as a fraction. A clean sine gives something near zero.
[[nodiscard]] float totalHarmonicDistortion(std::span<const float> signal,
                                            double frequencyHz,
                                            double sampleRate = kTestSampleRate,
                                            int harmonics = 5);

/// The amplitude of \a signal at \a frequencyHz, by correlating it with a
/// sine and a cosine of that frequency. Used by the distortion estimate, and
/// useful on its own for a frequency response.
[[nodiscard]] float amplitudeAt(std::span<const float> signal, double frequencyHz, double sampleRate = kTestSampleRate);

} // namespace rb::test
