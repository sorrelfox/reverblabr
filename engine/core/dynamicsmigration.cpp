// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/dynamicsmigration.h"

#include "dsp/dynamics.h"

#include <QLatin1StringView>

#include <algorithm>

namespace rb::core
{

using namespace Qt::StringLiterals;

namespace
{

constexpr auto kThreshold = "threshold"_L1;
constexpr auto kHeight = "height"_L1;
constexpr auto kRatio = "ratio"_L1;
constexpr auto kInfiniteRatio = "infiniteRatio"_L1;
constexpr auto kKnee = "knee"_L1;
constexpr auto kExpanderThreshold = "expanderThreshold"_L1;
constexpr auto kExpanderRatio = "expanderRatio"_L1;
constexpr auto kExpanderRange = "expanderRange"_L1;

/// What a block's curve said when a parameter was left out.
constexpr double kOldThresholdDb = -20.0;
constexpr double kOldRatio = 20.0;
constexpr double kOldKneeDb = 12.0;
constexpr double kOldExpanderThresholdDb = -120.0;

/// Where a block with its expander off keeps the point the expander starts
/// at: the new default, away from the corner of the graph.
constexpr double kResting = -80.0;

/// How few decibels below its threshold an expander may take its whole old
/// range off. Any gate does it inside this, and keeps its steepness; a
/// shallower range sets the ratio that takes it off here.
constexpr double kRangeWithinDb = 3.0;

} // namespace

bool holdsOldDynamicsParams(const QJsonObject &params)
{
    return params.contains(kRatio) || params.contains(kKnee) || params.contains(kInfiniteRatio)
        || params.contains(kExpanderRange);
}

QJsonObject dynamicsParamsFromOld(const QJsonObject &params)
{
    QJsonObject result = params;
    for (const QLatin1StringView gone : {kRatio, kKnee, kInfiniteRatio, kExpanderRange}) {
        result.remove(gone);
    }

    // --- the top ---
    dsp::DynamicsSettings old;
    old.thresholdDb = static_cast<float>(params.value(kThreshold).toDouble(kOldThresholdDb));
    old.ratio = params.value(kInfiniteRatio).toBool(false)
        ? dsp::kInfiniteRatio
        : static_cast<float>(params.value(kRatio).toDouble(kOldRatio));
    old.kneeDb = static_cast<float>(params.value(kKnee).toDouble(kOldKneeDb));

    double thresholdDb = static_cast<double>(dsp::kDynamicsHighestThresholdDb);
    double heightDb = static_cast<double>(dsp::kDynamicsHighestHeightDb);
    if (old.ratio > 1.0f) {
        thresholdDb = std::clamp(static_cast<double>(old.thresholdDb) - static_cast<double>(old.kneeDb) * 0.5,
                                 static_cast<double>(dsp::kDynamicsFloorDb),
                                 static_cast<double>(dsp::kDynamicsHighestThresholdDb));
        // **Above the new threshold, so that there is a bend to speak of**:
        // a threshold at full scale is compared a little past it.
        const float comparedAtDb = std::max(0.0f, static_cast<float>(thresholdDb) + 3.0f);
        heightDb = static_cast<double>(
            dsp::dynamicsHeightFor(static_cast<float>(thresholdDb), comparedAtDb, dsp::outputDb(comparedAtDb, old)));
    }

    // --- the bottom ---
    const double ratio = params.value(kExpanderRatio).toDouble(1.0);
    const double rangeDb = params.value(kExpanderRange).toDouble(0.0);
    double expanderRatio = 1.0;
    if (ratio > 1.0 && rangeDb > 0.0) {
        expanderRatio = std::clamp(
            std::min(ratio, 1.0 + rangeDb / kRangeWithinDb), 1.0, static_cast<double>(dsp::kDynamicsHighestRatio));
    }
    const double expanderDb =
        expanderRatio > 1.0 ? params.value(kExpanderThreshold).toDouble(kOldExpanderThresholdDb) : kResting;

    result.insert(kThreshold, thresholdDb);
    result.insert(kHeight, heightDb);
    result.insert(kExpanderThreshold, std::min(expanderDb, thresholdDb));
    result.insert(kExpanderRatio, expanderRatio);
    return result;
}

} // namespace rb::core
