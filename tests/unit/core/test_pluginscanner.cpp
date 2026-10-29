// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Finding the plugins installed on this machine, with the real plugin
// host describing each candidate: what is found and cached, a candidate
// that crashes or hangs being blocklisted while the scan goes on, and a
// scan looking only at what changed.

#include "blocks/builtinblocks.h"
#include "core/engine.h"
#include "core/pluginscanner.h"
#include "core/sessionstore.h"
#include "dsp/blockregistry.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>

using namespace rb;
using namespace rb::core;
using namespace Qt::StringLiterals;
using namespace std::chrono_literals;

namespace
{

void needApplication()
{
    if (QCoreApplication::instance() == nullptr) {
        static int argc = 1;
        static char name[] = "reverblabr-engine-test";
        static char *argv[] = {name, nullptr};
        static QCoreApplication application(argc, argv);
    }
}

/// Where the formats look, for the length of one test: the built-in
/// format's test candidates in a folder of the test's own, LV2 in \a lv2,
/// LADSPA in \a ladspa and CLAP in \a clap (empty folders by default, so
/// the machine's own plugins stay out of the tests that are not about
/// them).
class Folders
{
public:
    explicit Folders(const QString &lv2 = {}, const QString &ladspa = {}, const QString &clap = {})
    {
        EXPECT_TRUE(m_root.isValid());
        QDir(m_root.path()).mkpath(u"tests"_s);
        QDir(m_root.path()).mkpath(u"lv2"_s);
        QDir(m_root.path()).mkpath(u"ladspa"_s);
        QDir(m_root.path()).mkpath(u"clap"_s);
        qputenv("REVERBLABR_TEST_PLUGIN_PATH", testFolder().toUtf8());
        qputenv("LV2_PATH", (lv2.isEmpty() ? m_root.filePath(u"lv2"_s) : lv2).toUtf8());
        qputenv("LADSPA_PATH", (ladspa.isEmpty() ? m_root.filePath(u"ladspa"_s) : ladspa).toUtf8());
        // CLAP searches its own folders as well as CLAP_PATH: those
        // are what is replaced.
        qputenv("CLAP_PATH", "");
        qputenv(plugin::kClapDefaultPathVariable, (clap.isEmpty() ? m_root.filePath(u"clap"_s) : clap).toUtf8());
    }

    ~Folders()
    {
        qunsetenv("REVERBLABR_TEST_PLUGIN_PATH");
        qunsetenv("LV2_PATH");
        qunsetenv("LADSPA_PATH");
        qunsetenv("CLAP_PATH");
        qunsetenv(plugin::kClapDefaultPathVariable);
    }

    Folders(const Folders &) = delete;
    Folders &operator=(const Folders &) = delete;

    [[nodiscard]] QString testFolder() const
    {
        return m_root.filePath(u"tests"_s);
    }

    [[nodiscard]] QString cache() const
    {
        return m_root.filePath(u"cache/plugins.sqlite"_s);
    }

    /// A test candidate called \a name saying \a content.
    void write(const QString &name, const QByteArray &content) const
    {
        QFile file(QDir(testFolder()).filePath(name));
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(content);
    }

private:
    QTemporaryDir m_root;
};

/// Runs a scan to its end.
bool scanned(PluginScanner &scanner, bool retry = false, std::chrono::milliseconds limit = 20s)
{
    scanner.scan(retry);
    QElapsedTimer timer;
    timer.start();
    while (scanner.isScanning()) {
        if (timer.elapsed() > limit.count()) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return true;
}

void configured(PluginScanner &scanner)
{
    scanner.setHostProgram(QString::fromUtf8(RB_PLUGIN_HOST));
    scanner.setWatchingFolders(false);
    scanner.setTimeout(1500ms);
}

[[nodiscard]] QStringList idsOf(const QList<InstalledPlugin> &plugins)
{
    QStringList ids;
    for (const InstalledPlugin &plugin : plugins) {
        ids.append(plugin.format + u':' + plugin.id);
    }
    ids.sort();
    return ids;
}

/// Moves a file's modification time on, the way installing a new version
/// of it does.
void touch(const QString &path)
{
    const auto file = std::filesystem::path{path.toStdString()};
    std::filesystem::last_write_time(file, std::filesystem::last_write_time(file) + 2s);
}

} // namespace

TEST(PluginScannerTest, APluginThatCrashesOrHangsIsBlocklistedAndTheScanCompletes)
{
    needApplication();
    Folders folders;
    folders.write(u"a-good.rbtest"_s, "test.one|One|Somebody\ntest.two|Two|Somebody else\n");
    folders.write(u"b-crash.rbtest"_s, "crash");
    folders.write(u"c-hang.rbtest"_s, "hang");
    folders.write(u"d-unreadable.rbtest"_s, "unreadable");
    PluginScanner scanner(folders.cache());
    configured(scanner);

    ASSERT_TRUE(scanned(scanner)) << "the scan never finished";
    EXPECT_EQ(scanner.lastScanDescribed(), 5) << "the built-in plugins and the four test candidates";
    EXPECT_EQ(idsOf(scanner.plugins()),
              (QStringList{
                  u"builtin:test.gain"_s, u"builtin:test.one"_s, u"builtin:test.slowgain"_s, u"builtin:test.two"_s}));
    const QList<InstalledPlugin> plugins = scanner.plugins();
    const auto one =
        std::ranges::find_if(plugins, [](const InstalledPlugin &plugin) { return plugin.id == u"test.one"_s; });
    ASSERT_NE(one, plugins.end());
    EXPECT_EQ(one->name, u"One"_s);
    EXPECT_EQ(one->vendor, u"Somebody"_s);

    const QList<BlockedCandidate> blocked = scanner.blocked();
    ASSERT_EQ(blocked.size(), 2);
    EXPECT_TRUE(blocked[0].path.endsWith(u"b-crash.rbtest"_s));
    EXPECT_EQ(blocked[0].reason, u"crashed"_s);
    EXPECT_TRUE(blocked[1].path.endsWith(u"c-hang.rbtest"_s));
    EXPECT_EQ(blocked[1].reason, u"hung"_s);
}

// LSP's CLAP file, 194 plugins in a second on an idle machine, took
// over nine under a parallel build, and the limit on the whole of it killed
// it and blocklisted it as hung with nothing found. The limit is on
// progress: a file that goes on telling of plugins is let finish however
// long the whole takes.
TEST(PluginScannerTest, AFileSlowerThanTheLimitIsLetFinishWhileItGoesOnTellingOfPlugins)
{
    needApplication();
    Folders folders;
    QByteArray slow;
    for (int index = 1; index <= 6; ++index) {
        slow +=
            "sleep 400\ntest.slow" + QByteArray::number(index) + "|Slow " + QByteArray::number(index) + "|Somebody\n";
    }
    folders.write(u"slow.rbtest"_s, slow);
    PluginScanner scanner(folders.cache());
    configured(scanner); // 1.5 s; the file takes 2.4 s in all, 0.4 s a plugin.
    ASSERT_TRUE(scanned(scanner));
    const QStringList ids = idsOf(scanner.plugins());
    for (int index = 1; index <= 6; ++index) {
        EXPECT_TRUE(ids.contains(u"builtin:test.slow%1"_s.arg(index))) << ids.join(u' ').toStdString();
    }
    EXPECT_TRUE(scanner.blocked().isEmpty()) << "a slow file was blocklisted as hung";
    EXPECT_TRUE(scanner.incomplete().isEmpty());
}

// A file that tells of some plugins and then stops is not blocklisted
// with nothing: what it told is kept, so is what an earlier scan found in it
// that this one did not reach, it is said to be incomplete, and the next
// scan looks into it again without being asked.
TEST(PluginScannerTest, AFileThatStopsPartOfTheWayKeepsWhatItToldAndWhatWasFoundBefore)
{
    needApplication();
    Folders folders;
    folders.write(u"part.rbtest"_s, "test.a|A|Somebody\ntest.b|B|Somebody\ntest.c|C|Somebody\n");
    PluginScanner scanner(folders.cache());
    configured(scanner);
    ASSERT_TRUE(scanned(scanner));
    ASSERT_TRUE(idsOf(scanner.plugins()).contains(u"builtin:test.c"_s));

    folders.write(u"part.rbtest"_s, "test.a|A, again|Somebody\ntest.b|B|Somebody\nhang\n");
    touch(QDir(folders.testFolder()).filePath(u"part.rbtest"_s));
    ASSERT_TRUE(scanned(scanner));
    const QStringList ids = idsOf(scanner.plugins());
    EXPECT_TRUE(ids.contains(u"builtin:test.a"_s)) << ids.join(u' ').toStdString();
    EXPECT_TRUE(ids.contains(u"builtin:test.b"_s)) << ids.join(u' ').toStdString();
    EXPECT_TRUE(ids.contains(u"builtin:test.c"_s)) << "what the earlier scan found and this one did not reach went";
    const QList<InstalledPlugin> plugins = scanner.plugins();
    const auto a =
        std::ranges::find_if(plugins, [](const InstalledPlugin &plugin) { return plugin.id == u"test.a"_s; });
    ASSERT_NE(a, plugins.end());
    EXPECT_EQ(a->name, u"A, again"_s) << "what this scan told did not replace what was there";
    EXPECT_TRUE(scanner.blocked().isEmpty()) << "a file that told of plugins was blocklisted as hung";
    const QList<BlockedCandidate> incomplete = scanner.incomplete();
    ASSERT_EQ(incomplete.size(), 1);
    EXPECT_TRUE(incomplete[0].path.endsWith(u"part.rbtest"_s));
    const QJsonObject json = pluginsJson(scanner.plugins(), scanner.blocked(), false, scanner.incomplete());
    EXPECT_EQ(json.value("incomplete"_L1).toArray().size(), 1);

    // Not changed, and still looked into: it has not been got through yet.
    ASSERT_TRUE(scanned(scanner));
    EXPECT_EQ(scanner.lastScanDescribed(), 1);
    // Got through: no longer incomplete, and what it no longer has goes.
    folders.write(u"part.rbtest"_s, "test.a|A|Somebody\ntest.b|B|Somebody\n");
    touch(QDir(folders.testFolder()).filePath(u"part.rbtest"_s));
    ASSERT_TRUE(scanned(scanner));
    EXPECT_TRUE(scanner.incomplete().isEmpty());
    EXPECT_FALSE(idsOf(scanner.plugins()).contains(u"builtin:test.c"_s));
}

TEST(PluginScannerTest, ARescanLooksOnlyAtWhatChanged)
{
    needApplication();
    Folders folders;
    folders.write(u"one.rbtest"_s, "test.one|One|Somebody\n");
    folders.write(u"two.rbtest"_s, "test.two|Two|Somebody\n");
    folders.write(u"crash.rbtest"_s, "crash");
    PluginScanner scanner(folders.cache());
    configured(scanner);
    ASSERT_TRUE(scanned(scanner));
    EXPECT_EQ(scanner.lastScanDescribed(), 4);

    // Nothing changed: nothing is looked into, a blocklisted one included.
    ASSERT_TRUE(scanned(scanner));
    EXPECT_EQ(scanner.lastScanDescribed(), 0);

    // One changed, one new, one gone.
    folders.write(u"one.rbtest"_s, "test.one|One, again|Somebody\n");
    touch(QDir(folders.testFolder()).filePath(u"one.rbtest"_s));
    folders.write(u"three.rbtest"_s, "test.three|Three|Somebody\n");
    ASSERT_TRUE(QFile::remove(QDir(folders.testFolder()).filePath(u"two.rbtest"_s)));
    ASSERT_TRUE(scanned(scanner));
    EXPECT_EQ(scanner.lastScanDescribed(), 2);
    EXPECT_EQ(idsOf(scanner.plugins()),
              (QStringList{
                  u"builtin:test.gain"_s, u"builtin:test.one"_s, u"builtin:test.slowgain"_s, u"builtin:test.three"_s}));

    // The blocklisted one is looked at again only when asked.
    ASSERT_TRUE(scanned(scanner, true));
    EXPECT_EQ(scanner.lastScanDescribed(), 1);
    EXPECT_EQ(scanner.blocked().size(), 1);
}

TEST(PluginScannerTest, WhatWasFoundIsThereBeforeTheNextScan)
{
    needApplication();
    Folders folders;
    folders.write(u"one.rbtest"_s, "test.one|One|Somebody\n");
    {
        PluginScanner scanner(folders.cache());
        configured(scanner);
        ASSERT_TRUE(scanned(scanner));
    }
    PluginScanner again(folders.cache());
    configured(again);
    EXPECT_EQ(idsOf(again.plugins()),
              (QStringList{u"builtin:test.gain"_s, u"builtin:test.one"_s, u"builtin:test.slowgain"_s}));
    ASSERT_TRUE(scanned(again));
    EXPECT_EQ(again.lastScanDescribed(), 0);
}

TEST(PluginScannerTest, LadspaLibrariesAreDescribedAndOneThatCrashesIsBlocklisted)
{
    // The plugins tests/ladspa builds, and a library that aborts when asked
    // what is in it.
    needApplication();
    Folders folders({}, QString::fromUtf8(RB_TEST_LADSPA_DIR) + u':' + QString::fromUtf8(RB_TEST_LADSPA_CRASH_DIR));
    PluginScanner scanner(folders.cache());
    configured(scanner);
    ASSERT_TRUE(scanned(scanner)) << "the scan never finished";

    const QList<InstalledPlugin> plugins = scanner.plugins();
    QStringList ladspa;
    for (const InstalledPlugin &plugin : plugins) {
        if (plugin.format == u"ladspa"_s) {
            ladspa.append(plugin.id);
        }
    }
    ladspa.sort();
    EXPECT_EQ(ladspa,
              (QStringList{u"rb-test-ladspa.so:rb_hints"_s,
                           u"rb-test-ladspa.so:rb_mono_gain"_s,
                           u"rb-test-ladspa.so:rb_stereo_gain"_s}));
    const auto stereo = std::ranges::find_if(
        plugins, [](const InstalledPlugin &plugin) { return plugin.id == u"rb-test-ladspa.so:rb_stereo_gain"_s; });
    ASSERT_NE(stereo, plugins.end());
    EXPECT_EQ(stereo->name, u"Test Stereo Gain"_s);
    EXPECT_EQ(stereo->vendor, u"Reverblabr"_s);
    EXPECT_EQ(stereo->audioInputs, 2);
    EXPECT_EQ(stereo->audioOutputs, 2);
    EXPECT_TRUE(stereo->candidate.endsWith(u"rb-test-ladspa.so"_s));
    // Its settings, kept for a block to be built from without loading it.
    const std::optional<plugin::PluginDescription> described =
        scanner.describe(u"ladspa"_s, u"rb-test-ladspa.so:rb_stereo_gain"_s);
    ASSERT_TRUE(described.has_value());
    ASSERT_EQ(described->params.size(), 1U);
    EXPECT_EQ(described->params[0].id, "gain");
    EXPECT_EQ(described->params[0].maximum, 4.0f);
    EXPECT_EQ(described->params[0].defaultValue, 1.0f);

    const QList<BlockedCandidate> blocked = scanner.blocked();
    ASSERT_EQ(blocked.size(), 1);
    EXPECT_TRUE(blocked[0].path.endsWith(u"rb-test-ladspa-crash.so"_s));
    EXPECT_EQ(blocked[0].reason, u"crashed"_s);
}

TEST(PluginScannerTest, LspsLv2PluginsAreFoundWithWhatTheySayOfThemselves)
{
    needApplication();
    const QString bundles = u"/usr/lib64/lv2"_s;
    if (!QFileInfo::exists(bundles + u"/lsp-plugins.lv2"_s)) {
        GTEST_SKIP() << "LSP's LV2 plugins are not installed (lsp-plugins-lv2)";
    }
    Folders folders(bundles);
    PluginScanner scanner(folders.cache());
    configured(scanner);
    scanner.setTimeout(10s);
    QElapsedTimer took;
    took.start();
    ASSERT_TRUE(scanned(scanner, false, 60s));
    std::printf("[measure] %d candidates described in %lld ms, %lld plugins found\n",
                scanner.lastScanDescribed(),
                static_cast<long long>(took.elapsed()),
                static_cast<long long>(scanner.plugins().size()));

    const QList<InstalledPlugin> plugins = scanner.plugins();
    const auto compressor = std::ranges::find_if(plugins, [](const InstalledPlugin &plugin) {
        return plugin.id == u"http://lsp-plug.in/plugins/lv2/compressor_stereo"_s;
    });
    ASSERT_NE(compressor, plugins.end()) << "LSP Compressor Stereo was not found";
    EXPECT_EQ(compressor->format, u"lv2"_s);
    EXPECT_EQ(compressor->name, u"LSP Compressor Stereo"_s);
    EXPECT_EQ(compressor->vendor, u"LSP LV2"_s);
    EXPECT_EQ(compressor->category, u"Compressor Plugin"_s);
    EXPECT_EQ(compressor->audioInputs, 2);
    EXPECT_EQ(compressor->audioOutputs, 2);
    EXPECT_GT(compressor->parameters, 10);
    EXPECT_TRUE(compressor->candidate.endsWith(u"lsp-plugins.lv2"_s));
    const auto mono = std::ranges::find_if(plugins, [](const InstalledPlugin &plugin) {
        return plugin.id == u"http://lsp-plug.in/plugins/lv2/compressor_mono"_s;
    });
    ASSERT_NE(mono, plugins.end());
    EXPECT_EQ(mono->audioInputs, 1);
    EXPECT_TRUE(scanner.blocked().isEmpty());

    ASSERT_TRUE(scanned(scanner));
    EXPECT_EQ(scanner.lastScanDescribed(), 0) << "a scan with nothing changed looked into something";
}

TEST(PluginScannerTest, LspsClapPluginsAreFoundWithWhatTheySayOfThemselves)
{
    needApplication();
    const QString folder = u"/usr/lib64/clap"_s;
    if (plugin::formatNamed("clap") == nullptr) {
        GTEST_SKIP() << "this build has no CLAP SDK (clap-devel)";
    }
    if (!QFileInfo::exists(folder + u"/lsp-plugins.clap"_s)) {
        GTEST_SKIP() << "LSP's CLAP plugins are not installed (lsp-plugins-clap)";
    }
    Folders folders({}, {}, folder);
    PluginScanner scanner(folders.cache());
    configured(scanner);
    scanner.setTimeout(10s);
    QElapsedTimer took;
    took.start();
    ASSERT_TRUE(scanned(scanner, false, 60s));
    std::printf("[measure] %d candidates described in %lld ms, %lld plugins found\n",
                scanner.lastScanDescribed(),
                static_cast<long long>(took.elapsed()),
                static_cast<long long>(scanner.plugins().size()));

    const QList<InstalledPlugin> plugins = scanner.plugins();
    const auto compressor = std::ranges::find_if(
        plugins, [](const InstalledPlugin &plugin) { return plugin.id == u"in.lsp-plug.compressor_stereo"_s; });
    ASSERT_NE(compressor, plugins.end()) << "LSP Compressor Stereo was not found";
    EXPECT_EQ(compressor->format, u"clap"_s);
    EXPECT_EQ(compressor->name, u"Compressor Stereo"_s);
    EXPECT_EQ(compressor->vendor, u"LSP CLAP"_s);
    EXPECT_EQ(compressor->category, u"Compressor"_s);
    EXPECT_EQ(compressor->audioInputs, 2);
    EXPECT_EQ(compressor->audioOutputs, 2);
    EXPECT_GT(compressor->parameters, 10);
    EXPECT_TRUE(compressor->candidate.endsWith(u"lsp-plugins.clap"_s));
    const auto mono = std::ranges::find_if(
        plugins, [](const InstalledPlugin &plugin) { return plugin.id == u"in.lsp-plug.compressor_mono"_s; });
    ASSERT_NE(mono, plugins.end());
    EXPECT_EQ(mono->audioInputs, 1);
    EXPECT_TRUE(scanner.blocked().isEmpty());

    // What a host is told to load: the file the scan found it in.
    const std::optional<plugin::PluginDescription> described =
        scanner.describe(u"clap"_s, u"in.lsp-plug.compressor_stereo"_s);
    ASSERT_TRUE(described.has_value());
    EXPECT_EQ(described->location, compressor->candidate.toStdString());
    // A choice LSP steps through fractions of 0 to 1 is found as a choice.
    const auto mode = std::ranges::find_if(
        described->params, [](const plugin::PluginParam &param) { return param.name == "Sidechain mode"; });
    ASSERT_NE(mode, described->params.end());
    EXPECT_EQ(mode->kind, plugin::PluginParam::Kind::Choice);
    std::vector<std::string> labels;
    for (const auto &[value, label] : mode->choices) {
        labels.push_back(label);
    }
    EXPECT_EQ(labels, (std::vector<std::string>{"Peak", "RMS", "LPF", "SMA"}));

    ASSERT_TRUE(scanned(scanner));
    EXPECT_EQ(scanner.lastScanDescribed(), 0) << "a scan with nothing changed looked into something";
}

// The engine: asked for its plugins, it says what its cache holds and
// scans for what changed, the first time only, and says it again when the
// scan ends — with the real host describing, into the test's own cache.
TEST(PluginScannerTest, TheEngineListsItsPluginsAndScansTheFirstTimeItIsAsked)
{
    needApplication();
    Folders folders;
    folders.write(u"one.rbtest"_s, "test.one|One|Somebody\n");
    qputenv("REVERBLABR_PLUGIN_HOST", RB_PLUGIN_HOST);
    dsp::BlockRegistry registry;
    ASSERT_TRUE(blocks::registerBuiltinBlocks(registry));
    QTemporaryDir sessions;
    Engine engine(registry, SessionStore{sessions.path()});
    QSignalSpy changed(&engine, &EngineFacade::pluginsChanged);

    engine.listPlugins();
    ASSERT_GE(changed.count(), 1);
    const auto last = [&changed] { return QJsonDocument::fromJson(changed.back().at(0).toString().toUtf8()).object(); };
    QElapsedTimer waited;
    waited.start();
    while (last().value("scanning"_L1).toBool() || changed.count() < 2) {
        ASSERT_LT(waited.elapsed(), 20000) << "the scan never ended";
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    QStringList ids;
    for (const QJsonValue &plugin : last().value("plugins"_L1).toArray()) {
        ids.append(plugin.toObject().value("id"_L1).toString());
    }
    EXPECT_TRUE(ids.contains(u"test.gain"_s)) << ids.join(u' ').toStdString();
    EXPECT_TRUE(ids.contains(u"test.one"_s)) << ids.join(u' ').toStdString();

    // The second time, what it has; no scan.
    changed.clear();
    engine.listPlugins();
    ASSERT_EQ(changed.count(), 1);
    EXPECT_FALSE(last().value("scanning"_L1).toBool());
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    EXPECT_EQ(changed.count(), 1);
    qunsetenv("REVERBLABR_PLUGIN_HOST");
}
