// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What the engine makes of a Live Stream block: the stream's settings read
// off it, the status the facade carries, the encoders the catalogue says
// are missing, and the key going where a Broadcast block's password goes.

#include "blocks/livestream.h"
#include "core/broadcasts.h"
#include "core/editcommand.h"
#include "core/livestreams.h"
#include "core/mockengine.h"
#include "core/session.h"
#include "dsp/paramvalue.h"
#include "rt/audiotap.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <gtest/gtest.h>

using namespace rb;
using namespace rb::core;
using namespace Qt::StringLiterals;

namespace
{

void needApplication()
{
    if (QCoreApplication::instance() == nullptr) {
        static int argc = 1;
        static char name[] = "reverblabr-engine-test";
        static char *argv[] = {name, nullptr};
        static QCoreApplication application(argc, argv);
    }
}

[[nodiscard]] QJsonObject parsed(const QString &json)
{
    return QJsonDocument::fromJson(json.toUtf8()).object();
}

} // namespace

TEST(LiveStreamsTest, TheStreamsSettingsAreTheBlocksButTheKey)
{
    blocks::LiveStream block;
    ASSERT_TRUE(block.setParam("url", dsp::ParamValue::ofString("  rtmp://live.example.com/app ")).has_value());
    ASSERT_TRUE(block.setParam("quality", dsp::ParamValue::ofEnum("good")).has_value());
    ASSERT_TRUE(block.setParam("encoder", dsp::ParamValue::ofEnum("openh264")).has_value());
    ASSERT_TRUE(block.setParam("subtitle", dsp::ParamValue::ofString("Live from %session")).has_value());
    ASSERT_TRUE(block.setParam("visualisation", dsp::ParamValue::ofEnum("meter")).has_value());
    ASSERT_TRUE(block.setParam("background", dsp::ParamValue::ofString("#ffffff")).has_value());
    ASSERT_TRUE(block.setParam("logo", dsp::ParamValue::ofString("~/logo.png")).has_value());
    ASSERT_TRUE(block.setParam("nowPlaying", dsp::ParamValue::ofEnum("file")).has_value());
    ASSERT_TRUE(block.setParam("nowPlayingFile", dsp::ParamValue::ofString("~/now.txt")).has_value());

    const stream::LiveStreamSettings settings = liveStreamSettings(block, u"Night Shift"_s);
    EXPECT_EQ(settings.url, "rtmp://live.example.com/app");
    EXPECT_EQ(settings.quality, stream::liveQuality(stream::LiveQualityPreset::Good));
    EXPECT_EQ(settings.encoder, stream::VideoEncoderKind::OpenH264);
    EXPECT_EQ(settings.scene.title, "Night Shift") << "the default title is the session's name";
    EXPECT_EQ(settings.scene.subtitle, "Live from Night Shift");
    EXPECT_EQ(settings.scene.visualisation, stream::Visualisation::Meter);
    EXPECT_EQ(settings.scene.background, (stream::Rgb{255, 255, 255}));
    EXPECT_EQ(settings.scene.logo, std::filesystem::path{QString(QDir::homePath() + u"/logo.png"_s).toStdString()});
    EXPECT_EQ(settings.nowPlayingFile, std::filesystem::path{QString(QDir::homePath() + u"/now.txt"_s).toStdString()});
    EXPECT_EQ(settings.titleTemplate, "%artist - %title");

    // "auto" is no encoder in particular; a colour that is not one is the
    // default.
    ASSERT_TRUE(block.setParam("encoder", dsp::ParamValue::ofEnum("auto")).has_value());
    ASSERT_TRUE(block.setParam("background", dsp::ParamValue::ofString("blue")).has_value());
    const stream::LiveStreamSettings automatic = liveStreamSettings(block, u"Night Shift"_s);
    EXPECT_FALSE(automatic.encoder.has_value());
    EXPECT_EQ(automatic.scene.background, (stream::Rgb{0x16, 0x18, 0x1d}));
}

// **A player is followed for it as for a Broadcast block**: the same
// setting, read by the same function.
TEST(LiveStreamsTest, ItsTitleFollowsAPlayerAsBroadcastsDoes)
{
    blocks::LiveStream block;
    EXPECT_FALSE(broadcastPlayer(block).has_value());
    ASSERT_TRUE(block.setParam("nowPlaying", dsp::ParamValue::ofEnum("player")).has_value());
    ASSERT_TRUE(block.setParam("nowPlayingPlayer", dsp::ParamValue::ofString("vlc")).has_value());
    EXPECT_EQ(broadcastPlayer(block), u"vlc"_s);
    EXPECT_TRUE(nowPlayingFileOf(block).empty());
    EXPECT_TRUE(isStreamingType("output.livestream"));
    EXPECT_TRUE(isStreamingType("output.broadcast"));
    EXPECT_FALSE(isStreamingType("output.recorder"));
}

TEST(LiveStreamsTest, TheRuntimeMakesAndConfiguresItsStream)
{
    blocks::LiveStream block;
    auto tap = std::make_shared<rt::AudioTap>(2, 4800);
    std::unique_ptr<stream::OutputStream> made = makeOutputStream(block, tap, [](const QJsonObject &) { });
    ASSERT_NE(made, nullptr);
    auto *live = dynamic_cast<stream::LiveStream *>(made.get());
    ASSERT_NE(live, nullptr);
    configureOutputStream(*made, block, u"Show"_s);
    made->requestStop();
}

TEST(LiveStreamsTest, TheStatusIsWhatTheInterfaceXmlDescribes)
{
    stream::LiveStreamStatus status;
    status.stream.state = stream::BroadcastStatus::State::Connected;
    status.stream.bytesSent = 4096;
    status.stream.title = "Artist - Song";
    status.encoder = "x264";
    status.width = 1280;
    status.height = 720;
    status.fps = 30;
    status.framesSent = 900;
    status.framesDropped = 3;
    const QJsonObject json = liveStreamStatusJson(status);
    EXPECT_EQ(json.value(u"state"_s).toString(), u"connected"_s);
    EXPECT_EQ(json.value(u"bytesSent"_s).toDouble(), 4096.0);
    EXPECT_EQ(json.value(u"title"_s).toString(), u"Artist - Song"_s);
    EXPECT_EQ(json.value(u"encoder"_s).toString(), u"x264"_s);
    EXPECT_EQ(json.value(u"width"_s).toInt(), 1280);
    EXPECT_EQ(json.value(u"height"_s).toInt(), 720);
    EXPECT_EQ(json.value(u"fps"_s).toInt(), 30);
    EXPECT_EQ(json.value(u"framesSent"_s).toDouble(), 900.0);
    EXPECT_EQ(json.value(u"framesDropped"_s).toDouble(), 3.0);
    EXPECT_FALSE(json.contains(u"error"_s));
}

TEST(LiveStreamsTest, TheCatalogueSaysWhichEncodersAreMissing)
{
    stream::VideoEncoderProbe probe;
    probe.openh264 = true;
    const UnavailableChoices some = liveStreamUnavailableChoices(probe);
    const auto &encoders = some.at({"output.livestream", "encoder"});
    EXPECT_EQ(encoders.at("vaapi"), "encoderMissing");
    EXPECT_EQ(encoders.at("nvenc"), "encoderMissing");
    EXPECT_EQ(encoders.at("x264"), "encoderMissing");
    EXPECT_FALSE(encoders.contains("openh264"));
    EXPECT_FALSE(encoders.contains("auto"));

    // **With none at all, the block cannot stream here**, and says so.
    const UnavailableChoices none = liveStreamUnavailableChoices(stream::VideoEncoderProbe{});
    EXPECT_EQ(none.at({"output.livestream", "encoder"}).at("auto"), "noH264Encoder");
}

TEST(LiveStreamsTest, TheMocksKeyIsSaidStoredAndNeverInTheSession)
{
    needApplication();
    MockEngine engine;
    QSignalSpy opened(&engine, &EngineFacade::sessionOpened);
    QSignalSpy statuses(&engine, &EngineFacade::broadcastStatusChanged);
    QSignalSpy snapshots(&engine, &EngineFacade::sessionSnapshot);
    engine.createSession(u"On Air"_s);
    ASSERT_EQ(opened.count(), 1);
    const QString id = opened.back().at(0).toString();
    BlockInstance block;
    block.id = u"live"_s;
    block.type = u"output.livestream"_s;
    AddBlock add;
    add.block = block;
    engine.applyEdit(id, QString::fromUtf8(QJsonDocument(editCommandToJson(add)).toJson(QJsonDocument::Compact)));

    engine.setBroadcastPassword(id, u"live"_s, u"live_0123456789"_s);
    ASSERT_EQ(statuses.count(), 1);
    EXPECT_TRUE(parsed(statuses.back().at(2).toString()).value(u"passwordStored"_s).toBool());
    engine.resynchronise(id);
    EXPECT_FALSE(snapshots.back().at(2).toString().contains(u"live_0123456789"_s)) << "the key is in the session";
}

// **The engine's thread never waits for the graphics cards**: the first
// catalogue offers what is known without them, and the catalogue is said
// again once they have answered.
TEST(LiveStreamsTest, TheCatalogueIsSaidAgainOnceTheCardsHaveAnswered)
{
    needApplication();
    const bool knownBefore = stream::finishedVideoEncoderProbe().has_value();
    MockEngine engine;
    QSignalSpy catalogues(&engine, &EngineFacade::blockCatalogueChanged);
    engine.getBlockCatalogue();
    ASSERT_GE(catalogues.count(), 1);
    if (!knownBefore) {
        EXPECT_TRUE(catalogues.wait(20000) || catalogues.count() >= 2) << "the catalogue was not said again";
        EXPECT_TRUE(stream::finishedVideoEncoderProbe().has_value());
    }
    // From then on, once a request.
    const qsizetype said = catalogues.count();
    engine.getBlockCatalogue();
    EXPECT_EQ(catalogues.count(), said + 1);
}
