// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Autosave: there is no Save, and an edit is on the disk a second
// later — or at once, if the session is closed or stopped before the second
// is up, or if the engine goes away.
//
// **No PipeWire here.** Making a session and editing it needs no server, and
// this is the half of the engine that can be tested in milliseconds rather
// than in a private PipeWire instance. What autosave writes is the same
// session file `test_sessionstore.cpp` already reads back field for field.

#include "blocks/builtinblocks.h"
#include "core/editcommand.h"
#include "core/engine.h"
#include "core/session.h"
#include "core/sessionstore.h"
#include "dsp/blockregistry.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <array>
#include <memory>

using namespace rb;
using namespace rb::core;
using namespace Qt::StringLiterals;

namespace
{

[[nodiscard]] dsp::BlockRegistry builtins()
{
    dsp::BlockRegistry registry;
    EXPECT_TRUE(blocks::registerBuiltinBlocks(registry));
    return registry;
}

[[nodiscard]] QString asJson(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

/// A parameter value as the interface sends it: **wrapped in an array**,
/// because Qt will not parse a bare number or a bare string as a document.
[[nodiscard]] QString paramJson(const QJsonValue &value)
{
    return QString::fromUtf8(QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact));
}

[[nodiscard]] QString addBlockEdit(const QString &blockId, const QString &type)
{
    BlockInstance block;
    block.id = blockId;
    block.type = type;
    return asJson(editCommandToJson(AddBlock{block, -1}));
}

/// Turns the event loop until \a predicate holds or \a milliseconds pass.
/// **The autosave is a timer**, so a test that only checked the disk once
/// would be testing how fast the machine is.
template <typename Predicate>
bool waitFor(Predicate predicate, int milliseconds = 4000)
{
    QElapsedTimer clock;
    clock.start();
    while (!predicate() && clock.elapsed() < milliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return predicate();
}

/// One session in a directory of its own, opened through the facade.
class Autosave : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // **A timer needs a thread with an event dispatcher on it**, which is
        // what an application object gives a test with no event loop of its
        // own. Autosave is a timer, so without this it would never fire and
        // the test would prove the opposite of what it says.
        application = std::make_unique<QCoreApplication>(argc, argv.data());
        ASSERT_TRUE(sessions.isValid());
        engine = std::make_unique<Engine>(registry, SessionStore{sessions.path()});
        store = std::make_unique<SessionStore>(sessions.path());
        QSignalSpy opened(engine.get(), &EngineFacade::sessionOpened);
        engine->createSession(u"Autosaved"_s);
        ASSERT_EQ(opened.count(), 1);
        id = opened.at(0).at(0).toString();
    }

    /// The session as the disk holds it now.
    [[nodiscard]] std::expected<Session, SessionLoadFailure> onDisk() const
    {
        return store->load(QUuid(id));
    }

    /// How many blocks the disk says the session has.
    [[nodiscard]] qsizetype blocksOnDisk() const
    {
        const std::expected<Session, SessionLoadFailure> loaded = onDisk();
        return loaded ? loaded->blocks.size() : -1;
    }

    void TearDown() override
    {
        // The engine goes before the application it borrowed the dispatcher
        // from, and its destructor is one of the things under test.
        engine.reset();
        application.reset();
    }

    int argc = 1;
    char name[9] = "autosave";
    std::array<char *, 2> argv{static_cast<char *>(name), nullptr};
    std::unique_ptr<QCoreApplication> application;
    dsp::BlockRegistry registry = builtins();
    QTemporaryDir sessions;
    std::unique_ptr<Engine> engine;
    std::unique_ptr<SessionStore> store;
    QString id;
};

} // namespace

TEST_F(Autosave, AnEditIsOnTheDiskWithoutAnybodyAskingForIt)
{
    // **There is no Save step in the normal flow**. A new session is
    // written when it is made, so the change this watches for is the block.
    ASSERT_EQ(blocksOnDisk(), 0);

    engine->applyEdit(id, addBlockEdit(u"b1"_s, u"effect.volume"_s));
    EXPECT_TRUE(engine->hasUnsavedChanges()) << "the edit did not arm the autosave";

    EXPECT_TRUE(waitFor([this] { return blocksOnDisk() == 1; })) << "the edit never reached the disk";
    EXPECT_FALSE(engine->hasUnsavedChanges());
}

TEST_F(Autosave, AnUndoAndARedoAreOnTheDiskLikeAnyEdit)
{
    // Undo and redo applied their step and said nothing to the clock,
    // so the file kept what had been undone until something else was
    // edited, and a restart brought it back.
    engine->applyEdit(id, addBlockEdit(u"b1"_s, u"effect.volume"_s));
    ASSERT_TRUE(waitFor([this] { return blocksOnDisk() == 1; })) << "the edit never reached the disk";

    engine->undo(id);
    EXPECT_TRUE(engine->hasUnsavedChanges()) << "the undo did not arm the autosave";
    EXPECT_TRUE(waitFor([this] { return blocksOnDisk() == 0; })) << "the undo never reached the disk";

    engine->redo(id);
    EXPECT_TRUE(engine->hasUnsavedChanges()) << "the redo did not arm the autosave";
    EXPECT_TRUE(waitFor([this] { return blocksOnDisk() == 1; })) << "the redo never reached the disk";
}

TEST_F(Autosave, AWholeDragOfAKnobIsOneWriteAndTheLastValueWins)
{
    engine->applyEdit(id, addBlockEdit(u"b1"_s, u"effect.volume"_s));
    engine->flushAutosave();
    ASSERT_EQ(blocksOnDisk(), 1);

    // What a slider does: a value every few milliseconds. The clock starts
    // again with each one, so none of them is written on its own.
    for (int percent = 10; percent <= 100; percent += 10) {
        engine->setParam(id, u"b1"_s, u"gain"_s, paramJson(percent / 100.0));
        EXPECT_TRUE(engine->hasUnsavedChanges());
        const std::expected<Session, SessionLoadFailure> loaded = onDisk();
        ASSERT_TRUE(loaded.has_value());
        EXPECT_FALSE(loaded->blocks.at(0).params.contains(u"gain"_s))
            << "the middle of a drag was written at " << percent << " %";
    }

    // And the value that is written is the one the drag ended on.
    EXPECT_TRUE(waitFor([this] {
        const std::expected<Session, SessionLoadFailure> loaded = onDisk();
        return loaded && loaded->blocks.at(0).params.contains(u"gain"_s);
    })) << "the drag never reached the disk";
    const std::expected<Session, SessionLoadFailure> loaded = onDisk();
    ASSERT_TRUE(loaded.has_value());
    EXPECT_DOUBLE_EQ(loaded->blocks.at(0).params.value(u"gain"_s).toDouble(), 1.0);
}

TEST_F(Autosave, ClosingWritesWhatTheClockStillOwed)
{
    engine->applyEdit(id, addBlockEdit(u"b1"_s, u"effect.volume"_s));
    ASSERT_TRUE(engine->hasUnsavedChanges());

    // **No waiting.** After this there is no session to write, so closing
    // has to be the thing that writes it.
    engine->closeSession(id);
    EXPECT_FALSE(engine->hasUnsavedChanges());
    EXPECT_EQ(blocksOnDisk(), 1);
}

TEST_F(Autosave, TheEngineGoingAwayWritesWhatTheClockStillOwed)
{
    engine->applyEdit(id, addBlockEdit(u"b1"_s, u"effect.volume"_s));
    engine->applyEdit(id, addBlockEdit(u"b2"_s, u"meter.peakrms"_s));
    ASSERT_TRUE(engine->hasUnsavedChanges());

    // What `kill -9` cannot be helped with, but an ordinary quit can.
    engine.reset();
    EXPECT_EQ(blocksOnDisk(), 2);
}

TEST_F(Autosave, AfterAKillNineTheLastCommittedStateComesBack)
{
    // **What autosave has to do, spelled out.** `kill -9` means
    // no destructor, no flush and no close — so what comes back is whatever
    // the autosave had already written, and the whole point is that it had.
    engine->applyEdit(id, addBlockEdit(u"b1"_s, u"effect.volume"_s));
    engine->applyEdit(id, addBlockEdit(u"b2"_s, u"meter.peakrms"_s));
    engine->setParam(id, u"b1"_s, u"gain"_s, paramJson(0.25));
    ASSERT_TRUE(waitFor([this] { return !engine->hasUnsavedChanges(); })) << "the autosave never ran";

    // **The disk is read while the first engine is still alive**, which is
    // the whole point: what `kill -9` leaves behind is whatever autosave had
    // already written, with no destructor and no flush to help. Asserting it
    // now proves autosave did the work — the check above that nothing is
    // waiting to be written says the same thing from the other side.
    //
    // **The engine is not leaked to mime the kill.** An earlier version of
    // this released it, and LeakSanitizer was right to call that a leak: a
    // deliberate one is still one, and the asan suite is not a place to
    // teach exceptions.
    const std::expected<Session, SessionLoadFailure> loaded = onDisk();

    // A second engine on the same directory is the next launch.
    auto next = std::make_unique<Engine>(registry, SessionStore{sessions.path()});
    QSignalSpy opened(next.get(), &EngineFacade::sessionOpened);
    next->openSession(id);
    EXPECT_EQ(opened.count(), 1) << "the session did not come back at all";
    ASSERT_TRUE(loaded.has_value());
    ASSERT_EQ(loaded->blocks.size(), 2) << "the blocks that were added did not come back";
    EXPECT_EQ(loaded->blocks.at(0).id, u"b1"_s);
    EXPECT_EQ(loaded->blocks.at(1).id, u"b2"_s);
    EXPECT_DOUBLE_EQ(loaded->blocks.at(0).params.value(u"gain"_s).toDouble(), 0.25)
        << "the last committed value did not come back";
}

TEST_F(Autosave, ASessionThatWasDeletedIsNotWrittenBackOut)
{
    engine->applyEdit(id, addBlockEdit(u"b1"_s, u"effect.volume"_s));
    ASSERT_TRUE(engine->hasUnsavedChanges());

    engine->deleteSession(id);
    EXPECT_FALSE(engine->hasUnsavedChanges()) << "a deleted session was still owed a write";
    EXPECT_FALSE(onDisk().has_value()) << "the file came back after being deleted";

    // And the clock running out afterwards does not bring it back either.
    engine->flushAutosave();
    EXPECT_FALSE(onDisk().has_value());
}

TEST_F(Autosave, ACorruptSessionFileIsMovedAsideAndReportedAndTheOthersStillOpen)
{
    // **What restoring sessions has to survive.** A file that will not read
    // is moved aside and reported, and the application starts normally —
    // which is the part that matters: one session nobody can open must not
    // be a launch nobody can get past. The store's own move-aside is
    // checked in test_sessionstore.cpp; this is the engine reporting it and
    // carrying on.
    engine->applyEdit(id, addBlockEdit(u"b1"_s, u"effect.volume"_s));
    engine->flushAutosave();
    const QString good = id;

    // A second session, and nonsense written over its file behind the
    // engine's back.
    QSignalSpy opened(engine.get(), &EngineFacade::sessionOpened);
    engine->createSession(u"Broken"_s);
    ASSERT_EQ(opened.count(), 1);
    const QString broken = opened.back().at(0).toString();
    engine->closeSession(broken);
    const QString brokenPath = store->pathFor(QUuid(broken));
    {
        QFile file(brokenPath);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        ASSERT_GT(file.write("this is not a session\n"), 0);
    }

    QSignalSpy errors(engine.get(), &EngineFacade::error);
    engine->openSession(broken);

    // **Reported, with both paths**, so a person can go and find the bytes.
    ASSERT_GE(errors.count(), 1) << "nothing was said about a file that would not read";
    EXPECT_EQ(errors.back().at(0).toString(), u"session.corruptFile"_s);
    const QJsonObject args = QJsonDocument::fromJson(errors.back().at(1).toString().toUtf8()).object();
    EXPECT_EQ(args.value(u"file"_s).toString(), brokenPath);
    EXPECT_FALSE(args.value(u"backup"_s).toString().isEmpty()) << "no backup location was named";

    // **Moved aside, not thrown away and not left to trip over again.**
    EXPECT_FALSE(QFile::exists(brokenPath)) << "the unreadable file was left where it was";
    EXPECT_TRUE(QFile::exists(args.value(u"backup"_s).toString())) << "the bytes were not kept";

    // And the engine carries on: the other session opens as if nothing had
    // happened, which is what "the app starts normally" means.
    QSignalSpy after(engine.get(), &EngineFacade::sessionOpened);
    engine->closeSession(good);
    engine->openSession(good);
    EXPECT_EQ(after.count(), 1) << "a corrupt file stopped a good session from opening";
    EXPECT_EQ(blocksOnDisk(), 1);
}

TEST_F(Autosave, AskingForASaveWritesWhetherOrNotAnythingChanged)
{
    engine->applyEdit(id, addBlockEdit(u"b1"_s, u"effect.volume"_s));
    engine->flushAutosave();
    ASSERT_EQ(blocksOnDisk(), 1);
    ASSERT_FALSE(engine->hasUnsavedChanges());

    // A caller saying "now" is answered, not told there was nothing to do.
    ASSERT_TRUE(QFile::remove(store->pathFor(QUuid(id))));
    ASSERT_EQ(blocksOnDisk(), -1);
    engine->saveSession(id);
    EXPECT_EQ(blocksOnDisk(), 1);
}

TEST_F(Autosave, OpeningASessionThatIsOpenAlreadyAnswersAsOpeningAnyOther)
{
    // Not autosave, but the fixture this needs: an engine with a session
    // open and no PipeWire. The daemon keeps a session open after the
    // window that opened it has gone, and the next window asks for it
    // again. It was sent a snapshot but never SessionOpened, which is what
    // gives it a tab, so its restore waited for ever.
    QSignalSpy opened(engine.get(), &EngineFacade::sessionOpened);
    QSignalSpy snapshots(engine.get(), &EngineFacade::sessionSnapshot);

    engine->openSession(id);

    ASSERT_EQ(opened.count(), 1);
    EXPECT_EQ(opened.at(0).at(0).toString(), id);
    EXPECT_EQ(snapshots.count(), 1);
}
