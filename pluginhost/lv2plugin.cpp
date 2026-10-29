// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lv2plugin.h"

#include "scan.h"

#include <lv2/atom/atom.h>
#include <lv2/core/lv2.h>
#include <lv2/state/state.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace rb::pluginhost
{

namespace
{

/// Room for what an atom port says in one cycle, in 64-bit words.
constexpr std::size_t kAtomWords = 1024;

/// **The plugin's own connect_port and run**, which LV2 puts in the audio
/// threading class: made for the audio thread, which the compiler cannot
/// know of a function pointer. The claim is made here, once, as the session
/// node makes it for PipeWire's buffer.
void connectPort(LilvInstance *instance, std::uint32_t port, void *data) noexcept RB_NONBLOCKING
{
    using Connect = void (*)(LV2_Handle, std::uint32_t, void *) RB_NONBLOCKING;
    const auto connect = reinterpret_cast<Connect>(instance->lv2_descriptor->connect_port);
    connect(instance->lv2_handle, port, data);
}

void run(LilvInstance *instance, std::uint32_t frames) noexcept RB_NONBLOCKING
{
    using Run = void (*)(LV2_Handle, std::uint32_t) RB_NONBLOCKING;
    const auto go = reinterpret_cast<Run>(instance->lv2_descriptor->run);
    go(instance->lv2_handle, frames);
}

/// Ports numbered as the engine's plugin block numbers them.
[[nodiscard]] dsp::PortSpec numbered(std::uint32_t count, std::string_view prefix)
{
    std::vector<std::string> labels;
    for (std::uint32_t index = 0; index < count; ++index) {
        labels.push_back(std::string{prefix} + std::to_string(index + 1));
    }
    return dsp::PortSpec::fixed(count, std::move(labels));
}

/// The distinct values of a choice, in order: what its position means.
[[nodiscard]] std::vector<float> choiceValues(const plugin::PluginParam &param)
{
    std::vector<float> values;
    for (const auto &[value, label] : param.choices) {
        if (values.empty() || values.back() != value) {
            values.push_back(value);
        }
    }
    return values;
}

} // namespace

Lv2World::Lv2World()
    : m_world(lilv_world_new())
{
    lilv_world_load_all(m_world);
    m_map.handle = this;
    m_map.map = [](LV2_URID_Map_Handle handle, const char *uri) { return static_cast<Lv2World *>(handle)->map(uri); };
    m_unmap.handle = this;
    m_unmap.unmap = [](LV2_URID_Unmap_Handle handle, LV2_URID urid) {
        return static_cast<Lv2World *>(handle)->unmap(urid);
    };
}

Lv2World::~Lv2World()
{
    lilv_world_free(m_world);
}

const LilvPlugin *Lv2World::plugin(const std::string &uri) const
{
    LilvNode *node = lilv_new_uri(m_world, uri.c_str());
    const LilvPlugin *found = lilv_plugins_get_by_uri(lilv_world_get_all_plugins(m_world), node);
    lilv_node_free(node);
    return found;
}

LV2_URID Lv2World::map(const char *uri)
{
    const std::lock_guard lock(m_lock);
    const auto found = m_ids.find(uri);
    if (found != m_ids.end()) {
        return found->second;
    }
    m_uris.emplace_back(uri);
    const auto id = static_cast<LV2_URID>(m_uris.size());
    m_ids.emplace(uri, id);
    return id;
}

const char *Lv2World::unmap(LV2_URID urid)
{
    const std::lock_guard lock(m_lock);
    return urid >= 1 && urid <= m_uris.size() ? m_uris[urid - 1].c_str() : nullptr;
}

std::unique_ptr<Lv2Plugin> Lv2Plugin::make(const std::shared_ptr<Lv2World> &world, const std::string &uri, Mono mono)
{
    const LilvPlugin *plugin = world->plugin(uri);
    if (plugin == nullptr) {
        return nullptr;
    }
    return std::unique_ptr<Lv2Plugin>(new Lv2Plugin(world, plugin, mono));
}

Lv2Plugin::Lv2Plugin(std::shared_ptr<Lv2World> world, const LilvPlugin *plugin, Mono mono)
    : m_world(std::move(world))
    , m_plugin(plugin)
    , m_mono(mono)
{
    LilvWorld *lilv = m_world->world();
    m_params = lv2Params(lilv, plugin);
    m_values = std::vector<std::atomic<float>>(m_params.size());
    for (std::size_t index = 0; index < m_params.size(); ++index) {
        m_choices.push_back(choiceValues(m_params[index]));
        m_values[index].store(m_params[index].defaultValue);
        dsp::ParamDescriptor descriptor;
        descriptor.id = m_params[index].id;
        descriptor.type = dsp::ParamType::Float;
        descriptor.defaultValue = dsp::ParamValue::ofFloat(m_params[index].defaultValue);
        m_descriptors.push_back(std::move(descriptor));
    }

    LilvNode *audio = lilv_new_uri(lilv, LILV_URI_AUDIO_PORT);
    LilvNode *control = lilv_new_uri(lilv, LILV_URI_CONTROL_PORT);
    LilvNode *atom = lilv_new_uri(lilv, LILV_URI_ATOM_PORT);
    LilvNode *input = lilv_new_uri(lilv, LILV_URI_INPUT_PORT);
    LilvNode *latency = lilv_new_uri(lilv, LV2_CORE__reportsLatency);
    std::uint32_t param = 0;
    const std::uint32_t count = lilv_plugin_get_num_ports(plugin);
    for (std::uint32_t index = 0; index < count; ++index) {
        const LilvPort *port = lilv_plugin_get_port_by_index(plugin, index);
        const bool isInput = lilv_port_is_a(plugin, port, input);
        Port entry;
        entry.index = index;
        if (lilv_port_is_a(plugin, port, audio)) {
            entry.kind = isInput ? Port::Kind::AudioIn : Port::Kind::AudioOut;
            (isInput ? m_audioIns : m_audioOuts) += 1;
        } else if (lilv_port_is_a(plugin, port, control)) {
            entry.kind = isInput ? Port::Kind::ControlIn : Port::Kind::ControlOut;
            if (isInput) {
                entry.param = param++;
            } else if (lilv_port_has_property(plugin, port, latency)) {
                m_latencyPort = index;
            }
        } else if (lilv_port_is_a(plugin, port, atom)) {
            entry.kind = isInput ? Port::Kind::AtomIn : Port::Kind::AtomOut;
        }
        m_ports.push_back(entry);
    }
    for (LilvNode *node : {audio, control, atom, input, latency}) {
        lilv_node_free(node);
    }

    m_info.typeId = "lv2";
    m_info.category = dsp::BlockCategory::Plugins;
    m_info.inputs = numbered(m_audioIns == 1 ? 2 : m_audioIns, "in_");
    m_info.outputs = numbered(m_audioOuts == 1 ? 2 : m_audioOuts, "out_");

    m_sequence = m_world->map(LV2_ATOM__Sequence);
    m_chunk = m_world->map(LV2_ATOM__Chunk);
    m_mapFeature = LV2_Feature{LV2_URID__map, m_world->mapFeature()};
    m_unmapFeature = LV2_Feature{LV2_URID__unmap, m_world->unmapFeature()};
    m_features = {&m_mapFeature, &m_unmapFeature, nullptr};
}

Lv2Plugin::~Lv2Plugin()
{
    freeInstances();
}

void Lv2Plugin::freeInstances()
{
    for (LilvInstance *instance : m_instances) {
        lilv_instance_deactivate(instance);
        lilv_instance_free(instance);
    }
    m_instances.clear();
}

std::expected<void, dsp::BlockError> Lv2Plugin::prepare(const dsp::PrepareSpec &spec)
{
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > dsp::kMaxBlockFrames) {
        return std::unexpected(dsp::BlockError::UnsupportedSpec);
    }
    freeInstances();
    m_rate = spec.sampleRate;
    // **Twice for a mono plugin on a stereo lane, once a side**, unless it
    // is to hear the two sides mixed — or makes more than one side itself.
    const std::size_t count = m_audioIns == 1 && m_audioOuts <= 1 && m_mono == Mono::DualMono ? 2 : 1;
    m_controls.assign(count, std::vector<float>(m_ports.size(), 0.0f));
    m_atoms.assign(count, {});
    for (std::size_t which = 0; which < count; ++which) {
        LilvInstance *instance = lilv_plugin_instantiate(m_plugin, spec.sampleRate, m_features.data());
        if (instance == nullptr) {
            freeInstances();
            return std::unexpected(dsp::BlockError::UnsupportedSpec);
        }
        m_instances.push_back(instance);
        connectFixedPorts(instance, which);
        lilv_instance_activate(instance);
    }
    m_silence.assign(spec.maxBlockFrames, 0.0f);
    m_mix.assign(spec.maxBlockFrames, 0.0f);
    m_spare.assign(spec.maxBlockFrames, 0.0f);
    if (!m_pendingState.empty()) {
        const std::string pending = std::exchange(m_pendingState, {});
        (void)loadStateText(pending);
    }
    return {};
}

void Lv2Plugin::connectFixedPorts(LilvInstance *instance, std::size_t which)
{
    std::vector<float> &controls = m_controls[which];
    for (const Port &port : m_ports) {
        switch (port.kind) {
        case Port::Kind::ControlIn:
            controls[port.index] = m_values[port.param].load();
            lilv_instance_connect_port(instance, port.index, &controls[port.index]);
            break;
        case Port::Kind::ControlOut:
            lilv_instance_connect_port(instance, port.index, &controls[port.index]);
            break;
        case Port::Kind::AtomIn:
        case Port::Kind::AtomOut: {
            m_atoms[which].emplace_back(kAtomWords, 0);
            lilv_instance_connect_port(instance, port.index, m_atoms[which].back().data());
            break;
        }
        default:
            break;
        }
    }
}

void Lv2Plugin::process(dsp::ProcessContext &context) noexcept RB_NONBLOCKING
{
    const std::uint32_t frames = context.frames;
    if (m_instances.empty() || frames == 0 || frames > m_silence.size()) {
        for (std::uint32_t channel = 0; channel < context.output.channelCount(); ++channel) {
            const std::span<float> out = context.output.channel(channel);
            std::fill(out.begin(), out.end(), 0.0f);
        }
        return;
    }
    const auto inputOf = [&context, this](std::uint32_t channel) -> float * {
        const std::span<float> in =
            channel < context.input.channelCount() ? context.input.channel(channel) : std::span<float>{};
        return in.empty() ? m_silence.data() : in.data();
    };
    const auto outputOf = [&context, this](std::uint32_t channel) -> float * {
        const std::span<float> out =
            channel < context.output.channelCount() ? context.output.channel(channel) : std::span<float>{};
        return out.empty() ? m_spare.data() : out.data();
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
        LilvInstance *instance = m_instances[which];
        std::uint32_t audioIn = 0;
        std::uint32_t audioOut = 0;
        std::size_t atom = 0;
        for (const Port &port : m_ports) {
            switch (port.kind) {
            case Port::Kind::AudioIn: {
                float *buffer = dual ? inputOf(static_cast<std::uint32_t>(which))
                    : downmix        ? m_mix.data()
                                     : inputOf(audioIn);
                connectPort(instance, port.index, buffer);
                ++audioIn;
                break;
            }
            case Port::Kind::AudioOut: {
                float *buffer = dual ? outputOf(static_cast<std::uint32_t>(which)) : outputOf(audioOut);
                connectPort(instance, port.index, buffer);
                ++audioOut;
                break;
            }
            case Port::Kind::AtomIn: {
                // An empty sequence: nothing to say to the plugin this cycle.
                auto *sequence = reinterpret_cast<LV2_Atom_Sequence *>(m_atoms[which][atom++].data());
                sequence->atom.size = sizeof(LV2_Atom_Sequence_Body);
                sequence->atom.type = m_sequence;
                sequence->body.unit = 0;
                sequence->body.pad = 0;
                break;
            }
            case Port::Kind::AtomOut: {
                // All of it, for the plugin to write into.
                auto *chunk = reinterpret_cast<LV2_Atom *>(m_atoms[which][atom++].data());
                chunk->size = static_cast<std::uint32_t>(kAtomWords * sizeof(std::uint64_t) - sizeof(LV2_Atom));
                chunk->type = m_chunk;
                break;
            }
            default:
                break;
            }
        }
        run(instance, frames);
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

void Lv2Plugin::reset() noexcept RB_NONBLOCKING { }

void Lv2Plugin::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    if (index >= m_params.size()) {
        return;
    }
    const plugin::PluginParam &param = m_params[index];
    float actual = value;
    if (param.kind == plugin::PluginParam::Kind::Choice && !param.choices.empty()) {
        // A choice arrives as its position among the plugin's values.
        const std::vector<float> &values = m_choices[index];
        const auto position = static_cast<std::size_t>(std::max(0L, std::lround(value)));
        actual = values[std::min(position, values.size() - 1)];
    }
    setValue(index, actual);
}

void Lv2Plugin::setValue(std::uint32_t index, float value) noexcept
{
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

std::expected<void, dsp::BlockError> Lv2Plugin::setParam(std::string_view id, const dsp::ParamValue &value)
{
    for (std::uint32_t index = 0; index < m_params.size(); ++index) {
        if (m_params[index].id == id) {
            // The value itself, as a state holds it: a choice's value, not
            // its position.
            setValue(index, static_cast<float>(value.asNumber().value_or(0.0)));
            return {};
        }
    }
    return std::unexpected(dsp::BlockError::UnknownParam);
}

dsp::ParamValue Lv2Plugin::param(std::string_view id) const
{
    for (std::uint32_t index = 0; index < m_params.size(); ++index) {
        if (m_params[index].id == id) {
            return dsp::ParamValue::ofFloat(m_values[index].load(std::memory_order_relaxed));
        }
    }
    return {};
}

dsp::ParamValueMap Lv2Plugin::saveState() const
{
    dsp::ParamValueMap state;
    for (std::uint32_t index = 0; index < m_params.size(); ++index) {
        state.emplace(m_params[index].id, dsp::ParamValue::ofFloat(m_values[index].load(std::memory_order_relaxed)));
    }
    return state;
}

std::expected<void, dsp::BlockError> Lv2Plugin::loadState(const dsp::ParamValueMap &state)
{
    for (const auto &[id, value] : state) {
        (void)setParam(id, value);
    }
    return {};
}

namespace
{

struct StateCall {
    Lv2Plugin *plugin = nullptr;
    const std::vector<plugin::PluginParam> *params = nullptr;
    std::vector<std::atomic<float>> *values = nullptr;
    std::vector<float> storage;
    LV2_URID floatType = 0;
};

} // namespace

std::string Lv2Plugin::stateText() const
{
    if (m_instances.empty()) {
        return m_pendingState;
    }
    StateCall call;
    call.params = &m_params;
    call.values = const_cast<std::vector<std::atomic<float>> *>(&m_values);
    call.storage.resize(m_params.size());
    call.floatType = m_world->map(LV2_ATOM__Float);
    const auto getValue = [](const char *symbol, void *user, std::uint32_t *size, std::uint32_t *type) -> const void * {
        auto *state = static_cast<StateCall *>(user);
        for (std::size_t index = 0; index < state->params->size(); ++index) {
            if ((*state->params)[index].id == symbol) {
                state->storage[index] = (*state->values)[index].load(std::memory_order_relaxed);
                *size = sizeof(float);
                *type = state->floatType;
                return &state->storage[index];
            }
        }
        *size = 0;
        *type = 0;
        return nullptr;
    };
    LilvState *state = lilv_state_new_from_instance(m_plugin,
                                                    m_instances.front(),
                                                    m_world->mapFeature(),
                                                    nullptr,
                                                    nullptr,
                                                    nullptr,
                                                    nullptr,
                                                    getValue,
                                                    &call,
                                                    LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE,
                                                    m_features.data());
    if (state == nullptr) {
        return {};
    }
    char *text = lilv_state_to_string(
        m_world->world(), m_world->mapFeature(), m_world->unmapFeature(), state, "urn:reverblabr:state", nullptr);
    std::string result = text != nullptr ? std::string{text} : std::string{};
    lilv_free(text);
    lilv_state_free(state);
    return result;
}

bool Lv2Plugin::loadStateText(const std::string &text)
{
    if (m_instances.empty()) {
        m_pendingState = text;
        return true;
    }
    LilvState *state = lilv_state_new_from_string(m_world->world(), m_world->mapFeature(), text.c_str());
    if (state == nullptr) {
        return false;
    }
    StateCall call;
    call.plugin = this;
    call.floatType = m_world->map(LV2_ATOM__Float);
    const auto setValue =
        [](const char *symbol, void *user, const void *value, std::uint32_t size, std::uint32_t type) {
            auto *call = static_cast<StateCall *>(user);
            if (type != call->floatType || size != sizeof(float)) {
                return;
            }
            (void)call->plugin->setParam(symbol, dsp::ParamValue::ofFloat(*static_cast<const float *>(value)));
        };
    // Every instance: a mono plugin run twice is one plugin to the person.
    for (LilvInstance *instance : m_instances) {
        lilv_state_restore(
            state, instance, instance == m_instances.front() ? setValue : nullptr, &call, 0, m_features.data());
    }
    lilv_state_free(state);
    return true;
}

void Lv2Plugin::refreshLatency() noexcept
{
    m_info.latencySamples = m_latency.load(std::memory_order_relaxed);
}

} // namespace rb::pluginhost
