// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// A recording's file name: the tokens, what is made safe, and never
// landing on a file that is already there.

#include "record/filename.h"

#include <gtest/gtest.h>

#include <chrono>
#include <set>
#include <string>

using namespace rb::record;
using namespace std::chrono;

namespace
{

[[nodiscard]] FileNameValues podcastAt(int hour, int minute, int second)
{
    FileNameValues values;
    values.session = "Podcast";
    values.block = "Recorder 1";
    values.when = local_days{year{2026} / September / 25} + hours{hour} + minutes{minute} + seconds{second};
    return values;
}

} // namespace

TEST(FileName, TheTokensAreFilledIn)
{
    const FileNameValues values = podcastAt(14, 3, 22);
    // ISO order throughout; hyphens in the time, because a colon is refused
    // by the file systems memory sticks use.
    EXPECT_EQ(renderFileName("%session %date %time", values), "Podcast 2026-09-25 14-03-22");
    EXPECT_EQ(renderFileName("%block-%counter", values), "Recorder 1-001");
    EXPECT_EQ(renderFileName("take 100%% %session", values), "take 100% Podcast");
    EXPECT_EQ(renderFileName("", values), "");
}

TEST(FileName, APercentThatStartsNoTokenIsKeptAsTyped)
{
    const FileNameValues values = podcastAt(9, 0, 0);
    EXPECT_EQ(renderFileName("%sesion", values), "%sesion");
    EXPECT_EQ(renderFileName("50%", values), "50%");
}

TEST(FileName, ANameIsMadeSafeForEveryFileSystem)
{
    // A session called "Mix 1/2" is a name, not a folder.
    EXPECT_EQ(sanitizeFileName("Mix 1/2"), "Mix 1-2");
    EXPECT_EQ(sanitizeFileName("a\\b"), "a-b");
    EXPECT_EQ(sanitizeFileName("line\nbreak\ttab"), "line break tab");
    // No hidden file, and nothing to trip over at either end.
    EXPECT_EQ(sanitizeFileName("...hidden"), "hidden");
    EXPECT_EQ(sanitizeFileName("  spaced out.  "), "spaced out");
    EXPECT_EQ(sanitizeFileName(""), "Recording");
    EXPECT_EQ(sanitizeFileName(" / "), "-");
    EXPECT_EQ(sanitizeFileName(" . "), "Recording");
    EXPECT_EQ(sanitizeFileName("Grüße · 音"), "Grüße · 音");
}

TEST(FileName, ALongNameIsCutBetweenCharacters)
{
    // Two-byte characters across the limit: the cut must not leave half of
    // one, which would not be UTF-8.
    std::string name;
    while (name.size() < 400) {
        name += "é";
    }
    const std::string cut = sanitizeFileName(name);
    EXPECT_LE(cut.size(), kMaxFileNameBytes);
    EXPECT_GE(cut.size(), kMaxFileNameBytes - 1);
    EXPECT_EQ(cut.size() % 2, 0U) << "a character was cut in half";
}

TEST(FileName, AFreeNameIsFoundAndNothingIsOverwritten)
{
    std::set<std::filesystem::path> taken;
    const auto exists = [&taken](const std::filesystem::path &path) { return taken.contains(path); };
    const std::filesystem::path folder{"/music"};

    // Without a counter: " (2)", " (3)" after the name.
    const FileNameValues values = podcastAt(14, 3, 22);
    for (const char *expected : {"Podcast 2026-09-25 14-03-22.wav",
                                 "Podcast 2026-09-25 14-03-22 (2).wav",
                                 "Podcast 2026-09-25 14-03-22 (3).wav"}) {
        const std::filesystem::path path = freePath(folder, "%session %date %time", values, "wav", exists);
        EXPECT_EQ(path, folder / expected);
        taken.insert(path);
    }

    // With one: the counter goes up instead.
    for (const char *expected : {"Podcast 001.flac", "Podcast 002.flac"}) {
        const std::filesystem::path path = freePath(folder, "%session %counter", values, "flac", exists);
        EXPECT_EQ(path, folder / expected);
        taken.insert(path);
    }
}
