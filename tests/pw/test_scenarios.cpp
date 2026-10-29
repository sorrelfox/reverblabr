// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The four sessions M1 is judged by, each built the way the window
// builds one — through the engine's own interface, one edit at a time, from
// the main blocks and nothing else — and run against a real PipeWire.
//
// **The other suites test a block; these test the blocks together.** Each
// lane plays a tone of its own frequency, so what arrives at the far end can
// be taken apart again and every source accounted for; and every number the
// interface would show is read the way the interface reads it, from the
// telemetry snapshot, and held against what the device actually received.
// A meter that said one thing while the device heard another would pass a
// test of the meter and a test of the device, and fail here.
//
// **The dynamics are checked against the curve**, the same function the
// curve editor draws (core::DynamicsCurve), so "what is drawn is what is
// heard" is a measurement rather than a promise.

#include "blocks/applicationsource.h"
#include "blocks/builtinblocks.h"
#include "blocks/ducking.h"
#include "blocks/dynamics.h"
#include "blocks/firfilter.h"
#include "blocks/inputdevice.h"
#include "blocks/inputswitch.h"
#include "blocks/mixer.h"
#include "blocks/outputdevice.h"
#include "blocks/peakrmsmeter.h"
#ifdef RB_HAVE_RNNOISE
#include "blocks/speechdenoise.h"
#endif
#include "blocks/systemaudio.h"
#include "blocks/volume.h"
#include "blocks/wave.h"
#include "core/dynamicscurve.h"
#include "core/editcommand.h"
#include "core/engine.h"
#include "core/session.h"
#include "core/sessionruntime.h"
#include "core/telemetry.h"
#include "dsp/blockregistry.h"
#include "dsp/decibel.h"
#include "scenario.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <gtest/gtest.h>
#include <sndfile.h>

#include <algorithm>
#include <array>
#include <ctime>
#include <iostream>
#include <map>
#include <numeric>
#include <optional>
#include <string>
#include <vector>

using namespace rb;
using namespace rb::core;
using namespace rb::pwtest;
using namespace Qt::StringLiterals;

namespace
{

/// A frequency no lane plays, for checking that nothing arrived that was
/// not sent.
constexpr double kNobodyHz = 3500.0;

/// Whether this is a sanitizer build, which runs the DSP several times
/// slower: a load an optimised build carries with room to spare can take
/// every cycle whole there.
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
constexpr bool kSanitized = true;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
constexpr bool kSanitized = true;
#else
constexpr bool kSanitized = false;
#endif
#else
constexpr bool kSanitized = false;
#endif

/// What the curve editor would draw a steady \a inputDb coming out at.
[[nodiscard]] double curveSays(const QJsonObject &settings, double inputDb)
{
    DynamicsCurve curve;
    curve.setParams(settings.toVariantMap());
    return curve.outputDb(inputDb);
}

/// The multi-lane session of scenario 4, shared by the scenario and the CPU
/// budget: three applications and two microphones merging into one tail.
struct MultiLane {
    static constexpr std::array<double, 3> kAppHz{700.0, 1100.0, 1700.0};
    static constexpr double kStereoMicHz = 2300.0;
    static constexpr double kMonoMicHz = 2900.0;

    static QStringList tail()
    {
        return {u"tail-meter"_s, u"tail-dynamics"_s, u"tail-volume"_s};
    }

    static QStringList sources()
    {
        return {u"app0"_s, u"app1"_s, u"app2"_s, u"mic0"_s, u"mic1"_s};
    }

    /// Five tones at −12 dBFS each add up to +2 dBFS at worst, so the tail
    /// is set to bring that down rather than to clip it: the compressor
    /// takes the sum's peaks down four to one above −12 dB, and the volume
    /// halves what is left.
    static QJsonObject tailDynamics()
    {
        return dynamicsSettings(-12.0, 4.0);
    }

    static void build(Scenario &session, const QString &device)
    {
        for (std::size_t index = 0; index < kAppHz.size(); ++index) {
            session.add(u"app%1"_s.arg(index),
                        blocks::ApplicationSource::kTypeId,
                        QJsonObject{{"binary"_L1, u"lane%1"_s.arg(index)}, {"outputMode"_L1, u"any"_s}});
        }
        session.add(
            u"mic0"_s, blocks::InputDevice::kTypeId, QJsonObject{{"device"_L1, QString::fromLatin1(kStereoSource)}});
        session.add(
            u"mic1"_s, blocks::InputDevice::kTypeId, QJsonObject{{"device"_L1, QString::fromLatin1(kMonoSource)}});
        session.add(u"tail-meter"_s, blocks::PeakRmsMeter::kTypeId);
        session.add(u"tail-dynamics"_s, blocks::Dynamics::kTypeId, tailDynamics());
        session.add(u"tail-volume"_s, blocks::Volume::kTypeId, QJsonObject{{"gain"_L1, 0.5}});
        session.add(u"out"_s, blocks::OutputDevice::kTypeId, QJsonObject{{"device"_L1, device}});
        // **Five links into one input**, which the engine sums.
        for (const QString &source : sources()) {
            session.link(source, u"tail-meter"_s);
        }
        session.lane(tail() << u"out"_s);
    }
};

} // namespace

using Scenarios = SessionTest;

// Scenario 1, the reference usage: a microphone through a meter, a volume and
// a compressor, metered again, to a device.
TEST_F(Scenarios, AMicrophoneIsMeteredLeveledCompressedAndHeard)
{
    CaptureNode device(deviceOptions("rbtest.scenario.one", 1000.0));
    ASSERT_TRUE(device.waitUntilReady().has_value());

    Scenario session(u"Microphone"_s);
    ASSERT_TRUE(session.open());
    const QJsonObject dynamics = dynamicsSettings(-24.0, 4.0);
    session.add(u"mic"_s, blocks::InputDevice::kTypeId, QJsonObject{{"device"_L1, QString::fromLatin1(kStereoSource)}});
    session.add(u"pre"_s, blocks::PeakRmsMeter::kTypeId);
    session.add(u"volume"_s, blocks::Volume::kTypeId, QJsonObject{{"gain"_L1, 0.5}});
    session.add(u"dynamics"_s, blocks::Dynamics::kTypeId, dynamics);
    session.add(u"post"_s, blocks::PeakRmsMeter::kTypeId);
    session.add(u"out"_s, blocks::OutputDevice::kTypeId, QJsonObject{{"device"_L1, u"rbtest.scenario.one"_s}});
    session.lane({u"mic"_s, u"pre"_s, u"volume"_s, u"dynamics"_s, u"post"_s, u"out"_s});
    ASSERT_EQ(session.errors(), "");

    Microphone voice(session.pump(), kStereoSource, "scenariovoice", 1000.0);
    ASSERT_TRUE(voice.fed()) << "the tone never reached the virtual microphone";
    ASSERT_TRUE(session.start().has_value()) << session.errors();

    const QStringList blocks{u"mic"_s, u"pre"_s, u"volume"_s, u"dynamics"_s, u"post"_s, u"out"_s};
    ASSERT_TRUE(session.pump().pumpUntil([&] { return session.allLive(blocks); }))
        << "a block never showed a signal: " << session.firstQuiet(blocks);
    session.pump().pump(std::chrono::milliseconds{700});

    // **The meters say what the chain does**, stage by stage: the tone as
    // the microphone gave it, then halved, then down the curve.
    const double arriving = kTonePeakDb;
    const double levelled = arriving - 6.02;
    const double compressed = curveSays(dynamics, levelled);
    const std::optional<BlockTelemetry> pre = session.block(u"pre"_s);
    const std::optional<BlockTelemetry> comp = session.block(u"dynamics"_s);
    const std::optional<BlockTelemetry> post = session.block(u"post"_s);
    ASSERT_TRUE(pre && comp && post);
    EXPECT_NEAR(dsp::gainToDb(pre->peak[0]), arriving, 0.5) << "the first meter";
    // The curve marker: where the sound is on the curve, and the curve and
    // the processor agreeing about where that puts it.
    EXPECT_NEAR(comp->values[blocks::Dynamics::kInputLevelValue], levelled, 0.5) << "the marker's input";
    EXPECT_NEAR(comp->values[blocks::Dynamics::kOutputLevelValue], compressed, 0.5) << "the marker's output";
    EXPECT_NEAR(dsp::gainToDb(post->peak[0]), compressed, 0.75) << "the second meter";

    // **And the device heard what the last meter showed.**
    const std::map<std::string, SineCheck> heard = device.measureSine(400ms);
    ASSERT_TRUE(heard.contains("FL") && heard.contains("FR"));
    EXPECT_NEAR(heard.at("FL").peakDb, compressed, 0.75) << "the compressed tone did not reach the device";
    EXPECT_NEAR(heard.at("FR").peakDb, heard.at("FL").peakDb, 0.1);
    EXPECT_NEAR(heard.at("FL").peakDb, dsp::gainToDb(post->peak[0]), 0.5) << "the device and the meter disagree";
    std::cout << "scenario 1: in " << arriving << ", levelled " << levelled << ", curve " << compressed << ", device "
              << heard.at("FL").peakDb << " dBFS, residual " << heard.at("FL").residualDb << " dB\n";
    EXPECT_LT(heard.at("FL").residualDb, -40.0) << "the chain is not passing a clean tone";
    EXPECT_EQ(session.errors(), "");

    session.stop();
}

// Scenario 2: an application turned up past its own level, then compressed.
TEST_F(Scenarios, AnApplicationIsTurnedUpPastItsLevelCompressedAndHeard)
{
    CaptureNode device(deviceOptions("rbtest.scenario.two", 1000.0));
    ASSERT_TRUE(device.waitUntilReady().has_value());

    Scenario session(u"Application"_s);
    ASSERT_TRUE(session.open());
    const QJsonObject dynamics = dynamicsSettings(-12.0, 2.0);
    session.add(u"app"_s,
                blocks::ApplicationSource::kTypeId,
                QJsonObject{{"binary"_L1, u"scenarioapp"_s}, {"outputMode"_L1, u"any"_s}});
    session.add(u"meter"_s, blocks::PeakRmsMeter::kTypeId);
    // **200 %, which is the top of the slider only with overdrive on.**
    session.add(u"volume"_s,
                blocks::Volume::kTypeId,
                QJsonObject{{"gain"_L1, 2.0}, {"overdrive"_L1, typeName(blocks::Volume::kOverdrive2x)}});
    session.add(u"dynamics"_s, blocks::Dynamics::kTypeId, dynamics);
    session.add(u"out"_s, blocks::OutputDevice::kTypeId, QJsonObject{{"device"_L1, u"rbtest.scenario.two"_s}});
    session.lane({u"app"_s, u"meter"_s, u"volume"_s, u"dynamics"_s, u"out"_s});
    ASSERT_EQ(session.errors(), "");
    ASSERT_TRUE(session.start().has_value()) << session.errors();

    FakeApp app(toneApp("ScenarioApp", "scenarioapp", 1000.0));
    const QStringList blocks{u"app"_s, u"meter"_s, u"volume"_s, u"dynamics"_s, u"out"_s};
    ASSERT_TRUE(session.pump().pumpUntil([&] { return session.allLive(blocks); }))
        << "a block never showed a signal: " << session.firstQuiet(blocks);
    session.pump().pump(std::chrono::milliseconds{700});

    const double arriving = kTonePeakDb;
    const double boosted = arriving + 6.02;
    const double compressed = curveSays(dynamics, boosted);
    const std::optional<BlockTelemetry> meter = session.block(u"meter"_s);
    const std::optional<BlockTelemetry> comp = session.block(u"dynamics"_s);
    ASSERT_TRUE(meter && comp);
    EXPECT_NEAR(dsp::gainToDb(meter->peak[0]), arriving, 0.5);
    EXPECT_NEAR(comp->values[blocks::Dynamics::kInputLevelValue], boosted, 0.5) << "200 % was not twice the level";
    EXPECT_NEAR(comp->values[blocks::Dynamics::kOutputLevelValue], compressed, 0.5);

    const std::map<std::string, SineCheck> heard = device.measureSine(400ms);
    ASSERT_TRUE(heard.contains("FL"));
    std::cout << "scenario 2: in " << arriving << ", boosted " << boosted << ", curve " << compressed << ", device "
              << heard.at("FL").peakDb << " dBFS, residual " << heard.at("FL").residualDb << " dB\n";
    EXPECT_NEAR(heard.at("FL").peakDb, compressed, 0.75);
    EXPECT_LT(heard.at("FL").residualDb, -40.0);
    EXPECT_EQ(session.errors(), "");

    session.stop();
}

// Scenario 3, the second M1 exit session: everything playing but one
// application, and a microphone, to one device.
TEST_F(Scenarios, SystemAudioLessOneApplicationAndAMicrophoneAreHeardTogether)
{
    constexpr double kKeptHz = 1000.0;
    constexpr double kVoiceHz = 1500.0;
    constexpr double kLeftOutHz = 2500.0;

    CaptureNode device(deviceOptions("rbtest.scenario.three"));
    ASSERT_TRUE(device.waitUntilReady().has_value());

    Scenario session(u"Everything"_s);
    ASSERT_TRUE(session.open());
    // The microphone's stand-in is a playback stream too, and System Audio
    // takes every playback stream there is: it is left out along with the
    // application the scenario leaves out, or the voice would arrive twice.
    session.add(u"system"_s,
                blocks::SystemAudio::kTypeId,
                QJsonObject{{"excluded"_L1, QJsonArray{u"binary:leftout"_s, u"binary:scenariomic"_s}}});
    session.add(u"mic"_s, blocks::InputDevice::kTypeId, QJsonObject{{"device"_L1, QString::fromLatin1(kStereoSource)}});
    session.add(u"out"_s, blocks::OutputDevice::kTypeId, QJsonObject{{"device"_L1, u"rbtest.scenario.three"_s}});
    session.link(u"system"_s, u"out"_s);
    session.link(u"mic"_s, u"out"_s);
    ASSERT_EQ(session.errors(), "");

    Microphone voice(session.pump(), kStereoSource, "scenariomic", kVoiceHz);
    ASSERT_TRUE(voice.fed()) << "the tone never reached the virtual microphone";
    ASSERT_TRUE(session.start().has_value()) << session.errors();

    FakeApp kept(toneApp("Kept", "kept", kKeptHz));
    FakeApp leftOut(toneApp("LeftOut", "leftout", kLeftOutHz));
    const QStringList blocks{u"system"_s, u"mic"_s, u"out"_s};
    ASSERT_TRUE(session.pump().pumpUntil([&] { return session.allLive(blocks); }))
        << "a block never showed a signal: " << session.firstQuiet(blocks);
    session.pump().pump(std::chrono::milliseconds{700});

    const std::map<std::string, double> keptHeard = device.measureTone(kKeptHz, 400ms);
    const std::map<std::string, double> voiceHeard = device.measureTone(kVoiceHz, 400ms);
    const std::map<std::string, double> leftOutHeard = device.measureTone(kLeftOutHz, 400ms);
    ASSERT_TRUE(keptHeard.contains("FL") && voiceHeard.contains("FL") && leftOutHeard.contains("FL"));
    std::cout << "scenario 3: kept " << keptHeard.at("FL") << ", voice " << voiceHeard.at("FL") << ", left out "
              << leftOutHeard.at("FL") << " dBFS\n";
    EXPECT_NEAR(keptHeard.at("FL"), kTonePeakDb, 0.5) << "the application System Audio keeps was not heard whole";
    EXPECT_NEAR(voiceHeard.at("FL"), kTonePeakDb, 0.5) << "the microphone was not heard whole, or was heard twice";
    EXPECT_LT(leftOutHeard.at("FL"), kTonePeakDb - 40.0) << "the application left out was heard";
    EXPECT_EQ(session.errors(), "");

    session.stop();
}

// Scenario 4: three applications and two microphones, one of them mono,
// merging into one tail.
TEST_F(Scenarios, FiveLanesMergeIntoOneTailAndEveryOneIsHeard)
{
    CaptureNode device(deviceOptions("rbtest.scenario.four"));
    ASSERT_TRUE(device.waitUntilReady().has_value());

    Scenario session(u"Lanes"_s);
    ASSERT_TRUE(session.open());
    MultiLane::build(session, u"rbtest.scenario.four"_s);
    ASSERT_EQ(session.errors(), "");

    Microphone stereo(session.pump(), kStereoSource, "lanemic0", MultiLane::kStereoMicHz);
    Microphone mono(session.pump(), kMonoSource, "lanemic1", MultiLane::kMonoMicHz);
    ASSERT_TRUE(stereo.fed() && mono.fed()) << "a tone never reached its virtual microphone";
    ASSERT_TRUE(session.start().has_value()) << session.errors();

    std::vector<std::unique_ptr<FakeApp>> apps;
    for (std::size_t index = 0; index < MultiLane::kAppHz.size(); ++index) {
        const std::string binary = "lane" + std::to_string(index);
        apps.push_back(std::make_unique<FakeApp>(toneApp(binary, binary, MultiLane::kAppHz.at(index))));
    }
    const QStringList blocks = MultiLane::sources() + MultiLane::tail() + QStringList{u"out"_s};
    ASSERT_TRUE(session.pump().pumpUntil([&] { return session.allLive(blocks); }))
        << "a block never showed a signal: " << session.firstQuiet(blocks);
    session.pump().pump(std::chrono::milliseconds{700});

    // **Every lane arrives**, each at the same share of the tail's gain:
    // they went through one compressor and one volume together, so none of
    // them may come out louder or quieter than the rest.
    std::vector<double> frequencies(MultiLane::kAppHz.begin(), MultiLane::kAppHz.end());
    frequencies.push_back(MultiLane::kStereoMicHz);
    frequencies.push_back(MultiLane::kMonoMicHz);
    std::vector<double> levels;
    for (const double hz : frequencies) {
        const std::map<std::string, double> heard = device.measureTone(hz, 400ms);
        ASSERT_TRUE(heard.contains("FL") && heard.contains("FR")) << hz;
        std::cout << "scenario 4: " << hz << " Hz at " << heard.at("FL") << " / " << heard.at("FR") << " dBFS\n";
        EXPECT_GT(heard.at("FL"), kTonePeakDb - 20.0) << "the lane at " << hz << " Hz was not heard";
        EXPECT_NEAR(heard.at("FR"), heard.at("FL"), 0.5) << "the lane at " << hz << " Hz is lopsided";
        levels.push_back(heard.at("FL"));
    }
    const auto [quietest, loudest] = std::ranges::minmax(levels);
    EXPECT_LT(loudest - quietest, 1.5) << "the lanes did not come through the tail together";

    // The tail's meter saw the sum, and the compressor took it down the
    // curve it draws.
    const std::optional<BlockTelemetry> comp = session.block(u"tail-dynamics"_s);
    ASSERT_TRUE(comp.has_value());
    EXPECT_GT(comp->values[blocks::Dynamics::kInputLevelValue], kTonePeakDb) << "the tail did not see five lanes";
    EXPECT_NEAR(comp->values[blocks::Dynamics::kOutputLevelValue],
                curveSays(MultiLane::tailDynamics(), comp->values[blocks::Dynamics::kInputLevelValue]),
                0.75);

    const std::map<std::string, double> nobody = device.measureTone(kNobodyHz, 400ms);
    EXPECT_LT(nobody.at("FL"), quietest - 30.0) << "something arrived that nobody played";
    EXPECT_EQ(session.errors(), "");

    session.stop();
}

// Wave: a generator with nothing coming in, through a meter to a
// device, heard on the side it was sent to and nowhere else.
TEST_F(Scenarios, AWaveIsHeardOnTheDeviceAtItsLevelAndOnItsSide)
{
    CaptureNode device(deviceOptions("rbtest.scenario.wave"));
    ASSERT_TRUE(device.waitUntilReady().has_value());

    Scenario session(u"Wave"_s);
    ASSERT_TRUE(session.open());
    session.add(u"wave"_s,
                blocks::Wave::kTypeId,
                QJsonObject{{"waveform"_L1, u"sine"_s}, {"frequency"_L1, 1000.0}, {"level"_L1, -12.0}});
    session.add(u"meter"_s, blocks::PeakRmsMeter::kTypeId);
    session.add(u"out"_s, blocks::OutputDevice::kTypeId, QJsonObject{{"device"_L1, u"rbtest.scenario.wave"_s}});
    session.lane({u"wave"_s, u"meter"_s, u"out"_s});
    ASSERT_EQ(session.errors(), "");
    const std::optional<Graph::Node> node = session.start();
    ASSERT_TRUE(node.has_value()) << session.errors();
    EXPECT_EQ(session.errors(), "") << "the session would not run";

    const QStringList blocks{u"wave"_s, u"meter"_s, u"out"_s};
    ASSERT_TRUE(session.pump().pumpUntil([&] { return session.allLive(blocks); }))
        << "a block never showed a signal: " << session.firstQuiet(blocks) << "; errors: " << session.errors();
    session.pump().pump(std::chrono::milliseconds{500});

    // **The Output Device's two ports and nothing else.** Listed under
    // Outputs, a Wave still sends nothing to the audio system itself, so
    // the node has no port of its own to offer anybody.
    const Graph graph = Graph::capture();
    std::vector<std::string> ports;
    for (const std::string direction : {"input", "output"}) {
        for (const Graph::Port &port : graph.portsOf(node->id, direction)) {
            ports.push_back(port.prop("port.name"));
        }
    }
    std::ranges::sort(ports);
    EXPECT_EQ(ports, (std::vector<std::string>{"out:out_FL", "out:out_FR"}));

    const std::map<std::string, double> both = device.measureTone(1000.0, 400ms);
    ASSERT_TRUE(both.contains("FL") && both.contains("FR"));
    std::cout << "wave, both: " << both.at("FL") << " / " << both.at("FR") << " dBFS\n";
    EXPECT_NEAR(both.at("FL"), -12.0, 0.2);
    EXPECT_NEAR(both.at("FR"), -12.0, 0.2);

    // To the right only: the left goes quiet, on a running session.
    session.setParam(u"wave"_s, u"channels"_s, u"right"_s);
    session.pump().pump(std::chrono::milliseconds{500});
    const std::map<std::string, double> right = device.measureTone(1000.0, 400ms);
    std::cout << "wave, right: " << right.at("FL") << " / " << right.at("FR") << " dBFS\n";
    EXPECT_LT(right.at("FL"), -80.0) << "the left side still carried a right-only tone";
    EXPECT_NEAR(right.at("FR"), -12.0, 0.2);
    EXPECT_EQ(session.errors(), "");

    session.stop();
}

// **The budget**: the multi-lane session at 48 kHz and quantum 256
// uses under 3 % of one core. Measured as the CPU time this process spends
// while the session runs, less what it spends with the same applications
// playing and the session stopped, so that the engine is charged for the
// session and not for being connected. The device is one of the harness's
// null sinks, in the daemon: a capture filter would run in this process and
// be charged to the session.
TEST_F(Scenarios, FiveLanesCostUnderThreePerCentOfOneCore)
{
    [[maybe_unused]] constexpr double kBudget = 0.03;
    // **Five seconds a window**, about nine hundred cycles at 256 frames:
    // the reading is a quarter of the budget, so a longer window
    // bought precision nobody needed at ten seconds of the suite's time.
    constexpr std::chrono::milliseconds kWindow{5000};

    ForcedClock clock;
    clock.setRate(48000);
    clock.setQuantum(256);

    Scenario session(u"Budget"_s);
    ASSERT_TRUE(session.open());
    MultiLane::build(session, QString::fromLatin1(kStereoSink));
    ASSERT_EQ(session.errors(), "");
    Microphone stereo(session.pump(), kStereoSource, "lanemic0", MultiLane::kStereoMicHz);
    Microphone mono(session.pump(), kMonoSource, "lanemic1", MultiLane::kMonoMicHz);
    ASSERT_TRUE(stereo.fed() && mono.fed());
    std::vector<std::unique_ptr<FakeApp>> apps;
    for (std::size_t index = 0; index < MultiLane::kAppHz.size(); ++index) {
        const std::string binary = "lane" + std::to_string(index);
        apps.push_back(std::make_unique<FakeApp>(toneApp(binary, binary, MultiLane::kAppHz.at(index))));
    }
    for (std::size_t index = 0; index < MultiLane::kAppHz.size(); ++index) {
        ASSERT_TRUE(session.pump().awaitMatch(playbackStreamOf("lane" + std::to_string(index))).has_value());
    }

    // What this process costs with everything playing and nothing to do.
    const double idleStart = processSeconds();
    idleFor(kWindow);
    const double idle = (processSeconds() - idleStart) / std::chrono::duration<double>(kWindow).count();

    ASSERT_TRUE(session.start().has_value()) << session.errors();
    const QStringList blocks = MultiLane::sources() + MultiLane::tail() + QStringList{u"out"_s};
    ASSERT_TRUE(session.pump().pumpUntil([&] { return session.allLive(blocks); }))
        << "a block never showed a signal: " << session.firstQuiet(blocks);
    idleFor(std::chrono::milliseconds{500});
    const TelemetrySnapshot before = session.snapshot();
    ASSERT_EQ(before.sampleRate, 48000U);
    ASSERT_EQ(before.frames, 256U) << "the graph is not running at the quantum the budget is for";

    const double runningStart = processSeconds();
    idleFor(kWindow);
    const double running = (processSeconds() - runningStart) / std::chrono::duration<double>(kWindow).count();
    const TelemetrySnapshot after = session.snapshot();
    const double cost = running - idle;

    std::cout << "five lanes at 48 kHz / 256: " << running * 100.0 << " % of a core running, " << idle * 100.0
              << " % idle, " << cost * 100.0 << " % for the session; the worst cycle took " << after.peakDspLoad * 100.0
              << " % of its period, " << (after.xruns - before.xruns) << " xruns in the window\n";
    EXPECT_EQ(session.errors(), "");
    EXPECT_TRUE(session.allLive(blocks)) << "a lane went quiet during the measurement: " << session.firstQuiet(blocks);

    session.stop();

#ifdef NDEBUG
    EXPECT_LT(cost, kBudget) << "the session costs " << cost * 100.0 << " % of a core";
#else
    GTEST_SKIP() << "the figure above is from a debug build; the budget is judged in release";
#endif
}

// Ducking: a voice on the main lane and music on the background, both
// through the real graph to a device. The music is heard at the ducked
// volume while the voice is, and back at full once the voice has stopped
// for longer than the hold and the release.
TEST_F(Scenarios, MusicIsDuckedUnderAVoiceAndComesBackWhenItStops)
{
    constexpr double kVoiceHz = 1000.0;
    constexpr double kMusicHz = 2500.0;
    CaptureNode device(deviceOptions("rbtest.scenario.ducking"));
    ASSERT_TRUE(device.waitUntilReady().has_value());

    Scenario session(u"Ducking"_s);
    ASSERT_TRUE(session.open());
    session.add(u"voice"_s,
                blocks::Wave::kTypeId,
                QJsonObject{{"waveform"_L1, u"sine"_s}, {"frequency"_L1, kVoiceHz}, {"level"_L1, -12.0}});
    session.add(u"music"_s,
                blocks::Wave::kTypeId,
                QJsonObject{{"waveform"_L1, u"sine"_s}, {"frequency"_L1, kMusicHz}, {"level"_L1, -12.0}});
    session.add(u"duck"_s, blocks::Ducking::kTypeId, QJsonObject{{"duckedVolume"_L1, 0.25}});
    session.add(u"out"_s, blocks::OutputDevice::kTypeId, QJsonObject{{"device"_L1, u"rbtest.scenario.ducking"_s}});
    session.linkInto(u"voice"_s, u"duck"_s, 0);
    session.linkInto(u"music"_s, u"duck"_s, 2);
    session.link(u"duck"_s, u"out"_s);
    ASSERT_EQ(session.errors(), "");
    ASSERT_TRUE(session.start().has_value()) << session.errors();

    const QStringList blocks{u"voice"_s, u"music"_s, u"duck"_s, u"out"_s};
    ASSERT_TRUE(session.pump().pumpUntil([&] { return session.allLive(blocks); }))
        << "a block never showed a signal: " << session.firstQuiet(blocks) << "; errors: " << session.errors();
    session.pump().pump(std::chrono::milliseconds{500});

    // The voice as it was, the music a quarter of itself: 12.04 dB down.
    const std::map<std::string, double> voiceHeard = device.measureTone(kVoiceHz, 400ms);
    const std::map<std::string, double> duckedHeard = device.measureTone(kMusicHz, 400ms);
    ASSERT_TRUE(voiceHeard.contains("FL") && duckedHeard.contains("FR"));
    const std::optional<BlockTelemetry> ducking = session.block(u"duck"_s);
    ASSERT_TRUE(ducking.has_value());
    std::cout << "ducking: voice " << voiceHeard.at("FL") << ", music under it " << duckedHeard.at("FL") << " / "
              << duckedHeard.at("FR") << " dBFS, published " << ducking->values[blocks::Ducking::kDuckingValue]
              << " dB\n";
    EXPECT_NEAR(voiceHeard.at("FL"), -12.0, 0.3) << "the voice was touched";
    EXPECT_NEAR(duckedHeard.at("FL"), -12.0 + dsp::gainToDb(0.25f), 0.3) << "the music was not ducked";
    EXPECT_NEAR(duckedHeard.at("FR"), duckedHeard.at("FL"), 0.1);
    EXPECT_NEAR(ducking->values[blocks::Ducking::kDuckingValue], dsp::gainToDb(0.25f), 0.1);
    EXPECT_FLOAT_EQ(ducking->values[blocks::Ducking::kActiveValue], 1.0f);

    // The voice stops: after the 300 ms hold and the 500 ms release, the
    // music is back at full.
    session.setParam(u"voice"_s, u"level"_s, -80.0);
    session.pump().pump(std::chrono::milliseconds{1200});
    const std::map<std::string, double> backHeard = device.measureTone(kMusicHz, 400ms);
    ASSERT_TRUE(backHeard.contains("FL"));
    std::cout << "ducking: music after the voice " << backHeard.at("FL") << " dBFS\n";
    EXPECT_NEAR(backHeard.at("FL"), -12.0, 0.3) << "the music did not come back";
    EXPECT_EQ(session.errors(), "");

    session.stop();
}

// Mixer: two tones on inputs 1 and 3 of a mixer, through the real
// graph to a device. Each is heard at its strip's volume; and a fade sent
// the way the panel sends it — the ramp, the volume, the ramp put back, in
// one edit — takes its two seconds in the real engine too, where the three
// values reach the block before the audio thread takes any of them.
TEST_F(Scenarios, TwoSourcesAreMixedAtTheirStripsLevelsAndAFadeTakesItsTime)
{
    constexpr double kFirstHz = 700.0;
    constexpr double kThirdHz = 1900.0;
    CaptureNode device(deviceOptions("rbtest.scenario.mixer"));
    ASSERT_TRUE(device.waitUntilReady().has_value());

    Scenario session(u"Mixer"_s);
    ASSERT_TRUE(session.open());
    session.add(u"first"_s,
                blocks::Wave::kTypeId,
                QJsonObject{{"waveform"_L1, u"sine"_s}, {"frequency"_L1, kFirstHz}, {"level"_L1, -12.0}});
    session.add(u"third"_s,
                blocks::Wave::kTypeId,
                QJsonObject{{"waveform"_L1, u"sine"_s}, {"frequency"_L1, kThirdHz}, {"level"_L1, -12.0}});
    session.add(u"mix"_s, blocks::Mixer::kTypeId, QJsonObject{{"volume3"_L1, 0.5}});
    session.add(u"out"_s, blocks::OutputDevice::kTypeId, QJsonObject{{"device"_L1, u"rbtest.scenario.mixer"_s}});
    session.linkInto(u"first"_s, u"mix"_s, 0);
    session.linkInto(u"third"_s, u"mix"_s, 4);
    session.link(u"mix"_s, u"out"_s);
    ASSERT_EQ(session.errors(), "");
    ASSERT_TRUE(session.start().has_value()) << session.errors();

    const QStringList blocks{u"first"_s, u"third"_s, u"mix"_s, u"out"_s};
    ASSERT_TRUE(session.pump().pumpUntil([&] { return session.allLive(blocks); }))
        << "a block never showed a signal: " << session.firstQuiet(blocks) << "; errors: " << session.errors();
    session.pump().pump(std::chrono::milliseconds{500});

    const std::map<std::string, double> first = device.measureTone(kFirstHz, 400ms);
    const std::map<std::string, double> third = device.measureTone(kThirdHz, 400ms);
    ASSERT_TRUE(first.contains("FL") && third.contains("FR"));
    std::cout << "mixer: input 1 " << first.at("FL") << ", input 3 at 50 % " << third.at("FL") << " dBFS\n";
    EXPECT_NEAR(first.at("FL"), -12.0, 0.3);
    EXPECT_NEAR(third.at("FL"), -12.0 + dsp::gainToDb(0.5f), 0.3);
    EXPECT_NEAR(third.at("FR"), third.at("FL"), 0.1);

    // A two-second fade of input 1. Half a second in, it is still clearly
    // there, where a fade that took the 20 ms left behind would be gone.
    session.setParams(u"mix"_s, {{u"ramp1"_s, 2000.0}, {u"volume1"_s, 0.0}, {u"ramp1"_s, 20.0}});
    session.pump().pump(std::chrono::milliseconds{300});
    const std::map<std::string, double> fading = device.measureTone(kFirstHz, 200ms);
    session.pump().pump(std::chrono::milliseconds{2200});
    const std::map<std::string, double> faded = device.measureTone(kFirstHz, 400ms);
    ASSERT_TRUE(fading.contains("FL") && faded.contains("FL"));
    std::cout << "mixer: input 1 fading " << fading.at("FL") << ", after the fade " << faded.at("FL") << " dBFS\n";
    EXPECT_GT(fading.at("FL"), -18.0) << "the fade was over far sooner than two seconds";
    EXPECT_LT(fading.at("FL"), -12.2) << "the fade had not started";
    EXPECT_LT(faded.at("FL"), -60.0) << "the fade did not reach silence";
    EXPECT_EQ(session.errors(), "");

    session.stop();
}

// Input Switch: two tones on A and B through the real graph. A is
// heard and B is not; a switch sent as the panel sends it — the time, the
// input, the time put back — crossfades, and afterwards B is heard alone.
TEST_F(Scenarios, AnInputSwitchPassesOneInputAndCrossfadesToTheOther)
{
    constexpr double kAHz = 700.0;
    constexpr double kBHz = 1900.0;
    CaptureNode device(deviceOptions("rbtest.scenario.switch"));
    ASSERT_TRUE(device.waitUntilReady().has_value());

    Scenario session(u"Switch"_s);
    ASSERT_TRUE(session.open());
    session.add(u"a"_s,
                blocks::Wave::kTypeId,
                QJsonObject{{"waveform"_L1, u"sine"_s}, {"frequency"_L1, kAHz}, {"level"_L1, -12.0}});
    session.add(u"b"_s,
                blocks::Wave::kTypeId,
                QJsonObject{{"waveform"_L1, u"sine"_s}, {"frequency"_L1, kBHz}, {"level"_L1, -12.0}});
    session.add(u"switch"_s, blocks::InputSwitch::kTypeId);
    session.add(u"out"_s, blocks::OutputDevice::kTypeId, QJsonObject{{"device"_L1, u"rbtest.scenario.switch"_s}});
    session.linkInto(u"a"_s, u"switch"_s, 0);
    session.linkInto(u"b"_s, u"switch"_s, 2);
    session.link(u"switch"_s, u"out"_s);
    ASSERT_EQ(session.errors(), "");
    ASSERT_TRUE(session.start().has_value()) << session.errors();

    const QStringList blocks{u"a"_s, u"b"_s, u"switch"_s, u"out"_s};
    ASSERT_TRUE(session.pump().pumpUntil([&] { return session.allLive(blocks); }))
        << "a block never showed a signal: " << session.firstQuiet(blocks) << "; errors: " << session.errors();
    session.pump().pump(std::chrono::milliseconds{500});

    const std::map<std::string, double> aBefore = device.measureTone(kAHz, 300ms);
    const std::map<std::string, double> bBefore = device.measureTone(kBHz, 300ms);
    ASSERT_TRUE(aBefore.contains("FL") && bBefore.contains("FL"));
    EXPECT_NEAR(aBefore.at("FL"), -12.0, 0.3) << "A was not passed whole";
    EXPECT_LT(bBefore.at("FL"), -60.0) << "B was heard while A was passing";

    session.setParams(u"switch"_s, {{u"crossfade"_s, 2000.0}, {u"input"_s, u"b"_s}, {u"crossfade"_s, 500.0}});
    // **A fifth of the way into the two seconds**, A is still most of what
    // is heard and B has only begun: the switch took the time it was sent
    // with, not the half second left behind it.
    session.pump().pump(std::chrono::milliseconds{300});
    const std::map<std::string, double> aDuring = device.measureTone(kAHz, 200ms);
    const std::map<std::string, double> bDuring = device.measureTone(kBHz, 200ms);
    ASSERT_TRUE(aDuring.contains("FL") && bDuring.contains("FL"));
    std::cout << "switch, part way: A " << aDuring.at("FL") << ", B " << bDuring.at("FL") << " dBFS\n";
    EXPECT_GT(aDuring.at("FL"), -15.0) << "A was gone far sooner than two seconds";
    EXPECT_LT(bDuring.at("FL"), -16.0) << "B was in far sooner than two seconds";
    EXPECT_GT(bDuring.at("FL"), -40.0) << "the crossfade had not started";
    session.pump().pump(std::chrono::milliseconds{2200});
    const std::map<std::string, double> aAfter = device.measureTone(kAHz, 300ms);
    const std::map<std::string, double> bAfter = device.measureTone(kBHz, 300ms);
    ASSERT_TRUE(aAfter.contains("FL") && bAfter.contains("FL"));
    std::cout << "switch: A " << aBefore.at("FL") << " then " << aAfter.at("FL") << ", B " << bBefore.at("FL")
              << " then " << bAfter.at("FL") << " dBFS\n";
    EXPECT_NEAR(bAfter.at("FL"), -12.0, 0.3) << "B was not passed whole after the switch";
    EXPECT_LT(aAfter.at("FL"), -60.0) << "A was still heard after the switch";
    EXPECT_EQ(session.errors(), "");

    session.stop();
}

// Latency: a Volume's limiter looks ahead 1.5 ms, which is latency.
// **The same 1 kHz tone** goes to the output two ways, through the Volume
// and straight in. Left a lookahead apart they would be a cycle and a half
// apart, opposite in phase, and cancel; lined up they add to twice the
// level, +6.02 dB. The session also says how late it is to the audio system
// as its process latency, where pw-dump shows it — and says it again when
// the Volume is taken out.
TEST_F(Scenarios, TheSessionReportsItsProcessLatencyToTheAudioSystem)
{
    CaptureNode device(deviceOptions("rbtest.scenario.latency"));
    ASSERT_TRUE(device.waitUntilReady().has_value());

    Scenario session(u"Latency"_s);
    ASSERT_TRUE(session.open());
    session.add(u"tone"_s,
                blocks::Wave::kTypeId,
                QJsonObject{{"waveform"_L1, u"sine"_s}, {"frequency"_L1, 1000.0}, {"level"_L1, -12.0}});
    session.add(u"other"_s,
                blocks::Wave::kTypeId,
                QJsonObject{{"waveform"_L1, u"sine"_s}, {"frequency"_L1, 2000.0}, {"level"_L1, -12.0}});
    session.add(u"volume"_s, blocks::Volume::kTypeId);
    session.add(u"out"_s, blocks::OutputDevice::kTypeId, QJsonObject{{"device"_L1, u"rbtest.scenario.latency"_s}});
    // Two paths into the output: one through the Volume, one straight in.
    session.lane({u"tone"_s, u"volume"_s, u"out"_s});
    session.link(u"tone"_s, u"out"_s);
    session.link(u"other"_s, u"out"_s);
    ASSERT_EQ(session.errors(), "");
    const std::optional<Graph::Node> node = session.start();
    ASSERT_TRUE(node.has_value()) << session.errors();

    const std::uint32_t lookahead = static_cast<std::uint32_t>(std::lround(1.5e-3 * 48000.0));
    // **Waited for with the event loop turning**, not blocked on: the
    // engine talks to PipeWire from this thread's loop, and a param update
    // sits in the socket until that loop runs.
    const auto reported = [&node, &session](std::uint32_t samples) {
        return session.pump().pumpUntil([&node, samples] {
            const Graph graph = Graph::capture();
            const Graph::Node *found = graph.node(node->id);
            return found != nullptr && found->processLatencySamples == samples;
        });
    };
    EXPECT_TRUE(reported(lookahead)) << "pw-dump does not show a process latency of " << lookahead;

    const QStringList blocks{u"tone"_s, u"volume"_s, u"out"_s};
    ASSERT_TRUE(session.pump().pumpUntil([&] { return session.allLive(blocks); }))
        << "a block never showed a signal: " << session.firstQuiet(blocks);
    session.pump().pump(std::chrono::milliseconds{400});
    const std::map<std::string, double> both = device.measureTone(1000.0, 300ms);
    ASSERT_TRUE(both.contains("FL"));
    std::cout << "latency: the tone two ways " << both.at("FL") << " dBFS\n";
    EXPECT_NEAR(both.at("FL"), -12.0 + 6.02, 0.3) << "the two paths did not arrive together";

    session.remove(u"volume"_s);
    EXPECT_TRUE(reported(0)) << "the latency was not taken back when the Volume went";
    const Graph after = Graph::capture();
    const Graph::Node *shown = after.node(node->id);
    ASSERT_NE(shown, nullptr);
    std::cout << "latency: " << lookahead << " samples with the Volume, " << shown->processLatencySamples.value_or(9999)
              << " without\n";
    EXPECT_EQ(session.errors(), "");
    session.stop();
}

// FIR Filter: a 2 s stereo impulse response at 48 kHz, given to the
// block while the session runs at quantum 256, loses no more cycles than
// the same session passing the sound through, and the session tells the
// audio system the 256 samples the block adds.
//
// **A quarter of that under a sanitizer**. Under AddressSanitizer the
// 2 s response took the whole of a 5.3 ms cycle on a machine at a load of
// 15, and a session that overruns every cycle keeps the data loop's thread
// from ever sleeping: after 200 ms of that the kernel kills the process
// (RLIMIT_RTTIME, rtkit's limit, the soft one equal to the hard one), which
// is how pw.scenarios was killed once with no report of any kind. What the
// criterion is about — the cost — is judged in the builds that are
// optimised; a sanitizer build still takes a response over while running.
TEST_F(Scenarios, ATwoSecondStereoImpulseResponseRunsAtQuantum256WithoutLosingCycles)
{
    constexpr std::size_t kFrames = (kSanitized ? 48000 / 4 : 2 * 48000);
    constexpr std::chrono::milliseconds kWindow{5000};
    ForcedClock clock;
    clock.setRate(48000);
    clock.setQuantum(256);

    // A hall: noise dying away by 60 dB over two seconds, a different
    // noise on each side.
    QTemporaryDir folder;
    const std::string path = folder.filePath(u"hall.wav"_s).toStdString();
    {
        SF_INFO info{};
        info.samplerate = 48000;
        info.channels = 2;
        info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
        SNDFILE *file = sf_open(path.c_str(), SFM_WRITE, &info);
        ASSERT_NE(file, nullptr);
        std::vector<float> samples(kFrames * 2);
        std::uint32_t noise = 84;
        for (std::size_t n = 0; n < samples.size(); ++n) {
            noise = noise * 1664525U + 1013904223U;
            const double decay = std::pow(10.0, -3.0 * static_cast<double>(n / 2) / kFrames);
            samples[n] = static_cast<float>((static_cast<double>(noise) / 4294967296.0 - 0.5) * decay);
        }
        ASSERT_EQ(sf_writef_float(file, samples.data(), kFrames), static_cast<sf_count_t>(kFrames));
        sf_close(file);
    }

    CaptureNode device(deviceOptions("rbtest.scenario.fir"));
    ASSERT_TRUE(device.waitUntilReady().has_value());
    Scenario session(u"FIR"_s);
    ASSERT_TRUE(session.open());
    session.add(u"tone"_s,
                blocks::Wave::kTypeId,
                QJsonObject{{"waveform"_L1, u"sine"_s}, {"frequency"_L1, 1000.0}, {"level"_L1, -12.0}});
    session.add(u"fir"_s, blocks::FirFilter::kTypeId);
    session.add(u"out"_s, blocks::OutputDevice::kTypeId, QJsonObject{{"device"_L1, u"rbtest.scenario.fir"_s}});
    session.lane({u"tone"_s, u"fir"_s, u"out"_s});
    ASSERT_EQ(session.errors(), "");
    const std::optional<Graph::Node> node = session.start();
    ASSERT_TRUE(node.has_value()) << session.errors();
    const QStringList blocks{u"tone"_s, u"fir"_s, u"out"_s};
    ASSERT_TRUE(session.pump().pumpUntil([&] { return session.allLive(blocks); }))
        << "a block never showed a signal: " << session.firstQuiet(blocks);
    EXPECT_TRUE(session.pump().pumpUntil([&node] {
        const Graph graph = Graph::capture();
        const Graph::Node *found = graph.node(node->id);
        return found != nullptr && found->processLatencySamples == blocks::FirFilter::kPartition;
    })) << "pw-dump does not show the block's 256 samples";

    // A window with the block passing the sound through, and one with the
    // response, over the same session.
    const auto window = [&session, kWindow] {
        idleFor(std::chrono::milliseconds{500});
        const TelemetrySnapshot before = session.snapshot();
        idleFor(kWindow);
        const TelemetrySnapshot after = session.snapshot();
        EXPECT_EQ(after.frames, 256U) << "the graph is not running at the quantum the criterion is for";
        return std::pair{after.xruns - before.xruns, after.peakDspLoad};
    };
    const auto [passingLost, passingPeak] = window();

    session.setParam(u"fir"_s, u"file"_s, QString::fromStdString(path));
    ASSERT_TRUE(session.pump().pumpUntil([&session] {
        const std::optional<BlockTelemetry> fir = session.block(u"fir"_s);
        return fir.has_value() && fir->values[blocks::FirFilter::kShapeValue] == 2.0f
            && fir->values[blocks::FirFilter::kTapsValue] == static_cast<float>(kFrames);
    })) << "the block never said it had the response";
    const auto [convolvingLost, convolvingPeak] = window();

    std::cout << "fir at 48 kHz / 256: passing through, " << passingLost << " cycles lost, the worst at "
              << passingPeak * 100.0f << " % of its period; with a " << static_cast<double>(kFrames) / 48000.0
              << " s stereo response, " << convolvingLost << " lost, the worst at " << convolvingPeak * 100.0f
              << " %\n";
    EXPECT_TRUE(session.allLive(blocks)) << "the lane went quiet: " << session.firstQuiet(blocks);
    EXPECT_LE(convolvingLost, passingLost + 1);
    EXPECT_LT(convolvingPeak, 1.0f);
    EXPECT_EQ(session.errors(), "");
    session.stop();
}

#ifdef RB_HAVE_RNNOISE
// Speech Denoise: its 1440 samples are the session's process latency
// in pw-dump, and a path around it is held back by as much. A 750 Hz
// tone goes to the output two ways, through the block and straight in.
// With the reduction at 0 dB the block is its input exactly, late: lined
// up the two add to +6.02 dB, and 1440 samples apart — 22.5 periods —
// they would cancel.
TEST_F(Scenarios, SpeechDenoiseReportsItsLatencyAndAPathAroundItIsLinedUp)
{
    CaptureNode device(deviceOptions("rbtest.scenario.speech"));
    ASSERT_TRUE(device.waitUntilReady().has_value());
    ForcedClock clock;
    clock.setRate(48000);

    Scenario session(u"Speech"_s);
    ASSERT_TRUE(session.open());
    session.add(u"tone"_s,
                blocks::Wave::kTypeId,
                QJsonObject{{"waveform"_L1, u"sine"_s}, {"frequency"_L1, 750.0}, {"level"_L1, -12.0}});
    session.add(u"speech"_s, blocks::SpeechDenoise::kTypeId, QJsonObject{{"reduction"_L1, 0.0}});
    session.add(u"out"_s, blocks::OutputDevice::kTypeId, QJsonObject{{"device"_L1, u"rbtest.scenario.speech"_s}});
    session.lane({u"tone"_s, u"speech"_s, u"out"_s});
    session.link(u"tone"_s, u"out"_s);
    ASSERT_EQ(session.errors(), "");
    const std::optional<Graph::Node> node = session.start();
    ASSERT_TRUE(node.has_value()) << session.errors();

    EXPECT_TRUE(session.pump().pumpUntil([&node] {
        const Graph graph = Graph::capture();
        const Graph::Node *found = graph.node(node->id);
        return found != nullptr && found->processLatencySamples == blocks::SpeechDenoise::kLatency;
    })) << "pw-dump does not show the block's 1440 samples";

    const QStringList blocks{u"tone"_s, u"speech"_s, u"out"_s};
    ASSERT_TRUE(session.pump().pumpUntil([&] { return session.allLive(blocks); }))
        << "a block never showed a signal: " << session.firstQuiet(blocks);
    session.pump().pump(std::chrono::milliseconds{400});
    const std::map<std::string, double> both = device.measureTone(750.0, 300ms);
    ASSERT_TRUE(both.contains("FL"));
    std::cout << "speech denoise: the tone two ways " << both.at("FL") << " dBFS\n";
    EXPECT_NEAR(both.at("FL"), -12.0 + 6.02, 0.3) << "the two paths did not arrive together";
    EXPECT_EQ(session.errors(), "");
    session.stop();
}
#endif
