// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Where a VU needle points and what it reads: the scale the dials are
// drawn with, and the reading the real block's published amplitude comes to
// on the way to the screen.

#include "blocks/vumeter.h"
#include "offlinerunner.h"
#include "rt/meter.h"
#include "signals.h"
#include "vuscale.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace rb;
using namespace rb::ui;
using namespace Qt::StringLiterals;

TEST(VuScaleTest, TheScaleIsLinearInAmplitudeAndEndsAtPlusThree)
{
    EXPECT_FLOAT_EQ(vuScale::positionOf(3.0f), 1.0f);
    // 0 VU is 71 % of the way along, which is where the red starts on
    // every VU dial and on the design's.
    EXPECT_NEAR(vuScale::positionOf(0.0f), 0.708f, 0.001f);
    EXPECT_NEAR(vuScale::positionOf(-6.0f), vuScale::positionOf(0.0f) / 2.0f, 0.001f)
        << "6 dB down is half the swing: the scale is in proportion to the amplitude";
    EXPECT_NEAR(vuScale::positionOf(-19.9f), 0.0716f, 0.001f);
}

TEST(VuScaleTest, ANeedleStaysOnItsDial)
{
    EXPECT_EQ(vuScale::positionOf(6.0f), 1.0f);
    EXPECT_EQ(vuScale::positionOf(-20.0f), 0.0f);
    EXPECT_EQ(vuScale::positionOf(-80.0f), 0.0f);
    EXPECT_EQ(vuScale::positionOf(std::nanf("")), 0.0f);
    EXPECT_EQ(vuScale::vuOf(0.0f, -18.0f), vuScale::kBottomVu) << "silence reads the pin, not minus infinity";
}

TEST(VuScaleTest, EveryReferenceIsTheLevelThatReadsZero)
{
    ASSERT_EQ(vuScale::referenceChoices(), (QStringList{u"-20"_s, u"-18"_s, u"-14"_s}));
    EXPECT_EQ(vuScale::defaultReference(), u"-18"_s);
    for (const QString &choice : vuScale::referenceChoices()) {
        const float db = vuScale::referenceDbOf(choice);
        EXPECT_NEAR(vuScale::vuOf(std::pow(10.0f, db / 20.0f), db), 0.0f, 1e-4f) << choice.toStdString();
    }
    EXPECT_EQ(vuScale::referenceDbOf(u"-12"_s), -18.0f) << "an unknown id reads as the default";
}

TEST(VuScaleTest, EveryMarkIsOnTheDialInOrder)
{
    const QList<qreal> marks = vuScale::marks();
    ASSERT_EQ(marks.front(), vuScale::kBottomVu);
    ASSERT_EQ(marks.back(), vuScale::kTopVu);
    for (qsizetype i = 1; i < marks.size(); ++i) {
        EXPECT_GT(vuScale::positionOf(static_cast<float>(marks[i])),
                  vuScale::positionOf(static_cast<float>(marks[i - 1])));
    }
}

TEST(VuScaleTest, ASineEighteenDownThroughTheRealBlockReadsZeroVuOnTheScreen)
{
    // The criterion end to end: the real block, what it publishes where the
    // interface looks, and the reading the dial is drawn from.
    blocks::VuMeter block;
    rt::BlockMeter meter;
    test::BlockRunner runner(block, 2, 2);
    runner.setMeter(&meter);
    ASSERT_TRUE(runner.prepare().has_value());

    const float amplitude = std::pow(10.0f, -18.0f / 20.0f);
    test::MultiSignal input;
    input.push_back(test::sine(96000, 1000.0, test::kTestSampleRate, amplitude));
    input.push_back(test::sine(96000, 1000.0, test::kTestSampleRate, amplitude));
    (void)runner.run(input);

    const VuScale scale;
    const QString reference = scale.defaultReference();
    EXPECT_NEAR(scale.vuOf(meter.value(blocks::VuMeter::kLevelLeftValue), reference), 0.0, 0.05);
    EXPECT_NEAR(scale.vuOf(meter.value(blocks::VuMeter::kLevelRightValue), reference), 0.0, 0.05);
    // And the needle sits on the edge of the red.
    EXPECT_NEAR(scale.positionOf(scale.vuOf(meter.value(blocks::VuMeter::kLevelLeftValue), reference)),
                vuScale::positionOf(0.0f),
                0.005);
}
