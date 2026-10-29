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

#if defined(RB_HAVE_CLAP) || defined(RB_HAVE_VST3)

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

#endif

#ifdef RB_HAVE_CLAP

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

#ifdef RB_HAVE_VST3

TEST(Vst3Format, APluginIsABundleAnywhereUnderItsFoldersAndNotLookedInto)
{
    const Folder named;
    const Folder defaults;
    // A bundle is a folder; a vendor may keep its own in a folder of its own.
    const fs::path top = named.file("top.vst3/Contents/x86_64-linux/top.so").parent_path().parent_path().parent_path();
    const fs::path deep =
        named.file("Vendor/deep.vst3/Contents/x86_64-linux/deep.so").parent_path().parent_path().parent_path();
    // **Inside a bundle is the plugin's own**: a folder there that ends in
    // .vst3 is not another plugin.
    fs::create_directories(top / "Contents" / "Resources" / "inner.vst3");
    (void)named.file("loose.vst3.txt");
    (void)named.file("file.vst3");
    const Variable path("VST3_PATH", named.path());
    const Variable instead(kVst3DefaultPathVariable, defaults.path());
    const PluginFormat *vst3 = formatNamed("vst3");
    ASSERT_NE(vst3, nullptr) << "a build with the VST3 SDK looks for VST3 plugins";
    EXPECT_TRUE(vst3->searchesSubfolders());
    std::vector<std::string> expected{deep.string(), top.string()};
    std::ranges::sort(expected);
    EXPECT_EQ(pathsOf(candidatesOf(*vst3)), expected) << "folders ending in .vst3, however deep, and nothing in them";
}

TEST(Vst3Format, TheUsualFoldersAreTheHomeFoldersAndTheSystemsAndVst3PathAddsToThem)
{
    const Variable home("HOME", "/home/somebody");
    const Variable path("VST3_PATH", "/opt/mine/vst3");
    const Variable instead(kVst3DefaultPathVariable, nullptr);
    const PluginFormat *vst3 = formatNamed("vst3");
    ASSERT_NE(vst3, nullptr);
    const std::vector<fs::path> searched = vst3->searchPaths();
    ASSERT_GE(searched.size(), 3U);
    EXPECT_EQ(searched[0], fs::path{"/opt/mine/vst3"}) << "what VST3_PATH names comes first";
    EXPECT_EQ(searched[1], fs::path{"/home/somebody/.vst3"});
    EXPECT_NE(std::ranges::find(searched, fs::path{"/usr/lib/vst3"}), searched.end());
    EXPECT_NE(std::ranges::find(searched, fs::path{"/usr/lib64/vst3"}), searched.end()) << "where Fedora installs them";
}

#else

TEST(Vst3Format, ABuildWithoutTheVst3SdkDoesNotLookForVst3Plugins)
{
    EXPECT_EQ(formatNamed("vst3"), nullptr);
}

#endif
