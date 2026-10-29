// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The tests' CLAP plugin: a stereo gain, parameter 1, from 0 to 4.
//
// **Its "window" is a stand-in that needs no display**: it says it can
// float as an X11 window, and when shown it asks the host for a timer and
// watches a descriptor of its own through the host, as a real window's
// toolkit would. On its timer it behaves as a person: it takes hold of the
// gain, moves it to a quarter, rests for longer than a guessed gesture
// lasts, moves it to 0.3 and lets go, all as output events from its process
// call. It closes itself, as a person closing it would, once the file
// <log>.close is there. It writes down, to the file RB_TEST_CLAP_LOG names:
//
//   shown | hidden | destroyed     what the host did with the window
//   fd                             its descriptor was read
//   gain <value>                   the value the host last set, as it heard it
//
// **Its presets**: one of its own ("Half", 0.5), and a file of type
// ".rbgain" for each in the folder RB_TEST_CLAP_PRESETS names, holding the
// gain as text; through preset discovery, put in through preset-load.

#include <clap/clap.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <new>
#include <string>

namespace
{

constexpr clap_id kGain = 1;
constexpr const char *kId = "org.reverblabr.test.gain";
/// Timer calls between the two moves, at ten milliseconds each: more than
/// the half second a guessed gesture ends after.
constexpr int kHeldFor = 70;

const char *const kFeatures[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, nullptr};

const clap_plugin_descriptor g_descriptor{CLAP_VERSION,
                                          kId,
                                          "Reverblabr test gain",
                                          "Reverblabr",
                                          "",
                                          "",
                                          "",
                                          "1",
                                          "The tests' CLAP plugin",
                                          kFeatures};

struct Plugin {
    clap_plugin plugin{};
    const clap_host *host = nullptr;
    std::atomic<float> gain{1.0f};
    /// The last value the host sent, for the log, written from the timer.
    std::atomic<float> heard{-1.0f};
    float logged = -1.0f;
    /// What the window wants said in the next process call: 1 hold and
    /// move to a quarter, 2 move to 0.3 and let go.
    std::atomic<int> toSay{0};
    bool created = false;
    bool shown = false;
    clap_id timer = CLAP_INVALID_ID;
    int ticks = 0;
    int fd = -1;
    std::string log;

    void note(const std::string &line) const
    {
        if (log.empty()) {
            return;
        }
        if (std::FILE *file = std::fopen(log.c_str(), "a"); file != nullptr) {
            std::fprintf(file, "%s\n", line.c_str());
            std::fclose(file);
        }
    }

    template <typename Extension>
    [[nodiscard]] const Extension *hostExtension(const char *id) const
    {
        return static_cast<const Extension *>(host->get_extension(host, id));
    }
};

Plugin *self(const clap_plugin *plugin)
{
    return static_cast<Plugin *>(plugin->plugin_data);
}

// --- parameters ---

std::uint32_t paramCount(const clap_plugin *)
{
    return 1;
}

bool paramInfo(const clap_plugin *, std::uint32_t index, clap_param_info *info)
{
    if (index != 0) {
        return false;
    }
    *info = clap_param_info{};
    info->id = kGain;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    std::snprintf(info->name, sizeof(info->name), "Gain");
    info->min_value = 0.0;
    info->max_value = 4.0;
    info->default_value = 1.0;
    return true;
}

bool paramValue(const clap_plugin *plugin, clap_id id, double *value)
{
    if (id != kGain) {
        return false;
    }
    *value = self(plugin)->gain.load();
    return true;
}

bool valueToText(const clap_plugin *, clap_id id, double value, char *text, std::uint32_t size)
{
    if (id != kGain) {
        return false;
    }
    std::snprintf(text, size, "%.3f", value);
    return true;
}

bool textToValue(const clap_plugin *, clap_id id, const char *text, double *value)
{
    if (id != kGain) {
        return false;
    }
    *value = std::strtod(text, nullptr);
    return true;
}

void take(Plugin *plugin, const clap_input_events *in)
{
    for (std::uint32_t at = 0; in != nullptr && at < in->size(in); ++at) {
        const clap_event_header *event = in->get(in, at);
        if (event->space_id == CLAP_CORE_EVENT_SPACE_ID && event->type == CLAP_EVENT_PARAM_VALUE) {
            const auto *change = reinterpret_cast<const clap_event_param_value *>(event);
            if (change->param_id == kGain) {
                plugin->gain.store(static_cast<float>(change->value));
                plugin->heard.store(static_cast<float>(change->value));
            }
        }
    }
}

void flush(const clap_plugin *plugin, const clap_input_events *in, const clap_output_events *)
{
    take(self(plugin), in);
}

const clap_plugin_params g_params{paramCount, paramInfo, paramValue, valueToText, textToValue, flush};

// --- audio ports, state, latency ---

std::uint32_t portCount(const clap_plugin *, bool)
{
    return 1;
}

bool portInfo(const clap_plugin *, std::uint32_t index, bool input, clap_audio_port_info *info)
{
    if (index != 0) {
        return false;
    }
    *info = clap_audio_port_info{};
    info->id = input ? 0 : 1;
    std::snprintf(info->name, sizeof(info->name), input ? "In" : "Out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports g_ports{portCount, portInfo};

bool saveState(const clap_plugin *plugin, const clap_ostream *stream)
{
    const std::string text = "gain=" + std::to_string(self(plugin)->gain.load());
    return stream->write(stream, text.data(), text.size()) == static_cast<std::int64_t>(text.size());
}

bool loadState(const clap_plugin *plugin, const clap_istream *stream)
{
    char text[64] = {};
    const std::int64_t got = stream->read(stream, text, sizeof(text) - 1);
    if (got <= 5 || std::strncmp(text, "gain=", 5) != 0) {
        return false;
    }
    self(plugin)->gain.store(std::strtof(text + 5, nullptr));
    return true;
}

const clap_plugin_state g_state{saveState, loadState};

// --- the window's stand-in ---

void onTimer(const clap_plugin *plugin, clap_id)
{
    Plugin *me = self(plugin);
    const int tick = me->ticks++;
    if (tick == 0) {
        me->toSay.store(1);
        // Something for the descriptor to say, as a toolkit's would.
        const std::uint64_t one = 1;
        (void)::write(me->fd, &one, sizeof(one));
    } else if (tick == kHeldFor) {
        me->toSay.store(2);
    }
    if (const float heard = me->heard.load(); heard != me->logged) {
        me->logged = heard;
        char line[64];
        std::snprintf(line, sizeof(line), "gain %g", static_cast<double>(heard));
        me->note(line);
    }
    if (!me->log.empty() && ::access((me->log + ".close").c_str(), F_OK) == 0) {
        if (const auto *gui = me->hostExtension<clap_host_gui>(CLAP_EXT_GUI); gui != nullptr) {
            gui->closed(me->host, false);
        }
    }
}

const clap_plugin_timer_support g_timer{onTimer};

void onFd(const clap_plugin *plugin, int fd, clap_posix_fd_flags_t)
{
    std::uint64_t count = 0;
    if (::read(fd, &count, sizeof(count)) == sizeof(count)) {
        self(plugin)->note("fd");
    }
}

const clap_plugin_posix_fd_support g_fd{onFd};

bool apiSupported(const clap_plugin *, const char *api, bool floating)
{
    return floating && std::strcmp(api, CLAP_WINDOW_API_X11) == 0;
}

bool preferredApi(const clap_plugin *, const char **api, bool *floating)
{
    *api = CLAP_WINDOW_API_X11;
    *floating = true;
    return true;
}

bool create(const clap_plugin *plugin, const char *api, bool floating)
{
    Plugin *me = self(plugin);
    if (!apiSupported(plugin, api, floating) || me->created) {
        return false;
    }
    me->created = true;
    me->ticks = 0;
    return true;
}

void destroy(const clap_plugin *plugin)
{
    Plugin *me = self(plugin);
    if (me->timer != CLAP_INVALID_ID) {
        if (const auto *timers = me->hostExtension<clap_host_timer_support>(CLAP_EXT_TIMER_SUPPORT)) {
            (void)timers->unregister_timer(me->host, me->timer);
        }
        me->timer = CLAP_INVALID_ID;
    }
    if (me->fd >= 0) {
        if (const auto *fds = me->hostExtension<clap_host_posix_fd_support>(CLAP_EXT_POSIX_FD_SUPPORT)) {
            (void)fds->unregister_fd(me->host, me->fd);
        }
        ::close(me->fd);
        me->fd = -1;
    }
    me->created = false;
    me->note("destroyed");
}

bool setScale(const clap_plugin *, double)
{
    return false;
}

bool getSize(const clap_plugin *, std::uint32_t *width, std::uint32_t *height)
{
    *width = 320;
    *height = 200;
    return true;
}

bool canResize(const clap_plugin *)
{
    return false;
}

bool resizeHints(const clap_plugin *, clap_gui_resize_hints *)
{
    return false;
}

bool adjustSize(const clap_plugin *, std::uint32_t *, std::uint32_t *)
{
    return false;
}

bool setSize(const clap_plugin *, std::uint32_t, std::uint32_t)
{
    return false;
}

bool setParent(const clap_plugin *, const clap_window *)
{
    return false;
}

bool setTransient(const clap_plugin *, const clap_window *)
{
    return false;
}

void suggestTitle(const clap_plugin *, const char *) { }

bool show(const clap_plugin *plugin)
{
    Plugin *me = self(plugin);
    if (!me->created) {
        return false;
    }
    // **The host's loop**, as a toolkit asks for it.
    const auto *timers = me->hostExtension<clap_host_timer_support>(CLAP_EXT_TIMER_SUPPORT);
    const auto *fds = me->hostExtension<clap_host_posix_fd_support>(CLAP_EXT_POSIX_FD_SUPPORT);
    if (timers == nullptr || fds == nullptr) {
        return false;
    }
    if (me->timer == CLAP_INVALID_ID && !timers->register_timer(me->host, 10, &me->timer)) {
        return false;
    }
    if (me->fd < 0) {
        me->fd = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (me->fd < 0 || !fds->register_fd(me->host, me->fd, CLAP_POSIX_FD_READ)) {
            return false;
        }
    }
    me->shown = true;
    me->note("shown");
    return true;
}

bool hide(const clap_plugin *plugin)
{
    Plugin *me = self(plugin);
    if (me->shown) {
        me->shown = false;
        me->note("hidden");
    }
    return true;
}

const clap_plugin_gui g_gui{apiSupported,
                            preferredApi,
                            create,
                            destroy,
                            setScale,
                            getSize,
                            canResize,
                            resizeHints,
                            adjustSize,
                            setSize,
                            setParent,
                            setTransient,
                            suggestTitle,
                            show,
                            hide};

// --- presets ---

bool fromLocation(const clap_plugin *plugin, std::uint32_t kind, const char *location, const char *key)
{
    Plugin *me = self(plugin);
    float value = 0.0f;
    if (kind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN) {
        if (key == nullptr || std::strcmp(key, "half") != 0) {
            return false;
        }
        value = 0.5f;
    } else {
        std::ifstream file(location != nullptr ? location : "");
        if (!(file >> value)) {
            return false;
        }
    }
    me->gain.store(value);
    // **Said as a plugin says it**: its values moved, and which preset.
    if (const auto *params = me->hostExtension<clap_host_params>(CLAP_EXT_PARAMS)) {
        params->rescan(me->host, CLAP_PARAM_RESCAN_VALUES);
    }
    if (const auto *load = me->hostExtension<clap_host_preset_load>(CLAP_EXT_PRESET_LOAD)) {
        load->loaded(me->host, kind, location, key);
    }
    return true;
}

const clap_plugin_preset_load g_presetLoad{fromLocation};

// --- the plugin ---

bool init(const clap_plugin *plugin)
{
    if (const char *log = std::getenv("RB_TEST_CLAP_LOG"); log != nullptr) {
        self(plugin)->log = log;
    }
    return true;
}

void destroyPlugin(const clap_plugin *plugin)
{
    Plugin *me = self(plugin);
    if (me->created) {
        destroy(plugin);
    }
    delete me;
}

bool activate(const clap_plugin *, double, std::uint32_t, std::uint32_t)
{
    return true;
}

void deactivate(const clap_plugin *) { }

bool startProcessing(const clap_plugin *)
{
    return true;
}

void stopProcessing(const clap_plugin *) { }

void reset(const clap_plugin *) { }

clap_process_status process(const clap_plugin *plugin, const clap_process *data)
{
    Plugin *me = self(plugin);
    take(me, data->in_events);
    // **What the window's person did**, as a plugin says it: a value with
    // the gesture round it.
    if (const int say = me->toSay.exchange(0); say != 0 && data->out_events != nullptr) {
        const auto gesture = [&](std::uint16_t type) {
            clap_event_param_gesture event{};
            event.header = {sizeof(event), 0, CLAP_CORE_EVENT_SPACE_ID, type, 0};
            event.param_id = kGain;
            (void)data->out_events->try_push(data->out_events, &event.header);
        };
        const auto value = [&](float to) {
            me->gain.store(to);
            clap_event_param_value event{};
            event.header = {sizeof(event), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
            event.param_id = kGain;
            event.note_id = -1;
            event.port_index = -1;
            event.channel = -1;
            event.key = -1;
            event.value = to;
            (void)data->out_events->try_push(data->out_events, &event.header);
        };
        if (say == 1) {
            gesture(CLAP_EVENT_PARAM_GESTURE_BEGIN);
            value(0.25f);
        } else {
            value(0.3f);
            gesture(CLAP_EVENT_PARAM_GESTURE_END);
        }
    }
    const float gain = me->gain.load();
    if (data->audio_inputs_count < 1 || data->audio_outputs_count < 1) {
        return CLAP_PROCESS_CONTINUE;
    }
    for (std::uint32_t channel = 0; channel < 2; ++channel) {
        const float *in = data->audio_inputs[0].data32[channel];
        float *out = data->audio_outputs[0].data32[channel];
        for (std::uint32_t frame = 0; frame < data->frames_count; ++frame) {
            out[frame] = in[frame] * gain;
        }
    }
    return CLAP_PROCESS_CONTINUE;
}

const void *pluginExtension(const clap_plugin *, const char *id)
{
    const std::string_view wanted{id};
    if (wanted == CLAP_EXT_PARAMS) {
        return &g_params;
    }
    if (wanted == CLAP_EXT_AUDIO_PORTS) {
        return &g_ports;
    }
    if (wanted == CLAP_EXT_STATE) {
        return &g_state;
    }
    if (wanted == CLAP_EXT_GUI) {
        return &g_gui;
    }
    if (wanted == CLAP_EXT_TIMER_SUPPORT) {
        return &g_timer;
    }
    if (wanted == CLAP_EXT_POSIX_FD_SUPPORT) {
        return &g_fd;
    }
    if (wanted == CLAP_EXT_PRESET_LOAD) {
        return &g_presetLoad;
    }
    return nullptr;
}

void onMainThread(const clap_plugin *) { }

// --- the factories ---

std::uint32_t pluginCount(const clap_plugin_factory *)
{
    return 1;
}

const clap_plugin_descriptor *pluginDescriptor(const clap_plugin_factory *, std::uint32_t index)
{
    return index == 0 ? &g_descriptor : nullptr;
}

const clap_plugin *createPlugin(const clap_plugin_factory *, const clap_host *host, const char *id)
{
    if (std::strcmp(id, kId) != 0) {
        return nullptr;
    }
    auto *made = new (std::nothrow) Plugin;
    if (made == nullptr) {
        return nullptr;
    }
    made->host = host;
    made->plugin = clap_plugin{&g_descriptor,
                               made,
                               init,
                               destroyPlugin,
                               activate,
                               deactivate,
                               startProcessing,
                               stopProcessing,
                               reset,
                               process,
                               pluginExtension,
                               onMainThread};
    return &made->plugin;
}

const clap_plugin_factory g_factory{pluginCount, pluginDescriptor, createPlugin};

const clap_preset_discovery_provider_descriptor g_provider{CLAP_VERSION,
                                                           "org.reverblabr.test.gain.presets",
                                                           "The tests' presets",
                                                           "Reverblabr"};

struct Provider {
    clap_preset_discovery_provider provider{};
    const clap_preset_discovery_indexer *indexer = nullptr;
};

bool providerInit(const clap_preset_discovery_provider *provider)
{
    const auto *indexer = static_cast<Provider *>(provider->provider_data)->indexer;
    const clap_preset_discovery_filetype type{"Test gain", "", "rbgain"};
    (void)indexer->declare_filetype(indexer, &type);
    const clap_preset_discovery_location own{
        CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT, "Built in", CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr};
    (void)indexer->declare_location(indexer, &own);
    if (const char *folder = std::getenv("RB_TEST_CLAP_PRESETS"); folder != nullptr) {
        const clap_preset_discovery_location files{
            CLAP_PRESET_DISCOVERY_IS_USER_CONTENT, "Files", CLAP_PRESET_DISCOVERY_LOCATION_FILE, folder};
        (void)indexer->declare_location(indexer, &files);
    }
    return true;
}

void providerDestroy(const clap_preset_discovery_provider *provider)
{
    delete static_cast<Provider *>(provider->provider_data);
}

bool metadata(const clap_preset_discovery_provider *,
              std::uint32_t kind,
              const char *location,
              const clap_preset_discovery_metadata_receiver *receiver)
{
    const clap_universal_plugin_id ours{"clap", kId};
    if (kind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN) {
        if (receiver->begin_preset(receiver, "Half", "half")) {
            receiver->add_plugin_id(receiver, &ours);
        }
        return true;
    }
    std::string name = location != nullptr ? location : "";
    name = name.substr(name.find_last_of('/') + 1);
    name = name.substr(0, name.find_last_of('.'));
    if (receiver->begin_preset(receiver, name.c_str(), nullptr)) {
        receiver->add_plugin_id(receiver, &ours);
    }
    return true;
}

const void *providerExtension(const clap_preset_discovery_provider *, const char *)
{
    return nullptr;
}

std::uint32_t providerCount(const clap_preset_discovery_factory *)
{
    return 1;
}

const clap_preset_discovery_provider_descriptor *providerDescriptor(const clap_preset_discovery_factory *,
                                                                    std::uint32_t index)
{
    return index == 0 ? &g_provider : nullptr;
}

const clap_preset_discovery_provider *
createProvider(const clap_preset_discovery_factory *, const clap_preset_discovery_indexer *indexer, const char *id)
{
    if (std::strcmp(id, g_provider.id) != 0) {
        return nullptr;
    }
    auto *made = new (std::nothrow) Provider;
    if (made == nullptr) {
        return nullptr;
    }
    made->indexer = indexer;
    made->provider =
        clap_preset_discovery_provider{&g_provider, made, providerInit, providerDestroy, metadata, providerExtension};
    return &made->provider;
}

const clap_preset_discovery_factory g_presetFactory{providerCount, providerDescriptor, createProvider};

bool entryInit(const char *)
{
    return true;
}

void entryDeinit() { }

const void *entryFactory(const char *id)
{
    if (std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) == 0) {
        return &g_factory;
    }
    if (std::strcmp(id, CLAP_PRESET_DISCOVERY_FACTORY_ID) == 0) {
        return &g_presetFactory;
    }
    return nullptr;
}

} // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry clap_entry{CLAP_VERSION, entryInit, entryDeinit, entryFactory};
