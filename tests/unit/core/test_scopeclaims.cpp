// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Who has a scope slot: a block a surface has asked for lately, while the
// session has it.

#include "core/scopeclaims.h"

#include <gtest/gtest.h>

#include <vector>

using namespace rb;
using namespace rb::core;
using namespace Qt::StringLiterals;

TEST(ScopeClaims, NothingAskedForIsNothingToDo)
{
    ScopeClaims claims;
    EXPECT_TRUE(claims.isIdle());
    claims.renew(u"a"_s, 100);
    EXPECT_FALSE(claims.isIdle());
    claims.expire(100);
    EXPECT_TRUE(claims.isIdle());
}

TEST(ScopeClaims, ALeaseRunsOutUnlessItIsRenewed)
{
    ScopeClaims claims;
    claims.renew(u"a"_s, 100);
    claims.expire(50);
    EXPECT_TRUE(claims.isWanted(u"a"_s));
    claims.renew(u"a"_s, 200);
    claims.expire(150);
    EXPECT_TRUE(claims.isWanted(u"a"_s)) << "a second word did not extend the first";
    claims.expire(200);
    EXPECT_FALSE(claims.isWanted(u"a"_s));
}

TEST(ScopeClaims, ASlotStaysUntilItIsLetGoOf)
{
    // Whoever writes has to be stopped first, so a lease running out cannot
    // be what frees it.
    ScopeClaims claims;
    claims.renew(u"a"_s, 100);
    const int slot = claims.give(u"a"_s, nullptr);
    ASSERT_GE(slot, 0);
    claims.expire(200);
    EXPECT_EQ(claims.slotOf(u"a"_s), slot);
    claims.letGo(u"a"_s);
    EXPECT_EQ(claims.slotOf(u"a"_s), -1);
    EXPECT_TRUE(claims.isIdle());
}

TEST(ScopeClaims, EachBlockHasItsOwnSlotUntilThereAreNoneLeft)
{
    ScopeClaims claims;
    std::vector<int> slots;
    for (std::size_t block = 0; block < rt::kScopeSlots; ++block) {
        slots.push_back(claims.give(QString::number(block), nullptr));
    }
    std::ranges::sort(slots);
    for (std::size_t block = 0; block < rt::kScopeSlots; ++block) {
        EXPECT_EQ(slots[block], static_cast<int>(block));
    }
    EXPECT_EQ(claims.give(u"one too many"_s, nullptr), -1);
    claims.letGo(u"3"_s);
    EXPECT_GE(claims.give(u"one too many"_s, nullptr), 0) << "a slot let go of is free again";
}
