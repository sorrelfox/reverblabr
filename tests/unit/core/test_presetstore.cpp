// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Presets on disk and in the binary: what a preset file is, what the
// shipped ones say, and that saving, renaming and deleting come back to the
// same thing they started as.

#include "blocks/builtinblocks.h"
#include "core/paramjson.h"
#include "core/preset.h"
#include "core/presetstore.h"
#include "dsp/blockregistry.h"
#include "dsp/paramdescriptor.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QTemporaryDir>
#include <gtest/gtest.h>

using namespace rb;
using namespace rb::core;
using namespace Qt::StringLiterals;

namespace
{

[[nodiscard]] Preset volumePreset(const QString &name, double gain)
{
    Preset preset;
    preset.typeId = u"effect.volume"_s;
    preset.name = name;
    preset.params = QJsonObject{{u"gain"_s, gain}};
    return preset;
}

class Presets : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(directory.isValid());
        store = std::make_unique<PresetStore>(directory.path());
    }

    QTemporaryDir directory;
    std::unique_ptr<PresetStore> store;
};

} // namespace

TEST_F(Presets, APresetIsATypeAndSomeValuesAndNothingElse)
{
    // **Not a block.** No id, no position, no nickname, no wires — which is
    // what lets one preset apply to every Volume block in every session.
    const Preset preset = volumePreset(u"Loud"_s, 2.0);
    const QJsonObject json = presetToJson(preset);
    EXPECT_FALSE(json.contains(u"id"_s));
    EXPECT_EQ(json.value(u"type"_s).toString(), u"effect.volume"_s);
    EXPECT_EQ(json.value(u"schema"_s).toInt(), kPresetSchemaVersion);

    const std::expected<Preset, PresetError> back = presetFromJson(json);
    ASSERT_TRUE(back.has_value()) << presetErrorName(back.error());
    EXPECT_EQ(back->typeId, preset.typeId);
    EXPECT_EQ(back->name, preset.name);
    EXPECT_EQ(back->params, preset.params);
}

TEST_F(Presets, SaveApplyRenameAndDeleteComeBackToWhereTheyStarted)
{
    // The criterion: the round trip.
    const std::expected<Preset, PresetError> saved = store->save(volumePreset(u"Loud"_s, 2.0));
    ASSERT_TRUE(saved.has_value()) << presetErrorName(saved.error());
    EXPECT_EQ(saved->id, u"Loud"_s);
    EXPECT_FALSE(saved->factory);

    const std::expected<Preset, PresetError> read = store->preset(u"effect.volume"_s, u"Loud"_s);
    ASSERT_TRUE(read.has_value()) << presetErrorName(read.error());
    EXPECT_EQ(read->params.value(u"gain"_s).toDouble(), 2.0);

    const std::expected<Preset, PresetError> renamed = store->rename(u"effect.volume"_s, u"Loud"_s, u"Louder"_s);
    ASSERT_TRUE(renamed.has_value()) << presetErrorName(renamed.error());
    EXPECT_EQ(renamed->name, u"Louder"_s);
    EXPECT_FALSE(store->preset(u"effect.volume"_s, u"Loud"_s).has_value()) << "the old name still answers";
    EXPECT_TRUE(store->preset(u"effect.volume"_s, u"Louder"_s).has_value());
    EXPECT_FALSE(QFile::exists(store->pathFor(u"effect.volume"_s, u"Loud"_s))) << "the old file was left behind";

    EXPECT_TRUE(store->remove(u"effect.volume"_s, u"Louder"_s).has_value());
    EXPECT_FALSE(store->preset(u"effect.volume"_s, u"Louder"_s).has_value());
}

TEST_F(Presets, AFactoryPresetCannotBeSavedOverRenamedOrDeleted)
{
    // The criterion: **read-only, and not by a file permission**. Saving
    // over a shipped preset would leave somebody with no way back to what
    // the block was tuned to.
    const QList<Preset> shipped = PresetStore::factoryPresetsFor(u"effect.volume"_s);
    ASSERT_FALSE(shipped.isEmpty()) << "no factory presets are shipping at all";
    const QString id = shipped.constFirst().id;

    EXPECT_EQ(store->rename(u"effect.volume"_s, id, u"Mine"_s).error(), PresetError::ReadOnly);
    EXPECT_EQ(store->remove(u"effect.volume"_s, id).error(), PresetError::ReadOnly);

    // **And saving under a factory preset's name does not touch it.** The
    // person's own is listed beside the shipped one, which is still there.
    Preset mine = volumePreset(id, 3.0);
    ASSERT_TRUE(store->save(mine).has_value());
    const QList<Preset> both = store->presetsFor(u"effect.volume"_s);
    const auto factoryStill =
        std::ranges::find_if(both, [&id](const Preset &preset) { return preset.id == id && preset.factory; });
    ASSERT_NE(factoryStill, both.end()) << "the shipped preset was written over";
    EXPECT_NE(factoryStill->params.value(u"gain"_s).toDouble(), 3.0);
}

TEST_F(Presets, TheShippedOnesComeFirstAndInTheOrderTheyShip)
{
    ASSERT_TRUE(store->save(volumePreset(u"Aardvark"_s, 0.1)).has_value());
    ASSERT_TRUE(store->save(volumePreset(u"Zebra"_s, 0.2)).has_value());

    const QList<Preset> presets = store->presetsFor(u"effect.volume"_s);
    const QList<Preset> shipped = PresetStore::factoryPresetsFor(u"effect.volume"_s);
    ASSERT_EQ(presets.size(), shipped.size() + 2);

    for (qsizetype at = 0; at < shipped.size(); ++at) {
        EXPECT_TRUE(presets.at(at).factory) << "a shipped preset was not among the first";
        EXPECT_EQ(presets.at(at).id, shipped.at(at).id) << "the shipping order was not kept";
    }
    // **A factory list is a sequence somebody chose**, so it is not sorted
    // in with the person's own — theirs follow, in name order.
    EXPECT_EQ(presets.at(shipped.size()).name, u"Aardvark"_s);
    EXPECT_EQ(presets.at(shipped.size() + 1).name, u"Zebra"_s);
}

TEST_F(Presets, ANameThatCannotBeAFileNameIsRefusedBeforeAnythingIsWritten)
{
    // **A slash is the dangerous one**: "../../thing" would be written
    // outside the presets directory altogether.
    for (const QString &bad : {u"../escape"_s, u"a/b"_s, u""_s, u"   "_s, u"."_s, u".."_s, u"a\\b"_s, u"a:b"_s}) {
        const std::expected<Preset, PresetError> saved = store->save(volumePreset(bad, 1.0));
        ASSERT_FALSE(saved.has_value()) << "accepted the name " << bad.toStdString();
        EXPECT_EQ(saved.error(), PresetError::BadName) << bad.toStdString();
    }
    EXPECT_TRUE(store->presetsFor(u"effect.volume"_s).size()
                == PresetStore::factoryPresetsFor(u"effect.volume"_s).size())
        << "something was written despite the name";
}

TEST_F(Presets, SavingOntoOneIsRefusedUnlessTheCallerSaysToReplaceIt)
{
    ASSERT_TRUE(store->save(volumePreset(u"Loud"_s, 2.0)).has_value());

    const std::expected<Preset, PresetError> again = store->save(volumePreset(u"Loud"_s, 3.0));
    ASSERT_FALSE(again.has_value());
    EXPECT_EQ(again.error(), PresetError::AlreadyExists) << "so the interface can ask first";
    EXPECT_EQ(store->preset(u"effect.volume"_s, u"Loud"_s)->params.value(u"gain"_s).toDouble(), 2.0);

    ASSERT_TRUE(store->save(volumePreset(u"Loud"_s, 3.0), true).has_value());
    EXPECT_EQ(store->preset(u"effect.volume"_s, u"Loud"_s)->params.value(u"gain"_s).toDouble(), 3.0);
}

TEST_F(Presets, RenamingOntoAnotherIsRefusedAndLeavesBothWhereTheyWere)
{
    ASSERT_TRUE(store->save(volumePreset(u"One"_s, 1.0)).has_value());
    ASSERT_TRUE(store->save(volumePreset(u"Two"_s, 2.0)).has_value());

    EXPECT_EQ(store->rename(u"effect.volume"_s, u"One"_s, u"Two"_s).error(), PresetError::AlreadyExists);
    EXPECT_EQ(store->preset(u"effect.volume"_s, u"One"_s)->params.value(u"gain"_s).toDouble(), 1.0);
    EXPECT_EQ(store->preset(u"effect.volume"_s, u"Two"_s)->params.value(u"gain"_s).toDouble(), 2.0);
}

TEST_F(Presets, APresetFileIsTheSameBytesInAnyLocale)
{
    // The criterion: **valid under de_DE**, where a decimal separator is a
    // comma. The values are JSON numbers rather than the text a panel shows,
    // so a preset saved on one desktop opens on another.
    const QLocale was = QLocale();
    QLocale::setDefault(QLocale(u"de_DE"_s));
    const std::expected<Preset, PresetError> saved = store->save(volumePreset(u"Halb"_s, 0.5));
    QLocale::setDefault(was);
    ASSERT_TRUE(saved.has_value()) << presetErrorName(saved.error());

    QFile file(store->pathFor(u"effect.volume"_s, u"Halb"_s));
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    EXPECT_TRUE(bytes.contains("0.5")) << "a decimal comma reached the file: " << bytes.toStdString();
    EXPECT_FALSE(bytes.contains("0,5")) << bytes.toStdString();

    // And it reads back as the number it was, wherever it is opened.
    QLocale::setDefault(QLocale(u"de_DE"_s));
    const std::expected<Preset, PresetError> read = store->preset(u"effect.volume"_s, u"Halb"_s);
    QLocale::setDefault(was);
    ASSERT_TRUE(read.has_value());
    EXPECT_DOUBLE_EQ(read->params.value(u"gain"_s).toDouble(), 0.5);
}

TEST_F(Presets, EveryShippedPresetNamesParametersItsBlockActuallyHas)
{
    // **The one thing that cannot be checked by reading either side.** A
    // preset is JSON written by hand; a parameter renamed in the block would
    // leave it setting something that no longer exists, silently.
    dsp::BlockRegistry registry;
    ASSERT_TRUE(blocks::registerBuiltinBlocks(registry));

    int shippedCount = 0;
    for (const std::string &typeId : registry.typeIds()) {
        const QString type = QString::fromStdString(typeId);
        const QList<Preset> presets = PresetStore::factoryPresetsFor(type);
        const std::expected<std::unique_ptr<dsp::Block>, dsp::BlockError> block = registry.create(typeId);
        ASSERT_TRUE(block.has_value()) << typeId;

        for (const Preset &preset : presets) {
            ++shippedCount;
            EXPECT_EQ(preset.typeId, type);
            EXPECT_TRUE(preset.factory);
            EXPECT_TRUE(preset.name.isEmpty()) << "a factory preset carries words the catalogue should hold";
            EXPECT_FALSE(preset.id.isEmpty());
            EXPECT_FALSE(preset.params.isEmpty()) << preset.id.toStdString();

            for (auto value = preset.params.constBegin(); value != preset.params.constEnd(); ++value) {
                const auto named = std::ranges::find_if((*block)->params(), [&value](const dsp::ParamDescriptor &d) {
                    return QString::fromStdString(d.id) == value.key();
                });
                EXPECT_NE(named, (*block)->params().end())
                    << typeId << "/" << preset.id.toStdString() << " sets '" << value.key().toStdString()
                    << "', which that block does not have";
                if (named == (*block)->params().end()) {
                    continue;
                }
                // **And to something that block would take.** A value out
                // of range or of the wrong type is dropped when the preset
                // is applied, and the preset quietly is not what it says.
                const std::optional<dsp::ParamValue> read = valueFromJson(*named, *value);
                EXPECT_TRUE(read.has_value() && dsp::isValueAllowed(*named, *read))
                    << typeId << "/" << preset.id.toStdString() << " sets '" << value.key().toStdString()
                    << "' to a value that block would refuse";
            }
        }
    }
    EXPECT_EQ(shippedCount, 39) << "the shipped presets are three for Volume and five for Dynamics, "
                                   "twenty for the 10-Band EQ, two for Dehum, three for "
                                   "Hearing Compensation 16, three for Hearing Compensation 64 "
                                   " and three for Reverb";
}

TEST_F(Presets, APresetMatchesTheValuesItNamesAndNoOthers)
{
    // **What makes the menu say "Manual".** A block whose values are a
    // preset's is showing it whether or not anybody clicked it.
    const Preset preset = volumePreset(u"Loud"_s, 2.0);
    EXPECT_TRUE(presetMatches(preset, QJsonObject{{u"gain"_s, 2.0}}));
    // Parameters it does not mention are not compared: applying it would not
    // have touched them.
    EXPECT_TRUE(presetMatches(preset, QJsonObject{{u"gain"_s, 2.0}, {u"muted"_s, true}}));
    EXPECT_FALSE(presetMatches(preset, QJsonObject{{u"gain"_s, 1.0}}));
    EXPECT_FALSE(presetMatches(preset, QJsonObject{})) << "a block with nothing set is not showing a preset";

    // A preset that says nothing matches nothing, rather than everything.
    Preset empty = preset;
    empty.params = QJsonObject{};
    EXPECT_FALSE(presetMatches(empty, QJsonObject{{u"gain"_s, 2.0}}));
}

TEST_F(Presets, AFileThatIsNotAPresetIsNotOfferedAndDoesNotStopTheRest)
{
    ASSERT_TRUE(store->save(volumePreset(u"Good"_s, 1.0)).has_value());

    const QString rubbish = store->pathFor(u"effect.volume"_s, u"Rubbish"_s);
    {
        QFile file(rubbish);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        ASSERT_GT(file.write("this is not a preset\n"), 0);
    }
    // And one that is JSON, but for another kind of block.
    Preset otherType = volumePreset(u"Wrong"_s, 1.0);
    otherType.typeId = u"effect.dynamics"_s;
    const QString wrong = store->pathFor(u"effect.volume"_s, u"Wrong"_s);
    {
        QFile file(wrong);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        ASSERT_GT(file.write(QJsonDocument(presetToJson(otherType)).toJson()), 0);
    }

    const QList<Preset> presets = store->presetsFor(u"effect.volume"_s);
    const auto named = [&presets](const QString &name) {
        return std::ranges::any_of(presets, [&name](const Preset &preset) { return preset.name == name; });
    };
    EXPECT_TRUE(named(u"Good"_s)) << "one bad file hid the good ones";
    EXPECT_FALSE(named(u"Rubbish"_s));
    EXPECT_FALSE(named(u"Wrong"_s)) << "a preset for another block was offered for this one";
}
