// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Simple Compressor's modes as the interface is handed them: the
// same ids the catalogue offers, and the numbers each one sets.

#include "blocks/builtinblocks.h"
#include "core/blockcatalogue.h"
#include "core/compressormodes.h"

#include <QJsonArray>
#include <QJsonObject>
#include <gtest/gtest.h>

using namespace rb;
using namespace rb::core;
using namespace Qt::StringLiterals;

TEST(CompressorModesTest, TheIdsAreTheOnesTheCatalogueOffers)
{
    dsp::BlockRegistry registry;
    ASSERT_TRUE(blocks::registerBuiltinBlocks(registry));
    const QJsonObject catalogue = blockCatalogueJson(registry);

    QJsonObject block;
    for (const QJsonValue &entry : catalogue.value("blocks"_L1).toArray()) {
        if (entry.toObject().value("type"_L1).toString() == CompressorModes::typeId()) {
            block = entry.toObject();
        }
    }
    ASSERT_FALSE(block.isEmpty()) << "the engine does not offer the Simple Compressor";
    EXPECT_EQ(block.value("face"_L1).toString(), u"text"_s);

    const QJsonObject mode = block.value("params"_L1).toArray().first().toObject();
    EXPECT_EQ(mode.value("id"_L1).toString(), u"mode"_s);
    QStringList choices;
    for (const QJsonValue &choice : mode.value("choices"_L1).toArray()) {
        choices.append(choice.toString());
    }
    EXPECT_EQ(choices, CompressorModes::ids());
    EXPECT_EQ(mode.value("default"_L1).toString(), CompressorModes::defaultId());
    EXPECT_EQ(CompressorModes::defaultId(), u"gentle"_s);
}

TEST(CompressorModesTest, AModeSaysEverythingItSets)
{
    const QVariantMap gentle = CompressorModes::settings(u"gentle"_s);
    EXPECT_DOUBLE_EQ(gentle.value(u"threshold"_s).toDouble(), -18.0);
    EXPECT_DOUBLE_EQ(gentle.value(u"ratio"_s).toDouble(), 2.0);
    EXPECT_DOUBLE_EQ(gentle.value(u"knee"_s).toDouble(), 12.0);
    EXPECT_DOUBLE_EQ(gentle.value(u"makeup"_s).toDouble(), 3.0);
    EXPECT_DOUBLE_EQ(gentle.value(u"attack"_s).toDouble(), 20.0);
    EXPECT_DOUBLE_EQ(gentle.value(u"release"_s).toDouble(), 250.0);
    EXPECT_EQ(gentle.value(u"detector"_s).toString(), u"rms"_s);

    EXPECT_EQ(CompressorModes::settings(u"broadcast"_s).value(u"detector"_s).toString(), u"peak"_s);
    for (const QString &id : CompressorModes::ids()) {
        EXPECT_EQ(CompressorModes::settings(id).size(), 7) << id.toStdString();
    }
    EXPECT_TRUE(CompressorModes::settings(u"loudest"_s).isEmpty());
}
