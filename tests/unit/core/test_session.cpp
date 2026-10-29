// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/session.h"
#include "sessionfixtures.h"

#include <QString>
#include <gtest/gtest.h>

using namespace rb::core;
using rb::test::fullSession;

TEST(Session, StartsWithAnIdOfItsOwn)
{
    const Session first;
    const Session second;
    EXPECT_FALSE(first.id.isNull());
    EXPECT_NE(first.id, second.id);
    EXPECT_TRUE(first.blocks.isEmpty());
    EXPECT_EQ(first.revision, 0u);
}

TEST(Session, FindsBlocksByIdAndSaysWhenThereIsNoSuchOne)
{
    const Session session = fullSession();
    ASSERT_NE(session.findBlock(QStringLiteral("b2")), nullptr);
    EXPECT_EQ(session.findBlock(QStringLiteral("b2"))->type, QStringLiteral("meter.peakrms"));
    EXPECT_EQ(session.findBlock(QStringLiteral("b9")), nullptr);
    EXPECT_TRUE(session.hasBlock(QStringLiteral("b1")));
    EXPECT_FALSE(session.hasBlock(QStringLiteral("b9")));
}

TEST(Session, GivesOutTheLowestFreeBlockId)
{
    Session session;
    EXPECT_EQ(session.nextBlockId(), QStringLiteral("b1"));

    session = fullSession();
    EXPECT_EQ(session.nextBlockId(), QStringLiteral("b4"));

    // A session that has had blocks removed fills the gap rather than
    // counting on for ever.
    session.blocks.removeIf([](const BlockInstance &block) { return block.id == QStringLiteral("b2"); });
    EXPECT_EQ(session.nextBlockId(), QStringLiteral("b2"));
}

TEST(Session, ListsTheWiresThatTouchABlock)
{
    const Session session = fullSession();
    EXPECT_EQ(session.wiresTouching(QStringLiteral("b1")).size(), 2);
    EXPECT_EQ(session.wiresTouching(QStringLiteral("b2")).size(), 3);
    EXPECT_EQ(session.wiresTouching(QStringLiteral("b3")).size(), 1);
    EXPECT_TRUE(session.wiresTouching(QStringLiteral("b9")).isEmpty());
}

TEST(Session, FollowsTheWiresForwardsToSeeWhatASignalCanReach)
{
    const Session session = fullSession();
    // b1 -> b2 -> b3, so the first reaches all three and nothing reaches
    // backwards. A block reaches itself, which is what makes "would this
    // wire close a loop" one question rather than two.
    EXPECT_TRUE(session.reaches(QStringLiteral("b1"), QStringLiteral("b1")));
    EXPECT_TRUE(session.reaches(QStringLiteral("b1"), QStringLiteral("b2")));
    EXPECT_TRUE(session.reaches(QStringLiteral("b1"), QStringLiteral("b3")));
    EXPECT_FALSE(session.reaches(QStringLiteral("b3"), QStringLiteral("b1")));
    EXPECT_FALSE(session.reaches(QStringLiteral("b2"), QStringLiteral("b1")));
    EXPECT_FALSE(session.reaches(QStringLiteral("b1"), QStringLiteral("b9")));
}

TEST(Session, WalksASessionThatAlreadyHoldsALoopExactlyOnce)
{
    // A file written by hand, or by a version that let this through: the
    // walk has to end rather than follow the loop round for ever.
    Session session = fullSession();
    session.wires.append(Wire{QStringLiteral("b3"), 0, QStringLiteral("b1"), 0});
    EXPECT_TRUE(session.reaches(QStringLiteral("b3"), QStringLiteral("b2")));
    EXPECT_FALSE(session.reaches(QStringLiteral("b1"), QStringLiteral("b9")));
}

TEST(Session, TwoSessionsWithTheSameContentsAreEqual)
{
    const Session first = fullSession();
    Session second = fullSession();
    EXPECT_EQ(first, second);

    second.blocks[1].position = QPointF{0.0, 0.0};
    EXPECT_NE(first, second) << "every field counts, positions included";
}

TEST(Wire, IsIdentifiedByBothEndsAndBothPorts)
{
    const Wire wire{QStringLiteral("b1"), 0, QStringLiteral("b2"), 0};
    EXPECT_EQ(wire, (Wire{QStringLiteral("b1"), 0, QStringLiteral("b2"), 0}));
    EXPECT_NE(wire, (Wire{QStringLiteral("b1"), 1, QStringLiteral("b2"), 0}));
    EXPECT_NE(wire, (Wire{QStringLiteral("b2"), 0, QStringLiteral("b1"), 0})) << "a wire has a direction";
}
