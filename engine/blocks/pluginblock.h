// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "dsp/paramstore.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rb::blocks
{

/// **A plugin, as the session sees it** (ADR-0014).
///
/// The plugin itself runs in a `reverblabr-plugin-host` process of its own,
/// and its sound goes through that host's node; the session is cut into
/// segments around it. This block is what stays in the engine: which plugin
/// it is, the parameters the plugin has, the values they were last given,
/// the plugin's own state as the host last gave it, and the latency the host
/// reported, which is what latency compensation lines every other path up
/// with.
///
/// **Its parameters are the plugin's**, after four of its own: which
/// plugin, its saved state, for the built-in test plugin how much latency
/// to have, and what a mono plugin does on a stereo lane. A knob on it goes
/// to the host's audio thread through shared memory rather than to a
/// graph. **The plugin's parameters come from what a scan found**
/// (plugin::describePlugin), never from loading it here.
///
/// It is never run by a graph while the plugin is running in its host. A
/// block that is switched off is not hosted at all: it is compiled into its
/// segment as the pass-through every switched-off block is, holding the
/// sound back by the latency the plugin last reported so that nothing
/// shifts against a parallel path. process() passes its input through, for
/// that and nothing else.
class PluginBlock final : public dsp::Block
{
public:
    /// The plugins built into the host. Each format is a type of its own,
    /// `plugin.<format>`: `plugin.lv2`, `plugin.ladspa`, and
    /// `plugin.clap` where the build has the CLAP SDK.
    static constexpr std::string_view kTypeId = "plugin.builtin";
    static constexpr std::string_view kLv2TypeId = "plugin.lv2";
    static constexpr std::string_view kLadspaTypeId = "plugin.ladspa";
    static constexpr std::string_view kClapTypeId = "plugin.clap";
    static constexpr std::string_view kTypePrefix = "plugin.";

    static constexpr std::string_view kPluginParam = "plugin";
    static constexpr std::string_view kStateParam = "state";
    static constexpr std::string_view kLatencyParam = "builtinLatency";
    /// **What a plugin with one input does on a stereo lane**: runs
    /// twice, once a side ("dualMono", the default), or on the two sides
    /// mixed ("downmix").
    static constexpr std::string_view kMonoParam = "monoMode";
    static constexpr std::string_view kDualMono = "dualMono";
    static constexpr std::string_view kDownmix = "downmix";
    /// The block's own parameters, which come before the plugin's.
    static constexpr std::uint32_t kOwnParams = 4;

    static constexpr std::string_view kDefaultPlugin = "test.gain";

    /// Which of the block's published values is which (rt::BlockMeter):
    /// what its host is doing, as core::PluginHealth numbers it — 0 not
    /// hosted, 1 starting, 2 running, 3 crashed, 4 stopped responding. The
    /// words are the interface's.
    static constexpr std::size_t kHostStateValue = 0;

    /// A plugin block of \a format: "builtin", "lv2", "ladspa" or "clap".
    explicit PluginBlock(std::string format = "builtin");

    [[nodiscard]] static std::unique_ptr<dsp::Block> make();
    [[nodiscard]] static std::unique_ptr<dsp::Block> makeLv2();
    [[nodiscard]] static std::unique_ptr<dsp::Block> makeLadspa();
    [[nodiscard]] static std::unique_ptr<dsp::Block> makeClap();

    /// Whether \a typeId is a plugin block's, of any format.
    [[nodiscard]] static bool isPluginType(std::string_view typeId) noexcept
    {
        return typeId.starts_with(kTypePrefix);
    }

    /// Which plugin, in the host's words.
    [[nodiscard]] std::string pluginId() const;
    [[nodiscard]] std::string_view format() const noexcept
    {
        return m_format;
    }
    /// "dualMono" or "downmix".
    [[nodiscard]] std::string monoMode() const;
    /// The plugin's own channels, which the block's ports may be more than:
    /// a mono plugin on a stereo lane has two of each here.
    [[nodiscard]] std::uint32_t pluginInputs() const noexcept
    {
        return m_pluginInputs;
    }
    [[nodiscard]] std::uint32_t pluginOutputs() const noexcept
    {
        return m_pluginOutputs;
    }
    /// The plugin's own state as the host last gave it, or empty.
    [[nodiscard]] std::string state() const;
    /// For the built-in test plugin: how much it delays, and reports.
    [[nodiscard]] std::uint32_t builtinLatency() const;

    /// What the host said the plugin's latency is. Read when a graph is
    /// compiled, never by a cycle.
    void setReportedLatency(std::uint32_t samples) noexcept
    {
        m_info.latencySamples = samples;
    }

    /// The host's index of the block's parameter \a index, or nothing for
    /// one of the block's own.
    [[nodiscard]] static std::optional<std::uint32_t> pluginParamIndex(std::uint32_t index) noexcept;

    [[nodiscard]] const dsp::BlockInfo &info() const noexcept override
    {
        return m_info;
    }

    [[nodiscard]] std::span<const dsp::ParamDescriptor> params() const noexcept override
    {
        return m_descriptors;
    }

    [[nodiscard]] std::expected<void, dsp::BlockError> prepare(const dsp::PrepareSpec &spec) override;
    void process(dsp::ProcessContext &context) noexcept RB_NONBLOCKING override;
    void reset() noexcept RB_NONBLOCKING override;
    void applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING override;
    [[nodiscard]] std::expected<void, dsp::BlockError> setParam(std::string_view id,
                                                                const dsp::ParamValue &value) override;
    [[nodiscard]] dsp::ParamValue param(std::string_view id) const override;
    [[nodiscard]] dsp::ParamValueMap saveState() const override;
    [[nodiscard]] std::expected<void, dsp::BlockError> loadState(const dsp::ParamValueMap &state) override;

private:
    /// Takes on the parameters and ports of \a plugin, keeping the values
    /// of the block's own parameters.
    void becomePlugin(const std::string &plugin);

    std::string m_format;
    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    std::unique_ptr<dsp::ParamStore> m_store;
    /// **Settings of a plugin this machine cannot describe**, kept as they
    /// were loaded and saved again as they are: a session opened before its
    /// plugin is found, or on a machine without it, must not lose them.
    dsp::ParamValueMap m_kept;
    std::uint32_t m_pluginInputs = 2;
    std::uint32_t m_pluginOutputs = 2;
};

} // namespace rb::blocks
