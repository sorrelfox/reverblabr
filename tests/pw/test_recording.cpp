// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Recording a live session, through the engine's own interface and
// against a real PipeWire: two Recorders on one tone, one of them switched
// off and on again while the session runs, and every file read back.

#include "blocks/applicationsource.h"
#include "blocks/builtinblocks.h"
#include "blocks/outputdevice.h"
#include "blocks/recorder.h"
#include "blocks/wave.h"
#include "core/editcommand.h"
#include "core/engine.h"
#include "core/session.h"
#include "core/sessionruntime.h"
#include "dsp/blockregistry.h"
#include "sessionharness.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <gtest/gtest.h>
#include <sndfile.h>

#include <chrono>
#include <cmath>
#include <iostream>
#include <numbers>
#include <optional>
#include <vector>

using namespace rb;
using namespace rb::core;
using namespace rb::pwtest;
using namespace Qt::StringLiterals;
using namespace std::chrono_literals;

namespace
{

[[nodiscard]] QString asJson(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

[[nodiscard]] QString typeName(std::string_view type)
{
    return QString::fromUtf8(type.data(), static_cast<qsizetype>(type.size()));
}

[[nodiscard]] QString addBlock(const QString &id, std::string_view type, const QJsonObject &params)
{
    BlockInstance block;
    block.id = id;
    block.type = typeName(type);
    block.params = params;
    AddBlock command;
    command.block = block;
    return asJson(editCommandToJson(command));
}

[[nodiscard]] QString connect(const QString &from, const QString &to, std::uint32_t channel)
{
    Wire wire;
    wire.fromBlock = from;
    wire.fromPort = channel;
    wire.toBlock = to;
    wire.toPort = channel;
    return asJson(editCommandToJson(Connect{wire, -1}));
}

/// A file as libsndfile reads it: the rate, and the left channel.
struct Heard {
    int sampleRate = 0;
    std::int64_t frames = 0;
    std::vector<float> left;
};

[[nodiscard]] Heard hear(const QString &path)
{
    Heard heard;
    SF_INFO info{};
    SNDFILE *file = sf_open(path.toUtf8().constData(), SFM_READ, &info);
    if (file == nullptr) {
        return heard;
    }
    heard.sampleRate = info.samplerate;
    heard.frames = info.frames;
    std::vector<float> buffer(4096 * static_cast<std::size_t>(info.channels));
    for (;;) {
        const sf_count_t got = sf_readf_float(file, buffer.data(), 4096);
        if (got <= 0) {
            break;
        }
        for (sf_count_t frame = 0; frame < got; ++frame) {
            heard.left.push_back(buffer[static_cast<std::size_t>(frame * info.channels)]);
        }
    }
    sf_close(file);
    return heard;
}

/// The biggest step from one sample to the next. **A cycle lost on the way
/// to the file is a jump in the sine's phase**, which is a step no sine of
/// this frequency and level ever takes.
[[nodiscard]] float biggestStep(const std::vector<float> &samples, std::size_t from)
{
    float biggest = 0.0f;
    for (std::size_t index = from + 1; index < samples.size(); ++index) {
        biggest = std::max(biggest, std::fabs(samples[index] - samples[index - 1]));
    }
    return biggest;
}

} // namespace

using Recording = SessionTest;

TEST_F(Recording, ALiveSessionIsRecordedWholeByEveryRecorderOnIt)
{
    CaptureNodeOptions deviceOptions = captureOptions("rbtest.recording.out");
    deviceOptions.mediaClass = "Audio/Sink";
    CaptureNode device(deviceOptions);
    ASSERT_TRUE(device.waitUntilReady().has_value());

    QTemporaryDir sessions;
    QTemporaryDir recordings;
    ASSERT_TRUE(sessions.isValid() && recordings.isValid());
    dsp::BlockRegistry registry;
    ASSERT_TRUE(blocks::registerBuiltinBlocks(registry));
    Engine engine(registry, SessionStore{sessions.path()});
    QSignalSpy opened(&engine, &EngineFacade::sessionOpened);
    QSignalSpy started(&engine, &EngineFacade::recordingFileStarted);
    QSignalSpy finished(&engine, &EngineFacade::recordingFileFinished);
    QSignalSpy errors(&engine, &EngineFacade::error);
    const auto said = [&errors] {
        std::string text;
        for (const QList<QVariant> &error : errors) {
            text += error.at(0).toString().toStdString() + " " + error.at(1).toString().toStdString() + "; ";
        }
        return text;
    };

    engine.connectToPipeWire();
    Waiter pump;
    ASSERT_TRUE(pump.pumpUntil([&] { return engine.isConnected(); }));
    engine.createSession(u"Take"_s);
    ASSERT_EQ(opened.count(), 1);
    const QString id = opened.back().at(0).toString();

    // A tone, through a WAV Recorder to the device, and a FLAC Recorder
    // hanging off the tone as well with nothing after it.
    constexpr double kHz = 1000.0;
    constexpr double kLevelDb = -12.0;
    engine.applyEdit(id,
                     addBlock(u"wave"_s,
                              blocks::Wave::kTypeId,
                              {{"waveform"_L1, u"sine"_s}, {"frequency"_L1, kHz}, {"level"_L1, kLevelDb}}));
    const QJsonObject where{{"folder"_L1, recordings.path()}, {"fileName"_L1, u"%session %block"_s}};
    QJsonObject asWav = where;
    asWav.insert("format"_L1, u"wav"_s);
    asWav.insert("sampleFormat"_L1, u"float32"_s);
    QJsonObject asFlac = where;
    asFlac.insert("format"_L1, u"flac"_s);
    engine.applyEdit(id, addBlock(u"wav"_s, blocks::Recorder::kTypeId, asWav));
    engine.applyEdit(id, addBlock(u"flac"_s, blocks::Recorder::kTypeId, asFlac));
    engine.applyEdit(id, addBlock(u"out"_s, blocks::OutputDevice::kTypeId, {{"device"_L1, u"rbtest.recording.out"_s}}));
    for (std::uint32_t channel = 0; channel < 2; ++channel) {
        engine.applyEdit(id, connect(u"wave"_s, u"wav"_s, channel));
        engine.applyEdit(id, connect(u"wav"_s, u"out"_s, channel));
        engine.applyEdit(id, connect(u"wave"_s, u"flac"_s, channel));
    }
    ASSERT_TRUE(errors.isEmpty()) << said();

    engine.start(id);
    ASSERT_TRUE(errors.isEmpty()) << said();
    ASSERT_TRUE(pump.pumpUntil([&] { return started.count() == 2; })) << "the files were never started; " << said();
    pump.pump(1000ms);

    // Off and on again while it runs: its first file is finished there and
    // then, and the second is a file of its own beside it.
    engine.applyEdit(id, asJson(editCommandToJson(SetEnabled{u"flac"_s, false})));
    ASSERT_TRUE(pump.pumpUntil([&] { return finished.count() == 1; })) << said();
    EXPECT_EQ(finished.back().at(1).toString(), u"flac"_s);
    pump.pump(300ms);
    engine.applyEdit(id, asJson(editCommandToJson(SetEnabled{u"flac"_s, true})));
    ASSERT_TRUE(pump.pumpUntil([&] { return started.count() == 3; })) << said();
    pump.pump(500ms);

    // **Stopping finishes every file before it returns.**
    engine.stop(id);
    ASSERT_EQ(finished.count(), 3) << said();
    EXPECT_TRUE(errors.isEmpty()) << said();

    const QDir folder(recordings.path());
    EXPECT_EQ(folder.entryList(QDir::Files, QDir::Name),
              (QStringList{u"Take flac (2).flac"_s, u"Take flac.flac"_s, u"Take wav.wav"_s}));

    std::map<QString, std::int64_t> framesOf;
    for (const QList<QVariant> &file : finished) {
        const QString path = file.at(2).toString();
        const QJsonObject details = QJsonDocument::fromJson(file.at(3).toString().toUtf8()).object();
        const auto frames = static_cast<std::int64_t>(details.value("frames"_L1).toDouble());
        framesOf[QFileInfo(path).fileName()] = frames;
        SCOPED_TRACE(path.toStdString());

        const Heard heard = hear(path);
        EXPECT_EQ(heard.sampleRate, details.value("sampleRate"_L1).toInt());
        EXPECT_EQ(heard.frames, frames) << "the file holds something other than what was said";
        ASSERT_EQ(static_cast<std::int64_t>(heard.left.size()), frames);
        ASSERT_GT(frames, heard.sampleRate / 4);

        // The tone, whole: past the Wave's own fade-in, no step bigger than a
        // 1 kHz sine at -12 dBFS takes from one sample to the next.
        const double amplitude = std::pow(10.0, kLevelDb / 20.0);
        const double sineStep = 2.0 * std::numbers::pi * kHz / heard.sampleRate * amplitude;
        const std::size_t settled = static_cast<std::size_t>(heard.sampleRate) / 20;
        EXPECT_LE(biggestStep(heard.left, settled), sineStep * 1.02)
            << "the recording jumps, so audio was lost or doubled on the way to it";
        double sum = 0.0;
        for (std::size_t index = settled; index < heard.left.size(); ++index) {
            sum += static_cast<double>(heard.left[index]) * heard.left[index];
        }
        const double rmsDb = 10.0 * std::log10(sum / static_cast<double>(heard.left.size() - settled));
        EXPECT_NEAR(rmsDb, kLevelDb - 3.01, 0.1);
        std::cout << QFileInfo(path).fileName().toStdString() << ": " << frames << " frames, "
                  << static_cast<double>(frames) / heard.sampleRate << " s\n";
    }

    // The two that ran the whole time heard the same cycles: the WAV from
    // start to stop, and the FLAC's two files with the time it was off
    // between them.
    EXPECT_GT(framesOf[u"Take wav.wav"_s], framesOf[u"Take flac.flac"_s] + framesOf[u"Take flac (2).flac"_s]);
}

namespace
{

/// The longest run of samples quieter than -80 dBFS, in frames.
[[nodiscard]] std::size_t longestSilence(const std::vector<float> &samples)
{
    std::size_t longest = 0;
    std::size_t run = 0;
    for (const float sample : samples) {
        run = std::fabs(sample) < 1e-4f ? run + 1 : 0;
        longest = std::max(longest, run);
    }
    return longest;
}

} // namespace

// **An application that stops and starts again**, captured twice: one
// source leaves its pause out of the recording, the other keeps it as
// silence. The first file is the two parts joined; the second is the whole
// time, pause and all.
TEST_F(Recording, AnApplicationsPauseIsLeftOutOrKeptAsSilenceAsItsSourceSays)
{
    CaptureNodeOptions deviceOptions = captureOptions("rbtest.recording.gaps");
    deviceOptions.mediaClass = "Audio/Sink";
    CaptureNode device(deviceOptions);
    ASSERT_TRUE(device.waitUntilReady().has_value());

    QTemporaryDir sessions;
    QTemporaryDir recordings;
    ASSERT_TRUE(sessions.isValid() && recordings.isValid());
    dsp::BlockRegistry registry;
    ASSERT_TRUE(blocks::registerBuiltinBlocks(registry));
    Engine engine(registry, SessionStore{sessions.path()});
    QSignalSpy opened(&engine, &EngineFacade::sessionOpened);
    QSignalSpy finished(&engine, &EngineFacade::recordingFileFinished);
    QSignalSpy errors(&engine, &EngineFacade::error);
    engine.connectToPipeWire();
    Waiter pump;
    ASSERT_TRUE(pump.pumpUntil([&] { return engine.isConnected(); }));
    engine.createSession(u"Gaps"_s);
    ASSERT_EQ(opened.count(), 1);
    const QString id = opened.back().at(0).toString();

    const auto source = [](bool fill) {
        return QJsonObject{{"binary"_L1, u"gapapp"_s}, {"outputMode"_L1, u"any"_s}, {"fillGapsWithSilence"_L1, fill}};
    };
    const QJsonObject recorder{{"folder"_L1, recordings.path()},
                               {"fileName"_L1, u"%block"_s},
                               {"format"_L1, u"wav"_s},
                               {"sampleFormat"_L1, u"float32"_s}};
    engine.applyEdit(id, addBlock(u"leaves"_s, blocks::ApplicationSource::kTypeId, source(false)));
    engine.applyEdit(id, addBlock(u"keeps"_s, blocks::ApplicationSource::kTypeId, source(true)));
    engine.applyEdit(id, addBlock(u"joined"_s, blocks::Recorder::kTypeId, recorder));
    engine.applyEdit(id, addBlock(u"whole"_s, blocks::Recorder::kTypeId, recorder));
    engine.applyEdit(id,
                     addBlock(u"out"_s, blocks::OutputDevice::kTypeId, {{"device"_L1, u"rbtest.recording.gaps"_s}}));
    for (std::uint32_t channel = 0; channel < 2; ++channel) {
        engine.applyEdit(id, connect(u"leaves"_s, u"joined"_s, channel));
        engine.applyEdit(id, connect(u"joined"_s, u"out"_s, channel));
        engine.applyEdit(id, connect(u"keeps"_s, u"whole"_s, channel));
    }
    ASSERT_TRUE(errors.isEmpty());
    engine.start(id);
    ASSERT_TRUE(errors.isEmpty());

    const auto playFor = [&](std::chrono::milliseconds time) {
        FakeAppOptions options = appOptions("GapApp", "gapapp");
        options.toneHz = 1000.0;
        FakeApp app(options);
        // Heard, and then for as long as it plays.
        ASSERT_TRUE(
            pump.pumpUntil([&] { return QFileInfo::exists(QDir(recordings.path()).filePath(u"joined.wav"_s)); }));
        pump.pump(time);
    };
    playFor(800ms);
    pump.pump(1000ms); // away
    playFor(800ms);
    engine.stop(id);
    ASSERT_EQ(finished.count(), 2);

    std::map<QString, Heard> files;
    for (const QList<QVariant> &file : finished) {
        files[file.at(1).toString()] = hear(file.at(2).toString());
    }
    const Heard &joined = files[u"joined"_s];
    const Heard &whole = files[u"whole"_s];
    ASSERT_GT(joined.sampleRate, 0);
    const auto seconds = [](const Heard &heard) { return static_cast<double>(heard.left.size()) / heard.sampleRate; };
    std::cout << "joined " << seconds(joined) << " s, longest silence "
              << static_cast<double>(longestSilence(joined.left)) / joined.sampleRate << " s\n";
    std::cout << "whole " << seconds(whole) << " s, longest silence "
              << static_cast<double>(longestSilence(whole.left)) / whole.sampleRate << " s\n";

    // The pause is in the whole recording, as silence, and not in the other.
    EXPECT_GT(longestSilence(whole.left), static_cast<std::size_t>(whole.sampleRate) * 7 / 10);
    EXPECT_LT(longestSilence(joined.left), static_cast<std::size_t>(joined.sampleRate) / 10);
    EXPECT_GT(seconds(whole) - seconds(joined), 0.7);
    EXPECT_GT(seconds(joined), 1.4);
}

// **Pause, resume, split and a limit set while it runs**, through the
// facade on a live session: the Recorder pauses and resumes when told, a
// split makes a second file, and silence turned on as a rule in the middle
// of the recording stops it once the tone falls silent.
TEST_F(Recording, ARunningRecordingIsPausedSplitAndStoppedBySilence)
{
    CaptureNodeOptions deviceOptions = captureOptions("rbtest.recording.controls");
    deviceOptions.mediaClass = "Audio/Sink";
    CaptureNode device(deviceOptions);
    ASSERT_TRUE(device.waitUntilReady().has_value());

    QTemporaryDir sessions;
    QTemporaryDir recordings;
    ASSERT_TRUE(sessions.isValid() && recordings.isValid());
    dsp::BlockRegistry registry;
    ASSERT_TRUE(blocks::registerBuiltinBlocks(registry));
    Engine engine(registry, SessionStore{sessions.path()});
    QSignalSpy opened(&engine, &EngineFacade::sessionOpened);
    QSignalSpy started(&engine, &EngineFacade::recordingFileStarted);
    QSignalSpy finished(&engine, &EngineFacade::recordingFileFinished);
    QSignalSpy paused(&engine, &EngineFacade::recordingPaused);
    QSignalSpy stopped(&engine, &EngineFacade::recordingStopped);
    QSignalSpy errors(&engine, &EngineFacade::error);
    engine.connectToPipeWire();
    Waiter pump;
    ASSERT_TRUE(pump.pumpUntil([&] { return engine.isConnected(); }));
    engine.createSession(u"Controls"_s);
    const QString id = opened.back().at(0).toString();
    engine.applyEdit(id,
                     addBlock(u"wave"_s,
                              blocks::Wave::kTypeId,
                              {{"waveform"_L1, u"sine"_s}, {"frequency"_L1, 1000.0}, {"level"_L1, -12.0}}));
    engine.applyEdit(id,
                     addBlock(u"rec"_s,
                              blocks::Recorder::kTypeId,
                              {{"folder"_L1, recordings.path()},
                               {"fileName"_L1, u"%counter"_s},
                               {"format"_L1, u"wav"_s},
                               {"sampleFormat"_L1, u"float32"_s}}));
    engine.applyEdit(
        id, addBlock(u"out"_s, blocks::OutputDevice::kTypeId, {{"device"_L1, u"rbtest.recording.controls"_s}}));
    for (std::uint32_t channel = 0; channel < 2; ++channel) {
        engine.applyEdit(id, connect(u"wave"_s, u"rec"_s, channel));
        engine.applyEdit(id, connect(u"rec"_s, u"out"_s, channel));
    }
    engine.start(id);
    ASSERT_TRUE(pump.pumpUntil([&] { return started.count() == 1; }));
    pump.pump(400ms);

    engine.controlRecording(id, u"rec"_s, u"pause"_s);
    ASSERT_TRUE(pump.pumpUntil([&] { return paused.count() == 1; }));
    EXPECT_TRUE(paused.back().at(2).toBool());
    pump.pump(600ms); // not in any file
    engine.controlRecording(id, u"rec"_s, u"resume"_s);
    ASSERT_TRUE(pump.pumpUntil([&] { return paused.count() == 2; }));
    EXPECT_FALSE(paused.back().at(2).toBool());
    pump.pump(400ms);

    engine.controlRecording(id, u"rec"_s, u"split"_s);
    ASSERT_TRUE(pump.pumpUntil([&] { return finished.count() == 1 && started.count() == 2; }));
    pump.pump(300ms);

    // Stop on a third of a second of silence, set while it records, and
    // then the tone goes quiet.
    engine.setParam(id, u"rec"_s, u"silenceAction"_s, u"[\"stop\"]"_s);
    engine.setParam(id, u"rec"_s, u"silenceSeconds"_s, u"[0.3]"_s);
    engine.setParam(id, u"rec"_s, u"silenceEnabled"_s, u"[true]"_s);
    pump.pump(200ms);
    ASSERT_TRUE(errors.isEmpty()) << errors.back().at(1).toString().toStdString();
    engine.setParam(id, u"wave"_s, u"level"_s, u"[-80]"_s);
    ASSERT_TRUE(pump.pumpUntil([&] { return stopped.count() == 1; })) << "silence never stopped it";
    EXPECT_EQ(stopped.back().at(1).toString(), u"rec"_s);
    EXPECT_EQ(stopped.back().at(2).toString(), u"silence"_s);
    ASSERT_EQ(finished.count(), 2);
    engine.stop(id);
    EXPECT_EQ(finished.count(), 2) << "a file was started after it had stopped";
    EXPECT_TRUE(errors.isEmpty());

    const Heard first = hear(finished.at(0).at(2).toString());
    const Heard second = hear(finished.at(1).at(2).toString());
    const double firstSeconds = static_cast<double>(first.left.size()) / first.sampleRate;
    const double secondSeconds = static_cast<double>(second.left.size()) / second.sampleRate;
    std::cout << "first file " << firstSeconds << " s, second " << secondSeconds << " s\n";
    // 0.4 s, the pause left out, and 0.4 s: well under the 1.4 s it ran.
    EXPECT_GT(firstSeconds, 0.7);
    EXPECT_LT(firstSeconds, 1.15);
    // The second ends a third of a second into the silence, give or take
    // the time the level takes to fall.
    std::size_t quietAtEnd = 0;
    for (auto sample = second.left.rbegin(); sample != second.left.rend() && std::fabs(*sample) < 1e-3f; ++sample) {
        ++quietAtEnd;
    }
    EXPECT_NEAR(static_cast<double>(quietAtEnd) / second.sampleRate, 0.3, 0.05);
}
