// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The session as the interface reads it: it follows the engine's copy
// rather than keeping one of its own, and a knob being dragged costs the
// engine sixty edits a second rather than one per mouse event.

#include "core/editcommand.h"
#include "core/mockengine.h"
#include "core/session.h"
#include "sessionmodel.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QVariantMap>
#include <gtest/gtest.h>

#include <array>
#include <memory>

using namespace rb;
using namespace Qt::StringLiterals;

namespace
{

[[nodiscard]] QString asJson(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

[[nodiscard]] QString addBlockEdit(const QString &id, const QString &type)
{
    core::BlockInstance block;
    block.id = id;
    block.type = type;
    core::AddBlock command;
    command.block = block;
    return asJson(core::editCommandToJson(command));
}

/// A session in the mock with one Volume block in it, followed by \a model.
[[nodiscard]] QString openSessionWithBlock(core::MockEngine &engine, ui::SessionModel &model)
{
    QSignalSpy opened(&engine, &core::EngineFacade::sessionOpened);
    engine.createSession(u"Panel"_s);
    EXPECT_EQ(opened.count(), 1);
    const QString id = opened.isEmpty() ? QString{} : opened.back().at(0).toString();
    model.setSessionId(id);
    engine.applyEdit(id, addBlockEdit(u"b1"_s, u"effect.volume"_s));
    return id;
}

/// **A timer needs an event loop to exist in.** Without an application
/// QTimer::start() does not take, and a test of what the coalescing does
/// would be measuring Qt rather than this code.
class SessionModelTest : public ::testing::Test
{
public:
    static void SetUpTestSuite()
    {
        s_argv = {s_name.data(), nullptr};
        s_argc = 1;
        s_application = std::make_unique<QCoreApplication>(s_argc, s_argv.data());
    }

    static void TearDownTestSuite()
    {
        s_application.reset();
    }

private:
    static std::array<char, 6> s_name;
    static std::array<char *, 2> s_argv;
    static int s_argc;
    static std::unique_ptr<QCoreApplication> s_application;
};

std::array<char, 6> SessionModelTest::s_name{'m', 'o', 'd', 'e', 'l', '\0'};
std::array<char *, 2> SessionModelTest::s_argv{};
int SessionModelTest::s_argc = 1;
std::unique_ptr<QCoreApplication> SessionModelTest::s_application;

} // namespace

TEST_F(SessionModelTest, ItFollowsTheEnginesCopyRatherThanKeepingOne)
{
    core::MockEngine engine;
    ui::SessionModel model;
    model.attach(&engine);
    const QString id = openSessionWithBlock(engine, model);

    EXPECT_EQ(model.sessionId(), id);
    EXPECT_EQ(model.name(), u"Panel"_s);
    ASSERT_EQ(model.blockIds(), QStringList{u"b1"_s});
    EXPECT_EQ(model.block(u"b1"_s).value("type"_L1).toString(), u"effect.volume"_s);
    EXPECT_TRUE(model.block(u"b1"_s).value("enabled"_L1).toBool());

    // A parameter the session does not mention is at its default, which is
    // the descriptor's business and not the session's.
    EXPECT_FALSE(model.param(u"b1"_s, u"gain"_s).isValid());

    model.setParam(u"b1"_s, u"gain"_s, 2.5);
    EXPECT_DOUBLE_EQ(model.param(u"b1"_s, u"gain"_s).toDouble(), 2.5);
    EXPECT_EQ(id, model.sessionId());
}

TEST_F(SessionModelTest, AKnobDoesNotComeBackAsAPatchButAnUndoDoes)
{
    // **The engine sends no patch for SetParam, on purpose**: sixty a
    // second telling the client what it just did would be sixty messages a
    // second saying nothing. So the value is kept here as it is sent — and
    // an undo, which *is* a patch, overwrites it.
    core::MockEngine engine;
    ui::SessionModel model;
    model.attach(&engine);
    const QString id = openSessionWithBlock(engine, model);

    QSignalSpy patches(&engine, &core::EngineFacade::sessionPatch);
    model.setParam(u"b1"_s, u"gain"_s, 2.5);
    EXPECT_EQ(patches.count(), 0) << "a turned knob came back as a patch";
    EXPECT_DOUBLE_EQ(model.param(u"b1"_s, u"gain"_s).toDouble(), 2.5);

    engine.undo(id);
    EXPECT_GE(patches.count(), 1) << "an undo did not come back as a patch";
    EXPECT_FALSE(model.param(u"b1"_s, u"gain"_s).isValid()) << "the undone value stayed behind";
}

TEST_F(SessionModelTest, ItAsksForTheSnapshotAgainWhenItHasMissedAPatch)
{
    // **A gap is something to notice.** Patches are numbered so that a
    // client which has missed one can say so rather than going on with a
    // session that is quietly wrong.
    core::MockEngine engine;
    ui::SessionModel model;
    model.attach(&engine);
    const QString id = openSessionWithBlock(engine, model);
    const qulonglong before = model.revision();

    QSignalSpy snapshots(&engine, &core::EngineFacade::sessionSnapshot);
    // A patch from the future: the model cannot apply it and must not try.
    Q_EMIT engine.sessionPatch(id, before + 7, addBlockEdit(u"b2"_s, u"effect.volume"_s));
    EXPECT_EQ(model.blockIds().size(), 1) << "a patch out of order was applied anyway";
    EXPECT_GE(snapshots.count(), 1) << "the model did not ask to resynchronise";

    // And it is whole again afterwards.
    engine.applyEdit(id, addBlockEdit(u"b2"_s, u"effect.volume"_s));
    EXPECT_EQ(model.blockIds().size(), 2);
}

TEST_F(SessionModelTest, ANameAndASwitchGoToTheEngineAsEdits)
{
    core::MockEngine engine;
    ui::SessionModel model;
    model.attach(&engine);
    const QString id = openSessionWithBlock(engine, model);

    model.renameBlock(u"b1"_s, u"Voice"_s);
    EXPECT_EQ(model.block(u"b1"_s).value("nickname"_L1).toString(), u"Voice"_s);

    model.setBlockEnabled(u"b1"_s, false);
    EXPECT_FALSE(model.block(u"b1"_s).value("enabled"_L1).toBool());

    // **Nothing is changed here.** Both came back as patches, which is what
    // makes undo, a second window and a daemon all behave the same.
    model.setBlockEnabled(u"b1"_s, true);
    EXPECT_TRUE(model.block(u"b1"_s).value("enabled"_L1).toBool());
}

// What the Info tab writes: the session's name and its own settings,
// each an edit that comes back as a patch.
TEST_F(SessionModelTest, TheInfoTabsSettingsGoToTheEngineAsEdits)
{
    core::MockEngine engine;
    ui::SessionModel model;
    model.attach(&engine);
    const QString id = openSessionWithBlock(engine, model);

    // What a session starts as, so that a panel has something true to draw
    // before anybody touches it.
    EXPECT_EQ(model.name(), u"Panel"_s);
    EXPECT_TRUE(model.autoConnect());
    EXPECT_EQ(model.sampleRateSetting(), u"auto"_s);
    EXPECT_EQ(model.latency(), u"default"_s);

    model.renameSession(u"Interview"_s);
    EXPECT_EQ(model.name(), u"Interview"_s);
    // **A session with no name is a session nobody finds again**, so an
    // empty field is a field nobody typed in rather than a rename.
    model.renameSession(QString{});
    EXPECT_EQ(model.name(), u"Interview"_s);

    model.setAutoConnect(false);
    EXPECT_FALSE(model.autoConnect());

    model.setLatency(u"low"_s);
    EXPECT_EQ(model.latency(), u"low"_s);

    // A value the engine does not take is refused there and changes
    // nothing here: the model never writes into its own copy and hopes.
    QSignalSpy errors(&engine, &core::EngineFacade::error);
    model.setLatency(u"as low as it goes"_s);
    EXPECT_EQ(model.latency(), u"low"_s);
    EXPECT_EQ(errors.count(), 1);
}

// What the Info tab writes into the notes box.
TEST_F(SessionModelTest, TheNotesGoToTheEngineAsAnEditAndAnEmptyOneIsStillAnEdit)
{
    core::MockEngine engine;
    ui::SessionModel model;
    model.attach(&engine);
    const QString id = openSessionWithBlock(engine, model);
    EXPECT_TRUE(model.notes().isEmpty()) << "a new session has nothing written about it yet";

    QSignalSpy patches(&engine, &core::EngineFacade::sessionPatch);
    const QString written = u"Two microphones.\nMixed down on the laptop."_s;
    model.setNotes(written);
    EXPECT_EQ(model.notes(), written);
    EXPECT_EQ(patches.count(), 1) << "a session-wide edit comes back as a patch, unlike a turned knob";

    // **Empty is a note**, unlike an empty name: clearing the box is how
    // somebody takes back what they wrote, so it has to reach the engine.
    model.setNotes(QString{});
    EXPECT_TRUE(model.notes().isEmpty());
    EXPECT_EQ(patches.count(), 2) << "emptying the box was swallowed here";

    // The criterion: one undo, and what was there is back.
    engine.undo(id);
    EXPECT_EQ(model.notes(), written);

    // Writing what is already there is not an edit. The field sends its
    // note whenever it is done with, which includes the cursor passing
    // through it without anybody typing a thing.
    const int sofar = patches.count();
    model.setNotes(written);
    EXPECT_EQ(patches.count(), sofar) << "an unchanged note was sent as an edit";
}

TEST_F(SessionModelTest, ADragCostsSixtyEditsASecondAndNotOnePerMouseEvent)
{
    // The criterion: about 120 calls for a two-second drag. A mouse can
    // produce ten times that, and every one of them would be an edit.
    core::MockEngine engine;
    ui::SessionModel model;
    model.attach(&engine);
    (void)openSessionWithBlock(engine, model);

    // Two seconds of a slider being moved, as fast as the machine will go.
    QElapsedTimer started;
    started.start();
    int moves = 0;
    while (started.elapsed() < 2000) {
        const double position = static_cast<double>(started.elapsed()) / 2000.0 * 4.0;
        model.setParam(u"b1"_s, u"gain"_s, position, true);
        ++moves;
        QCoreApplication::processEvents();
    }
    model.commitParam(u"b1"_s, u"gain"_s);

    EXPECT_GT(moves, 200) << "the drag was not fast enough to be a test of anything";
    EXPECT_LE(model.sentParamCount(), 140) << "a two-second drag sent " << model.sentParamCount() << " edits";
    EXPECT_GE(model.sentParamCount(), 30) << "the drag barely reached the engine at all";
    // **And the last one always arrives.** Wherever the timer was when the
    // button came up, what the engine ends at is where the slider stopped.
    EXPECT_NEAR(model.param(u"b1"_s, u"gain"_s).toDouble(), 4.0, 0.2);
}

TEST_F(SessionModelTest, LettingGoOfASliderSendsWhereItStopped)
{
    core::MockEngine engine;
    ui::SessionModel model;
    model.attach(&engine);
    (void)openSessionWithBlock(engine, model);

    // Three values in one sixtieth of a second: only the last is worth
    // sending, and it must be sent.
    model.setParam(u"b1"_s, u"gain"_s, 1.0, true);
    model.setParam(u"b1"_s, u"gain"_s, 2.0, true);
    model.setParam(u"b1"_s, u"gain"_s, 3.0, true);
    model.commitParam(u"b1"_s, u"gain"_s);

    EXPECT_DOUBLE_EQ(model.param(u"b1"_s, u"gain"_s).toDouble(), 3.0);
    // The first of a drag goes at once so the control never feels behind
    // the pointer; the rest were coalesced into the one that was committed.
    EXPECT_EQ(model.sentParamCount(), 2);
}

TEST_F(SessionModelTest, AValueStillBeingDraggedIsWhatTheControlShows)
{
    core::MockEngine engine;
    ui::SessionModel model;
    model.attach(&engine);
    (void)openSessionWithBlock(engine, model);

    model.setParam(u"b1"_s, u"gain"_s, 1.0, true);
    model.setParam(u"b1"_s, u"gain"_s, 2.75, true);
    // Not sent yet, but it is where the slider is; a panel that read back
    // the engine's last value would snap backwards under the pointer.
    EXPECT_DOUBLE_EQ(model.param(u"b1"_s, u"gain"_s).toDouble(), 2.75);
}

TEST_F(SessionModelTest, LettingGoTellsTheWindowEvenWhenTheLastValueHadAlreadyGone)
{
    // A click on a slider's track is one value, and the first value
    // of a gesture goes at once, so by the time the button comes up there
    // is nothing left to send. The window still has to hear that the
    // gesture is over, or every control reads the value from before it and
    // the handle jumps back.
    core::MockEngine engine;
    ui::SessionModel model;
    model.attach(&engine);
    (void)openSessionWithBlock(engine, model);

    QSignalSpy changed(&model, &ui::SessionModel::changed);
    model.setParam(u"b1"_s, u"gain"_s, 2.5, true);
    EXPECT_EQ(changed.count(), 0) << "a value mid-gesture is not news yet";
    model.commitParam(u"b1"_s, u"gain"_s);
    EXPECT_EQ(changed.count(), 1) << "the end of the gesture was never announced";
    EXPECT_DOUBLE_EQ(model.param(u"b1"_s, u"gain"_s).toDouble(), 2.5);
}

TEST_F(SessionModelTest, WhatIsDrawnFromTheSessionFollowsADragOnceATick)
{
    // A curve or a readout is a binding on paramRevision and params(). Waiting
    // for the button to come up before it moved made a setting impossible
    // to aim by its picture; telling it of every mouse event would draw it
    // far more often than the screen shows it.
    core::MockEngine engine;
    ui::SessionModel model;
    model.attach(&engine);
    (void)openSessionWithBlock(engine, model);

    QSignalSpy redraw(&model, &ui::SessionModel::paramRevisionChanged);
    QSignalSpy changed(&model, &ui::SessionModel::changed);

    model.setParam(u"b1"_s, u"gain"_s, 1.5, true);
    EXPECT_EQ(redraw.count(), 1) << "the first value of a drag was not drawn at once";
    EXPECT_DOUBLE_EQ(model.params(u"b1"_s).value(u"gain"_s).toDouble(), 1.5);

    // Many mouse events inside one tick are one redraw, of the last of them.
    for (int step = 0; step < 20; ++step) {
        model.setParam(u"b1"_s, u"gain"_s, 2.0 + step * 0.05, true);
    }
    EXPECT_EQ(redraw.count(), 1) << "a redraw went out before the tick came round";
    ASSERT_TRUE(redraw.wait(1000)) << "the tick never drew the dragged value";
    EXPECT_EQ(redraw.count(), 2);
    EXPECT_DOUBLE_EQ(model.params(u"b1"_s).value(u"gain"_s).toDouble(), 2.95);

    // **Still not news** for what rebuilds the canvas's tiles and wires.
    EXPECT_EQ(changed.count(), 0) << "a value mid-drag was announced as an edit";

    model.commitParam(u"b1"_s, u"gain"_s);
    EXPECT_EQ(changed.count(), 1);
    EXPECT_EQ(redraw.count(), 3) << "letting go was not drawn";
}

TEST_F(SessionModelTest, AWholeDragIsOneStepToUndo)
{
    // What the menu's Undo takes back is the drag, not the last tick of
    // it: the session goes back to where it was before the button went down.
    core::MockEngine engine;
    ui::SessionModel model;
    model.attach(&engine);
    (void)openSessionWithBlock(engine, model);
    model.setParam(u"b1"_s, u"gain"_s, 0.5);
    // Another kind of edit in between, so the drag cannot join the one
    // before it however quickly it follows.
    model.setParam(u"b1"_s, u"muted"_s, false);

    const auto dragFor = [&model](int from, int to) {
        for (int step = from; step <= to; ++step) {
            model.setParam(u"b1"_s, u"gain"_s, 0.5 + step * 0.1, true);
            QSignalSpy redraw(&model, &ui::SessionModel::paramRevisionChanged);
            (void)redraw.wait(100);
        }
    };
    dragFor(1, 6);
    // **A hand resting on the slider** to look at the curve, twice as long
    // as the engine waits before it counts an edit as a new step.
    QElapsedTimer resting;
    resting.start();
    while (resting.elapsed() < 1000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    dragFor(7, 12);
    model.commitParam(u"b1"_s, u"gain"_s);
    ASSERT_GT(model.sentParamCount(), 6) << "the drag did not reach the engine as it went";

    model.undo();
    EXPECT_DOUBLE_EQ(model.param(u"b1"_s, u"gain"_s).toDouble(), 0.5)
        << "undo took back only part of the drag, the part after the rest";
    EXPECT_EQ(model.param(u"b1"_s, u"muted"_s), QVariant{false}) << "the edit before the drag went with it";
    model.undo();
    EXPECT_FALSE(model.param(u"b1"_s, u"muted"_s).isValid());
    EXPECT_DOUBLE_EQ(model.param(u"b1"_s, u"gain"_s).toDouble(), 0.5);
}

TEST_F(SessionModelTest, ADragIsLetGoOfWhenItEnds)
{
    // Held open only while it lasts: after the button comes up, or an
    // undo, nothing is sent again, or it would put back what was undone.
    core::MockEngine engine;
    ui::SessionModel model;
    model.attach(&engine);
    (void)openSessionWithBlock(engine, model);

    const auto restFor = [](int ms) {
        QElapsedTimer resting;
        resting.start();
        while (resting.elapsed() < ms) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }
    };

    model.setParam(u"b1"_s, u"gain"_s, 1.5, true);
    restFor(600);
    EXPECT_GE(model.sentParamCount(), 2) << "a resting drag was not held open";

    model.commitParam(u"b1"_s, u"gain"_s);
    const int committed = model.sentParamCount();
    restFor(600);
    EXPECT_EQ(model.sentParamCount(), committed) << "a drag that ended was still being sent";

    model.setParam(u"b1"_s, u"gain"_s, 2.5, true);
    model.undo();
    const int undone = model.sentParamCount();
    restFor(600);
    EXPECT_EQ(model.sentParamCount(), undone) << "a drag kept going after an undo";
}

// --- undo and redo in the window ---

TEST_F(SessionModelTest, UndoAndRedoFollowTheEnginesHistoryAndSayWhatTheyWouldDo)
{
    core::MockEngine engine;
    ui::SessionModel model;
    model.attach(&engine);
    (void)openSessionWithBlock(engine, model);

    ASSERT_TRUE(model.canUndo()) << "adding a block left nothing to undo";
    EXPECT_FALSE(model.canRedo());
    EXPECT_EQ(model.undoText(), u"Undo Add Block"_s);

    model.undo();
    EXPECT_TRUE(model.blockIds().isEmpty()) << "undo did not take the block back";
    EXPECT_FALSE(model.canUndo());
    ASSERT_TRUE(model.canRedo());
    EXPECT_EQ(model.redoText(), u"Redo Add Block"_s);

    model.redo();
    EXPECT_EQ(model.blockIds(), QStringList{u"b1"_s}) << "redo did not put the block back";
    EXPECT_TRUE(model.canUndo());
    EXPECT_FALSE(model.canRedo());
}

TEST_F(SessionModelTest, TheHistoryIsTheSessionShowing)
{
    // Switching tabs switches whose history the menu follows: an Undo
    // that took back something in a session nobody is looking at would be
    // a change nobody saw being made.
    core::MockEngine engine;
    ui::SessionModel model;
    model.attach(&engine);
    const QString edited = openSessionWithBlock(engine, model);
    ASSERT_TRUE(model.canUndo());

    QSignalSpy opened(&engine, &core::EngineFacade::sessionOpened);
    engine.createSession(u"Fresh"_s);
    ASSERT_EQ(opened.count(), 1);
    model.setSessionId(opened.back().at(0).toString());
    EXPECT_FALSE(model.canUndo()) << "a fresh session offered the other one's undo";
    EXPECT_EQ(model.undoText(), u"Undo"_s);

    model.setSessionId(edited);
    EXPECT_TRUE(model.canUndo()) << "going back to the edited session lost its history";
}

TEST_F(SessionModelTest, UndoingInTheMiddleOfADragSendsTheDragFirst)
{
    // Whatever was still pending is part of the gesture being undone; left
    // in the queue it would arrive after the undo and put it back.
    core::MockEngine engine;
    ui::SessionModel model;
    model.attach(&engine);
    (void)openSessionWithBlock(engine, model);
    model.setParam(u"b1"_s, u"gain"_s, 1.5, true);
    model.setParam(u"b1"_s, u"gain"_s, 2.5, true);

    model.undo();
    QCoreApplication::processEvents();
    EXPECT_FALSE(model.param(u"b1"_s, u"gain"_s).isValid()) << "the drag's last value came back after the undo";
}

// --- wires ---

namespace
{

/// A session with a source, an effect and an output in it, unwired.
[[nodiscard]] QString openSessionWithAChainsWorthOfBlocks(core::MockEngine &engine, ui::SessionModel &model)
{
    QSignalSpy opened(&engine, &core::EngineFacade::sessionOpened);
    engine.createSession(u"Wires"_s);
    const QString id = opened.isEmpty() ? QString{} : opened.back().at(0).toString();
    model.setSessionId(id);
    engine.applyEdit(id, addBlockEdit(u"b1"_s, u"source.device"_s));
    engine.applyEdit(id, addBlockEdit(u"b2"_s, u"effect.volume"_s));
    engine.applyEdit(id, addBlockEdit(u"b3"_s, u"output.device"_s));
    return id;
}

} // namespace

TEST_F(SessionModelTest, AWireGoesToTheEngineAndComesBackInTheList)
{
    core::MockEngine engine;
    ui::SessionModel model;
    model.attach(&engine);
    (void)openSessionWithAChainsWorthOfBlocks(engine, model);

    EXPECT_TRUE(model.connectBlocks(u"b1"_s, 0, u"b2"_s, 0));
    EXPECT_EQ(model.wireCount(), 1);
    ASSERT_EQ(model.wires().size(), 1);
    const QVariantMap wire = model.wires().at(0).toMap();
    EXPECT_EQ(wire.value(u"fromBlock"_s).toString(), u"b1"_s);
    EXPECT_EQ(wire.value(u"toPort"_s).toInt(), 0);
    EXPECT_TRUE(model.hasWire(u"b1"_s, 0, u"b2"_s, 0));

    model.disconnectBlocks(u"b1"_s, 0, u"b2"_s, 0);
    EXPECT_EQ(model.wireCount(), 0);
}

TEST_F(SessionModelTest, TheRefusalIsTheEnginesOwnAndIsGivenBeforeAnythingIsSent)
{
    core::MockEngine engine;
    ui::SessionModel model;
    model.attach(&engine);
    (void)openSessionWithAChainsWorthOfBlocks(engine, model);

    ASSERT_TRUE(model.connectBlocks(u"b1"_s, 0, u"b2"_s, 0));
    ASSERT_TRUE(model.connectBlocks(u"b2"_s, 0, u"b3"_s, 0));

    EXPECT_EQ(model.wireRefusal(u"b1"_s, 0, u"b2"_s, 0), u"wireAlreadyExists"_s);
    EXPECT_EQ(model.wireRefusal(u"b2"_s, 0, u"b2"_s, 1), u"wireToItself"_s);
    EXPECT_EQ(model.wireRefusal(u"b1"_s, 0, u"b9"_s, 0), u"wireBlockNotFound"_s);
    EXPECT_EQ(model.wireRefusal(u"b3"_s, 0, u"b1"_s, 0), u"wireWouldCycle"_s);
    EXPECT_TRUE(model.wireRefusal(u"b1"_s, 1, u"b2"_s, 1).isEmpty());

    // **Refused means nothing was sent**, so the session is exactly as it
    // was and the canvas can put the wire back under the pointer.
    const int before = model.wireCount();
    EXPECT_FALSE(model.connectBlocks(u"b3"_s, 0, u"b1"_s, 0));
    EXPECT_EQ(model.wireCount(), before);
}

TEST_F(SessionModelTest, ThePortEveryWireArrivesAtIsCounted)
{
    core::MockEngine engine;
    ui::SessionModel model;
    model.attach(&engine);
    (void)openSessionWithAChainsWorthOfBlocks(engine, model);

    ASSERT_TRUE(model.connectBlocks(u"b1"_s, 0, u"b3"_s, 0));
    EXPECT_EQ(model.arrivingCount(u"b3"_s, 0), 1);
    // Two wires into one port is the engine's implicit sum, which is what
    // the merge marker on the canvas is drawn from.
    ASSERT_TRUE(model.connectBlocks(u"b2"_s, 0, u"b3"_s, 0));
    EXPECT_EQ(model.arrivingCount(u"b3"_s, 0), 2);
    EXPECT_EQ(model.arrivingCount(u"b3"_s, 1), 0);
    EXPECT_EQ(model.wiresTouching(u"b3"_s).size(), 2);
    EXPECT_EQ(model.wiresTouching(u"b1"_s).size(), 1);
}

TEST_F(SessionModelTest, ALinkMovedToALaneThatWillNotHaveItEndsUpWhereItStarted)
{
    core::MockEngine engine;
    ui::SessionModel model;
    model.attach(&engine);
    (void)openSessionWithAChainsWorthOfBlocks(engine, model);

    ASSERT_TRUE(model.connectLanes(u"b1"_s, u"b2"_s, 2));
    EXPECT_EQ(model.wireCount(), 2);

    // Onto the lane it is already on: not a move at all, and the link is
    // left exactly where it was rather than taken off and drawn again.
    EXPECT_FALSE(model.moveLane(u"b1"_s, u"b2"_s, u"b2"_s, 2));
    EXPECT_TRUE(model.hasWire(u"b1"_s, 0, u"b2"_s, 0));
    EXPECT_TRUE(model.hasWire(u"b1"_s, 1, u"b2"_s, 1));
    EXPECT_EQ(model.wireCount(), 2);

    // And onto one that will have it, which moves **every channel of it**,
    // because half a link is not something anybody asked for.
    EXPECT_TRUE(model.moveLane(u"b1"_s, u"b2"_s, u"b3"_s, 2));
    EXPECT_FALSE(model.hasWire(u"b1"_s, 0, u"b2"_s, 0));
    EXPECT_TRUE(model.hasWire(u"b1"_s, 0, u"b3"_s, 0));
    EXPECT_TRUE(model.hasWire(u"b1"_s, 1, u"b3"_s, 1));
    EXPECT_EQ(model.wireCount(), 2);
}

TEST_F(SessionModelTest, ALinkIsDrawnAndTakenOutAChannelAtATimeButAskedForAsOne)
{
    core::MockEngine engine;
    ui::SessionModel model;
    model.attach(&engine);
    (void)openSessionWithAChainsWorthOfBlocks(engine, model);

    // **One call, every channel**: the session holds a wire a
    // channel, because a link only ever joins two ports of the same channel
    // position, and what a person drew is one link.
    ASSERT_TRUE(model.connectLanes(u"b1"_s, u"b3"_s, 2));
    EXPECT_EQ(model.wireCount(), 2);
    EXPECT_EQ(model.arrivingLinks(u"b3"_s), 1) << "the channels of one link are not two links arriving";

    // b1 is a source and b2 an effect, and both can feed the output: two
    // links into one lane, which is the sum the merge marker is drawn for.
    ASSERT_TRUE(model.connectLanes(u"b2"_s, u"b3"_s, 2));
    EXPECT_EQ(model.wireCount(), 4);
    EXPECT_EQ(model.arrivingLinks(u"b3"_s), 2);

    model.disconnectLanes(u"b1"_s, u"b3"_s);
    EXPECT_EQ(model.wireCount(), 2) << "taking a link out left some of its channels behind";
    EXPECT_FALSE(model.hasWire(u"b1"_s, 0, u"b3"_s, 0));
    EXPECT_FALSE(model.hasWire(u"b1"_s, 1, u"b3"_s, 1));
    EXPECT_EQ(model.arrivingLinks(u"b3"_s), 1);
}

TEST_F(SessionModelTest, RemovingABlockInTheMiddleOfALaneWiresItsNeighboursTogether)
{
    core::MockEngine engine;
    ui::SessionModel model;
    model.attach(&engine);
    (void)openSessionWithAChainsWorthOfBlocks(engine, model);
    ASSERT_TRUE(model.autoConnect()) << "a new session wires itself up by default";

    ASSERT_TRUE(model.connectBlocks(u"b1"_s, 0, u"b2"_s, 0));
    ASSERT_TRUE(model.connectBlocks(u"b2"_s, 0, u"b3"_s, 0));

    model.removeBlock(u"b2"_s);
    EXPECT_EQ(model.blockIds(), (QStringList{u"b1"_s, u"b3"_s}));
    EXPECT_TRUE(model.hasWire(u"b1"_s, 0, u"b3"_s, 0)) << "the lane was left with a hole in it";
    EXPECT_EQ(model.wireCount(), 1);
}

TEST_F(SessionModelTest, WithAutomaticConnectionsOffARemovedBlockLeavesTheGap)
{
    core::MockEngine engine;
    ui::SessionModel model;
    model.attach(&engine);
    (void)openSessionWithAChainsWorthOfBlocks(engine, model);

    ASSERT_TRUE(model.connectBlocks(u"b1"_s, 0, u"b2"_s, 0));
    ASSERT_TRUE(model.connectBlocks(u"b2"_s, 0, u"b3"_s, 0));
    model.setAutoConnect(false);
    ASSERT_FALSE(model.autoConnect());

    model.removeBlock(u"b2"_s);
    EXPECT_EQ(model.wireCount(), 0) << "the session joined two blocks up with the setting switched off";
}

TEST_F(SessionModelTest, WithNoSessionNothingIsSentAndNothingIsClaimed)
{
    core::MockEngine engine;
    ui::SessionModel model;
    model.attach(&engine);

    model.setParam(u"b1"_s, u"gain"_s, 1.0);
    model.renameBlock(u"b1"_s, u"Voice"_s);
    model.setBlockEnabled(u"b1"_s, false);
    EXPECT_FALSE(model.connectBlocks(u"b1"_s, 0, u"b2"_s, 0));

    EXPECT_EQ(model.sentParamCount(), 0);
    EXPECT_TRUE(model.blockIds().isEmpty());
    EXPECT_TRUE(model.block(u"b1"_s).isEmpty());
    EXPECT_TRUE(model.wires().isEmpty());
}
