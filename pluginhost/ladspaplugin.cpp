// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ladspaplugin.h"

#include <dlfcn.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <system_error>

namespace rb::pluginhost
{

namespace fs = std::filesystem;

namespace
{

/// The plugin's properties bit that says it cannot write its output over
/// its input.
constexpr int kInPlaceBroken = 0x2;

void connectPort(const ladspa::Descriptor *descriptor,
                 ladspa::Handle instance,
                 std::uint32_t port,
                 float *data) noexcept RB_NONBLOCKING
{
    // The format asks connect_port and run to be real-time safe.
    using Connect = void (*)(ladspa::Handle, unsigned long, float *) RB_NONBLOCKING;
    const auto connect = reinterpret_cast<Connect>(descriptor->connect_port);
    connect(instance, port, data);
}

void run(const ladspa::Descriptor *descriptor, ladspa::Handle instance, std::uint32_t frames) noexcept RB_NONBLOCKING
{
    using Run = void (*)(ladspa::Handle, unsigned long) RB_NONBLOCKING;
    const auto go = reinterpret_cast<Run>(descriptor->run);
    go(instance, frames);
}

[[nodiscard]] dsp::PortSpec numbered(std::uint32_t count, std::string_view prefix)
{
    std::vector<std::string> labels;
    for (std::uint32_t index = 0; index < count; ++index) {
        labels.push_back(std::string{prefix} + std::to_string(index + 1));
    }
    return dsp::PortSpec::fixed(count, std::move(labels));
}

[[nodiscard]] bool is(int descriptor, int bits)
{
    return (descriptor & bits) == bits;
}

/// A port's name made into an id: lower case, letters and digits, the rest
/// one underscore. `Gain (dB)` is `gain_db`.
[[nodiscard]] std::string idOf(std::string_view name)
{
    std::string id;
    for (const char c : name) {
        const auto u = static_cast<unsigned char>(c);
        if (std::isalnum(u) != 0) {
            id += static_cast<char>(std::tolower(u));
        } else if (!id.empty() && id.back() != '_') {
            id += '_';
        }
    }
    while (!id.empty() && id.back() == '_') {
        id.pop_back();
    }
    return id.empty() ? std::string{"control"} : id;
}

/// Between \a low and \a high, a quarter (\a weight 0.25), half or three
/// quarters of the way, on a logarithmic scale when asked and possible.
[[nodiscard]] float between(float low, float high, float weight, bool logarithmic)
{
    if (logarithmic && low > 0.0f && high > 0.0f) {
        return std::exp(std::log(low) * (1.0f - weight) + std::log(high) * weight);
    }
    return low * (1.0f - weight) + high * weight;
}

[[nodiscard]] bool isLatencyPort(std::string_view name)
{
    std::string lower;
    for (const char c : name) {
        lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return lower == "latency" || lower == "_latency";
}

} // namespace

std::shared_ptr<LadspaLibrary> LadspaLibrary::open(const fs::path &path, std::string &error)
{
    void *handle = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        const char *reason = ::dlerror();
        error = reason != nullptr ? reason : "dlopen failed";
        return nullptr;
    }
    const auto function = reinterpret_cast<ladspa::DescriptorFunction>(::dlsym(handle, ladspa::kDescriptorSymbol));
    if (function == nullptr) {
        error = "not a LADSPA library: no " + std::string{ladspa::kDescriptorSymbol};
        ::dlclose(handle);
        return nullptr;
    }
    std::shared_ptr<LadspaLibrary> library(new LadspaLibrary());
    library->m_handle = handle;
    for (unsigned long index = 0;; ++index) {
        const ladspa::Descriptor *descriptor = function(index);
        if (descriptor == nullptr) {
            break;
        }
        library->m_plugins.push_back(descriptor);
    }
    return library;
}

LadspaLibrary::~LadspaLibrary()
{
    if (m_handle != nullptr) {
        ::dlclose(m_handle);
    }
}

const ladspa::Descriptor *LadspaLibrary::plugin(std::string_view label) const
{
    for (const ladspa::Descriptor *descriptor : m_plugins) {
        if (descriptor->Label != nullptr && label == descriptor->Label) {
            return descriptor;
        }
    }
    return nullptr;
}

std::string ladspaId(const fs::path &library, std::string_view label)
{
    return library.filename().string() + ":" + std::string{label};
}

std::optional<LadspaLocation> locateLadspa(std::string_view id)
{
    // **The library ends at its `.so`**: a label may have colons of its own
    // (LSP's are URLs), so the first colon is not where it ends.
    const std::size_t end = id.find(".so:");
    if (end == std::string_view::npos || end == 0 || end + 4 == id.size()) {
        return std::nullopt;
    }
    const std::size_t colon = end + 3;
    const fs::path file{std::string{id.substr(0, colon)}};
    LadspaLocation location{fs::path{}, std::string{id.substr(colon + 1)}};
    std::error_code error;
    if (file.is_absolute()) {
        if (!fs::is_regular_file(file, error)) {
            return std::nullopt;
        }
        location.library = file;
        return location;
    }
    const plugin::PluginFormat *format = plugin::formatNamed("ladspa");
    if (format == nullptr) {
        return std::nullopt;
    }
    for (const fs::path &folder : format->searchPaths()) {
        const fs::path candidate = folder / file;
        if (fs::is_regular_file(candidate, error)) {
            location.library = candidate;
            return location;
        }
    }
    return std::nullopt;
}

std::vector<plugin::PluginParam> ladspaParams(const ladspa::Descriptor &descriptor)
{
    std::vector<plugin::PluginParam> params;
    std::vector<std::string> taken;
    for (unsigned long index = 0; index < descriptor.PortCount; ++index) {
        const int kind = descriptor.PortDescriptors[index];
        if (!is(kind, ladspa::port::kControl | ladspa::port::kInput)) {
            continue;
        }
        const char *portName = descriptor.PortNames != nullptr ? descriptor.PortNames[index] : nullptr;
        plugin::PluginParam param;
        param.name = portName != nullptr ? portName : "Control " + std::to_string(index + 1);
        // **Unique within the plugin**: LADSPA names ports for people, and
        // nothing stops two from reading the same.
        std::string id = idOf(param.name);
        for (int suffix = 2; std::ranges::find(taken, id) != taken.end(); ++suffix) {
            id = idOf(param.name) + "_" + std::to_string(suffix);
        }
        taken.push_back(id);
        param.id = id;

        const ladspa::PortRangeHint hint = descriptor.PortRangeHints != nullptr ? descriptor.PortRangeHints[index]
                                                                                : ladspa::PortRangeHint{0, 0.0f, 0.0f};
        const int hints = hint.HintDescriptor;
        const float scale = is(hints, ladspa::hint::kSampleRate) ? static_cast<float>(kLadspaDescribedRate) : 1.0f;
        const bool below = is(hints, ladspa::hint::kBoundedBelow);
        const bool above = is(hints, ladspa::hint::kBoundedAbove);
        // A default the hints give as a number, which an open end of the
        // range has to leave room for.
        std::optional<float> fixed;
        switch (hints & ladspa::hint::kDefaultMask) {
        case ladspa::hint::kDefault0:
            fixed = 0.0f;
            break;
        case ladspa::hint::kDefault1:
            fixed = 1.0f;
            break;
        case ladspa::hint::kDefault100:
            fixed = 100.0f;
            break;
        case ladspa::hint::kDefault440:
            fixed = 440.0f;
            break;
        default:
            break;
        }
        if (is(hints, ladspa::hint::kToggled)) {
            param.kind = plugin::PluginParam::Kind::Toggle;
            param.minimum = 0.0f;
            param.maximum = 1.0f;
        } else {
            // **A range open at an end** is given one: from nothing, or to
            // twice the default (the SDK's amplifiers, "0 to ..., 1", go to
            // 2), and never narrower than one.
            param.minimum = below ? hint.LowerBound * scale : std::min(0.0f, fixed.value_or(0.0f));
            param.maximum = above
                ? hint.UpperBound * scale
                : std::max({param.minimum + 1.0f, 1.0f, 2.0f * fixed.value_or(0.0f), param.minimum * 2.0f});
            if (param.maximum < param.minimum) {
                std::swap(param.minimum, param.maximum);
            }
            if (is(hints, ladspa::hint::kInteger)) {
                param.kind = plugin::PluginParam::Kind::Int;
            }
            param.logarithmic = is(hints, ladspa::hint::kLogarithmic) && param.minimum > 0.0f;
        }
        const bool logarithmic = is(hints, ladspa::hint::kLogarithmic);
        switch (hints & ladspa::hint::kDefaultMask) {
        case ladspa::hint::kDefaultMinimum:
            param.defaultValue = param.minimum;
            break;
        case ladspa::hint::kDefaultLow:
            param.defaultValue = between(param.minimum, param.maximum, 0.25f, logarithmic);
            break;
        case ladspa::hint::kDefaultMiddle:
            param.defaultValue = between(param.minimum, param.maximum, 0.5f, logarithmic);
            break;
        case ladspa::hint::kDefaultHigh:
            param.defaultValue = between(param.minimum, param.maximum, 0.75f, logarithmic);
            break;
        case ladspa::hint::kDefaultMaximum:
            param.defaultValue = param.maximum;
            break;
        case ladspa::hint::kDefault0:
        case ladspa::hint::kDefault1:
        case ladspa::hint::kDefault100:
        case ladspa::hint::kDefault440:
            param.defaultValue = fixed.value_or(0.0f);
            break;
        default:
            param.defaultValue = below ? param.minimum : 0.0f;
            break;
        }
        param.defaultValue = std::clamp(param.defaultValue, param.minimum, param.maximum);
        if (param.kind == plugin::PluginParam::Kind::Int) {
            param.defaultValue = std::round(param.defaultValue);
        }
        params.push_back(std::move(param));
    }
    return params;
}

plugin::PluginDescription describeLadspa(const fs::path &library, const ladspa::Descriptor &descriptor)
{
    plugin::PluginDescription description;
    description.format = "ladspa";
    description.id = ladspaId(library, descriptor.Label != nullptr ? descriptor.Label : "");
    description.name =
        descriptor.Name != nullptr ? descriptor.Name : (descriptor.Label != nullptr ? descriptor.Label : "");
    description.vendor = descriptor.Maker != nullptr ? descriptor.Maker : "";
    for (unsigned long index = 0; index < descriptor.PortCount; ++index) {
        const int kind = descriptor.PortDescriptors[index];
        if (is(kind, ladspa::port::kAudio | ladspa::port::kInput)) {
            ++description.audioInputs;
        } else if (is(kind, ladspa::port::kAudio | ladspa::port::kOutput)) {
            ++description.audioOutputs;
        }
    }
    description.params = ladspaParams(descriptor);
    description.parameters = static_cast<std::uint32_t>(description.params.size());
    return description;
}

std::unique_ptr<LadspaPlugin> LadspaPlugin::make(std::string_view id, Mono mono)
{
    const std::optional<LadspaLocation> location = locateLadspa(id);
    if (!location) {
        return nullptr;
    }
    std::string error;
    std::shared_ptr<LadspaLibrary> library = LadspaLibrary::open(location->library, error);
    if (!library) {
        return nullptr;
    }
    const ladspa::Descriptor *descriptor = library->plugin(location->label);
    if (descriptor == nullptr) {
        return nullptr;
    }
    return make(std::move(library), descriptor, mono);
}

std::unique_ptr<LadspaPlugin>
LadspaPlugin::make(std::shared_ptr<LadspaLibrary> library, const ladspa::Descriptor *descriptor, Mono mono)
{
    if (!library || descriptor == nullptr || descriptor->instantiate == nullptr || descriptor->connect_port == nullptr
        || descriptor->run == nullptr) {
        return nullptr;
    }
    return std::unique_ptr<LadspaPlugin>(new LadspaPlugin(std::move(library), descriptor, mono));
}

LadspaPlugin::LadspaPlugin(std::shared_ptr<LadspaLibrary> library, const ladspa::Descriptor *descriptor, Mono mono)
    : m_library(std::move(library))
    , m_descriptor(descriptor)
    , m_mono(mono)
    , m_inPlaceBroken((descriptor->Properties & kInPlaceBroken) != 0)
{
    m_params = ladspaParams(*descriptor);
    m_values = std::vector<std::atomic<float>>(m_params.size());
    for (std::size_t index = 0; index < m_params.size(); ++index) {
        m_values[index].store(m_params[index].defaultValue);
        dsp::ParamDescriptor param;
        param.id = m_params[index].id;
        param.type = dsp::ParamType::Float;
        param.defaultValue = dsp::ParamValue::ofFloat(m_params[index].defaultValue);
        m_descriptors.push_back(std::move(param));
    }
    std::uint32_t param = 0;
    for (unsigned long index = 0; index < descriptor->PortCount; ++index) {
        const int kind = descriptor->PortDescriptors[index];
        Port entry;
        entry.index = static_cast<std::uint32_t>(index);
        const bool input = is(kind, ladspa::port::kInput);
        if (is(kind, ladspa::port::kAudio)) {
            entry.kind = input ? Port::Kind::AudioIn : Port::Kind::AudioOut;
            (input ? m_audioIns : m_audioOuts) += 1;
        } else {
            entry.kind = input ? Port::Kind::ControlIn : Port::Kind::ControlOut;
            if (input) {
                entry.param = param++;
            } else if (descriptor->PortNames != nullptr && descriptor->PortNames[index] != nullptr
                       && isLatencyPort(descriptor->PortNames[index])) {
                m_latencyPort = entry.index;
            }
        }
        m_ports.push_back(entry);
    }
    m_info.typeId = "ladspa";
    m_info.category = dsp::BlockCategory::Plugins;
    m_info.inputs = numbered(m_audioIns == 1 ? 2 : m_audioIns, "in_");
    m_info.outputs = numbered(m_audioOuts == 1 ? 2 : m_audioOuts, "out_");
}

LadspaPlugin::~LadspaPlugin()
{
    freeInstances();
}

void LadspaPlugin::freeInstances() noexcept
{
    for (ladspa::Handle instance : m_instances) {
        if (m_descriptor->deactivate != nullptr) {
            m_descriptor->deactivate(instance);
        }
        if (m_descriptor->cleanup != nullptr) {
            m_descriptor->cleanup(instance);
        }
    }
    m_instances.clear();
}

std::expected<void, dsp::BlockError> LadspaPlugin::prepare(const dsp::PrepareSpec &spec)
{
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > dsp::kMaxBlockFrames) {
        return std::unexpected(dsp::BlockError::UnsupportedSpec);
    }
    freeInstances();
    // **Twice for a mono plugin on a stereo lane, once a side**, unless it
    // is to hear the two sides mixed — or makes more than one side itself.
    const std::size_t count = m_audioIns == 1 && m_audioOuts <= 1 && m_mono == Mono::DualMono ? 2 : 1;
    m_controls.assign(count, std::vector<float>(m_ports.size(), 0.0f));
    for (std::size_t which = 0; which < count; ++which) {
        ladspa::Handle instance =
            m_descriptor->instantiate(m_descriptor, static_cast<unsigned long>(std::lround(spec.sampleRate)));
        if (instance == nullptr) {
            freeInstances();
            return std::unexpected(dsp::BlockError::UnsupportedSpec);
        }
        m_instances.push_back(instance);
        std::vector<float> &controls = m_controls[which];
        for (const Port &port : m_ports) {
            if (port.kind == Port::Kind::ControlIn) {
                controls[port.index] = m_values[port.param].load();
                m_descriptor->connect_port(instance, port.index, &controls[port.index]);
            } else if (port.kind == Port::Kind::ControlOut) {
                m_descriptor->connect_port(instance, port.index, &controls[port.index]);
            }
        }
        if (m_descriptor->activate != nullptr) {
            m_descriptor->activate(instance);
        }
    }
    m_silence.assign(spec.maxBlockFrames, 0.0f);
    m_mix.assign(spec.maxBlockFrames, 0.0f);
    m_spare.assign(spec.maxBlockFrames, 0.0f);
    m_inputCopies.assign(m_inPlaceBroken ? std::max<std::uint32_t>(m_audioIns, 2) : 0,
                         std::vector<float>(spec.maxBlockFrames, 0.0f));
    return {};
}

void LadspaPlugin::process(dsp::ProcessContext &context) noexcept RB_NONBLOCKING
{
    const std::uint32_t frames = context.frames;
    if (m_instances.empty() || frames == 0 || frames > m_silence.size()) {
        for (std::uint32_t channel = 0; channel < context.output.channelCount(); ++channel) {
            const std::span<float> out = context.output.channel(channel);
            std::fill(out.begin(), out.end(), 0.0f);
        }
        return;
    }
    const auto outputOf = [&context, this](std::uint32_t channel) -> float * {
        const std::span<float> out =
            channel < context.output.channelCount() ? context.output.channel(channel) : std::span<float>{};
        return out.empty() ? m_spare.data() : out.data();
    };
    const auto inputOf = [&context, &outputOf, frames, this](std::uint32_t channel) -> float * {
        const std::span<float> in =
            channel < context.input.channelCount() ? context.input.channel(channel) : std::span<float>{};
        if (in.empty()) {
            return m_silence.data();
        }
        // **A plugin that cannot write over what it reads** gets a copy
        // when the two are the same buffer.
        if (m_inPlaceBroken && channel < m_inputCopies.size() && in.data() == outputOf(channel)) {
            std::copy_n(in.begin(), frames, m_inputCopies[channel].begin());
            return m_inputCopies[channel].data();
        }
        return in.data();
    };
    const bool dual = m_instances.size() == 2;
    const bool downmix = m_audioIns == 1 && !dual;
    if (downmix) {
        const float *left = inputOf(0);
        const float *right = inputOf(1);
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            m_mix[frame] = 0.5f * (left[frame] + right[frame]);
        }
    }

    for (std::size_t which = 0; which < m_instances.size(); ++which) {
        ladspa::Handle instance = m_instances[which];
        std::uint32_t audioIn = 0;
        std::uint32_t audioOut = 0;
        for (const Port &port : m_ports) {
            if (port.kind == Port::Kind::AudioIn) {
                float *buffer = dual ? inputOf(static_cast<std::uint32_t>(which))
                    : downmix        ? m_mix.data()
                                     : inputOf(audioIn);
                connectPort(m_descriptor, instance, port.index, buffer);
                ++audioIn;
            } else if (port.kind == Port::Kind::AudioOut) {
                float *buffer = dual ? outputOf(static_cast<std::uint32_t>(which)) : outputOf(audioOut);
                connectPort(m_descriptor, instance, port.index, buffer);
                ++audioOut;
            }
        }
        run(m_descriptor, instance, frames);
    }

    // A mono output goes to both sides.
    if (m_audioOuts == 1 && !dual && context.output.channelCount() > 1) {
        const std::span<float> left = context.output.channel(0);
        const std::span<float> right = context.output.channel(1);
        if (!left.empty() && !right.empty()) {
            std::copy_n(left.begin(), frames, right.begin());
        }
    }
    if (m_latencyPort != UINT32_MAX) {
        const float reported = m_controls[0][m_latencyPort];
        m_latency.store(reported > 0.0f ? static_cast<std::uint32_t>(std::lround(reported)) : 0U,
                        std::memory_order_relaxed);
    }
}

void LadspaPlugin::reset() noexcept RB_NONBLOCKING { }

void LadspaPlugin::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    if (index >= m_params.size()) {
        return;
    }
    m_values[index].store(value, std::memory_order_relaxed);
    for (const Port &port : m_ports) {
        if (port.kind == Port::Kind::ControlIn && port.param == index) {
            for (std::vector<float> &controls : m_controls) {
                controls[port.index] = value;
            }
            break;
        }
    }
}

std::expected<void, dsp::BlockError> LadspaPlugin::setParam(std::string_view id, const dsp::ParamValue &value)
{
    for (std::uint32_t index = 0; index < m_params.size(); ++index) {
        if (m_params[index].id == id) {
            applyParam(index, static_cast<float>(value.asNumber().value_or(0.0)));
            return {};
        }
    }
    return std::unexpected(dsp::BlockError::UnknownParam);
}

dsp::ParamValue LadspaPlugin::param(std::string_view id) const
{
    for (std::uint32_t index = 0; index < m_params.size(); ++index) {
        if (m_params[index].id == id) {
            return dsp::ParamValue::ofFloat(m_values[index].load(std::memory_order_relaxed));
        }
    }
    return {};
}

dsp::ParamValueMap LadspaPlugin::saveState() const
{
    dsp::ParamValueMap state;
    for (std::uint32_t index = 0; index < m_params.size(); ++index) {
        state.emplace(m_params[index].id, dsp::ParamValue::ofFloat(m_values[index].load(std::memory_order_relaxed)));
    }
    return state;
}

std::expected<void, dsp::BlockError> LadspaPlugin::loadState(const dsp::ParamValueMap &state)
{
    for (const auto &[id, value] : state) {
        (void)setParam(id, value);
    }
    return {};
}

void LadspaPlugin::refreshLatency() noexcept
{
    m_info.latencySamples = m_latency.load(std::memory_order_relaxed);
}

} // namespace rb::pluginhost
