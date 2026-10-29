// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The canvas's view of the session: that it follows the session row by
// row rather than throwing the tiles away every time anything changes, and
// that adding, moving and removing a block go to the engine and come back as
// patches like every other edit.
//
// And what it tells the meter hub: every meter block's `peakHold`,
// whenever the session opens, a block arrives or the parameter moves —
// rather than only the block whose settings panel somebody has opened.

#include "blockmodel.h"
#include "core/mockengine.h"
#include "meterhub.h"
#include "meters.h"
#include "peakhold.h"
#include "sessionmodel.h"

#include <QAbstractItemModelTester>
#include <QCoreApplication>
#include <QSignalSpy>
#include <gtest/gtest.h>

#include <array>
#include <memory>

using namespace rb;
using namespace Qt::StringLiterals;

namespace
{

/// **A timer needs an event loop to exist in.** The session model starts one
/// for a parameter being dragged, and without an application QTimer::start()
/// does not take.
class BlockModelTest : public ::testing::Test
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
    static std::array<char, 7> s_name;
    static std::array<char *, 2> s_argv;
    static int s_argc;
    static std::unique_ptr<QCoreApplication> s_application;
};

std::array<char, 7> BlockModelTest::s_name{'b', 'l', 'o', 'c', 'k', 's', '\0'};
std::array<char *, 2> BlockModelTest::s_argv{};
int BlockModelTest::s_argc = 1;
std::unique_ptr<QCoreApplication> BlockModelTest::s_application;

/// A session in the mock, followed by \a model.
[[nodiscard]] QString openSession(core::MockEngine &engine, ui::SessionModel &model)
{
    QSignalSpy opened(&engine, &core::EngineFacade::sessionOpened);
    engine.createSession(u"Canvas"_s);
    EXPECT_EQ(opened.count(), 1);
    const QString id = opened.isEmpty() ? QString{} : opened.back().at(0).toString();
    model.setSessionId(id);
    return id;
}

[[nodiscard]] QString roleAt(const ui::BlockModel &blocks, int row, ui::BlockModel::Role role)
{
    return blocks.data(blocks.index(row), role).toString();
}

} // namespace

TEST_F(BlockModelTest, ItHoldsTheSessionsBlocksInTheSessionsOrder)
{
    core::MockEngine engine;
    ui::SessionModel model;
    model.attach(&engine);
    ui::BlockModel blocks;
    // Qt's own conformance check: a model that lies about its rows takes a
    // view down with it, and the lie is never where the crash is.
    QAbstractItemModelTester tester(&blocks, QAbstractItemModelTester::FailureReportingMode::Warning);
    blocks.setSession(&model);

    const QString id = openSession(engine, model);
    ASSERT_FALSE(id.isEmpty());
    EXPECT_EQ(blocks.rowCount(), 0);

    const QString first = model.addBlock(u"effect.volume"_s, QPointF{16, 24});
    const QString second = model.addBlock(u"meter.peakrms"_s, QPointF{160, 24});

    ASSERT_EQ(blocks.rowCount(), 2);
    EXPECT_EQ(roleAt(blocks, 0, ui::BlockModel::BlockIdRole), first);
    EXPECT_EQ(roleAt(blocks, 1, ui::BlockModel::BlockIdRole), second);
    EXPECT_EQ(roleAt(blocks, 0, ui::BlockModel::TypeIdRole), u"effect.volume"_s);
    EXPECT_EQ(blocks.data(blocks.index(0), ui::BlockModel::PositionRole).toPointF(), QPointF(16, 24));
    EXPECT_TRUE(blocks.data(blocks.index(0), ui::BlockModel::BlockEnabledRole).toBool());
    EXPECT_FALSE(blocks.data(blocks.index(0), ui::BlockModel::UnknownTypeRole).toBool());

    EXPECT_EQ(blocks.rowOf(second), 1);
    EXPECT_EQ(blocks.rowOf(u"nobody"_s), -1);
    EXPECT_EQ(blocks.at(1).value("type"_L1).toString(), u"meter.peakrms"_s);
    EXPECT_TRUE(blocks.at(7).isEmpty());
}

TEST_F(BlockModelTest, AMoveTouchesOneRowAndLeavesTheOthersAlone)
{
    // **The whole reason this is a list model.** Thirty tiles with live
    // faces in them are thirty delegates, thirty meters and thirty
    // bindings; a session that said "something changed" and got all of them
    // rebuilt would lose the selection, the focus ring and the drag that
    // caused it.
    core::MockEngine engine;
    ui::SessionModel model;
    model.attach(&engine);
    ui::BlockModel blocks;
    blocks.setSession(&model);
    const QString id = openSession(engine, model);
    ASSERT_FALSE(id.isEmpty());

    model.addBlock(u"effect.volume"_s, QPointF{0, 0});
    const QString second = model.addBlock(u"effect.volume"_s, QPointF{160, 0});
    ASSERT_EQ(blocks.rowCount(), 2);

    QSignalSpy reset(&blocks, &QAbstractItemModel::modelReset);
    QSignalSpy changed(&blocks, &QAbstractItemModel::dataChanged);
    QSignalSpy inserted(&blocks, &QAbstractItemModel::rowsInserted);
    QSignalSpy removed(&blocks, &QAbstractItemModel::rowsRemoved);

    model.moveBlock(second, QPointF{320, 80});

    EXPECT_EQ(reset.count(), 0);
    EXPECT_EQ(inserted.count(), 0);
    EXPECT_EQ(removed.count(), 0);
    ASSERT_EQ(changed.count(), 1);
    EXPECT_EQ(changed.back().at(0).toModelIndex().row(), 1);
    EXPECT_EQ(blocks.data(blocks.index(1), ui::BlockModel::PositionRole).toPointF(), QPointF(320, 80));
    EXPECT_EQ(blocks.data(blocks.index(0), ui::BlockModel::PositionRole).toPointF(), QPointF(0, 0));
}

TEST_F(BlockModelTest, ARemovedBlockTakesItsRowAndAnUndoPutsItBackWhereItWas)
{
    core::MockEngine engine;
    ui::SessionModel model;
    model.attach(&engine);
    ui::BlockModel blocks;
    blocks.setSession(&model);
    const QString id = openSession(engine, model);

    const QString first = model.addBlock(u"effect.volume"_s, QPointF{0, 0});
    const QString second = model.addBlock(u"meter.peakrms"_s, QPointF{160, 0});
    const QString third = model.addBlock(u"effect.volume"_s, QPointF{320, 0});
    ASSERT_EQ(blocks.rowCount(), 3);

    QSignalSpy removed(&blocks, &QAbstractItemModel::rowsRemoved);
    model.removeBlock(second);

    ASSERT_EQ(removed.count(), 1);
    EXPECT_EQ(removed.back().at(1).toInt(), 1);
    ASSERT_EQ(blocks.rowCount(), 2);
    EXPECT_EQ(roleAt(blocks, 0, ui::BlockModel::BlockIdRole), first);
    EXPECT_EQ(roleAt(blocks, 1, ui::BlockModel::BlockIdRole), third);

    // **Undo puts it back in the middle, not at the end.** The order the
    // session holds is the order the file holds and the order the canvas
    // tabs through, so a block that comes back at the end has moved.
    engine.undo(id);
    ASSERT_EQ(blocks.rowCount(), 3);
    EXPECT_EQ(roleAt(blocks, 1, ui::BlockModel::BlockIdRole), second);
    EXPECT_EQ(roleAt(blocks, 2, ui::BlockModel::BlockIdRole), third);
}

TEST_F(BlockModelTest, EveryEditGoesToTheEngineAndComesBackAsAPatch)
{
    core::MockEngine engine;
    ui::SessionModel model;
    model.attach(&engine);
    const QString id = openSession(engine, model);

    QSignalSpy patched(&engine, &core::EngineFacade::sessionPatch);

    const QString block = model.addBlock(u"effect.volume"_s, QPointF{8, 8});
    ASSERT_FALSE(block.isEmpty());
    EXPECT_EQ(patched.count(), 1);

    model.moveBlock(block, QPointF{24, 8});
    model.renameBlock(block, u"Podcast level"_s);
    model.setBlockEnabled(block, false);
    EXPECT_EQ(patched.count(), 4);

    const QVariantMap after = model.block(block);
    EXPECT_EQ(after.value("position"_L1).toPointF(), QPointF(24, 8));
    EXPECT_EQ(after.value("nickname"_L1).toString(), u"Podcast level"_s);
    EXPECT_FALSE(after.value("enabled"_L1).toBool());

    // Several at once, which is what Del on a multiple selection does.
    const QString other = model.addBlock(u"meter.peakrms"_s, QPointF{160, 8});
    model.removeBlocks({block, other});
    EXPECT_TRUE(model.blockIds().isEmpty());
}

TEST_F(BlockModelTest, ABlockCannotBeAddedWithNoTypeAndNoSession)
{
    core::MockEngine engine;
    ui::SessionModel model;
    model.attach(&engine);

    // Nothing is open, so there is nowhere to put a block. Answering with
    // an empty id is what lets the canvas say so rather than select a tile
    // that is not there.
    EXPECT_TRUE(model.addBlock(u"effect.volume"_s, QPointF{}).isEmpty());

    const QString id = openSession(engine, model);
    ASSERT_FALSE(id.isEmpty());
    EXPECT_TRUE(model.addBlock(QString{}, QPointF{}).isEmpty());
    EXPECT_TRUE(model.blockIds().isEmpty());
}

TEST_F(BlockModelTest, AMeterBlocksHoldReachesTheHubWithNoPanelEverOpened)
{
    // Until now the only thing that told the hub what a meter holds
    // for was the block's settings panel, so a meter whose panel nobody had
    // opened held for the hub's default — in a session where its `peakHold`
    // was right there. Nothing in this test makes a panel or a face.
    core::MockEngine engine;
    ui::SessionModel model;
    model.attach(&engine);
    const QString id = openSession(engine, model);
    ASSERT_FALSE(id.isEmpty());

    // A session as it is on disk: two meters with different holds, and
    // something that is not a meter at all between them.
    const QString in = model.addBlock(u"meter.peakrms"_s, QPointF{0, 0});
    const QString volume = model.addBlock(u"effect.volume"_s, QPointF{160, 0});
    const QString out = model.addBlock(u"meter.peakrms"_s, QPointF{320, 0});
    model.setParam(in, u"peakHold"_s, u"1s"_s);
    model.setParam(out, u"peakHold"_s, u"5s"_s);

    // Opened fresh: the model starts following a session that already holds
    // all of it, which is what happens when a file is opened.
    ui::Meters meters;
    ui::BlockModel blocks;
    blocks.setMeters(&meters);
    blocks.setSession(&model);

    EXPECT_EQ(meters.holdMs(in), 1000);
    EXPECT_EQ(meters.holdMs(out), 5000);
    // Both of them from the moment it opened, not one of them.
    EXPECT_GT(meters.decayDbPerSecond(in), 0.0);
    EXPECT_GT(meters.decayDbPerSecond(out), 0.0);
    // A block that is not a meter has no hold of its own to speak of, and
    // is left on the default rather than given somebody else's.
    EXPECT_EQ(meters.holdMs(volume), static_cast<int>(ui::MeterHub::kHoldMs));
}

TEST_F(BlockModelTest, AMeterAddedToARunningSessionAndOneRetunedAreBothToldAbout)
{
    core::MockEngine engine;
    ui::SessionModel model;
    model.attach(&engine);
    ui::Meters meters;
    ui::BlockModel blocks;
    blocks.setMeters(&meters);
    blocks.setSession(&model);
    const QString id = openSession(engine, model);
    ASSERT_FALSE(id.isEmpty());

    // A meter dropped on the canvas of a session already open. It has no
    // tile yet, let alone a panel.
    const QString late = model.addBlock(u"meter.peakrms"_s, QPointF{0, 0});
    ASSERT_FALSE(late.isEmpty());
    EXPECT_EQ(meters.holdMs(late), ui::peakHold::holdMs(ui::peakHold::defaultChoice()));

    // And the parameter changed from anywhere — an undo, a second window,
    // the panel — not only from the panel that happens to be on screen.
    model.setParam(late, u"peakHold"_s, u"5s"_s);
    EXPECT_EQ(meters.holdMs(late), 5000);

    // "Forever" is a line that never falls rather than a very long wait, so
    // it is the fall rate that has to arrive, not the wait.
    model.setParam(late, u"peakHold"_s, u"forever"_s);
    EXPECT_DOUBLE_EQ(meters.decayDbPerSecond(late), 0.0);
}

TEST_F(BlockModelTest, TheHubIsToldAgainAfterItChangesSession)
{
    // The hub drops every block's hold when it is pointed at another
    // session, because block ids are only unique inside one. Whether a
    // meter then holds for its own setting must not come down to whether
    // the session's blocks arrived before or after that.
    core::MockEngine engine;
    ui::SessionModel model;
    model.attach(&engine);
    ui::Meters meters;
    ui::BlockModel blocks;
    blocks.setMeters(&meters);
    blocks.setSession(&model);
    const QString id = openSession(engine, model);
    ASSERT_FALSE(id.isEmpty());

    const QString meter = model.addBlock(u"meter.peakrms"_s, QPointF{0, 0});
    model.setParam(meter, u"peakHold"_s, u"1s"_s);
    ASSERT_EQ(meters.holdMs(meter), 1000);

    meters.watch(id);
    EXPECT_EQ(meters.holdMs(meter), 1000) << "the hub forgot the hold and nobody said it again";
}

TEST_F(BlockModelTest, WithNoMetersItFollowsTheSessionAnyway)
{
    // A model built in C++ with no QML around it has no window's meters to
    // find. It is still a list of blocks.
    core::MockEngine engine;
    ui::SessionModel model;
    model.attach(&engine);
    ui::BlockModel blocks;
    blocks.setSession(&model);
    const QString id = openSession(engine, model);
    ASSERT_FALSE(id.isEmpty());

    EXPECT_EQ(blocks.meters(), nullptr);
    model.addBlock(u"meter.peakrms"_s, QPointF{0, 0});
    EXPECT_EQ(blocks.rowCount(), 1);
}

TEST_F(BlockModelTest, WithNoSessionItIsEmptyRatherThanStale)
{
    core::MockEngine engine;
    ui::SessionModel model;
    model.attach(&engine);
    ui::BlockModel blocks;
    blocks.setSession(&model);
    const QString id = openSession(engine, model);
    ASSERT_FALSE(id.isEmpty());
    model.addBlock(u"effect.volume"_s, QPointF{});
    ASSERT_EQ(blocks.rowCount(), 1);

    QSignalSpy counted(&blocks, &ui::BlockModel::countChanged);
    // Closing a session is not a session with one block still on the canvas.
    model.setSessionId(QString{});
    EXPECT_EQ(blocks.rowCount(), 0);
    EXPECT_EQ(counted.count(), 1);

    blocks.setSession(nullptr);
    EXPECT_EQ(blocks.session(), nullptr);
    EXPECT_EQ(blocks.rowCount(), 0);
}
