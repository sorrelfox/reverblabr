// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "clapplugin.h"

#include <dlfcn.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>

namespace rb::pluginhost
{

namespace
{

/// A stepped parameter with more whole numbers than this is a number,
/// whatever its words.
constexpr double kMostSteps = 128.0;
/// A stepped parameter whose words change more often than this across its
/// range is a number stepped finely, not a choice.
constexpr std::size_t kMostChoices = 64;
/// How finely the place where a stepped parameter's words change is found:
/// this part of its range.
constexpr double kFinest = 1.0 / 1024.0;
/// Room for what the plugin says a value is.
constexpr std::size_t kWordsLength = 256;

/// Whether this thread is acting for the audio thread (ClapHost::ActingAsAudio).
thread_local bool t_actingAsAudio = false;

/// **The plugin's own process call**, which CLAP puts on the audio thread:
/// made for it, which the compiler cannot know of a function pointer. The
/// claim is made here, once, as the LV2 host makes it for run().
clap_process_status processOf(const clap_plugin *plugin, const clap_process *process) noexcept RB_NONBLOCKING
{
    using Process = clap_process_status (*)(const clap_plugin *, const clap_process *) RB_NONBLOCKING;
    const auto call = reinterpret_cast<Process>(plugin->process);
    return call(plugin, process);
}

bool startProcessingOf(const clap_plugin *plugin) noexcept RB_NONBLOCKING
{
    using Start = bool (*)(const clap_plugin *) RB_NONBLOCKING;
    const auto call = reinterpret_cast<Start>(plugin->start_processing);
    return call(plugin);
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

/// The channels of each of \a plugin's audio ports, inputs or outputs.
[[nodiscard]] std::vector<std::uint32_t> portsOf(const clap_plugin *plugin, bool input)
{
    std::vector<std::uint32_t> channels;
    const auto *ports =
        static_cast<const clap_plugin_audio_ports *>(plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
    if (ports == nullptr) {
        return channels;
    }
    const std::uint32_t count = ports->count(plugin, input);
    for (std::uint32_t index = 0; index < count; ++index) {
        clap_audio_port_info info{};
        if (ports->get(plugin, index, input, &info)) {
            channels.push_back(info.channel_count);
        }
    }
    return channels;
}

[[nodiscard]] std::uint32_t sum(const std::vector<std::uint32_t> &channels)
{
    std::uint32_t total = 0;
    for (const std::uint32_t count : channels) {
        total += count;
    }
    return total;
}

[[nodiscard]] std::string lowered(std::string text)
{
    std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

/// Whether \a words are only a number: what a stepped parameter that is a
/// number says of its values.
[[nodiscard]] bool isNumber(const std::string &words)
{
    if (words.empty()) {
        return false;
    }
    char *end = nullptr;
    (void)std::strtod(words.c_str(), &end);
    while (end != nullptr && *end != '\0' && std::isspace(static_cast<unsigned char>(*end)) != 0) {
        ++end;
    }
    return end != nullptr && end != words.c_str() && *end == '\0';
}

/// **What a stepped parameter's steps are**, from what the plugin calls its
/// values (see clapParams()).
void describeSteps(const clap_plugin *plugin,
                   const clap_plugin_params *params,
                   const clap_param_info &info,
                   plugin::PluginParam &param)
{
    const auto words = [&](double value) {
        std::array<char, kWordsLength> text{};
        if (!params->value_to_text(plugin, info.id, value, text.data(), static_cast<std::uint32_t>(text.size()))) {
            return std::string{};
        }
        return std::string{text.data()};
    };
    const double low = std::min(info.min_value, info.max_value);
    const double high = std::max(info.min_value, info.max_value);
    const double span = high - low;
    const bool whole = low == std::floor(low) && high == std::floor(high);

    if (whole && span > kMostSteps) {
        param.kind = plugin::PluginParam::Kind::Int;
        return;
    }
    if (whole && span >= 2.0) {
        bool numbers = true;
        std::vector<std::pair<float, std::string>> choices;
        for (double value = low; value <= high; value += 1.0) {
            std::string said = words(value);
            numbers = numbers && isNumber(said);
            choices.emplace_back(static_cast<float>(value), std::move(said));
        }
        if (numbers) {
            param.kind = plugin::PluginParam::Kind::Int;
        } else {
            param.kind = plugin::PluginParam::Kind::Choice;
            param.choices = std::move(choices);
        }
        return;
    }
    if (whole && span == 1.0) {
        // Two whole numbers: two values, unless the words change between
        // them too, which is a plugin stepping through fractions.
        const std::string first = words(low);
        const std::string last = words(high);
        const std::string middle = words(low + span / 2.0);
        if (middle == first || middle == last) {
            const bool onOff = lowered(first) == "off" && lowered(last) == "on";
            param.kind = onOff ? plugin::PluginParam::Kind::Toggle : plugin::PluginParam::Kind::Choice;
            if (!onOff) {
                param.choices = {{static_cast<float>(low), first}, {static_cast<float>(high), last}};
            }
            return;
        }
    }
    if (span <= 0.0) {
        param.kind = plugin::PluginParam::Kind::Float;
        return;
    }
    // **Where the words change**, found by halving: a stretch whose two ends
    // say the same is taken to say it throughout, and one whose ends differ
    // is halved until the change is pinned down. Each stretch of the same
    // words is one choice, at a whole number inside it if there is one,
    // otherwise at its middle, which is inside whatever the plugin rounds
    // to.
    const double finest = span * kFinest;
    std::vector<std::pair<double, std::string>> starts{{low, words(low)}};
    bool tooMany = false;
    const auto split = [&](auto &self, double from, const std::string &left, double to, const std::string &right) {
        if (left == right || tooMany) {
            return;
        }
        if (to - from <= finest) {
            starts.emplace_back(to, right);
            tooMany = starts.size() > kMostChoices;
            return;
        }
        const double middle = (from + to) / 2.0;
        const std::string said = words(middle);
        self(self, from, left, middle, said);
        self(self, middle, said, to, right);
    };
    split(split, low, starts.front().second, high, words(high));
    if (tooMany) {
        // Words for every step: a number, stepped finer than a choice.
        param.kind = plugin::PluginParam::Kind::Float;
        return;
    }
    std::vector<std::pair<float, std::string>> choices;
    for (std::size_t index = 0; index < starts.size(); ++index) {
        const double from = starts[index].first;
        const double to = index + 1 < starts.size() ? starts[index + 1].first - finest : high;
        double value = (from + to) / 2.0;
        if (const double inside = std::ceil(from); inside <= to) {
            value = inside;
        }
        choices.emplace_back(static_cast<float>(value), starts[index].second);
    }
    param.kind = plugin::PluginParam::Kind::Choice;
    param.choices = std::move(choices);
}

/// **The plugin's own word for what it is**, from its features: the first
/// that says what it does, rather than that it is an effect or how many
/// channels it has. "compressor" becomes "Compressor".
[[nodiscard]] std::string categoryOf(const clap_plugin_descriptor *descriptor)
{
    static constexpr std::array<std::string_view, 9> kKinds = {CLAP_PLUGIN_FEATURE_INSTRUMENT,
                                                               CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
                                                               CLAP_PLUGIN_FEATURE_NOTE_EFFECT,
                                                               CLAP_PLUGIN_FEATURE_NOTE_DETECTOR,
                                                               CLAP_PLUGIN_FEATURE_ANALYZER,
                                                               CLAP_PLUGIN_FEATURE_MONO,
                                                               CLAP_PLUGIN_FEATURE_STEREO,
                                                               CLAP_PLUGIN_FEATURE_SURROUND,
                                                               CLAP_PLUGIN_FEATURE_AMBISONIC};
    std::string found;
    for (const char *const *feature = descriptor->features; feature != nullptr && *feature != nullptr; ++feature) {
        if (std::ranges::find(kKinds, std::string_view{*feature}) == kKinds.end()) {
            found = *feature;
            break;
        }
        if (found.empty() && std::string_view{*feature} == CLAP_PLUGIN_FEATURE_ANALYZER) {
            found = *feature;
        }
    }
    std::ranges::replace(found, '-', ' ');
    if (!found.empty()) {
        found[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(found[0])));
    }
    return found;
}

/// Base64, for the plugin's state in a session file.
constexpr std::string_view kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

[[nodiscard]] std::string toBase64(const std::string &bytes)
{
    std::string text;
    text.reserve((bytes.size() + 2) / 3 * 4);
    std::size_t at = 0;
    while (at + 2 < bytes.size()) {
        const auto triple = (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at])) << 16)
            | (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + 1])) << 8)
            | static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + 2]));
        for (const int shift : {18, 12, 6, 0}) {
            text += kAlphabet[(triple >> shift) & 0x3f];
        }
        at += 3;
    }
    if (const std::size_t left = bytes.size() - at; left > 0) {
        std::uint32_t triple = static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at])) << 16;
        if (left == 2) {
            triple |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + 1])) << 8;
        }
        text += kAlphabet[(triple >> 18) & 0x3f];
        text += kAlphabet[(triple >> 12) & 0x3f];
        text += left == 2 ? kAlphabet[(triple >> 6) & 0x3f] : '=';
        text += '=';
    }
    return text;
}

[[nodiscard]] std::optional<std::string> fromBase64(const std::string &text)
{
    std::string bytes;
    std::uint32_t gathered = 0;
    int bits = 0;
    for (const char c : text) {
        if (c == '=' || std::isspace(static_cast<unsigned char>(c)) != 0) {
            continue;
        }
        const std::size_t value = kAlphabet.find(c);
        if (value == std::string_view::npos) {
            return std::nullopt;
        }
        gathered = (gathered << 6) | static_cast<std::uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            bytes += static_cast<char>((gathered >> bits) & 0xff);
        }
    }
    return bytes;
}

/// **The changes of one cycle, as CLAP's input events.**
struct InputEvents {
    clap_input_events events{};
    const std::vector<clap_event_param_value> *items = nullptr;
    std::uint32_t count = 0;
};

std::uint32_t sizeOf(const clap_input_events *list) noexcept
{
    return static_cast<const InputEvents *>(list->ctx)->count;
}

const clap_event_header *eventAt(const clap_input_events *list, std::uint32_t index) noexcept
{
    const auto *events = static_cast<const InputEvents *>(list->ctx);
    return index < events->count ? &(*events->items)[index].header : nullptr;
}

/// **What the plugin says back**: a value it changed itself is taken as
/// what the setting now is. Anything else it says is not needed.
struct OutputEvents {
    clap_output_events events{};
    const std::vector<std::pair<clap_id, std::uint32_t>> *byId = nullptr;
    std::vector<std::atomic<float>> *values = nullptr;
};

bool pushed(const clap_output_events *list, const clap_event_header *event) noexcept
{
    const auto *out = static_cast<const OutputEvents *>(list->ctx);
    if (event->space_id != CLAP_CORE_EVENT_SPACE_ID || event->type != CLAP_EVENT_PARAM_VALUE) {
        return true;
    }
    const auto *change = reinterpret_cast<const clap_event_param_value *>(event);
    const auto found = std::ranges::lower_bound(
        *out->byId, change->param_id, {}, [](const std::pair<clap_id, std::uint32_t> &entry) { return entry.first; });
    if (found != out->byId->end() && found->first == change->param_id) {
        (*out->values)[found->second].store(static_cast<float>(change->value), std::memory_order_relaxed);
    }
    return true;
}

} // namespace

// --- ClapLibrary ---

std::shared_ptr<ClapLibrary> ClapLibrary::open(const std::string &path)
{
    void *handle = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        std::fprintf(stderr, "not a loadable CLAP file: %s\n", ::dlerror());
        return nullptr;
    }
    const auto *entry = static_cast<const clap_plugin_entry *>(::dlsym(handle, "clap_entry"));
    if (entry == nullptr || !clap_version_is_compatible(entry->clap_version) || !entry->init(path.c_str())) {
        std::fprintf(stderr, "not a CLAP file this host can use: %s\n", path.c_str());
        ::dlclose(handle);
        return nullptr;
    }
    const auto *factory = static_cast<const clap_plugin_factory *>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (factory == nullptr) {
        entry->deinit();
        ::dlclose(handle);
        return nullptr;
    }
    std::shared_ptr<ClapLibrary> library(new ClapLibrary());
    library->m_handle = handle;
    library->m_entry = entry;
    library->m_factory = factory;
    return library;
}

ClapLibrary::~ClapLibrary()
{
    // **Not unloaded**: what a plugin leaves running in the file (a thread,
    // a destructor at exit) must still have its code there. The host ends
    // soon after anyway.
    m_entry->deinit();
}

std::uint32_t ClapLibrary::count() const
{
    return m_factory->get_plugin_count(m_factory);
}

const clap_plugin_descriptor *ClapLibrary::descriptorAt(std::uint32_t index) const
{
    return m_factory->get_plugin_descriptor(m_factory, index);
}

const clap_plugin *ClapLibrary::create(const clap_host *host, std::string_view id) const
{
    const std::string wanted{id};
    const clap_plugin *plugin = m_factory->create_plugin(m_factory, host, wanted.c_str());
    if (plugin == nullptr) {
        return nullptr;
    }
    if (!plugin->init(plugin)) {
        plugin->destroy(plugin);
        return nullptr;
    }
    return plugin;
}

// --- ClapHost ---

ClapHost::ClapHost()
    : m_main(std::this_thread::get_id())
{
    m_host.clap_version = CLAP_VERSION;
    m_host.host_data = this;
    m_host.name = "Reverblabr";
    m_host.vendor = "Reverblabr";
    m_host.url = "";
    m_host.version = "1";
    m_host.get_extension = &ClapHost::extension;
    m_host.request_restart = [](const clap_host *host) { self(host)->m_restart.store(true); };
    m_host.request_process = [](const clap_host *) {
        // The host's node always runs: there is nothing to wake.
    };
    m_host.request_callback = [](const clap_host *host) { self(host)->m_callback.store(true); };
}

ClapHost *ClapHost::self(const clap_host *host)
{
    return static_cast<ClapHost *>(host->host_data);
}

ClapHost::ActingAsAudio::ActingAsAudio()
{
    t_actingAsAudio = true;
}

ClapHost::ActingAsAudio::~ActingAsAudio()
{
    t_actingAsAudio = false;
}

const void *ClapHost::extension(const clap_host *, const char *id)
{
    static const clap_host_log log{[](const clap_host *, clap_log_severity severity, const char *message) {
        if (severity >= CLAP_LOG_WARNING) {
            std::fprintf(stderr, "plugin: %s\n", message);
        }
    }};
    static const clap_host_thread_check threads{
        [](const clap_host *host) { return !t_actingAsAudio && std::this_thread::get_id() == self(host)->m_main; },
        [](const clap_host *host) { return t_actingAsAudio || std::this_thread::get_id() != self(host)->m_main; }};
    static const clap_host_params params{[](const clap_host *, clap_param_rescan_flags) { },
                                         [](const clap_host *, clap_id, clap_param_clear_flags) { },
                                         [](const clap_host *) {
                                             // The host's node always runs, so a change is flushed with the
                                             // next cycle anyway.
                                         }};
    static const clap_host_latency latency{[](const clap_host *) {
        // Asked for again by the host's timer, which is where a latency is
        // told to the engine.
    }};
    static const clap_host_state state{[](const clap_host *) { }};
    static const clap_host_audio_ports ports{[](const clap_host *, std::uint32_t) { return false; },
                                             [](const clap_host *, std::uint32_t) { }};
    const std::string_view wanted{id};
    if (wanted == CLAP_EXT_LOG) {
        return &log;
    }
    if (wanted == CLAP_EXT_THREAD_CHECK) {
        return &threads;
    }
    if (wanted == CLAP_EXT_PARAMS) {
        return &params;
    }
    if (wanted == CLAP_EXT_LATENCY) {
        return &latency;
    }
    if (wanted == CLAP_EXT_STATE) {
        return &state;
    }
    if (wanted == CLAP_EXT_AUDIO_PORTS) {
        return &ports;
    }
    return nullptr;
}

// --- describing ---

std::vector<plugin::PluginParam>
clapParams(const clap_plugin *plugin, std::vector<clap_id> *ids, std::vector<void *> *cookies)
{
    std::vector<plugin::PluginParam> found;
    const auto *params = static_cast<const clap_plugin_params *>(plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    if (params == nullptr) {
        return found;
    }
    const std::uint32_t count = params->count(plugin);
    for (std::uint32_t index = 0; index < count; ++index) {
        clap_param_info info{};
        if (!params->get_info(plugin, index, &info)
            || (info.flags & (CLAP_PARAM_IS_HIDDEN | CLAP_PARAM_IS_READONLY)) != 0) {
            continue;
        }
        plugin::PluginParam param;
        param.id = std::to_string(info.id);
        param.name = info.module[0] != '\0' ? std::string{info.module} + "/" + info.name : std::string{info.name};
        param.minimum = static_cast<float>(std::min(info.min_value, info.max_value));
        param.maximum = static_cast<float>(std::max(info.min_value, info.max_value));
        param.defaultValue = std::clamp(static_cast<float>(info.default_value), param.minimum, param.maximum);
        if ((info.flags & CLAP_PARAM_IS_STEPPED) != 0) {
            describeSteps(plugin, params, info, param);
        }
        found.push_back(std::move(param));
        if (ids != nullptr) {
            ids->push_back(info.id);
        }
        if (cookies != nullptr) {
            cookies->push_back(info.cookie);
        }
    }
    return found;
}

bool describeClap(const std::string &path, const std::function<void(const plugin::PluginDescription &)> &found)
{
    const std::shared_ptr<ClapLibrary> library = ClapLibrary::open(path);
    if (!library) {
        return false;
    }
    ClapHost host;
    // **Each plugin is let go once the next is made**: a file's plugins may
    // share what the last one alive keeps running. LSP's share a worker
    // thread that takes some 50 ms to stop, and letting each go first
    // stopped and started it again for every plugin: 6.5 s for its 194.
    const clap_plugin *previous = nullptr;
    const std::uint32_t count = library->count();
    for (std::uint32_t index = 0; index < count; ++index) {
        const clap_plugin_descriptor *descriptor = library->descriptorAt(index);
        if (descriptor == nullptr || descriptor->id == nullptr) {
            continue;
        }
        plugin::PluginDescription description;
        description.format = "clap";
        description.id = descriptor->id;
        description.name = descriptor->name != nullptr ? descriptor->name : descriptor->id;
        description.vendor = descriptor->vendor != nullptr ? descriptor->vendor : "";
        description.category = categoryOf(descriptor);
        const clap_plugin *plugin = library->create(host.host(), descriptor->id);
        if (previous != nullptr) {
            previous->destroy(previous);
            previous = nullptr;
        }
        if (plugin == nullptr) {
            // One that will not start is not offered: its block could not
            // run it.
            std::fprintf(stderr, "the plugin %s would not start\n", descriptor->id);
            continue;
        }
        description.audioInputs = sum(portsOf(plugin, true));
        description.audioOutputs = sum(portsOf(plugin, false));
        description.params = clapParams(plugin);
        description.parameters = static_cast<std::uint32_t>(description.params.size());
        previous = plugin;
        found(description);
    }
    if (previous != nullptr) {
        previous->destroy(previous);
    }
    return true;
}

// --- ClapPlugin ---

std::unique_ptr<ClapPlugin>
ClapPlugin::make(const std::shared_ptr<ClapLibrary> &library, const std::string &id, Mono mono)
{
    std::unique_ptr<ClapPlugin> made(new ClapPlugin(library, id, mono));
    if (made->m_instances.empty()) {
        return nullptr;
    }
    return made;
}

ClapPlugin::ClapPlugin(std::shared_ptr<ClapLibrary> library, std::string id, Mono mono)
    : m_library(std::move(library))
    , m_id(std::move(id))
    , m_mono(mono)
    , m_host(std::make_unique<ClapHost>())
{
    const clap_plugin *first = m_library->create(m_host->host(), m_id);
    if (first == nullptr) {
        return;
    }
    m_instances.push_back(first);
    m_params = clapParams(first, &m_ids, &m_cookies);
    m_values = std::vector<std::atomic<float>>(m_params.size());
    for (std::uint32_t index = 0; index < m_params.size(); ++index) {
        m_choices.push_back(choiceValues(m_params[index]));
        m_values[index].store(m_params[index].defaultValue);
        m_byId.emplace_back(m_ids[index], index);
        dsp::ParamDescriptor descriptor;
        descriptor.id = m_params[index].id;
        descriptor.type = dsp::ParamType::Float;
        descriptor.defaultValue = dsp::ParamValue::ofFloat(m_params[index].defaultValue);
        m_descriptors.push_back(std::move(descriptor));
    }
    std::ranges::sort(m_byId);
    m_changed.assign(m_params.size(), 0);
    m_changedList.assign(m_params.size(), 0);
    m_events.resize(m_params.size());

    m_inputPorts = portsOf(first, true);
    m_outputPorts = portsOf(first, false);
    m_audioIns = sum(m_inputPorts);
    m_audioOuts = sum(m_outputPorts);
    m_info.typeId = "clap";
    m_info.category = dsp::BlockCategory::Plugins;
    m_info.inputs = numbered(m_audioIns == 1 ? 2 : m_audioIns, "in_");
    m_info.outputs = numbered(m_audioOuts == 1 ? 2 : m_audioOuts, "out_");
}

ClapPlugin::~ClapPlugin()
{
    freeInstances();
}

void ClapPlugin::freeInstances()
{
    for (std::size_t which = 0; which < m_instances.size(); ++which) {
        const clap_plugin *instance = m_instances[which];
        if (which < m_processing.size() && m_processing[which] != 0) {
            // The audio thread has let go of this plugin (the host swaps a
            // block out between cycles), so this thread stands in for it.
            const ClapHost::ActingAsAudio acting;
            instance->stop_processing(instance);
        }
        if (m_active) {
            instance->deactivate(instance);
        }
        instance->destroy(instance);
    }
    m_instances.clear();
    m_processing.clear();
    m_active = false;
}

std::expected<void, dsp::BlockError> ClapPlugin::prepare(const dsp::PrepareSpec &spec)
{
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > dsp::kMaxBlockFrames
        || m_instances.empty()) {
        return std::unexpected(dsp::BlockError::UnsupportedSpec);
    }
    // Once, like the LV2 host: a plugin at another rate is another block.
    if (m_active) {
        return std::unexpected(dsp::BlockError::UnsupportedSpec);
    }
    // **Twice for a mono plugin on a stereo lane, once a side**, unless it
    // is to hear the two sides mixed — or makes more than one side itself.
    if (m_audioIns == 1 && m_audioOuts <= 1 && m_mono == Mono::DualMono) {
        const clap_plugin *second = m_library->create(m_host->host(), m_id);
        if (second == nullptr) {
            return std::unexpected(dsp::BlockError::UnsupportedSpec);
        }
        m_instances.push_back(second);
    }
    flushValues();
    for (const clap_plugin *instance : m_instances) {
        if (!instance->activate(instance, spec.sampleRate, 1, spec.maxBlockFrames)) {
            // The ones already active are let go with the rest.
            for (const clap_plugin *started : m_instances) {
                if (started == instance) {
                    break;
                }
                started->deactivate(started);
            }
            return std::unexpected(dsp::BlockError::UnsupportedSpec);
        }
    }
    m_active = true;
    m_processing.assign(m_instances.size(), 0);

    const std::size_t count = m_instances.size();
    m_inBuffers.assign(count, std::vector<clap_audio_buffer>(m_inputPorts.size()));
    m_outBuffers.assign(count, std::vector<clap_audio_buffer>(m_outputPorts.size()));
    m_inChannels.assign(count, std::vector<float *>(std::max<std::uint32_t>(m_audioIns, 1), nullptr));
    m_outChannels.assign(count, std::vector<float *>(std::max<std::uint32_t>(m_audioOuts, 1), nullptr));
    for (std::size_t which = 0; which < count; ++which) {
        std::uint32_t at = 0;
        for (std::size_t port = 0; port < m_inputPorts.size(); ++port) {
            clap_audio_buffer &buffer = m_inBuffers[which][port];
            buffer.data32 = m_inChannels[which].data() + at;
            buffer.channel_count = m_inputPorts[port];
            at += m_inputPorts[port];
        }
        at = 0;
        for (std::size_t port = 0; port < m_outputPorts.size(); ++port) {
            clap_audio_buffer &buffer = m_outBuffers[which][port];
            buffer.data32 = m_outChannels[which].data() + at;
            buffer.channel_count = m_outputPorts[port];
            at += m_outputPorts[port];
        }
    }
    m_silence.assign(spec.maxBlockFrames, 0.0f);
    m_mix.assign(spec.maxBlockFrames, 0.0f);
    m_spare.assign(spec.maxBlockFrames, 0.0f);
    refreshLatency();
    if (!m_pendingState.empty()) {
        const std::string pending = std::exchange(m_pendingState, {});
        (void)loadStateText(pending);
    }
    return {};
}

void ClapPlugin::flushValues()
{
    const auto *params = static_cast<const clap_plugin_params *>(
        m_instances.front()->get_extension(m_instances.front(), CLAP_EXT_PARAMS));
    if (params == nullptr || m_params.empty()) {
        return;
    }
    std::vector<clap_event_param_value> changes;
    for (std::uint32_t index = 0; index < m_params.size(); ++index) {
        clap_event_param_value change{};
        change.header = {sizeof(clap_event_param_value), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
        change.param_id = m_ids[index];
        change.cookie = m_cookies[index];
        change.note_id = -1;
        change.port_index = -1;
        change.channel = -1;
        change.key = -1;
        change.value = m_values[index].load(std::memory_order_relaxed);
        changes.push_back(change);
    }
    InputEvents in;
    in.items = &changes;
    in.count = static_cast<std::uint32_t>(changes.size());
    in.events.ctx = &in;
    in.events.size = &sizeOf;
    in.events.get = &eventAt;
    OutputEvents out;
    out.byId = &m_byId;
    out.values = &m_values;
    out.events.ctx = &out;
    out.events.try_push = &pushed;
    for (const clap_plugin *instance : m_instances) {
        const auto *theirs =
            static_cast<const clap_plugin_params *>(instance->get_extension(instance, CLAP_EXT_PARAMS));
        theirs->flush(instance, &in.events, &out.events);
    }
}

void ClapPlugin::process(dsp::ProcessContext &context) noexcept RB_NONBLOCKING
{
    const std::uint32_t frames = context.frames;
    const auto silenceOutputs = [&context] {
        for (std::uint32_t channel = 0; channel < context.output.channelCount(); ++channel) {
            const std::span<float> out = context.output.channel(channel);
            std::fill(out.begin(), out.end(), 0.0f);
        }
    };
    if (!m_active || frames == 0 || frames > m_silence.size()) {
        silenceOutputs();
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

    // The changes since the last cycle, as events at its first sample.
    m_eventCount = 0;
    for (std::uint32_t at = 0; at < m_changedCount; ++at) {
        const std::uint32_t index = m_changedList[at];
        m_changed[index] = 0;
        clap_event_param_value &change = m_events[m_eventCount++];
        change.header = {sizeof(clap_event_param_value), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
        change.param_id = m_ids[index];
        change.cookie = m_cookies[index];
        change.note_id = -1;
        change.port_index = -1;
        change.channel = -1;
        change.key = -1;
        change.value = m_values[index].load(std::memory_order_relaxed);
    }
    m_changedCount = 0;
    InputEvents in;
    in.items = &m_events;
    in.count = m_eventCount;
    in.events.ctx = &in;
    in.events.size = &sizeOf;
    in.events.get = &eventAt;
    OutputEvents out;
    out.byId = &m_byId;
    out.values = &m_values;
    out.events.ctx = &out;
    out.events.try_push = &pushed;

    for (std::size_t which = 0; which < m_instances.size(); ++which) {
        const clap_plugin *instance = m_instances[which];
        if (m_processing[which] == 0) {
            if (!startProcessingOf(instance)) {
                silenceOutputs();
                return;
            }
            m_processing[which] = 1;
        }
        std::vector<float *> &ins = m_inChannels[which];
        std::vector<float *> &outs = m_outChannels[which];
        for (std::uint32_t channel = 0; channel < m_audioIns; ++channel) {
            ins[channel] = dual ? inputOf(static_cast<std::uint32_t>(which))
                : downmix       ? m_mix.data()
                                : inputOf(channel);
        }
        for (std::uint32_t channel = 0; channel < m_audioOuts; ++channel) {
            outs[channel] = dual ? outputOf(static_cast<std::uint32_t>(which)) : outputOf(channel);
        }
        clap_process process{};
        process.steady_time = m_steadyTime;
        process.frames_count = frames;
        process.transport = nullptr;
        process.audio_inputs = m_inBuffers[which].data();
        process.audio_outputs = m_outBuffers[which].data();
        process.audio_inputs_count = static_cast<std::uint32_t>(m_inBuffers[which].size());
        process.audio_outputs_count = static_cast<std::uint32_t>(m_outBuffers[which].size());
        process.in_events = &in.events;
        process.out_events = &out.events;
        if (processOf(instance, &process) == CLAP_PROCESS_ERROR) {
            silenceOutputs();
            return;
        }
    }
    m_steadyTime += frames;

    // A mono output goes to both sides.
    if (m_audioOuts == 1 && !dual && context.output.channelCount() > 1) {
        const std::span<float> left = context.output.channel(0);
        const std::span<float> right = context.output.channel(1);
        if (!left.empty() && !right.empty()) {
            std::copy_n(left.begin(), frames, right.begin());
        }
    }
}

void ClapPlugin::reset() noexcept RB_NONBLOCKING { }

void ClapPlugin::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    if (index >= m_params.size()) {
        return;
    }
    float actual = value;
    if (m_params[index].kind == plugin::PluginParam::Kind::Choice && !m_choices[index].empty()) {
        // A choice arrives as its position among the plugin's values.
        const std::vector<float> &values = m_choices[index];
        const auto position = static_cast<std::size_t>(std::max(0L, std::lround(value)));
        actual = values[std::min(position, values.size() - 1)];
    }
    remember(index, actual);
    if (m_changed[index] == 0) {
        m_changed[index] = 1;
        m_changedList[m_changedCount++] = index;
    }
}

void ClapPlugin::remember(std::uint32_t index, float value) noexcept
{
    m_values[index].store(value, std::memory_order_relaxed);
}

std::expected<void, dsp::BlockError> ClapPlugin::setParam(std::string_view id, const dsp::ParamValue &value)
{
    for (std::uint32_t index = 0; index < m_params.size(); ++index) {
        if (m_params[index].id == id) {
            // The value itself, as a state holds it; it reaches the plugin
            // when it starts. Once it runs, a change comes through
            // applyParam().
            remember(index, static_cast<float>(value.asNumber().value_or(0.0)));
            return {};
        }
    }
    return std::unexpected(dsp::BlockError::UnknownParam);
}

dsp::ParamValue ClapPlugin::param(std::string_view id) const
{
    for (std::uint32_t index = 0; index < m_params.size(); ++index) {
        if (m_params[index].id == id) {
            return dsp::ParamValue::ofFloat(m_values[index].load(std::memory_order_relaxed));
        }
    }
    return {};
}

dsp::ParamValueMap ClapPlugin::saveState() const
{
    dsp::ParamValueMap state;
    for (std::uint32_t index = 0; index < m_params.size(); ++index) {
        state.emplace(m_params[index].id, dsp::ParamValue::ofFloat(m_values[index].load(std::memory_order_relaxed)));
    }
    return state;
}

std::expected<void, dsp::BlockError> ClapPlugin::loadState(const dsp::ParamValueMap &state)
{
    for (const auto &[id, value] : state) {
        (void)setParam(id, value);
    }
    return {};
}

std::string ClapPlugin::stateText() const
{
    if (!m_active) {
        return m_pendingState;
    }
    const clap_plugin *first = m_instances.front();
    const auto *state = static_cast<const clap_plugin_state *>(first->get_extension(first, CLAP_EXT_STATE));
    if (state == nullptr) {
        return {};
    }
    std::string bytes;
    clap_ostream stream{};
    stream.ctx = &bytes;
    stream.write = [](const clap_ostream *out, const void *buffer, std::uint64_t size) -> std::int64_t {
        static_cast<std::string *>(out->ctx)->append(static_cast<const char *>(buffer), size);
        return static_cast<std::int64_t>(size);
    };
    if (!state->save(first, &stream)) {
        return {};
    }
    return toBase64(bytes);
}

bool ClapPlugin::loadStateText(const std::string &text)
{
    if (!m_active) {
        m_pendingState = text;
        return true;
    }
    const std::optional<std::string> bytes = fromBase64(text);
    if (!bytes) {
        return false;
    }
    struct Reading {
        const std::string *bytes;
        std::size_t at;
    };
    // Every instance: a mono plugin run twice is one plugin to the person.
    for (const clap_plugin *instance : m_instances) {
        const auto *state = static_cast<const clap_plugin_state *>(instance->get_extension(instance, CLAP_EXT_STATE));
        if (state == nullptr) {
            return false;
        }
        Reading reading{&*bytes, 0};
        clap_istream stream{};
        stream.ctx = &reading;
        stream.read = [](const clap_istream *in, void *buffer, std::uint64_t size) -> std::int64_t {
            auto *from = static_cast<Reading *>(in->ctx);
            const std::size_t left = from->bytes->size() - from->at;
            const std::size_t taken = std::min<std::size_t>(left, size);
            std::memcpy(buffer, from->bytes->data() + from->at, taken);
            from->at += taken;
            return static_cast<std::int64_t>(taken);
        };
        if (!state->load(instance, &stream)) {
            return false;
        }
    }
    readValues();
    return true;
}

void ClapPlugin::readValues()
{
    const clap_plugin *first = m_instances.front();
    const auto *params = static_cast<const clap_plugin_params *>(first->get_extension(first, CLAP_EXT_PARAMS));
    if (params == nullptr) {
        return;
    }
    for (std::uint32_t index = 0; index < m_params.size(); ++index) {
        double value = 0.0;
        if (params->get_value(first, m_ids[index], &value)) {
            remember(index, static_cast<float>(value));
        }
    }
}

void ClapPlugin::refreshLatency() noexcept
{
    if (!m_active) {
        return;
    }
    const clap_plugin *first = m_instances.front();
    const auto *latency = static_cast<const clap_plugin_latency *>(first->get_extension(first, CLAP_EXT_LATENCY));
    m_info.latencySamples = latency != nullptr ? latency->get(first) : 0U;
}

bool ClapPlugin::idle()
{
    if (m_host->takeCallback()) {
        for (const clap_plugin *instance : m_instances) {
            instance->on_main_thread(instance);
        }
    }
    return m_host->takeRestart();
}

} // namespace rb::pluginhost
