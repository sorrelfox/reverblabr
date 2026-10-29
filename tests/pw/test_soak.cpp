// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The long runs: a session of the size people build, left running, and
// what it costs.
//
// **The reference session** is five sources and thirty blocks: three
// applications and two microphones, each through a lane of its own, summed
// into one tail that is compressed, limited, given a room and recorded to
// WAV and to MP3 while it plays to a device. Both tests here build it the
// way the window would, through the engine's interface (scenario.h).
//
// **Soak.TheReferenceSessionRunsSteadily** runs it for RB_SOAK_SECONDS
// (ten by default, so it can be tried in a minute; the nightly run gives it
// hours), touching it as a person would: a setting changed every few
// seconds, the applications' tones ending and starting again. Every so
// often it writes down what the process holds — resident memory, file
// descriptors, threads — and what the node counted, and at the end it holds
// those against the start. Every file the recorders finish is read back and
// then deleted, so a run of days does not fill the disk.
//
// **SessionCost.TheReferenceSession** measures what the same session costs
// this process, as pw.scenarios does for its budget, and writes the figures
// down rather than judging them: tools/perf/report.py holds them against a
// baseline.
//
// Both write JSON under RB_PERF_OUT when it is set (perfreport.h).

#include "../perf/perfreport.h"
#include "blocks/applicationsource.h"
#include "blocks/basstreble.h"
#include "blocks/dynamics.h"
#include "blocks/equalizer10.h"
#include "blocks/inputdevice.h"
#include "blocks/limiter.h"
#include "blocks/outputdevice.h"
#include "blocks/peakrmsmeter.h"
#include "blocks/recorder.h"
#include "blocks/reverb.h"
#include "blocks/volume.h"
#include "blocks/vumeter.h"
#include "scenario.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <gtest/gtest.h>
#include <sndfile.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace rb;
using namespace rb::core;
using namespace rb::pwtest;
using namespace Qt::StringLiterals;
using namespace std::chrono_literals;

namespace
{

/// What the reference session is made of, and the tones that feed it.
struct Reference {
    static constexpr std::array<double, 3> kAppHz{500.0, 900.0, 1300.0};
    static constexpr double kStereoMicHz = 1900.0;
    static constexpr double kMonoMicHz = 2500.0;

    static QStringList sources()
    {
        return {u"app0"_s, u"app1"_s, u"app2"_s, u"mic0"_s, u"mic1"_s};
    }

    static QStringList tail()
    {
        return {u"sum-meter"_s, u"eq"_s, u"dynamics"_s, u"limiter"_s, u"room"_s, u"vu"_s, u"master"_s};
    }

    /// The blocks that show a signal once it is playing: every source,
    /// every lane, the tail and the device.
    static QStringList audible()
    {
        QStringList blocks = sources();
        for (const QString &source : sources()) {
            blocks << source + u"-volume"_s << source + u"-tone"_s << source + u"-meter"_s;
        }
        return blocks + tail() + QStringList{u"out"_s};
    }

    /// Builds it, recording into \a folder; a file a minute when
    /// \a splitMinutes is set, so a long run hands over finished files to
    /// check while it goes on.
    static void build(Scenario &session, const QString &device, const QString &folder, int splitMinutes)
    {
        for (std::size_t index = 0; index < kAppHz.size(); ++index) {
            session.add(u"app%1"_s.arg(index),
                        blocks::ApplicationSource::kTypeId,
                        QJsonObject{{"binary"_L1, u"soak%1"_s.arg(index)}, {"outputMode"_L1, u"any"_s}});
        }
        session.add(
            u"mic0"_s, blocks::InputDevice::kTypeId, QJsonObject{{"device"_L1, QString::fromLatin1(kStereoSource)}});
        session.add(
            u"mic1"_s, blocks::InputDevice::kTypeId, QJsonObject{{"device"_L1, QString::fromLatin1(kMonoSource)}});
        // A lane each: a level, a tone control and a meter, as a person
        // sets up a source before it joins the others.
        for (const QString &source : sources()) {
            session.add(source + u"-volume"_s, blocks::Volume::kTypeId, QJsonObject{{"gain"_L1, 0.8}});
            session.add(source + u"-tone"_s, blocks::BassTreble::kTypeId);
            session.add(source + u"-meter"_s, blocks::PeakRmsMeter::kTypeId);
            session.lane({source, source + u"-volume"_s, source + u"-tone"_s, source + u"-meter"_s});
        }
        session.add(u"sum-meter"_s, blocks::PeakRmsMeter::kTypeId);
        session.add(u"eq"_s, blocks::Equalizer10::kTypeId);
        // Five tones at −12 dBFS, less a little each, add up past full
        // scale: brought down four to one above −12 dB, and the limiter
        // catches what is left.
        session.add(u"dynamics"_s, blocks::Dynamics::kTypeId, dynamicsSettings(-12.0, 4.0));
        session.add(u"limiter"_s, blocks::Limiter::kTypeId);
        session.add(u"room"_s, blocks::Reverb::kTypeId);
        session.add(u"vu"_s, blocks::VuMeter::kTypeId);
        session.add(u"master"_s, blocks::Volume::kTypeId, QJsonObject{{"gain"_L1, 0.5}});
        QJsonObject recording{{"folder"_L1, folder}, {"fileName"_L1, u"%session %block %counter"_s}};
        if (splitMinutes > 0) {
            recording.insert("splitEnabled"_L1, true);
            recording.insert("splitEvery"_L1, splitMinutes);
            recording.insert("splitUnit"_L1, typeName(blocks::Recorder::kUnitMinutes));
        }
        QJsonObject wav = recording;
        wav.insert("format"_L1, typeName(blocks::Recorder::kFormatWav));
        QJsonObject mp3 = recording;
        mp3.insert("format"_L1, typeName(blocks::Recorder::kFormatMp3));
        session.add(u"wav"_s, blocks::Recorder::kTypeId, wav);
        session.add(u"mp3"_s, blocks::Recorder::kTypeId, mp3);
        session.add(u"out"_s, blocks::OutputDevice::kTypeId, QJsonObject{{"device"_L1, device}});
        for (const QString &source : sources()) {
            session.link(source + u"-meter"_s, u"sum-meter"_s);
        }
        session.lane(tail() << u"wav"_s << u"out"_s);
        session.link(u"master"_s, u"mp3"_s);
    }
};

/// The tones the reference session hears: three applications and two
/// microphones, each played again when its minute is up.
class Feeds
{
public:
    explicit Feeds(Waiter &pump)
        : m_pump(pump)
    {
        for (std::size_t index = 0; index < Reference::kAppHz.size(); ++index) {
            const std::string binary = "soak" + std::to_string(index);
            m_apps.push_back(std::make_unique<FakeApp>(toneApp(binary, binary, Reference::kAppHz.at(index))));
            m_appAge.emplace_back().start();
        }
        m_stereo = std::make_unique<Microphone>(m_pump, kStereoSource, "soakmic0", Reference::kStereoMicHz);
        m_mono = std::make_unique<Microphone>(m_pump, kMonoSource, "soakmic1", Reference::kMonoMicHz);
        m_micAge.start();
    }

    [[nodiscard]] bool fed() const
    {
        return m_stereo->fed() && m_mono->fed();
    }

    /// Plays a new tone wherever the old one is near its end. Answers how
    /// many were started.
    ///
    /// **By age, not by the process**: pw-cat can be done with the file
    /// while its process is still winding down, and a tone that has ended
    /// with the process still there is a lane gone quiet for good.
    int keepPlaying()
    {
        int restarted = 0;
        for (std::size_t index = 0; index < m_apps.size(); ++index) {
            if (!m_apps[index]->running() || m_appAge[index].elapsed() >= kToneLife) {
                skip(index);
                ++restarted;
            }
        }
        // A microphone's tone is linked by hand, so a new one is a new
        // Microphone: the old links went with the old stream.
        if (!m_stereo->playing() || !m_mono->playing() || m_micAge.elapsed() >= kToneLife) {
            m_stereo.reset();
            m_mono.reset();
            m_stereo = std::make_unique<Microphone>(m_pump, kStereoSource, "soakmic0", Reference::kStereoMicHz);
            m_mono = std::make_unique<Microphone>(m_pump, kMonoSource, "soakmic1", Reference::kMonoMicHz);
            m_micAge.restart();
            restarted += 2;
        }
        return restarted;
    }

    /// Ends one application's tone early and starts a new one, as somebody
    /// skipping a track does: the capture has to find the new stream.
    void skip(std::size_t index)
    {
        m_apps.at(index % m_apps.size())->restart();
        m_appAge.at(index % m_apps.size()).restart();
    }

private:
    /// The test tone lasts a minute; a new one goes in before it ends.
    static constexpr qint64 kToneLife = 50000;

    Waiter &m_pump;
    std::vector<std::unique_ptr<FakeApp>> m_apps;
    std::vector<QElapsedTimer> m_appAge;
    std::unique_ptr<Microphone> m_stereo;
    std::unique_ptr<Microphone> m_mono;
    QElapsedTimer m_micAge;
};

/// What this process holds, read from /proc.
struct Holding {
    double seconds = 0.0; ///< Since the measured part of the run began.
    double rssMiB = 0.0;
    int fds = 0;
    int threads = 0;
    /// What the descriptors are open on, for a message that has to say
    /// which one is new.
    std::string fdTargets;
    /// The threads by name, for the same.
    std::string threadNames;
    std::uint64_t cycles = 0;
    std::uint64_t xruns = 0;
    std::uint64_t lostCycles = 0;
};

[[nodiscard]] double statusField(const std::string &field)
{
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.starts_with(field + ":")) {
            return std::atof(line.c_str() + field.size() + 1);
        }
    }
    return 0.0;
}

/// What each descriptor is open on, sorted: "pipe:[…]", "socket:[…]" and
/// files by their paths, with the numbers in brackets dropped so that two
/// readings compare.
[[nodiscard]] std::string descriptorTargets()
{
    std::vector<std::string> targets;
    std::error_code error;
    for (auto it = std::filesystem::directory_iterator("/proc/self/fd", error);
         !error && it != std::filesystem::directory_iterator();
         it.increment(error)) {
        std::error_code unreadable;
        std::string target = std::filesystem::read_symlink(it->path(), unreadable).string();
        if (const std::size_t bracket = target.find(":["); bracket != std::string::npos) {
            target.resize(bracket);
        }
        targets.push_back(target);
    }
    std::ranges::sort(targets);
    std::string joined;
    for (const std::string &target : targets) {
        joined += (joined.empty() ? "" : ", ") + target;
    }
    return joined;
}

/// How many descriptors more at the end than at the start are the moment
/// the reading was taken rather than a leak (see the test).
constexpr int kDescriptorSlack = 3;

[[nodiscard]] int openDescriptors()
{
    int count = 0;
    std::error_code error;
    for (auto it = std::filesystem::directory_iterator("/proc/self/fd", error);
         !error && it != std::filesystem::directory_iterator();
         it.increment(error)) {
        ++count;
    }
    // Less the one the iterator itself has open.
    return count - 1;
}

[[nodiscard]] Holding holding(const pw::SessionNode &node, double seconds)
{
    const pw::SessionTelemetry telemetry = node.telemetry();
    Holding now;
    now.seconds = seconds;
    now.rssMiB = statusField("VmRSS") / 1024.0;
    now.fds = openDescriptors();
    now.fdTargets = descriptorTargets();
    // **Qt's pooled threads are not counted**: the global pool keeps an
    // idle one for half a minute in case it is wanted again, and has a
    // fixed ceiling, so one alive at the end is the pool and not a leak.
    int pooled = 0;
    std::vector<std::string> names;
    std::error_code error;
    for (auto it = std::filesystem::directory_iterator("/proc/self/task", error);
         !error && it != std::filesystem::directory_iterator();
         it.increment(error)) {
        std::ifstream comm(it->path() / "comm");
        std::string name;
        std::getline(comm, name);
        if (name == "Thread (pooled)") {
            ++pooled;
            continue;
        }
        names.push_back(name);
    }
    std::ranges::sort(names);
    now.threads = static_cast<int>(statusField("Threads")) - pooled;
    for (const std::string &name : names) {
        now.threadNames += (now.threadNames.empty() ? "" : ", ") + name;
    }
    now.cycles = telemetry.cycles;
    now.xruns = telemetry.xruns;
    now.lostCycles = telemetry.lostCycles;
    return now;
}

/// How fast the resident memory grew over \a samples, in MiB an hour, by
/// least squares: one reading that happens to land on a burst says
/// little, and a slope through all of them is what a leak looks like.
[[nodiscard]] double growthPerHour(const std::vector<Holding> &samples)
{
    if (samples.size() < 3) {
        return 0.0;
    }
    double meanT = 0.0;
    double meanR = 0.0;
    for (const Holding &sample : samples) {
        meanT += sample.seconds;
        meanR += sample.rssMiB;
    }
    meanT /= static_cast<double>(samples.size());
    meanR /= static_cast<double>(samples.size());
    double covariance = 0.0;
    double variance = 0.0;
    for (const Holding &sample : samples) {
        covariance += (sample.seconds - meanT) * (sample.rssMiB - meanR);
        variance += (sample.seconds - meanT) * (sample.seconds - meanT);
    }
    return variance > 0.0 ? covariance / variance * 3600.0 : 0.0;
}

/// A finished recording, read back the way any other program would read
/// it. Answers why it is not right, or nothing.
[[nodiscard]] std::string checkRecording(const QString &path, const QString &detailsJson)
{
    const QJsonObject details = QJsonDocument::fromJson(detailsJson.toUtf8()).object();
    const auto said = static_cast<std::int64_t>(details.value("frames"_L1).toDouble());
    SF_INFO info{};
    SNDFILE *file = sf_open(path.toUtf8().constData(), SFM_READ, &info);
    if (file == nullptr) {
        return "it cannot be opened: " + std::string(sf_strerror(nullptr));
    }
    std::vector<float> buffer(4096 * static_cast<std::size_t>(std::max(info.channels, 1)));
    std::int64_t read = 0;
    for (;;) {
        const sf_count_t got = sf_readf_float(file, buffer.data(), 4096);
        if (got <= 0) {
            break;
        }
        read += got;
    }
    sf_close(file);
    if (read == 0) {
        return "it holds no audio";
    }
    // **An MP3 is read back a frame's worth longer or shorter**: the
    // encoder pads its first and last frames, and not every reader knows by
    // how much. A WAV holds exactly what was said.
    const std::int64_t slack = path.endsWith(u".mp3"_s) ? 4608 : 0;
    if (std::llabs(read - said) > slack) {
        return "it holds " + std::to_string(read) + " frames where " + std::to_string(said) + " were written";
    }
    return {};
}

/// Seconds to sample over: the run's length divided into about two hundred
/// readings, never closer than two seconds or further apart than a minute.
[[nodiscard]] std::chrono::milliseconds sampleEvery(int seconds)
{
    const auto every = std::chrono::milliseconds{static_cast<long>(seconds) * 1000 / 200};
    return std::clamp(every, std::chrono::milliseconds{2000}, std::chrono::milliseconds{60000});
}

} // namespace

using Soak = SessionTest;

TEST_F(Soak, TheReferenceSessionRunsSteadily)
{
    const int seconds = soakSeconds();
    // **Quantum 1024 at 48 kHz**, where the reference session's budget is
    // stated, and a clock held still so that nothing else changes it
    // halfway through a run of hours.
    ForcedClock clock;
    clock.setRate(48000);
    clock.setQuantum(1024);

    QTemporaryDir recordings;
    ASSERT_TRUE(recordings.isValid());
    Scenario session(u"Soak"_s);
    ASSERT_TRUE(session.open());
    // A file a minute once the run is long enough to finish one: the files
    // are checked as they are handed over, rather than all at the end.
    Reference::build(session, QString::fromLatin1(kStereoSink), recordings.path(), seconds >= 180 ? 1 : 0);
    ASSERT_EQ(session.errors(), "");

    int recordingsChecked = 0;
    std::vector<std::string> badRecordings;
    QObject::connect(&session.engine(),
                     &EngineFacade::recordingFileFinished,
                     &session.engine(),
                     [&](const QString &, const QString &, const QString &path, const QString &details) {
                         if (std::string why = checkRecording(path, details); !why.empty()) {
                             badRecordings.push_back(QFileInfo(path).fileName().toStdString() + ": " + why);
                         }
                         ++recordingsChecked;
                         QFile::remove(path);
                     });

    Feeds feeds(session.pump());
    ASSERT_TRUE(feeds.fed()) << "a tone never reached its virtual microphone";
    ASSERT_TRUE(session.start().has_value()) << session.errors();
    const QStringList blocks = Reference::audible();
    ASSERT_TRUE(session.pump().pumpUntil([&] { return session.allLive(blocks); }))
        << "a block never showed a signal: " << session.firstQuiet(blocks);
    const pw::SessionNode *node = session.runtime()->node();
    ASSERT_NE(node, nullptr);

    // **A warm-up before the first reading**: the allocator, the encoders
    // and the scene of first-time work every process goes through are not
    // growth. Then what the machine loses with nothing changing, which is
    // what the session's lost cycles are judged against.
    idleFor(3s);
    MachineCost machine(session.pump(), *session.runtime()->node());

    QElapsedTimer run;
    run.start();
    const auto elapsed = [&run] { return static_cast<double>(run.nsecsElapsed()) * 1e-9; };
    std::vector<Holding> samples{holding(*node, 0.0)};
    const std::chrono::milliseconds every = sampleEvery(seconds);
    int restarts = 0;
    int edits = 0;
    QElapsedTimer sinceSample;
    sinceSample.start();
    for (int tick = 1; run.elapsed() < static_cast<qint64>(seconds) * 1000; ++tick) {
        idleFor(1s);
        restarts += feeds.keepPlaying();
        // **Touched as a person would**: a lane's level moved every
        // couple of seconds, and a track skipped every half minute.
        if (tick % 2 == 0) {
            const QString lane = Reference::sources().at(tick / 2 % Reference::sources().size());
            session.setParam(lane + u"-volume"_s, u"gain"_s, (tick / 2) % 2 == 0 ? 0.7 : 0.8);
            ++edits;
        }
        if (tick % 30 == 0) {
            feeds.skip(static_cast<std::size_t>(tick / 30));
            ++restarts;
        }
        if (sinceSample.elapsed() >= every.count()) {
            samples.push_back(holding(*node, elapsed()));
            sinceSample.restart();
        }
    }
    samples.push_back(holding(*node, elapsed()));
    const Holding &first = samples.front();
    const Holding &last = samples.back();
    const TelemetrySnapshot snapshot = session.snapshot();
    // **Given the time a restarted tone takes to be found again**: the run
    // may end a moment after one, and a block that is quiet then is one
    // being caught up with, not one that went quiet.
    const bool allLive = session.pump().pumpUntil([&] { return session.allLive(blocks); });
    const std::string quiet = session.firstQuiet(blocks);
    session.stop();
    // The files still open were finished by the stop; let their signals
    // arrive and be read.
    session.pump().pump(500ms);

    const double took = last.seconds;
    const std::uint64_t cycles = last.cycles - first.cycles;
    const std::uint64_t xruns = last.xruns - first.xruns;
    const std::uint64_t lost = last.lostCycles - first.lostCycles;
    const double expectedCycles = took * 48000.0 / 1024.0;
    const double rssGrowth = last.rssMiB - first.rssMiB;
    // **The slope from the second half on**, once whatever a process
    // settles into has been settled into.
    const std::vector<Holding> settled(samples.begin() + static_cast<std::ptrdiff_t>(samples.size() / 2),
                                       samples.end());
    const double slope = growthPerHour(settled);
    const double hours = took / 3600.0;

    std::cout << "soak: " << took << " s, " << cycles << " cycles of " << expectedCycles << " expected, " << xruns
              << " xruns costing " << lost << " cycles; RSS " << first.rssMiB << " -> " << last.rssMiB << " MiB ("
              << slope << " MiB an hour in the second half), descriptors " << first.fds << " -> " << last.fds
              << ", threads " << first.threads << " -> " << last.threads << "; " << edits << " edits, " << restarts
              << " tones restarted, " << recordingsChecked << " recordings read back\n";

    perf::Report report("soak");
    report.note("seconds", std::to_string(seconds));
    report.note("session", "5 sources, 30 blocks, WAV and MP3 recorders, 48000 Hz / 1024");
    const auto add = [&report](std::string name,
                               double value,
                               std::string unit,
                               perf::Better better,
                               std::optional<double> tolerance,
                               std::optional<double> noise) {
        report.add(perf::Metric{.name = "soak." + std::move(name),
                                .value = value,
                                .unit = std::move(unit),
                                .better = better,
                                .tolerance = tolerance,
                                .noise = noise,
                                .detail = {}});
    };
    add("seconds", took, "s", perf::Better::Neither, std::nullopt, std::nullopt);
    add("xruns-per-hour", static_cast<double>(xruns) / hours, "an hour", perf::Better::Lower, 0.5, 2.0);
    add("lost-cycles-per-hour", static_cast<double>(lost) / hours, "an hour", perf::Better::Lower, 0.5, 5.0);
    add("cycles-run",
        static_cast<double>(cycles) / expectedCycles * 100.0,
        "% of expected",
        perf::Better::Higher,
        0.01,
        0.5);
    add("rss-start", first.rssMiB, "MiB", perf::Better::Lower, 0.2, 4.0);
    add("rss-growth", rssGrowth, "MiB", perf::Better::Lower, 0.5, 4.0);
    add("rss-growth-per-hour", slope, "MiB an hour", perf::Better::Lower, 0.5, 2.0);
    add("fd-growth", last.fds - first.fds, "descriptors", perf::Better::Lower, 0.0, kDescriptorSlack);
    add("thread-growth", last.threads - first.threads, "threads", perf::Better::Lower, 0.0, 0.0);
    add("peak-dsp-load", snapshot.peakDspLoad * 100.0, "% of a cycle", perf::Better::Lower, 0.5, 5.0);
    EXPECT_TRUE(report.writeToOutputFolder("soak.json"));

    EXPECT_EQ(session.errors(), "");
    EXPECT_TRUE(allLive) << "a block went quiet during the run: " << quiet;
    EXPECT_TRUE(badRecordings.empty()) << badRecordings.front();
    EXPECT_GE(recordingsChecked, 2) << "the two recorders never handed over a file";
    // **Every cycle run.** One that stops running loses them all from
    // then on; a node that skipped every so often would fall short too.
    EXPECT_GE(static_cast<double>(cycles), expectedCycles * 0.98)
        << "the session ran " << cycles << " cycles in " << took << " s, where " << expectedCycles << " were due";
    const std::uint64_t allowed =
        machine.allowanceFor(std::chrono::milliseconds{static_cast<long>(took * 1000)}, cycles);
    EXPECT_LE(xruns, allowed) << xruns << " cycles were missed in " << took << " s, where " << allowed
                              << " were allowed (the machine lost " << machine.lost() << " in the second before)";
    // **Nothing held on to.** Every tone restarted is a stream found and
    // linked again, every edit a plan handed over, every file a writer: a
    // descriptor or a thread kept by any of them shows here, however few.
    //
    // **Descriptors, give or take the test's own few.** The tones are
    // processes of this test's, each with its pipes, and one may be
    // between two of them at any reading; the recorders' files open and
    // close as they split. So a few either way are the moment the reading
    // was taken, and a leak is more than that, or, in a long run, a count
    // that is still climbing in its second half.
    EXPECT_LE(last.fds, first.fds + kDescriptorSlack)
        << "the run ended holding " << last.fds - first.fds << " more file descriptors: " << first.fdTargets
        << " at the start, " << last.fdTargets << " at the end";
    if (took >= 1800.0) {
        const auto most = [](auto from, auto to) {
            return std::max_element(from, to, [](const Holding &a, const Holding &b) { return a.fds < b.fds; })->fds;
        };
        const auto half = samples.begin() + static_cast<std::ptrdiff_t>(samples.size() / 2);
        EXPECT_LE(most(half, samples.end()), most(samples.begin(), half) + kDescriptorSlack)
            << "the descriptors were still climbing in the second half of the run";
    }
    EXPECT_LE(last.threads, first.threads)
        << "the run ended with " << last.threads - first.threads << " more threads: " << first.threadNames
        << " at the start, " << last.threadNames << " at the end";
    // **Memory**: a fixed allowance for a short run, where a few allocator
    // pages are noise, and a rate for a long one, where they are not.
    EXPECT_LT(rssGrowth, 32.0) << "the resident memory grew by " << rssGrowth << " MiB";
    if (took >= 1800.0) {
        EXPECT_LT(slope, 2.0) << "the resident memory was growing by " << slope << " MiB an hour";
    }
}

namespace
{

/// What this process costs for \a window, as a share of one core.
[[nodiscard]] double costOver(std::chrono::milliseconds window)
{
    const double started = processSeconds();
    idleFor(window);
    return (processSeconds() - started) / std::chrono::duration<double>(window).count();
}

/// How long each cost is measured over: RB_PERF_WINDOW_SECONDS, or five.
[[nodiscard]] std::chrono::milliseconds costWindow()
{
    if (const char *value = std::getenv("RB_PERF_WINDOW_SECONDS"); value != nullptr && std::atoi(value) > 0) {
        return std::chrono::milliseconds{std::atoi(value) * 1000};
    }
    return 5000ms;
}

} // namespace

using SessionCost = SessionTest;

// **What the reference session costs**, at the rate and quantum the scope
// states its budget for (3 % of one core at 48 kHz / 1024) and at 256,
// where a cycle's fixed costs count four times as often. Charged as in
// pw.scenarios: the process running the session, less the same process with
// the same tones playing and the session stopped.
TEST_F(SessionCost, TheReferenceSession)
{
    const std::chrono::milliseconds window = costWindow();
    QTemporaryDir recordings;
    ASSERT_TRUE(recordings.isValid());
    Scenario session(u"Cost"_s);
    ASSERT_TRUE(session.open());
    Reference::build(session, QString::fromLatin1(kStereoSink), recordings.path(), 0);
    ASSERT_EQ(session.errors(), "");
    Feeds feeds(session.pump());
    ASSERT_TRUE(feeds.fed());

    perf::Report report("session");
    report.note("session", "5 sources, 30 blocks, WAV and MP3 recorders");
    for (const int quantum : {1024, 256}) {
        ForcedClock clock;
        clock.setRate(48000);
        clock.setQuantum(quantum);
        (void)feeds.keepPlaying();
        idleFor(500ms);
        const double idle = costOver(window);

        ASSERT_TRUE(session.start().has_value()) << session.errors();
        const QStringList blocks = Reference::audible();
        ASSERT_TRUE(session.pump().pumpUntil([&] { return session.allLive(blocks); }))
            << "a block never showed a signal: " << session.firstQuiet(blocks);
        idleFor(500ms);
        ASSERT_EQ(session.snapshot().frames, static_cast<std::uint32_t>(quantum))
            << "the graph is not running at the quantum being measured";
        const double running = costOver(window);
        const TelemetrySnapshot after = session.snapshot();
        session.stop();
        // The recorders' files are finished by the stop; nothing reads
        // them here.
        for (const QString &file : QDir(recordings.path()).entryList(QDir::Files)) {
            QFile::remove(QDir(recordings.path()).filePath(file));
        }

        const double cost = (running - idle) * 100.0;
        std::cout << "reference session at 48 kHz / " << quantum << ": " << cost << " % of a core (running "
                  << running * 100.0 << " %, idle " << idle * 100.0 << " %), worst cycle " << after.peakDspLoad * 100.0
                  << " % of its period\n";
        report.add(perf::Metric{
            .name = "session.reference-" + std::to_string(quantum),
            .value = cost,
            .unit = "% of a core",
            .better = perf::Better::Lower,
            .tolerance = 0.25,
            .noise = 0.3,
            .detail = "running " + std::to_string(running * 100.0) + " %, idle " + std::to_string(idle * 100.0) + " %",
        });
        report.add(perf::Metric{
            .name = "session.reference-" + std::to_string(quantum) + ".peak-dsp-load",
            .value = after.peakDspLoad * 100.0,
            .unit = "% of a cycle",
            .better = perf::Better::Lower,
            .tolerance = 0.5,
            .noise = 2.0,
            .detail = {},
        });
        EXPECT_EQ(session.errors(), "");
    }
    EXPECT_TRUE(report.writeToOutputFolder("session.json"));
}
