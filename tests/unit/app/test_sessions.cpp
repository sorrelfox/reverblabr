// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Which sessions the window is holding: the tabs, which one is in
// front, and the launch that puts back what was open. The window that draws
// them is checked in tests/qml.
//
// **The state file is the test's own**, so running this does not reach into
// whoever is running it, and so that "the next launch" is a second Sessions
// object reading what the first one wrote.

#include "core/mockengine.h"
#include "core/sessionstore.h"
#include "errormessages.h"
#include "errorsurface.h"
#include "sessions.h"

#include <KConfigGroup>
#include <KSharedConfig>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <gtest/gtest.h>

#include <memory>
#include <utility>

using namespace rb;
using namespace Qt::StringLiterals;

namespace
{

/// The ids of \a rows, which is what a tab bar draws in order.
[[nodiscard]] QStringList idsOf(const QVariantList &rows)
{
    QStringList ids;
    for (const QVariant &entry : rows) {
        ids.append(entry.toMap().value(u"id"_s).toString());
    }
    return ids;
}

[[nodiscard]] QStringList namesOf(const QVariantList &rows)
{
    QStringList names;
    for (const QVariant &entry : rows) {
        names.append(entry.toMap().value(u"name"_s).toString());
    }
    return names;
}

/// A state file of this test's own, and the Sessions objects reading it.
///
/// **Two of them, one after the other**, is what a relaunch is: the first
/// writes down what was open and the second is asked to put it back.
class SessionsFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(state.isValid());
        config = KSharedConfig::openConfig(state.filePath(u"reverblabrstaterc"_s), KConfig::SimpleConfig);
        ui::Sessions::setConfig(config);
    }

    void TearDown() override
    {
        sessions.reset();
        ui::Sessions::setConfig({});
    }

    /// A Sessions attached to \a engine, as a launch makes one.
    [[nodiscard]] ui::Sessions *relaunch()
    {
        sessions = std::make_unique<ui::Sessions>();
        sessions->attach(&engine);
        return sessions.get();
    }

    QTemporaryDir state;
    KSharedConfig::Ptr config;
    core::MockEngine engine;
    std::unique_ptr<ui::Sessions> sessions;
};

} // namespace

TEST_F(SessionsFixture, TheFirstLaunchHasNothingToPutBackSoItMakesOne)
{
    ui::Sessions *held = relaunch();
    ASSERT_TRUE(held->open().isEmpty()) << "nothing is open before the restore";

    held->restore(u"Untitled"_s);

    // **The window is never empty.** With nothing remembered there is one
    // session, it is in front, and it is called what the interface said.
    ASSERT_EQ(held->open().size(), 1);
    EXPECT_EQ(namesOf(held->open()), QStringList{u"Untitled"_s});
    EXPECT_EQ(held->currentId(), idsOf(held->open()).constFirst());
    EXPECT_TRUE(held->isOpen(held->currentId()));
}

TEST_F(SessionsFixture, WhatWasOpenComesBackInTheOrderItSatIn)
{
    ui::Sessions *first = relaunch();
    first->restore(u"Untitled"_s);
    first->create(u"Second"_s);
    first->create(u"Third"_s);
    const QStringList before = idsOf(first->open());
    ASSERT_EQ(before.size(), 3);
    // The tab that was in front is not the last one made.
    first->setCurrentId(before.at(1));
    const QString front = first->currentId();

    // What closing the application does, and then opening it again.
    sessions.reset();
    ui::Sessions *again = relaunch();
    again->restore(u"Untitled"_s);

    EXPECT_EQ(idsOf(again->open()), before) << "the tabs came back in a different order";
    EXPECT_EQ(again->currentId(), front) << "a different tab was in front than the one that was";
    EXPECT_EQ(namesOf(again->open()), (QStringList{u"Untitled"_s, u"Second"_s, u"Third"_s}));
}

TEST_F(SessionsFixture, ASessionIsOpenedStoppedAndNotStarted)
{
    // **What restoring a session must not do.** A session that began making
    // sound because the application was opened would be a surprise nobody
    // asked for, and on a machine with no audio system it would be an error
    // banner at every launch.
    ui::Sessions *first = relaunch();
    first->restore(u"Untitled"_s);
    const QString id = first->currentId();
    QSignalSpy states(&engine, &core::EngineFacade::stateChanged);

    sessions.reset();
    relaunch()->restore(u"Untitled"_s);

    for (const QList<QVariant> &said : states) {
        EXPECT_NE(said.at(1).toString(), u"running"_s) << "a restored session started itself";
        EXPECT_NE(said.at(1).toString(), u"starting"_s) << "a restored session started itself";
    }
    EXPECT_TRUE(sessions->isOpen(id));
}

TEST_F(SessionsFixture, ASessionWhoseFileHasGoneIsDroppedAndTheRestStillOpen)
{
    // **A missing session is not an error to stop on.** It was deleted,
    // which is a thing that happens, and the criterion is that the window
    // still opens.
    ui::Sessions *first = relaunch();
    first->restore(u"Untitled"_s);
    first->create(u"Kept"_s);
    const QStringList before = idsOf(first->open());
    ASSERT_EQ(before.size(), 2);

    // Deleted behind the application's back: the state file still names it.
    KConfigGroup group(config, u"Sessions"_s);
    group.writeEntry(u"Open"_s, QStringList{u"00000000-0000-0000-0000-000000000000"_s, before.at(1)});
    group.writeEntry(u"Current"_s, u"00000000-0000-0000-0000-000000000000"_s);
    group.sync();

    sessions.reset();
    ui::Sessions *again = relaunch();
    again->restore(u"Untitled"_s);

    EXPECT_EQ(idsOf(again->open()), QStringList{before.at(1)}) << "the missing one was not dropped";
    EXPECT_EQ(again->currentId(), before.at(1)) << "nothing came to the front when the front tab had gone";
}

TEST_F(SessionsFixture, WhatIsOpenIsRememberedByNameAsWellAsById)
{
    ui::Sessions *first = relaunch();
    first->restore(u"Untitled"_s);
    first->create(u"Podcast"_s);
    const QString podcast = first->currentId();
    first->remember();

    EXPECT_EQ(ui::Sessions::rememberedName(podcast), u"Podcast"_s);

    // **Only what is open.** A closed session's name is not kept for it.
    first->closeSession(podcast);
    first->remember();
    EXPECT_TRUE(ui::Sessions::rememberedName(podcast).isEmpty()) << "a closed session's name was kept";
}

TEST_F(SessionsFixture, ASessionWhoseFileHasGoneIsReportedByNameOnceAndThenForgotten)
{
    // The banner read "There's no session called 826ced7e-…" — an id
    // offered as a name — and whether it was there at all depended on which
    // of two answers came back first.
    ui::Sessions *first = relaunch();
    first->restore(u"Untitled"_s);
    const QString kept = first->currentId();
    first->remember();

    // A session called "Podcast" was open, and its file has gone since.
    const QString gone = u"00000000-0000-0000-0000-000000000002"_s;
    KConfigGroup group(config, u"Sessions"_s);
    group.writeEntry(u"Open"_s, QStringList{gone, kept});
    group.writeEntry(u"Current"_s, gone);
    group.group(u"Names"_s).writeEntry(gone, u"Podcast"_s);
    group.sync();

    sessions.reset();
    ui::ErrorSurface surface;
    surface.attach(&engine);
    ui::Sessions *again = relaunch();
    again->restore(u"Untitled"_s);
    // The window follows the tab in front, as Main.qml binds it.
    surface.setSessionId(again->currentId());

    ASSERT_EQ(idsOf(again->open()), QStringList{kept});
    EXPECT_TRUE(surface.isVisible()) << "a session that could not be put back was not reported";
    EXPECT_EQ(surface.code(), u"session.notFound"_s);
    EXPECT_EQ(surface.message(), app::errorMessage(u"session.notFound"_s, {{u"session"_s, u"Podcast"_s}}));
    EXPECT_TRUE(surface.message().contains(u"Podcast"_s)) << surface.message().toStdString();
    EXPECT_FALSE(surface.message().contains(gone)) << "the id was shown where the name belongs";

    // **Once.** The restore wrote down what did open, so the next start
    // does not ask for it again.
    sessions.reset();
    ui::ErrorSurface later;
    later.attach(&engine);
    ui::Sessions *third = relaunch();
    third->restore(u"Untitled"_s);
    later.setSessionId(third->currentId());
    EXPECT_FALSE(later.isVisible()) << "the missing session was reported again on the next start";
}

TEST_F(SessionsFixture, WhenEveryRememberedSessionHasGoneTheWindowStillGetsOne)
{
    KConfigGroup group(config, u"Sessions"_s);
    group.writeEntry(u"Open"_s, QStringList{u"00000000-0000-0000-0000-000000000001"_s});
    group.sync();

    ui::Sessions *held = relaunch();
    held->restore(u"Untitled"_s);

    ASSERT_EQ(held->open().size(), 1) << "the window was left with no session at all";
    EXPECT_EQ(namesOf(held->open()), QStringList{u"Untitled"_s});
    EXPECT_FALSE(held->currentId().isEmpty());
}

TEST_F(SessionsFixture, ClosingATabLeavesTheOneBesideItInFront)
{
    ui::Sessions *held = relaunch();
    held->restore(u"Untitled"_s);
    held->create(u"Second"_s);
    held->create(u"Third"_s);
    const QStringList ids = idsOf(held->open());
    ASSERT_EQ(ids.size(), 3);

    held->setCurrentId(ids.at(1));
    held->closeSession(ids.at(1));
    EXPECT_EQ(idsOf(held->open()), (QStringList{ids.at(0), ids.at(2)}));
    EXPECT_EQ(held->currentId(), ids.at(2)) << "the eye was already beside it, not back at the start";

    // The last one is closed by the tab going away, and the one before it
    // comes forward because there is nothing after it.
    held->setCurrentId(ids.at(2));
    held->closeSession(ids.at(2));
    EXPECT_EQ(held->currentId(), ids.at(0));

    // **And the last tab of all is not closed.** A window with no session
    // has nothing to show and no way back to one.
    held->closeSession(ids.at(0));
    EXPECT_EQ(held->open().size(), 1);
    EXPECT_EQ(held->currentId(), ids.at(0));
}

TEST_F(SessionsFixture, ClosingASessionLeavesItOnDiskAndDeletingDoesNot)
{
    ui::Sessions *held = relaunch();
    held->restore(u"Untitled"_s);
    held->create(u"Second"_s);
    const QStringList ids = idsOf(held->open());
    ASSERT_EQ(ids.size(), 2);
    ASSERT_EQ(idsOf(held->saved()).size(), 2);

    held->closeSession(ids.at(1));
    EXPECT_EQ(held->open().size(), 1);
    EXPECT_EQ(idsOf(held->saved()).size(), 2) << "closing a session threw it away";

    held->deleteSession(ids.at(1));
    EXPECT_EQ(idsOf(held->saved()), QStringList{ids.at(0)}) << "a deleted session is still offered";
}

TEST_F(SessionsFixture, DeletingTheSessionInFrontTakesTheTabWithIt)
{
    ui::Sessions *held = relaunch();
    held->restore(u"Untitled"_s);
    held->create(u"Second"_s);
    const QStringList ids = idsOf(held->open());
    held->setCurrentId(ids.at(1));

    held->deleteSession(ids.at(1));
    EXPECT_EQ(idsOf(held->open()), QStringList{ids.at(0)});
    EXPECT_EQ(held->currentId(), ids.at(0));

    // And it is not asked for again on the next launch.
    sessions.reset();
    ui::Sessions *again = relaunch();
    again->restore(u"Untitled"_s);
    EXPECT_EQ(idsOf(again->open()), QStringList{ids.at(0)});
}

TEST_F(SessionsFixture, RenamingShowsOnTheTabAndInTheList)
{
    ui::Sessions *held = relaunch();
    held->restore(u"Untitled"_s);
    const QString id = held->currentId();
    QSignalSpy changed(held, &ui::Sessions::openChanged);

    held->renameSession(id, u"Podcast"_s);

    EXPECT_EQ(namesOf(held->open()), QStringList{u"Podcast"_s});
    EXPECT_EQ(held->nameOf(id), u"Podcast"_s);
    EXPECT_GE(changed.count(), 1) << "the tab was not told its name had changed";

    // **A rename with no new name is not a rename.** A session nobody can
    // name is a session nobody finds again.
    held->renameSession(id, QString{});
    EXPECT_EQ(held->nameOf(id), u"Podcast"_s);
}

TEST_F(SessionsFixture, OpeningOneThatIsAlreadyOpenBringsItToTheFront)
{
    ui::Sessions *held = relaunch();
    held->restore(u"Untitled"_s);
    held->create(u"Second"_s);
    const QStringList ids = idsOf(held->open());
    ASSERT_EQ(held->currentId(), ids.at(1));

    held->openSession(ids.at(0));
    EXPECT_EQ(held->currentId(), ids.at(0));
    EXPECT_EQ(held->open().size(), 2) << "the same session was opened twice";
}

TEST_F(SessionsFixture, ASessionAskedForDuringTheRestoreEndsUpInFront)
{
    ui::Sessions *first = relaunch();
    first->restore(u"Untitled"_s);
    first->create(u"Second"_s);
    const QStringList remembered = idsOf(first->open());
    sessions.reset();
    // A session the window did not have open, as KRunner's Open asks for
    // when it starts the window.
    QSignalSpy made(&engine, &core::EngineFacade::sessionOpened);
    engine.createSession(u"From KRunner"_s);
    ASSERT_EQ(made.count(), 1);
    const QString asked = made.first().first().toString();

    // **Asked for while the tabs are still coming back**: the request is
    // delivered once the window is up, which may be before every
    // remembered session has arrived.
    ui::Sessions *again = relaunch();
    bool askedOnce = false;
    QObject::connect(again, &ui::Sessions::opened, again, [&](const QString &) {
        if (!std::exchange(askedOnce, true)) {
            again->openSession(asked);
        }
    });
    again->restore(u"Untitled"_s);

    EXPECT_EQ(idsOf(again->open()), remembered + QStringList{asked}) << "the remembered tabs lost their places";
    EXPECT_EQ(again->currentId(), asked) << "the restore put its own front tab over the one asked for";
}

TEST_F(SessionsFixture, ASessionAskedForDuringTheRestoreThatNeverOpensLeavesTheFrontAlone)
{
    ui::Sessions *first = relaunch();
    first->restore(u"Untitled"_s);
    first->create(u"Second"_s);
    first->create(u"Third"_s);
    const QStringList remembered = idsOf(first->open());
    first->setCurrentId(remembered.at(1));
    sessions.reset();

    ui::Sessions *again = relaunch();
    bool askedOnce = false;
    QObject::connect(again, &ui::Sessions::opened, again, [&](const QString &) {
        if (!std::exchange(askedOnce, true)) {
            again->openSession(u"00000000-0000-4000-8000-000000000000"_s); // no such session
        }
    });
    again->restore(u"Untitled"_s);

    EXPECT_EQ(idsOf(again->open()), remembered);
    EXPECT_EQ(again->currentId(), remembered.at(1))
        << "a session that never opened took the front from the one that had it";
}

TEST_F(SessionsFixture, TabsCanBeReorderedAndTheOrderIsRemembered)
{
    ui::Sessions *held = relaunch();
    held->restore(u"Untitled"_s);
    held->create(u"Second"_s);
    held->create(u"Third"_s);
    const QStringList ids = idsOf(held->open());

    held->moveTab(0, 2);
    EXPECT_EQ(idsOf(held->open()), (QStringList{ids.at(1), ids.at(2), ids.at(0)}));

    // A move to nowhere is not a move.
    held->moveTab(1, 1);
    held->moveTab(-1, 0);
    held->moveTab(0, 99);
    EXPECT_EQ(idsOf(held->open()), (QStringList{ids.at(1), ids.at(2), ids.at(0)}));

    sessions.reset();
    ui::Sessions *again = relaunch();
    again->restore(u"Untitled"_s);
    EXPECT_EQ(idsOf(again->open()), (QStringList{ids.at(1), ids.at(2), ids.at(0)}))
        << "the order somebody dragged the tabs into was not remembered";
}

TEST_F(SessionsFixture, ASessionExportsToAFileAndComesBackFromOneAsACopy)
{
    ui::Sessions *held = relaunch();
    held->restore(u"Untitled"_s);
    held->renameSession(held->currentId(), u"Podcast"_s);
    const QString original = held->currentId();
    const QUrl file = QUrl::fromLocalFile(state.filePath(u"sent-to-somebody.rbsession"_s));

    held->exportCurrent(file);
    EXPECT_TRUE(QFile::exists(file.toLocalFile())) << "nothing was written";

    held->importFrom(file);

    // **An import is a copy**, and that is the whole point: importing a
    // session somebody sent must not overwrite one of your own whose id
    // happens to match. So it arrives beside the original, not over it.
    ASSERT_EQ(held->open().size(), 2);
    const QString imported = held->currentId();
    EXPECT_NE(imported, original) << "the import took the original's id and overwrote it";
    EXPECT_EQ(held->nameOf(imported), u"Podcast"_s) << "what was in the file did not come back";
    EXPECT_TRUE(held->isOpen(original)) << "the original was closed by importing a copy of it";

    // And the same file again is a third session, not the second one over.
    held->importFrom(file);
    EXPECT_EQ(held->open().size(), 3);
    EXPECT_NE(held->currentId(), imported);

    // The file that was imported is left exactly as it was.
    EXPECT_TRUE(QFile::exists(file.toLocalFile()));
}

TEST_F(SessionsFixture, AnExportOrImportOfSomethingThatIsNotAFileIsRefusedQuietly)
{
    ui::Sessions *held = relaunch();
    held->restore(u"Untitled"_s);
    const int before = static_cast<int>(held->open().size());

    // A url that names no file at all, which is what a cancelled dialog and
    // a remote location both look like from here.
    held->exportCurrent(QUrl{});
    held->exportCurrent(QUrl(u"https://example.org/session.rbsession"_s));
    held->importFrom(QUrl{});
    held->importFrom(QUrl(u"https://example.org/session.rbsession"_s));
    EXPECT_EQ(held->open().size(), before) << "a url that is not a file opened something";

    // A file that is not there is reported by the engine and opens nothing.
    held->importFrom(QUrl::fromLocalFile(state.filePath(u"no-such-session.rbsession"_s)));
    EXPECT_EQ(held->open().size(), before);
}

TEST_F(SessionsFixture, TheSuffixIsTheOneTheEngineWrites)
{
    // The interface's file dialogs and the engine must not disagree about
    // what a session file is called, so there is one answer.
    EXPECT_EQ(ui::Sessions::fileSuffix(), QString(core::kSessionFileSuffix));
    EXPECT_EQ(ui::Sessions::fileSuffix(), u".rbsession"_s);
}

TEST_F(SessionsFixture, TheSavedListIsNotAnsweredBeforeTheEngineHasAnswered)
{
    // An empty list has to mean "none" and not "not asked yet", or a window
    // would offer New Session as the only thing a person could do while the
    // engine was still reading the directory.
    ui::Sessions loose;
    EXPECT_FALSE(loose.isReady());
    EXPECT_TRUE(loose.saved().isEmpty());

    ui::Sessions *held = relaunch();
    EXPECT_TRUE(held->isReady()) << "the mock answers at once, so this is ready as soon as it is attached";
}
