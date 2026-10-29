// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Undo and redo. The history is a stack of gestures, so most of what
// is worth checking here is what counts as one gesture and what does not.

#include "core/edithistory.h"
#include "sessionfixtures.h"

#include <QPointF>
#include <gtest/gtest.h>

#include <random>

using namespace rb::core;
using namespace Qt::StringLiterals;

namespace
{

/// A clock a test winds by hand, so that a two-second drag costs the test
/// nothing at all.
class Hand
{
public:
    void wind(std::int64_t milliseconds)
    {
        m_now += milliseconds;
    }

    [[nodiscard]] std::function<std::int64_t()> clock()
    {
        return [this] { return m_now; };
    }

private:
    std::int64_t m_now = 0;
};

/// Applies a command and records it, the way the engine does.
[[nodiscard]] bool doIt(Session &session, EditHistory &history, const EditCommand &command)
{
    const std::expected<EditInverse, EditError> applied = applyEdit(session, command);
    if (!applied) {
        return false;
    }
    history.record(command, *applied);
    return true;
}

/// Takes the top step back, the way the engine does.
[[nodiscard]] bool undoOne(Session &session, EditHistory &history)
{
    const std::optional<EditStep> step = history.takeUndo();
    if (!step) {
        return false;
    }
    for (const EditCommand &command : step->undo) {
        if (!applyEdit(session, command)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool redoOne(Session &session, EditHistory &history)
{
    const std::optional<EditStep> step = history.takeRedo();
    if (!step) {
        return false;
    }
    for (const EditCommand &command : step->redo) {
        if (!applyEdit(session, command)) {
            return false;
        }
    }
    return true;
}

/// The same session but for the revision, which counts every change and so
/// never comes back down.
[[nodiscard]] bool sameModel(Session left, const Session &right)
{
    left.revision = right.revision;
    return left == right;
}

[[nodiscard]] BlockInstance block(const QString &id, QPointF position = {})
{
    BlockInstance made;
    made.id = id;
    made.type = u"gain.simple"_s;
    made.position = position;
    return made;
}

[[nodiscard]] const BlockInstance *find(const Session &session, const QString &id)
{
    for (const BlockInstance &block : session.blocks) {
        if (block.id == id) {
            return &block;
        }
    }
    return nullptr;
}

} // namespace

TEST(EditHistoryTest, EachEditIsAStepWithAName)
{
    Session session = rb::test::fullSession();
    EditHistory history;

    EXPECT_FALSE(history.canUndo());
    EXPECT_TRUE(history.undoCode().isEmpty());

    ASSERT_TRUE(doIt(session, history, AddBlock{block(u"b4"_s), -1}));
    EXPECT_TRUE(history.canUndo());
    EXPECT_FALSE(history.canRedo());
    EXPECT_EQ(history.undoCode(), u"edit.addBlock"_s);
    EXPECT_EQ(history.undoSize(), 1U);

    ASSERT_TRUE(doIt(session, history, SetEnabled{u"b4"_s, false}));
    EXPECT_EQ(history.undoCode(), u"edit.setEnabled"_s);
    EXPECT_EQ(history.undoSize(), 2U);
}

TEST(EditHistoryTest, UndoPutsTheSessionBackAndRedoDoesItAgain)
{
    Session session = rb::test::fullSession();
    const Session before = session;
    EditHistory history;

    ASSERT_TRUE(doIt(session, history, AddBlock{block(u"b4"_s), -1}));
    const Session added = session;
    ASSERT_TRUE(undoOne(session, history));
    EXPECT_TRUE(sameModel(session, before));
    EXPECT_FALSE(history.canUndo());
    EXPECT_TRUE(history.canRedo());
    EXPECT_EQ(history.redoCode(), u"edit.addBlock"_s);

    ASSERT_TRUE(redoOne(session, history));
    EXPECT_TRUE(sameModel(session, added));
    EXPECT_TRUE(history.canUndo());
    EXPECT_FALSE(history.canRedo());
}

TEST(EditHistoryTest, RemovingABlockComesBackWithItsWires)
{
    Session session = rb::test::fullSession();
    const Session before = session;
    EditHistory history;

    // b2 is wired at both ends in the fixture, so its removal takes three
    // wires with it and one step has to put all four things back.
    ASSERT_TRUE(doIt(session, history, RemoveBlock{u"b2"_s}));
    EXPECT_EQ(find(session, u"b2"_s), nullptr);
    EXPECT_TRUE(session.wires.isEmpty());

    ASSERT_TRUE(undoOne(session, history));
    EXPECT_TRUE(sameModel(session, before)) << "the block or its wires did not come back where they were";
}

TEST(EditHistoryTest, ATwoSecondDragOfOneKnobIsOneStep)
{
    Session session = rb::test::fullSession();
    const Session before = session;
    EditHistory history;
    Hand hand;
    history.setClock(hand.clock());

    // Sixty frames a second for two seconds, which is what a slider dragged
    // across its range sends.
    for (int frame = 0; frame < 120; ++frame) {
        const double value = static_cast<double>(frame) / 119.0;
        ASSERT_TRUE(doIt(session, history, SetParam{u"b1"_s, u"gain"_s, value}));
        hand.wind(16);
    }

    EXPECT_EQ(history.undoSize(), 1U) << "the drag became " << history.undoSize() << " steps";
    EXPECT_DOUBLE_EQ(find(session, u"b1"_s)->params.value(u"gain"_s).toDouble(), 1.0);

    ASSERT_TRUE(undoOne(session, history));
    EXPECT_TRUE(sameModel(session, before)) << "undo left the knob somewhere in the middle of the drag";

    // And redoing it lands where the drag ended, not where it passed.
    ASSERT_TRUE(redoOne(session, history));
    EXPECT_DOUBLE_EQ(find(session, u"b1"_s)->params.value(u"gain"_s).toDouble(), 1.0);
}

TEST(EditHistoryTest, AHandThatStopsEndsTheGesture)
{
    Session session = rb::test::fullSession();
    EditHistory history;
    Hand hand;
    history.setClock(hand.clock());

    ASSERT_TRUE(doIt(session, history, SetParam{u"b1"_s, u"gain"_s, 0.2}));
    hand.wind(EditHistory::kGesture.count() + 1);
    ASSERT_TRUE(doIt(session, history, SetParam{u"b1"_s, u"gain"_s, 0.4}));
    EXPECT_EQ(history.undoSize(), 2U) << "a pause in the middle should have split the drag";

    // As does letting go, whatever the clock says.
    hand.wind(1);
    history.endGesture();
    ASSERT_TRUE(doIt(session, history, SetParam{u"b1"_s, u"gain"_s, 0.6}));
    EXPECT_EQ(history.undoSize(), 3U);
}

TEST(EditHistoryTest, AGestureSomebodySaidStartedLastsUntilItIsSaidToEnd)
{
    // A plugin's own window says when a knob is taken hold of and let go
    // of: a hand that rests in the middle is still the same drag.
    Session session = rb::test::fullSession();
    EditHistory history;
    Hand hand;
    history.setClock(hand.clock());

    ASSERT_TRUE(doIt(session, history, SetParam{u"b1"_s, u"gain"_s, 0.1}));
    hand.wind(16);
    history.beginGesture(u"b1"_s, u"gain"_s);
    ASSERT_TRUE(doIt(session, history, SetParam{u"b1"_s, u"gain"_s, 0.2}));
    hand.wind(EditHistory::kGesture.count() * 4);
    ASSERT_TRUE(doIt(session, history, SetParam{u"b1"_s, u"gain"_s, 0.4}));
    hand.wind(EditHistory::kGesture.count() * 4);
    ASSERT_TRUE(doIt(session, history, SetParam{u"b1"_s, u"gain"_s, 0.6}));
    // The end of another control's gesture is not this one's.
    history.endGesture(u"b1"_s, u"pan"_s);
    hand.wind(EditHistory::kGesture.count() * 4);
    ASSERT_TRUE(doIt(session, history, SetParam{u"b1"_s, u"gain"_s, 0.6}));
    history.endGesture(u"b1"_s, u"gain"_s);
    EXPECT_EQ(history.undoSize(), 2U) << "the said gesture should be one step of its own";

    // One Ctrl+Z takes the whole drag back, to before it began.
    ASSERT_TRUE(undoOne(session, history));
    EXPECT_EQ(session.findBlock(u"b1"_s)->params.value(u"gain"_s).toDouble(), 0.1);

    // And once it has been said to end, time decides again.
    hand.wind(1);
    ASSERT_TRUE(doIt(session, history, SetParam{u"b1"_s, u"gain"_s, 0.3}));
    hand.wind(EditHistory::kGesture.count() + 1);
    ASSERT_TRUE(doIt(session, history, SetParam{u"b1"_s, u"gain"_s, 0.5}));
    EXPECT_EQ(history.undoSize(), 3U);
}

TEST(EditHistoryTest, AGestureThatNeverEndsHoldsNothingForEver)
{
    // A window that never said it let go: another knob is another step,
    // a long rest ends it anyway, and so does an undo.
    Session session = rb::test::fullSession();
    EditHistory history;
    Hand hand;
    history.setClock(hand.clock());

    history.beginGesture(u"b1"_s, u"gain"_s);
    ASSERT_TRUE(doIt(session, history, SetParam{u"b1"_s, u"gain"_s, 0.2}));
    hand.wind(16);
    ASSERT_TRUE(doIt(session, history, SetParam{u"b1"_s, u"pan"_s, 0.5}));
    EXPECT_EQ(history.undoSize(), 2U) << "another control joined the held one";
    hand.wind(16);
    ASSERT_TRUE(doIt(session, history, SetParam{u"b1"_s, u"gain"_s, 0.3}));
    EXPECT_EQ(history.undoSize(), 3U);
    hand.wind(EditHistory::kHeldGesture.count() + 1);
    ASSERT_TRUE(doIt(session, history, SetParam{u"b1"_s, u"gain"_s, 0.4}));
    EXPECT_EQ(history.undoSize(), 4U) << "a gesture held past the longest rest";

    history.beginGesture(u"b1"_s, u"gain"_s);
    ASSERT_TRUE(doIt(session, history, SetParam{u"b1"_s, u"gain"_s, 0.5}));
    ASSERT_TRUE(undoOne(session, history));
    hand.wind(EditHistory::kGesture.count() * 4);
    ASSERT_TRUE(doIt(session, history, SetParam{u"b1"_s, u"gain"_s, 0.6}));
    ASSERT_TRUE(doIt(session, history, SetParam{u"b1"_s, u"gain"_s, 0.7}));
    hand.wind(EditHistory::kGesture.count() * 4);
    ASSERT_TRUE(doIt(session, history, SetParam{u"b1"_s, u"gain"_s, 0.8}));
    EXPECT_EQ(history.undoSize(), 6U) << "an undo did not end the held gesture";
}

TEST(EditHistoryTest, TwoKnobsAreTwoSteps)
{
    Session session = rb::test::fullSession();
    EditHistory history;
    Hand hand;
    history.setClock(hand.clock());

    ASSERT_TRUE(doIt(session, history, SetParam{u"b1"_s, u"gain"_s, 0.2}));
    hand.wind(16);
    ASSERT_TRUE(doIt(session, history, SetParam{u"b1"_s, u"pan"_s, 0.5}));
    hand.wind(16);
    ASSERT_TRUE(doIt(session, history, SetParam{u"b3"_s, u"gain"_s, 0.7}));
    EXPECT_EQ(history.undoSize(), 3U) << "different knobs are different things to take back";
}

TEST(EditHistoryTest, MovingSeveralTilesIsOneStep)
{
    Session session = rb::test::fullSession();
    const Session before = session;
    EditHistory history;
    Hand hand;
    history.setClock(hand.clock());

    // A selection of three tiles dragged together: the GUI sends one move
    // per tile per frame.
    for (int frame = 1; frame <= 20; ++frame) {
        const QPointF by{static_cast<double>(frame) * 3.0, static_cast<double>(frame)};
        for (const QString &id : {u"b1"_s, u"b2"_s, u"b3"_s}) {
            ASSERT_TRUE(doIt(session, history, MoveBlock{id, find(session, id)->position + by}));
        }
        hand.wind(16);
    }

    EXPECT_EQ(history.undoSize(), 1U) << "the drag became " << history.undoSize() << " steps";
    EXPECT_EQ(history.undoCode(), u"edit.moveBlock"_s);
    ASSERT_TRUE(undoOne(session, history));
    EXPECT_TRUE(sameModel(session, before)) << "not every tile went back where it started";

    ASSERT_TRUE(redoOne(session, history));
    for (const QString &id : {u"b1"_s, u"b2"_s, u"b3"_s}) {
        EXPECT_NE(find(session, id)->position, find(before, id)->position) << id.toStdString();
    }
}

TEST(EditHistoryTest, ABlockAndTheWiresAutomaticModeDrewWithItAreOneStep)
{
    // Automatic connections send the block and then the wires that go with
    // it. Half of that taken back is a session nobody ever saw: a block
    // sitting there with nothing joined to it, or — the other way round —
    // wires to a block that has gone.
    Session session = rb::test::fullSession();
    const Session before = session;
    EditHistory history;
    Hand hand;
    history.setClock(hand.clock());

    ASSERT_TRUE(doIt(session, history, AddBlock{block(u"b4"_s, QPointF{600.0, 120.0})}));
    hand.wind(1);
    ASSERT_TRUE(doIt(session, history, Connect{Wire{u"b3"_s, 0, u"b4"_s, 0}}));
    hand.wind(1);
    ASSERT_TRUE(doIt(session, history, Connect{Wire{u"b3"_s, 1, u"b4"_s, 1}}));

    EXPECT_EQ(history.undoSize(), 1U) << "adding a block in automatic mode became " << history.undoSize() << " steps";
    EXPECT_EQ(history.undoCode(), u"edit.addBlock"_s) << "the step is named after what started it";

    ASSERT_TRUE(undoOne(session, history)) << "the undo would not apply";
    EXPECT_TRUE(sameModel(session, before)) << "the block or its wires were left behind";

    ASSERT_TRUE(redoOne(session, history));
    EXPECT_EQ(session.wires.size(), before.wires.size() + 2);
}

TEST(EditHistoryTest, ARemovedBlockAndTheWireDrawnAroundItAreOneStep)
{
    // Taking a block out of the middle of a lane wires its neighbours
    // together. Undoing only the repair would leave the lane with a hole in
    // it and no block to put back.
    Session session = rb::test::fullSession();
    const Session before = session;
    EditHistory history;
    Hand hand;
    history.setClock(hand.clock());

    ASSERT_TRUE(doIt(session, history, RemoveBlock{u"b2"_s}));
    hand.wind(1);
    ASSERT_TRUE(doIt(session, history, Connect{Wire{u"b1"_s, 0, u"b3"_s, 0}}));

    EXPECT_EQ(history.undoSize(), 1U);
    ASSERT_TRUE(undoOne(session, history)) << "the undo would not apply in the order it was kept";
    EXPECT_TRUE(sameModel(session, before));
}

TEST(EditHistoryTest, AWireDraggedFromOnePortToAnotherIsOneStep)
{
    // A Ctrl-drag that takes a wire off one port and puts it on another
    // sends a disconnect and a connect. They share an end, which is what
    // tells a wire that moved from two wires somebody edited one after the
    // other — and one Ctrl+Z has to put it back where it was.
    Session session = rb::test::fullSession();
    const Session before = session;
    EditHistory history;
    Hand hand;
    history.setClock(hand.clock());

    ASSERT_TRUE(doIt(session, history, Disconnect{Wire{u"b2"_s, 0, u"b3"_s, 0}}));
    hand.wind(1);
    ASSERT_TRUE(doIt(session, history, Connect{Wire{u"b2"_s, 0, u"b3"_s, 1}}));

    EXPECT_EQ(history.undoSize(), 1U) << "moving a wire became " << history.undoSize() << " steps";
    ASSERT_TRUE(undoOne(session, history));
    EXPECT_TRUE(sameModel(session, before)) << "the wire did not go back where it was";
}

TEST(EditHistoryTest, TwoWiresWithNothingInCommonAreTwoSteps)
{
    // The other side of the rule above. Somebody who takes one wire out and
    // draws an unrelated one has done two things, however quickly.
    Session session = rb::test::fullSession();
    EditHistory history;
    Hand hand;
    history.setClock(hand.clock());

    ASSERT_TRUE(doIt(session, history, Disconnect{Wire{u"b1"_s, 0, u"b2"_s, 0}}));
    hand.wind(1);
    ASSERT_TRUE(doIt(session, history, Connect{Wire{u"b2"_s, 1, u"b3"_s, 1}}));

    EXPECT_EQ(history.undoSize(), 2U) << "two unrelated wires were merged into one step";
}

TEST(EditHistoryTest, AnEditOfAnotherKindNeverJoinsAGesture)
{
    Session session = rb::test::fullSession();
    EditHistory history;
    Hand hand;
    history.setClock(hand.clock());

    ASSERT_TRUE(doIt(session, history, MoveBlock{u"b1"_s, QPointF{10.0, 10.0}}));
    hand.wind(1);
    ASSERT_TRUE(doIt(session, history, SetEnabled{u"b1"_s, false}));
    hand.wind(1);
    // Back to moving, but the gesture is over: the enabling ended it.
    ASSERT_TRUE(doIt(session, history, MoveBlock{u"b1"_s, QPointF{20.0, 20.0}}));
    EXPECT_EQ(history.undoSize(), 3U);
}

TEST(EditHistoryTest, ANoteChangedIsOneStepToTakeBack)
{
    // What session notes need from undo. The Info tab sends the whole note
    // once the field is done with, so however much was typed into it, one
    // Ctrl+Z brings back what was there before.
    Session session = rb::test::fullSession();
    const QString before = session.notes;
    EditHistory history;
    Hand hand;
    history.setClock(hand.clock());

    ASSERT_TRUE(doIt(session, history, SetNotes{u"Two microphones.\nMixed down on the laptop."_s}));
    EXPECT_EQ(history.undoSize(), 1U);
    EXPECT_EQ(history.undoCode(), u"edit.setNotes"_s);

    ASSERT_TRUE(undoOne(session, history));
    EXPECT_EQ(session.notes, before);
    EXPECT_FALSE(history.canUndo());

    // A second edit is a second step however soon it arrives: unlike a knob
    // being turned, one finished note does not carry on from another.
    ASSERT_TRUE(doIt(session, history, SetNotes{u"One"_s}));
    hand.wind(1);
    ASSERT_TRUE(doIt(session, history, SetNotes{QString{}}));
    EXPECT_EQ(history.undoSize(), 2U);
    ASSERT_TRUE(undoOne(session, history));
    EXPECT_EQ(session.notes, u"One"_s) << "emptying the box came back as something else";
}

TEST(EditHistoryTest, AnEditAfterAnUndoLosesTheRedo)
{
    Session session = rb::test::fullSession();
    EditHistory history;

    ASSERT_TRUE(doIt(session, history, AddBlock{block(u"b4"_s), -1}));
    ASSERT_TRUE(undoOne(session, history));
    EXPECT_TRUE(history.canRedo());

    ASSERT_TRUE(doIt(session, history, AddBlock{block(u"b5"_s), -1}));
    EXPECT_FALSE(history.canRedo()) << "the history was left, so what was undone cannot be done again";
    EXPECT_EQ(history.undoSize(), 1U);
}

TEST(EditHistoryTest, TheOldestStepFallsOffTheBottom)
{
    Session session = rb::test::fullSession();
    EditHistory history;
    Hand hand;
    history.setClock(hand.clock());

    for (std::size_t step = 0; step < EditHistory::kLimit + 50; ++step) {
        ASSERT_TRUE(doIt(session, history, Rename{QString{}, u"Take %1"_s.arg(step)}));
        hand.wind(EditHistory::kGesture.count() + 1); // each one a thing of its own
    }
    EXPECT_EQ(history.undoSize(), EditHistory::kLimit);

    // Undoing everything that is left cannot reach the name it started with,
    // and must not pretend otherwise.
    while (history.canUndo()) {
        ASSERT_TRUE(undoOne(session, history));
    }
    EXPECT_EQ(session.name, u"Take 49"_s);
}

TEST(EditHistoryTest, ClosingASessionForgetsIt)
{
    Session session = rb::test::fullSession();
    EditHistory history;

    ASSERT_TRUE(doIt(session, history, AddBlock{block(u"b4"_s), -1}));
    ASSERT_TRUE(undoOne(session, history));
    history.clear();
    EXPECT_FALSE(history.canUndo());
    EXPECT_FALSE(history.canRedo());
    EXPECT_EQ(history.undoSize(), 0U);
    EXPECT_EQ(history.redoSize(), 0U);
}

TEST(EditHistoryTest, AnyRunOfEditsUndoneInFullLeavesTheSessionAsItWas)
{
    // The property the whole thing rests on. The sequences are random but
    // the seed is not, so a failure can be run again.
    for (unsigned int seed = 1; seed <= 200; ++seed) {
        std::mt19937 random(seed);
        Session session = rb::test::fullSession();
        const Session before = session;
        EditHistory history;
        Hand hand;
        history.setClock(hand.clock());

        int made = 0;
        for (int edit = 0; edit < 40; ++edit) {
            const QStringList ids{u"b1"_s, u"b2"_s, u"b3"_s};
            const QString id = ids.at(static_cast<qsizetype>(random() % 3));
            EditCommand command = Rename{QString{}, u"Take %1"_s.arg(edit)};
            switch (random() % 9) {
            case 0:
                command = AddBlock{block(u"n%1"_s.arg(edit)), static_cast<qsizetype>(random() % 4) - 1};
                break;
            case 1:
                command = RemoveBlock{id};
                break;
            case 2:
                command = MoveBlock{id, QPointF{static_cast<double>(random() % 500), 20.5}};
                break;
            case 3:
                command = Connect{Wire{u"b1"_s, 0, u"b3"_s, 1}, -1};
                break;
            case 4:
                command = Disconnect{Wire{u"b1"_s, 0, u"b2"_s, 0}};
                break;
            case 5:
                command = SetParam{id, u"gain"_s, static_cast<double>(random() % 100) / 100.0};
                break;
            case 6:
                command = SetEnabled{id, (random() % 2) == 0};
                break;
            case 7:
                // Sometimes emptied, which is a change like any other.
                command = SetNotes{(random() % 2) == 0 ? QString{} : u"Take %1"_s.arg(edit)};
                break;
            default:
                break;
            }
            // An edit the model refuses — a wire that is already there, a
            // block that has been removed — changes nothing and is not a
            // step, which is itself part of what is being checked.
            if (doIt(session, history, command)) {
                ++made;
            }
            hand.wind(static_cast<std::int64_t>(random() % 900)); // sometimes a gesture, sometimes not
        }
        ASSERT_GT(made, 0U) << "seed " << seed << " made no edits at all";

        while (history.canUndo()) {
            ASSERT_TRUE(undoOne(session, history)) << "seed " << seed;
        }
        EXPECT_TRUE(sameModel(session, before)) << "seed " << seed;

        // And everything can be put back again, in one go.
        while (history.canRedo()) {
            ASSERT_TRUE(redoOne(session, history)) << "seed " << seed;
        }
        while (history.canUndo()) {
            ASSERT_TRUE(undoOne(session, history)) << "seed " << seed;
        }
        EXPECT_TRUE(sameModel(session, before)) << "seed " << seed << " after a redo of everything";
    }
}
