// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The word, the hold, the fade and the trail, by the clock: one copy of them,
// whatever draws it. The drawing is in tst_soundside.qml.

#include "appshell.h"
#include "blocks/soundside.h"
#include "core/editcommand.h"
#include "core/mockengine.h"
#include "soundsidereadings.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QThread>
#include <gtest/gtest.h>

using namespace rb;
using namespace rb::ui;
using State = blocks::SoundSide::State;
using Word = SoundSideReading::Word;
using namespace Qt::StringLiterals;

namespace
{

constexpr qint64 kStep = 16;

/// Sound at \a pan and \a width for \a ms, a read every 16 ms.
qint64 sound(SoundSideReading &reading, qint64 from, qint64 ms, float pan, float width = 1.0f)
{
    qint64 now = from;
    for (; now < from + ms; now += kStep) {
        reading.saw(now, pan, -30.0f, width, State::Sounding);
    }
    return now;
}

qint64 quiet(SoundSideReading &reading, qint64 from, qint64 ms)
{
    qint64 now = from;
    for (; now < from + ms; now += kStep) {
        reading.saw(now, 0.0f, -80.0f, 0.0f, State::Quiet);
    }
    return now;
}

} // namespace

TEST(SoundSideReading, StartsQuietWithNoMark)
{
    SoundSideReading reading;
    quiet(reading, 0, 100);
    EXPECT_EQ(reading.shown().word, Word::Quiet);
    EXPECT_EQ(reading.shown().strength, 0.0f);
}

TEST(SoundSideReading, TheMarkDoesNotWaitAndTheWordDoes)
{
    SoundSideReading reading;
    const qint64 now = quiet(reading, 0, 100);
    reading.saw(now, -0.6f, -30.0f, 0.0f, State::Sounding);
    EXPECT_EQ(reading.shown().strength, 1.0f) << "the attack is never smoothed";
    EXPECT_EQ(reading.shown().pan, -0.6f);
    EXPECT_EQ(reading.shown().word, Word::Quiet);
    sound(reading, now + kStep, 80, -0.6f);
    EXPECT_EQ(reading.shown().word, Word::Quiet) << "not before 100 ms";
    sound(reading, now + kStep + 80, 32, -0.6f);
    EXPECT_EQ(reading.shown().word, Word::Left);
}

TEST(SoundSideReading, TheWordsFollowTheTable)
{
    struct Row {
        float pan;
        float width;
        Word word;
    };
    for (const Row row : {Row{-0.5f, 1.0f, Word::Left},
                          Row{0.5f, 1.0f, Word::Right},
                          Row{0.1f, 0.5f, Word::Centre},
                          Row{-0.1f, 0.49f, Word::Both},
                          Row{0.25f, 0.0f, Word::Right},
                          Row{0.24f, 0.9f, Word::Centre}}) {
        SoundSideReading reading;
        sound(reading, 0, 200, row.pan, row.width);
        EXPECT_EQ(reading.shown().word, row.word) << row.pan << ' ' << row.width;
    }
}

TEST(SoundSideReading, ASideStaysUntilUnderTwentyPercent)
{
    SoundSideReading reading;
    qint64 now = sound(reading, 0, 200, 0.3f);
    ASSERT_EQ(reading.shown().word, Word::Right);
    now = sound(reading, now, 300, 0.21f);
    EXPECT_EQ(reading.shown().word, Word::Right) << "21 % does not leave a side";
    sound(reading, now, 200, 0.19f);
    EXPECT_EQ(reading.shown().word, Word::Centre);
}

TEST(SoundSideReading, TheOtherSideIsNoShortCut)
{
    SoundSideReading reading;
    const qint64 now = sound(reading, 0, 200, 0.3f);
    sound(reading, now, 200, -0.22f);
    EXPECT_EQ(reading.shown().word, Word::Centre) << "20 % holds a side only for the side it is on";
}

TEST(SoundSideReading, AFlickerNeverReachesTheWord)
{
    SoundSideReading reading;
    qint64 now = sound(reading, 0, 200, -0.5f);
    ASSERT_EQ(reading.shown().word, Word::Left);
    for (int turn = 0; turn < 40; ++turn) {
        now = sound(reading, now, 48, turn % 2 == 0 ? 0.5f : -0.5f);
        EXPECT_EQ(reading.shown().word, Word::Left) << turn;
    }
}

TEST(SoundSideReading, HoldsThenFadesThenQuiet)
{
    SoundSideReading reading;
    qint64 now = sound(reading, 0, 200, -0.8f);
    const qint64 last = now - kStep;
    now = quiet(reading, now, 200);
    EXPECT_EQ(reading.shown().strength, 1.0f) << "it holds";
    EXPECT_EQ(reading.shown().word, Word::Left);
    reading.saw(last + 375, 0.0f, -80.0f, 0.0f, State::Quiet);
    EXPECT_NEAR(reading.shown().strength, 0.5f, 0.01f) << "halfway through the fade";
    EXPECT_EQ(reading.shown().pan, -0.8f) << "it fades where it stood";
    EXPECT_EQ(reading.shown().word, Word::Left);
    reading.saw(last + 500, 0.0f, -80.0f, 0.0f, State::Quiet);
    EXPECT_EQ(reading.shown().strength, 0.0f);
    EXPECT_EQ(reading.shown().word, Word::Quiet) << "the word goes with the mark";
}

TEST(SoundSideReading, AClickHoldsAWordThoughItIsShorterThanAWordsWait)
{
    SoundSideReading reading;
    qint64 now = quiet(reading, 0, 100);
    reading.saw(now, -1.0f, -30.0f, 0.0f, State::Sounding);
    bool saidLeft = false;
    for (now += kStep; now < 700; now += kStep) {
        reading.saw(now, 0.0f, -80.0f, 0.0f, State::Quiet);
        saidLeft = saidLeft || reading.shown().word == Word::Left;
    }
    EXPECT_TRUE(saidLeft);
    EXPECT_EQ(reading.shown().word, Word::Quiet);
}

TEST(SoundSideReading, SilenceIsNeverACentredMark)
{
    SoundSideReading reading;
    qint64 now = sound(reading, 0, 200, 0.9f);
    now = quiet(reading, now, 2000);
    EXPECT_EQ(reading.shown().strength, 0.0f);
    EXPECT_EQ(reading.shown().word, Word::Quiet);
    for (const SoundSideReading::Ghost &ghost : reading.shown().ghosts) {
        EXPECT_EQ(ghost.strength, 0.0f);
    }
}

TEST(SoundSideReading, TheTrailShowsAJumpAndGoes)
{
    SoundSideReading reading;
    qint64 now = sound(reading, 0, 200, -0.8f);
    now = sound(reading, now, 64, 0.8f);
    const SoundSideReading::Shown shown = reading.shown();
    EXPECT_EQ(shown.pan, 0.8f);
    EXPECT_EQ(shown.ghosts[0].pan, -0.8f);
    EXPECT_FLOAT_EQ(shown.ghosts[0].strength, 0.45f);
    EXPECT_FLOAT_EQ(shown.ghosts[1].strength, 0.28f);
    EXPECT_FLOAT_EQ(shown.ghosts[2].strength, 0.14f);
    sound(reading, now, 200, 0.8f);
    for (const SoundSideReading::Ghost &ghost : reading.shown().ghosts) {
        EXPECT_EQ(ghost.strength, 0.0f) << "a mark that stands still has no trail";
    }
}

TEST(SoundSideReading, TheMarkOpensForNoOnePlaceAndNotForOneSide)
{
    SoundSideReading reading;
    sound(reading, 0, 50, 0.0f, 0.0f);
    EXPECT_NEAR(reading.shown().opening, 1.0f, 0.001f);
    sound(reading, 100, 50, 0.0f, 1.0f);
    EXPECT_NEAR(reading.shown().opening, 0.0f, 0.001f);
    // A hard pan has nothing on the other side to match.
    sound(reading, 200, 50, 1.0f, 0.0f);
    EXPECT_NEAR(reading.shown().opening, 0.0f, 0.001f);
}

TEST(SoundSideReading, SidesAlikeEmptiesTheTrackAndSaysSame)
{
    SoundSideReading reading;
    qint64 now = sound(reading, 0, 200, 0.0f);
    for (const qint64 end = now + 120; now < end; now += kStep) {
        reading.saw(now, 0.0f, -30.0f, 1.0f, State::SidesAlike);
    }
    EXPECT_EQ(reading.shown().word, Word::Same);
    EXPECT_EQ(reading.shown().strength, 0.0f);
    sound(reading, now, 200, 0.9f);
    EXPECT_EQ(reading.shown().word, Word::Right) << "it clears at the next stereo sound";
}

TEST(SoundSideReading, TenSecondsAtOneEndIsSoundOnOneSideOnly)
{
    SoundSideReading reading;
    qint64 now = sound(reading, 0, 9000, 1.0f);
    EXPECT_FALSE(reading.shown().oneSideOnly);
    now = sound(reading, now, 1100, 1.0f);
    EXPECT_TRUE(reading.shown().oneSideOnly);
    sound(reading, now, 100, 0.5f);
    EXPECT_FALSE(reading.shown().oneSideOnly);
}

TEST(SoundSideReading, AHardPanFirstSeenLateIsNoSteadyTenSeconds)
{
    // The clock is the application's, so a first read comes long after nought.
    SoundSideReading reading;
    reading.saw(600000, 1.0f, -30.0f, 0.0f, State::Sounding);
    EXPECT_FALSE(reading.shown().oneSideOnly);
    sound(reading, 600016, 9000, 1.0f);
    EXPECT_FALSE(reading.shown().oneSideOnly);

    reading.reset();
    reading.saw(700000, 1.0f, -30.0f, 0.0f, State::Sounding);
    EXPECT_FALSE(reading.shown().oneSideOnly) << "nor after a reset";
}

TEST(SoundSideReading, OneSideOnlyGoesWhenTheSoundGoes)
{
    SoundSideReading reading;
    qint64 now = sound(reading, 0, 10100, 1.0f);
    ASSERT_TRUE(reading.shown().oneSideOnly);
    now = quiet(reading, now, 200);
    EXPECT_TRUE(reading.shown().oneSideOnly) << "while the mark stands";
    now = quiet(reading, now, 400);
    EXPECT_EQ(reading.shown().word, Word::Quiet);
    EXPECT_FALSE(reading.shown().oneSideOnly);

    now = sound(reading, now, 10100, 1.0f);
    ASSERT_TRUE(reading.shown().oneSideOnly);
    reading.saw(now, 0.0f, -30.0f, 1.0f, State::SidesAlike);
    EXPECT_FALSE(reading.shown().oneSideOnly);
}

TEST(SoundSideReading, SeparateSoundsDoNotAddUpToSteady)
{
    SoundSideReading reading;
    qint64 now = 0;
    for (int sounds = 0; sounds < 4; ++sounds) {
        now = sound(reading, now, 4000, 1.0f);
        now = quiet(reading, now, 1000);
    }
    now = sound(reading, now, 4000, 1.0f);
    EXPECT_FALSE(reading.shown().oneSideOnly);
}

TEST(SoundSideReading, ResetForgetsEverything)
{
    SoundSideReading reading;
    sound(reading, 0, 200, -0.8f);
    reading.reset();
    EXPECT_EQ(reading.shown().word, Word::Quiet);
    EXPECT_EQ(reading.shown().strength, 0.0f);
}

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

class SoundSideReadingsTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        needApplication();
        ui::AppShell::setEngine(&engine, [this](const QString &id) { return engine.telemetryOf(id); });
        readings.setClock([this] { return now; });
        QSignalSpy opened(&engine, &core::EngineFacade::sessionOpened);
        engine.createSession(u"Game"_s);
        id = opened.at(0).at(0).toString();
        core::BlockInstance block;
        block.id = u"side"_s;
        block.type = u"meter.soundside"_s;
        engine.applyEdit(
            id,
            QString::fromUtf8(
                QJsonDocument(core::editCommandToJson(core::AddBlock{block, -1})).toJson(QJsonDocument::Compact)));
        readings.hold(id, u"side"_s);
    }

    void TearDown() override
    {
        readings.release(id, u"side"_s);
        ui::AppShell::setEngine(nullptr);
    }

    [[nodiscard]] QString kind()
    {
        return readings.shown(id, u"side"_s).value(u"kind"_s).toString();
    }

    /// Reads until the block's made-up values have reached the snapshot.
    bool waitFor(const QString &wanted)
    {
        return spinUntil([&] {
            now += kStep;
            readings.read();
            return kind() == wanted;
        });
    }

    core::MockEngine engine;
    SoundSideReadings readings;
    qint64 now = 1000;
    QString id;
};

} // namespace

TEST_F(SoundSideReadingsTest, AStoppedSessionIsStopped)
{
    EXPECT_EQ(kind(), "stopped"_L1) << "before anything was read";
    readings.read();
    EXPECT_EQ(kind(), "stopped"_L1);
    EXPECT_EQ(readings.shown(id, u"side"_s).value(u"watch"_s).toString(), "stopped"_L1);
    EXPECT_EQ(readings.shown(id, u"nobody"_s).value(u"kind"_s).toString(), "stopped"_L1);
}

TEST_F(SoundSideReadingsTest, ARunningBlockSaysWhatItPublishes)
{
    engine.start(id);
    EXPECT_TRUE(waitFor(u"quiet"_s));
    engine.setFakeBlockValues(id, u"side"_s, {-0.6f, -30.0f, 1.0f, 1.0f});
    EXPECT_TRUE(waitFor(u"left"_s));
    const QVariantMap shown = readings.shown(id, u"side"_s);
    EXPECT_EQ(shown.value(u"percent"_s).toInt(), 60);
    EXPECT_EQ(shown.value(u"markLevel"_s).toInt(), -30);
    EXPECT_FLOAT_EQ(shown.value(u"strength"_s).toFloat(), 1.0f);
}

TEST_F(SoundSideReadingsTest, OverloadStoppingAndADeadEngineEmptyTheTrack)
{
    engine.start(id);
    engine.setFakeBlockValues(id, u"side"_s, {-0.6f, -30.0f, 1.0f, 1.0f});
    ASSERT_TRUE(waitFor(u"left"_s));
    engine.setFakeOverload(id, true);
    EXPECT_TRUE(waitFor(u"stopped"_s));
    EXPECT_EQ(readings.shown(id, u"side"_s).value(u"watch"_s).toString(), "overloaded"_L1);
    EXPECT_EQ(readings.shown(id, u"side"_s).value(u"strength"_s).toFloat(), 0.0f);
    engine.setFakeOverload(id, false);
    ASSERT_TRUE(waitFor(u"left"_s));

    engine.setFakeFrozen(id, true);
    now += 1000;
    EXPECT_TRUE(waitFor(u"stopped"_s));
    EXPECT_EQ(readings.shown(id, u"side"_s).value(u"watch"_s).toString(), "noNews"_L1);
}

TEST_F(SoundSideReadingsTest, ABlockSwitchedOffIsOffAndKeepsNoMark)
{
    engine.start(id);
    engine.setFakeBlockValues(id, u"side"_s, {-0.6f, -30.0f, 1.0f, 1.0f});
    ASSERT_TRUE(waitFor(u"left"_s));
    engine.applyEdit(
        id,
        QString::fromUtf8(
            QJsonDocument(core::editCommandToJson(core::SetEnabled{u"side"_s, false})).toJson(QJsonDocument::Compact)));
    EXPECT_TRUE(waitFor(u"off"_s));
    EXPECT_EQ(readings.shown(id, u"side"_s).value(u"strength"_s).toFloat(), 0.0f);
}

TEST_F(SoundSideReadingsTest, NothingIsSaidBetweenChanges)
{
    engine.start(id);
    ASSERT_TRUE(waitFor(u"quiet"_s));
    ASSERT_TRUE(spinUntil([&] {
        readings.read();
        return readings.shown(id, u"side"_s).value(u"level"_s).toInt() == -120;
    })) << "the first snapshot had not come";
    QSignalSpy said(&readings, &SoundSideReadings::changed);
    for (int read = 0; read < 20; ++read) {
        now += kStep;
        readings.read();
    }
    EXPECT_EQ(said.count(), 0) << "nothing playing asks for no frames";
}

TEST_F(SoundSideReadingsTest, TwoHoldersShareOneReadingAndTheLastLetsGo)
{
    engine.start(id);
    readings.hold(id, u"side"_s);
    readings.release(id, u"side"_s);
    EXPECT_TRUE(waitFor(u"quiet"_s)) << "one holder is left";
}
