// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/paramdescriptor.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rb::dsp
{

/// What one ear hears, as the listening blocks start from it: the
/// quietest level heard at each of eleven frequencies, and optionally the
/// level at which sound becomes too loud.
///
/// **One ear.** Every listening block works on one ear (D1), and the
/// same block type is added once for each; each keeps its own ear's
/// audiogram among its parameters.
///
/// **Stored as block parameters, all of them numbers or choices**, so
/// editing one never makes the session be planned again: see
/// audiogramParams().

/// The frequencies an audiogram has a point at, in Hz, low to high.
inline constexpr std::array<int, 11>
    kAudiogramFrequenciesHz{125, 250, 500, 750, 1000, 1500, 2000, 3000, 4000, 6000, 8000};
inline constexpr std::size_t kAudiogramPoints = kAudiogramFrequenciesHz.size();

/// What an audiogram's levels are measured in.
enum class AudiogramUnit : std::uint8_t {
    /// Decibels hearing level: typed in from an audiogram someone already
    /// has, where 0 is the quietest level an average young listener hears at
    /// that frequency, whatever the frequency.
    DbHl,
    /// Decibels against full scale, measured through the very headphones
    /// the sound will be played on (a later listening check). No level of
    /// this unit means anything through other headphones.
    DbFs,
};

/// What is known at one frequency.
enum class AudiogramMark : std::uint8_t {
    None, ///< Nothing was measured here; the level is worked out from the neighbours.
    Heard, ///< The level is the quietest heard.
    /// Nothing was heard, even at the loudest level tried, which is what
    /// the level holds. More gain here is unlikely to help; moving the sound
    /// to where it is heard may (Frequency Lowering).
    NoResponse,
};

struct AudiogramPoint {
    AudiogramMark mark = AudiogramMark::None;
    /// The quietest level heard or, for NoResponse, the loudest level tried.
    float levelDb = 0.0f;
    /// The level at which sound becomes too loud, when it was measured.
    std::optional<float> tooLoudDb;

    [[nodiscard]] bool operator==(const AudiogramPoint &other) const = default;
};

struct Audiogram {
    AudiogramUnit unit = AudiogramUnit::DbHl;
    /// One point for each of kAudiogramFrequenciesHz, in the same order.
    std::array<AudiogramPoint, kAudiogramPoints> points{};

    /// Whether any point was measured, heard or not.
    [[nodiscard]] bool isEmpty() const noexcept;

    [[nodiscard]] bool operator==(const Audiogram &other) const = default;
};

/// The stable names of a unit and a mark, as the parameters hold them.
[[nodiscard]] std::string_view audiogramUnitName(AudiogramUnit unit) noexcept;
[[nodiscard]] std::optional<AudiogramUnit> audiogramUnitFromName(std::string_view name) noexcept;
[[nodiscard]] std::string_view audiogramMarkName(AudiogramMark mark) noexcept;
[[nodiscard]] std::optional<AudiogramMark> audiogramMarkFromName(std::string_view name) noexcept;

/// What each parameter of one point is.
enum class AudiogramField : std::uint8_t {
    Mark, ///< Enum: "none", "heard", "noResponse".
    LevelDb, ///< Float, dB.
    TooLoudGiven, ///< Bool: whether TooLoudDb was measured.
    TooLoudDb, ///< Float, dB.
};

/// The id of the parameter holding \a field of the point at
/// kAudiogramFrequenciesHz[\a point]: for example "audiogram1500",
/// "audiogram1500Db", "audiogram1500TooLoud", "audiogram1500TooLoudDb".
/// Stable for ever, since a session file holds them.
[[nodiscard]] std::string audiogramParamId(std::size_t point, AudiogramField field);

/// The id of the parameter holding the unit: "audiogramUnit".
inline constexpr std::string_view kAudiogramUnitParamId = "audiogramUnit";

/// The lowest and highest level a point may hold, in either unit.
inline constexpr float kAudiogramMinDb = -140.0f;
inline constexpr float kAudiogramMaxDb = 140.0f;

/// The descriptors a listening block adds to its own for one ear's
/// audiogram: the unit, then four for each point. **Hidden**, since the
/// block's panel draws its own audiogram editor, and **automatable**, since
/// none of them changes the block's ports.
[[nodiscard]] std::vector<ParamDescriptor> audiogramParams();

/// An audiogram's level at \a hz, worked out from the points that were
/// measured, heard or not: in between two, on a straight line in log
/// frequency; below the lowest and above the highest, that point's level
/// carries on. Nothing when no point was measured.
[[nodiscard]] std::optional<float> audiogramLevelAt(const Audiogram &audiogram, double hz);

/// The measured point nearest \a hz in log frequency, as an index into
/// kAudiogramFrequenciesHz, or nothing when no point was measured.
[[nodiscard]] std::optional<std::size_t> nearestMeasuredPoint(const Audiogram &audiogram, double hz);

/// The level at which sound becomes too loud at \a hz, from the points
/// where it was measured, in the same way as audiogramLevelAt().
[[nodiscard]] std::optional<float> audiogramTooLoudAt(const Audiogram &audiogram, double hz);

} // namespace rb::dsp
