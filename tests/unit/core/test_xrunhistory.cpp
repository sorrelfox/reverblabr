// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/xrunhistory.h"

#include <gtest/gtest.h>

using namespace rb::core;

TEST(XrunHistoryTest, EachRiseOfTheCountIsOneEventWithWhatWasNew)
{
    XrunHistory history;
    history.observe(0, 1000);
    history.observe(0, 1016);
    history.observe(2, 1032);
    history.observe(2, 1048);
    history.observe(5, 1064);
    const std::vector<XrunEvent> events = history.events();
    ASSERT_EQ(events.size(), 2U);
    EXPECT_EQ(events[0], (XrunEvent{1032, 2, 2}));
    EXPECT_EQ(events[1], (XrunEvent{1064, 5, 3}));
}

TEST(XrunHistoryTest, ANodeStartedAgainCountsFromNoughtAndKeepsWhatCameBefore)
{
    XrunHistory history;
    history.observe(4, 100);
    history.restart();
    history.observe(1, 200);
    // A count that went down without a restart is a new node too.
    history.observe(3, 300);
    history.observe(1, 400);
    const std::vector<XrunEvent> events = history.events();
    ASSERT_EQ(events.size(), 4U);
    EXPECT_EQ(events[1], (XrunEvent{200, 1, 1}));
    EXPECT_EQ(events[2], (XrunEvent{300, 3, 2}));
    EXPECT_EQ(events[3], (XrunEvent{400, 1, 1}));
}

TEST(XrunHistoryTest, OnlyTheLatestAreKept)
{
    XrunHistory history;
    for (std::uint32_t total = 1; total <= XrunHistory::kCapacity + 10; ++total) {
        history.observe(total, total);
    }
    const std::vector<XrunEvent> events = history.events();
    ASSERT_EQ(events.size(), XrunHistory::kCapacity);
    EXPECT_EQ(events.front().total, 11U);
    EXPECT_EQ(events.back().total, XrunHistory::kCapacity + 10);
}
