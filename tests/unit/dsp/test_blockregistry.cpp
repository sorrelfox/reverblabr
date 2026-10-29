// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "dsp/block.h"
#include "dsp/blockregistry.h"
#include "gainblock.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

using namespace rb::dsp;
using rb::test::GainBlock;

namespace
{

/// A second type, so that listing and ordering are worth testing.
std::unique_ptr<Block> makeAnother()
{
    return GainBlock::make();
}

} // namespace

TEST(BlockRegistry, StartsEmpty)
{
    const BlockRegistry registry;
    EXPECT_TRUE(registry.isEmpty());
    EXPECT_EQ(registry.size(), 0u);
    EXPECT_TRUE(registry.typeIds().empty());
}

TEST(BlockRegistry, ListsWhatIsRegistered)
{
    BlockRegistry registry;
    ASSERT_TRUE(registry.add("effect.volume", &makeAnother).has_value());
    ASSERT_TRUE(registry.add("meter.peakrms", &makeAnother).has_value());
    ASSERT_TRUE(registry.add("source.application", &makeAnother).has_value());

    EXPECT_EQ(registry.size(), 3u);
    EXPECT_TRUE(registry.contains("meter.peakrms"));
    EXPECT_FALSE(registry.contains("effect.reverb"));
    // In id order, not registration order, so a listing is the same every run.
    EXPECT_EQ(registry.typeIds(), (std::vector<std::string>{"effect.volume", "meter.peakrms", "source.application"}));
}

TEST(BlockRegistry, MakesABlockOfTheAskedForType)
{
    BlockRegistry registry;
    ASSERT_TRUE(registry.add(std::string{GainBlock::kTypeId}, &GainBlock::make).has_value());

    const std::expected<std::unique_ptr<Block>, BlockError> block = registry.create(GainBlock::kTypeId);
    ASSERT_TRUE(block.has_value());
    ASSERT_NE(*block, nullptr);
    EXPECT_EQ((*block)->info().typeId, GainBlock::kTypeId);
}

TEST(BlockRegistry, AnUnknownIdReturnsAnErrorCode)
{
    BlockRegistry registry;
    ASSERT_TRUE(registry.add("effect.volume", &makeAnother).has_value());

    const std::expected<std::unique_ptr<Block>, BlockError> block = registry.create("effect.reverb");
    ASSERT_FALSE(block.has_value());
    EXPECT_EQ(block.error(), BlockError::UnknownType);
    EXPECT_EQ(registry.describe("effect.reverb").error(), BlockError::UnknownType);
}

TEST(BlockRegistry, RegisteringTheSameIdTwiceIsRefused)
{
    // The second registration would shadow the first without a word, and a
    // session file would then mean something different depending on the order
    // the code ran in.
    BlockRegistry registry;
    ASSERT_TRUE(registry.add("effect.volume", &makeAnother).has_value());
    EXPECT_EQ(registry.add("effect.volume", &makeAnother).error(), BlockError::DuplicateType);
    EXPECT_EQ(registry.size(), 1u);
}

TEST(BlockRegistry, RefusesAnEmptyIdOrNoFactory)
{
    BlockRegistry registry;
    EXPECT_EQ(registry.add("", &makeAnother).error(), BlockError::UnknownType);
    EXPECT_EQ(registry.add("effect.volume", nullptr).error(), BlockError::UnknownType);
    EXPECT_TRUE(registry.isEmpty());
}

TEST(BlockRegistry, DescribesATypeWithoutKeepingABlock)
{
    BlockRegistry registry;
    ASSERT_TRUE(registry.add(std::string{GainBlock::kTypeId}, &GainBlock::make).has_value());

    const std::expected<BlockInfo, BlockError> info = registry.describe(GainBlock::kTypeId);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->typeId, GainBlock::kTypeId);
    EXPECT_EQ(info->category, BlockCategory::Effects);
    EXPECT_EQ(info->face, FaceKind::Value);
    EXPECT_EQ(info->inputs.minCount, 2u);
    EXPECT_EQ(info->inputs.label(0), "in_FL");
    EXPECT_EQ(info->outputs.label(1), "out_FR");
    EXPECT_EQ(info->outputs.label(9), "") << "a port with no label reads as empty, not out of bounds";
}

TEST(BlockRegistry, EachCreatedBlockIsItsOwn)
{
    BlockRegistry registry;
    ASSERT_TRUE(registry.add(std::string{GainBlock::kTypeId}, &GainBlock::make).has_value());

    const auto first = registry.create(GainBlock::kTypeId);
    const auto second = registry.create(GainBlock::kTypeId);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_NE(first->get(), second->get());

    ASSERT_TRUE((*first)->setParam("gainDb", ParamValue::ofFloat(-12.0f)).has_value());
    EXPECT_FLOAT_EQ(*(*second)->param("gainDb").asFloat(), 0.0f) << "one block's state is not another's";
}

TEST(BlockCategory, NamesAreStable)
{
    // These travel over the facade and into the library sidebar's own table.
    EXPECT_EQ(blockCategoryName(BlockCategory::Sources), "sources");
    EXPECT_EQ(blockCategoryName(BlockCategory::Outputs), "outputs");
    EXPECT_EQ(blockCategoryName(BlockCategory::Effects), "effects");
    EXPECT_EQ(blockCategoryName(BlockCategory::Meters), "meters");
    EXPECT_EQ(blockCategoryName(BlockCategory::Advanced), "advanced");
    EXPECT_EQ(blockCategoryName(BlockCategory::Plugins), "plugins");
    EXPECT_EQ(blockCategoryName(BlockCategory::Hearing), "hearing");
}

TEST(FaceKind, NamesAreStable)
{
    EXPECT_EQ(faceKindName(FaceKind::Meter), "meter");
    EXPECT_EQ(faceKindName(FaceKind::Value), "value");
    EXPECT_EQ(faceKindName(FaceKind::Icon), "icon");
    EXPECT_EQ(faceKindName(FaceKind::Text), "text");
    EXPECT_EQ(faceKindName(FaceKind::Bars), "bars");
    EXPECT_EQ(faceKindName(FaceKind::Curve), "curve");
    EXPECT_EQ(faceKindName(FaceKind::Vu), "vu");
}

TEST(BlockError, NamesAreStable)
{
    EXPECT_EQ(blockErrorName(BlockError::UnknownType), "unknownType");
    EXPECT_EQ(blockErrorName(BlockError::ParamOutOfRange), "paramOutOfRange");
    EXPECT_EQ(blockErrorName(BlockError::ParamReadOnly), "paramReadOnly");
}
