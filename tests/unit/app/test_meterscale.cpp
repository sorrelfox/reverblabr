// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Where a level sits on a meter. **One mapping**, shared by every
// meter and every scale beside one: a bar drawn with one curve and a scale
// drawn with another is a meter that lies about itself.

#include "meterscale.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

using namespace rb::ui;

TEST(MeterScaleTest, TheEndsAreTheEnds)
{
    EXPECT_FLOAT_EQ(meterScale::positionOf(meterScale::kTopDb), 1.0f);
    EXPECT_FLOAT_EQ(meterScale::positionOf(meterScale::kFloorDb), 0.0f);

    // **Clamped, not extrapolated.** A bar drawn past the end of its meter
    // is not information, and a level above full scale is what a clip light
    // is for.
    EXPECT_FLOAT_EQ(meterScale::positionOf(6.0f), 1.0f);
    EXPECT_FLOAT_EQ(meterScale::positionOf(-200.0f), 0.0f);
    // Silence is kSilenceDb, which is well below the floor and must land at
    // the bottom rather than nowhere.
    EXPECT_FLOAT_EQ(meterScale::positionOf(-std::numeric_limits<float>::infinity()), 0.0f);
    EXPECT_FLOAT_EQ(meterScale::positionOf(std::numeric_limits<float>::quiet_NaN()), 0.0f);
}

TEST(MeterScaleTest, EveryTickHasAPlaceAndTheyAreInOrder)
{
    // The criterion: the mapping is tested at every tick value. A scale
    // whose marks cross over or double up is worse than no scale.
    for (const meterScale::Ticks which : {meterScale::Ticks::Full, meterScale::Ticks::Compact}) {
        const std::span<const float> ticks = meterScale::ticks(which);
        ASSERT_FALSE(ticks.empty());
        float previous = 2.0f; // above the top, so the first tick is below it
        for (const float db : ticks) {
            const float position = meterScale::positionOf(db);
            EXPECT_GE(position, 0.0f) << db << " dB is off the bottom";
            EXPECT_LE(position, 1.0f) << db << " dB is off the top";
            EXPECT_LT(position, previous) << db << " dB is not below the tick above it";
            previous = position;
        }
    }
}

TEST(MeterScaleTest, TheTopTwelveDecibelsGetNearlyHalfTheMeter)
{
    // **The loud end is where the work happens.** The difference between −3
    // and −1 dBFS matters and the difference between −50 and −48 does not,
    // so the top is given room.
    const float twelve = meterScale::positionOf(-12.0f);
    EXPECT_NEAR(1.0f - twelve, meterScale::kLoudShare, 0.01f);

    // And the quiet end is compressed rather than collapsed: the bottom
    // 30 dB still get more than a fifth of the meter.
    EXPECT_GT(meterScale::positionOf(-30.0f), 0.2f);
}

TEST(MeterScaleTest, ItIsMonotonicEverywhereAndNotOnlyAtTheTicks)
{
    // A curve that goes backwards anywhere is a meter whose bar shrinks as
    // the sound gets louder.
    float previous = -1.0f;
    for (float db = meterScale::kFloorDb; db <= meterScale::kTopDb; db += 0.25f) {
        const float position = meterScale::positionOf(db);
        EXPECT_GE(position, previous) << "the curve went backwards at " << db << " dB";
        previous = position;
    }
}

TEST(MeterScaleTest, ReadingItBackwardsGivesTheSameLevel)
{
    // The scale places its marks with positionOf(); something that reads a
    // position — a click on the meter, a tooltip — needs the other
    // direction, and the two have to be the same curve.
    for (float db = meterScale::kFloorDb; db <= meterScale::kTopDb; db += 1.0f) {
        EXPECT_NEAR(meterScale::dbAt(meterScale::positionOf(db)), db, 0.01f) << "at " << db << " dB";
    }
    for (float position = 0.0f; position <= 1.0f; position += 0.01f) {
        EXPECT_NEAR(meterScale::positionOf(meterScale::dbAt(position)), position, 0.001f)
            << "at " << position << " of the meter";
    }
}

TEST(MeterScaleTest, QmlReadsTheSameCurveAsTheBars)
{
    // The scale is drawn in QML and the bars in C++, so they read one
    // mapping rather than each having its own.
    const MeterScale scale;
    EXPECT_DOUBLE_EQ(scale.topDb(), meterScale::kTopDb);
    EXPECT_DOUBLE_EQ(scale.floorDb(), meterScale::kFloorDb);
    EXPECT_EQ(scale.fullTicks().size(), static_cast<qsizetype>(meterScale::ticks(meterScale::Ticks::Full).size()));
    EXPECT_EQ(scale.compactTicks().size(),
              static_cast<qsizetype>(meterScale::ticks(meterScale::Ticks::Compact).size()));

    for (const qreal db : scale.fullTicks()) {
        EXPECT_NEAR(scale.positionOf(db), meterScale::positionOf(static_cast<float>(db)), 1e-6);
    }
    EXPECT_NEAR(scale.dbAt(0.5), meterScale::dbAt(0.5f), 1e-6);
}
