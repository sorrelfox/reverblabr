// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// **The media players on a bus**, as the watcher and a stream's feed see
// them, against players of the test's own (fakemprisplayer.cpp), each a
// process of its own, on a private bus in a PID namespace of its own
// where there is one (tests/bus/CMakeLists.txt). On a desktop's bus it
// would read the person's own players, which the test and the fake player
// both refuse to do.

#include "mpris/playerwatcher.h"
#include "mpris/trackfeed.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <gtest/gtest.h>

#include <chrono>
#include <list>
#include <memory>

using namespace rb::mpris;
using namespace Qt::StringLiterals;
using namespace std::chrono_literals;

namespace
{

template <typename Predicate>
bool waitFor(Predicate predicate, int milliseconds = 10000)
{
    QElapsedTimer clock;
    clock.start();
    while (!predicate() && clock.elapsed() < milliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return predicate();
}

/// Turns the event loop for \a milliseconds whatever happens.
void pump(int milliseconds)
{
    (void)waitFor([] { return false; }, milliseconds);
}

class MprisTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // **Refuse to run anywhere but a private bus**: the watcher would
        // read the person's own players.
        const QByteArray address = qgetenv("DBUS_SESSION_BUS_ADDRESS");
        ASSERT_FALSE(address.isEmpty());
        ASSERT_FALSE(address.contains(QByteArray("/run/user/")))
            << "this is the real session bus; run it through ctest";
        ASSERT_TRUE(QDBusConnection::sessionBus().isConnected());
    }

    void TearDown() override
    {
        // **Every player stopped while the bus is still there**: a leftover
        // would hold ctest's output open until the timeout.
        for (QProcess &player : players) {
            player.terminate();
            if (!player.waitForFinished(5000)) {
                player.kill();
                player.waitForFinished(5000);
            }
        }
        players.clear();
    }

    /// Starts a player called org.mpris.MediaPlayer2.\a name, from
    /// \a program (the fake player, or a link to it under another name), and
    /// waits until it is ready.
    QProcess *startPlayer(const QString &name,
                          const QString &identity = u"Test Player"_s,
                          const QString &desktopEntry = {},
                          const QString &program = QStringLiteral(RB_FAKE_MPRIS_PLAYER))
    {
        QProcess &player = players.emplace_back();
        player.setProcessChannelMode(QProcess::SeparateChannels);
        player.setReadChannel(QProcess::StandardOutput);
        QStringList arguments{u"--name"_s, name, u"--identity"_s, identity};
        if (!desktopEntry.isEmpty()) {
            arguments << u"--desktop-entry"_s << desktopEntry;
        }
        player.start(program, arguments);
        if (!player.waitForStarted(5000)
            || !waitFor([&player] { return player.canReadLine() || player.waitForReadyRead(10); })) {
            return nullptr;
        }
        return player.readLine().trimmed() == "ready" ? &player : nullptr;
    }

    static void stopPlayer(QProcess *player)
    {
        player->terminate();
        player->waitForFinished(5000);
    }

    /// Tells the player \a name to \a member, with \a arguments.
    static bool tell(const QString &name, const QString &member, const QVariantList &arguments)
    {
        QDBusMessage call = QDBusMessage::createMethodCall(
            u"org.mpris.MediaPlayer2."_s + name, u"/org/mpris/MediaPlayer2"_s, u"org.reverblabr.Test"_s, member);
        call.setArguments(arguments);
        return QDBusConnection::sessionBus().call(call, QDBus::Block, 5000).type() == QDBusMessage::ReplyMessage;
    }

    static bool setTrack(const QString &name, const QString &artist, const QString &title, const QString &album = {})
    {
        return tell(name, u"SetTrack"_s, {artist, title, album});
    }

    [[nodiscard]] static const Player *findPlayer(const PlayerWatcher &watcher, const QString &key)
    {
        for (const Player &player : watcher.players()) {
            if (player.key == key) {
                return &player;
            }
        }
        return nullptr;
    }

    std::list<QProcess> players;
};

} // namespace

TEST_F(MprisTest, APlayerThatComesIsReadAndOneThatGoesIsForgotten)
{
    PlayerWatcher watcher(QDBusConnection::sessionBus());
    ASSERT_TRUE(waitFor([&] { return watcher.isReady(); }));
    EXPECT_TRUE(watcher.players().empty());

    QProcess *process = startPlayer(u"fakeplayer.instance77"_s, u"Fake One"_s, u"org.example.FakeOne"_s);
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(waitFor([&] {
        const Player *player = findPlayer(watcher, u"fakeplayer"_s);
        return player != nullptr && !player->identity.isEmpty() && player->pid != 0;
    }));
    const Player &player = *findPlayer(watcher, u"fakeplayer"_s);
    EXPECT_EQ(player.busName, u"org.mpris.MediaPlayer2.fakeplayer.instance77"_s);
    EXPECT_EQ(player.identity, u"Fake One"_s);
    EXPECT_EQ(player.desktopEntry, u"org.example.FakeOne"_s);
    EXPECT_EQ(player.pid, static_cast<std::uint32_t>(process->processId()));
    EXPECT_TRUE(player.binaries.contains(u"reverblabr-fake-mpris-player"_s))
        << player.binaries.join(u' ').toStdString();
    EXPECT_EQ(player.playback, Playback::Stopped);

    // **Told, not asked**: each change arrives as the player says it.
    QSignalSpy changed(&watcher, &PlayerWatcher::changed);
    ASSERT_TRUE(setTrack(u"fakeplayer.instance77"_s, u"Hania Rani"_s, u"Eden"_s, u"Esja"_s));
    ASSERT_TRUE(waitFor([&] { return findPlayer(watcher, u"fakeplayer"_s)->track.title == u"Eden"_s; }));
    EXPECT_EQ(findPlayer(watcher, u"fakeplayer"_s)->track, (Track{u"Hania Rani"_s, u"Eden"_s, u"Esja"_s}));
    EXPECT_FALSE(changed.isEmpty());
    ASSERT_TRUE(tell(u"fakeplayer.instance77"_s, u"SetPlayback"_s, {u"Playing"_s}));
    ASSERT_TRUE(waitFor([&] { return findPlayer(watcher, u"fakeplayer"_s)->playback == Playback::Playing; }));

    // A change said only as "this has changed" is read again.
    ASSERT_TRUE(tell(u"fakeplayer.instance77"_s, u"SetInvalidateOnly"_s, {true}));
    ASSERT_TRUE(setTrack(u"fakeplayer.instance77"_s, u"Hania Rani"_s, u"Glass"_s));
    ASSERT_TRUE(waitFor([&] { return findPlayer(watcher, u"fakeplayer"_s)->track.title == u"Glass"_s; }));
    EXPECT_TRUE(findPlayer(watcher, u"fakeplayer"_s)->track.album.isEmpty());

    stopPlayer(process);
    ASSERT_TRUE(waitFor([&] { return watcher.players().empty(); }));
}

TEST_F(MprisTest, ThePlayersAlreadyThereAreListedWithWhatTheyArePlaying)
{
    ASSERT_NE(startPlayer(u"first"_s), nullptr);
    ASSERT_NE(startPlayer(u"second.instance5"_s), nullptr);
    ASSERT_TRUE(setTrack(u"second.instance5"_s, u"Kiasmos"_s, u"Blurred"_s));
    ASSERT_TRUE(tell(u"second.instance5"_s, u"SetPlayback"_s, {u"Paused"_s}));

    PlayerWatcher watcher(QDBusConnection::sessionBus());
    ASSERT_TRUE(waitFor([&] {
        const Player *second = findPlayer(watcher, u"second"_s);
        return findPlayer(watcher, u"first"_s) != nullptr && second != nullptr && second->track.title == u"Blurred"_s;
    }));
    ASSERT_EQ(watcher.players().size(), 2U);
    EXPECT_EQ(watcher.players()[0].key, u"first"_s) << "in the order of their names";
    EXPECT_EQ(findPlayer(watcher, u"second"_s)->playback, Playback::Paused);
    EXPECT_EQ(findPlayer(watcher, u"second"_s)->track.artist, u"Kiasmos"_s);
}

// **The correct player among several**: the one the session captures,
// found by the name its process was started by, and not the other that is
// playing too; and a title change reaching the stream within two seconds,
// several in a row handed on as one.
TEST_F(MprisTest, AFeedFollowsThePlayerTheSessionCapturesAndHandsOnItsTitles)
{
    QTemporaryDir links;
    ASSERT_TRUE(links.isValid());
    const QString elisa = links.filePath(u"fake-elisa"_s);
    ASSERT_TRUE(QFile::link(QStringLiteral(RB_FAKE_MPRIS_PLAYER), elisa));
    ASSERT_NE(startPlayer(u"fakeelisa"_s, u"Fake Elisa"_s, {}, elisa), nullptr);
    ASSERT_NE(startPlayer(u"fakevlc"_s, u"Fake VLC"_s), nullptr);
    for (const QString &name : {u"fakeelisa"_s, u"fakevlc"_s}) {
        ASSERT_TRUE(setTrack(name, u"Artist"_s, name));
        ASSERT_TRUE(tell(name, u"SetPlayback"_s, {u"Playing"_s}));
    }

    PlayerWatcher watcher(QDBusConnection::sessionBus());
    ASSERT_TRUE(waitFor([&] {
        const Player *found = findPlayer(watcher, u"fakeelisa"_s);
        return found != nullptr && found->binaries.contains(u"fake-elisa"_s)
            && findPlayer(watcher, u"fakevlc"_s) != nullptr
            && findPlayer(watcher, u"fakevlc"_s)->playback == Playback::Playing;
    }));
    // VLC started playing last, and is still not the one.
    ASSERT_TRUE(tell(u"fakevlc"_s, u"SetPlayback"_s, {u"Paused"_s}));
    ASSERT_TRUE(tell(u"fakevlc"_s, u"SetPlayback"_s, {u"Playing"_s}));

    TrackFeed feed(watcher, TrackFeed::Options{100ms, 600ms});
    QSignalSpy tracks(&feed, &TrackFeed::trackChanged);
    feed.setChoice({}, {CapturedApplication{u"fake-elisa"_s, {}, {}}, CapturedApplication{u"zoom"_s, {}, {}}});
    ASSERT_TRUE(waitFor([&] { return !tracks.isEmpty(); }));
    EXPECT_EQ(tracks.back().at(0).value<Track>().title, u"fakeelisa"_s);
    EXPECT_EQ(feed.playerBusName(), u"org.mpris.MediaPlayer2.fakeelisa"_s);

    // The other player's title is not this stream's.
    tracks.clear();
    ASSERT_TRUE(setTrack(u"fakevlc"_s, u"Someone"_s, u"Elsewhere"_s));
    pump(800);
    EXPECT_TRUE(tracks.isEmpty());

    // **Within two seconds**, and three changes in a row are handed on as
    // the last of them, not one by one.
    QElapsedTimer clock;
    clock.start();
    ASSERT_TRUE(setTrack(u"fakeelisa"_s, u"Jon Hopkins"_s, u"Open Eye Signal"_s));
    ASSERT_TRUE(setTrack(u"fakeelisa"_s, u"Jon Hopkins"_s, u"Collider"_s));
    ASSERT_TRUE(setTrack(u"fakeelisa"_s, u"Jon Hopkins"_s, u"Abandon Window"_s, u"Immunity"_s));
    ASSERT_TRUE(waitFor(
        [&] { return !tracks.isEmpty() && tracks.back().at(0).value<Track>().title == u"Abandon Window"_s; }, 2000))
        << "the title did not arrive within two seconds";
    EXPECT_LT(clock.elapsed(), 2000);
    pump(800);
    EXPECT_LE(tracks.count(), 2) << "every step was handed on";
    EXPECT_EQ(tracks.back().at(0).value<Track>(), (Track{u"Jon Hopkins"_s, u"Abandon Window"_s, u"Immunity"_s}));

    // **A player named** is followed instead, and a stop is no title.
    tracks.clear();
    feed.setChoice(u"fakevlc"_s, {CapturedApplication{u"fake-elisa"_s, {}, {}}});
    ASSERT_TRUE(waitFor([&] { return !tracks.isEmpty(); }));
    EXPECT_EQ(tracks.back().at(0).value<Track>().title, u"Elsewhere"_s);
    tracks.clear();
    ASSERT_TRUE(tell(u"fakevlc"_s, u"SetPlayback"_s, {u"Stopped"_s}));
    ASSERT_TRUE(waitFor([&] { return !tracks.isEmpty(); }));
    EXPECT_TRUE(tracks.back().at(0).value<Track>().isEmpty());
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
