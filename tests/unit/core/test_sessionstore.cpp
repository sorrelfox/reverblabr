// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/editcommand.h"
#include "core/session.h"
#include "core/sessionstore.h"
#include "sessionfixtures.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QTemporaryDir>
#include <QUuid>
#include <gtest/gtest.h>

#include <algorithm>
#include <clocale>
#include <random>

using namespace rb::core;
using rb::test::fullSession;

namespace
{

/// A store in a directory of its own, thrown away with the test.
class TemporaryStore
{
public:
    TemporaryStore()
        : m_store(m_directory.path())
    {
    }

    [[nodiscard]] const SessionStore &store() const
    {
        return m_store;
    }

    [[nodiscard]] QString path() const
    {
        return m_directory.path();
    }

    [[nodiscard]] bool isValid() const
    {
        return m_directory.isValid();
    }

private:
    QTemporaryDir m_directory;
    SessionStore m_store;
};

/// Everything in the directory, so a test can see what a load left behind.
QStringList entries(const QString &directory)
{
    return QDir(directory).entryList(QDir::Files, QDir::Name);
}

} // namespace

TEST(SessionJson, RoundTripsEveryField)
{
    const Session written = fullSession();
    const std::expected<Session, SessionError> read = sessionFromJson(sessionToJson(written));
    ASSERT_TRUE(read.has_value()) << sessionErrorName(read.error());

    EXPECT_EQ(read->id, written.id);
    EXPECT_EQ(read->name, written.name);
    EXPECT_EQ(read->notes, written.notes);
    EXPECT_EQ(read->settings, written.settings);
    EXPECT_EQ(read->blocks, written.blocks);
    // Bypass is part of the session, so a block that was switched off comes
    // back switched off. The fixture has one.
    const auto offBefore =
        std::ranges::count_if(written.blocks, [](const BlockInstance &block) { return !block.enabled; });
    const auto offAfter =
        std::ranges::count_if(read->blocks, [](const BlockInstance &block) { return !block.enabled; });
    EXPECT_EQ(offAfter, offBefore);
    EXPECT_GT(offBefore, 0) << "the fixture should have a block that is switched off";
    EXPECT_EQ(read->wires, written.wires);
    EXPECT_EQ(read->timers, written.timers) << "a field this version does not use has to survive";
    EXPECT_EQ(read->automations, written.automations);
    EXPECT_EQ(*read, written) << "and nothing else differs either";
}

TEST(SessionJson, WritesTheSchemaVersion)
{
    const QJsonObject json = sessionToJson(fullSession());
    EXPECT_EQ(json.value(QStringLiteral("schemaVersion")).toInt(), kSessionSchemaVersion);
}

TEST(SessionJson, RefusesAFileFromALaterVersion)
{
    // Opening it would mean saving back less than it came with.
    QJsonObject json = sessionToJson(fullSession());
    json.insert(QStringLiteral("schemaVersion"), kSessionSchemaVersion + 1);
    EXPECT_EQ(sessionFromJson(json).error(), SessionError::SchemaTooNew);
}

TEST(SessionJson, RefusesSomethingThatIsNotASession)
{
    EXPECT_EQ(sessionFromJson(QJsonObject{}).error(), SessionError::MissingId);
    EXPECT_EQ(sessionFromJson(QJsonObject{{QStringLiteral("id"), QStringLiteral("not-a-uuid")}}).error(),
              SessionError::MissingId);
}

TEST(SessionJson, RefusesMalformedBlocksAndWires)
{
    const QJsonObject good = sessionToJson(fullSession());

    QJsonObject noId = good;
    QJsonArray blocks = noId.value(QStringLiteral("blocks")).toArray();
    QJsonObject block = blocks.at(0).toObject();
    block.remove(QStringLiteral("id"));
    blocks.replace(0, block);
    noId.insert(QStringLiteral("blocks"), blocks);
    EXPECT_EQ(sessionFromJson(noId).error(), SessionError::MalformedBlock);

    QJsonObject notAnObject = good;
    notAnObject.insert(QStringLiteral("blocks"), QJsonArray{QStringLiteral("b1")});
    EXPECT_EQ(sessionFromJson(notAnObject).error(), SessionError::MalformedBlock);

    QJsonObject duplicate = good;
    QJsonArray twice = duplicate.value(QStringLiteral("blocks")).toArray();
    twice.append(twice.at(0));
    duplicate.insert(QStringLiteral("blocks"), twice);
    EXPECT_EQ(sessionFromJson(duplicate).error(), SessionError::DuplicateBlockId);

    QJsonObject shortWire = good;
    shortWire.insert(QStringLiteral("wires"),
                     QJsonArray{QJsonObject{{QStringLiteral("from"), QJsonArray{QStringLiteral("b1")}},
                                            {QStringLiteral("to"), QJsonArray{QStringLiteral("b2"), 0}}}});
    EXPECT_EQ(sessionFromJson(shortWire).error(), SessionError::MalformedWire);

    QJsonObject danglingWire = good;
    danglingWire.insert(QStringLiteral("wires"),
                        QJsonArray{QJsonObject{{QStringLiteral("from"), QJsonArray{QStringLiteral("b1"), 0}},
                                               {QStringLiteral("to"), QJsonArray{QStringLiteral("b9"), 0}}}});
    EXPECT_EQ(sessionFromJson(danglingWire).error(), SessionError::WireBlockNotFound);
}

TEST(SessionJson, ASettingInTheWrongShapeFallsBackRatherThanStoppingTheLoad)
{
    QJsonObject json = sessionToJson(fullSession());
    json.insert(QStringLiteral("settings"), QJsonObject{{QStringLiteral("autoConnect"), QStringLiteral("yes")}});
    const std::expected<Session, SessionError> read = sessionFromJson(json);
    ASSERT_TRUE(read.has_value());
    EXPECT_TRUE(read->settings.autoConnect) << "the default, not a failure to open the session";
}

// **Auto Run is off unless the file says "login"**: a value from a
// later version, or a file from before the setting, must not start a session
// at login that this version was never asked to start.
TEST(SessionJson, AutoRunIsOffUnlessTheFileSaysLogin)
{
    QJsonObject json = sessionToJson(fullSession());
    QJsonObject settings = json.value(QStringLiteral("settings")).toObject();
    ASSERT_EQ(settings.value(QStringLiteral("autoRun")).toString(), QStringLiteral("login"));

    settings.insert(QStringLiteral("autoRun"), QStringLiteral("always"));
    json.insert(QStringLiteral("settings"), settings);
    std::expected<Session, SessionError> read = sessionFromJson(json);
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(read->settings.autoRun, QStringLiteral("off"));

    settings.remove(QStringLiteral("autoRun"));
    json.insert(QStringLiteral("settings"), settings);
    read = sessionFromJson(json);
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(read->settings.autoRun, QStringLiteral("off"));
}

TEST(SessionJson, AnUnknownBlockTypeIsKeptAndFlagged)
{
    // A session written by a later version names a block this one does not
    // have. Dropping it would lose the user's work the next time they saved.
    Session written = fullSession();
    BlockInstance fromTheFuture;
    fromTheFuture.id = QStringLiteral("b4");
    fromTheFuture.type = QStringLiteral("effect.timeMachine");
    fromTheFuture.params = QJsonObject{{QStringLiteral("years"), 10.0}};
    written.blocks.append(fromTheFuture);

    const auto isKnownType = [](QStringView type) { return type != QStringLiteral("effect.timeMachine"); };
    const std::expected<Session, SessionError> read = sessionFromJson(sessionToJson(written), isKnownType);
    ASSERT_TRUE(read.has_value());
    ASSERT_EQ(read->blocks.size(), 4);
    EXPECT_TRUE(read->blocks[3].unknownType);
    EXPECT_FALSE(read->blocks[0].unknownType);
    EXPECT_EQ(read->blocks[3].params, fromTheFuture.params) << "its parameters came through untouched";

    // And saving it again writes the same block back.
    Session saved = *read;
    saved.blocks[3].unknownType = false; // the flag is not part of the file
    EXPECT_EQ(sessionToJson(saved), sessionToJson(written));
}

TEST(SessionStore, SavesAndLoadsASession)
{
    TemporaryStore temporary;
    ASSERT_TRUE(temporary.isValid());
    const Session written = fullSession();

    ASSERT_TRUE(temporary.store().save(written).has_value());
    EXPECT_TRUE(QFile::exists(temporary.store().pathFor(written.id)));

    const std::expected<Session, SessionLoadFailure> read = temporary.store().load(written.id);
    ASSERT_TRUE(read.has_value()) << sessionErrorName(read.error().error);
    EXPECT_EQ(*read, written);
}

TEST(SessionStore, NotesEditedIntoASessionAreThereWhenItIsOpenedAgain)
{
    // What session notes need from the store, without a window: the edit,
    // the save, the close, and what the file gives back.
    TemporaryStore temporary;
    ASSERT_TRUE(temporary.isValid());
    Session session = fullSession();
    const QString written = QStringLiteral("Two microphones.\n\nMixed down on the laptop — 96 kHz.");

    ASSERT_TRUE(applyEdit(session, SetNotes{written}).has_value());
    ASSERT_TRUE(temporary.store().save(session).has_value());

    const std::expected<Session, SessionLoadFailure> reopened = temporary.store().load(session.id);
    ASSERT_TRUE(reopened.has_value()) << sessionErrorName(reopened.error().error);
    EXPECT_EQ(reopened->notes, written) << "the blank line or the newlines did not survive the file";

    // And an emptied box comes back empty rather than as what was there
    // before it was cleared.
    Session again = *reopened;
    ASSERT_TRUE(applyEdit(again, SetNotes{QString{}}).has_value());
    ASSERT_TRUE(temporary.store().save(again).has_value());
    const std::expected<Session, SessionLoadFailure> cleared = temporary.store().load(again.id);
    ASSERT_TRUE(cleared.has_value()) << sessionErrorName(cleared.error().error);
    EXPECT_TRUE(cleared->notes.isEmpty());
}

TEST(SessionStore, LeavesNoTemporaryFileBehind)
{
    TemporaryStore temporary;
    ASSERT_TRUE(temporary.isValid());
    const Session written = fullSession();
    ASSERT_TRUE(temporary.store().save(written).has_value());
    ASSERT_TRUE(temporary.store().save(written).has_value()) << "saving over an existing file works too";

    const QStringList files = entries(temporary.path());
    EXPECT_EQ(files.size(), 1) << "the temporary file must not survive the save";
    EXPECT_TRUE(files.at(0).endsWith(QStringLiteral(".rbsession")));
}

TEST(SessionStore, ListsWhatIsThereInIdOrder)
{
    TemporaryStore temporary;
    ASSERT_TRUE(temporary.isValid());

    QList<QUuid> written;
    for (int index = 0; index < 3; ++index) {
        Session session = fullSession();
        session.id = QUuid::createUuid();
        written.append(session.id);
        ASSERT_TRUE(temporary.store().save(session).has_value());
    }
    std::ranges::sort(written);

    EXPECT_EQ(temporary.store().list(), written);
}

TEST(SessionStore, IgnoresFilesThatAreNotSessions)
{
    TemporaryStore temporary;
    ASSERT_TRUE(temporary.isValid());
    ASSERT_TRUE(temporary.store().save(fullSession()).has_value());

    QFile stray(temporary.path() + QStringLiteral("/notes.txt"));
    ASSERT_TRUE(stray.open(QIODevice::WriteOnly));
    stray.write("hello");
    stray.close();

    EXPECT_EQ(temporary.store().list().size(), 1);
}

TEST(SessionStore, RemovesASession)
{
    TemporaryStore temporary;
    ASSERT_TRUE(temporary.isValid());
    const Session session = fullSession();
    ASSERT_TRUE(temporary.store().save(session).has_value());

    EXPECT_TRUE(temporary.store().remove(session.id).has_value());
    EXPECT_TRUE(temporary.store().list().isEmpty());
    EXPECT_EQ(temporary.store().remove(session.id).error(), SessionError::FileNotFound);
}

TEST(SessionStore, AMissingFileIsReportedAndLeftAlone)
{
    TemporaryStore temporary;
    ASSERT_TRUE(temporary.isValid());

    const std::expected<Session, SessionLoadFailure> read = temporary.store().load(QUuid::createUuid());
    ASSERT_FALSE(read.has_value());
    EXPECT_EQ(read.error().error, SessionError::FileNotFound);
    EXPECT_TRUE(read.error().backupPath.isEmpty()) << "nothing was moved: there was nothing there";
}

TEST(SessionStore, ACorruptFileIsMovedAsideAndReported)
{
    TemporaryStore temporary;
    ASSERT_TRUE(temporary.isValid());
    const Session session = fullSession();
    ASSERT_TRUE(temporary.store().save(session).has_value());

    const QString path = temporary.store().pathFor(session.id);
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("{ this is not json");
    file.close();

    const std::expected<Session, SessionLoadFailure> read = temporary.store().load(session.id);
    ASSERT_FALSE(read.has_value());
    EXPECT_EQ(read.error().error, SessionError::NotJson);
    ASSERT_FALSE(read.error().backupPath.isEmpty());
    EXPECT_TRUE(QFile::exists(read.error().backupPath));
    EXPECT_FALSE(QFile::exists(path)) << "the unreadable file is out of the way, so start-up carries on";
    EXPECT_TRUE(temporary.store().list().isEmpty());
}

TEST(SessionStore, AFileFromALaterVersionIsLeftWhereItIs)
{
    // It is not damaged, and a newer build could still open it. Moving it
    // aside would take it away from that build.
    TemporaryStore temporary;
    ASSERT_TRUE(temporary.isValid());
    const Session session = fullSession();

    QJsonObject json = sessionToJson(session);
    json.insert(QStringLiteral("schemaVersion"), kSessionSchemaVersion + 1);
    const QString path = temporary.store().pathFor(session.id);
    ASSERT_TRUE(QDir().mkpath(temporary.path()));
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(json).toJson());
    file.close();

    const std::expected<Session, SessionLoadFailure> read = temporary.store().load(session.id);
    ASSERT_FALSE(read.has_value());
    EXPECT_EQ(read.error().error, SessionError::SchemaTooNew);
    EXPECT_TRUE(read.error().backupPath.isEmpty());
    EXPECT_TRUE(QFile::exists(path));
}

TEST(SessionStore, SurvivesEveryMutationOfAValidFile)
{
    // The fuzz check: take a good session file and change bytes in it at
    // random. Every attempt must either load or come back with an error code.
    // Nothing may crash, hang, or be reported as a success that is not one.
    TemporaryStore temporary;
    ASSERT_TRUE(temporary.isValid());
    const Session session = fullSession();
    const QByteArray good = QJsonDocument(sessionToJson(session)).toJson();

    std::mt19937 random(20260920); // fixed, so a failure can be reproduced
    std::uniform_int_distribution<int> position(0, static_cast<int>(good.size()) - 1);
    std::uniform_int_distribution<int> byte(0, 255);
    std::uniform_int_distribution<int> mutations(1, 8);

    const QString path = temporary.path() + QStringLiteral("/fuzz.rbsession");
    int loaded = 0;
    int refused = 0;
    for (int attempt = 0; attempt < 2000; ++attempt) {
        QByteArray mutated = good;
        const int count = mutations(random);
        for (int change = 0; change < count; ++change) {
            mutated[position(random)] = static_cast<char>(byte(random));
        }

        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(mutated);
        file.close();

        const std::expected<Session, SessionLoadFailure> read = temporary.store().loadFile(path);
        if (read.has_value()) {
            // Whatever loaded has to be a session that holds together.
            EXPECT_FALSE(read->id.isNull());
            for (const Wire &wire : read->wires) {
                EXPECT_TRUE(read->hasBlock(wire.fromBlock)) << "at attempt " << attempt;
                EXPECT_TRUE(read->hasBlock(wire.toBlock)) << "at attempt " << attempt;
            }
            ++loaded;
        } else {
            EXPECT_NE(read.error().error, SessionError::FileNotFound) << "at attempt " << attempt;
            ++refused;
        }
        QFile::remove(path);
        QDir(temporary.path()).removeRecursively();
        ASSERT_TRUE(QDir().mkpath(temporary.path()));
    }
    EXPECT_GT(refused, 0) << "the mutations were too gentle to prove anything";
    EXPECT_GT(loaded, 0) << "and too harsh: some had to still be readable";
}

TEST(SessionStore, WritesDecimalPointsWhateverTheLocaleIs)
{
    // A session written on a machine set to a locale with decimal commas must
    // still be JSON, and must still open anywhere else.
    if (std::setlocale(LC_NUMERIC, "de_DE.UTF-8") == nullptr) {
        GTEST_SKIP() << "the de_DE.UTF-8 locale is not installed here";
    }

    TemporaryStore temporary;
    ASSERT_TRUE(temporary.isValid());
    const Session session = fullSession(); // its positions are not whole numbers
    ASSERT_TRUE(temporary.store().save(session).has_value());

    QFile file(temporary.store().pathFor(session.id));
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QByteArray written = file.readAll();
    file.close();
    std::setlocale(LC_NUMERIC, "C");

    EXPECT_TRUE(written.contains("40.5")) << "a decimal point, not a comma";
    EXPECT_FALSE(written.contains("40,5"));

    const std::expected<Session, SessionLoadFailure> read = temporary.store().load(session.id);
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(read->blocks[0].position, session.blocks[0].position);
}

TEST(SessionStore, TheDefaultDirectoryIsUnderTheApplicationsData)
{
    const QString directory = SessionStore::defaultDirectory();
    EXPECT_TRUE(directory.endsWith(QStringLiteral("/sessions"))) << directory.toStdString();
    EXPECT_FALSE(directory.startsWith(QStringLiteral("/sessions"))) << "it has to be under a data directory";
}
