// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Recording Inspector's outline: how the index's peaks become the
// bars the width has room for, without a loud moment averaged away.

#include "waveformitem.h"

#include <gtest/gtest.h>

using namespace rb::ui;

TEST(WaveformItemTest, EachBarIsTheLoudestPeakUnderIt)
{
    QList<qreal> peaks(512, 0.1);
    peaks[300] = 0.9; // one loud moment, a single peak wide
    const QList<qreal> bars = WaveformItem::barsOf(peaks, 64);
    ASSERT_EQ(bars.size(), 64);
    EXPECT_DOUBLE_EQ(bars[300 * 64 / 512], 0.9) << "the loud moment was averaged away";
    EXPECT_DOUBLE_EQ(bars[0], 0.1);
    int loud = 0;
    for (const qreal bar : bars) {
        loud += bar > 0.5 ? 1 : 0;
    }
    EXPECT_EQ(loud, 1);
}

TEST(WaveformItemTest, MoreBarsThanPeaksRepeatThePeakTheyFallOn)
{
    const QList<qreal> bars = WaveformItem::barsOf({0.2, 0.8}, 6);
    EXPECT_EQ(bars, (QList<qreal>{0.2, 0.2, 0.2, 0.8, 0.8, 0.8}));
    EXPECT_TRUE(WaveformItem::barsOf({}, 10).isEmpty());
    EXPECT_TRUE(WaveformItem::barsOf({0.5}, 0).isEmpty());
    EXPECT_EQ(WaveformItem::barsOf({1.5, -0.2}, 2), (QList<qreal>{1.0, 0.0})) << "a bar left its lane";
}

TEST(WaveformItemTest, AsManyBarsAsTheWidthHasRoomForAndAClickIsAFraction)
{
    WaveformItem item;
    item.setPeaks(QList<qreal>(512, 0.5));
    item.setWidth(399); // bars of 3 and gaps of 1: (399 + 1) / 4
    item.setHeight(100);
    EXPECT_EQ(item.barCount(), 100);
    EXPECT_EQ(item.bars().size(), 100);
    EXPECT_DOUBLE_EQ(item.fractionAt(99.75), 0.25);
    EXPECT_DOUBLE_EQ(item.fractionAt(-5), 0.0);
    EXPECT_DOUBLE_EQ(item.fractionAt(1000), 1.0);
    item.setPeaks({});
    EXPECT_EQ(item.barCount(), 0);
}
