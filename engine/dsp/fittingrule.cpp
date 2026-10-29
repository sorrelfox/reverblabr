// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "dsp/fittingrule.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace rb::dsp
{

namespace
{

/// NAL-R's constant per frequency (Byrne & Dillon, 1986), in dB: what
/// shapes the gain to the spectrum of speech, so that every band of it is
/// equally loud to the listener. Between two frequencies it is taken on a
/// straight line in log frequency; below 250 Hz and above 6 kHz, where the
/// formula stops, the nearest one carries on.
constexpr std::array<std::pair<double, double>, 9> kNalRConstants{{
    {250.0, -17.0},
    {500.0, -8.0},
    {750.0, -3.0},
    {1000.0, 1.0},
    {1500.0, 1.0},
    {2000.0, -1.0},
    {3000.0, -2.0},
    {4000.0, -2.0},
    {6000.0, -2.0},
}};

/// How much of the loss at each frequency NAL-R gives back, and how much of
/// the sum of the losses at 500, 1000 and 2000 Hz it adds everywhere.
constexpr double kNalRSlope = 0.31;
constexpr double kNalRAverage = 0.05;

[[nodiscard]] double nalRConstant(double hz)
{
    if (hz <= kNalRConstants.front().first) {
        return kNalRConstants.front().second;
    }
    if (hz >= kNalRConstants.back().first) {
        return kNalRConstants.back().second;
    }
    for (std::size_t index = 1; index < kNalRConstants.size(); ++index) {
        const auto [highHz, highDb] = kNalRConstants[index];
        if (hz <= highHz) {
            const auto [lowHz, lowDb] = kNalRConstants[index - 1];
            const double along = std::log2(hz / lowHz) / std::log2(highHz / lowHz);
            return lowDb + ((highDb - lowDb) * along);
        }
    }
    return kNalRConstants.back().second;
}

} // namespace

std::string_view fittingRuleName(FittingRule rule) noexcept
{
    switch (rule) {
    case FittingRule::HalfGain:
        return "halfGain";
    case FittingRule::NalR:
        return "nalR";
    }
    return "nalR";
}

std::optional<FittingRule> fittingRuleFromName(std::string_view name) noexcept
{
    for (const FittingRule rule : {FittingRule::HalfGain, FittingRule::NalR}) {
        if (fittingRuleName(rule) == name) {
            return rule;
        }
    }
    return std::nullopt;
}

FittingOptions defaultFittingOptions(AudiogramUnit unit) noexcept
{
    FittingOptions options;
    if (unit == AudiogramUnit::DbFs) {
        // **Placeholders.** Through headphones of unknown sensitivity, no
        // dBFS level means a loudness; the listening check (later) measures
        // where the listener's no-loss level and speech sit, and replaces
        // these.
        options.averageLevelDb = -30.0f;
        options.noLossDb = -90.0f;
        options.usualTooLoudDb = -6.0f;
    }
    return options;
}

float averageGainDb(FittingRule rule, double hz, float lossDb, float loss500Db, float loss1000Db, float loss2000Db)
{
    double gain = 0.0;
    switch (rule) {
    case FittingRule::HalfGain:
        gain = 0.5 * lossDb;
        break;
    case FittingRule::NalR:
        gain = (kNalRSlope * lossDb) + (kNalRAverage * (loss500Db + loss1000Db + loss2000Db)) + nalRConstant(hz);
        break;
    }
    // **Never more gain than the loss.** NAL-R's constants are positive
    // round 1 kHz, so the bare formula would lift an ear that hears
    // normally by a decibel; a band that has lost nothing is left alone.
    return static_cast<float>(std::clamp(gain, 0.0, std::max(0.0, static_cast<double>(lossDb))));
}

std::vector<BandFit>
fitAudiogram(const Audiogram &audiogram, std::span<const double> centresHz, const FittingOptions &options)
{
    std::vector<BandFit> bands;
    bands.reserve(centresHz.size());

    const auto lossAt = [&](double hz) {
        return audiogramLevelAt(audiogram, hz).value_or(options.noLossDb) - options.noLossDb;
    };
    const float loss500 = lossAt(500.0);
    const float loss1000 = lossAt(1000.0);
    const float loss2000 = lossAt(2000.0);

    const float average = options.averageLevelDb;
    const float soft = average - options.levelStepDb;
    const float loud = average + options.levelStepDb;

    for (const double hz : centresHz) {
        BandFit band;
        const float level = audiogramLevelAt(audiogram, hz).value_or(options.noLossDb);
        const float loss = level - options.noLossDb;

        const float tooLoud =
            audiogramTooLoudAt(audiogram, hz).value_or(std::max(options.usualTooLoudDb, level + kTooLoudAboveLevelDb));
        band.ceilingDb = tooLoud - options.ceilingMarginDb;

        // What the rule asks for average sound; soft sound is lifted at
        // least to just above the quietest level heard, and loud sound is
        // given no more than average sound is.
        const float averageGain = averageGainDb(options.rule, hz, loss, loss500, loss1000, loss2000);
        const float softGain = std::max(averageGain, level + options.softMarginDb - soft);
        const float loudGain = averageGain;

        // **Under the ceiling, and under the most any band may have.** Both
        // limits fall as the input rises (the ceiling is nearer a loud
        // sound than a soft one), so taking the smaller of each keeps soft ≥
        // average ≥ loud.
        band.softGainDb = std::min({softGain, band.ceilingDb - soft, options.maximumGainDb});
        band.averageGainDb = std::min({averageGain, band.ceilingDb - average, options.maximumGainDb});
        band.loudGainDb = std::min({loudGain, band.ceilingDb - loud, options.maximumGainDb});

        band.beyondMaximum = std::min(softGain, band.ceilingDb - soft) > options.maximumGainDb;
        if (const std::optional<std::size_t> nearest = nearestMeasuredPoint(audiogram, hz)) {
            band.noResponse = audiogram.points[*nearest].mark == AudiogramMark::NoResponse;
        }
        bands.push_back(band);
    }
    return bands;
}

} // namespace rb::dsp
