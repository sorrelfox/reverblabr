// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The item in a real window: it asks for the sound only while it can be seen,
// a tile and a card share one transform, it draws no more bars than there is
// room for, and it goes on working when it is moved to another window, which
// is what happens to a card taken outside the main one.

#include "core/sharedtelemetry.h"
#include "dsp/barspectrum.h"
#include "meterhub.h"
#include "scopehub.h"
#include "spectrumitem.h"

#include <QGuiApplication>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <numbers>
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

class SpectrumItemTest : public ::testing::Test
{
protected:
    SpectrumItemTest()
        : hub(&meters)
    {
        (void)application();
        hub.setAsker([this](const QString &, const QString &) { ++asked; });
        meters.watch(&memory.slot());
        hub.watch(u"session"_s, &memory.scopes());
        std::strncpy(snapshot.blocks[0].id, "bars", core::kMaxTelemetryIdBytes - 1);
        snapshot.blocks[0].enabled = true;
        snapshot.blocks[0].scopeSlot = 0;
        snapshot.running = true;
        snapshot.blockCount = 1;
        snapshot.sampleRate = 48000;
        snapshot.frames = 1024;
        rt::claimScopeSlot(memory.scopes().slots[0]);
    }

    /// A tone of \a hz, the same in both channels.
    void hear(std::uint32_t frames, double hz = 1000.0)
    {
        std::vector<float> samples(frames);
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            samples[frame] = static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * (frame + heard) / 48000.0));
        }
        heard += frames;
        writer.write(memory.scopes().slots[0], samples.data(), samples.data(), frames, 48000);
        ++snapshot.frame;
        memory.slot().store(snapshot);
        (void)meters.tick();
    }

    [[nodiscard]] std::unique_ptr<SpectrumItem> itemIn(QQuickWindow &window, double width = 400.0)
    {
        auto item = std::make_unique<SpectrumItem>();
        item->setSize(QSizeF(width, 120.0));
        item->setHub(&hub);
        item->setBlockId(u"bars"_s);
        item->setParentItem(window.contentItem());
        return item;
    }

    [[nodiscard]] static bool show(QQuickWindow &window)
    {
        window.resize(500, 300);
        window.show();
        return QTest::qWaitForWindowExposed(&window);
    }

    core::PublishedSlot memory;
    core::TelemetrySnapshot snapshot;
    MeterHub meters;
    ScopeHub hub;
    rt::ScopeWriter writer;
    std::uint64_t heard = 0;
    int asked = 0;
};

[[nodiscard]] double highestOf(const QVariantList &levels)
{
    double highest = -1000.0;
    for (const QVariant &level : levels) {
        highest = std::max(highest, level.toDouble());
    }
    return highest;
}

} // namespace

TEST_F(SpectrumItemTest, ASineReachesTheTopInItsBar)
{
    QQuickWindow window;
    ASSERT_TRUE(show(window));
    const std::unique_ptr<SpectrumItem> item = itemIn(window);
    ASSERT_TRUE(item->isSubscribed());
    hear(9000);
    item->work();
    ASSERT_TRUE(item->isShowing());
    const QVariantList levels = item->levels();
    ASSERT_EQ(levels.size(), 64);
    EXPECT_NEAR(levels[item->barOf(1000.0)].toDouble(), 0.0, dsp::FrameSpectrum::kAccuracyDb);
    EXPECT_NEAR(highestOf(levels), 0.0, dsp::FrameSpectrum::kAccuracyDb);
}

TEST_F(SpectrumItemTest, ATileHasFewerBarsAndTheSameReading)
{
    QQuickWindow window;
    ASSERT_TRUE(show(window));
    const std::unique_ptr<SpectrumItem> item = itemIn(window, 150.0);
    item->setBars(128);
    EXPECT_EQ(item->barsShown(), 50);
    hear(9000, 19000.0);
    item->work();
    EXPECT_EQ(item->levels().size(), 50);
    EXPECT_NEAR(item->levels()[item->barOf(19000.0)].toDouble(), 0.0, dsp::FrameSpectrum::kAccuracyDb);
    QSignalSpy shown(item.get(), &SpectrumItem::barsShownChanged);
    item->setWidth(600.0);
    EXPECT_EQ(item->barsShown(), 128);
    EXPECT_EQ(shown.count(), 1);
    item->work();
    EXPECT_EQ(item->levels().size(), 128);
}

TEST_F(SpectrumItemTest, ATileAndACardShareOneTransform)
{
    QQuickWindow window;
    ASSERT_TRUE(show(window));
    const std::unique_ptr<SpectrumItem> tile = itemIn(window, 150.0);
    const std::unique_ptr<SpectrumItem> card = itemIn(window);
    EXPECT_EQ(hub.feedCount(), 1);
    hear(9000);
    tile->work();
    card->work();
    ScopeFeed *feed = hub.subscribe(u"bars"_s);
    const std::uint64_t transforms = feed->spectrum().transforms();
    hub.unsubscribe(feed);
    // One look's frames however many draw it; one more for the second is a
    // whole transform too many at every refresh.
    EXPECT_EQ(transforms, 1U);
    EXPECT_FALSE(tile->levels().empty());
    EXPECT_FALSE(card->levels().empty());
}

TEST_F(SpectrumItemTest, ItAsksForEverySampleAndOnlyWhileShown)
{
    QQuickWindow window;
    const std::unique_ptr<SpectrumItem> item = itemIn(window);
    EXPECT_FALSE(item->isSubscribed());
    EXPECT_EQ(asked, 0);
    ASSERT_TRUE(show(window));
    EXPECT_TRUE(item->isSubscribed());
    hear(9000);
    item->work();
    EXPECT_GT(asked, 0);
    ScopeFeed *feed = hub.subscribe(u"bars"_s);
    EXPECT_EQ(feed->frame().framesPerPoint, 1U);
    EXPECT_EQ(feed->frame().points.size(), rt::kScopePoints - 1U);
    hub.unsubscribe(feed);

    item->setVisible(false);
    QTRY_VERIFY(!item->isSubscribed());
    EXPECT_EQ(hub.feedCount(), 0);
}

TEST_F(SpectrumItemTest, ItStopsTickingWhenNothingCanMove)
{
    QQuickWindow window;
    ASSERT_TRUE(show(window));
    const std::unique_ptr<SpectrumItem> item = itemIn(window);
    hear(9000);
    item->work();
    EXPECT_FALSE(item->isTicking()) << "Real-time draws when there is news";
    item->setMotion(u"smooth"_s);
    EXPECT_TRUE(item->isTicking());
    item->setHold(true);
    EXPECT_FALSE(item->isTicking()) << "held bars have nowhere to go";
    item->setHold(false);
    EXPECT_TRUE(item->isTicking());
    item->setVisible(false);
    QTRY_VERIFY(!item->isTicking());
}

TEST_F(SpectrumItemTest, AHeldPictureStandsAndCanStillBeReadDifferently)
{
    QQuickWindow window;
    ASSERT_TRUE(show(window));
    const std::unique_ptr<SpectrumItem> item = itemIn(window);
    hear(9000);
    item->work();
    item->setHold(true);
    const QVariantList held = item->levels();
    hear(9000, 5000.0);
    item->work();
    EXPECT_EQ(item->levels(), held) << "a held picture takes no new sound";

    item->setFrameSize(1024);
    item->work();
    EXPECT_NEAR(item->levels()[item->barOf(1000.0)].toDouble(), 0.0, dsp::FrameSpectrum::kAccuracyDb)
        << "from the same sound, at the new size";
    item->setChannels(u"left"_s);
    item->work();
    EXPECT_NEAR(item->levels()[item->barOf(1000.0)].toDouble(), 0.0, dsp::FrameSpectrum::kAccuracyDb);
}

TEST_F(SpectrumItemTest, ItSurvivesGoingFromOneWindowToAnother)
{
    QQuickWindow first;
    ASSERT_TRUE(show(first));
    auto second = std::make_unique<QQuickWindow>();
    ASSERT_TRUE(show(*second));
    const std::unique_ptr<SpectrumItem> item = itemIn(first);
    item->setMotion(u"smooth"_s);
    hear(9000);
    item->work();
    ASSERT_TRUE(item->isTicking());
    const QVariantList levels = item->levels();

    item->setParentItem(second->contentItem());
    QCoreApplication::processEvents();
    EXPECT_TRUE(item->isSubscribed());
    EXPECT_TRUE(item->isShowing()) << "the sound went with it";
    EXPECT_TRUE(item->isTicking());
    EXPECT_EQ(item->window(), second.get());
    hear(1024);
    item->work();
    EXPECT_EQ(item->levels().size(), levels.size());

    item->setParentItem(first.contentItem());
    second.reset();
    QCoreApplication::processEvents();
    EXPECT_TRUE(item->isShowing());
    EXPECT_TRUE(item->isTicking());
}

TEST_F(SpectrumItemTest, TheMarksFollowTheScaleAndTheFloor)
{
    SpectrumItem item;
    item.setFloorDb(-60.0);
    QVariantList levels = item.levelMarks();
    ASSERT_EQ(levels.size(), 2);
    EXPECT_DOUBLE_EQ(levels[0].toMap().value(u"db"_s).toDouble(), -20.0);
    EXPECT_NEAR(levels[1].toMap().value(u"position"_s).toDouble(), 1.0 / 3.0, 1.0e-6);
    item.setFloorDb(-120.0);
    EXPECT_EQ(item.levelMarks().size(), 5);

    const auto positionOf = [&item](double hz) {
        const QVariantList marks = item.frequencyMarks();
        for (const QVariant &mark : marks) {
            if (mark.toMap().value(u"hz"_s).toDouble() == hz) {
                return mark.toMap().value(u"position"_s).toDouble();
            }
        }
        return -1.0;
    };
    EXPECT_NEAR(positionOf(1000.0), 0.566, 0.001) << "two of the three decades up";
    item.setFrequencyScale(u"linear"_s);
    EXPECT_NEAR(positionOf(10000.0), 0.5, 0.001);
    EXPECT_LT(positionOf(1000.0), 0.0) << "too near the bottom to name on this scale";
}
