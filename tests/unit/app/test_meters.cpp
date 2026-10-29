// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Where the window's meters read (ADR-0018): an engine in another
// process has no address for a session's levels until their memory is
// mapped, and takes it away again when the session closes or the daemon
// goes. The meters read afresh whenever they are told it moved.

#include "appshell.h"
#include "core/mockengine.h"
#include "meters.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <memory>

using namespace rb;
using namespace Qt::StringLiterals;

namespace
{

/// **The meters tick on a timer**, which needs an application to exist in.
class MeterAddressTest : public ::testing::Test
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

    void TearDown() override
    {
        ui::AppShell::setEngine(nullptr);
    }

private:
    static std::array<char, 7> s_name;
    static std::array<char *, 2> s_argv;
    static int s_argc;
    static std::unique_ptr<QCoreApplication> s_application;
};

std::array<char, 7> MeterAddressTest::s_name{'m', 'e', 't', 'e', 'r', 's', '\0'};
std::array<char *, 2> MeterAddressTest::s_argv{};
int MeterAddressTest::s_argc = 1;
std::unique_ptr<QCoreApplication> MeterAddressTest::s_application;

/// Turns the event loop until \a done holds or half a second has gone.
template <typename Predicate>
bool spinUntil(Predicate done)
{
    QElapsedTimer clock;
    clock.start();
    while (!done() && clock.elapsed() < 500) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return done();
}

} // namespace

TEST_F(MeterAddressTest, AMovedAddressIsReadAfreshForTheSessionWatchedAndNoOther)
{
    core::MockEngine engine;
    core::TelemetrySlot slot;
    // Where the engine in another process says the levels are: nowhere,
    // until their memory has been mapped.
    const core::TelemetrySlot *where = nullptr;
    ui::AppShell::setEngine(&engine, [&](const QString &sessionId) { return sessionId == "s"_L1 ? where : nullptr; });

    ui::Meters meters;
    QSignalSpy watching(&meters, &ui::Meters::watching);
    meters.watch(u"s"_s);
    ASSERT_EQ(watching.count(), 1);
    EXPECT_FALSE(meters.isLive());

    core::TelemetrySnapshot snapshot;
    snapshot.frame = 1;
    snapshot.running = true;
    snapshot.blockCount = 1;
    std::strncpy(snapshot.blocks[0].id, "volume", core::kMaxTelemetryIdBytes - 1);
    snapshot.blocks[0].channelCount = 1;
    snapshot.blocks[0].peak[0] = 0.5f;
    snapshot.blocks[0].active = true;
    slot.store(snapshot);
    where = &slot;

    ui::AppShell::telemetryMoved(u"another"_s);
    EXPECT_EQ(watching.count(), 1) << "read afresh for a session it does not watch";

    // **Mapped**: the meters read there from the next frame on.
    ui::AppShell::telemetryMoved(u"s"_s);
    EXPECT_EQ(watching.count(), 2);
    EXPECT_TRUE(spinUntil([&] { return meters.isLive(); })) << "the mapped levels were never read";

    // **Going**: they let go before the memory does.
    where = nullptr;
    ui::AppShell::telemetryMoved(u"s"_s);
    EXPECT_EQ(watching.count(), 3);
    EXPECT_FALSE(meters.isLive());
}

TEST_F(MeterAddressTest, TheAnalysedBlocksBandValuesAndSoundBeforeItAreReadForItAlone)
{
    // A panel asks for its block's spectra of one ear, and reads the
    // block's band values and the spectrum before it beside the one after.
    core::MockEngine engine;
    QSignalSpy opened(&engine, &core::EngineFacade::sessionOpened);
    engine.createSession(u"Hearing"_s);
    ASSERT_EQ(opened.count(), 1);
    const QString sessionId = opened.at(0).at(0).toString();
    core::TelemetrySlot slot;
    ui::AppShell::setEngine(&engine, [&](const QString &id) { return id == sessionId ? &slot : nullptr; });

    ui::Meters meters;
    meters.watch(sessionId);
    meters.analyse(u"left ear"_s, 0);
    EXPECT_EQ(engine.analysed(sessionId), u"left ear"_s);
    EXPECT_EQ(engine.analysedChannel(sessionId), 0) << "the ear was not passed on";
    EXPECT_EQ(meters.analysedChannel(), 0);

    core::TelemetrySnapshot snapshot;
    snapshot.frame = 1;
    snapshot.running = true;
    std::strncpy(snapshot.analysedId, "left ear", core::kMaxTelemetryIdBytes - 1);
    snapshot.analysedChannel = 0;
    snapshot.spectrumBands = core::kSpectrumBands;
    snapshot.inputSpectrumBands = core::kSpectrumBands;
    for (std::size_t band = 0; band < core::kSpectrumBands; ++band) {
        snapshot.spectrum[band] = -40.0f;
        snapshot.inputSpectrum[band] = -30.0f - static_cast<float>(band) * 0.1f;
    }
    snapshot.bandValueCount = rt::kMaxBandValues;
    for (std::size_t band = 0; band < rt::kMaxBandValues; ++band) {
        snapshot.bandValues[band] = static_cast<float>(band) * 0.5f;
    }
    slot.store(snapshot);
    ASSERT_TRUE(spinUntil([&] { return meters.bandValues(u"left ear"_s).size() == rt::kMaxBandValues; }))
        << "the band values were never read";

    const QVariantList values = meters.bandValues(u"left ear"_s);
    const QVariantList before = meters.inputSpectrum(u"left ear"_s);
    ASSERT_EQ(before.size(), static_cast<qsizetype>(core::kSpectrumBands));
    for (qsizetype band = 0; band < values.size(); ++band) {
        EXPECT_DOUBLE_EQ(values.at(band).toDouble(), static_cast<double>(band) * 0.5) << band;
    }
    EXPECT_FLOAT_EQ(before.at(10).toFloat(), -31.0f);
    EXPECT_TRUE(meters.bandValues(u"right ear"_s).isEmpty()) << "another block's panel was given these";
    EXPECT_TRUE(meters.inputSpectrum(u"right ear"_s).isEmpty());

    // **A block with no inputs has no spectrum before it**, rather than one
    // of silence.
    snapshot.frame = 2;
    snapshot.inputSpectrumBands = 0;
    slot.store(snapshot);
    ASSERT_TRUE(spinUntil([&] { return meters.inputSpectrum(u"left ear"_s).isEmpty(); }));

    // The other ear of the same block is a new question for the engine.
    meters.analyse(u"left ear"_s, 1);
    EXPECT_EQ(engine.analysedChannel(sessionId), 1);
}
