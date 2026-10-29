// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/mockengine.h"
#include "diagnostics.h"

#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSysInfo>
#include <gtest/gtest.h>

using namespace rb;
using namespace rb::ui;
using namespace Qt::StringLiterals;

namespace
{

void needApplication()
{
    if (QCoreApplication::instance() == nullptr) {
        static int argc = 1;
        static char name[] = "reverblabr-app-test";
        static char *argv[] = {name, nullptr};
        static QGuiApplication application(argc, argv);
    }
}

/// **No test here writes to a clipboard**: copyBundle() hands its text to
/// this, and it is put back however the test ends.
struct CapturedClipboard {
    QString text;
    int copies = 0;
    CapturedClipboard()
    {
        Diagnostics::setClipboardSinkForTesting([this](const QString &copied) {
            text = copied;
            ++copies;
        });
    }
    ~CapturedClipboard()
    {
        Diagnostics::setClipboardSinkForTesting({});
    }
};

/// An answer as the engine gives one: a running session that is not
/// real-time and was late, a stopped one, one link, a missing format, and a
/// plugin left out.
[[nodiscard]] QJsonObject answer()
{
    return QJsonObject{
        {"version"_L1, 1},
        {"engine"_L1,
         QJsonObject{{"process"_L1, u"reverblabrd"_s},
                     {"version"_L1, u"0.9.1+3.gabc1234"_s},
                     {"pid"_L1, 4242},
                     {"pipeWireHeaders"_L1, u"1.6.0"_s},
                     {"pipeWireLibrary"_L1, u"1.6.9"_s}}},
        {"audioSystem"_L1,
         QJsonObject{{"state"_L1, u"connected"_s},
                     {"server"_L1, u"PipeWire"_s},
                     {"serverVersion"_L1, u"1.6.9"_s},
                     {"sessionManager"_L1, u"WirePlumber"_s},
                     {"sessionManagerVersion"_L1, u"0.5.12"_s},
                     {"driver"_L1, u"Built-in Audio"_s},
                     {"rate"_L1, 48000},
                     {"quantum"_L1, 1024}}},
        {"sessions"_L1,
         QJsonArray{
             QJsonObject{
                 {"id"_L1, u"s1"_s},
                 {"name"_L1, u"Morning call"_s},
                 {"state"_L1, u"running"_s},
                 {"running"_L1, true},
                 {"dspLoad"_L1, 0.12},
                 {"peakDspLoad"_L1, 0.3},
                 {"xruns"_L1, 3},
                 {"lostCycles"_L1, 4},
                 {"rate"_L1, 48000},
                 {"quantum"_L1, 256},
                 {"realtime"_L1, false},
                 {"longRunsNotRealtime"_L1, 2},
                 {"xrunHistory"_L1,
                  QJsonArray{QJsonObject{{"at"_L1, u"2026-09-28T10:00:00.000"_s}, {"total"_L1, 3}, {"added"_L1, 3}}}}},
             QJsonObject{{"id"_L1, u"s2"_s},
                         {"name"_L1, u"Podcast"_s},
                         {"state"_L1, u"idle"_s},
                         {"running"_L1, false},
                         {"xrunHistory"_L1, QJsonArray{}}},
         }},
        {"graph"_L1,
         QJsonObject{
             {"nodes"_L1, QJsonArray{QJsonObject{{"id"_L1, 40}, {"name"_L1, u"reverblabr.s1.seg0"_s}}}},
             {"ports"_L1, QJsonArray{}},
             {"links"_L1,
              QJsonArray{QJsonObject{{"from"_L1, u"Firefox:output_FL"_s}, {"to"_L1, u"reverblabr.s1.seg0:in_FL"_s}}}}}},
        {"recordingFormats"_L1,
         QJsonArray{QJsonObject{{"id"_L1, u"wav"_s}, {"available"_L1, true}},
                    QJsonObject{{"id"_L1, u"aac"_s}, {"available"_L1, false}, {"reason"_L1, u"encoderMissing"_s}}}},
        {"plugins"_L1,
         QJsonObject{
             {"scanning"_L1, false},
             {"formats"_L1, QJsonObject{{"lv2"_L1, 5}}},
             {"total"_L1, 5},
             {"blocked"_L1,
              QJsonArray{QJsonObject{
                  {"format"_L1, u"lv2"_s}, {"path"_L1, u"/usr/lib64/lv2/broken.lv2"_s}, {"reason"_L1, u"crashed"_s}}}},
             {"incomplete"_L1,
              QJsonArray{QJsonObject{{"format"_L1, u"clap"_s}, {"path"_L1, u"/usr/lib64/clap/many.clap"_s}}}}}},
        {"log"_L1, QJsonArray{u"2026-09-28T10:00:00.000 warning reverblabr.pw: the device went away"_s}},
    };
}

} // namespace

TEST(DiagnosticsBundleTest, SaysWhatABugReportNeeds)
{
    const QString text = Diagnostics::bundleText(answer(), {}, u"0.9.0"_s, u"2026-09-28T10:05:00Z"_s);
    for (const QString &expected : {
             u"Reverblabr diagnostics"_s,
             u"Generated: 2026-09-28T10:05:00Z"_s,
             u"Window: reverblabr 0.9.0"_s,
             u"Engine: reverblabrd 0.9.1+3.gabc1234 (pid 4242)"_s,
             u"PipeWire library: 1.6.9, built with 1.6.0"_s,
             u"Server: PipeWire 1.6.9"_s,
             u"Session manager: WirePlumber 0.5.12"_s,
             u"Rate: 48000 Hz, quantum 1024"_s,
             u"Morning call [s1]: running, DSP load 12% (peak 30%), xruns 3, lost cycles 4, 48000 Hz, quantum 256, "
             u"real-time NO, 2 long runs without real-time"_s,
             u"  late at 2026-09-28T10:00:00.000: +3 (total 3)"_s,
             u"Podcast [s2]: idle"_s,
             u"wav: yes"_s,
             u"aac: no (encoderMissing)"_s,
             u"Found: 5 (lv2 5)"_s,
             u"Blocked: lv2 /usr/lib64/lv2/broken.lv2 (crashed)"_s,
             u"Incomplete: clap /usr/lib64/clap/many.clap"_s,
             u"\"from\": \"Firefox:output_FL\""_s,
             u"== Engine log (last 1 lines) =="_s,
             u"warning reverblabr.pw: the device went away"_s,
         }) {
        EXPECT_TRUE(text.contains(expected)) << "missing: " << expected.toStdString() << "\n" << text.toStdString();
    }
    EXPECT_FALSE(text.contains(u"Window log"_s)) << "no window lines were given";
}

TEST(DiagnosticsBundleTest, TheWindowsOwnLinesComeAfterTheEnginesWhenTheyAreSeparate)
{
    const QString text = Diagnostics::bundleText(
        answer(), {u"window line one"_s, u"window line two"_s}, u"0.9.0"_s, u"2026-09-28T10:05:00Z"_s);
    const qsizetype engineLog = text.indexOf(u"== Engine log"_s);
    const qsizetype windowLog = text.indexOf(u"== Window log (last 2 lines) =="_s);
    ASSERT_GE(engineLog, 0);
    ASSERT_GT(windowLog, engineLog);
    EXPECT_GT(text.indexOf(u"window line two"_s), windowLog);
}

TEST(DiagnosticsBundleTest, TheHomeFolderUserAndMachineAreTakenOut)
{
    const QString text =
        u"Blocked: lv2 /home/alice/.lv2/odd.lv2 (crashed)\n"
        u"warning: alice's session on workbench-7 could not start\n"
        u"application.process.user = alice; host workbench-7\n"
        u"a plugin by alicecorp and a node called workbench-70 stay\n"_s;
    const QString redacted = Diagnostics::redact(text, u"/home/alice"_s, u"alice"_s, u"workbench-7"_s);
    EXPECT_FALSE(redacted.contains(u"/home/alice"_s)) << redacted.toStdString();
    EXPECT_TRUE(redacted.contains(u"~/.lv2/odd.lv2"_s)) << redacted.toStdString();
    EXPECT_TRUE(redacted.contains(u"<user>'s session on <host>"_s)) << redacted.toStdString();
    EXPECT_TRUE(redacted.contains(u"application.process.user = <user>; host <host>"_s)) << redacted.toStdString();
    // **Whole words only**: a name that happens to start with the user's is
    // somebody else's.
    EXPECT_TRUE(redacted.contains(u"alicecorp"_s)) << redacted.toStdString();
    EXPECT_TRUE(redacted.contains(u"workbench-70"_s)) << redacted.toStdString();
    EXPECT_EQ(Diagnostics::redact(text, QString(), QString(), QString()), text) << "nothing to take out";
}

TEST(DiagnosticsModelTest, FollowsTheEnginesAnswerAndCopiesARedactedBundle)
{
    needApplication();
    const CapturedClipboard clipboard;
    core::MockEngine engine;
    Diagnostics diagnostics;
    diagnostics.attach(&engine);
    EXPECT_FALSE(diagnostics.isReady()) << "nothing is asked until the page asks";

    QJsonObject fake = answer();
    // A line naming this machine's own home folder, as a real path in a log
    // would: it must not reach the clipboard.
    fake.insert("log"_L1, QJsonArray{QString(u"could not open "_s + QDir::homePath() + u"/Music/take.wav"_s)});
    engine.setFakeDiagnostics(QString::fromUtf8(QJsonDocument(fake).toJson(QJsonDocument::Compact)));
    diagnostics.refresh();

    ASSERT_TRUE(diagnostics.isReady());
    ASSERT_EQ(diagnostics.sessions().size(), 2);
    EXPECT_EQ(diagnostics.sessions().at(0).toMap().value(u"realtime"_s), QVariant(false));
    EXPECT_EQ(diagnostics.links().size(), 1);
    EXPECT_EQ(diagnostics.nodes().size(), 1);
    EXPECT_EQ(diagnostics.recordingFormats().size(), 2);
    EXPECT_EQ(diagnostics.plugins().value(u"total"_s).toInt(), 5);
    EXPECT_EQ(diagnostics.logLines(), 1);

    diagnostics.copyBundle();
    ASSERT_EQ(clipboard.copies, 1);
    EXPECT_EQ(clipboard.text, diagnostics.bundle());
    EXPECT_TRUE(clipboard.text.contains(u"could not open ~/Music/take.wav"_s)) << clipboard.text.toStdString();
    EXPECT_FALSE(clipboard.text.contains(QDir::homePath())) << "the home folder reached the clipboard";
}
