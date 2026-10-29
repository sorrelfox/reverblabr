// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "pw/version.h"

#include <gtest/gtest.h>

#include <charconv>
#include <string_view>
#include <utility>

namespace
{

// Parses the "major.minor" prefix of a version string such as "1.6.8".
std::pair<int, int> majorMinor(std::string_view version)
{
    int major = -1;
    int minor = -1;
    const char *end = version.data() + version.size();
    auto [next, error] = std::from_chars(version.data(), end, major);
    if (error == std::errc{} && next != end && *next == '.') {
        std::from_chars(next + 1, end, minor);
    }
    return {major, minor};
}

// The minimum PipeWire version the project supports.
constexpr std::pair<int, int> kMinimum{1, 6};

} // namespace

TEST(Version, HeadersMeetMinimum)
{
    EXPECT_GE(majorMinor(rb::pw::headersVersion()), kMinimum) << rb::pw::headersVersion();
}

TEST(Version, LibraryMeetsMinimum)
{
    EXPECT_GE(majorMinor(rb::pw::libraryVersion()), kMinimum) << rb::pw::libraryVersion();
}
