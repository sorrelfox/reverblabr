// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/errorcodes.h"

#include <gtest/gtest.h>

#include <regex>
#include <set>
#include <string>

using namespace rb::core;

TEST(ErrorCodes, NamesFollowTheConvention)
{
    const std::regex pattern("^[a-z]+\\.[a-z][A-Za-z0-9]*$");
    std::set<std::string_view> seen;
    for (const ErrorCodeInfo &info : kErrorCodes) {
        EXPECT_TRUE(std::regex_match(std::string(info.name), pattern)) << info.name;
        EXPECT_TRUE(seen.insert(info.name).second) << "duplicate code " << info.name;
    }
}

TEST(ErrorCodes, ArgumentNamesAreUniqueAndPacked)
{
    for (const ErrorCodeInfo &info : kErrorCodes) {
        std::set<std::string_view> names;
        bool sawEmpty = false;
        for (const std::string_view name : info.arguments) {
            if (name.empty()) {
                sawEmpty = true;
                continue;
            }
            EXPECT_FALSE(sawEmpty) << info.name << ": arguments must come before the empty slots";
            EXPECT_TRUE(names.insert(name).second) << info.name << ": duplicate argument " << name;
        }
    }
}

TEST(ErrorCodes, NameLookup)
{
    EXPECT_EQ(errorCodeName(ErrorCode::GraphCycle), "graph.cycle");
    EXPECT_EQ(errorCodeName(ErrorCode::SessionCorruptFile), "session.corruptFile");
}
