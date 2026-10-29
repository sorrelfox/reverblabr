// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Edits as the facade carries them. ApplyEdit takes one of these and a patch
// carries one back, so an edit that does not survive the round trip is an
// edit the GUI and the engine disagree about.

#include "core/editcommand.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <gtest/gtest.h>

#include <variant>
#include <vector>

using namespace rb::core;
using namespace Qt::StringLiterals;

namespace
{

[[nodiscard]] BlockInstance sampleBlock()
{
    BlockInstance block;
    block.id = u"b7"_s;
    block.type = u"effect.volume"_s;
    block.nickname = u"Voice"_s;
    block.enabled = false;
    block.position = QPointF{12.5, -3.25};
    block.params = QJsonObject{{u"gainDb"_s, -6.0}, {u"muted"_s, true}};
    block.preset = u"Podcast"_s;
    return block;
}

[[nodiscard]] Wire sampleWire()
{
    Wire wire;
    wire.fromBlock = u"b1"_s;
    wire.fromPort = 1;
    wire.toBlock = u"b7"_s;
    wire.toPort = 0;
    return wire;
}

/// One of every command, so that adding an eleventh without teaching it JSON
/// fails here rather than at the facade.
[[nodiscard]] std::vector<EditCommand> everyCommand()
{
    AddBlock add;
    add.block = sampleBlock();
    add.index = 2;

    Connect connect;
    connect.wire = sampleWire();
    connect.index = 4;

    MoveBlock move;
    move.blockId = u"b7"_s;
    move.position = QPointF{100.0, 200.5};

    SetSessionSetting setting;
    setting.key = SessionSettingKey::SampleRate;
    setting.value = QJsonValue{48000};

    // **With both halves filled in**: a preset that only sets things would
    // not carry the list of ones to clear through the round trip, and that
    // list is what an undo of a preset is made of.
    ApplyPreset preset;
    preset.blockId = u"b7"_s;
    preset.presetId = u"gentleLeveling"_s;
    preset.params = QJsonObject{{u"ratio"_s, 2.0}, {u"detector"_s, u"rms"_s}};
    preset.clear = {u"makeup"_s};

    return {add,
            RemoveBlock{u"b7"_s},
            move,
            connect,
            Disconnect{sampleWire()},
            SetParam{u"b7"_s, u"gainDb"_s, QJsonValue{-3.5}},
            Rename{u"b7"_s, u"Announcer"_s},
            SetNotes{u"Two microphones.\nMixed down on the laptop."_s},
            SetEnabled{u"b7"_s, true},
            setting,
            preset};
}

} // namespace

TEST(EditCommandJson, EveryCommandSurvivesTheRoundTrip)
{
    for (const EditCommand &command : everyCommand()) {
        const QJsonObject json = editCommandToJson(command);
        EXPECT_EQ(json.value(u"op"_s).toString().toStdString(), std::string{editCommandName(command)});

        const std::expected<EditCommand, EditJsonError> read = editCommandFromJson(json);
        ASSERT_TRUE(read.has_value()) << editCommandName(command) << ": " << editJsonErrorName(read.error());
        EXPECT_EQ(*read, command) << editCommandName(command) << " came back different";
    }
}

TEST(EditCommandJson, TheNumberOfCommandsIsTheNumberThatCanBeCarried)
{
    // std::variant knows how many there are; if an eleventh is added and this
    // file is not taught about it, this fails rather than the facade
    // silently refusing an edit nobody thought to try.
    EXPECT_EQ(std::variant_size_v<EditCommand>, everyCommand().size());
}

TEST(EditCommandJson, AnEditWithNoOperationIsRefused)
{
    EXPECT_EQ(editCommandFromJson(QJsonObject{}).error(), EditJsonError::NoOperation);
    EXPECT_EQ(editCommandFromJson(QJsonObject{{u"op"_s, 7}}).error(), EditJsonError::NoOperation);
}

TEST(EditCommandJson, AnOperationThisBuildDoesNotKnowIsRefusedAsSuch)
{
    // Told apart from a malformed one on purpose: a client from a later
    // version asking for something newer is a different problem from a
    // client sending rubbish, and only one of them is a bug here.
    const std::expected<EditCommand, EditJsonError> read =
        editCommandFromJson(QJsonObject{{u"op"_s, u"teleportBlock"_s}});
    ASSERT_FALSE(read.has_value());
    EXPECT_EQ(read.error(), EditJsonError::UnknownOperation);
    EXPECT_EQ(editJsonErrorName(EditJsonError::UnknownOperation), "unknownOperation");
}

TEST(EditCommandJson, AKnownOperationWithTheWrongArgumentsIsRefused)
{
    const std::vector<QJsonObject> broken{
        {{u"op"_s, u"addBlock"_s}}, // no block
        {{u"op"_s, u"addBlock"_s}, {u"block"_s, QJsonObject{{u"id"_s, u"b1"_s}}}}, // no type
        {{u"op"_s, u"removeBlock"_s}}, // no id
        {{u"op"_s, u"moveBlock"_s}, {u"blockId"_s, u"b1"_s}}, // nowhere to move it to
        {{u"op"_s, u"connect"_s}, {u"wire"_s, QJsonObject{}}},
        {{u"op"_s, u"setParam"_s}, {u"blockId"_s, u"b1"_s}}, // no key, no value
        {{u"op"_s, u"setEnabled"_s}, {u"blockId"_s, u"b1"_s}}, // on or off?
        {{u"op"_s, u"setNotes"_s}}, // no note at all, which is not an empty one
        {{u"op"_s, u"setNotes"_s}, {u"notes"_s, 7}}, // nor is a number
        {{u"op"_s, u"setSessionSetting"_s}, {u"key"_s, u"colour"_s}, {u"value"_s, 1}},
    };
    for (const QJsonObject &json : broken) {
        const std::expected<EditCommand, EditJsonError> read = editCommandFromJson(json);
        ASSERT_FALSE(read.has_value()) << QJsonDocument(json).toJson(QJsonDocument::Compact).toStdString();
        EXPECT_EQ(read.error(), EditJsonError::MalformedArguments)
            << QJsonDocument(json).toJson(QJsonDocument::Compact).toStdString();
    }
}

TEST(EditCommandJson, AnEmptyNoteSurvivesTheRoundTrip)
{
    // Clearing the box is an edit, and one a round trip that treats empty
    // as missing would quietly drop on the way to the engine.
    const EditCommand cleared = SetNotes{QString{}};
    const std::expected<EditCommand, EditJsonError> read = editCommandFromJson(editCommandToJson(cleared));
    ASSERT_TRUE(read.has_value()) << editJsonErrorName(read.error());
    EXPECT_EQ(*read, cleared);
}

TEST(EditCommandJson, RenamingTheSessionCarriesAnEmptyBlockId)
{
    // An empty block id means the session itself, which is easy to lose in
    // a round trip that treats empty as missing.
    const EditCommand rename = Rename{QString{}, u"Podcast"_s};
    const std::expected<EditCommand, EditJsonError> read = editCommandFromJson(editCommandToJson(rename));
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(*read, rename);
}
