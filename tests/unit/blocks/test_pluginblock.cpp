// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The plugin block: what the engine keeps of a plugin that runs in a
// host of its own.

#include "blocks/pluginblock.h"
#include "plugin/formats.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace rb;
using namespace rb::blocks;
using rb::dsp::ParamValue;

namespace
{

[[nodiscard]] std::vector<std::string> paramIds(const dsp::Block &block)
{
    std::vector<std::string> ids;
    for (const dsp::ParamDescriptor &descriptor : block.params()) {
        ids.push_back(descriptor.id);
    }
    return ids;
}

} // namespace

TEST(PluginBlockTest, ItsParametersAreItsOwnThenThePlugins)
{
    PluginBlock block;
    EXPECT_EQ(block.info().category, dsp::BlockCategory::Plugins);
    EXPECT_EQ(block.pluginId(), "test.gain");
    EXPECT_EQ(paramIds(block), (std::vector<std::string>{"plugin", "state", "builtinLatency", "monoMode", "gain"}));
    EXPECT_FALSE(PluginBlock::pluginParamIndex(3).has_value());
    EXPECT_EQ(PluginBlock::pluginParamIndex(4), 0U);
    // The host's ports, by the names the host gives them.
    EXPECT_EQ(block.info().inputs.label(0), "in_FL");
    EXPECT_EQ(block.info().outputs.label(1), "out_FR");
    // The plugin's parameter goes to the host as it moves; the block's own
    // change the host.
    EXPECT_TRUE(block.params()[4].flags.automatable);
    EXPECT_FALSE(block.params()[0].flags.automatable);
}

TEST(PluginBlockTest, APluginThisBuildDoesNotHaveKeepsItsNameAndAStereoPair)
{
    PluginBlock block;
    ASSERT_TRUE(block.setParam("plugin", ParamValue::ofString("test.elsewhere")).has_value());
    EXPECT_EQ(block.pluginId(), "test.elsewhere");
    EXPECT_EQ(paramIds(block), (std::vector<std::string>{"plugin", "state", "builtinLatency", "monoMode"}));
    EXPECT_EQ(block.info().inputs.maxCount, 2U);
    EXPECT_EQ(block.info().outputs.maxCount, 2U);
}

TEST(PluginBlockTest, WhatWasSetIsWhatComesBack)
{
    PluginBlock block;
    ASSERT_TRUE(block.setParam("gain", ParamValue::ofFloat(2.5f)).has_value());
    ASSERT_TRUE(block.setParam("state", ParamValue::ofString("gain=2.5\n")).has_value());
    ASSERT_TRUE(block.setParam("builtinLatency", ParamValue::ofInt(256)).has_value());

    PluginBlock opened;
    ASSERT_TRUE(opened.loadState(block.saveState()).has_value());
    EXPECT_FLOAT_EQ(*opened.param("gain").asFloat(), 2.5f);
    EXPECT_EQ(opened.state(), "gain=2.5\n");
    EXPECT_EQ(opened.builtinLatency(), 256U);
    EXPECT_EQ(opened.pluginId(), "test.gain");
}

TEST(PluginBlockTest, ItsLatencyIsWhatTheHostReported)
{
    PluginBlock block;
    EXPECT_EQ(block.info().latencySamples, 0U);
    block.setReportedLatency(480);
    EXPECT_EQ(block.info().latencySamples, 480U);
}

namespace
{

/// A plugin as a scan would describe it, for the plugin block to ask about.
class Described
{
public:
    Described()
    {
        rb::plugin::setPluginInfoSource([](std::string_view format,
                                           std::string_view id) -> std::optional<rb::plugin::PluginDescription> {
            using Kind = rb::plugin::PluginParam::Kind;
            if (format == "clap" && id == "org.example.comp") {
                // As LSP's CLAP plugins describe a choice: fractions of 0
                // to 1, and a default between two of them.
                rb::plugin::PluginDescription described;
                described.format = "clap";
                described.id = std::string{id};
                described.audioInputs = 2;
                described.audioOutputs = 2;
                described.params = {
                    {"3630838676", "Output gain", Kind::Float, -80.0f, 60.0f, 0.0f, false, {}},
                    {"3810080779",
                     "Sidechain mode",
                     Kind::Choice,
                     0.0f,
                     1.0f,
                     0.333333343f,
                     false,
                     {{0.0f, "Peak"}, {0.5f, "RMS"}, {0.833007812f, "LPF"}, {1.0f, "SMA"}}},
                };
                return described;
            }
            if (format != "lv2" || (id != "urn:test:mono" && id != "urn:test:stereo")) {
                return std::nullopt;
            }
            rb::plugin::PluginDescription described;
            described.format = "lv2";
            described.id = std::string{id};
            described.audioInputs = id == "urn:test:mono" ? 1 : 2;
            described.audioOutputs = described.audioInputs;
            described.params = {
                {"freq", "Frequency", Kind::Float, 20.0f, 20000.0f, 1000.0f, true, {}},
                {"level", "Level", Kind::Float, 0.0f, 10.0f, 1.0f, true, {}},
                {"bands", "Bands", Kind::Int, 1.0f, 8.0f, 4.0f, false, {}},
                {"on", "On", Kind::Toggle, 0.0f, 1.0f, 1.0f, false, {}},
                {"mode", "Mode", Kind::Choice, 0.0f, 2.0f, 1.0f, false, {{0.0f, "Peak"}, {1.0f, "RMS"}, {2.5f, "LPF"}}},
            };
            return described;
        });
    }
    ~Described()
    {
        rb::plugin::setPluginInfoSource({});
    }
};

} // namespace

TEST(PluginBlockTest, AnLv2PluginsSettingsAreWhatItsScanSays)
{
    const Described described;
    PluginBlock block("lv2");
    EXPECT_EQ(block.info().typeId, "plugin.lv2");
    EXPECT_EQ(block.pluginId(), "") << "an LV2 block starts with no plugin chosen";
    ASSERT_TRUE(block.setParam("plugin", ParamValue::ofString("urn:test:stereo")).has_value());
    EXPECT_EQ(paramIds(block),
              (std::vector<std::string>{
                  "plugin", "state", "builtinLatency", "monoMode", "freq", "level", "bands", "on", "mode"}));
    const auto params = block.params();
    EXPECT_EQ(params[4].type, dsp::ParamType::Float);
    EXPECT_EQ(params[4].taper, dsp::ParamTaper::Logarithmic);
    EXPECT_EQ(params[5].taper, dsp::ParamTaper::Linear) << "logarithmic from zero is not a taper there is";
    EXPECT_EQ(params[6].type, dsp::ParamType::Int);
    EXPECT_EQ(params[7].type, dsp::ParamType::Bool);
    EXPECT_EQ(params[8].type, dsp::ParamType::Enum);
    EXPECT_EQ(params[8].choices, (std::vector<std::string>{"0", "1", "2.5"}));
    EXPECT_EQ(block.param("mode").text(), "1");
    EXPECT_EQ(block.info().inputs.label(0), "in_1");
    EXPECT_EQ(block.info().outputs.maxCount, 2U);
}

TEST(PluginBlockTest, ALadspaPluginIsABlockOfItsOwnTypeWithTheSettingsItsScanSays)
{
    // A LADSPA plugin by its library and label.
    rb::plugin::setPluginInfoSource(
        [](std::string_view format, std::string_view id) -> std::optional<rb::plugin::PluginDescription> {
            if (format != "ladspa" || id != "amp.so:amp_mono") {
                return std::nullopt;
            }
            rb::plugin::PluginDescription described;
            described.format = "ladspa";
            described.id = std::string{id};
            described.audioInputs = 1;
            described.audioOutputs = 1;
            described.params = {{"gain", "Gain", rb::plugin::PluginParam::Kind::Float, 0.0f, 2.0f, 1.0f, false, {}}};
            return described;
        });
    const std::unique_ptr<dsp::Block> made = PluginBlock::makeLadspa();
    auto &block = static_cast<PluginBlock &>(*made);
    EXPECT_EQ(block.info().typeId, "plugin.ladspa");
    EXPECT_EQ(block.format(), "ladspa");
    EXPECT_EQ(block.pluginId(), "") << "a LADSPA block starts with no plugin chosen";
    ASSERT_TRUE(block.setParam("plugin", ParamValue::ofString("amp.so:amp_mono")).has_value());
    EXPECT_EQ(paramIds(block), (std::vector<std::string>{"plugin", "state", "builtinLatency", "monoMode", "gain"}));
    EXPECT_FLOAT_EQ(*block.param("gain").asFloat(), 1.0f);
    EXPECT_EQ(block.pluginInputs(), 1U);
    EXPECT_EQ(block.info().inputs.maxCount, 2U) << "a mono plugin on a stereo pair";
    rb::plugin::setPluginInfoSource({});
}

TEST(PluginBlockTest, AClapPluginsChoicesAreItsValuesAsTheScanWroteThem)
{
    const Described described;
    PluginBlock block("clap");
    EXPECT_EQ(block.info().typeId, "plugin.clap");
    ASSERT_TRUE(block.setParam("plugin", ParamValue::ofString("org.example.comp")).has_value());
    const auto params = block.params();
    ASSERT_EQ(params.size(), PluginBlock::kOwnParams + 2);
    EXPECT_EQ(params[5].id, "3810080779");
    // Each id is the value as the scan's JSON has it, which is what the
    // panel sends back.
    EXPECT_EQ(params[5].choices, (std::vector<std::string>{"0", "0.5", "0.833007812", "1"}));
    EXPECT_TRUE(block.setParam("3810080779", ParamValue::ofEnum("0.833007812")).has_value())
        << "the panel's choice was refused";
    // The default falls between two choices: the nearer is taken.
    PluginBlock fresh("clap");
    ASSERT_TRUE(fresh.setParam("plugin", ParamValue::ofString("org.example.comp")).has_value());
    EXPECT_EQ(fresh.param("3810080779").text(), "0.5");
}

TEST(PluginBlockTest, AMonoPluginHasAStereoPairOfPorts)
{
    const Described described;
    PluginBlock block("lv2");
    ASSERT_TRUE(block.setParam("plugin", ParamValue::ofString("urn:test:mono")).has_value());
    EXPECT_EQ(block.pluginInputs(), 1U);
    EXPECT_EQ(block.info().inputs.maxCount, 2U);
    EXPECT_EQ(block.info().outputs.maxCount, 2U);
    EXPECT_EQ(block.monoMode(), "dualMono");
}

TEST(PluginBlockTest, APluginThisMachineCannotDescribeKeepsItsSettings)
{
    const Described described;
    PluginBlock block("lv2");
    dsp::ParamValueMap state{{"plugin", ParamValue::ofString("urn:test:elsewhere")},
                             {"threshold", ParamValue::ofFloat(0.25f)},
                             {"state", ParamValue::ofString("@prefix …")}};
    ASSERT_TRUE(block.loadState(state).has_value());
    const dsp::ParamValueMap saved = block.saveState();
    ASSERT_TRUE(saved.contains("threshold")) << "a setting of a plugin that is not here was lost";
    EXPECT_FLOAT_EQ(*saved.at("threshold").asFloat(), 0.25f);
    EXPECT_EQ(saved.at("state").text(), "@prefix …");

    // Another plugin: what was kept was the other plugin's.
    ASSERT_TRUE(block.setParam("plugin", ParamValue::ofString("urn:test:stereo")).has_value());
    EXPECT_FALSE(block.saveState().contains("threshold"));
}
