// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/audiogram.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace rb::dsp
{

/// Which published formula sets the gain for sounds of average loudness.
///
/// **Public formulas only**. NAL-NL2 and DSL v5 are licensed and are
/// not reimplemented here.
enum class FittingRule : std::uint8_t {
    /// Half the loss, at every frequency: the oldest and simplest rule of
    /// thumb (Lybarger, 1944).
    HalfGain,
    /// NAL-R (Byrne & Dillon, 1986): 0.31 of the loss at each frequency,
    /// plus 0.05 of the sum of the losses at 500, 1000 and 2000 Hz, plus a
    /// constant per frequency that shapes the result to the spectrum of
    /// speech. See docs/fitting-rules.md for the source and the table.
    NalR,
};

[[nodiscard]] std::string_view fittingRuleName(FittingRule rule) noexcept;
[[nodiscard]] std::optional<FittingRule> fittingRuleFromName(std::string_view name) noexcept;

/// What a fit is worked out against. Every level is in the audiogram's own
/// unit; the defaults are for dB HL (defaultFittingOptions()).
struct FittingOptions {
    FittingRule rule = FittingRule::NalR;
    /// Where sound of average loudness sits, per band, on the audiogram's
    /// scale: roughly where ordinary speech falls on an audiogram.
    float averageLevelDb = 45.0f;
    /// How far soft sounds sit below average ones, and loud ones above
    /// (15 dB).
    float levelStepDb = 15.0f;
    /// The level the audiogram calls no loss. 0 dB HL by definition; a dBFS
    /// audiogram needs the level a listener without loss would hear.
    float noLossDb = 0.0f;
    /// How far above the quietest level heard a soft sound is put.
    float softMarginDb = 5.0f;
    /// How far below the "too loud" level the ceiling is put.
    float ceilingMarginDb = 5.0f;
    /// The "too loud" level where none was measured: a level most
    /// listeners find uncomfortable, and never less than kTooLoudAboveLevelDb
    /// above the quietest level heard.
    float usualTooLoudDb = 100.0f;
    /// The most gain any band may be given, for any sound.
    float maximumGainDb = 50.0f;
};

/// Where the "too loud" level is assumed to be at least, above the quietest
/// level heard, when it was not measured.
inline constexpr float kTooLoudAboveLevelDb = 10.0f;

/// The options to fit an audiogram of \a unit with. For dBFS, the levels are
/// placeholders until the listening check (later) says where they are.
[[nodiscard]] FittingOptions defaultFittingOptions(AudiogramUnit unit) noexcept;

/// One band's result.
///
/// **Soft ≥ average ≥ loud** in every band, and none of the three sounds is
/// put above the ceiling: that is what makes the block lift quiet sound a
/// lot and loud sound little.
struct BandFit {
    float softGainDb = 0.0f;
    float averageGainDb = 0.0f;
    float loudGainDb = 0.0f;
    /// The highest level the band may put out, on the audiogram's scale.
    float ceilingDb = 0.0f;
    /// The ear heard nothing here at the loudest level tried. The panel
    /// suggests moving the sound instead (Frequency Lowering).
    bool noResponse = false;
    /// The rule asked for more than FittingOptions::maximumGainDb, and was
    /// given that much.
    bool beyondMaximum = false;

    [[nodiscard]] bool operator==(const BandFit &other) const = default;
};

/// Works out every band's gains from \a audiogram, for bands centred at
/// \a centresHz (the block's layout; its filterbank decides it).
///
/// **A pure function**: the same audiogram, layout and options always give
/// the same numbers, which is what lets the tests keep reviewed tables of
/// them. An audiogram with nothing measured gives no gain anywhere.
[[nodiscard]] std::vector<BandFit>
fitAudiogram(const Audiogram &audiogram, std::span<const double> centresHz, const FittingOptions &options);

/// The gain \a rule asks for, for sound of average loudness, at \a hz, given
/// the loss there and at the three frequencies NAL-R averages. Never below
/// zero and never more than the loss itself.
[[nodiscard]] float
averageGainDb(FittingRule rule, double hz, float lossDb, float loss500Db, float loss1000Db, float loss2000Db);

} // namespace rb::dsp
