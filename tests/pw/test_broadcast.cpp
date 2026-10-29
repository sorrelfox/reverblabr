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
// with the session and with a block taken out for good.

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

#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <chrono>

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

} // namespace

using Broadcasting = SessionTest;

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
