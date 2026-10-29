// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The feedback guard's reasoning, on graphs written out by hand. No PipeWire
// is involved: the rings it has to find are a property of the graph, and
// saying so here means every shape can be tried, including the ones that are
// awkward to build in a real session.

#include "pw/feedbackguard.h"
#include "pw/pwobjects.h"

#include <QList>
#include <QString>
#include <gtest/gtest.h>

#include <cstdint>

using namespace rb::pw;
using namespace Qt::StringLiterals;

namespace
{

PwNode node(std::uint32_t id, const QString &name, const QString &linkGroup = {})
{
    PwNode result;
    result.object = PwObjectId{id, id};
    result.name = name;
    result.description = name;
    result.linkGroup = linkGroup;
    return result;
}

PwLink link(std::uint32_t outputNode, std::uint32_t inputNode)
{
    PwLink result;
    result.outputNode = outputNode;
    result.inputNode = inputNode;
    return result;
}

} // namespace

TEST(FeedbackGuard, AnEmptyGraphHasNoLoops)
{
    EXPECT_FALSE(findFeedbackPath(1, 2, {}, {}).has_value());
}

TEST(FeedbackGuard, ANodeLinkedToItselfIsALoop)
{
    const QList<PwNode> nodes{node(1, u"Podcast"_s)};
    const std::optional<FeedbackPath> path = findFeedbackPath(1, 1, nodes, {});
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(path->nodes, (QList<std::uint32_t>{1}));
    EXPECT_EQ(path->describe(), u"Podcast"_s);
}

TEST(FeedbackGuard, CapturingTheMonitorOfASinkWeArePlayingToIsALoop)
{
    // A sink and its monitor are the same node, so the ring is of two: our
    // session feeds the speakers, and the speakers' monitor would feed the
    // session.
    const QList<PwNode> nodes{node(1, u"Podcast"_s), node(2, u"Speakers"_s)};
    const QList<PwLink> links{link(1, 2)}; // the session already plays to the speakers

    const std::optional<FeedbackPath> path = findFeedbackPath(2, 1, nodes, links);
    ASSERT_TRUE(path.has_value()) << "capturing the monitor of the sink we play to has to be refused";
    EXPECT_EQ(path->describe(), u"Podcast → Speakers"_s);
}

TEST(FeedbackGuard, ApplicationToOutputDeviceIsNotALoop)
{
    // The ordinary case, and the one a false positive would break.
    const QList<PwNode> nodes{node(1, u"Firefox"_s), node(2, u"Podcast"_s), node(3, u"Speakers"_s)};
    EXPECT_FALSE(findFeedbackPath(1, 2, nodes, {}).has_value()) << "the application into the session";
    EXPECT_FALSE(findFeedbackPath(2, 3, nodes, {link(1, 2)}).has_value()) << "the session out to the speakers";
}

TEST(FeedbackGuard, TwoSessionsOnDifferentSinksDoNotSeeEachOther)
{
    const QList<PwNode> nodes{node(1, u"Firefox"_s),
                              node(2, u"Podcast"_s),
                              node(3, u"Speakers"_s),
                              node(4, u"Music"_s),
                              node(5, u"Stream"_s),
                              node(6, u"Headphones"_s)};
    const QList<PwLink> links{link(1, 2), link(2, 3), link(4, 5)};
    EXPECT_FALSE(findFeedbackPath(5, 6, nodes, links).has_value());
}

TEST(FeedbackGuard, ALongerRingIsFoundAndReportedInOrder)
{
    const QList<PwNode> nodes{node(1, u"Podcast"_s), node(2, u"Speakers"_s), node(3, u"Monitor Tap"_s)};
    const QList<PwLink> links{link(1, 2), link(2, 3)};

    const std::optional<FeedbackPath> path = findFeedbackPath(3, 1, nodes, links);
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(path->nodes, (QList<std::uint32_t>{1, 2, 3}));
    EXPECT_EQ(path->describe(), u"Podcast → Speakers → Monitor Tap"_s);
}

TEST(FeedbackGuard, NodesSharingALinkGroupPassSoundWithNoLinkBetweenThem)
{
    // A loopback, a filter chain, a combined sink: two nodes joined inside
    // PipeWire. A ring through one of them is invisible in the links alone,
    // which is exactly how a virtual source fed by our own output looks.
    const QList<PwNode> nodes{
        node(1, u"Podcast"_s),
        node(2, u"Virtual Cable (in)"_s, u"loopback-7"_s),
        node(3, u"Virtual Cable (out)"_s, u"loopback-7"_s),
    };
    const QList<PwLink> links{link(1, 2)}; // our output feeds the cable

    const std::optional<FeedbackPath> path = findFeedbackPath(3, 1, nodes, links);
    ASSERT_TRUE(path.has_value()) << "the link group was not followed";
    EXPECT_EQ(path->nodes, (QList<std::uint32_t>{1, 2, 3}));

    // Without the link group there is nothing joining the two halves, and
    // the same question has a different answer.
    const QList<PwNode> unjoined{node(1, u"Podcast"_s), node(2, u"A"_s), node(3, u"B"_s)};
    EXPECT_FALSE(findFeedbackPath(3, 1, unjoined, links).has_value());
}

TEST(FeedbackGuard, ALinkCanBeAskedWhetherItIsTheOneClosingTheRing)
{
    // A ring can close around a link that was safe when it was made. Leaving
    // that link out of the walk asks exactly that question.
    const QList<PwNode> nodes{node(1, u"Podcast"_s), node(2, u"Speakers"_s), node(3, u"Tap"_s)};
    const QList<PwLink> links{link(1, 2), link(2, 3), link(3, 1)};

    // Our link is 3 → 1. Without it, can the sound still get from 1 back to 3?
    EXPECT_TRUE(findFeedbackPath(3, 1, nodes, links, NodeEdge{3, 1}).has_value());

    // A link that is not part of any ring is not blamed for one.
    const QList<PwLink> straight{link(1, 2), link(2, 3)};
    EXPECT_FALSE(findFeedbackPath(2, 3, nodes, straight, NodeEdge{2, 3}).has_value());
}

TEST(FeedbackGuard, AGraphWithNoWayBackIsAccepted)
{
    // Twenty blocks in a row, which is the shape of a long session.
    QList<PwNode> nodes;
    QList<PwLink> links;
    for (std::uint32_t id = 1; id <= 20; ++id) {
        nodes.append(node(id, u"b%1"_s.arg(id)));
        if (id > 1) {
            links.append(link(id - 1, id));
        }
    }
    EXPECT_FALSE(findFeedbackPath(20, 21, nodes, links).has_value()) << "off the end of the chain";
    EXPECT_TRUE(findFeedbackPath(20, 1, nodes, links).has_value()) << "and back to the start closes it";
}
