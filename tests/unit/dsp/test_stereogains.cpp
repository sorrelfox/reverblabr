// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The stereo gain laws and their smoother: what Balance, and later
// Pan, multiply each side by.

#include "dsp/stereogains.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace rb::dsp;

TEST(StereoGainsTest, TheBalanceLawOnlyEverTurnsOneSideDown)
{
    // Centre: both sides untouched, exactly.
    EXPECT_EQ(balanceGains(0.0f), (StereoGains{.left = 1.0f, .right = 1.0f}));
    // Half-way: the far side at half its amplitude, the near side at one.
    EXPECT_EQ(balanceGains(-0.5f), (StereoGains{.left = 1.0f, .right = 0.5f}));
    EXPECT_EQ(balanceGains(0.5f), (StereoGains{.left = 0.5f, .right = 1.0f}));
    // The ends: the far side gone.
    EXPECT_EQ(balanceGains(-1.0f), (StereoGains{.left = 1.0f, .right = 0.0f}));
    EXPECT_EQ(balanceGains(1.0f), (StereoGains{.left = 0.0f, .right = 1.0f}));
    // "L 30" leaves the right at 70 %.
    EXPECT_FLOAT_EQ(balanceGains(-0.3f).right, 0.7f);

    // Nothing is ever turned up, anywhere along the control.
    for (int step = -100; step <= 100; ++step) {
        const StereoGains gains = balanceGains(static_cast<float>(step) / 100.0f);
        EXPECT_LE(gains.left, 1.0f);
        EXPECT_LE(gains.right, 1.0f);
        EXPECT_TRUE(gains.left == 1.0f || gains.right == 1.0f) << "both sides moved at " << step;
    }
}

TEST(StereoGainsTest, APositionPastTheEndIsTheEnd)
{
    EXPECT_EQ(balanceGains(-3.0f), balanceGains(-1.0f));
    EXPECT_EQ(balanceGains(7.0f), balanceGains(1.0f));
}

TEST(StereoGainsTest, BothSidesGlideTogetherAndArriveTogether)
{
    StereoGainSmoother smoother;
    smoother.prepare(48000.0, 20.0f);
    smoother.reset(balanceGains(1.0f));
    smoother.setTarget(balanceGains(-1.0f));

    // 20 ms at 48 kHz is 960 samples; both ramps are straight, so each step
    // is the whole distance over that many.
    StereoGains previous = smoother.current();
    float steepest = 0.0f;
    for (int sample = 0; sample < 960; ++sample) {
        const StereoGains now = smoother.next();
        steepest = std::max({steepest, std::abs(now.left - previous.left), std::abs(now.right - previous.right)});
        previous = now;
    }
    EXPECT_EQ(smoother.current(), balanceGains(-1.0f));
    EXPECT_FALSE(smoother.isSmoothing());
    EXPECT_NEAR(steepest, 1.0f / 960.0f, 1e-6f);
}

TEST(StereoGainsTest, ThePanLawKeepsThePowerWhereverTheSoundIsPut)
{
    // The ends: one side alone, exactly.
    EXPECT_EQ(panGains(-1.0f), (StereoGains{.left = 1.0f, .right = 0.0f}));
    EXPECT_EQ(panGains(1.0f), (StereoGains{.left = 0.0f, .right = 1.0f}));
    // The centre: −3.01 dB each side.
    EXPECT_NEAR(panGains(0.0f).left, std::sqrt(0.5f), 1e-6f);
    EXPECT_NEAR(panGains(0.0f).right, std::sqrt(0.5f), 1e-6f);
    // Half-way: cos and sin of an eighth of a turn.
    EXPECT_NEAR(panGains(-0.5f).left, 0.9238795f, 1e-6f);
    EXPECT_NEAR(panGains(-0.5f).right, 0.3826834f, 1e-6f);
    EXPECT_EQ(panGains(0.5f).left, panGains(-0.5f).right);

    for (int step = -100; step <= 100; ++step) {
        const StereoGains gains = panGains(static_cast<float>(step) / 100.0f);
        EXPECT_NEAR(gains.left * gains.left + gains.right * gains.right, 1.0f, 1e-5f) << "at " << step;
    }
    EXPECT_EQ(panGains(-4.0f), panGains(-1.0f));
    EXPECT_EQ(panGains(4.0f), panGains(1.0f));
}
