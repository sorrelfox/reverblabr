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
    write("voice.onnx", modelWithInputs({"phone", "phone_lengths", "pitch", "pitchf", "ds", "rnd"}));
    EXPECT_EQ(look().refusal, Refusal::NotYetSupported);
    EXPECT_EQ(look().family, Family::VoiceConversion);
    fs::remove(fs::path(m_folder) / "voice.onnx");

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
