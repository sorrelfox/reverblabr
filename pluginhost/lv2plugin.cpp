// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "lv2plugin.h"

#include "scan.h"
#include "x11window.h"

#include <lv2/atom/atom.h>
#include <lv2/atom/util.h>
#include <lv2/core/lv2.h>
#include <lv2/presets/presets.h>
#include <lv2/state/state.h>
#include <suil/suil.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>

namespace rb::pluginhost
{

namespace
{

/// Room for what an atom port says in one cycle, in 64-bit words: 64 KiB,
/// which an LSP plugin's largest graph fits in.
constexpr std::size_t kAtomWords = 8192;
/// The most of the window's messages put into one cycle: half a sequence,
/// so a burst is spread over cycles rather than filling one.
constexpr std::size_t kAtomBytesPerCycle = kAtomWords * sizeof(std::uint64_t) / 2;
/// How many bytes of atom messages can be on their way either way.
constexpr std::size_t kAtomRingBytes = std::size_t{1} << 20;
/// How often the window is told what changed, and given its idle call.
constexpr std::chrono::milliseconds kWindowTick{33};

/// **An empty atom sequence**, for this cycle's messages: written by hand
/// rather than with LV2's forge, whose writes may go through a function
/// pointer, which the audio thread cannot vouch for.
void openSequence(std::vector<std::uint64_t> &buffer, LV2_URID type) noexcept RB_NONBLOCKING
{
    auto *sequence = reinterpret_cast<LV2_Atom_Sequence *>(buffer.data());
    sequence->atom.size = sizeof(LV2_Atom_Sequence_Body);
    sequence->atom.type = type;
    sequence->body.unit = 0;
    sequence->body.pad = 0;
}

/// The bytes a message takes in a sequence, its event's header with it.
[[nodiscard]] std::size_t eventBytes(std::uint32_t size) noexcept RB_NONBLOCKING
{
    return lv2_atom_pad_size(static_cast<std::uint32_t>(sizeof(LV2_Atom_Event) + size));
}

/// Whether a message of \a size still fits in the sequence.
[[nodiscard]] bool hasRoom(const std::vector<std::uint64_t> &buffer, std::uint32_t size) noexcept RB_NONBLOCKING
{
    const auto *sequence = reinterpret_cast<const LV2_Atom_Sequence *>(buffer.data());
    return sizeof(LV2_Atom) + sequence->atom.size + eventBytes(size) <= buffer.size() * sizeof(std::uint64_t);
}

/// Adds one message at the sequence's first frame, padded as LV2 lays
/// events out; false when there is no room for it.
bool appendEvent(std::vector<std::uint64_t> &buffer,
                 std::uint32_t type,
                 std::uint32_t size,
                 const void *body) noexcept RB_NONBLOCKING
{
    auto *sequence = reinterpret_cast<LV2_Atom_Sequence *>(buffer.data());
    const std::size_t used = sizeof(LV2_Atom) + sequence->atom.size;
    const std::size_t needed = lv2_atom_pad_size(static_cast<std::uint32_t>(sizeof(LV2_Atom_Event) + size));
    if (used + needed > buffer.size() * sizeof(std::uint64_t)) {
        return false;
    }
    auto *event = reinterpret_cast<LV2_Atom_Event *>(reinterpret_cast<std::uint8_t *>(buffer.data()) + used);
    event->time.frames = 0;
    event->body.type = type;
    event->body.size = size;
    if (size > 0) {
        std::memcpy(event + 1, body, size);
    }
    sequence->atom.size += static_cast<std::uint32_t>(needed);
    return true;
}

/// A preset's name as part of a file's: letters and digits, the rest
/// made underscores, as other LV2 hosts name theirs.
[[nodiscard]] std::string fileWord(std::string_view text)
{
    std::string word;
    for (const char c : text) {
        // **A name in any script stays itself**: UTF-8's bytes past ASCII
        // are kept, so "Тёплый" and "Мягкий" are two files, not both "______".
        const auto letter = static_cast<unsigned char>(c);
        word += std::isalnum(letter) != 0 || c == '-' || letter >= 0x80 ? c : '_';
    }
    return word.empty() ? std::string{"preset"} : word;
}

/// A short mark of a whole name, for two names whose files would be one.
[[nodiscard]] std::string nameMark(std::string_view text)
{
    std::uint32_t hash = 2166136261U; // FNV-1a
    for (const char c : text) {
        hash = (hash ^ static_cast<unsigned char>(c)) * 16777619U;
    }
    std::array<char, 9> mark{};
    std::snprintf(mark.data(), mark.size(), "%08x", hash);
    return mark.data();
}

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
    , m_toPlugin(kAtomRingBytes)
    , m_toWindow(kAtomRingBytes)
    , m_atomScratch(kAtomWords * sizeof(std::uint64_t))
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
    m_eventTransfer = m_world->map(LV2_ATOM__eventTransfer);
    m_mapFeature = LV2_Feature{LV2_URID__map, m_world->mapFeature()};
    m_unmapFeature = LV2_Feature{LV2_URID__unmap, m_world->unmapFeature()};
    m_features = {&m_mapFeature, &m_unmapFeature, nullptr};
    m_outputs = std::vector<std::atomic<float>>(m_ports.size());

    // **The window this host would show**: one that shows itself, else an
    // X11 UI it can give a parent. A Gtk or Qt UI would need that toolkit
    // in the host, which has none.
    LilvNode *x11 = lilv_new_uri(lilv, LV2_UI__X11UI);
    LilvNode *extensionData = lilv_new_uri(lilv, LV2_CORE__extensionData);
    LilvNode *showInterface = lilv_new_uri(lilv, LV2_UI__showInterface);
    LilvUIs *uis = lilv_plugin_get_uis(plugin);
    LILV_FOREACH(uis, at, uis)
    {
        const LilvUI *ui = lilv_uis_get(uis, at);
        const bool showsItself = lilv_world_ask(lilv, lilv_ui_get_uri(ui), extensionData, showInterface);
        const bool isX11 = lilv_ui_is_a(ui, x11);
        if (!showsItself && !isX11) {
            continue;
        }
        if (m_uiChoice && (m_uiChoice->showsItself || !showsItself)) {
            continue; // one that shows itself is the better of two
        }
        const LilvNodes *classes = lilv_ui_get_classes(ui);
        const LilvNode *type = isX11 ? x11 : lilv_nodes_get(classes, lilv_nodes_begin(classes));
        char *bundle = lilv_file_uri_parse(lilv_node_as_uri(lilv_ui_get_bundle_uri(ui)), nullptr);
        char *binary = lilv_file_uri_parse(lilv_node_as_uri(lilv_ui_get_binary_uri(ui)), nullptr);
        if (bundle != nullptr && binary != nullptr && type != nullptr) {
            m_uiChoice =
                UiChoice{lilv_node_as_uri(lilv_ui_get_uri(ui)), lilv_node_as_uri(type), bundle, binary, showsItself};
        }
        lilv_free(bundle);
        lilv_free(binary);
    }
    lilv_uis_free(uis);
    for (LilvNode *node : {x11, extensionData, showInterface}) {
        lilv_node_free(node);
    }
}

Lv2Plugin::~Lv2Plugin()
{
    closeUi();
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
                // **A sequence opened for this cycle's messages**, which the
                // window's are written into below, all at its first frame.
                openSequence(m_atoms[which][atom++], m_sequence);
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
    }

    // **What the window said since the last cycle**, into every instance's
    // sequence for its port: a mono plugin run twice is one plugin to the
    // person.
    // **None is lost to a full cycle**: a message that does not fit this
    // cycle's sequence, or would take it past its share, waits for the next.
    // Only one that could never fit any sequence is let go of.
    AtomRing::Header said;
    std::size_t drained = 0;
    const std::size_t sequenceBytes = kAtomWords * sizeof(std::uint64_t) - sizeof(LV2_Atom_Sequence);
    while (m_toPlugin.peek(said)) {
        if (said.size > m_atomScratch.size() || eventBytes(said.size) > sequenceBytes) {
            (void)m_toPlugin.read(said, m_atomScratch.data(), m_atomScratch.size()); // never fits: dropped
            continue;
        }
        if (drained + eventBytes(said.size) > kAtomBytesPerCycle && drained > 0) {
            break;
        }
        bool room = true;
        for (std::size_t which = 0; which < m_instances.size() && room; ++which) {
            std::size_t atomAt = 0;
            for (const Port &port : m_ports) {
                if (port.kind != Port::Kind::AtomIn && port.kind != Port::Kind::AtomOut) {
                    continue;
                }
                if (port.kind == Port::Kind::AtomIn && port.index == said.port) {
                    room = room && hasRoom(m_atoms[which][atomAt], said.size);
                }
                ++atomAt;
            }
        }
        if (!room || !m_toPlugin.read(said, m_atomScratch.data(), m_atomScratch.size())) {
            break;
        }
        drained += eventBytes(said.size);
        for (std::size_t which = 0; which < m_instances.size(); ++which) {
            std::size_t atomAt = 0;
            for (const Port &port : m_ports) {
                if (port.kind != Port::Kind::AtomIn && port.kind != Port::Kind::AtomOut) {
                    continue;
                }
                if (port.kind == Port::Kind::AtomIn && port.index == said.port) {
                    (void)appendEvent(m_atoms[which][atomAt], said.type, said.size, m_atomScratch.data());
                }
                ++atomAt;
            }
        }
    }
    for (std::size_t which = 0; which < m_instances.size(); ++which) {
        run(m_instances[which], frames);
    }

    // **What the plugin says back**, for its window: the first instance's
    // atom outputs and control outputs.
    const bool listening = m_windowListens.load(std::memory_order_acquire);
    std::size_t atomAt = 0;
    for (const Port &port : m_ports) {
        if (port.kind == Port::Kind::AtomIn) {
            ++atomAt;
        } else if (port.kind == Port::Kind::AtomOut) {
            const auto *sequence = reinterpret_cast<const LV2_Atom_Sequence *>(m_atoms[0][atomAt++].data());
            if (listening && sequence->atom.type == m_sequence) {
                LV2_ATOM_SEQUENCE_FOREACH(sequence, event)
                {
                    (void)m_toWindow.write(
                        port.index, event->body.type, event->body.size, LV2_ATOM_BODY_CONST(&event->body));
                }
            }
        } else if (port.kind == Port::Kind::ControlOut) {
            m_outputs[port.index].store(m_controls[0][port.index], std::memory_order_relaxed);
        }
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

LilvState *Lv2Plugin::captureState() const
{
    if (m_instances.empty()) {
        return nullptr;
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
    return lilv_state_new_from_instance(m_plugin,
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
}

std::string Lv2Plugin::stateText() const
{
    if (m_instances.empty()) {
        return m_pendingState;
    }
    LilvState *state = captureState();
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
    restoreState(state);
    lilv_state_free(state);
    return true;
}

void Lv2Plugin::restoreState(const LilvState *state)
{
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
    const auto restore = [&] {
        // Every instance: a mono plugin run twice is one plugin to the person.
        for (LilvInstance *instance : m_instances) {
            lilv_state_restore(
                state, instance, instance == m_instances.front() ? setValue : nullptr, &call, 0, m_features.data());
        }
    };
    // **Never during run()**, which LV2 forbids: a plugin the audio thread
    // is running is held away from it while its state goes in.
    if (events() != nullptr && events()->whileHeld) {
        events()->whileHeld(restore);
    } else {
        restore();
    }
}

void Lv2Plugin::refreshLatency() noexcept
{
    m_info.latencySamples = m_latency.load(std::memory_order_relaxed);
}

// --- the plugin's own window ---

std::optional<std::uint32_t> Lv2Plugin::paramOfPort(std::uint32_t port) const
{
    for (const Port &entry : m_ports) {
        if (entry.index == port && entry.kind == Port::Kind::ControlIn) {
            return entry.param;
        }
    }
    return std::nullopt;
}

float Lv2Plugin::positionOf(std::uint32_t index, float value) const
{
    const std::vector<float> &values = m_choices[index];
    if (m_params[index].kind != plugin::PluginParam::Kind::Choice || values.empty()) {
        return value;
    }
    std::size_t nearest = 0;
    for (std::size_t at = 1; at < values.size(); ++at) {
        if (std::abs(values[at] - value) < std::abs(values[nearest] - value)) {
            nearest = at;
        }
    }
    return static_cast<float>(nearest);
}

void Lv2Plugin::fromWindow(std::uint32_t port, float value)
{
    const std::optional<std::uint32_t> index = paramOfPort(port);
    if (!index || !std::isfinite(value)) {
        return;
    }
    // **Taken as told**: the window has it, so it is not told back, and
    // what the main thread reads of the plugin is it from now on, before
    // the audio thread has taken it.
    m_told[port] = value;
    m_values[*index].store(value, std::memory_order_relaxed);
    if (events() != nullptr) {
        if (events()->toAudio) {
            events()->toAudio(*index, positionOf(*index, value));
        }
        if (events()->changed) {
            events()->changed(*index, value);
        }
    }
}

bool Lv2Plugin::hasUi() const
{
    return m_uiChoice && (m_uiChoice->showsItself || haveDisplay());
}

Editable::Opened Lv2Plugin::openUi()
{
    if (m_ui != nullptr) {
        return Opened::Open;
    }
    if (!hasUi() || loop() == nullptr || m_instances.empty()) {
        return Opened::Unavailable;
    }
    const UiChoice &choice = *m_uiChoice;
    m_uiFeatures = {&m_mapFeature, &m_unmapFeature};
    m_idleFeature = LV2_Feature{LV2_UI__idleInterface, nullptr};
    m_uiFeatures.push_back(&m_idleFeature);
    if (!choice.showsItself) {
        // **A window of the host's own to put the UI in**, where it was
        // last; the UI asks for its size through ui:resize.
        LilvNode *name = lilv_plugin_get_name(m_plugin); // ours to free
        const std::string title = name != nullptr ? lilv_node_as_string(name) : choice.uri;
        lilv_node_free(name);
        m_window = X11Window::open(
            *loop(),
            title,
            "lv2 " + std::string{lilv_node_as_uri(lilv_plugin_get_uri(m_plugin))},
            0,
            0,
            [this](bool failed) {
                // A person closed it, or the display went.
                closeUi();
                if (events() != nullptr && events()->closed) {
                    events()->closed(failed ? "failed" : "closed");
                }
            },
            [this](int width, int height) {
                if (m_ui == nullptr) {
                    return;
                }
                if (const auto *resize =
                        static_cast<const LV2UI_Resize *>(suil_instance_extension_data(m_ui, LV2_UI__resize));
                    resize != nullptr && resize->ui_resize != nullptr) {
                    resize->ui_resize(resize->handle, width, height);
                }
            });
        if (!m_window) {
            return Opened::Unavailable;
        }
        m_parentFeature = LV2_Feature{LV2_UI__parent, reinterpret_cast<void *>(m_window->id())};
        m_resizeHost.handle = this;
        m_resizeHost.ui_resize = [](LV2UI_Feature_Handle handle, int width, int height) {
            auto *self = static_cast<Lv2Plugin *>(handle);
            if (self->m_window) {
                self->m_window->resize(width, height);
            }
            return 0;
        };
        m_resizeFeature = LV2_Feature{LV2_UI__resize, &m_resizeHost};
        m_uiFeatures.push_back(&m_parentFeature);
        m_uiFeatures.push_back(&m_resizeFeature);
    }
    m_uiFeatures.push_back(nullptr);

    m_suilHost = suil_host_new(
        [](SuilController controller,
           std::uint32_t port,
           std::uint32_t size,
           std::uint32_t protocol,
           const void *buffer) {
            auto *self = static_cast<Lv2Plugin *>(controller);
            if (protocol == 0 && size == sizeof(float)) {
                self->fromWindow(port, *static_cast<const float *>(buffer));
            } else if (protocol == self->m_eventTransfer && size >= sizeof(LV2_Atom)) {
                // An atom for the plugin, in the next cycle.
                const auto *atom = static_cast<const LV2_Atom *>(buffer);
                if (sizeof(LV2_Atom) + atom->size <= size) {
                    (void)self->m_toPlugin.write(port, atom->type, atom->size, LV2_ATOM_BODY_CONST(atom));
                }
            }
        },
        [](SuilController controller, const char *symbol) -> std::uint32_t {
            auto *self = static_cast<Lv2Plugin *>(controller);
            LilvNode *wanted = lilv_new_string(self->m_world->world(), symbol);
            const LilvPort *port = lilv_plugin_get_port_by_symbol(self->m_plugin, wanted);
            lilv_node_free(wanted);
            return port != nullptr ? lilv_port_get_index(self->m_plugin, port) : LV2UI_INVALID_PORT_INDEX;
        },
        nullptr,
        nullptr);
    suil_host_set_touch_func(m_suilHost, [](SuilController controller, std::uint32_t port, bool grabbed) {
        auto *self = static_cast<Lv2Plugin *>(controller);
        if (const std::optional<std::uint32_t> index = self->paramOfPort(port);
            index && self->events() != nullptr && self->events()->gesture) {
            self->events()->gesture(*index, grabbed);
        }
    });
    m_told.assign(m_ports.size(), std::numeric_limits<float>::quiet_NaN());
    m_toWindow.clear();
    m_windowListens.store(true, std::memory_order_release);
    m_ui = suil_instance_new(m_suilHost,
                             this,
                             choice.type.c_str(),
                             lilv_node_as_uri(lilv_plugin_get_uri(m_plugin)),
                             choice.uri.c_str(),
                             choice.type.c_str(),
                             choice.bundle.c_str(),
                             choice.binary.c_str(),
                             m_uiFeatures.data());
    if (m_ui == nullptr) {
        closeUi();
        return Opened::Failed;
    }
    m_windowScratch.assign(m_atomScratch.size() + sizeof(LV2_Atom), 0);
    // What everything is now, before it is on the screen.
    tellWindow();
    if (choice.showsItself) {
        const auto *show =
            static_cast<const LV2UI_Show_Interface *>(suil_instance_extension_data(m_ui, LV2_UI__showInterface));
        if (show == nullptr || show->show(suil_instance_get_handle(m_ui)) != 0) {
            closeUi();
            return Opened::Failed;
        }
    } else {
        m_window->show();
    }
    m_idleTimer = loop()->addTimer(kWindowTick, [this] { idleUi(); });
    return Opened::Open;
}

void Lv2Plugin::idleUi()
{
    if (m_ui == nullptr) {
        return;
    }
    tellWindow();
    if (m_window) {
        m_window->pump();
    }
    if (m_ui == nullptr) {
        return; // closed in the pump
    }
    const auto *idle =
        static_cast<const LV2UI_Idle_Interface *>(suil_instance_extension_data(m_ui, LV2_UI__idleInterface));
    if (idle != nullptr && idle->idle(suil_instance_get_handle(m_ui)) != 0) {
        // **The UI closed itself**: a person closed its own window.
        closeUi();
        if (events() != nullptr && events()->closed) {
            events()->closed("closed");
        }
    }
}

void Lv2Plugin::tellWindow()
{
    // **Every control the window has not been told of**: what the engine
    // set, what a state put back, what the plugin itself reports.
    for (const Port &port : m_ports) {
        float now = 0.0f;
        if (port.kind == Port::Kind::ControlIn) {
            now = m_values[port.param].load(std::memory_order_relaxed);
        } else if (port.kind == Port::Kind::ControlOut) {
            now = m_outputs[port.index].load(std::memory_order_relaxed);
        } else {
            continue;
        }
        if (now != m_told[port.index]) {
            m_told[port.index] = now;
            suil_instance_port_event(m_ui, port.index, sizeof(float), 0, &now);
        }
    }
    // And what the plugin said on its atom outputs.
    AtomRing::Header said;
    auto *atom = reinterpret_cast<LV2_Atom *>(m_windowScratch.data());
    while (m_ui != nullptr
           && m_toWindow.read(
               said, m_windowScratch.data() + sizeof(LV2_Atom), m_windowScratch.size() - sizeof(LV2_Atom))) {
        atom->type = said.type;
        atom->size = said.size;
        suil_instance_port_event(
            m_ui, said.port, static_cast<std::uint32_t>(sizeof(LV2_Atom) + said.size), m_eventTransfer, atom);
    }
}

void Lv2Plugin::closeUi()
{
    if (m_idleTimer && loop() != nullptr) {
        loop()->remove(*m_idleTimer);
    }
    m_idleTimer.reset();
    m_windowListens.store(false, std::memory_order_release);
    if (m_ui != nullptr) {
        if (m_uiChoice && m_uiChoice->showsItself) {
            if (const auto *show = static_cast<const LV2UI_Show_Interface *>(
                    suil_instance_extension_data(m_ui, LV2_UI__showInterface));
                show != nullptr) {
                show->hide(suil_instance_get_handle(m_ui));
            }
        }
        // The UI before the window it is inside.
        suil_instance_free(m_ui);
        m_ui = nullptr;
    }
    if (m_suilHost != nullptr) {
        suil_host_free(m_suilHost);
        m_suilHost = nullptr;
    }
    m_window.reset();
}

// --- presets ---

std::vector<plugin::PresetEntry> Lv2Plugin::presets()
{
    std::vector<plugin::PresetEntry> found;
    LilvWorld *lilv = m_world->world();
    LilvNode *preset = lilv_new_uri(lilv, LV2_PRESETS__Preset);
    LilvNode *label = lilv_new_uri(lilv, LILV_NS_RDFS "label");
    LilvNodes *related = lilv_plugin_get_related(m_plugin, preset);
    LILV_FOREACH(nodes, at, related)
    {
        const LilvNode *node = lilv_nodes_get(related, at);
        // Its file is read only now: a world loads what is asked of it.
        lilv_world_load_resource(lilv, node);
        LilvNodes *labels = lilv_world_find_nodes(lilv, node, label, nullptr);
        const LilvNode *first = labels != nullptr ? lilv_nodes_get_first(labels) : nullptr;
        found.push_back(plugin::PresetEntry{lilv_node_as_uri(node),
                                            first != nullptr ? lilv_node_as_string(first) : lilv_node_as_uri(node)});
        lilv_nodes_free(labels);
    }
    lilv_nodes_free(related);
    lilv_node_free(label);
    lilv_node_free(preset);
    std::ranges::sort(found, [](const plugin::PresetEntry &left, const plugin::PresetEntry &right) {
        return left.name != right.name ? left.name < right.name : left.id < right.id;
    });
    return found;
}

bool Lv2Plugin::loadPreset(const std::string &id)
{
    if (m_instances.empty()) {
        return false;
    }
    LilvWorld *lilv = m_world->world();
    LilvNode *node = lilv_new_uri(lilv, id.c_str());
    lilv_world_load_resource(lilv, node);
    LilvState *state = lilv_state_new_from_world(lilv, m_world->mapFeature(), node);
    lilv_node_free(node);
    if (state == nullptr) {
        return false;
    }
    restoreState(state);
    lilv_state_free(state);
    if (events() != nullptr) {
        if (events()->valuesChanged) {
            events()->valuesChanged(true);
        }
        if (events()->presetShown) {
            events()->presetShown(id);
        }
    }
    return true;
}

std::optional<std::string> Lv2Plugin::savePreset(const std::string &name)
{
    const char *home = std::getenv("HOME");
    if (home == nullptr || name.empty()) {
        return std::nullopt;
    }
    LilvState *state = captureState();
    if (state == nullptr) {
        return std::nullopt;
    }
    // **Where every LV2 host looks**: a bundle of its own in ~/.lv2, named
    // after the plugin and the preset, as other hosts name theirs.
    LilvNode *pluginName = lilv_plugin_get_name(m_plugin); // ours to free
    const std::string prefix = fileWord(pluginName != nullptr ? lilv_node_as_string(pluginName) : "plugin");
    lilv_node_free(pluginName);
    LilvWorld *lilv = m_world->world();
    // **Never over another preset**: a bundle of that file name holding a
    // preset of another name (two names made the same file name) gets a
    // mark of the whole name; saving onto the same name replaces it.
    const auto labelIn = [&](const std::filesystem::path &file) -> std::optional<std::string> {
        std::error_code missing;
        if (!std::filesystem::exists(file, missing)) {
            return std::nullopt;
        }
        LilvState *there = lilv_state_new_from_file(lilv, m_world->mapFeature(), nullptr, file.c_str());
        std::string label =
            there != nullptr && lilv_state_get_label(there) != nullptr ? lilv_state_get_label(there) : "";
        lilv_state_free(there);
        return label;
    };
    std::string word = fileWord(name);
    std::filesystem::path bundle = std::filesystem::path{home} / ".lv2" / (prefix + "_" + word + ".preset.lv2");
    if (const std::optional<std::string> there = labelIn(bundle / (word + ".ttl")); there && *there != name) {
        word += "-" + nameMark(name);
        bundle = std::filesystem::path{home} / ".lv2" / (prefix + "_" + word + ".preset.lv2");
        if (const std::optional<std::string> again = labelIn(bundle / (word + ".ttl")); again && *again != name) {
            lilv_state_free(state);
            return std::nullopt;
        }
    }
    const std::string file = word + ".ttl";
    LilvNode *uri = lilv_new_file_uri(lilv, nullptr, (bundle / file).c_str());
    lilv_state_set_label(state, name.c_str());
    const int failed = lilv_state_save(lilv,
                                       m_world->mapFeature(),
                                       m_world->unmapFeature(),
                                       state,
                                       lilv_node_as_uri(uri),
                                       (bundle.string() + "/").c_str(),
                                       file.c_str());
    lilv_state_free(state);
    std::optional<std::string> saved;
    if (failed == 0) {
        saved = lilv_node_as_uri(uri);
        // Read again, so this world lists it as every other host will.
        LilvNode *folder = lilv_new_file_uri(lilv, nullptr, (bundle.string() + "/").c_str());
        lilv_world_unload_bundle(lilv, folder);
        lilv_world_load_bundle(lilv, folder);
        lilv_node_free(folder);
    }
    lilv_node_free(uri);
    return saved;
}

} // namespace rb::pluginhost
