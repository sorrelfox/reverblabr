// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The title listeners see: the template, and the now-playing file.

#include "stream/title.h"

#include <gtest/gtest.h>
#include <sys/stat.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace rb::stream;

namespace
{

[[nodiscard]] Track track(std::string artist, std::string title, std::string album = {})
{
    return Track{std::move(artist), std::move(title), std::move(album)};
}

void write(const std::filesystem::path &path, const std::string &text)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
}

} // namespace

TEST(BroadcastTitleTest, TheTokensAreTheTracksAndTheSessions)
{
    EXPECT_EQ(renderTitle("%artist - %title", track("Ana", "Morning"), "Show"), "Ana - Morning");
    EXPECT_EQ(renderTitle("%title (%album)", track("", "Morning", "Dawn"), "Show"), "Morning (Dawn)");
    EXPECT_EQ(renderTitle("%session: %title", track("", "Morning"), "Night Shift"), "Night Shift: Morning");
    EXPECT_EQ(renderTitle("100%% %title", track("", "Morning"), ""), "100% Morning");
    // Something that only looks like a token is text.
    EXPECT_EQ(renderTitle("%titles %x", track("", "A"), ""), "As %x");
}

TEST(BroadcastTitleTest, TextBetweenTokensGoesWithAnEmptyOne)
{
    EXPECT_EQ(renderTitle("%artist - %title", track("", "Morning"), ""), "Morning");
    EXPECT_EQ(renderTitle("%artist - %title", track("Ana", ""), ""), "Ana");
    EXPECT_EQ(renderTitle("Now: %artist - %title", track("", "Morning"), ""), "Now: Morning");
    EXPECT_EQ(renderTitle("%artist - %title (%album)", track("Ana", "Morning"), ""), "Ana - Morning");
    EXPECT_EQ(renderTitle("%title (%album)", track("", "Morning"), ""), "Morning");
    EXPECT_EQ(renderTitle("%title!", track("", "Morning"), ""), "Morning!");
}

TEST(BroadcastTitleTest, NothingToSayIsNoTitleAndATemplateWithoutTokensIsOne)
{
    EXPECT_EQ(renderTitle("%artist - %title", Track{}, "Show"), "");
    EXPECT_EQ(renderTitle("Live from the studio", Track{}, ""), "Live from the studio");
    EXPECT_EQ(renderTitle("", track("Ana", "Morning"), ""), "");
}

TEST(BroadcastTitleTest, ATitleHasNoLineBreaksAndFitsAServer)
{
    EXPECT_EQ(renderTitle("%title", track("", "One\r\nTwo"), ""), "OneTwo");
    const std::string longTitle(300, 'x');
    EXPECT_EQ(renderTitle("%title", track("", longTitle), "").size(), 256U);
    // Cut at a whole character, never in the middle of one.
    std::string accents;
    for (int index = 0; index < 200; ++index) {
        accents += "é";
    }
    const std::string cut = renderTitle("%title", track("", accents), "");
    EXPECT_LE(cut.size(), 256U);
    EXPECT_EQ(cut.size() % 2, 0U);
}

TEST(BroadcastTitleTest, TheNowPlayingFileIsItsFirstLineWithAnythingOnIt)
{
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / "rb-nowplaying-test";
    std::filesystem::create_directories(folder);
    const std::filesystem::path path = folder / "song.txt";
    write(path, "\xEF\xBB\xBF\n   \n  Ana - Morning  \nsecond line\n");
    EXPECT_EQ(readNowPlayingFile(path), "Ana - Morning");
    write(path, "");
    EXPECT_EQ(readNowPlayingFile(path), "");
    EXPECT_FALSE(readNowPlayingFile(folder / "missing.txt").has_value());
    std::filesystem::remove_all(folder);
}

TEST(BroadcastTitleTest, TheWatcherSaysWhatChangedOnceAndNothingElse)
{
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / "rb-nowplaying-watch";
    std::filesystem::create_directories(folder);
    const std::filesystem::path path = folder / "song.txt";
    NowPlayingFile watcher;
    EXPECT_FALSE(watcher.poll(path).has_value()) << "a file that is not there has nothing to say";

    write(path, "First\n");
    EXPECT_EQ(watcher.poll(path), "First");
    EXPECT_FALSE(watcher.poll(path).has_value()) << "the same title was said twice";

    write(path, "Second\n");
    // The file's time has to move for the change to be seen.
    std::filesystem::last_write_time(path, std::filesystem::last_write_time(path) + std::chrono::seconds{1});
    EXPECT_EQ(watcher.poll(path), "Second");

    write(path, "Second\n");
    std::filesystem::last_write_time(path, std::filesystem::last_write_time(path) + std::chrono::seconds{1});
    EXPECT_FALSE(watcher.poll(path).has_value()) << "written again with the same title";

    EXPECT_FALSE(watcher.poll({}).has_value());
    std::filesystem::remove_all(folder);
}

// **Nothing that can make a read wait**: a pipe named as the file is not
// read, and neither is a folder.
TEST(BroadcastTitleTest, OnlyARegularFileIsRead)
{
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / "rb-nowplaying-fifo";
    std::filesystem::create_directories(folder);
    const std::filesystem::path fifo = folder / "song";
    std::filesystem::remove(fifo);
    ASSERT_EQ(::mkfifo(fifo.c_str(), 0600), 0);
    const auto before = std::chrono::steady_clock::now();
    EXPECT_FALSE(readNowPlayingFile(fifo).has_value());
    NowPlayingFile watcher;
    EXPECT_FALSE(watcher.poll(fifo).has_value());
    EXPECT_LT(std::chrono::steady_clock::now() - before, std::chrono::seconds{1});
    EXPECT_FALSE(readNowPlayingFile(folder).has_value());
    std::filesystem::remove_all(folder);
}
