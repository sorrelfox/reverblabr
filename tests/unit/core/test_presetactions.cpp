// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What a preset holds and sets, where a block keeps a parameter out of
// presets: a hearing block's Ear says which of a pair the block is,
// and a preset made for one ear has to load into the block for the other.

#include "../dsp/earblock.h"
#include "core/editcommand.h"
#include "core/presetactions.h"
#include "core/session.h"
#include "dsp/blockregistry.h"

#include <QJsonObject>
#include <gtest/gtest.h>

#include <string>

using namespace rb;
using namespace rb::core;
using namespace Qt::StringLiterals;

namespace
{

[[nodiscard]] dsp::BlockRegistry registryWithAnEarBlock()
{
    dsp::BlockRegistry registry;
    EXPECT_TRUE(registry.add(std::string{test::OneEarBlock::kTypeId}, &test::OneEarBlock::make).has_value());
    return registry;
}

/// One block for the right ear, turned down.
[[nodiscard]] Session rightEarSession()
{
    Session session;
    BlockInstance block;
    block.id = u"r"_s;
    block.type = QString::fromUtf8(test::OneEarBlock::kTypeId);
    block.params = QJsonObject{{u"ear"_s, u"right"_s}, {u"gainDb"_s, -10.0}};
    session.blocks.append(block);
    return session;
}

} // namespace

TEST(PresetActions, APresetSavedFromOneEarDoesNotSayWhichEar)
{
    const dsp::BlockRegistry registry = registryWithAnEarBlock();
    const Session session = rightEarSession();

    const std::expected<Preset, PresetError> preset = presetOfBlock(registry, session, u"r"_s, u"My right ear"_s);
    ASSERT_TRUE(preset.has_value());
    EXPECT_DOUBLE_EQ(preset->params.value(u"gainDb"_s).toDouble(), -10.0) << "the setting itself was not saved";
    EXPECT_FALSE(preset->params.contains(u"ear"_s)) << "the preset would move the block it is loaded into";
}

TEST(PresetActions, LoadingAPresetKeepsTheBlocksEar)
{
    // **A preset that does name the ear** — saved before the rule, or
    // written by hand — still leaves the block on its own side.
    const dsp::BlockRegistry registry = registryWithAnEarBlock();
    Session session = rightEarSession();
    Preset preset;
    preset.id = u"Left"_s;
    preset.typeId = QString::fromUtf8(test::OneEarBlock::kTypeId);
    preset.params = QJsonObject{{u"ear"_s, u"left"_s}, {u"gainDb"_s, -3.0}};

    const std::expected<ApplyPreset, PresetError> edit = applyPresetEdit(registry, session, u"r"_s, preset);
    ASSERT_TRUE(edit.has_value());
    EXPECT_FALSE(edit->params.contains(u"ear"_s));
    ASSERT_TRUE(applyEdit(session, EditCommand{*edit}).has_value());

    const BlockInstance *block = session.findBlock(u"r"_s);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->params.value(u"ear"_s).toString(), u"right"_s) << "loading a preset moved the block to the left";
    EXPECT_DOUBLE_EQ(block->params.value(u"gainDb"_s).toDouble(), -3.0) << "the preset's setting was not applied";
}

TEST(PresetActions, APresetThatOnlyNamesTheEarIsNotAPreset)
{
    const dsp::BlockRegistry registry = registryWithAnEarBlock();
    const Session session = rightEarSession();
    Preset preset;
    preset.id = u"Just an ear"_s;
    preset.typeId = QString::fromUtf8(test::OneEarBlock::kTypeId);
    preset.params = QJsonObject{{u"ear"_s, u"left"_s}};

    const std::expected<ApplyPreset, PresetError> edit = applyPresetEdit(registry, session, u"r"_s, preset);
    ASSERT_FALSE(edit.has_value());
    EXPECT_EQ(edit.error(), PresetError::NotAPreset);
}
