// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What a media player says, read off its bus name and its metadata, and
// which of several a stream's title follows. Without a bus: the players on
// one, and their signals, are tests/bus.

#include "mpris/player.h"

#include <QDBusVariant>
#include <gtest/gtest.h>

#include <vector>

using namespace rb::mpris;
using namespace Qt::StringLiterals;

namespace
{

[[nodiscard]] Player player(const QString &busName, Playback playback, std::uint64_t since = 0)
{
    Player made;
    made.busName = busName;
    made.key = playerKey(busName);
    made.playback = playback;
    made.playingSince = since;
    made.track.title = busName;
    return made;
}

} // namespace

TEST(MprisPlayerTest, TheKeyIsTheSamePlayerAcrossItsCopies)
{
    EXPECT_EQ(playerKey(u"org.mpris.MediaPlayer2.vlc"_s), u"vlc"_s);
    EXPECT_EQ(playerKey(u"org.mpris.MediaPlayer2.vlc.instance4242"_s), u"vlc"_s);
    EXPECT_EQ(playerKey(u"org.mpris.MediaPlayer2.firefox.instance_1_23"_s), u"firefox"_s);
    EXPECT_EQ(playerKey(u"org.mpris.MediaPlayer2.chromium.instance10871"_s), u"chromium"_s);
    // A dot of its own is part of the name.
    EXPECT_EQ(playerKey(u"org.mpris.MediaPlayer2.org.kde.elisa"_s), u"org.kde.elisa"_s);
    EXPECT_TRUE(playerKey(u"org.mpris.MediaPlayer2"_s).isEmpty());
    EXPECT_TRUE(playerKey(u"org.mpris.MediaPlayer2."_s).isEmpty());
    EXPECT_TRUE(playerKey(u"org.kde.StatusNotifierWatcher"_s).isEmpty());
    EXPECT_TRUE(playerKey(u":1.42"_s).isEmpty());
}

TEST(MprisPlayerTest, TheTrackIsReadFromTheMetadata)
{
    const QVariantMap metadata{
        {u"xesam:artist"_s, QStringList{u" Nils Frahm "_s, u"Ólafur Arnalds"_s, u""_s}},
        {u"xesam:title"_s, QVariant::fromValue(QDBusVariant(u"Four "_s))},
        {u"xesam:album"_s, u"Trance Frendz"_s},
        {u"mpris:length"_s, qlonglong(240000000)},
    };
    const Track track = trackFromMetadata(metadata);
    EXPECT_EQ(track.artist, u"Nils Frahm, Ólafur Arnalds"_s);
    EXPECT_EQ(track.title, u"Four"_s);
    EXPECT_EQ(track.album, u"Trance Frendz"_s);

    // A player that gives the artist as one string, against the spec.
    const Track single = trackFromMetadata({{u"xesam:artist"_s, u"Grouper"_s}});
    EXPECT_EQ(single.artist, u"Grouper"_s);
    EXPECT_TRUE(single.title.isEmpty());
    EXPECT_TRUE(trackFromMetadata({}).isEmpty());
}

TEST(MprisPlayerTest, OnlyPlayingAndPausedAreNotStopped)
{
    EXPECT_EQ(playbackFromStatus(u"Playing"_s), Playback::Playing);
    EXPECT_EQ(playbackFromStatus(u"Paused"_s), Playback::Paused);
    EXPECT_EQ(playbackFromStatus(u"Stopped"_s), Playback::Stopped);
    EXPECT_EQ(playbackFromStatus(u"playing"_s), Playback::Stopped);
    EXPECT_EQ(playbackFromStatus({}), Playback::Stopped);
}

TEST(MprisPlayerTest, AnApplicationIsAPlayerByAnyOfItsNames)
{
    Player elisa = player(u"org.mpris.MediaPlayer2.elisa"_s, Playback::Playing);
    elisa.identity = u"Elisa"_s;
    elisa.desktopEntry = u"org.kde.elisa"_s;
    elisa.binaries = {u"elisa"_s};
    EXPECT_TRUE(isApplication(elisa, {u"elisa"_s, {}, {}}));
    EXPECT_TRUE(isApplication(elisa, {u"elisa-wrapped"_s, u"org.kde.elisa"_s, {}}));
    EXPECT_TRUE(isApplication(elisa, {{}, {}, u"elisa"_s}));
    EXPECT_FALSE(isApplication(elisa, {u"vlc"_s, u"org.videolan.VLC"_s, u"VLC media player"_s}));
    EXPECT_FALSE(isApplication(elisa, {}));
}

TEST(MprisPlayerTest, ANamedPlayerIsFollowedWhateverTheSessionCaptures)
{
    const std::vector<Player> players{player(u"org.mpris.MediaPlayer2.elisa"_s, Playback::Playing, 2),
                                      player(u"org.mpris.MediaPlayer2.vlc.instance7"_s, Playback::Paused, 1)};
    const std::vector<CapturedApplication> captured{{u"elisa"_s, {}, {}}};
    const Player *chosen = choosePlayer(players, u"vlc"_s, captured);
    ASSERT_NE(chosen, nullptr);
    EXPECT_EQ(chosen->busName, u"org.mpris.MediaPlayer2.vlc.instance7"_s);
    EXPECT_EQ(choosePlayer(players, u"spotify"_s, captured), nullptr) << "a player that is not running is none";
}

TEST(MprisPlayerTest, TheAutomaticChoiceIsAPlayerTheSessionCaptures)
{
    std::vector<Player> players{player(u"org.mpris.MediaPlayer2.elisa"_s, Playback::Paused, 1),
                                player(u"org.mpris.MediaPlayer2.vlc"_s, Playback::Playing, 2)};
    players[0].binaries = {u"elisa"_s};
    players[1].binaries = {u"vlc"_s};

    // **The captured one, even paused**, over another that is playing:
    // that one's music is not in the stream.
    const std::vector<CapturedApplication> elisa{{u"elisa"_s, {}, {}}, {u"zoom"_s, {}, {}}};
    ASSERT_NE(choosePlayer(players, {}, elisa), nullptr);
    EXPECT_EQ(choosePlayer(players, {}, elisa)->key, u"elisa"_s);
    // A session that captures applications, none of them a player, has none.
    const std::vector<CapturedApplication> zoom{{u"zoom"_s, {}, {}}};
    EXPECT_EQ(choosePlayer(players, {}, zoom), nullptr);
    // **A session that captures no application** (a device, the whole
    // system) takes the one that is playing.
    ASSERT_NE(choosePlayer(players, {}, {}), nullptr);
    EXPECT_EQ(choosePlayer(players, {}, {})->key, u"vlc"_s);
}

TEST(MprisPlayerTest, OfSeveralThePlayingOneThatStartedLastWins)
{
    std::vector<Player> players{player(u"org.mpris.MediaPlayer2.a"_s, Playback::Playing, 3),
                                player(u"org.mpris.MediaPlayer2.b"_s, Playback::Playing, 5),
                                player(u"org.mpris.MediaPlayer2.c"_s, Playback::Paused, 9),
                                player(u"org.mpris.MediaPlayer2.d"_s, Playback::Stopped, 0)};
    EXPECT_EQ(choosePlayer(players, {}, {})->key, u"b"_s);
    players[1].playback = Playback::Stopped;
    EXPECT_EQ(choosePlayer(players, {}, {})->key, u"a"_s);
    players[0].playback = Playback::Stopped;
    EXPECT_EQ(choosePlayer(players, {}, {})->key, u"c"_s) << "paused before stopped";
    EXPECT_EQ(choosePlayer({}, {}, {}), nullptr);
}

TEST(MprisPlayerTest, AStoppedPlayerIsPlayingNothing)
{
    Player vlc = player(u"org.mpris.MediaPlayer2.vlc"_s, Playback::Paused);
    EXPECT_EQ(nowPlaying(&vlc).title, vlc.busName) << "a pause keeps the title on the air";
    vlc.playback = Playback::Stopped;
    EXPECT_TRUE(nowPlaying(&vlc).isEmpty());
    EXPECT_TRUE(nowPlaying(nullptr).isEmpty());
}
