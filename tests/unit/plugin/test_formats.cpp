// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Where each plugin format looks, and what it takes for a plugin.

#include "plugin/formats.h"

#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

using namespace rb::plugin;
namespace fs = std::filesystem;

namespace
{

/// A folder of the test's own, gone with it.
class Folder
{
public:
    Folder()
    {
        std::string pattern = (fs::temp_directory_path() / "rb-formats-XXXXXX").string();
        m_path = ::mkdtemp(pattern.data()) != nullptr ? fs::path{pattern} : fs::path{};
        EXPECT_FALSE(m_path.empty());
    }
    ~Folder()
    {
        std::error_code ignored;
        fs::remove_all(m_path, ignored);
    }
    Folder(const Folder &) = delete;
    Folder &operator=(const Folder &) = delete;

    [[nodiscard]] const fs::path &path() const
    {
        return m_path;
    }

    /// An empty file at \a relative, with the folders it is in.
    fs::path file(const std::string &relative) const
    {
        const fs::path made = m_path / relative;
        fs::create_directories(made.parent_path());
        std::ofstream{made} << "x";
        return made;
    }

private:
    fs::path m_path;
};

/// An environment variable set, or unset, for the length of one test, and
/// put back as it was.
class Variable
{
public:
    Variable(const char *name, const char *value)
        : m_name(name)
    {
        if (const char *before = std::getenv(name); before != nullptr) {
            m_before = before;
        }
        if (value != nullptr) {
            ::setenv(name, value, 1);
        } else {
            ::unsetenv(name);
        }
    }
    Variable(const char *name, const fs::path &value)
        : Variable(name, value.c_str())
    {
    }
    ~Variable()
    {
        if (m_before) {
            ::setenv(m_name, m_before->c_str(), 1);
        } else {
            ::unsetenv(m_name);
        }
    }
    Variable(const Variable &) = delete;
    Variable &operator=(const Variable &) = delete;

private:
    const char *m_name;
    std::optional<std::string> m_before;
};

} // namespace

#ifdef RB_HAVE_CLAP

namespace
{

[[nodiscard]] std::vector<std::string> pathsOf(const std::vector<Candidate> &candidates)
{
    std::vector<std::string> paths;
    for (const Candidate &candidate : candidates) {
        paths.push_back(candidate.path);
    }
    return paths;
}

} // namespace

TEST(ClapFormat, APluginIsAClapFileAnywhereUnderItsFolders)
{
    Folder named;
    Folder defaults;
    const fs::path top = named.file("top.clap");
    const fs::path deep = named.file("vendor/suite/deep.clap");
    named.file("vendor/readme.txt");
    named.file("vendor/libsomething.so");
    fs::create_directories(named.path() / "folder.clap");
    const Variable path("CLAP_PATH", named.path());
    const Variable instead(kClapDefaultPathVariable, defaults.path());

    const PluginFormat *clap = formatNamed("clap");
    ASSERT_NE(clap, nullptr) << "a build with the CLAP SDK looks for CLAP plugins";
    EXPECT_TRUE(clap->searchesSubfolders());
    EXPECT_EQ(pathsOf(candidatesOf(*clap)), (std::vector<std::string>{top.string(), deep.string()}))
        << "only files ending in .clap, however deep, and never a folder of that name";
}

TEST(ClapFormat, ClapPathIsSearchedAsWellAsTheUsualFoldersNotInsteadOfThem)
{
    Folder named;
    Folder usual;
    const fs::path mine = named.file("mine.clap");
    const fs::path installed = usual.file("installed.clap");
    const Variable path("CLAP_PATH", named.path());
    const Variable instead(kClapDefaultPathVariable, usual.path());

    const PluginFormat *clap = formatNamed("clap");
    ASSERT_NE(clap, nullptr);
    const std::vector<fs::path> searched = clap->searchPaths();
    ASSERT_EQ(searched.size(), 2U);
    EXPECT_EQ(searched[0], named.path()) << "what CLAP_PATH names comes first";
    EXPECT_EQ(searched[1], usual.path());
    std::vector<std::string> expected{installed.string(), mine.string()};
    std::ranges::sort(expected); // candidates come in path order
    EXPECT_EQ(pathsOf(candidatesOf(*clap)), expected);
}

TEST(ClapFormat, TheUsualFoldersAreTheHomeFoldersAndTheSystems)
{
    const Variable home("HOME", "/home/somebody");
    const Variable path("CLAP_PATH", nullptr);
    const Variable instead(kClapDefaultPathVariable, nullptr);
    const PluginFormat *clap = formatNamed("clap");
    ASSERT_NE(clap, nullptr);
    const std::vector<fs::path> searched = clap->searchPaths();
    ASSERT_FALSE(searched.empty());
    EXPECT_EQ(searched.front(), fs::path{"/home/somebody/.clap"});
    EXPECT_NE(std::ranges::find(searched, fs::path{"/usr/lib/clap"}), searched.end());
    EXPECT_NE(std::ranges::find(searched, fs::path{"/usr/lib64/clap"}), searched.end()) << "where Fedora installs them";
}

#else

TEST(ClapFormat, ABuildWithoutTheClapSdkDoesNotLookForClapPlugins)
{
    EXPECT_EQ(formatNamed("clap"), nullptr);
}

#endif
