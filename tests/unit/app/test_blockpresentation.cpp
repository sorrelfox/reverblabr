// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blockpresentation.h"
#include "blocks/builtinblocks.h"
#include "dsp/block.h"
#include "dsp/blockregistry.h"

#include <QFile>
#include <QSet>
#include <QXmlStreamReader>
#include <gtest/gtest.h>

using namespace Qt::StringLiterals;

namespace
{

/// The blocks this build actually has, as the engine registers them.
rb::dsp::BlockRegistry builtins()
{
    rb::dsp::BlockRegistry registry;
    EXPECT_TRUE(rb::blocks::registerBuiltinBlocks(registry));
    return registry;
}

/// An icon's file, read out of the QML module's resources the way the
/// interface reaches it.
QString iconFile(const QString &typeId)
{
    return rb::ui::blockPresentation::icon(typeId).toString().replace("qrc:"_L1, ":"_L1);
}

std::string join(const QStringList &items)
{
    return items.join(u", "_s).toStdString();
}

} // namespace

// The sidebar and the canvas are given type ids and have to show words. A
// block this build can make and this table has never heard of is a blank row.
TEST(BlockPresentation, EveryRegisteredTypeHasAnEntry)
{
    QStringList missing;
    for (const std::string &typeId : builtins().typeIds()) {
        if (!rb::ui::blockPresentation::has(QString::fromStdString(typeId))) {
            missing.append(QString::fromStdString(typeId));
        }
    }
    EXPECT_TRUE(missing.isEmpty()) << "no name, description or icon for: " << join(missing);
}

// The table says which category a block is in for the blocks that do not
// exist yet; for the ones that do, the engine says it. They must agree, or
// the library would file a block under one heading and tint it as another.
TEST(BlockPresentation, CategoriesAgreeWithTheEngine)
{
    const rb::dsp::BlockRegistry registry = builtins();
    for (const std::string &typeId : registry.typeIds()) {
        const auto made = registry.create(typeId);
        ASSERT_TRUE(made.has_value()) << typeId;
        const std::string_view category = rb::dsp::blockCategoryName((*made)->info().category);
        EXPECT_EQ(rb::ui::blockPresentation::category(QString::fromStdString(typeId)).toStdString(),
                  std::string(category))
            << typeId;
    }
}

// Nothing in the table may be empty, including the blocks no build has yet:
// they are in the library as soon as they exist, and the words are written
// with the block, not after somebody notices the gap.
TEST(BlockPresentation, EveryEntryIsComplete)
{
    const QStringList types = rb::ui::blockPresentation::types();
    ASSERT_FALSE(types.isEmpty());
    for (const QString &typeId : types) {
        EXPECT_FALSE(rb::ui::blockPresentation::name(typeId).isEmpty()) << typeId.toStdString();
        EXPECT_FALSE(rb::ui::blockPresentation::description(typeId).isEmpty()) << typeId.toStdString();
        EXPECT_FALSE(rb::ui::blockPresentation::tooltip(typeId).isEmpty()) << typeId.toStdString();
        EXPECT_FALSE(rb::ui::blockPresentation::category(typeId).isEmpty()) << typeId.toStdString();
        EXPECT_FALSE(rb::ui::blockPresentation::icon(typeId).isEmpty()) << typeId.toStdString();
    }
}

// Two blocks with the same name, or the same description, are two blocks
// nobody can tell apart in a list.
TEST(BlockPresentation, NamesAndDescriptionsAreDistinct)
{
    QSet<QString> names;
    QSet<QString> descriptions;
    for (const QString &typeId : rb::ui::blockPresentation::types()) {
        const QString name = rb::ui::blockPresentation::name(typeId);
        const QString description = rb::ui::blockPresentation::description(typeId);
        EXPECT_FALSE(names.contains(name)) << "two blocks called " << name.toStdString();
        EXPECT_FALSE(descriptions.contains(description)) << "two blocks described as " << description.toStdString();
        names.insert(name);
        descriptions.insert(description);
    }
}

// A description is one line in a sidebar row, in sentence case and with no
// full stop; the longer form belongs in the tooltip.
TEST(BlockPresentation, DescriptionsFitOneRow)
{
    for (const QString &typeId : rb::ui::blockPresentation::types()) {
        const QString description = rb::ui::blockPresentation::description(typeId);
        EXPECT_LE(description.size(), 72) << description.toStdString();
        EXPECT_FALSE(description.endsWith(u'.')) << description.toStdString();
        EXPECT_TRUE(rb::ui::blockPresentation::tooltip(typeId).size()
                    >= rb::ui::blockPresentation::description(typeId).size())
            << typeId.toStdString();
    }
}

// The library reads along the signal — where sound comes from, where it
// goes, what happens to it on the way — and a heading out of that order
// would send somebody looking in the wrong place for the block they want.
TEST(BlockPresentation, CategoriesAreInTheOrderTheLibraryReads)
{
    const QStringList expected{
        u"sources"_s, u"outputs"_s, u"effects"_s, u"meters"_s, u"advanced"_s, u"hearing"_s, u"plugins"_s};
    EXPECT_EQ(rb::ui::blockPresentation::categories(), expected);
}

// Every category a block is in has a heading to go under, and every heading
// has a category. One without the other is a block nobody can find.
TEST(BlockPresentation, EveryCategoryHasAHeadingAndEveryHeadingHasBlocks)
{
    const QStringList categories = rb::ui::blockPresentation::categories();
    QSet<QString> used;
    for (const QString &typeId : rb::ui::blockPresentation::types()) {
        const QString category = rb::ui::blockPresentation::category(typeId);
        EXPECT_TRUE(categories.contains(category)) << typeId.toStdString() << " is in " << category.toStdString();
        used.insert(category);
    }
    for (const QString &category : categories) {
        EXPECT_TRUE(used.contains(category)) << "no block is in " << category.toStdString();
    }
}

// A heading with no words is a blank line in the sidebar, and two headings
// with the same words are two headings nobody can tell apart.
TEST(BlockPresentation, EveryCategoryHasItsOwnName)
{
    QSet<QString> names;
    for (const QString &category : rb::ui::blockPresentation::categories()) {
        const QString name = rb::ui::blockPresentation::categoryName(category);
        EXPECT_FALSE(name.isEmpty()) << category.toStdString();
        EXPECT_NE(name, category) << category.toStdString() << " is shown as its own id";
        EXPECT_FALSE(names.contains(name)) << "two categories called " << name.toStdString();
        names.insert(name);
    }
    // A category from a later version is a heading with no words rather
    // than a heading reading "quantum", which is what a guess would give.
    EXPECT_TRUE(rb::ui::blockPresentation::categoryName(u"quantum"_s).isEmpty());
}

// The tint follows the category, so a category the palette has no tint for
// is a block drawn in the wrong colour. The six are the engine's.
TEST(BlockPresentation, EveryCategoryIsOneTheEngineKnows)
{
    const QStringList known{
        u"sources"_s, u"outputs"_s, u"effects"_s, u"meters"_s, u"advanced"_s, u"plugins"_s, u"hearing"_s};
    for (const QString &typeId : rb::ui::blockPresentation::types()) {
        EXPECT_TRUE(known.contains(rb::ui::blockPresentation::category(typeId)))
            << typeId.toStdString() << " is in " << rb::ui::blockPresentation::category(typeId).toStdString();
    }
}

// Every icon is really there, is really a drawing, and is drawn in the one
// colour the interface hands it: a tint baked into the file is a tint that
// is wrong the moment the colour scheme changes.
TEST(BlockPresentation, EveryIconLoadsAndIsAMask)
{
    for (const QString &typeId : rb::ui::blockPresentation::types()) {
        QFile file(iconFile(typeId));
        ASSERT_TRUE(file.open(QIODevice::ReadOnly)) << "missing icon for " << typeId.toStdString();
        const QByteArray svg = file.readAll();
        EXPECT_FALSE(svg.isEmpty()) << typeId.toStdString();

        QXmlStreamReader reader(svg);
        ASSERT_TRUE(reader.readNextStartElement()) << typeId.toStdString();
        EXPECT_EQ(reader.name().toString(), u"svg"_s) << typeId.toStdString();
        // Drawn on the 24-pixel grid the interface shows them on.
        EXPECT_EQ(reader.attributes().value(u"viewBox"_s).toString(), u"0 0 24 24"_s) << typeId.toStdString();
        EXPECT_EQ(reader.attributes().value(u"stroke"_s).toString(), u"currentColor"_s) << typeId.toStdString();

        EXPECT_FALSE(svg.contains("#")) << typeId.toStdString() << " has a colour of its own";
        while (!reader.atEnd()) {
            reader.readNext();
        }
        EXPECT_FALSE(reader.hasError()) << typeId.toStdString() << ": " << reader.errorString().toStdString();
    }
}

// A session written by a later version names blocks this one has never
// heard of. It still has to be readable.
TEST(BlockPresentation, AnUnknownTypeIsItsOwnName)
{
    const QString unknown = u"effect.somethingLater"_s;
    EXPECT_FALSE(rb::ui::blockPresentation::has(unknown));
    EXPECT_EQ(rb::ui::blockPresentation::name(unknown), unknown);
    EXPECT_TRUE(rb::ui::blockPresentation::description(unknown).isEmpty());
    EXPECT_TRUE(rb::ui::blockPresentation::icon(unknown).isEmpty());
}

// The QML singleton and the table are the same table.
TEST(BlockPresentation, QmlSeesTheSameWords)
{
    rb::ui::BlockPresentation presentation;
    const QString typeId = u"effect.volume"_s;
    const QVariantMap described = presentation.describe(typeId);
    EXPECT_EQ(described.value(u"name"_s).toString(), rb::ui::blockPresentation::name(typeId));
    EXPECT_EQ(described.value(u"description"_s).toString(), rb::ui::blockPresentation::description(typeId));
    EXPECT_EQ(described.value(u"tooltip"_s).toString(), rb::ui::blockPresentation::tooltip(typeId));
    EXPECT_EQ(described.value(u"category"_s).toString(), u"effects"_s);
    EXPECT_EQ(presentation.types(), rb::ui::blockPresentation::types());
    EXPECT_TRUE(presentation.describe(u"effect.somethingLater"_s).isEmpty());
}
