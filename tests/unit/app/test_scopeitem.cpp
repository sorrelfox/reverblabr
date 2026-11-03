// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The item in a real window: it asks for the sound only while it can be seen,
// it draws every style from one feed, and it goes on working when it is moved
// to another window, which is what happens to a card taken outside the main
// one.

#include "core/sharedtelemetry.h"
#include "meterhub.h"
#include "scopehub.h"
#include "scopeitem.h"

#include <QGuiApplication>
#include <QQuickWindow>
#include <QTest>
#include <gtest/gtest.h>
#include <private/qwindow_p.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

using namespace rb;
using namespace rb::ui;
using namespace Qt::StringLiterals;

namespace
{

QGuiApplication &application()
{
    static QGuiApplication *const app = [] {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        static int argc = 1;
        static char name[] = "test";
        static char *argv[] = {name, nullptr};
        return new QGuiApplication(argc, argv);
    }();
    return *app;
}

class ScopeItemTest : public ::testing::Test
{
protected:
    ScopeItemTest()
        : hub(&meters)
    {
        (void)application();
        hub.setAsker([this](const QString &, const QString &) { ++asked; });
        meters.watch(&memory.slot());
        hub.watch(u"session"_s, &memory.scopes());
        std::strncpy(snapshot.blocks[0].id, "scope", core::kMaxTelemetryIdBytes - 1);
        snapshot.blocks[0].enabled = true;
        snapshot.blocks[0].scopeSlot = 0;
        snapshot.running = true;
        snapshot.blockCount = 1;
        snapshot.sampleRate = 48000;
        snapshot.frames = 256;
        rt::claimScopeSlot(memory.scopes().slots[0]);
    }

    void hear(std::uint32_t frames)
    {
        std::vector<float> left(frames);
        std::vector<float> right(frames);
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            left[frame] = 0.5f * std::sin(static_cast<float>(frame + heard) * 0.02f);
            right[frame] = 0.25f * left[frame];
        }
        heard += frames;
        writer.write(memory.scopes().slots[0], left.data(), right.data(), frames, 48000);
        ++snapshot.frame;
        memory.slot().store(snapshot);
        (void)meters.tick();
    }

    [[nodiscard]] std::unique_ptr<ScopeItem> itemIn(QQuickWindow &window)
    {
        auto item = std::make_unique<ScopeItem>();
        item->setSize(QSizeF(300.0, 100.0));
        item->setHub(&hub);
        item->setBlockId(u"scope"_s);
        item->setParentItem(window.contentItem());
        return item;
    }

    [[nodiscard]] static bool show(QQuickWindow &window)
    {
        window.resize(320, 120);
        window.show();
        return QTest::qWaitForWindowExposed(&window);
    }

    core::PublishedSlot memory;
    core::TelemetrySnapshot snapshot;
    MeterHub meters;
    ScopeHub hub;
    rt::ScopeWriter writer;
    std::uint32_t heard = 0;
    int asked = 0;
};

} // namespace

TEST_F(ScopeItemTest, ItAsksForSoundOnlyWhileItCanBeSeen)
{
    QQuickWindow window;
    const std::unique_ptr<ScopeItem> item = itemIn(window);
    EXPECT_FALSE(item->isSubscribed()) << "in a window nobody has shown";
    EXPECT_EQ(hub.feedCount(), 0);

    ASSERT_TRUE(show(window));
    EXPECT_TRUE(item->isSubscribed());
    EXPECT_EQ(hub.feedCount(), 1);

    item->setVisible(false);
    QCoreApplication::processEvents();
    EXPECT_FALSE(item->isSubscribed());
    EXPECT_EQ(hub.feedCount(), 0) << "hidden, nothing should keep the sound coming";

    item->setVisible(true);
    EXPECT_TRUE(item->isSubscribed());
    window.hide();
    QCoreApplication::processEvents();
    EXPECT_FALSE(item->isSubscribed()) << "a hidden window shows nothing either";
}

TEST_F(ScopeItemTest, AWindowThatCanNotBeSeenAsksForNoSound)
{
    // A minimised window goes on saying it is visible: only its being exposed
    // tells, and the desktop says when that changes. The offscreen platform
    // never does, so the test says it.
    QQuickWindow window;
    const std::unique_ptr<ScopeItem> item = itemIn(window);
    ASSERT_TRUE(show(window));
    EXPECT_TRUE(item->isSubscribed());
    const auto setExposed = [&window](bool exposed, const QRegion &region) {
        QWindowPrivate::get(&window)->exposed = exposed;
        QExposeEvent event(region);
        QCoreApplication::sendEvent(&window, &event);
        QCoreApplication::processEvents();
    };

    setExposed(false, QRegion());
    EXPECT_TRUE(window.isVisible());
    EXPECT_FALSE(item->isSubscribed());
    EXPECT_EQ(hub.feedCount(), 0);

    setExposed(true, QRect(0, 0, 320, 120));
    EXPECT_TRUE(item->isSubscribed());
}

TEST_F(ScopeItemTest, TwoItemsOfOneBlockDrawFromOneFeed)
{
    QQuickWindow window;
    const std::unique_ptr<ScopeItem> tile = itemIn(window);
    const std::unique_ptr<ScopeItem> card = itemIn(window);
    card->setWidth(600.0);
    ASSERT_TRUE(show(window));
    EXPECT_EQ(hub.feedCount(), 1);
    hear(4800);
    tile->work();
    card->work();
    EXPECT_EQ(tile->heights(0).size(), (300 + 1) * 2);
    EXPECT_EQ(card->heights(0).size(), (600 + 1) * 2);
}

TEST_F(ScopeItemTest, ACardThatClosesTakesWhatItSaidWithIt)
{
    // **A card is closed by being told it has no block**, and on its way out
    // it may say what an unknown block's settings are, the defaults, before
    // it lets go of the feed. The tile left showing must not be read for
    // with those: it was a fifth of a second read for a whole one, drawn as
    // a strip at the tile's right.
    QQuickWindow window;
    const std::unique_ptr<ScopeItem> tile = itemIn(window);
    const std::unique_ptr<ScopeItem> card = itemIn(window);
    tile->setSpanMs(1000.0);
    card->setSpanMs(1000.0);
    ASSERT_TRUE(show(window));
    hear(60000);
    const auto unheard = [](const ScopeItem &item) {
        const QVariantList heights = item.heights(0);
        return std::ranges::count_if(heights, [](const QVariant &height) { return !height.isValid(); }) / 2;
    };
    tile->work();
    ASSERT_LE(unheard(*tile), 1);

    card->setSpanMs(200.0);
    card->setBlockId(QString{});
    hear(1024);
    tile->work();

    EXPECT_LE(unheard(*tile), 1) << "the tile is read for what the closed card said";
}

TEST_F(ScopeItemTest, ACardThatClosesLeavesNoHoldBehind)
{
    QQuickWindow window;
    const std::unique_ptr<ScopeItem> tile = itemIn(window);
    const std::unique_ptr<ScopeItem> card = itemIn(window);
    ASSERT_TRUE(show(window));

    card->setHold(true);
    card->setBlockId(QString{});

    ScopeFeed *feed = hub.subscribe(u"scope"_s);
    EXPECT_FALSE(feed->isHeld()) << "the tile, which holds nothing, is left held";
    hub.unsubscribe(feed);
}

TEST_F(ScopeItemTest, WhatItShowsFollowsTheFeed)
{
    QQuickWindow window;
    const std::unique_ptr<ScopeItem> item = itemIn(window);
    ASSERT_TRUE(show(window));
    hear(4800);
    item->setSpanMs(50.0);
    item->work();
    EXPECT_TRUE(item->isShowing());
    EXPECT_EQ(item->feedState(), u"live"_s);

    snapshot.blocks[0].enabled = false;
    hear(256);
    item->work();
    EXPECT_FALSE(item->isShowing());
    EXPECT_EQ(item->feedState(), u"off"_s);
}

TEST_F(ScopeItemTest, ChangingAStyleTakesNothingButTheNextFrame)
{
    QQuickWindow window;
    const std::unique_ptr<ScopeItem> item = itemIn(window);
    ASSERT_TRUE(show(window));
    hear(4800);
    item->work();
    const QVariantList before = item->heights(0);
    const int pictures = item->pictures();
    for (const QString &style : {u"filled"_s, u"bars"_s, u"dots"_s, u"line"_s}) {
        item->setStyle(style);
        EXPECT_EQ(item->heights(0), before) << "a style is how it is drawn, not what";
    }
    EXPECT_EQ(item->pictures(), pictures) << "and nothing had to be worked out again for it";
}

TEST_F(ScopeItemTest, ItTicksOnlyWhileSmoothAndWithATraceToMove)
{
    QQuickWindow window;
    const std::unique_ptr<ScopeItem> item = itemIn(window);
    ASSERT_TRUE(show(window));
    hear(4800);
    item->work();
    EXPECT_FALSE(item->isTicking()) << "Real-time draws when a refresh brings news";

    item->setMotion(u"smooth"_s);
    item->work();
    EXPECT_TRUE(item->isTicking());

    item->setHold(true);
    EXPECT_FALSE(item->isTicking()) << "a held trace does not move";
    item->setHold(false);
    item->work();
    EXPECT_TRUE(item->isTicking());

    snapshot.blocks[0].enabled = false;
    hear(256);
    item->work();
    EXPECT_FALSE(item->isTicking()) << "and without a trace there is nothing to move";
}

TEST_F(ScopeItemTest, ItGoesOnDrawingInAnotherWindow)
{
    QQuickWindow first;
    QQuickWindow second;
    ASSERT_TRUE(show(first));
    ASSERT_TRUE(show(second));
    const std::unique_ptr<ScopeItem> item = itemIn(first);
    item->setMotion(u"smooth"_s);
    item->setSpanMs(500.0);
    hear(48000);
    item->work();
    ASSERT_TRUE(item->isTicking());
    const QVariantList before = item->heights(0);
    ASSERT_FALSE(before.isEmpty());

    QQuickWindow *from = &first;
    QQuickWindow *to = &second;
    for (int move = 0; move < 8; ++move) {
        item->setParentItem(to->contentItem());
        EXPECT_EQ(item->window(), to);
        EXPECT_TRUE(item->isSubscribed());
        EXPECT_TRUE(item->isShowing()) << "the trace goes with the card";
        EXPECT_TRUE(item->isTicking());
        EXPECT_FALSE(item->heights(0).isEmpty()) << "no window of its own to start again from";
        hear(1024);
        item->work();
        QTest::qWait(30);
        std::swap(from, to);
    }
    EXPECT_EQ(hub.feedCount(), 1);
}

TEST_F(ScopeItemTest, AWindowClosedUnderItLeavesNothingBehind)
{
    auto window = std::make_unique<QQuickWindow>();
    ASSERT_TRUE(show(*window));
    std::unique_ptr<ScopeItem> item = itemIn(*window);
    item->setMotion(u"smooth"_s);
    hear(4800);
    item->work();
    window.reset();
    QCoreApplication::processEvents();
    EXPECT_FALSE(item->isSubscribed());
    EXPECT_FALSE(item->isTicking());
    EXPECT_EQ(hub.feedCount(), 0);

    QQuickWindow next;
    ASSERT_TRUE(show(next));
    item->setParentItem(next.contentItem());
    EXPECT_TRUE(item->isSubscribed()) << "and it can be taken in again";
}

TEST_F(ScopeItemTest, AHubThatGoesFirstLeavesTheItemSafe)
{
    QQuickWindow window;
    auto temporary = std::make_unique<ScopeHub>(&meters);
    ASSERT_TRUE(show(window));
    {
        ScopeItem item;
        item.setSize(QSizeF(300.0, 100.0));
        item.setHub(temporary.get());
        item.setBlockId(u"scope"_s);
        item.setParentItem(window.contentItem());
        ASSERT_TRUE(item.isSubscribed());
        temporary.reset();
        EXPECT_FALSE(item.isSubscribed());
    }
}
