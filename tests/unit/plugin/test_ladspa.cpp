// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// LADSPA plugins as the host runs them, with no host process and no
// server: what a scan says of a library's plugins, and a plugin run as a
// block. Against the plugins tests/ladspa builds, and against the LADSPA
// SDK's amplifier where it is installed.

#include "allocationtrap.h"
#include "ladspaplugin.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

using namespace rb;
using namespace rb::pluginhost;
namespace fs = std::filesystem;

namespace
{

const fs::path kTestLibrary = fs::path{RB_TEST_LADSPA_DIR} / "rb-test-ladspa.so";

/// LADSPA_PATH as a test wants it, and as it was afterwards.
class LadspaPath
{
public:
    explicit LadspaPath(const std::string &value)
    {
        if (const char *before = std::getenv("LADSPA_PATH"); before != nullptr) {
            m_before = before;
        }
        ::setenv("LADSPA_PATH", value.c_str(), 1);
    }
    ~LadspaPath()
    {
        if (m_before) {
            ::setenv("LADSPA_PATH", m_before->c_str(), 1);
        } else {
            ::unsetenv("LADSPA_PATH");
        }
    }
    LadspaPath(const LadspaPath &) = delete;
    LadspaPath &operator=(const LadspaPath &) = delete;

private:
    std::optional<std::string> m_before;
};

[[nodiscard]] std::shared_ptr<LadspaLibrary> testLibrary()
{
    std::string error;
    std::shared_ptr<LadspaLibrary> library = LadspaLibrary::open(kTestLibrary, error);
    EXPECT_TRUE(library) << error;
    return library;
}

[[nodiscard]] std::unique_ptr<LadspaPlugin> testPlugin(std::string_view label,
                                                       LadspaPlugin::Mono mono = LadspaPlugin::Mono::DualMono)
{
    const std::shared_ptr<LadspaLibrary> library = testLibrary();
    if (!library) {
        return nullptr;
    }
    const ladspa::Descriptor *descriptor = library->plugin(label);
    EXPECT_NE(descriptor, nullptr) << label;
    return LadspaPlugin::make(library, descriptor, mono);
}

constexpr std::uint32_t kFrames = 256;

[[nodiscard]] bool prepared(dsp::Block &block)
{
    dsp::PrepareSpec spec;
    spec.sampleRate = 48000.0;
    spec.maxBlockFrames = kFrames;
    spec.inputChannels = block.info().inputs.maxCount;
    spec.outputChannels = block.info().outputs.maxCount;
    return block.prepare(spec).has_value();
}

/// One cycle of \a block with \a left and \a right held steady on its two
/// inputs; what comes out of each side, first sample.
struct Heard {
    float left = 0.0f;
    float right = 0.0f;
};
[[nodiscard]] Heard runOnce(dsp::Block &block, float left, float right)
{
    std::array<std::vector<float>, 2> in{std::vector<float>(kFrames, left), std::vector<float>(kFrames, right)};
    std::array<std::vector<float>, 2> out{std::vector<float>(kFrames, 9.0f), std::vector<float>(kFrames, 9.0f)};
    std::array<float *, 2> inputs{in[0].data(), in[1].data()};
    std::array<float *, 2> outputs{out[0].data(), out[1].data()};
    dsp::ProcessContext context;
    context.input = dsp::AudioBlock(inputs.data(), 2, kFrames);
    context.output = dsp::AudioBlock(outputs.data(), 2, kFrames);
    context.frames = kFrames;
    {
        const test::NoAllocations trap;
        block.process(context);
    }
    return Heard{out[0][kFrames - 1], out[1][kFrames - 1]};
}

[[nodiscard]] const plugin::PluginParam *paramNamed(const std::vector<plugin::PluginParam> &params, std::string_view id)
{
    const auto found = std::ranges::find(params, id, &plugin::PluginParam::id);
    return found == params.end() ? nullptr : &*found;
}

} // namespace

TEST(Ladspa, ALibrarysPluginsAreDescribedByTheirPorts)
{
    const std::shared_ptr<LadspaLibrary> library = testLibrary();
    ASSERT_TRUE(library);
    ASSERT_EQ(library->plugins().size(), 3U);
    const ladspa::Descriptor *stereo = library->plugin("rb_stereo_gain");
    ASSERT_NE(stereo, nullptr);

    const plugin::PluginDescription description = describeLadspa(kTestLibrary, *stereo);
    EXPECT_EQ(description.format, "ladspa");
    EXPECT_EQ(description.id, "rb-test-ladspa.so:rb_stereo_gain") << "the library's name and the plugin's label";
    EXPECT_EQ(description.name, "Test Stereo Gain");
    EXPECT_EQ(description.vendor, "Reverblabr");
    EXPECT_EQ(description.audioInputs, 2U);
    EXPECT_EQ(description.audioOutputs, 2U);
    ASSERT_EQ(description.params.size(), 1U);
    EXPECT_EQ(description.parameters, 1U);
    EXPECT_EQ(description.params[0].id, "gain");
    EXPECT_EQ(description.params[0].name, "Gain");
    EXPECT_EQ(description.params[0].minimum, 0.0f);
    EXPECT_EQ(description.params[0].maximum, 4.0f);
    EXPECT_EQ(description.params[0].defaultValue, 1.0f);

    EXPECT_EQ(library->plugin("no_such_label"), nullptr);
}

TEST(Ladspa, EveryRangeHintBecomesTheSettingItDescribes)
{
    const std::shared_ptr<LadspaLibrary> library = testLibrary();
    ASSERT_TRUE(library);
    const std::vector<plugin::PluginParam> params = ladspaParams(*library->plugin("rb_hints"));
    ASSERT_EQ(params.size(), 7U) << "the control inputs, and not the latency output";
    using Kind = plugin::PluginParam::Kind;

    // A fraction of the rate, described at 48 kHz; logarithmic; 440.
    const plugin::PluginParam *frequency = paramNamed(params, "frequency_hz");
    ASSERT_NE(frequency, nullptr) << "\"Frequency (Hz)\" made into an id";
    EXPECT_EQ(frequency->kind, Kind::Float);
    EXPECT_FLOAT_EQ(frequency->minimum, 4.8f);
    EXPECT_FLOAT_EQ(frequency->maximum, 24000.0f);
    EXPECT_FLOAT_EQ(frequency->defaultValue, 440.0f);
    EXPECT_TRUE(frequency->logarithmic);

    // Whole numbers, the middle of 1 to 8, rounded.
    const plugin::PluginParam *steps = paramNamed(params, "steps");
    ASSERT_NE(steps, nullptr);
    EXPECT_EQ(steps->kind, Kind::Int);
    EXPECT_EQ(steps->minimum, 1.0f);
    EXPECT_EQ(steps->maximum, 8.0f);
    EXPECT_EQ(steps->defaultValue, 5.0f);

    const plugin::PluginParam *bypass = paramNamed(params, "bypass");
    ASSERT_NE(bypass, nullptr);
    EXPECT_EQ(bypass->kind, Kind::Toggle);
    EXPECT_EQ(bypass->minimum, 0.0f);
    EXPECT_EQ(bypass->maximum, 1.0f);
    EXPECT_EQ(bypass->defaultValue, 0.0f);

    // "Low" on a logarithmic scale from 0.01 to 100 is 0.1, not 25.
    const plugin::PluginParam *level = paramNamed(params, "level");
    ASSERT_NE(level, nullptr);
    EXPECT_NEAR(level->defaultValue, 0.1f, 1e-4f);
    EXPECT_TRUE(level->logarithmic);

    // Two ports of one name are two settings.
    const plugin::PluginParam *gain = paramNamed(params, "gain");
    const plugin::PluginParam *secondGain = paramNamed(params, "gain_2");
    ASSERT_NE(gain, nullptr);
    ASSERT_NE(secondGain, nullptr);
    EXPECT_EQ(gain->name, "Gain");
    EXPECT_EQ(secondGain->name, "Gain");
    EXPECT_EQ(gain->defaultValue, 2.0f) << "its maximum";
    EXPECT_EQ(secondGain->defaultValue, -1.0f) << "its minimum";

    // No hints at all: from nothing to one.
    const plugin::PluginParam *unbounded = paramNamed(params, "unbounded");
    ASSERT_NE(unbounded, nullptr);
    EXPECT_EQ(unbounded->minimum, 0.0f);
    EXPECT_EQ(unbounded->maximum, 1.0f);
    EXPECT_EQ(unbounded->defaultValue, 0.0f);
}

TEST(Ladspa, AStereoPluginRunsAsABlockAndHearsItsSettings)
{
    std::unique_ptr<LadspaPlugin> plugin = testPlugin("rb_stereo_gain");
    ASSERT_TRUE(plugin);
    EXPECT_EQ(plugin->info().typeId, "ladspa");
    EXPECT_EQ(plugin->info().inputs.maxCount, 2U);
    EXPECT_EQ(plugin->info().outputs.maxCount, 2U);
    ASSERT_EQ(plugin->params().size(), 1U);
    ASSERT_TRUE(prepared(*plugin));
    EXPECT_EQ(plugin->instanceCount(), 1U);

    Heard heard = runOnce(*plugin, 0.5f, -0.25f);
    EXPECT_FLOAT_EQ(heard.left, 0.5f) << "the default gain, 1";
    EXPECT_FLOAT_EQ(heard.right, -0.25f);

    plugin->applyParam(0, 0.25f);
    heard = runOnce(*plugin, 0.5f, -0.25f);
    EXPECT_FLOAT_EQ(heard.left, 0.125f);
    EXPECT_FLOAT_EQ(heard.right, -0.0625f);

    // **Its state is its control values**, and a new plugin given it sounds
    // the same.
    const dsp::ParamValueMap state = plugin->saveState();
    ASSERT_EQ(state.size(), 1U);
    EXPECT_FLOAT_EQ(static_cast<float>(state.at("gain").asNumber().value_or(0.0)), 0.25f);
    std::unique_ptr<LadspaPlugin> again = testPlugin("rb_stereo_gain");
    ASSERT_TRUE(again);
    ASSERT_TRUE(again->loadState(state).has_value());
    ASSERT_TRUE(prepared(*again)) << "put back before it is made, as the host does";
    EXPECT_FLOAT_EQ(runOnce(*again, 0.5f, 0.5f).left, 0.125f);
    EXPECT_FLOAT_EQ(static_cast<float>(again->param("gain").asNumber().value_or(0.0)), 0.25f);
}

TEST(Ladspa, AMonoPluginRunsOnceASideOrOnTheSidesMixed)
{
    std::unique_ptr<LadspaPlugin> dual = testPlugin("rb_mono_gain", LadspaPlugin::Mono::DualMono);
    ASSERT_TRUE(dual);
    EXPECT_EQ(dual->info().inputs.maxCount, 2U) << "a stereo pair of ports either way";
    ASSERT_TRUE(prepared(*dual));
    EXPECT_EQ(dual->instanceCount(), 2U);
    dual->applyParam(0, 2.0f);
    Heard heard = runOnce(*dual, 0.2f, -0.4f);
    EXPECT_FLOAT_EQ(heard.left, 0.4f) << "each side as itself";
    EXPECT_FLOAT_EQ(heard.right, -0.8f);

    std::unique_ptr<LadspaPlugin> mixed = testPlugin("rb_mono_gain", LadspaPlugin::Mono::Downmix);
    ASSERT_TRUE(mixed);
    ASSERT_TRUE(prepared(*mixed));
    EXPECT_EQ(mixed->instanceCount(), 1U);
    mixed->applyParam(0, 2.0f);
    heard = runOnce(*mixed, 0.2f, -0.4f);
    EXPECT_FLOAT_EQ(heard.left, -0.2f) << "the sides mixed, on both";
    EXPECT_FLOAT_EQ(heard.right, -0.2f);
}

TEST(Ladspa, APluginThatCannotWriteOverItsInputIsGivenACopy)
{
    // The hints plugin clears its output before it reads its input: run on
    // one buffer for both, it hears silence.
    std::unique_ptr<LadspaPlugin> plugin = testPlugin("rb_hints");
    ASSERT_TRUE(plugin);
    ASSERT_TRUE(prepared(*plugin));
    std::array<std::vector<float>, 2> buffers{std::vector<float>(kFrames, 0.5f), std::vector<float>(kFrames, 0.5f)};
    std::array<float *, 2> channels{buffers[0].data(), buffers[1].data()};
    dsp::ProcessContext context;
    context.input = dsp::AudioBlock(channels.data(), 2, kFrames);
    context.output = dsp::AudioBlock(channels.data(), 2, kFrames);
    context.frames = kFrames;
    plugin->process(context);
    EXPECT_FLOAT_EQ(buffers[0][kFrames - 1], 0.5f) << "the plugin read what it had cleared";
    EXPECT_FLOAT_EQ(buffers[1][kFrames - 1], 0.5f);
}

TEST(Ladspa, TheLatencyItReportsIsTaken)
{
    std::unique_ptr<LadspaPlugin> plugin = testPlugin("rb_hints");
    ASSERT_TRUE(plugin);
    ASSERT_TRUE(prepared(*plugin));
    plugin->refreshLatency();
    EXPECT_EQ(plugin->info().latencySamples, 0U) << "nothing said before it has run";
    (void)runOnce(*plugin, 0.1f, 0.1f);
    plugin->refreshLatency();
    EXPECT_EQ(plugin->info().latencySamples, 64U) << "its \"latency\" output";
}

TEST(Ladspa, APluginIsFoundByItsLibraryAndLabelWhereTheFormatLooks)
{
    const LadspaPath path(RB_TEST_LADSPA_DIR);
    const std::optional<LadspaLocation> found = locateLadspa("rb-test-ladspa.so:rb_mono_gain");
    ASSERT_TRUE(found);
    EXPECT_EQ(found->library, kTestLibrary);
    EXPECT_EQ(found->label, "rb_mono_gain");
    EXPECT_TRUE(LadspaPlugin::make("rb-test-ladspa.so:rb_mono_gain", LadspaPlugin::Mono::DualMono));

    // A whole path is taken as it is, and a label may have colons of its
    // own (LSP's are URLs).
    const std::optional<LadspaLocation> whole = locateLadspa(kTestLibrary.string() + ":http://example.org/a:b");
    ASSERT_TRUE(whole);
    EXPECT_EQ(whole->library, kTestLibrary);
    EXPECT_EQ(whole->label, "http://example.org/a:b");

    EXPECT_FALSE(LadspaPlugin::make("rb-test-ladspa.so:no_such_label", LadspaPlugin::Mono::DualMono));
    EXPECT_FALSE(locateLadspa("elsewhere.so:rb_mono_gain")) << "not a library on the path";
    EXPECT_FALSE(locateLadspa("rb_mono_gain")) << "no library";
    EXPECT_FALSE(locateLadspa("rb-test-ladspa.so:")) << "no label";
}

TEST(Ladspa, TheSdksStereoAmplifierAmplifies)
{
    // **A common LADSPA plugin**: the SDK's example amplifier, where the
    // machine has it (the "ladspa" package) — gain "0 to ..., 1".
    const std::optional<LadspaLocation> amp = locateLadspa("amp.so:amp_stereo");
    if (!amp) {
        GTEST_SKIP() << "the LADSPA SDK's plugins are not installed (ladspa)";
    }
    std::unique_ptr<LadspaPlugin> plugin = LadspaPlugin::make("amp.so:amp_stereo", LadspaPlugin::Mono::DualMono);
    ASSERT_TRUE(plugin);
    ASSERT_EQ(plugin->params().size(), 1U);
    EXPECT_EQ(plugin->params()[0].id, "gain");
    // **Open above**: the panel's range goes to twice the default, not to 1.
    std::string error;
    const std::shared_ptr<LadspaLibrary> library = LadspaLibrary::open(amp->library, error);
    ASSERT_TRUE(library) << error;
    const std::vector<plugin::PluginParam> params = ladspaParams(*library->plugin("amp_stereo"));
    ASSERT_EQ(params.size(), 1U);
    EXPECT_EQ(params[0].minimum, 0.0f);
    EXPECT_EQ(params[0].maximum, 2.0f);
    EXPECT_EQ(params[0].defaultValue, 1.0f);
    ASSERT_TRUE(prepared(*plugin));
    EXPECT_FLOAT_EQ(runOnce(*plugin, 0.25f, -0.25f).left, 0.25f);
    plugin->applyParam(0, 2.0f);
    const Heard heard = runOnce(*plugin, 0.25f, -0.25f);
    EXPECT_FLOAT_EQ(heard.left, 0.5f);
    EXPECT_FLOAT_EQ(heard.right, -0.5f);
}
