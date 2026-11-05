// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What the floating windows show and where they sit, against the mock engine,
// with no window. The windows themselves are in tst_speechnow.qml.

#include "appshell.h"
#include "blocks/speechnow/speechnow.h"
#include "core/editcommand.h"
#include "core/mockengine.h"
#include "floatingsurface.h"
#include "traystate.h"
#include "windowstate.h"

#include <KConfig>
#include <KSharedConfig>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <gtest/gtest.h>

using namespace rb;
using namespace Qt::StringLiterals;

namespace
{

void needApplication()
{
    if (QCoreApplication::instance() == nullptr) {
        static int argc = 1;
        static char name[] = "reverblabr-app-test";
        static char *argv[] = {name, nullptr};
        [[maybe_unused]] static auto *application = new QCoreApplication(argc, argv);
    }
}

template <typename Predicate>
bool spinUntil(Predicate predicate, int milliseconds = 3000)
{
    QElapsedTimer clock;
    clock.start();
    while (!predicate() && clock.elapsed() < milliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(2);
    }
    return predicate();
}

[[nodiscard]] QString edit(const core::EditCommand &command)
{
    return QString::fromUtf8(QJsonDocument(core::editCommandToJson(command)).toJson(QJsonDocument::Compact));
}

[[nodiscard]] QString
addBlock(const QString &blockId, const QJsonObject &params = {}, const QString &type = u"meter.speechnow"_s)
{
    core::BlockInstance block;
    block.id = blockId;
    block.type = type;
    block.params = params;
    return edit(core::AddBlock{block, -1});
}

const QJsonObject kFloating{{u"showFloating"_s, true}};

class FloatingSurfaceTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        needApplication();
        ASSERT_TRUE(m_directory.isValid());
        ui::WindowState::setConfig(KSharedConfig::openConfig(m_directory.filePath(u"state"_s), KConfig::SimpleConfig));
        ui::AppShell::setEngine(&engine, [this](const QString &id) { return engine.telemetryOf(id); });
        tray = std::make_unique<ui::TrayState>(&engine);
        surface = std::make_unique<ui::FloatingSurface>(tray.get());
        surface->setClock([this] { return now; });
        QSignalSpy opened(&engine, &core::EngineFacade::sessionOpened);
        engine.createSession(u"Call"_s);
        id = opened.at(0).at(0).toString();
    }

    void TearDown() override
    {
        surface.reset();
        tray.reset();
        ui::AppShell::setEngine(nullptr);
        ui::WindowState::setConfig({});
    }

    [[nodiscard]] QVariantMap row(int index = 0) const
    {
        return surface->rows().at(index).toMap();
    }

    /// Runs \a session and waits for its first snapshot.
    void run(const QString &session, int row = 0)
    {
        engine.start(session);
        ASSERT_TRUE(spinUntil([&] {
            surface->read();
            return this->row(row).value(u"watch"_s).toString() == "watching"_L1;
        })) << "it never ran";
    }

    QTemporaryDir m_directory;
    core::MockEngine engine;
    std::unique_ptr<ui::TrayState> tray;
    std::unique_ptr<ui::FloatingSurface> surface;
    qint64 now = 1000;
    QString id;
};

} // namespace

TEST_F(FloatingSurfaceTest, NothingIsWantedUntilACheckBoxSaysSo)
{
    engine.applyEdit(id, addBlock(u"call"_s));
    ASSERT_TRUE(spinUntil([&] { return !tray->meters().isEmpty() || tray->floatingBlocks().isEmpty(); }));
    EXPECT_FALSE(surface->isWanted());

    QSignalSpy changed(surface.get(), &ui::FloatingSurface::rowsChanged);
    engine.applyEdit(id, edit(core::SetParam{u"call"_s, u"showFloating"_s, true}));
    ASSERT_TRUE(spinUntil([&] { return surface->isWanted(); }));
    EXPECT_GE(changed.count(), 1);

    engine.applyEdit(id, edit(core::SetParam{u"call"_s, u"showFloating"_s, false}));
    ASSERT_TRUE(spinUntil([&] { return !surface->isWanted(); }));
    EXPECT_TRUE(surface->rows().isEmpty());
}

TEST_F(FloatingSurfaceTest, ARowSaysWhatItsBlockPublishes)
{
    engine.applyEdit(id, addBlock(u"call"_s, kFloating));
    engine.applyEdit(id, edit(core::Rename{u"call"_s, u"Call"_s}));
    ASSERT_TRUE(spinUntil([&] { return surface->isWanted(); }));
    surface->read();
    EXPECT_EQ(row().value(u"watch"_s).toString(), "stopped"_L1);

    run(id);
    engine.setFakeBlockValues(id, u"call"_s, {blocks::SpeechNow::kSpeaking});
    surface->read();
    EXPECT_EQ(row().value(u"state"_s).toInt(), 1);
    EXPECT_EQ(row().value(u"nickname"_s).toString(), "Call"_L1);
}

TEST_F(FloatingSurfaceTest, AGaugesRowCarriesItsNumberAndItsMarks)
{
    engine.applyEdit(id, addBlock(u"you"_s, kFloating, u"meter.voicegauge"_s));
    ASSERT_TRUE(spinUntil([&] { return surface->isWanted(); }));
    run(id);
    engine.setFakeBlockValues(id, u"you"_s, {112.0, -24.04, -120.0, 4034.0});
    surface->read();
    EXPECT_EQ(row().value(u"type"_s).toString(), "meter.voicegauge"_L1);
    EXPECT_EQ(row().value(u"state"_s).toInt(), 112);
    EXPECT_DOUBLE_EQ(row().value(u"reading"_s).toDouble(), -24.0) << "a tenth is the most anyone could see";
    EXPECT_EQ(row().value(u"marks"_s).toInt(), 4034);
}

TEST_F(FloatingSurfaceTest, ASpeechNowsRowHasNoNumberToChangeBetweenWords)
{
    engine.applyEdit(id, addBlock(u"call"_s, kFloating));
    ASSERT_TRUE(spinUntil([&] { return surface->isWanted(); }));
    run(id);
    EXPECT_FALSE(row().contains(u"reading"_s));
}

TEST_F(FloatingSurfaceTest, ALevelThatMovesLessThanATenthIsNoChange)
{
    engine.applyEdit(id, addBlock(u"you"_s, kFloating, u"meter.voicegauge"_s));
    ASSERT_TRUE(spinUntil([&] { return surface->isWanted(); }));
    run(id);
    engine.setFakeBlockValues(id, u"you"_s, {112.0, -24.01, -120.0, 0.0});
    surface->read();
    QSignalSpy changes(surface.get(), &ui::FloatingSurface::rowsChanged);
    engine.setFakeBlockValues(id, u"you"_s, {112.0, -24.02, -120.0, 0.0});
    surface->read();
    EXPECT_EQ(changes.count(), 0);
}

TEST_F(FloatingSurfaceTest, ReadsAStoppedAnOverloadedAndASilentEngineAlike)
{
    engine.applyEdit(id, addBlock(u"call"_s, kFloating));
    ASSERT_TRUE(spinUntil([&] { return surface->isWanted(); }));
    run(id);

    engine.setFakeOverload(id, true);
    surface->read();
    EXPECT_EQ(row().value(u"watch"_s).toString(), "overloaded"_L1);
    engine.setFakeOverload(id, false);
    surface->read();
    EXPECT_EQ(row().value(u"watch"_s).toString(), "watching"_L1);

    // Nothing runs any more, and the memory still says something does.
    engine.setFakeFrozen(id, true);
    now += ui::TelemetryWatch::kUnmovedMs - 1;
    surface->read();
    EXPECT_EQ(row().value(u"watch"_s).toString(), "watching"_L1);
    now += 1;
    surface->read();
    EXPECT_EQ(row().value(u"watch"_s).toString(), "noNews"_L1);
    engine.setFakeFrozen(id, false);

    engine.stop(id);
    ASSERT_TRUE(spinUntil([&] {
        surface->read();
        return row().value(u"watch"_s).toString() == "stopped"_L1;
    }));
}

TEST_F(FloatingSurfaceTest, NothingChangesBetweenWords)
{
    engine.applyEdit(id, addBlock(u"call"_s, kFloating));
    ASSERT_TRUE(spinUntil([&] { return surface->isWanted(); }));
    run(id);
    QSignalSpy changed(surface.get(), &ui::FloatingSurface::rowsChanged);
    surface->read();
    surface->read();
    EXPECT_EQ(changed.count(), 0) << "a read that saw nothing new repainted";
    engine.setFakeBlockValues(id, u"call"_s, {blocks::SpeechNow::kSpeaking});
    surface->read();
    EXPECT_EQ(changed.count(), 1);
}

TEST_F(FloatingSurfaceTest, FourRowsAndWordOfTheRest)
{
    for (int block = 0; block < ui::FloatingSurface::kMostRows + 2; ++block) {
        engine.applyEdit(id, addBlock(u"b%1"_s.arg(block), kFloating));
    }
    ASSERT_TRUE(spinUntil([&] { return surface->leftOut() == 2; }));
    EXPECT_EQ(surface->rows().size(), ui::FloatingSurface::kMostRows);
}

TEST_F(FloatingSurfaceTest, EverySessionHasItsRows)
{
    engine.applyEdit(id, addBlock(u"call"_s, kFloating));
    QSignalSpy opened(&engine, &core::EngineFacade::sessionOpened);
    engine.createSession(u"Second"_s);
    const QString second = opened.at(0).at(0).toString();
    engine.applyEdit(second, addBlock(u"you"_s, kFloating));
    ASSERT_TRUE(spinUntil([&] { return surface->rows().size() == 2; }));
    run(second, 1);
    EXPECT_EQ(row(0).value(u"watch"_s).toString(), "stopped"_L1);
    engine.stop(second);
}

TEST_F(FloatingSurfaceTest, ADragIsWrittenDownWhenItEnds)
{
    ui::FloatingSurface other(tray.get());
    surface->setPlacement(u"label"_s, {{u"left"_s, 500}}, true);
    EXPECT_EQ(surface->placement(u"label"_s).value(u"left"_s).toInt(), 500) << "the window follows the drag";
    EXPECT_EQ(other.placement(u"label"_s).value(u"left"_s).toInt(), 40) << "the file was written during a drag";

    surface->setPlacement(u"label"_s, {});
    EXPECT_EQ(other.placement(u"label"_s).value(u"left"_s).toInt(), 500);
    EXPECT_EQ(surface->placement(u"label"_s).value(u"left"_s).toInt(), 500);
}

TEST_F(FloatingSurfaceTest, TheStripGoesToTheFirstBlockThatAsksForIt)
{
    QSignalSpy told(surface.get(), &ui::FloatingSurface::stripChanged);
    EXPECT_TRUE(surface->strip().isEmpty());
    engine.applyEdit(id, addBlock(u"first"_s, {{u"showStrip"_s, true}}, u"meter.soundside"_s));
    ASSERT_TRUE(spinUntil([&] { return !surface->strip().isEmpty(); }));
    EXPECT_EQ(surface->strip().value(u"blockId"_s).toString(), "first"_L1);
    EXPECT_EQ(surface->strip().value(u"sessionId"_s).toString(), id);
    EXPECT_EQ(told.count(), 1);
    EXPECT_FALSE(surface->isWanted()) << "a strip is not a row of the label";

    engine.applyEdit(id, addBlock(u"second"_s, {{u"showStrip"_s, true}}, u"meter.soundside"_s));
    EXPECT_FALSE(spinUntil([&] { return surface->strip().value(u"blockId"_s).toString() != "first"_L1; }, 50));

    engine.applyEdit(id, edit(core::RemoveBlock{u"first"_s}));
    ASSERT_TRUE(spinUntil([&] { return surface->strip().value(u"blockId"_s).toString() == "second"_L1; }));
}

TEST_F(FloatingSurfaceTest, TheStripStartsMediumAtTheTopInTheMiddle)
{
    const QVariantMap strip = surface->placement(u"strip"_s);
    EXPECT_EQ(strip.value(u"left"_s).toDouble(), 50.0);
    EXPECT_EQ(strip.value(u"top"_s).toDouble(), 4.0);
    EXPECT_EQ(strip.value(u"size"_s).toString(), "medium"_L1);
    EXPECT_EQ(strip.value(u"show"_s).toString(), "mark"_L1);

    surface->setPlacement(u"strip"_s, {{u"show"_s, u"bars"_s}});
    EXPECT_EQ(surface->placement(u"strip"_s).value(u"show"_s).toString(), "bars"_L1);
}

TEST_F(FloatingSurfaceTest, ALabelSitsInPixelsAndAStripInPercent)
{
    EXPECT_EQ(surface->placement(u"label"_s).value(u"unit"_s).toString(), "px"_L1);
    EXPECT_EQ(surface->placement(u"strip"_s).value(u"unit"_s).toString(), "percent"_L1);

    QSignalSpy moved(surface.get(), &ui::FloatingSurface::placementChanged);
    surface->setPlacement(u"label"_s, {{u"left"_s, 1480}, {u"size"_s, u"large"_s}});
    EXPECT_EQ(moved.count(), 1);
    EXPECT_EQ(surface->placement(u"label"_s).value(u"left"_s).toInt(), 1480);
    EXPECT_EQ(surface->placement(u"label"_s).value(u"top"_s).toInt(), 40) << "a key not given stays";
    EXPECT_EQ(surface->placement(u"label"_s).value(u"size"_s).toString(), "large"_L1);

    surface->setPlacement(u"strip"_s, {{u"left"_s, 140}, {u"top"_s, -3}});
    EXPECT_EQ(surface->placement(u"strip"_s).value(u"left"_s).toInt(), 100);
    EXPECT_EQ(surface->placement(u"strip"_s).value(u"top"_s).toInt(), 0);
    EXPECT_EQ(surface->placement(u"label"_s).value(u"left"_s).toInt(), 1480) << "the two are kept apart";
}
