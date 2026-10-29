// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Which blocks an edit can be heard through. A lane named here is faded
// across the swap; a lane not named here has to carry on sample for sample,
// so a wrong answer either clicks or dips where it should not.

#include "graph/graphdiff.h"
#include "testblocks.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace rb;
using namespace rb::graph;

namespace
{

/// Two independent chains: src -> gain -> sink, and src2 -> gain2 -> sink2.
/// Editing one must leave the other alone.
class Blocks
{
public:
    test::ConstantSource source{1.0f, 1};
    test::ConstantSource source2{0.5f, 1};
    test::GainStage gain{2.0f, 1};
    test::GainStage gain2{3.0f, 1};
    test::GainStage extra{1.5f, 1};
    test::Sink sink{1};
    test::Sink sink2{1};
};

[[nodiscard]] NodeSpec node(const std::string &id, dsp::Block *block, bool enabled = true)
{
    NodeSpec spec;
    spec.id = id;
    spec.block = block;
    spec.enabled = enabled;
    return spec;
}

[[nodiscard]] EdgeSpec edge(std::size_t from, std::size_t to)
{
    EdgeSpec spec;
    spec.fromNode = from;
    spec.fromPort = 0;
    spec.toNode = to;
    spec.toPort = 0;
    return spec;
}

/// src(0) -> gain(1) -> sink(2), src2(3) -> gain2(4) -> sink2(5)
[[nodiscard]] GraphSpec twoChains(Blocks &blocks)
{
    GraphSpec spec;
    spec.nodes = {node("src", &blocks.source),
                  node("gain", &blocks.gain),
                  node("sink", &blocks.sink),
                  node("src2", &blocks.source2),
                  node("gain2", &blocks.gain2),
                  node("sink2", &blocks.sink2)};
    spec.edges = {edge(0, 1), edge(1, 2), edge(3, 4), edge(4, 5)};
    return spec;
}

[[nodiscard]] bool names(const std::vector<std::string> &changed, const std::string &id)
{
    return std::ranges::find(changed, id) != changed.end();
}

} // namespace

TEST(GraphDiff, TwoGraphsThatSayTheSameThingDifferNowhere)
{
    Blocks blocks;
    EXPECT_TRUE(changedBlocks(twoChains(blocks), twoChains(blocks)).empty());
}

TEST(GraphDiff, ABlockAddedToAChainChangesItAndEverythingBelowIt)
{
    Blocks blocks;
    const GraphSpec before = twoChains(blocks);

    // src -> extra -> gain -> sink, leaving the second chain alone.
    GraphSpec after = before;
    after.nodes.push_back(node("extra", &blocks.extra));
    after.edges = {edge(0, 6), edge(6, 1), edge(1, 2), edge(3, 4), edge(4, 5)};

    const std::vector<std::string> changed = changedBlocks(before, after);
    EXPECT_TRUE(names(changed, "extra"));
    EXPECT_TRUE(names(changed, "gain")) << "it now hears the new block";
    EXPECT_TRUE(names(changed, "sink")) << "and so does what the gain feeds";
    EXPECT_FALSE(names(changed, "src")) << "the source is upstream of the edit and unaffected";
    EXPECT_FALSE(names(changed, "src2"));
    EXPECT_FALSE(names(changed, "gain2")) << "the other chain must carry on sample for sample";
    EXPECT_FALSE(names(changed, "sink2"));
}

TEST(GraphDiff, ABlockTakenOutIsNamedAndSoIsWhatItFed)
{
    Blocks blocks;
    const GraphSpec before = twoChains(blocks);

    GraphSpec after;
    after.nodes = {node("src", &blocks.source),
                   node("sink", &blocks.sink),
                   node("src2", &blocks.source2),
                   node("gain2", &blocks.gain2),
                   node("sink2", &blocks.sink2)};
    after.edges = {edge(0, 1), edge(2, 3), edge(3, 4)};

    const std::vector<std::string> changed = changedBlocks(before, after);
    EXPECT_TRUE(names(changed, "gain")) << "the block that is gone";
    EXPECT_TRUE(names(changed, "sink"));
    EXPECT_FALSE(names(changed, "sink2"));
}

TEST(GraphDiff, TurningABlockOffIsAChange)
{
    Blocks blocks;
    const GraphSpec before = twoChains(blocks);
    GraphSpec after = before;
    after.nodes[1].enabled = false;

    const std::vector<std::string> changed = changedBlocks(before, after);
    EXPECT_TRUE(names(changed, "gain"));
    EXPECT_TRUE(names(changed, "sink"));
    EXPECT_FALSE(names(changed, "src"));
    EXPECT_FALSE(names(changed, "gain2"));
}

TEST(GraphDiff, AnotherBlockUnderTheSameNameIsAChange)
{
    // The id is the session's; the block behind it is not guaranteed to be
    // the same instance, and a different instance has different state.
    Blocks blocks;
    const GraphSpec before = twoChains(blocks);
    GraphSpec after = before;
    after.nodes[1].block = &blocks.extra;

    EXPECT_TRUE(names(changedBlocks(before, after), "gain"));
    EXPECT_FALSE(names(changedBlocks(before, after), "gain2"));
}

TEST(GraphDiff, RewiringNamesBothEndsAndWhatFollows)
{
    Blocks blocks;
    const GraphSpec before = twoChains(blocks);

    // The second chain's source now feeds the first chain's gain.
    GraphSpec after = before;
    after.edges = {edge(3, 1), edge(1, 2), edge(3, 4), edge(4, 5)};

    const std::vector<std::string> changed = changedBlocks(before, after);
    EXPECT_TRUE(names(changed, "gain")) << "it hears a different source";
    EXPECT_TRUE(names(changed, "sink"));
    EXPECT_FALSE(names(changed, "src")) << "it still puts out what it always did; nobody is listening";
    EXPECT_FALSE(names(changed, "src2")) << "and so does the source that is now heard twice";
    EXPECT_FALSE(names(changed, "gain2")) << "but the second chain still hears the same thing";
    EXPECT_FALSE(names(changed, "sink2"));
}

TEST(GraphDiff, ChangesAreNamedInTheOrderTheNewGraphGivesThem)
{
    Blocks blocks;
    const GraphSpec before = twoChains(blocks);
    GraphSpec after = before;
    after.nodes[1].enabled = false;

    const std::vector<std::string> changed = changedBlocks(before, after);
    ASSERT_EQ(changed.size(), 2U);
    EXPECT_EQ(changed[0], "gain");
    EXPECT_EQ(changed[1], "sink");
}
