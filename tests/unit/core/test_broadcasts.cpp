// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What the engine makes of a Broadcast block: the stream's settings read
// off it, the status the facade carries, where its password is kept, and
// the password going to a store and never into the session.

#include "blocks/applicationsource.h"
#include "blocks/broadcast.h"
#include "core/broadcasts.h"
#include "core/editcommand.h"
#include "core/mockengine.h"
#include "core/secretstore.h"
#include "core/session.h"
#include "dsp/paramvalue.h"

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
        // Never destroyed. A static application would be destroyed at
        // exit after Qt's own globals, and Qt 6.10's SQL module (the
        // engine's recordings index) then crashes on its destroyed()
        // signal.
        [[maybe_unused]] static auto *application = new QCoreApplication(argc, argv);
    }
}

[[nodiscard]] QJsonObject parsed(const QString &json)
{
    return QJsonDocument::fromJson(json.toUtf8()).object();
}

} // namespace

TEST(BroadcastsTest, TheStreamsSettingsAreTheBlocksButThePassword)
{
    blocks::Broadcast block;
    ASSERT_TRUE(block.setParam("server", dsp::ParamValue::ofEnum("shoutcast")).has_value());
    ASSERT_TRUE(block.setParam("host", dsp::ParamValue::ofString("  radio.example.org ")).has_value());
    ASSERT_TRUE(block.setParam("port", dsp::ParamValue::ofInt(8010)).has_value());
    ASSERT_TRUE(block.setParam("format", dsp::ParamValue::ofEnum("vorbis")).has_value());
    ASSERT_TRUE(block.setParam("bitrate", dsp::ParamValue::ofInt(96)).has_value());
    ASSERT_TRUE(block.setParam("channels", dsp::ParamValue::ofEnum("mono")).has_value());
    ASSERT_TRUE(block.setParam("stationName", dsp::ParamValue::ofString("Night Shift")).has_value());
    ASSERT_TRUE(block.setParam("listed", dsp::ParamValue::ofBool(true)).has_value());
    ASSERT_TRUE(block.setParam("nowPlayingFile", dsp::ParamValue::ofString("~/now.txt")).has_value());

    stream::BroadcastSettings settings = broadcastSettings(block, u"Show"_s);
    EXPECT_EQ(settings.server.kind, stream::ServerKind::Shoutcast);
    EXPECT_EQ(settings.server.host, "radio.example.org");
    EXPECT_EQ(settings.server.port, 8010);
    EXPECT_EQ(settings.server.user, "source");
    EXPECT_TRUE(settings.server.password.empty()) << "a password is the secret store's";
    EXPECT_TRUE(settings.server.userAgent.starts_with("Reverblabr/"));
    EXPECT_EQ(settings.format, stream::StreamFormat::Vorbis);
    EXPECT_EQ(settings.bitrateKbps, 96U);
    EXPECT_TRUE(settings.mono);
    EXPECT_EQ(settings.station.name, "Night Shift");
    EXPECT_TRUE(settings.station.listed);
    EXPECT_EQ(settings.titleTemplate, "%artist - %title");
    EXPECT_EQ(settings.sessionName, "Show");
    // The file is read only while the title comes from one.
    EXPECT_TRUE(settings.nowPlayingFile.empty());
    ASSERT_TRUE(block.setParam("nowPlaying", dsp::ParamValue::ofEnum("file")).has_value());
    settings = broadcastSettings(block, u"Show"_s);
    EXPECT_EQ(QString::fromStdString(settings.nowPlayingFile.string()), QDir::homePath() + u"/now.txt"_s);
}

TEST(BroadcastsTest, TlsIsAskedOfIcecastAndNeverOfShoutcast)
{
    blocks::Broadcast block;
    EXPECT_FALSE(broadcastSettings(block, u"Show"_s).server.secure) << "a new block's local server is plain";
    ASSERT_TRUE(block.setParam("secure", dsp::ParamValue::ofBool(true)).has_value());
    EXPECT_TRUE(broadcastSettings(block, u"Show"_s).server.secure);
    // SHOUTcast's source protocol has no TLS; the setting is kept for a
    // switch back to Icecast.
    ASSERT_TRUE(block.setParam("server", dsp::ParamValue::ofEnum("shoutcast")).has_value());
    EXPECT_FALSE(broadcastSettings(block, u"Show"_s).server.secure);
    EXPECT_TRUE(*block.param("secure").asBool());
}

TEST(BroadcastsTest, ATitleFollowsAPlayerOnlyWhenItIsToldTo)
{
    blocks::Broadcast block;
    EXPECT_FALSE(broadcastPlayer(block).has_value());
    ASSERT_TRUE(block.setParam("nowPlayingPlayer", dsp::ParamValue::ofString("vlc")).has_value());
    EXPECT_FALSE(broadcastPlayer(block).has_value()) << "a player named while the title comes from nowhere";
    ASSERT_TRUE(block.setParam("nowPlaying", dsp::ParamValue::ofEnum("player")).has_value());
    EXPECT_EQ(broadcastPlayer(block), u"vlc"_s);
    ASSERT_TRUE(block.setParam("nowPlayingPlayer", dsp::ParamValue::ofString(" ")).has_value());
    EXPECT_EQ(broadcastPlayer(block), QString()) << "empty is the one the session captures";
    // A player's title never reads a file.
    ASSERT_TRUE(block.setParam("nowPlayingFile", dsp::ParamValue::ofString("~/now.txt")).has_value());
    EXPECT_TRUE(broadcastSettings(block, u"Show"_s).nowPlayingFile.empty());

    blocks::ApplicationSource source;
    ASSERT_TRUE(source.setParam("binary", dsp::ParamValue::ofString("elisa")).has_value());
    ASSERT_TRUE(source.setParam("flatpakId", dsp::ParamValue::ofString("org.kde.elisa")).has_value());
    const mpris::CapturedApplication captured = capturedApplication(source);
    EXPECT_EQ(captured.binary, u"elisa"_s);
    EXPECT_EQ(captured.flatpakId, u"org.kde.elisa"_s);
    EXPECT_TRUE(captured.name.isEmpty());

    const stream::Track track = streamTrack({u"Sigur Rós"_s, u"Svefn-g-englar"_s, u"Ágætis byrjun"_s});
    EXPECT_EQ(track.artist, "Sigur Rós");
    EXPECT_EQ(track.title, "Svefn-g-englar");
    EXPECT_EQ(track.album, "Ágætis byrjun");
}

TEST(BroadcastsTest, TheStatusIsWhatTheInterfaceXmlDescribes)
{
    stream::BroadcastStatus status;
    status.state = stream::BroadcastStatus::State::Retrying;
    status.error = stream::StreamError::Closed;
    status.bytesSent = 123456;
    status.kbps = 128.04;
    status.reconnects = 2;
    status.retryInMs = 4000;
    status.title = "Ana - Morning";
    const QJsonObject json = broadcastStatusJson(status);
    EXPECT_EQ(json.value(u"state"_s).toString(), u"retrying"_s);
    EXPECT_EQ(json.value(u"error"_s).toString(), u"closed"_s);
    EXPECT_EQ(json.value(u"bytesSent"_s).toDouble(), 123456.0);
    EXPECT_DOUBLE_EQ(json.value(u"kbps"_s).toDouble(), 128.0);
    EXPECT_EQ(json.value(u"reconnects"_s).toInt(), 2);
    EXPECT_EQ(json.value(u"retryIn"_s).toInt(), 4000);
    EXPECT_EQ(json.value(u"title"_s).toString(), u"Ana - Morning"_s);
    EXPECT_EQ(json.value(u"connectedSince"_s).toDouble(), 0.0);
    EXPECT_FALSE(json.value(u"secure"_s).toBool());
    status.state = stream::BroadcastStatus::State::Connected;
    status.secure = true;
    EXPECT_TRUE(broadcastStatusJson(status).value(u"secure"_s).toBool());
    EXPECT_FALSE(broadcastStatusJson(stream::BroadcastStatus{}).contains(u"error"_s)) << "nothing went wrong";
}

TEST(BroadcastsTest, APasswordIsKeptUnderItsSessionAndItsBlock)
{
    EXPECT_EQ(broadcastSecretKey(u"s1"_s, u"air"_s), u"broadcast/s1/air"_s);
    EXPECT_NE(broadcastSecretKey(u"s1"_s, u"air"_s), broadcastSecretKey(u"s2"_s, u"air"_s));
}

TEST(BroadcastsTest, TheMemoryStoreAnswersLaterAsTheDesktopsDoes)
{
    needApplication();
    MemorySecretStore store;
    QObject context;
    std::optional<SecretResult> answer;
    store.write(u"k"_s, u"hackme"_s, &context, [&answer](const SecretResult &result) { answer = result; });
    EXPECT_FALSE(answer.has_value()) << "answered before the event loop ran";
    QCoreApplication::processEvents();
    ASSERT_TRUE(answer && answer->ok);

    answer.reset();
    store.read(u"k"_s, &context, [&answer](const SecretResult &result) { answer = result; });
    QCoreApplication::processEvents();
    ASSERT_TRUE(answer && answer->ok);
    EXPECT_EQ(answer->secret, u"hackme"_s);

    store.write(u"k"_s, QString(), &context, [](const SecretResult &) { });
    QCoreApplication::processEvents();
    answer.reset();
    store.read(u"k"_s, &context, [&answer](const SecretResult &result) { answer = result; });
    QCoreApplication::processEvents();
    ASSERT_TRUE(answer && answer->ok);
    EXPECT_FALSE(answer->secret.has_value()) << "an empty password removes it";

    store.setBroken(true);
    answer.reset();
    store.read(u"k"_s, &context, [&answer](const SecretResult &result) { answer = result; });
    QCoreApplication::processEvents();
    ASSERT_TRUE(answer.has_value());
    EXPECT_FALSE(answer->ok);
    EXPECT_EQ(answer->error, u"noSecretStore"_s);
}

TEST(BroadcastsTest, TheMocksPasswordIsSaidStoredAndNeverInTheSession)
{
    needApplication();
    MockEngine engine;
    QSignalSpy opened(&engine, &EngineFacade::sessionOpened);
    QSignalSpy statuses(&engine, &EngineFacade::broadcastStatusChanged);
    QSignalSpy errors(&engine, &EngineFacade::error);
    QSignalSpy snapshots(&engine, &EngineFacade::sessionSnapshot);
    engine.createSession(u"On Air"_s);
    ASSERT_EQ(opened.count(), 1);
    const QString id = opened.back().at(0).toString();
    BlockInstance block;
    block.id = u"air"_s;
    block.type = u"output.broadcast"_s;
    AddBlock add;
    add.block = block;
    engine.applyEdit(id, QString::fromUtf8(QJsonDocument(editCommandToJson(add)).toJson(QJsonDocument::Compact)));

    engine.setBroadcastPassword(id, u"air"_s, u"hackme"_s);
    ASSERT_EQ(statuses.count(), 1);
    EXPECT_TRUE(parsed(statuses.back().at(2).toString()).value(u"passwordStored"_s).toBool());
    engine.start(id);
    EXPECT_EQ(parsed(statuses.back().at(2).toString()).value(u"state"_s).toString(), u"connected"_s);

    engine.resynchronise(id);
    EXPECT_FALSE(snapshots.back().at(2).toString().contains(u"hackme"_s)) << "the password is in the session";

    engine.setBroadcastPassword(id, u"nothere"_s, u"x"_s);
    ASSERT_EQ(errors.count(), 1);
    EXPECT_EQ(errors.back().at(0).toString(), u"broadcast.passwordNotSaved"_s);
    EXPECT_EQ(parsed(errors.back().at(1).toString()).value(u"reason"_s).toString(), u"notABroadcast"_s);
}
