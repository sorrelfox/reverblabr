// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "meterscale.h"

#include "dsp/decibel.h"

#include <algorithm>
#include <array>

namespace rb::ui
{

namespace meterScale
{

namespace
{

/// The curve, as the points it passes through: a level and where it sits.
/// **Three segments, not one.** A single straight line in decibels gives a
/// meter whose whole top half is the last 6 dB or whose quiet end is a
/// smear; these anchors give the loud end room without the quiet end
/// collapsing.
struct Anchor {
    float db;
    float position;
};

constexpr std::array<Anchor, 4> kCurve{{
    {kTopDb, 1.0f},
    {-12.0f, 1.0f - kLoudShare}, // the top 12 dB take 45 %
    {-30.0f, 0.28f},
    {kFloorDb, 0.0f},
}};

constexpr std::array<float, 12>
    kFullTicks{0.0f, -1.0f, -3.0f, -6.0f, -9.0f, -12.0f, -18.0f, -24.0f, -30.0f, -36.0f, -48.0f, -60.0f};
constexpr std::array<float, 4> kCompactTicks{-3.0f, -9.0f, -18.0f, -36.0f};

} // namespace

float positionOf(float db) noexcept
{
    if (!(db > kFloorDb)) {
        // Also catches NaN, which must land somewhere rather than nowhere.
        return 0.0f;
    }
    if (db >= kTopDb) {
        return 1.0f;
    }
    for (std::size_t segment = 1; segment < kCurve.size(); ++segment) {
        const Anchor &upper = kCurve[segment - 1];
        const Anchor &lower = kCurve[segment];
        if (db >= lower.db) {
            const float through = (db - lower.db) / (upper.db - lower.db);
            return lower.position + through * (upper.position - lower.position);
        }
    }
    return 0.0f;
}

float dbAt(float position) noexcept
{
    if (!(position > 0.0f)) {
        return kFloorDb;
    }
    if (position >= 1.0f) {
        return kTopDb;
    }
    for (std::size_t segment = 1; segment < kCurve.size(); ++segment) {
        const Anchor &upper = kCurve[segment - 1];
        const Anchor &lower = kCurve[segment];
        if (position >= lower.position) {
            const float through = (position - lower.position) / (upper.position - lower.position);
            return lower.db + through * (upper.db - lower.db);
        }
    }
    return kFloorDb;
}

std::span<const float> ticks(Ticks which) noexcept
{
    return which == Ticks::Compact ? std::span<const float>(kCompactTicks) : std::span<const float>(kFullTicks);
}

} // namespace meterScale

MeterScale::MeterScale(QObject *parent)
    : QObject(parent)
{
}

qreal MeterScale::topDb() const
{
    return meterScale::kTopDb;
}

qreal MeterScale::floorDb() const
{
    return meterScale::kFloorDb;
}

namespace
{

[[nodiscard]] QList<qreal> asList(std::span<const float> values)
{
    QList<qreal> list;
    list.reserve(static_cast<qsizetype>(values.size()));
    for (const float value : values) {
        list.append(static_cast<qreal>(value));
    }
    return list;
}

} // namespace

QList<qreal> MeterScale::fullTicks() const
{
    return asList(meterScale::ticks(meterScale::Ticks::Full));
}

QList<qreal> MeterScale::compactTicks() const
{
    return asList(meterScale::ticks(meterScale::Ticks::Compact));
}

qreal MeterScale::positionOf(qreal db) const
{
    return meterScale::positionOf(static_cast<float>(db));
}

qreal MeterScale::dbAt(qreal position) const
{
    return meterScale::dbAt(static_cast<float>(position));
}

qreal MeterScale::dbOf(qreal amplitude) const
{
    return dsp::gainToDb(static_cast<float>(amplitude));
}

} // namespace rb::ui
