// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/pluginblock.h"

#include "plugin/formats.h"
#include "plugin/testplugins.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace rb::blocks
{

using namespace rb::dsp;

namespace
{

constexpr std::int64_t kLongestBuiltinLatency = 96000;

[[nodiscard]] std::vector<ParamDescriptor> ownDescriptors()
{
    std::vector<ParamDescriptor> descriptors;

    ParamDescriptor plugin;
    plugin.id = std::string{PluginBlock::kPluginParam};
    plugin.type = ParamType::String;
    plugin.defaultValue = ParamValue::ofString(std::string{PluginBlock::kDefaultPlugin});
    // Another plugin is another host, other ports and other parameters.
    plugin.flags.automatable = false;
    descriptors.push_back(std::move(plugin));

    ParamDescriptor state;
    state.id = std::string{PluginBlock::kStateParam};
    state.type = ParamType::String;
    state.defaultValue = ParamValue::ofString({});
    state.flags.automatable = false;
    state.flags.hidden = true;
    descriptors.push_back(std::move(state));

    ParamDescriptor latency;
    latency.id = std::string{PluginBlock::kLatencyParam};
    latency.type = ParamType::Int;
    latency.minimum = ParamValue::ofInt(0);
    latency.maximum = ParamValue::ofInt(kLongestBuiltinLatency);
    latency.defaultValue = ParamValue::ofInt(0);
    latency.unit = ParamUnit::Samples;
    latency.flags.automatable = false;
    latency.flags.hidden = true;
    descriptors.push_back(std::move(latency));

    ParamDescriptor mono;
    mono.id = std::string{PluginBlock::kMonoParam};
    mono.type = ParamType::Enum;
    mono.choices = {std::string{PluginBlock::kDualMono}, std::string{PluginBlock::kDownmix}};
    mono.defaultValue = ParamValue::ofEnum(std::string{PluginBlock::kDualMono});
    // Two instances or one is another host.
    mono.flags.automatable = false;
    descriptors.push_back(std::move(mono));
    return descriptors;
}

/// **The id of a choice that is a plugin's value**: the value as the scan
/// wrote it (formats.cpp, `%.9g`), which is also what the panel reads it
/// as. Written shorter, 0.833007812 was "0.833008" here and "0.833007812"
/// in the panel, and choosing it there was refused.
[[nodiscard]] std::string choiceId(float value)
{
    std::array<char, 32> text{};
    std::snprintf(text.data(), text.size(), "%.9g", static_cast<double>(value));
    return text.data();
}

/// **A setting of a plugin, as a block parameter**, from what a scan
/// found. Everything a descriptor checks is made true here: a default
/// inside its range, a logarithmic taper only above zero, a whole-number
/// range the audio thread can carry.
[[nodiscard]] ParamDescriptor descriptorOf(const plugin::PluginParam &param)
{
    ParamDescriptor descriptor;
    descriptor.id = param.id;
    const float low = std::min(param.minimum, param.maximum);
    const float high = std::max(param.minimum, param.maximum);
    const float fallback = std::clamp(param.defaultValue, low, high);
    switch (param.kind) {
    case plugin::PluginParam::Kind::Toggle:
        descriptor.type = ParamType::Bool;
        descriptor.defaultValue = ParamValue::ofBool(fallback >= 0.5f);
        break;
    case plugin::PluginParam::Kind::Choice: {
        descriptor.type = ParamType::Enum;
        float nearest = param.choices.front().first;
        for (const auto &[value, label] : param.choices) {
            const std::string id = choiceId(value);
            if (std::ranges::find(descriptor.choices, id) == descriptor.choices.end()) {
                descriptor.choices.push_back(id);
            }
            if (std::abs(value - fallback) < std::abs(nearest - fallback)) {
                nearest = value;
            }
        }
        descriptor.defaultValue = ParamValue::ofEnum(choiceId(nearest));
        break;
    }
    case plugin::PluginParam::Kind::Int: {
        descriptor.type = ParamType::Int;
        const auto limit = static_cast<double>(kMaxIntParamMagnitude);
        const auto lowest = static_cast<std::int64_t>(std::clamp(std::ceil(double{low}), -limit, limit));
        const auto highest =
            std::max(lowest, static_cast<std::int64_t>(std::clamp(std::floor(double{high}), -limit, limit)));
        descriptor.minimum = ParamValue::ofInt(lowest);
        descriptor.maximum = ParamValue::ofInt(highest);
        descriptor.defaultValue =
            ParamValue::ofInt(std::clamp(static_cast<std::int64_t>(std::lround(fallback)), lowest, highest));
        break;
    }
    case plugin::PluginParam::Kind::Float:
        descriptor.type = ParamType::Float;
        descriptor.minimum = ParamValue::ofFloat(low);
        descriptor.maximum = ParamValue::ofFloat(high);
        descriptor.defaultValue = ParamValue::ofFloat(fallback);
        descriptor.taper = param.logarithmic && low > 0.0f ? ParamTaper::Logarithmic : ParamTaper::Linear;
        break;
    }
    return descriptor;
}

/// Ports numbered rather than named: a plugin's own port names are its
/// business, and the host names its ports the same way.
[[nodiscard]] PortSpec numberedPorts(std::uint32_t count, std::string_view prefix)
{
    std::vector<std::string> labels;
    for (std::uint32_t index = 0; index < count; ++index) {
        labels.push_back(std::string{prefix} + std::to_string(index + 1));
    }
    return PortSpec::fixed(count, std::move(labels));
}

} // namespace

PluginBlock::PluginBlock(std::string format)
    : m_format(std::move(format))
{
    m_info.typeId = std::string{kTypePrefix} + m_format;
    m_info.category = BlockCategory::Plugins;
    m_info.face = FaceKind::Icon;
    // The built-in format has a plugin to start with; any other is a block
    // somebody picks the plugin of.
    becomePlugin(m_format == "builtin" ? std::string{kDefaultPlugin} : std::string{});
}

std::unique_ptr<Block> PluginBlock::make()
{
    return std::make_unique<PluginBlock>("builtin");
}

std::unique_ptr<Block> PluginBlock::makeLv2()
{
    return std::make_unique<PluginBlock>("lv2");
}

std::unique_ptr<Block> PluginBlock::makeLadspa()
{
    return std::make_unique<PluginBlock>("ladspa");
}

std::unique_ptr<Block> PluginBlock::makeClap()
{
    return std::make_unique<PluginBlock>("clap");
}

void PluginBlock::becomePlugin(const std::string &plugin)
{
    // What was set before, which survives as far as the new plugin has
    // parameters of the same names.
    const ParamValueMap before = m_store ? m_store->save() : ParamValueMap{};

    m_descriptors = ownDescriptors();
    const std::unique_ptr<Block> made = m_format == "builtin" ? plugin::makeBuiltinPlugin(plugin, 0) : nullptr;
    const std::optional<plugin::PluginDescription> described =
        m_format == "builtin" || plugin.empty() ? std::nullopt : plugin::describePlugin(m_format, plugin);
    if (made) {
        for (const ParamDescriptor &descriptor : made->params()) {
            m_descriptors.push_back(descriptor);
        }
        m_info.inputs = made->info().inputs;
        m_info.outputs = made->info().outputs;
        m_pluginInputs = m_info.inputs.maxCount;
        m_pluginOutputs = m_info.outputs.maxCount;
    } else if (described) {
        for (const plugin::PluginParam &param : described->params) {
            if (param.kind != plugin::PluginParam::Kind::Choice || !param.choices.empty()) {
                m_descriptors.push_back(descriptorOf(param));
            }
        }
        m_pluginInputs = described->audioInputs;
        m_pluginOutputs = described->audioOutputs;
        // **A mono plugin on a stereo lane has a stereo pair of ports**
        // either way: twice over, or on the sides mixed, is the host's
        // business (monoMode).
        m_info.inputs = numberedPorts(m_pluginInputs == 1 ? 2 : m_pluginInputs, "in_");
        m_info.outputs = numberedPorts(m_pluginOutputs == 1 ? 2 : m_pluginOutputs, "out_");
    } else {
        // A plugin this machine has not got, or none chosen yet: a stereo
        // pair either way, so the wires the session has still have
        // somewhere to go, and a session that names it still opens.
        m_info.inputs = numberedPorts(2, "in_");
        m_info.outputs = numberedPorts(2, "out_");
        m_pluginInputs = 2;
        m_pluginOutputs = 2;
        if (m_format == "builtin") {
            m_info.inputs = PortSpec::fixed(2, {"in_FL", "in_FR"});
            m_info.outputs = PortSpec::fixed(2, {"out_FL", "out_FR"});
        }
    }
    m_store = std::make_unique<ParamStore>(m_descriptors);
    (void)m_store->load(before);
    (void)m_store->set(kPluginParam, ParamValue::ofString(plugin));
}

std::string PluginBlock::pluginId() const
{
    return m_store->value(kPluginParam).text();
}

std::string PluginBlock::state() const
{
    return m_store->value(kStateParam).text();
}

std::string PluginBlock::monoMode() const
{
    return m_store->value(kMonoParam).text();
}

std::uint32_t PluginBlock::builtinLatency() const
{
    return static_cast<std::uint32_t>(m_store->value(kLatencyParam).asNumber().value_or(0.0));
}

std::optional<std::uint32_t> PluginBlock::pluginParamIndex(std::uint32_t index) noexcept
{
    if (index < kOwnParams) {
        return std::nullopt;
    }
    return index - kOwnParams;
}

std::optional<ParamValue> PluginBlock::valueFromPlugin(std::string_view id, float value) const
{
    const auto found = std::ranges::find_if(m_descriptors, [id](const ParamDescriptor &d) { return d.id == id; });
    if (found == m_descriptors.end() || std::distance(m_descriptors.begin(), found) < kOwnParams
        || !std::isfinite(value)) {
        return std::nullopt;
    }
    const ParamDescriptor &descriptor = *found;
    switch (descriptor.type) {
    case ParamType::Bool:
        return ParamValue::ofBool(value >= 0.5f);
    case ParamType::Int: {
        const std::int64_t low = descriptor.minimum.asInt().value_or(0);
        const std::int64_t high = descriptor.maximum.asInt().value_or(0);
        return ParamValue::ofInt(std::clamp(static_cast<std::int64_t>(std::lround(value)), low, high));
    }
    case ParamType::Float: {
        const auto low = static_cast<float>(descriptor.minimum.asNumber().value_or(value));
        const auto high = static_cast<float>(descriptor.maximum.asNumber().value_or(value));
        return ParamValue::ofFloat(std::clamp(value, std::min(low, high), std::max(low, high)));
    }
    case ParamType::Enum: {
        // The choices are the plugin's values, written as the scan wrote
        // them: the nearest is the one the plugin is on.
        const std::string *nearest = nullptr;
        float distance = 0.0f;
        for (const std::string &choice : descriptor.choices) {
            const float away = std::abs(std::strtof(choice.c_str(), nullptr) - value);
            if (nearest == nullptr || away < distance) {
                nearest = &choice;
                distance = away;
            }
        }
        return nearest != nullptr ? std::optional{ParamValue::ofEnum(*nearest)} : std::nullopt;
    }
    default:
        return std::nullopt;
    }
}

std::expected<void, BlockError> PluginBlock::prepare(const PrepareSpec &spec)
{
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    return {};
}

void PluginBlock::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    for (std::uint32_t channel = 0; channel < context.output.channelCount(); ++channel) {
        const std::span<float> out = context.output.channel(channel);
        if (out.empty()) {
            continue;
        }
        const std::span<float> in =
            channel < context.input.channelCount() ? context.input.channel(channel) : std::span<float>{};
        if (in.empty()) {
            std::fill(out.begin(), out.end(), 0.0f);
        } else if (in.data() != out.data()) {
            std::copy_n(in.begin(), std::min(in.size(), out.size()), out.begin());
        }
    }
}

void PluginBlock::reset() noexcept RB_NONBLOCKING { }

void PluginBlock::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    m_store->applyNumber(index, value);
}

std::expected<void, BlockError> PluginBlock::setParam(std::string_view id, const ParamValue &value)
{
    if (id == kPluginParam) {
        if (value.type() != std::optional<ParamType>{ParamType::String}) {
            return std::unexpected(BlockError::ParamTypeMismatch);
        }
        if (value.text() != pluginId()) {
            // What was kept belonged to the plugin before.
            m_kept.clear();
            becomePlugin(value.text());
        }
        return {};
    }
    return m_store->set(id, value);
}

ParamValue PluginBlock::param(std::string_view id) const
{
    return m_store->value(id);
}

ParamValueMap PluginBlock::saveState() const
{
    ParamValueMap saved = m_store->save();
    for (const auto &[id, value] : m_kept) {
        saved.emplace(id, value);
    }
    return saved;
}

std::expected<void, BlockError> PluginBlock::loadState(const ParamValueMap &state)
{
    // Which plugin first: it says what the rest of the state means.
    if (const auto plugin = state.find(std::string{kPluginParam}); plugin != state.end()) {
        if (const std::expected<void, BlockError> set = setParam(kPluginParam, plugin->second); !set) {
            return set;
        }
    } else if (const std::string first = m_format == "builtin" ? std::string{kDefaultPlugin} : std::string{};
               pluginId() != first) {
        becomePlugin(first);
    }
    // What this plugin has no setting for is kept, and saved again.
    m_kept.clear();
    for (const auto &[id, value] : state) {
        if (std::ranges::none_of(m_descriptors, [&id](const ParamDescriptor &d) { return d.id == id; })) {
            m_kept.emplace(id, value);
        }
    }
    return m_store->load(state);
}

} // namespace rb::blocks
