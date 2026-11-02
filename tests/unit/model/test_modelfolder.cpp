// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What a folder is, read as the host reads it, with no process and no
// runtime: every refusal, with its reason, and the bounds on what a file may
// say. And the resampler, whose delay the block's latency is built from.

#include "model/description.h"
#include "model/minijson.h"
#include "model/onnxreader.h"
#include "model/recognise.h"
#include "model/resampler.h"
#include "model/shared.h"
#include "model/voice.h"
#include "onnxbytes.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <numbers>
#include <string>
#include <vector>

using namespace rb;
using namespace rb::model;
using namespace rb::test;

namespace
{

namespace fs = std::filesystem;

class ModelFolder : public ::testing::Test
{
protected:
    void SetUp() override
    {
        std::string pattern = (fs::temp_directory_path() / "rb-model-folder-XXXXXX").string();
        ASSERT_NE(::mkdtemp(pattern.data()), nullptr);
        m_folder = pattern;
    }

    void TearDown() override
    {
        std::error_code ignored;
        fs::remove_all(m_folder, ignored);
    }

    void write(const std::string &name, const std::string &bytes) const
    {
        std::ofstream(fs::path(m_folder) / name, std::ios::binary) << bytes;
    }

    void writeConfiguration(const std::string &body) const
    {
        write(std::string{kConfigurationName}, body);
    }

    /// A configuration in bounds, with \a extra among its fields.
    [[nodiscard]] static std::string configuration(const std::string &extra = {})
    {
        return R"({"format": 1, "file": "model.onnx", "sampleRate": 48000, "channels": 1, "frameSize": 480,
                   "input": "audio", "output": "out")"
            + extra + "}";
    }

    [[nodiscard]] Description look() const
    {
        return recognise(m_folder);
    }

    /// The three files of a voice, by these names.
    void holdVoice(const std::string &voice = "voice.onnx",
                   const std::string &content = "encoder.onnx",
                   const std::string &pitch = "estimator.onnx") const
    {
        write(voice, tinyVoice());
        write(content, tinyContentEncoder());
        write(pitch, tinyPitchEstimator(100));
    }

    std::string m_folder;
};

} // namespace

TEST_F(ModelFolder, AConfigurationDescribesAGenericModel)
{
    write("model.onnx", gainModel(0.5f, 480));
    writeConfiguration(configuration());
    write("LICENSE.txt", "terms");
    const Description found = look();
    EXPECT_EQ(found.refusal, Refusal::None);
    EXPECT_EQ(found.family, Family::Configured);
    EXPECT_EQ(found.modelFile, "model.onnx");
    EXPECT_EQ(found.sampleRate, 48000U);
    EXPECT_EQ(found.hopSize, 480U);
    EXPECT_EQ(found.licenceFile, "LICENSE.txt");
}

TEST_F(ModelFolder, TheConfigurationNamesTheLicenceFirst)
{
    write("model.onnx", gainModel(0.5f, 480));
    write("LICENSE", "x");
    write("TERMS.md", "y");
    writeConfiguration(configuration(R"(, "licence": "TERMS.md")"));
    EXPECT_EQ(look().licenceFile, "TERMS.md");
}

TEST_F(ModelFolder, WithNoLicenceFileThereIsNoneToName)
{
    write("model.onnx", gainModel(0.5f, 480));
    writeConfiguration(configuration());
    EXPECT_TRUE(look().licenceFile.empty());
}

TEST_F(ModelFolder, AFolderKeptUnderTheHomeIsFound)
{
    write("model.onnx", "x");
    const std::string parent = m_folder.substr(0, m_folder.rfind('/'));
    const std::string name = m_folder.substr(m_folder.rfind('/'));
    const char *const before = std::getenv("HOME");
    const std::string kept = before == nullptr ? std::string{} : before;
    ::setenv("HOME", parent.c_str(), 1);
    const Refusal found = recognise("~" + name).refusal;
    const std::string home = localFolder("~");
    const std::string plain = localFolder("/a/~/b");
    const std::string named = localFolder("~other/b");
    ::setenv("HOME", kept.c_str(), 1);
    EXPECT_NE(found, Refusal::NoFolder);
    EXPECT_EQ(home, parent);
    EXPECT_EQ(plain, "/a/~/b");
    EXPECT_EQ(named, "~other/b");
}

TEST_F(ModelFolder, ThatIsNotAFolderIsSaid)
{
    EXPECT_EQ(recognise(m_folder + "/not-there").refusal, Refusal::NoFolder);
    write("file", "x");
    EXPECT_EQ(recognise(m_folder + "/file").refusal, Refusal::NoFolder);
    EXPECT_EQ(recognise("").refusal, Refusal::NoFolder);
}

TEST_F(ModelFolder, AnEmptyFolderHasNothingToLoad)
{
    EXPECT_EQ(look().refusal, Refusal::NothingToLoad);
    write("notes.txt", "hello");
    EXPECT_EQ(look().refusal, Refusal::NothingToLoad);
}

TEST_F(ModelFolder, FilesThatRunCodeAreNotOpenedAndThatIsSaid)
{
    for (const char *name : {"model.pt", "model.pth", "weights.ckpt", "voice.pkl", "x.PICKLE"}) {
        write(name, "\x80\x04 anything");
    }
    EXPECT_EQ(look().refusal, Refusal::CodeFiles);
}

TEST_F(ModelFolder, AnOnnxFileBesideThemIsStillRead)
{
    write("model.pth", "pickle");
    write("model.onnx", gainModel(0.5f, 480));
    EXPECT_EQ(look().refusal, Refusal::NeedsConfiguration);
}

TEST_F(ModelFolder, GarbageIsNotAModel)
{
    write("model.onnx", std::string(4096, '\xFF'));
    EXPECT_EQ(look().refusal, Refusal::NotAModel);
}

TEST_F(ModelFolder, AnEmptyFileIsNotAModel)
{
    write("model.onnx", "");
    EXPECT_EQ(look().refusal, Refusal::NotAModel);
}

TEST_F(ModelFolder, EveryTruncationOfAModelIsRefusedOrReadInBounds)
{
    const std::string whole = gainModel(0.5f, 480);
    for (std::size_t length = 0; length < whole.size(); ++length) {
        write("model.onnx", whole.substr(0, length));
        const Description found = look();
        EXPECT_TRUE(found.refusal == Refusal::NotAModel || found.refusal == Refusal::NeedsConfiguration) << length;
    }
}

TEST_F(ModelFolder, ALengthThatRunsPastTheFileIsRefused)
{
    // The graph field says it is four gigabytes long.
    std::string bytes;
    bytes += '\x08';
    bytes += '\x08';
    bytes += '\x3A';
    bytes += "\xFF\xFF\xFF\xFF\x0F";
    bytes += "short";
    write("model.onnx", bytes);
    EXPECT_EQ(look().refusal, Refusal::NotAModel);
}

TEST_F(ModelFolder, AFileLargerThanAModelCanBeIsRefusedUnread)
{
    // Sparse: it takes no room, and reading it would take minutes.
    const fs::path path = fs::path(m_folder) / "model.onnx";
    std::ofstream(path, std::ios::binary) << "x";
    fs::resize_file(path, kMostModelBytes + 1);
    EXPECT_EQ(look().refusal, Refusal::FileTooLarge);
}

TEST_F(ModelFolder, TensorsFromOutsideTheFolderAreRefused)
{
    for (const char *location : {"../secret", "/etc/hostname", "a/../../b", "sub\\..\\..\\x"}) {
        write("model.onnx", externalDataModel(location));
        EXPECT_EQ(look().refusal, Refusal::UnsafeReference) << location;
    }
}

TEST_F(ModelFolder, TensorsBesideTheModelAreItsOwn)
{
    write("weights.bin", std::string(16, '\0'));
    write("model.onnx", externalDataModel("weights.bin"));
    EXPECT_EQ(look().refusal, Refusal::NeedsConfiguration);
}

TEST_F(ModelFolder, ALinkOutOfTheFolderIsOutOfTheFolder)
{
    write("model.onnx", externalDataModel("link"));
    fs::create_symlink("/etc/hostname", fs::path(m_folder) / "link");
    EXPECT_EQ(look().refusal, Refusal::UnsafeReference);
}

TEST_F(ModelFolder, AModelOfNoKnownFamilyNeedsAConfiguration)
{
    write("model.onnx", gainModel(0.5f, 480));
    EXPECT_EQ(look().refusal, Refusal::NeedsConfiguration);
}

TEST_F(ModelFolder, TheStreamingDeepFilterNetIsKnownByItsInputs)
{
    write("denoiser.onnx", modelWithInputs({"input_frame", "states", "atten_lim_db"}));
    const Description found = look();
    EXPECT_EQ(found.refusal, Refusal::None);
    EXPECT_EQ(found.family, Family::DeepFilterNetStream);
    EXPECT_EQ(found.sampleRate, 48000U);
    EXPECT_EQ(found.hopSize, 480U);
    EXPECT_EQ(found.state.size(), 1U);
    EXPECT_EQ(found.constants.size(), 1U);
}

TEST_F(ModelFolder, FamiliesNotBuiltYetAreNamedAndRefused)
{
    write("gtcrn.onnx", modelWithInputs({"mix", "conv_cache", "tra_cache", "inter_cache"}));
    EXPECT_EQ(look().family, Family::Cleanup);
    fs::remove(fs::path(m_folder) / "gtcrn.onnx");

    for (const char *name : {"enc.onnx", "erb_dec.onnx", "df_dec.onnx"}) {
        write(name, "not read at all");
    }
    EXPECT_EQ(look().refusal, Refusal::NotYetSupported);
    EXPECT_EQ(look().family, Family::Cleanup);
    for (const char *name : {"enc.onnx", "erb_dec.onnx", "df_dec.onnx"}) {
        fs::remove(fs::path(m_folder) / name);
    }

    write("language.gguf", "GGUF and the rest");
    EXPECT_EQ(look().refusal, Refusal::NotYetSupported);
    EXPECT_EQ(look().family, Family::Speech);
}

TEST_F(ModelFolder, ANameIsNotEnoughForAGgufFile)
{
    write("language.gguf", "something else");
    EXPECT_EQ(look().refusal, Refusal::NothingToLoad);
}

TEST_F(ModelFolder, AConfigurationThatCannotBeReadIsSaid)
{
    write("model.onnx", gainModel(0.5f, 480));
    for (const char *body : {"", "{", "[]", "not json", R"({"format": 2})"}) {
        writeConfiguration(body);
        EXPECT_EQ(look().refusal, Refusal::BadConfiguration) << body;
    }
}

TEST_F(ModelFolder, AConfigurationOutOfBoundsIsRefused)
{
    write("model.onnx", gainModel(0.5f, 480));
    const std::vector<std::string> bad{
        R"({"format": 1, "file": "model.onnx", "sampleRate": 100, "channels": 1, "frameSize": 480, "input": "audio", "output": "out"})",
        R"({"format": 1, "file": "model.onnx", "sampleRate": 48000, "channels": 9, "frameSize": 480, "input": "audio", "output": "out"})",
        R"({"format": 1, "file": "model.onnx", "sampleRate": 48000, "channels": 1, "frameSize": 1, "input": "audio", "output": "out"})",
        R"({"format": 1, "file": "model.onnx", "sampleRate": 48000, "channels": 1, "frameSize": 4000000000, "input": "audio", "output": "out"})",
        R"({"format": 1, "file": "model.onnx", "sampleRate": 48000.5, "channels": 1, "frameSize": 480, "input": "audio", "output": "out"})",
        R"({"format": 1, "file": "../model.onnx", "sampleRate": 48000, "channels": 1, "frameSize": 480, "input": "audio", "output": "out"})",
        R"({"format": 1, "file": "/etc/hostname", "sampleRate": 48000, "channels": 1, "frameSize": 480, "input": "audio", "output": "out"})",
        R"({"format": 1, "file": "notes.txt", "sampleRate": 48000, "channels": 1, "frameSize": 480, "input": "audio", "output": "out"})",
        configuration(R"(, "hopSize": 481)"),
        configuration(R"(, "threads": 1000)"),
        configuration(R"(, "latencySamples": 4000000000)"),
        configuration(R"(, "licence": "../../etc/passwd")"),
    };
    write("notes.txt", "x");
    for (const std::string &body : bad) {
        writeConfiguration(body);
        EXPECT_EQ(look().refusal, Refusal::BadConfiguration) << body;
    }
}

TEST_F(ModelFolder, AHugeConfigurationIsNotRead)
{
    write("model.onnx", gainModel(0.5f, 480));
    writeConfiguration(std::string(kMostJsonBytes + 1, ' '));
    EXPECT_EQ(look().refusal, Refusal::BadConfiguration);
}

TEST_F(ModelFolder, ADeeplyNestedConfigurationIsRefused)
{
    std::string deep(100000, '[');
    writeConfiguration(deep);
    write("model.onnx", gainModel(0.5f, 480));
    EXPECT_EQ(look().refusal, Refusal::BadConfiguration);
    EXPECT_FALSE(parseJson(deep).has_value());
}

TEST_F(ModelFolder, NamesTheModelDoesNotHaveAreAWrongShape)
{
    write("model.onnx", gainModel(0.5f, 480));
    writeConfiguration(R"({"format": 1, "file": "model.onnx", "sampleRate": 48000, "channels": 1, "frameSize": 480,
                           "input": "wrong", "output": "out"})");
    EXPECT_EQ(look().refusal, Refusal::WrongShape);
}

TEST_F(ModelFolder, AnInputLeftUnfilledIsAWrongShape)
{
    write("model.onnx", oneHopLateModel(480));
    writeConfiguration(configuration());
    EXPECT_EQ(look().refusal, Refusal::WrongShape);
}

TEST_F(ModelFolder, AConfigurationIsBelievedOverWhatTheFileLooksLike)
{
    write("model.onnx", gainModel(0.5f, 480));
    write("second.onnx", modelWithInputs({"phone", "pitch", "pitchf", "ds", "rnd"}));
    writeConfiguration(configuration());
    EXPECT_EQ(look().refusal, Refusal::None);
    EXPECT_EQ(look().family, Family::Configured);
}

TEST_F(ModelFolder, WhatTheHostSaysSurvivesTheTripToTheBlock)
{
    write("model.onnx", gainModel(0.5f, 480));
    writeConfiguration(configuration(R"(, "latencySamples": 96, "hopSize": 240)"));
    write("COPYING", "x");
    const Description found = parseDescriptionText(describeAsText(look()));
    EXPECT_EQ(found.refusal, Refusal::None);
    EXPECT_EQ(found.family, Family::Configured);
    EXPECT_EQ(found.licenceFile, "COPYING");
    EXPECT_EQ(found.hopSize, 240U);
    EXPECT_EQ(found.latencySamples, 96U);
}

TEST(ModelText, WhatIsNotADescriptionIsNotBelieved)
{
    EXPECT_EQ(parseDescriptionText("").refusal, Refusal::NoHost);
    EXPECT_EQ(parseDescriptionText("sampleRate=48000\n").refusal, Refusal::NoHost);
    EXPECT_EQ(parseDescriptionText(std::string(1 << 15, 'x')).refusal, Refusal::NoHost);
}

TEST(ModelTiming, TheLatencyAddsUp)
{
    Description same;
    same.sampleRate = 48000;
    same.hopSize = 480;
    same.latencySamples = 960;
    const std::optional<Timing> timing = timingFor(same, 48000, 2400);
    ASSERT_TRUE(timing.has_value());
    EXPECT_EQ(timing->hopFrames, 480U);
    EXPECT_EQ(timing->pathDelay, 960U);
    EXPECT_EQ(timing->latency, 3840U);
}

TEST(ModelTiming, AnotherRateCountsTheResamplers)
{
    Description low;
    low.sampleRate = 16000;
    low.hopSize = 160;
    const std::optional<Timing> timing = timingFor(low, 48000, 2400);
    ASSERT_TRUE(timing.has_value());
    EXPECT_EQ(timing->hopFrames, 480U);
    EXPECT_NEAR(timing->pathDelay,
                std::lround((Resampler::delaySeconds(48000, 16000) + Resampler::delaySeconds(16000, 48000)) * 48000.0),
                1);
    EXPECT_GT(timing->pathDelay, 0U);
}

TEST(ModelTiming, RatesWithNoResamplerAreSaid)
{
    Description odd;
    odd.sampleRate = 48000;
    odd.hopSize = 480;
    EXPECT_FALSE(timingFor(odd, 47999, 2400).has_value());
}

TEST(ModelRings, RingsMapAtBothEnds)
{
    std::expected<SharedRings, SharedError> made = SharedRings::create(1000);
    ASSERT_TRUE(made.has_value());
    EXPECT_EQ(made->capacity(), 1024U);
    made->output(1)[7] = 0.25f;
    std::expected<SharedRings, SharedError> seen = SharedRings::map(::dup(made->fd()));
    ASSERT_TRUE(seen.has_value());
    EXPECT_EQ(seen->capacity(), 1024U);
    EXPECT_EQ(seen->output(1)[7], 0.25f);
}

TEST(ModelRings, MemoryOfAnotherShapeIsRefused)
{
    std::expected<SharedRings, SharedError> made = SharedRings::create(1024);
    ASSERT_TRUE(made.has_value());
    made->header().capacity = 2048;
    EXPECT_EQ(SharedRings::map(::dup(made->fd())).error(), SharedError::WrongLayout);
    made->header().capacity = 1024;
    made->header().magic = 1;
    EXPECT_EQ(SharedRings::map(::dup(made->fd())).error(), SharedError::WrongLayout);
}

namespace
{

struct RatePair {
    std::uint32_t in;
    std::uint32_t out;
};

class ResamplerRates : public ::testing::TestWithParam<RatePair>
{
};

[[nodiscard]] std::vector<float>
throughResampler(const RatePair rates, const std::vector<float> &signal, std::size_t chunk)
{
    Resampler resampler;
    resampler.prepare(rates.in, rates.out);
    std::vector<float> out;
    for (std::size_t at = 0; at < signal.size(); at += chunk) {
        const std::size_t count = std::min(chunk, signal.size() - at);
        resampler.process(std::span<const float>{signal}.subspan(at, count), out);
    }
    return out;
}

} // namespace

TEST_P(ResamplerRates, ChunksChangeNothing)
{
    const std::vector<float> signal = whiteNoise(20000, 0.5f);
    const std::vector<float> whole = throughResampler(GetParam(), signal, signal.size());
    for (const std::size_t chunk : {1UZ, 7UZ, 64UZ, 1023UZ}) {
        const std::vector<float> cut = throughResampler(GetParam(), signal, chunk);
        ASSERT_EQ(cut.size(), whole.size()) << chunk;
        for (std::size_t index = 0; index < whole.size(); ++index) {
            ASSERT_EQ(cut[index], whole[index]) << chunk << " at " << index;
        }
    }
}

TEST_P(ResamplerRates, TheDelayIsTheOneItSays)
{
    const RatePair rates = GetParam();
    // A tone's phase says where it is, to well within a sample.
    constexpr double kHz = 700.0;
    const std::size_t frames = rates.in;
    std::vector<float> signal(frames);
    for (std::size_t n = 0; n < frames; ++n) {
        signal[n] = static_cast<float>(std::sin(2.0 * std::numbers::pi * kHz * static_cast<double>(n) / rates.in));
    }
    const std::vector<float> out = throughResampler(rates, signal, 512);
    const double omega = 2.0 * std::numbers::pi * kHz / rates.out;
    double real = 0.0;
    double imaginary = 0.0;
    const std::size_t first = out.size() / 4;
    const std::size_t last = out.size() * 3 / 4;
    for (std::size_t n = first; n < last; ++n) {
        real += out[n] * std::cos(omega * static_cast<double>(n));
        imaginary += out[n] * std::sin(omega * static_cast<double>(n));
    }
    const double phase = std::atan2(-real, imaginary);
    const double measured = phase / (2.0 * std::numbers::pi * kHz);
    const double said = Resampler::delaySeconds(rates.in, rates.out);
    const double period = 1.0 / kHz;
    const double apart = std::remainder(measured - said, period);
    EXPECT_NEAR(apart * rates.out, 0.0, 0.02);
    EXPECT_NEAR(2.0 * std::hypot(real, imaginary) / static_cast<double>(last - first), 1.0, 0.002);
}

INSTANTIATE_TEST_SUITE_P(Rates,
                         ResamplerRates,
                         ::testing::Values(RatePair{48000, 16000},
                                           RatePair{16000, 48000},
                                           RatePair{44100, 48000},
                                           RatePair{48000, 44100},
                                           RatePair{48000, 24000},
                                           RatePair{96000, 48000}));

TEST(Resampler, EqualRatesPassEveryFrameThrough)
{
    EXPECT_EQ(Resampler::delaySeconds(48000, 48000), 0.0);
    const std::vector<float> signal = whiteNoise(1000, 0.5f);
    EXPECT_EQ(throughResampler(RatePair{48000, 48000}, signal, 13), signal);
}

// **Voice conversion**: three files, told apart by what they take and give.

TEST_F(ModelFolder, ThreeFilesAreAVoiceWhateverTheyAreCalled)
{
    holdVoice("x.onnx", "y.onnx", "z.onnx");
    const Description found = look();
    ASSERT_EQ(found.refusal, Refusal::None);
    EXPECT_EQ(found.family, Family::VoiceConversion);
    EXPECT_EQ(found.modelFile, "x.onnx");
    EXPECT_EQ(found.voice.contentFile, "y.onnx");
    EXPECT_EQ(found.voice.pitchFile, "z.onnx");
    EXPECT_EQ(found.voice.contentInput, "source");
    EXPECT_TRUE(found.voice.contentMask.empty());
    EXPECT_EQ(found.voice.contentOutput, "embed");
    EXPECT_EQ(found.voice.contentBands, 768U);
    EXPECT_EQ(found.voice.features, "phone");
    EXPECT_EQ(found.voice.lengths, "phone_lengths");
    EXPECT_EQ(found.voice.speaker, "ds");
    EXPECT_EQ(found.voice.noise, "rnd");
    EXPECT_EQ(found.output, "audio");
}

TEST_F(ModelFolder, TheOtherExportsNamesAreAVoiceToo)
{
    TinyVoice voice;
    voice.otherNames = true;
    write("voice.onnx", tinyVoice(voice));
    write("encoder.onnx", tinyMaskedContentEncoder());
    write("estimator.onnx", tinyPitchEstimator(100));
    const Description found = look();
    ASSERT_EQ(found.refusal, Refusal::None);
    EXPECT_EQ(found.voice.features, "feats");
    EXPECT_EQ(found.voice.lengths, "p_len");
    EXPECT_EQ(found.voice.speaker, "sid");
    EXPECT_TRUE(found.voice.noise.empty()) << "that export makes its own";
    EXPECT_EQ(found.voice.contentInput, "input_values");
    EXPECT_EQ(found.voice.contentMask, "attention_mask");
    EXPECT_EQ(found.voice.contentOutput, "hidden_states");
}

TEST_F(ModelFolder, AVoicesRateShowsInItsGraph)
{
    for (const std::int64_t perFrame : {320, 400, 480}) {
        TinyVoice voice;
        voice.perFrame = perFrame;
        write("voice.onnx", tinyVoice(voice));
        write("encoder.onnx", tinyContentEncoder());
        write("estimator.onnx", tinyPitchEstimator(100));
        const Description found = look();
        ASSERT_EQ(found.refusal, Refusal::None);
        EXPECT_EQ(found.sampleRate, static_cast<std::uint32_t>(perFrame * 100));
        EXPECT_EQ(found.voice.samplesPerFrame, static_cast<std::uint32_t>(perFrame));
        EXPECT_EQ(found.rateIn(), kVoiceInputRate);
        EXPECT_EQ(found.hopOut(), found.hopSize / kVoiceFrame * perFrame);
    }
}

TEST_F(ModelFolder, AVoiceWhoseRateCannotBeToldAsksForAConfiguration)
{
    TinyVoice voice;
    voice.perFrame = 7;
    write("voice.onnx", tinyVoice(voice));
    write("encoder.onnx", tinyContentEncoder());
    write("estimator.onnx", tinyPitchEstimator(100));
    const Description found = look();
    EXPECT_EQ(found.refusal, Refusal::NeedsConfiguration);
    EXPECT_EQ(found.family, Family::VoiceConversion);
}

TEST_F(ModelFolder, EachFileAVoiceLacksIsNamed)
{
    write("voice.onnx", tinyVoice());
    EXPECT_EQ(look().refusal, Refusal::NoContentModel);
    EXPECT_EQ(look().family, Family::VoiceConversion);

    write("encoder.onnx", tinyContentEncoder());
    EXPECT_EQ(look().refusal, Refusal::NoPitchModel);

    write("estimator.onnx", tinyPitchEstimator(100));
    EXPECT_EQ(look().refusal, Refusal::None);

    fs::remove(fs::path(m_folder) / "voice.onnx");
    EXPECT_EQ(look().refusal, Refusal::NoVoiceModel);
    EXPECT_EQ(look().family, Family::VoiceConversion);
}

TEST_F(ModelFolder, OneSharedFileAloneAsksForAVoice)
{
    write("estimator.onnx", tinyPitchEstimator(100));
    EXPECT_EQ(look().refusal, Refusal::NoVoiceModel);
    fs::remove(fs::path(m_folder) / "estimator.onnx");
    write("encoder.onnx", tinyContentEncoder());
    EXPECT_EQ(look().refusal, Refusal::NoVoiceModel);
}

TEST_F(ModelFolder, AnEncoderOfTheWrongSizeIsSaidOf)
{
    holdVoice();
    write("encoder.onnx", tinyContentEncoder(TinyFault::None, 256));
    EXPECT_EQ(look().refusal, Refusal::ContentDoesNotFit);
}

TEST_F(ModelFolder, OfTwoEncodersTheOneThatFitsIsUsed)
{
    holdVoice();
    write("a-small.onnx", tinyContentEncoder(TinyFault::None, 256));
    const Description found = look();
    ASSERT_EQ(found.refusal, Refusal::None);
    EXPECT_EQ(found.voice.contentFile, "encoder.onnx");
}

TEST_F(ModelFolder, AVoiceTrainedWithNoPitchIsNamedAndNotRun)
{
    TinyVoice voice;
    voice.noPitch = true;
    write("voice.onnx", tinyVoice(voice));
    write("encoder.onnx", tinyContentEncoder());
    write("estimator.onnx", tinyPitchEstimator(100));
    const Description found = look();
    EXPECT_EQ(found.refusal, Refusal::NotYetSupported);
    EXPECT_EQ(found.family, Family::VoiceConversion);
}

TEST_F(ModelFolder, AVoiceWithInputsNobodyCanFillIsNamedAndNotRun)
{
    write("voice.onnx", modelWithInputs({"phone", "phone_lengths", "pitch", "pitchf", "ds", "rnd", "mystery"}));
    write("encoder.onnx", tinyContentEncoder());
    write("estimator.onnx", tinyPitchEstimator(100));
    EXPECT_EQ(look().refusal, Refusal::NotYetSupported);
}

TEST_F(ModelFolder, FilesThatCanRunCodeStayShutWhateverIsBesideThem)
{
    // A voice kept as a pickle, with the two shared files in ONNX: the
    // pickle counts for nothing, and the card asks for the voice.
    write("voice.pth", "not even opened");
    write("encoder.onnx", tinyContentEncoder());
    write("estimator.onnx", tinyPitchEstimator(100));
    EXPECT_EQ(look().refusal, Refusal::NoVoiceModel);
    fs::remove(fs::path(m_folder) / "encoder.onnx");
    fs::remove(fs::path(m_folder) / "estimator.onnx");
    EXPECT_EQ(look().refusal, Refusal::CodeFiles);

    // Beside a whole set, it is not looked at.
    holdVoice();
    EXPECT_EQ(look().refusal, Refusal::None);
    EXPECT_EQ(look().modelFile, "voice.onnx");
}

TEST_F(ModelFolder, AnyOfTheThreeThatIsBrokenIsRefused)
{
    for (const std::string name : {"voice.onnx", "encoder.onnx", "estimator.onnx"}) {
        holdVoice();
        write(name, "this is no model");
        const Description found = look();
        EXPECT_FALSE(found.usable()) << name;
    }
}

TEST_F(ModelFolder, TheSharedFilesMayBeLinks)
{
    // They run to hundreds of megabytes and are the same for every voice.
    const fs::path shared = fs::path(m_folder) / "shared";
    fs::create_directory(shared);
    write("shared/encoder.onnx", tinyContentEncoder());
    write("shared/estimator.onnx", tinyPitchEstimator(100));
    write("voice.onnx", tinyVoice());
    fs::create_symlink(shared / "encoder.onnx", fs::path(m_folder) / "encoder.onnx");
    fs::create_symlink(shared / "estimator.onnx", fs::path(m_folder) / "estimator.onnx");
    EXPECT_EQ(look().refusal, Refusal::None);
}

TEST_F(ModelFolder, AConfigurationChoosesTheFilesOfAVoice)
{
    holdVoice();
    write("spare-encoder.onnx", tinyContentEncoder());
    writeConfiguration(R"({"format": 1, "family": "voiceConversion", "file": "voice.onnx",
                           "contentModel": "spare-encoder.onnx", "pitchModel": "estimator.onnx",
                           "sampleRate": 48000, "threads": 2, "licence": "TERMS.md"})");
    const Description found = look();
    ASSERT_EQ(found.refusal, Refusal::None);
    EXPECT_EQ(found.family, Family::VoiceConversion);
    EXPECT_EQ(found.voice.contentFile, "spare-encoder.onnx");
    EXPECT_EQ(found.sampleRate, 48000U) << "over what the graph shows";
    EXPECT_EQ(found.threads, 2U);
    EXPECT_EQ(found.licenceFile, "TERMS.md");
}

TEST_F(ModelFolder, AConfigurationThatNamesAMissingFileSaysWhichKind)
{
    holdVoice();
    const std::string opening = R"({"format": 1, "family": "voiceConversion", )";
    writeConfiguration(opening
                       + R"("file": "voice.onnx", "contentModel": "gone.onnx", "pitchModel": "estimator.onnx"})");
    EXPECT_EQ(look().refusal, Refusal::NoContentModel);
    writeConfiguration(opening + R"("file": "voice.onnx", "contentModel": "encoder.onnx", "pitchModel": "gone.onnx"})");
    EXPECT_EQ(look().refusal, Refusal::NoPitchModel);
    writeConfiguration(opening
                       + R"("file": "gone.onnx", "contentModel": "encoder.onnx", "pitchModel": "estimator.onnx"})");
    EXPECT_EQ(look().refusal, Refusal::NoVoiceModel);
}

TEST_F(ModelFolder, AVoiceWhoseInputsAreOfTheWrongKindIsRefused)
{
    // The right names, and every one of them a float.
    write("voice.onnx", modelWithInputs({"phone", "phone_lengths", "pitch", "pitchf", "ds", "rnd"}));
    EXPECT_EQ(look().refusal, Refusal::WrongShape);
    EXPECT_EQ(look().family, Family::VoiceConversion);
}

TEST_F(ModelFolder, AConfigurationOfAVoiceIsHeldToItsBounds)
{
    holdVoice();
    const std::string opening = R"({"format": 1, "family": "voiceConversion", )";
    for (
        const std::string &rest :
        {std::string{R"("file": "voice.onnx", "pitchModel": "estimator.onnx"})"},
         std::string{R"("file": "voice.onnx", "contentModel": "../encoder.onnx", "pitchModel": "estimator.onnx"})"},
         std::string{R"("file": "/etc/passwd", "contentModel": "encoder.onnx", "pitchModel": "estimator.onnx"})"},
         std::string{
             R"("file": "voice.onnx", "contentModel": "encoder.onnx", "pitchModel": "estimator.onnx", "sampleRate": 44150})"},
         std::string{
             R"("file": "voice.onnx", "contentModel": "encoder.onnx", "pitchModel": "estimator.onnx", "threads": 99})"}}) {
        writeConfiguration(opening + rest);
        EXPECT_EQ(look().refusal, Refusal::BadConfiguration) << rest;
    }
}

TEST_F(ModelFolder, AConfigurationCannotMakeAVoiceOfSomethingElse)
{
    holdVoice();
    writeConfiguration(R"({"format": 1, "family": "voiceConversion", "file": "encoder.onnx",
                           "contentModel": "encoder.onnx", "pitchModel": "estimator.onnx"})");
    EXPECT_EQ(look().refusal, Refusal::WrongShape);
    writeConfiguration(R"({"format": 1, "family": "voiceConversion", "file": "voice.onnx",
                           "contentModel": "estimator.onnx", "pitchModel": "estimator.onnx"})");
    EXPECT_EQ(look().refusal, Refusal::WrongShape);
}

TEST_F(ModelFolder, AVoiceReachesTheBlockAsTextWithItsTwoRates)
{
    holdVoice();
    const Description found = look();
    ASSERT_TRUE(found.usable());
    const Description read = parseDescriptionText(describeAsText(found));
    EXPECT_EQ(read.family, Family::VoiceConversion);
    EXPECT_EQ(read.voice.contentFile, "encoder.onnx");
    EXPECT_EQ(read.voice.pitchFile, "estimator.onnx");
    EXPECT_EQ(read.sampleRate, 40000U);
    EXPECT_EQ(read.rateIn(), kVoiceInputRate);
    EXPECT_EQ(read.hopOut(), found.hopOut());
    EXPECT_EQ(read.runMs, found.runMs);
    const std::optional<Timing> sent = timingFor(found, 48000, 2400);
    const std::optional<Timing> got = timingFor(read, 48000, 2400);
    ASSERT_TRUE(sent && got);
    EXPECT_EQ(sent->latency, got->latency);
}

TEST(ModelTiming, AVoicesRunIsPartOfTheDelayAndNotOfWhatIsDropped)
{
    Description voice;
    voice.sampleRate = 40000;
    voice.inputRate = 16000;
    voice.hopSize = 2560;
    voice.outputHop = 6400;
    voice.latencySamples = 2400;
    Description same = voice;
    same.runMs = 0;
    voice.runMs = 100;
    const std::optional<Timing> with = timingFor(voice, 48000, 2400);
    const std::optional<Timing> without = timingFor(same, 48000, 2400);
    ASSERT_TRUE(with && without);
    EXPECT_EQ(with->hopFrames, 7680U) << "a hop is counted at the rate it goes in";
    EXPECT_EQ(with->pathDelay, without->pathDelay);
    EXPECT_EQ(with->latency, without->latency + 4800);
    // Both resamplers count, each at its own rate.
    const double delay = Resampler::delaySeconds(48000, 16000) + Resampler::delaySeconds(40000, 48000) + 0.06;
    EXPECT_NEAR(with->pathDelay, delay * 48000.0, 0.51);
}

TEST(VoiceBands, ATonePeaksInItsOwnBand)
{
    VoiceBands bands;
    const std::size_t samples = 8000;
    const std::size_t frames = VoiceBands::frames(samples);
    EXPECT_EQ(frames, 51U);
    // Bands get wider as they rise, so a high tone needs its own check.
    std::array<std::size_t, 2> peak{};
    std::array<double, 2> tones{200.0, 2000.0};
    for (std::size_t tone = 0; tone < tones.size(); ++tone) {
        std::vector<float> sound(samples);
        for (std::size_t n = 0; n < samples; ++n) {
            sound[n] = 0.5f * static_cast<float>(std::sin(2.0 * std::numbers::pi * tones[tone] * n / 16000.0));
        }
        std::vector<float> out(frames * kVoiceBands);
        bands.analyse(sound, out);
        float highest = -100.0f;
        for (std::size_t band = 0; band < kVoiceBands; ++band) {
            const float value = out[band * frames + frames / 2];
            if (value > highest) {
                highest = value;
                peak[tone] = band;
            }
        }
    }
    // The mel scale from 30 Hz to 8 kHz puts them near bands 10 and 67.
    EXPECT_GE(peak[0], 9U);
    EXPECT_LE(peak[0], 12U);
    EXPECT_GE(peak[1], 66U);
    EXPECT_LE(peak[1], 69U);
}

TEST(VoiceBands, SilenceIsTheFloorAndTheEndsAreMirrored)
{
    VoiceBands bands;
    const std::vector<float> quiet(1600, 0.0f);
    const std::size_t frames = VoiceBands::frames(quiet.size());
    std::vector<float> out(frames * kVoiceBands, 1.0f);
    bands.analyse(quiet, out);
    for (const float value : out) {
        EXPECT_NEAR(value, std::log(1e-5f), 1e-3f);
    }
    // A sound shorter than a window does not reach past its own ends.
    const std::vector<float> tiny(100, 0.5f);
    std::vector<float> few(VoiceBands::frames(tiny.size()) * kVoiceBands);
    bands.analyse(tiny, few);
    EXPECT_TRUE(std::ranges::all_of(few, [](float value) { return std::isfinite(value); }));
}

TEST(VoicePitch, ALikelihoodIsAPitch)
{
    std::vector<float> likely(kVoicePitches, 0.0f);
    // Bin 0 is 10 Hz up by 1997.38 cents, and each bin a fifth of a semitone.
    likely[0] = 1.0f;
    EXPECT_NEAR(pitchFromLikelihoods(likely), 31.7, 0.05);
    likely[0] = 0.0f;
    likely[195] = 1.0f;
    EXPECT_NEAR(pitchFromLikelihoods(likely), 31.7 * std::pow(2.0, 195.0 * 20.0 / 1200.0), 0.5);
    // Halfway between two bins.
    likely[196] = 1.0f;
    EXPECT_NEAR(pitchFromLikelihoods(likely), 31.7 * std::pow(2.0, 195.5 * 20.0 / 1200.0), 0.5);
}

TEST(VoicePitch, WhatIsNotSureIsNoPitch)
{
    std::vector<float> likely(kVoicePitches, 0.0f);
    likely[100] = 0.02f;
    EXPECT_EQ(pitchFromLikelihoods(likely), 0.0f);
    EXPECT_EQ(pitchFromLikelihoods({}), 0.0f);
    likely[100] = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(pitchFromLikelihoods(likely), 0.0f);
}

TEST(VoicePitch, ShiftsAreInCentsAndLeaveNoPitchAlone)
{
    EXPECT_NEAR(shiftedPitch(220.0f, 1200.0f), 440.0f, 1e-3f);
    EXPECT_NEAR(shiftedPitch(220.0f, -1200.0f), 110.0f, 1e-3f);
    EXPECT_EQ(shiftedPitch(0.0f, 1200.0f), 0.0f);
}

TEST(VoicePitch, TheStepsCoverTheirRangeAndStayOnIt)
{
    EXPECT_EQ(pitchStep(0.0f), 1);
    EXPECT_EQ(pitchStep(50.0f), 1);
    EXPECT_EQ(pitchStep(1100.0f), 255);
    EXPECT_EQ(pitchStep(5000.0f), 255);
    EXPECT_EQ(pitchStep(1.0f), 1);
    EXPECT_GT(pitchStep(200.0f), pitchStep(100.0f));
}
