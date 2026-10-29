// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <cstddef>
#include <vector>

namespace rb::dsp
{

/// The auditory frequency scale the listening blocks' bands are laid out on
/// (ADR-0019): Glasberg and Moore's equivalent rectangular bandwidth, and
/// the ERB-number ("Cam") scale made of it. Equal steps on it are equal
/// steps to the ear, which a logarithmic scale is only above about 500 Hz.

/// The lowest and highest band centres the blocks cover.
inline constexpr double kErbLowestHz = 100.0;
inline constexpr double kErbHighestHz = 16000.0;

/// How wide a band the ear hears as one at \a hz, in hertz.
[[nodiscard]] double erbHz(double hz);
/// How many ERBs lie below \a hz.
[[nodiscard]] double erbNumber(double hz);
/// The frequency \a erbs ERBs up the scale.
[[nodiscard]] double hzAtErbNumber(double erbs);

/// \a count centres evenly spaced in ERB-number from kErbLowestHz to
/// kErbHighestHz: 64 for the filterbank's channels, 16 for the bands of the
/// 16-band block.
[[nodiscard]] std::vector<double> erbCentres(std::size_t count);

/// How far apart neighbouring centres of erbCentres(\a count) are, in ERBs.
[[nodiscard]] double erbSpacing(std::size_t count);

} // namespace rb::dsp
