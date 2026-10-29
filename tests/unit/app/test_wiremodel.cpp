// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The canvas's view of the session's links: that a row is a
// link and not one of the channel wires it is carried by, that it follows
// them row by row rather than rebuilding every curve when anything changes,
// and that a lane several links arrive at says so, because that is an
// implicit sum in the engine and the canvas has to draw a marker on it.

#include "core/mockengine.h"
#include "sessionmodel.h"
#include "wiremodel.h"

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
class WireModelTest : public ::testing::Test
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

std::array<char, 6> WireModelTest::s_name{'w', 'i', 'r', 'e', 's', '\0'};
std::array<char *, 2> WireModelTest::s_argv{};
int WireModelTest::s_argc = 1;
std::unique_ptr<QCoreApplication> WireModelTest::s_application;

/// A session in the mock with four blocks in it, followed by \a model.
QString openSessionWithBlocks(core::MockEngine &engine, ui::SessionModel &model)
{
    QSignalSpy opened(&engine, &core::EngineFacade::sessionOpened);
    engine.createSession(u"Wires"_s);
    EXPECT_EQ(opened.count(), 1);
    const QString id = opened.isEmpty() ? QString{} : opened.back().at(0).toString();
    model.setSessionId(id);
    // Automatic connections would wire the lane back together on a removal,
    // which is a session model test; here the wires are drawn by hand.
    model.setAutoConnect(false);
    model.addBlock(u"source.device"_s, QPointF{0.0, 0.0});
    model.addBlock(u"effect.volume"_s, QPointF{200.0, 0.0});
    model.addBlock(u"meter.peakrms"_s, QPointF{400.0, 0.0});
    model.addBlock(u"output.device"_s, QPointF{600.0, 0.0});
    return id;
}

[[nodiscard]] QVariant roleAt(const ui::WireModel &wires, int row, ui::WireModel::Role role)
{
    return wires.data(wires.index(row), role);
}

} // namespace

TEST_F(WireModelTest, ItHoldsTheSessionsWiresInTheSessionsOrder)
{
    core::MockEngine engine;
    ui::SessionModel session;
    session.attach(&engine);
    (void)openSessionWithBlocks(engine, session);

    ui::WireModel wires;
    const QAbstractItemModelTester tester(&wires, QAbstractItemModelTester::FailureReportingMode::Warning);
    wires.setSession(&session);
    EXPECT_EQ(wires.count(), 0);

    ASSERT_TRUE(session.connectBlocks(u"b1"_s, 0, u"b2"_s, 0));
    ASSERT_TRUE(session.connectBlocks(u"b2"_s, 1, u"b3"_s, 1));

    ASSERT_EQ(wires.count(), 2);
    EXPECT_EQ(roleAt(wires, 0, ui::WireModel::FromBlockRole).toString(), u"b1"_s);
    EXPECT_EQ(roleAt(wires, 0, ui::WireModel::ToBlockRole).toString(), u"b2"_s);
    EXPECT_EQ(roleAt(wires, 0, ui::WireModel::ChannelsRole).toInt(), 1);
    EXPECT_EQ(roleAt(wires, 1, ui::WireModel::FromBlockRole).toString(), u"b2"_s);
    EXPECT_EQ(wires.at(1).value(u"toBlock"_s).toString(), u"b3"_s);
    EXPECT_TRUE(wires.at(9).isEmpty()) << "a row that is not there is not a link";

    // **The channels of one link are one row.** The session holds a wire a
    // channel, because a link only ever joins two ports of the same channel
    // position; what a person drew is the one thing this model shows.
    ASSERT_TRUE(session.connectBlocks(u"b1"_s, 1, u"b2"_s, 1));
    ASSERT_EQ(wires.count(), 2) << "a second channel should not be a second link";
    EXPECT_EQ(roleAt(wires, 0, ui::WireModel::ChannelsRole).toInt(), 2);
}

TEST_F(WireModelTest, AWireThatDidNotChangeKeepsItsRow)
{
    // **Why this is a list model at all.** A canvas that read the wires
    // again whenever the session said something changed would throw every
    // curve away sixty times a second while a tile is dragged, restarting
    // the flow animation on all of them and losing which one was selected.
    core::MockEngine engine;
    ui::SessionModel session;
    session.attach(&engine);
    (void)openSessionWithBlocks(engine, session);

    ui::WireModel wires;
    wires.setSession(&session);
    ASSERT_TRUE(session.connectBlocks(u"b1"_s, 0, u"b2"_s, 0));
    ASSERT_TRUE(session.connectBlocks(u"b2"_s, 0, u"b3"_s, 0));

    QSignalSpy inserted(&wires, &ui::WireModel::rowsInserted);
    QSignalSpy removed(&wires, &ui::WireModel::rowsRemoved);
    QSignalSpy reset(&wires, &ui::WireModel::modelReset);

    // A tile moved is a patch that reaches every row of this model.
    session.moveBlock(u"b2"_s, QPointF{260.0, 40.0});
    session.moveBlock(u"b2"_s, QPointF{280.0, 60.0});

    EXPECT_EQ(inserted.count(), 0);
    EXPECT_EQ(removed.count(), 0);
    EXPECT_EQ(reset.count(), 0);
    EXPECT_EQ(wires.count(), 2);
}

TEST_F(WireModelTest, ALaneSeveralLinksArriveAtSaysSo)
{
    core::MockEngine engine;
    ui::SessionModel session;
    session.attach(&engine);
    (void)openSessionWithBlocks(engine, session);

    ui::WireModel wires;
    wires.setSession(&session);
    ASSERT_TRUE(session.connectLanes(u"b1"_s, u"b4"_s, 2));
    EXPECT_EQ(wires.count(), 1);
    EXPECT_FALSE(roleAt(wires, 0, ui::WireModel::MergingRole).toBool())
        << "the two channels of one link are not two links arriving";

    // A second link into the same lane: the engine sums them, and both
    // links have to say so rather than only the newcomer.
    QSignalSpy changed(&wires, &ui::WireModel::dataChanged);
    ASSERT_TRUE(session.connectLanes(u"b2"_s, u"b4"_s, 2));
    EXPECT_TRUE(roleAt(wires, 0, ui::WireModel::MergingRole).toBool());
    EXPECT_TRUE(roleAt(wires, 1, ui::WireModel::MergingRole).toBool());
    EXPECT_GE(changed.count(), 1) << "the link that was already there was never told";

    session.disconnectLanes(u"b2"_s, u"b4"_s);
    EXPECT_EQ(wires.count(), 1);
    EXPECT_FALSE(roleAt(wires, 0, ui::WireModel::MergingRole).toBool());
}

TEST_F(WireModelTest, ALinkTakenOutTakesItsRowAndNoOther)
{
    core::MockEngine engine;
    ui::SessionModel session;
    session.attach(&engine);
    (void)openSessionWithBlocks(engine, session);

    ui::WireModel wires;
    const QAbstractItemModelTester tester(&wires, QAbstractItemModelTester::FailureReportingMode::Warning);
    wires.setSession(&session);
    ASSERT_TRUE(session.connectBlocks(u"b1"_s, 0, u"b2"_s, 0));
    ASSERT_TRUE(session.connectBlocks(u"b2"_s, 0, u"b3"_s, 0));
    ASSERT_TRUE(session.connectBlocks(u"b3"_s, 0, u"b4"_s, 0));

    session.disconnectBlocks(u"b2"_s, 0, u"b3"_s, 0);
    ASSERT_EQ(wires.count(), 2);
    EXPECT_EQ(roleAt(wires, 0, ui::WireModel::FromBlockRole).toString(), u"b1"_s);
    EXPECT_EQ(roleAt(wires, 1, ui::WireModel::FromBlockRole).toString(), u"b3"_s);
}

TEST_F(WireModelTest, WithNoSessionItIsEmptyRatherThanWrong)
{
    ui::WireModel wires;
    const QAbstractItemModelTester tester(&wires, QAbstractItemModelTester::FailureReportingMode::Warning);
    EXPECT_EQ(wires.count(), 0);
    EXPECT_EQ(wires.session(), nullptr);
    EXPECT_TRUE(wires.at(0).isEmpty());

    core::MockEngine engine;
    ui::SessionModel session;
    session.attach(&engine);
    (void)openSessionWithBlocks(engine, session);
    wires.setSession(&session);
    ASSERT_TRUE(session.connectBlocks(u"b1"_s, 0, u"b2"_s, 0));
    ASSERT_EQ(wires.count(), 1);

    // Letting go of the session empties it, which is what closing one does.
    wires.setSession(nullptr);
    EXPECT_EQ(wires.count(), 0);
}
