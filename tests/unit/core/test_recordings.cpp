// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What the engine makes of a Recorder's settings, and where it keeps
// recordings and their journal.

#include "blocks/builtinblocks.h"
#include "blocks/recorder.h"
#include "core/blockcatalogue.h"
#include "core/engine.h"
#include "core/recordings.h"
#include "core/sessionstore.h"
#include "dsp/blockregistry.h"
#include "record/audiofile.h"
#include "record/repair.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <array>
#include <fstream>

using namespace rb;
using namespace rb::core;
using namespace Qt::StringLiterals;

namespace
{

[[nodiscard]] std::filesystem::path toPath(const QString &path)
{
    return std::filesystem::path{path.toStdString()};
}

[[nodiscard]] std::uint32_t riffSize(const QString &path)
{
    std::ifstream file(toPath(path), std::ios::binary);
    std::array<unsigned char, 8> head{};
    file.read(reinterpret_cast<char *>(head.data()), head.size());
    return static_cast<std::uint32_t>(head[4]) | static_cast<std::uint32_t>(head[5]) << 8
        | static_cast<std::uint32_t>(head[6]) << 16 | static_cast<std::uint32_t>(head[7]) << 24;
}

} // namespace

TEST(Recordings, WithNoFolderOfItsOwnARecorderWritesToReverblabrInTheMusicFolder)
{
    const QString music = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    EXPECT_EQ(defaultRecordingFolder(), QDir(music.isEmpty() ? QDir::homePath() : music).filePath(u"Reverblabr"_s));
}

TEST(Recordings, TheJournalIsKeptWithTheUsersStateNotTheirDocuments)
{
    const QString state = qEnvironmentVariable("XDG_STATE_HOME");
    ASSERT_FALSE(state.isEmpty()) << "the tests are meant to have a state folder of their own";
    EXPECT_EQ(recordingJournalFolder(), QDir(state).filePath(u"reverblabr/recordings"_s));
}

TEST(Recordings, AFileIsMadeFromWhatTheBlockIsSetTo)
{
    blocks::Recorder block;
    record::RecordingSettings settings = recordingSettings(block, u"Podcast"_s, u"rec"_s);
    // Out of the box: a 256 kb/s variable-rate stereo MP3, 24 bits for
    // a lossless format, named for the session and the time, in
    // the default folder.
    EXPECT_EQ(settings.format, record::FileFormat::Mp3);
    EXPECT_EQ(settings.bitrateKbps, 256U);
    EXPECT_TRUE(settings.variableBitrate);
    EXPECT_FALSE(settings.mono);
    EXPECT_EQ(settings.sampleFormat, record::SampleFormat::Int24);
    EXPECT_EQ(settings.folder, toPath(defaultRecordingFolder()));
    EXPECT_EQ(settings.fileNamePattern, "%session %date %time");
    EXPECT_EQ(settings.sessionName, "Podcast");
    EXPECT_EQ(settings.blockName, "rec");

    ASSERT_TRUE(block.setParam("format", dsp::ParamValue::ofEnum("flac")).has_value());
    ASSERT_TRUE(block.setParam("sampleFormat", dsp::ParamValue::ofEnum("int16")).has_value());
    ASSERT_TRUE(block.setParam("folder", dsp::ParamValue::ofString("~/Podcasts")).has_value());
    ASSERT_TRUE(block.setParam("fileName", dsp::ParamValue::ofString("%session %counter")).has_value());
    settings = recordingSettings(block, u"Podcast"_s, u"rec"_s);
    EXPECT_EQ(settings.format, record::FileFormat::Flac);
    EXPECT_EQ(settings.sampleFormat, record::SampleFormat::Int16);
    EXPECT_EQ(settings.folder, toPath(QDir::homePath() + u"/Podcasts"_s));
    EXPECT_EQ(settings.fileNamePattern, "%session %counter");

    // The session's rate unless a rate is named.
    EXPECT_EQ(settings.fileRate, 0U);
    ASSERT_TRUE(block.setParam("sampleRate", dsp::ParamValue::ofEnum("96000")).has_value());
    EXPECT_EQ(recordingSettings(block, u"Podcast"_s, u"rec"_s).fileRate, 96000U);

    // A pattern rubbed out is the default again, not a file with no name.
    ASSERT_TRUE(block.setParam("fileName", dsp::ParamValue::ofString("  ")).has_value());
    EXPECT_EQ(recordingSettings(block, u"Podcast"_s, u"rec"_s).fileNamePattern, "%session %date %time");
}

// **On the next start**: an engine that is made finds the recordings a crash
// left behind in its journal and gives them back their length.
TEST(Recordings, TheEngineRepairsWhatACrashLeftBehindWhenItStarts)
{
    QTemporaryDir folder;
    ASSERT_TRUE(folder.isValid());
    // **A journal of its own.** Every engine that is made repairs what the
    // journal names, and other test processes are making engines at the
    // same moment; with the shared one, one of them could take this test's
    // entry half-written, or be half-way through repairing its file when
    // this test looks.
    QTemporaryDir state;
    ASSERT_TRUE(state.isValid());
    const QByteArray sharedState = qgetenv("XDG_STATE_HOME");
    qputenv("XDG_STATE_HOME", state.path().toUtf8());
    const auto restore = qScopeGuard([&sharedState] { qputenv("XDG_STATE_HOME", sharedState); });
    ASSERT_TRUE(recordingJournalFolder().startsWith(state.path()));
    const QString path = folder.filePath(u"cut off.wav"_s);
    {
        std::expected<record::AudioFileWriter, record::FileError> writer =
            record::AudioFileWriter::create(toPath(path), record::FileSpec{});
        ASSERT_TRUE(writer.has_value());
        const std::vector<float> samples(48000 * 2, 0.25f);
        ASSERT_TRUE(writer->write(samples).has_value());
        ASSERT_TRUE(writer->close().has_value());
    }
    const std::uint32_t whole = riffSize(path);
    // What the header said before anything was written, which is what a
    // file killed before its first update still says.
    {
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::ReadWrite));
        file.seek(4);
        const std::array<char, 4> small{36, 0, 0, 0};
        file.write(small.data(), small.size());
    }
    ASSERT_EQ(riffSize(path), 36U);
    // In the journal, as a process that has gone.
    QDir().mkpath(recordingJournalFolder());
    {
        QFile entry(QDir(recordingJournalFolder()).filePath(u"999999998-0-0.rec"_s));
        ASSERT_TRUE(entry.open(QIODevice::WriteOnly));
        entry.write(path.toUtf8());
    }

    QTemporaryDir sessions;
    dsp::BlockRegistry registry;
    ASSERT_TRUE(blocks::registerBuiltinBlocks(registry));
    const Engine engine(registry, SessionStore{sessions.path()});
    EXPECT_EQ(riffSize(path), whole);
    EXPECT_FALSE(QFile::exists(QDir(recordingJournalFolder()).filePath(u"999999998-0-0.rec"_s)));
}

namespace
{

[[nodiscard]] Session lanesInto(const QStringList &sources, const QString &recorder)
{
    Session session;
    for (const QString &source : sources) {
        for (std::uint32_t channel = 0; channel < 2; ++channel) {
            session.wires.append(Wire{source, channel, u"mix"_s, channel});
        }
    }
    for (std::uint32_t channel = 0; channel < 2; ++channel) {
        session.wires.append(Wire{u"mix"_s, channel, recorder, channel});
    }
    return session;
}

} // namespace

// **Which lanes a Recorder's pauses are read from**: the sources
// upstream of it, if every one of them leaves its pauses out.
TEST(Recordings, ARecordersPausesAreReadFromItsApplicationsLanesWhenAllOfThemLeavePausesOut)
{
    const QStringList lanes{u"mic"_s, u"app1"_s, u"app2"_s};
    const auto leaves = [](const QString &block) { return block.startsWith("app"_L1); };

    // Two applications through a mixer: pauses when both are away.
    EXPECT_EQ(recordingGapLanes(lanesInto({u"app1"_s, u"app2"_s}, u"rec"_s), u"rec"_s, lanes, leaves), 0b110U);
    // A microphone beside them goes on, and its silence is silence.
    EXPECT_EQ(recordingGapLanes(lanesInto({u"app1"_s, u"mic"_s}, u"rec"_s), u"rec"_s, lanes, leaves), 0U);
    // An application that keeps its pauses as silence is the same.
    const auto keeps = [](const QString &block) { return block == "app1"_L1; };
    EXPECT_EQ(recordingGapLanes(lanesInto({u"app1"_s, u"app2"_s}, u"rec"_s), u"rec"_s, lanes, keeps), 0U);
    // Nothing feeding it: nothing to read a pause from.
    EXPECT_EQ(recordingGapLanes(Session{}, u"rec"_s, lanes, leaves), 0U);
    // A source with no lane of its own — a Wave — is a sound that goes on.
    EXPECT_EQ(
        recordingGapLanes(lanesInto({u"wave"_s}, u"rec"_s), u"rec"_s, lanes, [](const QString &) { return true; }), 0U);
}

// The File Limits settings, each only when it is switched on.
TEST(Recordings, TheFileLimitsAreReadOffTheBlockWhenTheyAreSwitchedOn)
{
    blocks::Recorder block;
    record::FileLimits limits = recordingSettings(block, u"P"_s, u"r"_s).limits;
    EXPECT_EQ(limits.splitEverySeconds, 0.0);
    EXPECT_EQ(limits.splitEveryBytes, 0U);
    EXPECT_EQ(limits.endAfterSeconds, 0.0);
    EXPECT_EQ(limits.silence, record::FileLimits::Silence::Keep);

    const auto set = [&block](const char *id, dsp::ParamValue value) {
        ASSERT_TRUE(block.setParam(id, value).has_value()) << id;
    };
    set("splitEvery", dsp::ParamValue::ofInt(5));
    set("endAfter", dsp::ParamValue::ofInt(2));
    set("silenceSeconds", dsp::ParamValue::ofFloat(1.5f));
    // Numbers without their switches are kept, and do nothing.
    EXPECT_EQ(recordingSettings(block, u"P"_s, u"r"_s).limits.splitEverySeconds, 0.0);

    set("splitEnabled", dsp::ParamValue::ofBool(true));
    set("endEnabled", dsp::ParamValue::ofBool(true));
    set("silenceEnabled", dsp::ParamValue::ofBool(true));
    set("silenceAction", dsp::ParamValue::ofEnum("split"));
    set("silenceThreshold", dsp::ParamValue::ofFloat(-50.0f));
    limits = recordingSettings(block, u"P"_s, u"r"_s).limits;
    EXPECT_EQ(limits.splitEverySeconds, 5.0 * 60.0);
    EXPECT_EQ(limits.endAfterSeconds, 2.0 * 3600.0);
    EXPECT_EQ(limits.silence, record::FileLimits::Silence::Split);
    EXPECT_FLOAT_EQ(static_cast<float>(limits.silenceSeconds), 1.5f);
    EXPECT_EQ(limits.silenceThresholdDb, -50.0f);

    set("splitUnit", dsp::ParamValue::ofEnum("megabytes"));
    set("endUnit", dsp::ParamValue::ofEnum("minutes"));
    set("silenceAction", dsp::ParamValue::ofEnum("stop"));
    limits = recordingSettings(block, u"P"_s, u"r"_s).limits;
    EXPECT_EQ(limits.splitEverySeconds, 0.0);
    EXPECT_EQ(limits.splitEveryBytes, 5U * 1024U * 1024U);
    EXPECT_EQ(limits.endAfterSeconds, 2.0 * 60.0);
    EXPECT_EQ(limits.silence, record::FileLimits::Silence::Stop);
}

// Tags: read off the block as patterns, for the writer to fill in.
TEST(Recordings, TheTagsAreReadOffTheBlockAsPatterns)
{
    blocks::Recorder block;
    EXPECT_TRUE(recordingSettings(block, u"P"_s, u"r"_s).tags.isEmpty());
    ASSERT_TRUE(block.setParam("tagTitle", dsp::ParamValue::ofString("%session, part %counter")).has_value());
    ASSERT_TRUE(block.setParam("tagArtist", dsp::ParamValue::ofString("李明")).has_value());
    ASSERT_TRUE(block.setParam("tagTrack", dsp::ParamValue::ofString("%counter")).has_value());
    ASSERT_TRUE(block.setParam("tagDiscCount", dsp::ParamValue::ofString("2")).has_value());
    const record::Tags tags = recordingSettings(block, u"P"_s, u"r"_s).tags;
    EXPECT_EQ(tags.title, "%session, part %counter");
    EXPECT_EQ(tags.artist, "李明");
    EXPECT_EQ(tags.track, "%counter");
    EXPECT_EQ(tags.discCount, "2");
    EXPECT_TRUE(tags.album.empty());
}

// **Only the formats this system can write are offered**: with no
// M4A encoders the catalogue leaves AAC and ALAC out of the Recorder's
// formats and says why; with them, they are offered like the rest.
TEST(Recordings, TheCatalogueOffersOnlyTheFormatsThisSystemCanWrite)
{
    dsp::BlockRegistry registry;
    ASSERT_TRUE(blocks::registerBuiltinBlocks(registry));
    const auto formatOf = [&registry](const record::EncoderProbe &probe) {
        const QJsonObject catalogue = blockCatalogueJson(registry, recordingUnavailableChoices(probe));
        for (const QJsonValue &block : catalogue.value("blocks"_L1).toArray()) {
            if (block.toObject().value("type"_L1).toString() != "output.recorder"_L1) {
                continue;
            }
            for (const QJsonValue &param : block.toObject().value("params"_L1).toArray()) {
                if (param.toObject().value("id"_L1).toString() == "format"_L1) {
                    return param.toObject();
                }
            }
        }
        return QJsonObject{};
    };

    const QJsonObject without = formatOf(record::EncoderProbe{});
    const QJsonArray offered = without.value("choices"_L1).toArray();
    EXPECT_FALSE(offered.contains(u"aac"_s));
    EXPECT_FALSE(offered.contains(u"alac"_s));
    EXPECT_TRUE(offered.contains(u"flac"_s) && offered.contains(u"opus"_s));
    const QJsonArray missing = without.value("unavailableChoices"_L1).toArray();
    ASSERT_EQ(missing.size(), 2);
    EXPECT_EQ(missing.at(0).toObject().value("id"_L1).toString(), u"aac"_s);
    EXPECT_EQ(missing.at(0).toObject().value("reason"_L1).toString(), u"encoderMissing"_s);
    EXPECT_EQ(missing.at(1).toObject().value("id"_L1).toString(), u"alac"_s);

    const QJsonObject with = formatOf(record::EncoderProbe{true, true});
    EXPECT_TRUE(with.value("choices"_L1).toArray().contains(u"aac"_s));
    EXPECT_TRUE(with.value("choices"_L1).toArray().contains(u"alac"_s));
    EXPECT_FALSE(with.contains("unavailableChoices"_L1));
}
