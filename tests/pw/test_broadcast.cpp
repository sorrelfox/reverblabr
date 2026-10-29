// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// A Broadcast block in a real engine, in a private PipeWire, with a secret
// store in memory and a server made here: the password is asked for when
// the stream starts, a new one is used at once, a store that could not be
// asked is said as such and asked again next time, and the passwords go
// with the session and with a block taken out for good. And its title,
// from a media player of the test's own on the harness's private bus.

#include "blocks/applicationsource.h"
#include "blocks/broadcast.h"
#include "blocks/builtinblocks.h"
#include "blocks/wave.h"
#include "core/broadcasts.h"
#include "core/editcommand.h"
#include "core/engine.h"
#include "core/secretstore.h"
#include "core/session.h"
#include "dsp/blockregistry.h"
#include "fakeserver.h"
#include "sessionharness.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <chrono>
#include <list>

using namespace rb;
using namespace rb::core;
using namespace rb::pwtest;
using namespace Qt::StringLiterals;
using namespace std::chrono_literals;

namespace
{

[[nodiscard]] QString asJson(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

[[nodiscard]] QString addBlock(const QString &id, std::string_view type, const QJsonObject &params = {})
{
    BlockInstance block;
    block.id = id;
    block.type = QString::fromUtf8(type.data(), static_cast<qsizetype>(type.size()));
    block.params = params;
    AddBlock command;
    command.block = block;
    return asJson(editCommandToJson(command));
}

[[nodiscard]] QString connect(const QString &from, const QString &to, std::uint32_t channel)
{
    Wire wire;
    wire.fromBlock = from;
    wire.fromPort = channel;
    wire.toBlock = to;
    wire.toPort = channel;
    return asJson(editCommandToJson(Connect{wire, -1}));
}

[[nodiscard]] QJsonObject lastStatus(const QSignalSpy &statuses, const QString &blockId)
{
    for (qsizetype index = statuses.count() - 1; index >= 0; --index) {
        if (statuses.at(index).at(1).toString() == blockId) {
            return QJsonDocument::fromJson(statuses.at(index).at(2).toString().toUtf8()).object();
        }
    }
    return {};
}

/// **Media players of the test's own** (tests/bus/fakemprisplayer.cpp),
/// each a process on the harness's private bus, stopped while that bus is
/// still there.
class FakePlayers
{
public:
    ~FakePlayers()
    {
        for (QProcess &player : m_players) {
            player.terminate();
            if (!player.waitForFinished(5000)) {
                player.kill();
                player.waitForFinished(5000);
            }
        }
    }

    /// org.mpris.MediaPlayer2.\a name, started as \a program.
    [[nodiscard]] bool start(const QString &name, const QString &program)
    {
        QProcess &player = m_players.emplace_back();
        player.setReadChannel(QProcess::StandardOutput);
        player.start(program, {u"--name"_s, name});
        return player.waitForStarted(5000) && player.waitForReadyRead(10000) && player.readLine().trimmed() == "ready";
    }

    [[nodiscard]] static bool tell(const QString &name, const QString &member, const QVariantList &arguments)
    {
        QDBusMessage call = QDBusMessage::createMethodCall(
            u"org.mpris.MediaPlayer2."_s + name, u"/org/mpris/MediaPlayer2"_s, u"org.reverblabr.Test"_s, member);
        call.setArguments(arguments);
        return QDBusConnection::sessionBus().call(call, QDBus::Block, 5000).type() == QDBusMessage::ReplyMessage;
    }

private:
    std::list<QProcess> m_players;
};

} // namespace

using Broadcasting = SessionTest;

// **A title change in the player the session captures reaches the
// listeners within two seconds**, and one in another player that is playing
// too does not; a session's Application block is what says which.
TEST_F(Broadcasting, TheTitleFollowsThePlayerTheSessionCaptures)
{
    const QByteArray address = qgetenv("DBUS_SESSION_BUS_ADDRESS");
    ASSERT_FALSE(address.isEmpty());
    ASSERT_FALSE(address.contains(QByteArray("/run/user/"))) << "this is a desktop's bus; run it through ctest";
    QTemporaryDir links;
    ASSERT_TRUE(links.isValid());
    const QString radio = links.filePath(u"fake-radio-player"_s);
    ASSERT_TRUE(QFile::link(QStringLiteral(RB_FAKE_MPRIS_PLAYER), radio));
    FakePlayers players;
    ASSERT_TRUE(players.start(u"fakeradio"_s, radio));
    ASSERT_TRUE(players.start(u"fakeother"_s, QStringLiteral(RB_FAKE_MPRIS_PLAYER)));
    for (const QString &name : {u"fakeradio"_s, u"fakeother"_s}) {
        ASSERT_TRUE(FakePlayers::tell(name, u"SetTrack"_s, {u"Artist"_s, name, QString()}));
        ASSERT_TRUE(FakePlayers::tell(name, u"SetPlayback"_s, {u"Playing"_s}));
    }

    test::FakeStreamServer server;
    QTemporaryDir sessions;
    ASSERT_TRUE(sessions.isValid());
    dsp::BlockRegistry registry;
    ASSERT_TRUE(blocks::registerBuiltinBlocks(registry));
    Engine engine(registry, SessionStore{sessions.path()});
    engine.setSecretStore(std::make_unique<MemorySecretStore>());
    QSignalSpy opened(&engine, &EngineFacade::sessionOpened);
    QSignalSpy statuses(&engine, &EngineFacade::broadcastStatusChanged);
    engine.connectToPipeWire();
    Waiter pump;
    ASSERT_TRUE(pump.pumpUntil([&] { return engine.isConnected(); }));
    engine.createSession(u"Radio"_s);
    ASSERT_EQ(opened.count(), 1);
    const QString id = opened.back().at(0).toString();
    engine.applyEdit(
        id, addBlock(u"player"_s, blocks::ApplicationSource::kTypeId, {{"binary"_L1, u"fake-radio-player"_s}}));
    engine.applyEdit(id, addBlock(u"wave"_s, blocks::Wave::kTypeId, {{"level"_L1, -12.0}}));
    engine.applyEdit(id,
                     addBlock(u"air"_s,
                              blocks::Broadcast::kTypeId,
                              {{"host"_L1, u"127.0.0.1"_s},
                               {"port"_L1, server.port()},
                               {"mount"_L1, u"/live"_s},
                               {"nowPlaying"_L1, u"player"_s}}));
    for (std::uint32_t channel = 0; channel < 2; ++channel) {
        engine.applyEdit(id, connect(u"wave"_s, u"air"_s, channel));
    }
    engine.setBroadcastPassword(id, u"air"_s, u"hackme"_s);
    engine.start(id);
    const auto title = [&] { return lastStatus(statuses, u"air"_s).value(u"title"_s).toString(); };
    ASSERT_TRUE(pump.pumpUntil([&] { return title() == u"Artist - fakeradio"_s; }))
        << title().toStdString() << " / " << lastStatus(statuses, u"air"_s).value(u"state"_s).toString().toStdString();

    // The other player is playing, and is not in the session.
    ASSERT_TRUE(FakePlayers::tell(u"fakeother"_s, u"SetTrack"_s, {u"Someone"_s, u"Elsewhere"_s, QString()}));
    QElapsedTimer clock;
    clock.start();
    ASSERT_TRUE(FakePlayers::tell(u"fakeradio"_s, u"SetTrack"_s, {u"Nils Frahm"_s, u"Says"_s, u"Spaces"_s}));
    ASSERT_TRUE(pump.pumpUntil([&] { return title() == u"Nils Frahm - Says"_s; }, 2000ms))
        << "the new title did not reach the stream within two seconds: " << title().toStdString();
    EXPECT_LT(clock.elapsed(), 2000);

    // **Named instead**: the other one, at once.
    engine.applyEdit(id, asJson(editCommandToJson(SetParam{u"air"_s, u"nowPlayingPlayer"_s, u"fakeother"_s})));
    ASSERT_TRUE(pump.pumpUntil([&] { return title() == u"Someone - Elsewhere"_s; }, 2000ms)) << title().toStdString();

    // Back to the template alone: the player's title is forgotten.
    engine.applyEdit(id, asJson(editCommandToJson(SetParam{u"air"_s, u"titleTemplate"_s, u"%session"_s})));
    engine.applyEdit(id, asJson(editCommandToJson(SetParam{u"air"_s, u"nowPlaying"_s, u"none"_s})));
    ASSERT_TRUE(pump.pumpUntil([&] { return title() == u"Radio"_s; }, 2000ms)) << title().toStdString();
    engine.stop(id);
}

TEST_F(Broadcasting, ThePasswordComesFromTheStoreAndGoesWithTheSession)
{
    test::FakeStreamServer server;
    QTemporaryDir sessions;
    ASSERT_TRUE(sessions.isValid());
    dsp::BlockRegistry registry;
    ASSERT_TRUE(blocks::registerBuiltinBlocks(registry));
    Engine engine(registry, SessionStore{sessions.path()});
    auto owned = std::make_unique<MemorySecretStore>();
    MemorySecretStore &store = *owned;
    engine.setSecretStore(std::move(owned));
    QSignalSpy opened(&engine, &EngineFacade::sessionOpened);
    QSignalSpy statuses(&engine, &EngineFacade::broadcastStatusChanged);
    QSignalSpy errors(&engine, &EngineFacade::error);

    engine.connectToPipeWire();
    Waiter pump;
    ASSERT_TRUE(pump.pumpUntil([&] { return engine.isConnected(); }));
    engine.createSession(u"On Air"_s);
    ASSERT_EQ(opened.count(), 1);
    const QString id = opened.back().at(0).toString();
    const QJsonObject where{{"host"_L1, u"127.0.0.1"_s}, {"port"_L1, server.port()}, {"mount"_L1, u"/live"_s}};
    engine.applyEdit(id, addBlock(u"wave"_s, blocks::Wave::kTypeId, {{"level"_L1, -12.0}}));
    engine.applyEdit(id, addBlock(u"air"_s, blocks::Broadcast::kTypeId, where));
    engine.applyEdit(id, addBlock(u"spare"_s, blocks::Broadcast::kTypeId, where));
    for (std::uint32_t channel = 0; channel < 2; ++channel) {
        engine.applyEdit(id, connect(u"wave"_s, u"air"_s, channel));
    }
    engine.applyEdit(id, asJson(editCommandToJson(SetEnabled{u"spare"_s, false})));

    // **A store that cannot be asked is not "no password"**: it is said as
    // it is, with the store's reason, and not remembered.
    store.setBroken(true);
    engine.start(id);
    ASSERT_TRUE(
        pump.pumpUntil([&] { return lastStatus(statuses, u"air"_s).value(u"error"_s) == u"passwordUnavailable"_s; }));
    ASSERT_TRUE(pump.pumpUntil([&] { return !errors.isEmpty(); }));
    EXPECT_EQ(errors.back().at(0).toString(), u"broadcast.failed"_s);
    EXPECT_TRUE(errors.back().at(1).toString().contains(u"noSecretStore"_s))
        << errors.back().at(1).toString().toStdString();
    engine.stop(id);

    // **Asked again when the stream starts again**: there is none yet.
    store.setBroken(false);
    engine.start(id);
    ASSERT_TRUE(pump.pumpUntil([&] { return lastStatus(statuses, u"air"_s).value(u"error"_s) == u"noPassword"_s; }));
    EXPECT_EQ(server.sources(), 0U);

    // A new one is stored, and used at once.
    engine.setBroadcastPassword(id, u"air"_s, u"hackme"_s);
    ASSERT_TRUE(pump.pumpUntil([&] { return lastStatus(statuses, u"air"_s).value(u"state"_s) == u"connected"_s; }))
        << lastStatus(statuses, u"air"_s).value(u"state"_s).toString().toStdString();
    EXPECT_EQ(store.secrets().value(broadcastSecretKey(id, u"air"_s)), u"hackme"_s);
    EXPECT_TRUE(lastStatus(statuses, u"air"_s).value(u"passwordStored"_s).toBool());
    ASSERT_EQ(server.sources(), 1U);
    // source:hackme
    EXPECT_NE(server.requests()[0].find("Authorization: Basic c291cmNlOmhhY2ttZQ=="), std::string::npos);
    engine.stop(id);

    // A block taken out keeps its password while an undo can bring it
    // back, and loses it when the session closes and the history goes.
    engine.setBroadcastPassword(id, u"spare"_s, u"other"_s);
    ASSERT_TRUE(pump.pumpUntil([&] { return store.secrets().contains(broadcastSecretKey(id, u"spare"_s)); }));
    RemoveBlock remove;
    remove.blockId = u"spare"_s;
    engine.applyEdit(id, asJson(editCommandToJson(remove)));
    pump.pump(100ms);
    EXPECT_TRUE(store.secrets().contains(broadcastSecretKey(id, u"spare"_s)));
    engine.closeSession(id);
    ASSERT_TRUE(pump.pumpUntil([&] { return !store.secrets().contains(broadcastSecretKey(id, u"spare"_s)); }));
    EXPECT_TRUE(store.secrets().contains(broadcastSecretKey(id, u"air"_s)));

    // **And the rest go with the session.**
    engine.deleteSession(id);
    ASSERT_TRUE(pump.pumpUntil([&] { return store.secrets().isEmpty(); }));
}
