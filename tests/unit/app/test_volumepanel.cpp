// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The arithmetic behind the Volume block's face and panel: that a
// percentage on the screen is an amplitude in the session, that overdrive
// is the top of the slider, and that turning it down brings the stored gain
// with it rather than leaving a number nobody can see.

#include "volumescale.h"

#include <gtest/gtest.h>

using namespace rb::ui;
using namespace Qt::StringLiterals;

TEST(VolumePanelTest, TwoHundredPerCentIsTwiceTheSignal)
{
    // The criterion: the slider shows a percentage and writes an amplitude.
    // **One number, said two ways** — a panel that kept a percentage and an
    // amplitude would have two that could disagree.
    const VolumeScale scale;

    EXPECT_DOUBLE_EQ(scale.amplitudeOf(200.0), 2.0);
    EXPECT_DOUBLE_EQ(scale.amplitudeOf(100.0), 1.0);
    EXPECT_DOUBLE_EQ(scale.amplitudeOf(0.0), 0.0);
    EXPECT_DOUBLE_EQ(scale.amplitudeOf(400.0), 4.0);

    EXPECT_DOUBLE_EQ(scale.percentOf(2.0), 200.0);
    EXPECT_DOUBLE_EQ(scale.percentOf(1.0), 100.0);

    // And the two are each other's opposite at any point between.
    for (const double percent : {0.0, 37.5, 100.0, 183.0, 400.0}) {
        EXPECT_DOUBLE_EQ(scale.percentOf(scale.amplitudeOf(percent)), percent);
    }
}

TEST(VolumePanelTest, OverdriveIsTheTopOfTheSlider)
{
    // Not a multiplier: choosing 4× makes nothing louder, it makes the
    // slider able to. The block works the same number out from the choice's
    // position, and the two have to agree.
    const VolumeScale scale;

    EXPECT_DOUBLE_EQ(scale.topAmplitude(u"off"_s), 1.0);
    EXPECT_DOUBLE_EQ(scale.topAmplitude(u"2x"_s), 2.0);
    EXPECT_DOUBLE_EQ(scale.topAmplitude(u"3x"_s), 3.0);
    EXPECT_DOUBLE_EQ(scale.topAmplitude(u"4x"_s), 4.0);

    EXPECT_EQ(scale.overdriveChoices(), (QStringList{u"off"_s, u"2x"_s, u"3x"_s, u"4x"_s}));
    EXPECT_EQ(scale.defaultOverdrive(), u"off"_s);
    EXPECT_DOUBLE_EQ(scale.defaultGain(), 1.0);
}

TEST(VolumePanelTest, LoweringOverdriveBringsTheStoredGainWithIt)
{
    // The criterion: lowering Overdrive moves the slider to the new top
    // without the user having to. **What is stored is what is heard** —
    // leaving 400 % in the session under a slider that stops at 100 % would
    // be a number nobody could see and nobody could undo.
    const VolumeScale scale;

    EXPECT_DOUBLE_EQ(scale.clampedGain(3.5, u"4x"_s), 3.5) << "a gain within the range was moved anyway";
    EXPECT_DOUBLE_EQ(scale.clampedGain(3.5, u"2x"_s), 2.0);
    EXPECT_DOUBLE_EQ(scale.clampedGain(4.0, u"off"_s), 1.0);
    // Raising it again leaves the gain where it was: the slider grew, the
    // sound did not.
    EXPECT_DOUBLE_EQ(scale.clampedGain(0.5, u"4x"_s), 0.5);
}

TEST(VolumePanelTest, ASettingThisBuildHasNeverHeardOfIsUnity)
{
    // A session written by a later version is read by an older one. A
    // slider that went to some number nobody could name would be worse than
    // one that went to 100 %.
    const VolumeScale scale;

    EXPECT_DOUBLE_EQ(scale.topAmplitude(u"8x"_s), 1.0);
    EXPECT_DOUBLE_EQ(scale.topAmplitude(QString{}), 1.0);
    EXPECT_DOUBLE_EQ(scale.clampedGain(3.0, u"8x"_s), 1.0);
}
