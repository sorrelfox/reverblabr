// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/editcommand.h"
#include "core/session.h"
#include "sessionfixtures.h"

#include <QJsonObject>
#include <QJsonValue>
#include <gtest/gtest.h>

#include <vector>

using namespace rb::core;
using rb::test::fullSession;

namespace
{

/// Applies \a command, then applies its inverse, and requires the session to
/// be exactly what it was — every field of every block and wire, and the
/// settings.
///
/// This is the property undo is built on, so it is checked for each command
/// rather than for one of them.
void expectInvertible(const EditCommand &command)
{
    Session session = fullSession();
    const Session before = session;

    const std::expected<EditInverse, EditError> inverse = applyEdit(session, command);
    ASSERT_TRUE(inverse.has_value()) << "the command itself was refused: " << editErrorName(inverse.error());
    ASSERT_NE(session, before) << "a command that changes nothing proves nothing";

    const std::expected<EditInverse, EditBatchError> undone = applyEdits(session, *inverse);
    ASSERT_TRUE(undone.has_value()) << "the inverse was refused";

    // The revision counts every edit, the undo included, so it is the one
    // field that is meant to differ.
    session.revision = before.revision;
    EXPECT_EQ(session, before) << "applying " << editCommandName(command) << " and then its inverse changed it";
}

BlockInstance newBlock(const QString &id = QStringLiteral("b4"))
{
    BlockInstance block;
    block.id = id;
    block.type = QStringLiteral("effect.volume");
    block.position = QPointF{600.0, 240.0};
    block.params = QJsonObject{{QStringLiteral("gainDb"), -6.0}};
    return block;
}

} // namespace

TEST(EditCommand, EveryCommandIsExactlyInvertible)
{
    expectInvertible(AddBlock{newBlock()});
    expectInvertible(RemoveBlock{QStringLiteral("b2")});
    expectInvertible(MoveBlock{QStringLiteral("b1"), QPointF{12.5, 34.75}});
    expectInvertible(Connect{Wire{QStringLiteral("b1"), 0, QStringLiteral("b3"), 1}});
    expectInvertible(Disconnect{Wire{QStringLiteral("b1"), 0, QStringLiteral("b2"), 0}});
    expectInvertible(SetParam{QStringLiteral("b3"), QStringLiteral("device"), QJsonValue{QStringLiteral("usb")}});
    expectInvertible(SetParam{QStringLiteral("b2"), QStringLiteral("brandNew"), QJsonValue{7.0}});
    expectInvertible(Rename{QString{}, QStringLiteral("Another name")});
    expectInvertible(Rename{QStringLiteral("b1"), QStringLiteral("The browser")});
    expectInvertible(SetNotes{QStringLiteral("Mixed down on the laptop.")});
    expectInvertible(SetEnabled{QStringLiteral("b1"), false});
    expectInvertible(SetSessionSetting{SessionSettingKey::AutoConnect, QJsonValue{true}});
    expectInvertible(SetSessionSetting{SessionSettingKey::SampleRate, QJsonValue{QStringLiteral("auto")}});
    expectInvertible(SetSessionSetting{SessionSettingKey::Latency, QJsonValue{QStringLiteral("high")}});
    expectInvertible(SetSessionSetting{SessionSettingKey::AutoRun, QJsonValue{QStringLiteral("off")}});
}

TEST(EditCommand, RemovingABlockTakesItsWiresAndUndoingPutsThemBack)
{
    Session session = fullSession();
    const Session before = session;
    ASSERT_EQ(session.wires.size(), 3);

    const std::expected<EditInverse, EditError> inverse = applyEdit(session, RemoveBlock{QStringLiteral("b2")});
    ASSERT_TRUE(inverse.has_value());
    EXPECT_EQ(session.blocks.size(), 2);
    EXPECT_TRUE(session.wires.isEmpty()) << "every wire touched b2";
    EXPECT_EQ(inverse->size(), 4u) << "the block, then the three wires";

    ASSERT_TRUE(applyEdits(session, *inverse).has_value());
    session.revision = before.revision;
    EXPECT_EQ(session, before);
}

TEST(EditCommand, EachAppliedEditCountsTheRevisionUp)
{
    Session session = fullSession();
    EXPECT_EQ(session.revision, 0u);
    ASSERT_TRUE(applyEdit(session, MoveBlock{QStringLiteral("b1"), QPointF{1.0, 1.0}}).has_value());
    EXPECT_EQ(session.revision, 1u);
    ASSERT_TRUE(applyEdit(session, SetEnabled{QStringLiteral("b1"), false}).has_value());
    EXPECT_EQ(session.revision, 2u);

    static_cast<void>(applyEdit(session, MoveBlock{QStringLiteral("b9"), QPointF{}}));
    EXPECT_EQ(session.revision, 2u) << "a refused edit is not a change";
}

TEST(EditCommand, ARefusedEditLeavesTheSessionExactlyAsItWas)
{
    Session session = fullSession();
    const Session before = session;

    EXPECT_EQ(applyEdit(session, AddBlock{newBlock(QStringLiteral("b1"))}).error(), EditError::BlockIdInUse);
    EXPECT_EQ(applyEdit(session, AddBlock{newBlock(QString{})}).error(), EditError::BlockIdEmpty);
    EXPECT_EQ(applyEdit(session, RemoveBlock{QStringLiteral("b9")}).error(), EditError::BlockNotFound);
    EXPECT_EQ(applyEdit(session, MoveBlock{QStringLiteral("b9"), QPointF{}}).error(), EditError::BlockNotFound);
    EXPECT_EQ(applyEdit(session, SetEnabled{QStringLiteral("b9"), false}).error(), EditError::BlockNotFound);
    EXPECT_EQ(applyEdit(session, Rename{QStringLiteral("b9"), QStringLiteral("x")}).error(), EditError::BlockNotFound);
    EXPECT_EQ(applyEdit(session, SetParam{QStringLiteral("b1"), QString{}, QJsonValue{1.0}}).error(),
              EditError::ParamKeyEmpty);
    EXPECT_EQ(session, before);
}

TEST(EditCommand, ABlockWithNoTypeIsRefused)
{
    Session session;
    BlockInstance block;
    block.id = QStringLiteral("b1");
    EXPECT_EQ(applyEdit(session, AddBlock{block}).error(), EditError::BlockTypeEmpty);
    EXPECT_TRUE(session.blocks.isEmpty());
}

TEST(EditCommand, BadWiresAreRefused)
{
    Session session = fullSession();
    const Session before = session;

    EXPECT_EQ(applyEdit(session, Connect{Wire{QStringLiteral("b1"), 0, QStringLiteral("b9"), 0}}).error(),
              EditError::WireBlockNotFound);
    EXPECT_EQ(applyEdit(session, Connect{Wire{QStringLiteral("b1"), 0, QStringLiteral("b1"), 1}}).error(),
              EditError::WireToItself);
    EXPECT_EQ(applyEdit(session, Connect{Wire{QStringLiteral("b1"), 0, QStringLiteral("b2"), 0}}).error(),
              EditError::WireAlreadyExists);
    EXPECT_EQ(applyEdit(session, Disconnect{Wire{QStringLiteral("b3"), 0, QStringLiteral("b1"), 0}}).error(),
              EditError::WireNotFound);
    EXPECT_EQ(session, before);
}

TEST(EditCommand, AWireThatWouldCloseALoopIsRefused)
{
    // b1 -> b2 -> b3 already. Anything from further down the chain back to
    // something above it is a loop the graph compiler could not schedule,
    // and refusing it here is what stops somebody finding that out when
    // they press Start with the wire already drawn.
    Session session = fullSession();
    const Session before = session;

    EXPECT_EQ(applyEdit(session, Connect{Wire{QStringLiteral("b3"), 0, QStringLiteral("b1"), 0}}).error(),
              EditError::WireWouldCycle);
    EXPECT_EQ(applyEdit(session, Connect{Wire{QStringLiteral("b2"), 1, QStringLiteral("b1"), 0}}).error(),
              EditError::WireWouldCycle);
    EXPECT_EQ(session, before) << "a refused edit leaves the session exactly as it was";

    // The same two blocks the way the signal is already going is a second
    // wire between them, not a loop.
    EXPECT_TRUE(applyEdit(session, Connect{Wire{QStringLiteral("b1"), 1, QStringLiteral("b3"), 1}}).has_value());
}

TEST(EditCommand, TheRefusalIsTheSameWhetherItIsAskedOrApplied)
{
    // **What lets the interface grey out a port it would be refused on.**
    // If these two ever disagreed, a port would light up and then do
    // nothing, which is worse than one that never lit up.
    Session session = fullSession();
    const std::vector<Wire> wires{
        Wire{QStringLiteral("b1"), 0, QStringLiteral("b9"), 0},
        Wire{QStringLiteral("b1"), 0, QStringLiteral("b1"), 1},
        Wire{QStringLiteral("b1"), 0, QStringLiteral("b2"), 0},
        Wire{QStringLiteral("b3"), 0, QStringLiteral("b1"), 0},
        Wire{QStringLiteral("b1"), 1, QStringLiteral("b3"), 1},
    };
    for (const Wire &wire : wires) {
        Session copy = session;
        const std::optional<EditError> asked = connectRefusal(copy, wire);
        const std::expected<EditInverse, EditError> applied = applyEdit(copy, Connect{wire});
        EXPECT_EQ(asked.has_value(), !applied.has_value())
            << "asked and applied disagreed about " << wire.fromBlock.toStdString();
        if (asked) {
            EXPECT_EQ(*asked, applied.error());
        }
    }
}

TEST(EditCommand, TheSamePortsInTheOtherDirectionAreADifferentWire)
{
    Session session = fullSession();
    EXPECT_TRUE(applyEdit(session, Connect{Wire{QStringLiteral("b2"), 1, QStringLiteral("b3"), 1}}).has_value());
    EXPECT_EQ(session.wires.size(), 4);
}

TEST(EditCommand, ASettingOfTheWrongTypeOrValueIsRefused)
{
    Session session = fullSession();
    const SessionSettings before = session.settings;

    EXPECT_EQ(applyEdit(session, SetSessionSetting{SessionSettingKey::AutoConnect, QJsonValue{QStringLiteral("yes")}})
                  .error(),
              EditError::SettingTypeMismatch);
    EXPECT_EQ(applyEdit(session, SetSessionSetting{SessionSettingKey::SampleRate, QJsonValue{48000.0}}).error(),
              EditError::SettingTypeMismatch);
    EXPECT_EQ(applyEdit(session, SetSessionSetting{SessionSettingKey::SampleRate, QJsonValue{QStringLiteral("fast")}})
                  .error(),
              EditError::SettingValueNotAllowed);
    EXPECT_EQ(
        applyEdit(session, SetSessionSetting{SessionSettingKey::Latency, QJsonValue{QStringLiteral("quick")}}).error(),
        EditError::SettingValueNotAllowed);
    EXPECT_EQ(applyEdit(session, SetSessionSetting{SessionSettingKey::AutoRun, QJsonValue{true}}).error(),
              EditError::SettingTypeMismatch);
    EXPECT_EQ(
        applyEdit(session, SetSessionSetting{SessionSettingKey::AutoRun, QJsonValue{QStringLiteral("boot")}}).error(),
        EditError::SettingValueNotAllowed)
        << "only off and login: a value this version cannot act on must not be saved";
    EXPECT_EQ(session.settings, before);
}

TEST(EditCommand, SampleRateTakesAutoOrANumberWrittenOut)
{
    Session session;
    EXPECT_TRUE(
        applyEdit(session, SetSessionSetting{SessionSettingKey::SampleRate, QJsonValue{QStringLiteral("44100")}})
            .has_value());
    EXPECT_EQ(session.settings.sampleRate, QStringLiteral("44100"));
    EXPECT_TRUE(applyEdit(session, SetSessionSetting{SessionSettingKey::SampleRate, QJsonValue{QStringLiteral("auto")}})
                    .has_value());
    EXPECT_EQ(session.settings.sampleRate, QStringLiteral("auto"));
}

TEST(EditCommand, RenamingWithNoBlockRenamesTheSession)
{
    Session session = fullSession();
    ASSERT_TRUE(applyEdit(session, Rename{QString{}, QStringLiteral("Stream")}).has_value());
    EXPECT_EQ(session.name, QStringLiteral("Stream"));
    EXPECT_EQ(session.blocks[0].nickname, QStringLiteral("Browser")) << "no block was touched";
}

TEST(EditCommand, TheEngineRefusesNothingAboutTheNotes)
{
    // What session notes have to allow: any text is a valid note. Emptying
    // the box is an edit like any other, and undoing it brings back what was
    // there.
    Session session = fullSession();
    const QString before = session.notes;
    ASSERT_FALSE(before.isEmpty()) << "the fixture should start with something written in it";

    const std::expected<EditInverse, EditError> cleared = applyEdit(session, SetNotes{QString{}});
    ASSERT_TRUE(cleared.has_value()) << editErrorName(cleared.error());
    EXPECT_TRUE(session.notes.isEmpty());

    // Nothing anybody can type is refused either: newlines, punctuation and
    // a script this build has never seen.
    const QString awkward = QStringLiteral("line\n\ttab \"quoted\" — Ünïcödé — 日本語 — \\backslash\\");
    ASSERT_TRUE(applyEdit(session, SetNotes{awkward}).has_value());
    EXPECT_EQ(session.notes, awkward);

    ASSERT_TRUE(applyEdits(session, *cleared).has_value());
    EXPECT_EQ(session.notes, before);
}

TEST(EditCommand, SettingAParameterThatWasNotThereIsUndoneByRemovingIt)
{
    Session session = fullSession();
    const std::expected<EditInverse, EditError> inverse =
        applyEdit(session, SetParam{QStringLiteral("b2"), QStringLiteral("windowMs"), QJsonValue{300.0}});
    ASSERT_TRUE(inverse.has_value());
    EXPECT_TRUE(session.findBlock(QStringLiteral("b2"))->params.contains(QStringLiteral("windowMs")));

    ASSERT_TRUE(applyEdits(session, *inverse).has_value());
    EXPECT_FALSE(session.findBlock(QStringLiteral("b2"))->params.contains(QStringLiteral("windowMs")))
        << "undoing has to leave the parameter absent, not present and null";
}

TEST(EditCommand, ApplyingAPresetIsOneStepToTakeBack)
{
    // What presets need from undo. **A preset that moves nine knobs must not
    // be nine steps to undo**: somebody who tried one and did not like it
    // presses undo once, as they would for anything else they did once.
    Session session = fullSession();
    BlockInstance *block = session.findBlock(QStringLiteral("b2"));
    ASSERT_NE(block, nullptr);
    block->params = QJsonObject{{QStringLiteral("ratio"), 4.0}, {QStringLiteral("makeup"), 6.0}};
    const Session before = session;

    ApplyPreset preset;
    preset.blockId = QStringLiteral("b2");
    preset.presetId = QStringLiteral("gentleLeveling");
    // One value changed, one that was never set, and one taken off.
    preset.params = QJsonObject{{QStringLiteral("ratio"), 2.0}, {QStringLiteral("attack"), 20.0}};
    preset.clear = {QStringLiteral("makeup")};

    const std::expected<EditInverse, EditError> inverse = applyEdit(session, preset);
    ASSERT_TRUE(inverse.has_value());
    // **Asked for again rather than held across the copy.** `before` shares
    // the block list until an edit detaches it, and the pointer taken
    // beforehand then points into what `before` kept.
    const BlockInstance *after = session.findBlock(QStringLiteral("b2"));
    ASSERT_NE(after, nullptr);
    EXPECT_EQ(after->params.value(QStringLiteral("ratio")).toDouble(), 2.0);
    EXPECT_EQ(after->params.value(QStringLiteral("attack")).toDouble(), 20.0);
    EXPECT_FALSE(after->params.contains(QStringLiteral("makeup"))) << "a cleared parameter is gone, not null";
    EXPECT_EQ(after->preset, QStringLiteral("gentleLeveling")) << "the block does not say what it is showing";

    // **One inverse**, and the block is exactly as it was — the parameter
    // that was never set is absent again rather than present and null.
    EXPECT_EQ(inverse->size(), 1U) << "undoing a preset takes more than one step";
    ASSERT_TRUE(applyEdits(session, *inverse).has_value());
    session.revision = before.revision;
    EXPECT_EQ(session, before);
}

TEST(EditCommand, APresetLeavesAloneWhatItDoesNotMention)
{
    // What a preset does not name, applying it did not touch — so undoing it
    // must not touch it either, and a block can grow a parameter without
    // every preset for it becoming wrong.
    Session session = fullSession();
    BlockInstance *block = session.findBlock(QStringLiteral("b2"));
    block->params = QJsonObject{{QStringLiteral("ratio"), 4.0}, {QStringLiteral("somethingElse"), 7.0}};

    ApplyPreset preset;
    preset.blockId = QStringLiteral("b2");
    preset.params = QJsonObject{{QStringLiteral("ratio"), 2.0}};
    const std::expected<EditInverse, EditError> inverse = applyEdit(session, preset);
    ASSERT_TRUE(inverse.has_value());
    EXPECT_EQ(session.findBlock(QStringLiteral("b2"))->params.value(QStringLiteral("somethingElse")).toDouble(), 7.0);

    ASSERT_TRUE(applyEdits(session, *inverse).has_value());
    const BlockInstance *after = session.findBlock(QStringLiteral("b2"));
    EXPECT_EQ(after->params.value(QStringLiteral("somethingElse")).toDouble(), 7.0);
    EXPECT_EQ(after->params.value(QStringLiteral("ratio")).toDouble(), 4.0);
}

TEST(EditCommand, ClearingThePresetMarkerIsHowABlockGoesBackToManual)
{
    // **The marker is not the values.** A block showing nothing in
    // particular is what the panel calls "Manual", and it is reached by
    // applying a preset with no id — which is also what an undo of the
    // first preset somebody applied carries.
    Session session = fullSession();
    BlockInstance *block = session.findBlock(QStringLiteral("b2"));
    block->preset = QStringLiteral("voice");

    ApplyPreset toManual;
    toManual.blockId = QStringLiteral("b2");
    toManual.params = QJsonObject{{QStringLiteral("ratio"), 3.0}};
    ASSERT_TRUE(applyEdit(session, toManual).has_value());
    EXPECT_TRUE(session.findBlock(QStringLiteral("b2"))->preset.isEmpty());
}

TEST(EditCommand, APresetForABlockThatIsNotThereIsRefused)
{
    Session session = fullSession();
    const Session before = session;
    ApplyPreset preset;
    preset.blockId = QStringLiteral("b9");
    preset.params = QJsonObject{{QStringLiteral("ratio"), 2.0}};

    EXPECT_EQ(applyEdit(session, preset).error(), EditError::BlockNotFound);
    session.revision = before.revision;
    EXPECT_EQ(session, before) << "a refused edit leaves the session exactly as it was";
}

TEST(EditBatch, StopsAtTheFirstRefusalAndSaysHowFarItGot)
{
    Session session = fullSession();
    const std::vector<EditCommand> commands{
        MoveBlock{QStringLiteral("b1"), QPointF{1.0, 1.0}},
        SetEnabled{QStringLiteral("b1"), false},
        RemoveBlock{QStringLiteral("b9")}, // refused
        SetEnabled{QStringLiteral("b2"), true},
    };

    const std::expected<EditInverse, EditBatchError> result = applyEdits(session, commands);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), (EditBatchError{2, EditError::BlockNotFound}));
    EXPECT_FALSE(session.findBlock(QStringLiteral("b1"))->enabled) << "what was applied stays applied";
}

TEST(EditBatch, IsUndoneInTheOppositeOrder)
{
    Session session = fullSession();
    const Session before = session;
    const std::vector<EditCommand> commands{
        RemoveBlock{QStringLiteral("b2")},
        AddBlock{newBlock()},
        Connect{Wire{QStringLiteral("b1"), 0, QStringLiteral("b4"), 0}},
        Rename{QString{}, QStringLiteral("Changed")},
    };

    const std::expected<EditInverse, EditBatchError> inverse = applyEdits(session, commands);
    ASSERT_TRUE(inverse.has_value());
    ASSERT_TRUE(applyEdits(session, *inverse).has_value());

    session.revision = before.revision;
    EXPECT_EQ(session, before);
}

TEST(EditCommand, CommandsAndErrorsHaveStableNames)
{
    // These travel over the facade as ApplyEdit's kind and its error.
    EXPECT_EQ(editCommandName(AddBlock{newBlock()}), "addBlock");
    EXPECT_EQ(editCommandName(RemoveBlock{}), "removeBlock");
    EXPECT_EQ(editCommandName(MoveBlock{}), "moveBlock");
    EXPECT_EQ(editCommandName(Connect{}), "connect");
    EXPECT_EQ(editCommandName(Disconnect{}), "disconnect");
    EXPECT_EQ(editCommandName(SetParam{}), "setParam");
    EXPECT_EQ(editCommandName(Rename{}), "rename");
    EXPECT_EQ(editCommandName(SetNotes{}), "setNotes");
    EXPECT_EQ(editCommandName(SetEnabled{}), "setEnabled");
    EXPECT_EQ(editCommandName(SetSessionSetting{}), "setSessionSetting");

    EXPECT_EQ(editErrorName(EditError::BlockNotFound), "blockNotFound");
    EXPECT_EQ(editErrorName(EditError::WireToItself), "wireToItself");
    EXPECT_EQ(editErrorName(EditError::WireWouldCycle), "wireWouldCycle");
    EXPECT_EQ(sessionSettingKeyName(SessionSettingKey::AutoConnect), "autoConnect");
    EXPECT_EQ(sessionSettingKeyName(SessionSettingKey::SampleRate), "sampleRate");
    EXPECT_EQ(sessionSettingKeyName(SessionSettingKey::Latency), "latency");
    EXPECT_EQ(sessionSettingKeyName(SessionSettingKey::AutoRun), "autoRun");
}
