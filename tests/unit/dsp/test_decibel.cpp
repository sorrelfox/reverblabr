// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "dsp/decibel.h"

#include <gtest/gtest.h>

#include <limits>

using namespace rb::dsp;

TEST(Decibel, KnownGains)
{
    EXPECT_FLOAT_EQ(dbToGain(0.0f), 1.0f);
    EXPECT_FLOAT_EQ(dbToGain(20.0f), 10.0f);
    EXPECT_FLOAT_EQ(dbToGain(-20.0f), 0.1f);
    EXPECT_NEAR(dbToGain(-6.0206f), 0.5f, 1e-5f);
    EXPECT_NEAR(dbToGain(kSilenceDb), kSilenceGain, 1e-9f);
}

TEST(Decibel, KnownLevels)
{
    EXPECT_FLOAT_EQ(gainToDb(1.0f), 0.0f);
    EXPECT_FLOAT_EQ(gainToDb(10.0f), 20.0f);
    EXPECT_NEAR(gainToDb(0.5f), -6.0206f, 1e-4f);
}

TEST(Decibel, SilenceFloor)
{
    EXPECT_EQ(gainToDb(0.0f), kSilenceDb);
    EXPECT_EQ(gainToDb(-1.0f), kSilenceDb);
    EXPECT_EQ(gainToDb(kSilenceGain), kSilenceDb);
    EXPECT_EQ(gainToDb(std::numeric_limits<float>::quiet_NaN()), kSilenceDb);
    EXPECT_EQ(gainToDb(std::numeric_limits<float>::denorm_min()), kSilenceDb);
}

TEST(Decibel, RoundTrip)
{
    for (float db = -100.0f; db <= 24.0f; db += 0.5f) {
        EXPECT_NEAR(gainToDb(dbToGain(db)), db, 1e-3f) << "at " << db << " dB";
    }
}
